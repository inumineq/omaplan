#include "actions.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include "config.h"
#include "dateannotate.h"
#include "engine.h"
#include "prompt.h"
#include "proposal.h"
#include "translator.h"


static QJsonObject changeToJson(const Store::Change &change) {
    QJsonObject json;
    json["id"] = change.id;
    json["at"] = change.at.toString(Qt::ISODateWithMs);
    json["source"] = change.source;
    json["kind"] = change.kind;
    json["task_id"] = change.taskId;
    json["before"] = change.hasBefore ? QJsonValue(change.before.toJson()) : QJsonValue(QJsonValue::Null);
    json["after"] = change.hasAfter ? QJsonValue(change.after.toJson()) : QJsonValue(QJsonValue::Null);
    return json;
}

static QString startText(const Task &task) {
    if (!task.start.isValid())
        return QStringLiteral("-");
    return task.startHasTime ? task.start.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                             : task.start.date().toString(Qt::ISODate);
}

// effectiveDate() lives in task.h so models and actions share it.

ActionResult Actions::create(const Task &task, const QString &source) {
    QString error;
    const QString id = m_store->createTask(task, source, &error);
    if (id.isEmpty())
        return {false, error, {}, {}};
    Task stored;
    m_store->task(id, &stored, &error);
    QJsonObject payload;
    payload["task"] = stored.toJson();
    emit planChanged();
    return {true, {}, tr("Created %1: %2").arg(id, stored.title), payload};
}

ActionResult Actions::update(const QString &id, const QJsonObject &patch, const QString &source) {
    QString error;
    Task current;
    if (!m_store->task(id, &current, &error))
        return {false, error, {}, {}};

    // Merge the patch over the current JSON, then re-validate the whole task.
    QJsonObject json = current.toJson();
    for (auto it = patch.begin(); it != patch.end(); ++it)
        json[it.key()] = it.value();
    // Only known fields may be patched; a typo ("tilte") is an error, not a
    // silent no-op.
    for (auto it = patch.begin(); it != patch.end(); ++it)
        if (!current.toJson().contains(it.key()) || it.key() == QLatin1String("id")
            || it.key() == QLatin1String("created") || it.key() == QLatin1String("modified"))
            return {false, tr("Unknown or read-only field: %1").arg(it.key()), {}, {}};
    bool ok = false;
    const Task patched = Task::fromJson(json, &ok);
    if (!ok) {
        const QString why = patched.validate();
        return {false, why.isEmpty() ? tr("Invalid status or date in: %1").arg(
                                           QString::fromUtf8(QJsonDocument(patch).toJson(QJsonDocument::Compact)))
                                     : why, {}, {}};
    }
    if (!m_store->updateTask(patched, source, &error))
        return {false, error, {}, {}};
    QJsonObject payload;
    payload["task"] = patched.toJson();
    emit planChanged();
    return {true, {}, tr("Updated %1: %2").arg(id, patched.title), payload};
}

ActionResult Actions::move(const QString &id, const QDateTime &when, bool hasTime, const QString &source) {
    QString error;
    Task task;
    if (!m_store->task(id, &task, &error))
        return {false, error, {}, {}};
    task.start = when;
    task.startHasTime = hasTime;
    if (!m_store->updateTask(task, source, &error))
        return {false, error, {}, {}};
    QJsonObject payload;
    payload["task"] = task.toJson();
    emit planChanged();
    const QString whenText = hasTime ? when.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                     : when.date().toString(Qt::ISODate);
    return {true, {}, tr("Moved %1 to %2").arg(id, whenText), payload};
}

ActionResult Actions::setDone(const QString &id, bool done, const QString &source) {
    QString error;
    Task task;
    if (!m_store->task(id, &task, &error))
        return {false, error, {}, {}};
    task.status = done ? Task::Status::Done : Task::Status::Todo;
    if (!m_store->updateTask(task, source, &error))
        return {false, error, {}, {}};
    QJsonObject payload;
    payload["task"] = task.toJson();
    emit planChanged();
    return {true, {}, done ? tr("Marked done: %1").arg(task.title)
                           : tr("Marked undone: %1").arg(task.title), payload};
}

ActionResult Actions::remove(const QString &id, const QString &source) {
    QString error;
    Task task;
    if (!m_store->task(id, &task, &error))
        return {false, error, {}, {}};
    if (!m_store->removeTask(id, source, &error))
        return {false, error, {}, {}};
    QJsonObject payload;
    payload["id"] = id;
    emit planChanged();
    return {true, {}, tr("Deleted %1").arg(task.title), payload};
}

// Applies the inverse of one logged change and logs it. Which side of the
// change is set decides the inverse: after-only was a create (delete it),
// before-only a delete (re-insert it), both an update (restore before). The
// same rule re-applies an undo entry for redo, since an undo entry's sides
// are already the inverse of the original.
static bool invertChange(Store *store, const Store::Change &change, const QString &source, const QString &kind,
                         qlonglong undoOf, qlonglong *loggedId, QString *error) {
    bool ok = false;
    Task before, after;
    bool hasBefore = false, hasAfter = false;
    if (change.hasAfter && !change.hasBefore) {
        ok = store->deleteTask(change.after.id, error);
        before = change.after;
        hasBefore = true;
    } else if (change.hasBefore && !change.hasAfter) {
        ok = store->insertTask(change.before, error);
        after = change.before;
        hasAfter = true;
    } else if (change.hasBefore && change.hasAfter) {
        ok = store->replaceTask(change.before, error);
        before = change.after;
        after = change.before;
        hasBefore = hasAfter = true;
    } else {
        *error = QCoreApplication::translate("Actions", "Change %1 cannot be undone").arg(change.id);
    }
    if (!ok)
        return false;
    *loggedId = store->logChange(source, kind, change.taskId, hasBefore ? &before : nullptr,
                                 hasAfter ? &after : nullptr, undoOf);
    if (*loggedId <= 0) {
        *error = QCoreApplication::translate("Actions", "Could not write the change log");
        return false;
    }
    return true;
}

ActionResult Actions::undo(const QString &source) {
    QString error;
    // Everything happens inside one IMMEDIATE transaction, so two processes
    // undoing at the same moment cannot invert the same change twice.
    if (!m_store->begin(&error))
        return {false, error, {}, {}};
    QList<Store::Change> targets;
    if (!m_store->undoTargets(&targets, &error)) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    m_store->setBatch(targets.size() > 1 ? Task::newId() : QString());
    for (const Store::Change &target : targets) {
        qlonglong logged = 0;
        if (!invertChange(m_store, target, source, QStringLiteral("undo"), target.id, &logged, &error)) {
            m_store->setBatch({});
            m_store->rollback();
            return {false, error, {}, {}};
        }
    }
    m_store->setBatch({});
    if (!m_store->commit(&error))
        return {false, error, {}, {}};
    emit planChanged();
    QJsonObject payload;
    payload["change"] = changeToJson(targets.first());
    payload["count"] = targets.size();
    const QString text = targets.size() == 1
            ? tr("Undone %1 on task %2").arg(targets.first().kind, targets.first().taskId)
            : tr("Undone %n changes", nullptr, targets.size());
    return {true, {}, text, payload};
}

ActionResult Actions::redo(const QString &source) {
    QString error;
    if (!m_store->begin(&error))
        return {false, error, {}, {}};
    QList<Store::Change> targets;
    if (!m_store->redoTargets(&targets, &error)) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    m_store->setBatch(targets.size() > 1 ? Task::newId() : QString());
    for (const Store::Change &target : targets) {
        qlonglong redoId = 0;
        if (!invertChange(m_store, target, source, QStringLiteral("redo"), 0, &redoId, &error)
            || !m_store->markRedone(target.id, redoId, &error)) {
            m_store->setBatch({});
            m_store->rollback();
            return {false, error, {}, {}};
        }
    }
    m_store->setBatch({});
    if (!m_store->commit(&error))
        return {false, error, {}, {}};
    emit planChanged();
    QJsonObject payload;
    payload["change"] = changeToJson(targets.first());
    payload["count"] = targets.size();
    const QString text = targets.size() == 1
            ? tr("Redone %1 on task %2").arg(targets.first().kind, targets.first().taskId)
            : tr("Redone %n changes", nullptr, targets.size());
    return {true, {}, text, payload};
}

static QJsonObject wrapResult(const ActionResult &result) {
    QJsonObject json;
    json["ok"] = result.ok;
    json["error"] = result.error;
    json["text"] = result.text;
    json["payload"] = result.payload;
    return json;
}

QJsonObject Actions::guiUpdate(const QString &id, const QVariantMap &patch) {
    return wrapResult(update(id, QJsonObject::fromVariantMap(patch), QStringLiteral("gui")));
}

QJsonObject Actions::guiMove(const QString &id, const QDateTime &when, bool hasTime) {
    return wrapResult(move(id, when, hasTime, QStringLiteral("gui")));
}

QJsonObject Actions::guiSetDone(const QString &id, bool done) {
    return wrapResult(setDone(id, done, QStringLiteral("gui")));
}

QJsonObject Actions::guiRemove(const QString &id) {
    return wrapResult(remove(id, QStringLiteral("gui")));
}

QJsonObject Actions::guiUndo() {
    return wrapResult(undo(QStringLiteral("gui")));
}

QJsonObject Actions::guiRedo() {
    return wrapResult(redo(QStringLiteral("gui")));
}

QJsonObject Actions::guiProjects() {
    return wrapResult(projects());
}

QJsonObject Actions::guiTask(const QString &id) {
    return wrapResult(show(id));
}

static Actions::Fields fieldsFromVariant(const QVariantMap &map) {
    Actions::Fields fields;
    for (auto it = map.begin(); it != map.end(); ++it)
        fields.insert(it.key(), it.value().toString());
    return fields;
}

QJsonObject Actions::guiEdit(const QString &id, const QVariantMap &fields) {
    QJsonObject patch;
    QString error;
    if (!patchFromFields(fieldsFromVariant(fields), &patch, &error))
        return wrapResult({false, error, {}, {}});
    return wrapResult(update(id, patch, QStringLiteral("gui")));
}

QJsonObject Actions::guiNew(const QVariantMap &fields) {
    Task task;
    QString error;
    if (!taskFromFields(fieldsFromVariant(fields), &task, &error))
        return wrapResult({false, error, {}, {}});
    return wrapResult(create(task, QStringLiteral("gui")));
}

QJsonObject Actions::guiParseDate(const QString &text) const {
    QDateTime when;
    bool hasTime = false;
    QString error;
    const bool ok = parseDateTime(text, QDate::currentDate(), &when, &hasTime, &error);
    return {{QStringLiteral("ok"), ok},
            {QStringLiteral("error"), error},
            {QStringLiteral("date"), ok ? when.toString(QStringLiteral("yyyy-MM-ddTHH:mm")) : QString()},
            {QStringLiteral("hasTime"), hasTime}};
}

// ---------------------------------------------------------------------------
// AI proposals

// Maps the schema's guessed-field names to Task's guess keys; only the
// duration differs.
static QString guessKeyFromSchema(const QString &schemaField) {
    return schemaField == QLatin1String("duration_min") ? QStringLiteral("duration_minutes") : schemaField;
}

static QStringList existingTaskLines(Store *store) {
    QString error;
    QStringList lines;
    // Only what helps the engine place new tasks: open tasks from a week
    // back to two months ahead. The whole history would grow the prompt
    // (and its cost and latency) without bound.
    const QDate today = QDateTime::currentDateTime().date();
    for (const Task &task : store->tasks(&error)) {
        const QDate date = effectiveDate(task);
        if (task.status == Task::Status::Done || date < today.addDays(-7) || date > today.addDays(60))
            continue;
        QString line = task.title;
        if (task.start.isValid())
            line += QStringLiteral(" | %1").arg(task.startHasTime
                                                   ? task.start.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                                   : task.start.date().toString(Qt::ISODate));
        if (!task.project.isEmpty())
            line += QStringLiteral(" | %1").arg(task.project);
        line += QStringLiteral(" | %1").arg(Task::statusToString(task.status));
        lines << line;
    }
    return lines;
}

ActionResult Actions::runEngine(const QString &rawText, const QString &motorId,
                                const QString &modelOverride, const QString &endpointOverride,
                                QString *captureIdOut, bool wait) {
    QString error;
    const QString motor = motorId.isEmpty() ? AppConfig::load().engine : motorId;
    auto engine = createEngine(motor, modelOverride, endpointOverride, &error);
    if (!engine)
        return {false, error, {}, {}};
    QString whyNot;
    if (!engine->available(&whyNot))
        return {false, whyNot, {}, {}};

    // Deterministic date annotation before the call; both variants are kept
    // in `captures` so a proposal stays traceable to the raw text.
    const QDate today = QDateTime::currentDateTime().date();
    const QString annotated = annotateDates(rawText, today);

    PromptContext context;
    context.today = today;
    context.language = Translator::readLanguage(QDir::homePath() + QStringLiteral("/.config/omarchy/language"));
    context.existingTasks = existingTaskLines(m_store);

    m_engine = std::move(engine);
    const QString engineId = m_engine->id();
    const QString model = m_engine->modelInfo().isEmpty() ? m_engine->displayName() : m_engine->modelInfo();

    Proposal proposal;
    QString rawResponse;
    QString failure;
    bool done = false;
    QEventLoop loop;
    connect(m_engine.get(), &AiEngine::finished, this, [&](const Proposal &p, const QString &raw) {
        proposal = p;
        rawResponse = raw;
        done = true;
        if (wait)
            loop.quit();
    });
    connect(m_engine.get(), &AiEngine::failed, this, [&](const QString &message) {
        failure = message;
        done = true;
        if (wait)
            loop.quit();
    });
    m_engine->propose(annotated, context);
    if (wait && !done)
        loop.exec();
    if (!done) // async (GUI) path: signals carry the result later
        return {true, {}, {}, {}};

    if (!failure.isEmpty()) {
        m_engine.reset();
        return {false, failure, {}, {}};
    }

    const QString captureId = m_store->addCapture(rawText, annotated, engineId, model, rawResponse, &error);
    if (captureId.isEmpty()) {
        m_engine.reset();
        return {false, error, {}, {}};
    }
    if (captureIdOut)
        *captureIdOut = captureId;

    QJsonObject payload;
    payload["proposal"] = proposal.toJson();
    payload["capture_id"] = captureId;
    payload["engine"] = engineId;
    payload["model"] = model;
    QStringList lines;
    for (const ProposedTask &task : proposal.tasks)
        lines << task.title;
    m_engine.reset();
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

ActionResult Actions::propose(const QString &rawText, const QString &motorId,
                              const QString &modelOverride, const QString &endpointOverride,
                              QString *captureIdOut) {
    return runEngine(rawText, motorId, modelOverride, endpointOverride, captureIdOut, true);
}

ActionResult Actions::approve(const QJsonObject &proposalJson, const QString &captureId, const QString &source) {
    // Build and validate every task before writing any, so a bad field in
    // the third task never leaves the first two stored on their own.
    // A proposal saved before 0.1 may still carry the Danish keys.
    const QJsonArray tasks = withEnglishKeys(proposalJson).value(QStringLiteral("tasks")).toArray();
    const QDate today = QDateTime::currentDateTime().date();
    QList<Task> built;
    for (const QJsonValue &value : tasks) {
        const QJsonObject o = value.toObject();
        Task task;
        task.id = Task::newId();
        task.title = o.value(QStringLiteral("title")).toString().trimmed();
        task.notes = o.value(QStringLiteral("notes")).toString();
        task.project = o.value(QStringLiteral("project")).toString().trimmed();
        task.priority = o.value(QStringLiteral("priority")).toVariant().toInt();
        if (task.priority == 0)
            task.priority = 3;
        // Dates go through the same parser as the CLI: the engine's ISO
        // values, and whatever the user typed over them in the proposal
        // ("friday 10"). An unreadable date is an error, never a silent drop.
        const auto readDate = [&](const char *key, QDateTime *when, bool *hasTime, QString *error) {
            const QString text = o.value(QLatin1String(key)).toString().trimmed();
            if (text.isEmpty())
                return true;
            QString why;
            if (!parseDateTime(text, today, when, hasTime, &why)) {
                *error = tr("%1: %2").arg(task.title.isEmpty() ? QStringLiteral("?") : task.title, why);
                return false;
            }
            return true;
        };
        QString error;
        if (!readDate("start", &task.start, &task.startHasTime, &error)
            || !readDate("deadline", &task.deadline, &task.deadlineHasTime, &error))
            return {false, error, {}, {}};
        task.durationMinutes = o.value(QStringLiteral("duration_min")).toVariant().toInt();
        const QJsonArray guessed = o.value(QStringLiteral("guessed")).toArray();
        for (const QJsonValue &g : guessed)
            task.guesses.append(guessKeyFromSchema(g.toString()));
        task.captureId = captureId;
        const QString invalid = task.validate();
        if (!invalid.isEmpty())
            return {false, tr("%1: %2").arg(task.title.isEmpty() ? QStringLiteral("?") : task.title, invalid), {}, {}};
        built.append(task);
    }
    if (built.isEmpty())
        return {false, tr("The proposal has no tasks"), {}, {}};

    // One transaction and one batch: all tasks or none, and a single undo
    // takes the whole approval back.
    QString error;
    if (!m_store->begin(&error))
        return {false, error, {}, {}};
    m_store->setBatch(built.size() > 1 ? Task::newId() : QString());
    QJsonArray stored;
    QStringList lines;
    for (const Task &task : built) {
        const QString id = m_store->createTask(task, source, &error);
        if (id.isEmpty()) {
            m_store->setBatch({});
            m_store->rollback();
            return {false, error, {}, {}};
        }
        stored.append(id);
        lines << tr("Created %1: %2").arg(id, task.title);
    }
    m_store->setBatch({});
    if (!m_store->commit(&error))
        return {false, error, {}, {}};
    QJsonObject payload;
    payload["stored"] = stored;
    payload["count"] = stored.size();
    emit planChanged();
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

std::unique_ptr<AiEngine> Actions::startGuiEngine(const QString &motorId, const QString &text,
                                                   std::function<void(const QJsonObject &, const QString &)> onReady,
                                                   std::function<void(const QString &)> onFailed) {
    QString error;
    const QString motor = motorId.isEmpty() ? AppConfig::load().engine : motorId;
    auto engine = createEngine(motor, QString(), QString(), &error);
    if (!engine) {
        onFailed(error);
        return nullptr;
    }
    QString whyNot;
    if (!engine->available(&whyNot)) {
        onFailed(whyNot);
        return nullptr;
    }
    const QDate today = QDateTime::currentDateTime().date();
    const QString annotated = annotateDates(text, today);
    PromptContext context;
    context.today = today;
    context.language = Translator::readLanguage(QDir::homePath() + QStringLiteral("/.config/omarchy/language"));
    context.existingTasks = existingTaskLines(m_store);

    const QString engineId = engine->id();
    const QString model = engine->modelInfo().isEmpty() ? engine->displayName() : engine->modelInfo();
    connect(engine.get(), &AiEngine::finished, this,
            [this, text, annotated, engineId, model, onReady](const Proposal &p, const QString &raw) {
                QString error;
                const QString captureId = m_store->addCapture(text, annotated, engineId, model, raw, &error);
                onReady(p.toJson(), captureId);
            });
    connect(engine.get(), &AiEngine::failed, this, [onFailed](const QString &message) { onFailed(message); });
    engine->propose(annotated, context);
    return engine;
}

void Actions::guiPropose(const QString &text, const QString &motorId) {
    guiCancelProposal();
    m_engine = startGuiEngine(
        motorId, text, [this](const QJsonObject &proposal, const QString &captureId) { emit proposalReady(proposal, captureId); },
        [this](const QString &message) { emit proposalFailed(message); });
}

void Actions::guiCompare(const QString &text) {
    guiCancelProposal();
    const QStringList ids = AppConfig::load().compare;
    for (int side = 0; side < 2; ++side) {
        const QString id = ids.value(side);
        QString name = id;
        QString ignored;
        if (auto probe = createEngine(id, {}, {}, &ignored))
            name = probe->displayName();
        m_compare[side] = startGuiEngine(
            id, text,
            [this, side, name](const QJsonObject &proposal, const QString &captureId) {
                emit compareReady(side, proposal, captureId, name);
            },
            [this, side, name](const QString &message) { emit compareFailed(side, message, name); });
    }
}

QJsonObject Actions::guiEngineNames() const {
    const AppConfig config = AppConfig::load();
    const auto nameOf = [](const QString &id) {
        QString ignored;
        auto engine = createEngine(id, {}, {}, &ignored);
        return engine ? engine->displayName() : id;
    };
    return {{QStringLiteral("default"), nameOf(config.engine)},
            {QStringLiteral("compare"), QJsonArray{nameOf(config.compare.value(0)), nameOf(config.compare.value(1))}}};
}

void Actions::guiCancelProposal() {
    // Disconnect first: a cancelled run is the user's choice, not an error
    // to show.
    for (auto *engine : {m_engine.get(), m_compare[0].get(), m_compare[1].get()}) {
        if (engine && engine->isRunning()) {
            engine->disconnect(this);
            engine->cancel();
        }
    }
}

QJsonObject Actions::guiApprove(const QJsonObject &proposalJson, const QString &captureId) {
    return wrapResult(approve(proposalJson, captureId, QStringLiteral("gui")));
}

ActionResult Actions::list(const ListFilter &filter) {
    QString error;
    const QList<Task> all = m_store->tasks(&error);
    if (!error.isEmpty())
        return {false, error, {}, {}};

    QJsonArray array;
    QStringList lines;
    for (const Task &task : all) {
        if (!filter.project.isEmpty() && task.project != filter.project)
            continue;
        if (!filter.status.isEmpty() && Task::statusToString(task.status) != filter.status)
            continue;
        const QDate date = effectiveDate(task);
        if (filter.hasFrom && date < filter.from)
            continue;
        if (filter.hasTo && date > filter.to)
            continue;
        array.append(task.toJson());
        lines << QStringLiteral("%1  %2  %3  %4")
                     .arg(task.id, startText(task), Task::statusToString(task.status), task.title);
    }
    QJsonObject payload;
    payload["tasks"] = array;
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

ActionResult Actions::show(const QString &id) {
    QString error;
    Task task;
    if (!m_store->task(id, &task, &error))
        return {false, error, {}, {}};
    QJsonObject payload;
    payload["task"] = task.toJson();

    QStringList lines;
    lines << task.title;
    lines << tr("id: %1").arg(task.id);
    if (!task.project.isEmpty())
        lines << tr("project: %1").arg(task.project);
    if (!task.notes.isEmpty())
        lines << tr("notes: %1").arg(task.notes);
    lines << tr("priority: %1").arg(task.priority);
    lines << tr("status: %1").arg(Task::statusToString(task.status));
    if (task.start.isValid())
        lines << tr("start: %1").arg(startText(task));
    if (task.durationMinutes > 0)
        lines << tr("duration: %1 min").arg(task.durationMinutes);
    if (task.deadline.isValid())
        lines << tr("deadline: %1").arg(task.deadlineHasTime
                                             ? task.deadline.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                             : task.deadline.date().toString(Qt::ISODate));
    if (!task.guesses.isEmpty())
        lines << tr("guessed fields: %1").arg(task.guesses.join(QLatin1String(", ")));
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

ActionResult Actions::projects() {
    QString error;
    const QList<Task> all = m_store->tasks(&error);
    if (!error.isEmpty())
        return {false, error, {}, {}};
    QHash<QString, int> counts;
    for (const Task &task : all)
        if (!task.project.isEmpty())
            counts[task.project] += 1;
    QStringList names = counts.keys();
    names.sort(Qt::CaseInsensitive);
    QJsonArray array;
    QStringList lines;
    for (const QString &name : names) {
        QJsonObject entry;
        entry["name"] = name;
        entry["count"] = counts[name];
        array.append(entry);
        lines << QStringLiteral("%1 (%2)").arg(name, tr("%n tasks", nullptr, counts[name]));
    }
    QJsonObject payload;
    payload["projects"] = array;
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

ActionResult Actions::summary(bool week) {
    const QDate today = QDateTime::currentDateTime().date();
    QDate from, to;
    if (week) {
        from = today.addDays(1 - today.dayOfWeek()); // Monday, the Danish week start
        to = from.addDays(6);
    } else {
        from = QDate(today.year(), today.month(), 1);
        to = from.addMonths(1).addDays(-1);
    }

    QString error;
    const QList<Task> all = m_store->tasks(&error);
    if (!error.isEmpty())
        return {false, error, {}, {}};

    int total = 0, todo = 0, done = 0, overdue = 0;
    QHash<QString, int> perProject;
    for (const Task &task : all) {
        // Overdue counts every open task past due, also from earlier
        // periods: that is exactly what an agent asking for context needs.
        if (isOverdue(task, today))
            overdue += 1;
        const QDate date = effectiveDate(task);
        if (date < from || date > to)
            continue;
        total += 1;
        if (task.status == Task::Status::Done)
            done += 1;
        else
            todo += 1;
        if (!task.project.isEmpty())
            perProject[task.project] += 1;
    }

    QJsonObject projectsJson;
    for (auto it = perProject.begin(); it != perProject.end(); ++it)
        projectsJson[it.key()] = it.value();
    QJsonObject summaryJson;
    summaryJson["from"] = from.toString(Qt::ISODate);
    summaryJson["to"] = to.toString(Qt::ISODate);
    summaryJson["tasks"] = total;
    summaryJson["todo"] = todo;
    summaryJson["done"] = done;
    summaryJson["overdue"] = overdue;
    summaryJson["projects"] = projectsJson;
    QJsonObject payload;
    payload["summary"] = summaryJson;

    QStringList lines;
    lines << tr("Period: %1 – %2").arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate));
    lines << tr("Tasks: %1 (%2 todo, %3 done)").arg(total).arg(todo).arg(done);
    lines << tr("Overdue: %1").arg(overdue);
    QStringList projectNames = perProject.keys();
    projectNames.sort(Qt::CaseInsensitive);
    for (const QString &name : projectNames)
        lines << QStringLiteral("%1: %2").arg(name, tr("%n tasks", nullptr, perProject[name]));
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

// ---------------------------------------------------------------------------
// Fields from the CLI and MCP

static bool dateField(const QString &key, const QString &text, QDateTime *when, bool *hasTime, QString *error) {
    *when = QDateTime();
    *hasTime = false;
    if (text.trimmed().isEmpty()) // an empty value clears the field
        return true;
    QString why;
    if (!Actions::parseDateTime(text, QDate::currentDate(), when, hasTime, &why)) {
        *error = QCoreApplication::translate("Actions", "%1: %2").arg(key, why);
        return false;
    }
    return true;
}

// A number field must be a whole number; "1t" or "høj" is an error instead
// of silently becoming 0.
static bool intField(const Actions::Fields &fields, const QString &key, int *out, QString *error) {
    if (!fields.contains(key))
        return true;
    bool ok = false;
    const int value = fields.value(key).trimmed().toInt(&ok);
    if (!ok) {
        *error = QCoreApplication::translate("Actions", "%1 must be a whole number, got \"%2\"")
                     .arg(key, fields.value(key));
        return false;
    }
    *out = value;
    return true;
}

static const QStringList &knownFields() {
    static const QStringList keys = {
        QStringLiteral("title"), QStringLiteral("notes"), QStringLiteral("project"), QStringLiteral("priority"),
        QStringLiteral("start"), QStringLiteral("duration"), QStringLiteral("deadline"), QStringLiteral("status")};
    return keys;
}

const QHash<QString, QString> &Actions::legacyFieldNames() {
    static const QHash<QString, QString> names = {
        {QStringLiteral("titel"), QStringLiteral("title")},       {QStringLiteral("noter"), QStringLiteral("notes")},
        {QStringLiteral("projekt"), QStringLiteral("project")},   {QStringLiteral("prioritet"), QStringLiteral("priority")},
        {QStringLiteral("varighed"), QStringLiteral("duration")}, {QStringLiteral("frist"), QStringLiteral("deadline")},
    };
    return names;
}

// Renames the old Danish field names, then rejects anything still unknown.
static bool englishFields(const Actions::Fields &in, Actions::Fields *out, QString *error) {
    out->clear();
    for (auto it = in.begin(); it != in.end(); ++it) {
        const QString key = Actions::legacyFieldNames().value(it.key(), it.key());
        if (!knownFields().contains(key)) {
            *error = QCoreApplication::translate("Actions", "Unknown field: %1 (known: %2)")
                         .arg(it.key(), knownFields().join(QStringLiteral(", ")));
            return false;
        }
        out->insert(key, it.value());
    }
    return true;
}

bool Actions::taskFromFields(const Fields &given, Task *task, QString *error) {
    Fields fields;
    if (!englishFields(given, &fields, error))
        return false;
    if (fields.value(QStringLiteral("title")).trimmed().isEmpty()) {
        *error = QCoreApplication::translate("Actions", "Missing title");
        return false;
    }
    task->title = fields.value(QStringLiteral("title")).trimmed();
    task->notes = fields.value(QStringLiteral("notes"));
    task->project = fields.value(QStringLiteral("project")).trimmed();
    if (!intField(fields, QStringLiteral("priority"), &task->priority, error)
        || !intField(fields, QStringLiteral("duration"), &task->durationMinutes, error))
        return false;
    if (fields.contains(QStringLiteral("status"))) {
        bool ok = false;
        task->status = Task::statusFromString(fields.value(QStringLiteral("status")), &ok);
        if (!ok) {
            *error = QCoreApplication::translate("Actions", "status must be todo or done");
            return false;
        }
    }
    return dateField(QStringLiteral("start"), fields.value(QStringLiteral("start")), &task->start,
                     &task->startHasTime, error)
           && dateField(QStringLiteral("deadline"), fields.value(QStringLiteral("deadline")), &task->deadline,
                        &task->deadlineHasTime, error);
}

bool Actions::patchFromFields(const Fields &given, QJsonObject *patch, QString *error) {
    Fields fields;
    if (!englishFields(given, &fields, error))
        return false;
    if (fields.contains(QStringLiteral("title"))) (*patch)["title"] = fields.value(QStringLiteral("title"));
    if (fields.contains(QStringLiteral("notes"))) (*patch)["notes"] = fields.value(QStringLiteral("notes"));
    if (fields.contains(QStringLiteral("project"))) (*patch)["project"] = fields.value(QStringLiteral("project")).trimmed();
    if (fields.contains(QStringLiteral("status"))) (*patch)["status"] = fields.value(QStringLiteral("status"));
    int number = 0;
    if (fields.contains(QStringLiteral("priority"))) {
        if (!intField(fields, QStringLiteral("priority"), &number, error))
            return false;
        (*patch)["priority"] = number;
    }
    if (fields.contains(QStringLiteral("duration"))) {
        if (!intField(fields, QStringLiteral("duration"), &number, error))
            return false;
        (*patch)["duration_minutes"] = number;
    }
    const auto setDate = [&](const QString &field, const QString &key) {
        if (!fields.contains(field))
            return true;
        QDateTime when;
        bool hasTime = false;
        if (!dateField(field, fields.value(field), &when, &hasTime, error))
            return false;
        (*patch)[key] = !when.isValid() ? QString()
                        : hasTime      ? when.toString(QStringLiteral("yyyy-MM-ddTHH:mm"))
                                       : when.date().toString(Qt::ISODate);
        (*patch)[key + QStringLiteral("_has_time")] = hasTime;
        return true;
    };
    return setDate(QStringLiteral("start"), QStringLiteral("start"))
           && setDate(QStringLiteral("deadline"), QStringLiteral("deadline"));
}

// ---------------------------------------------------------------------------
// Date parsing

static const QHash<QString, int> &weekdays() {
    static const QHash<QString, int> map = {
        {QStringLiteral("mandag"), 1}, {QStringLiteral("tirsdag"), 2}, {QStringLiteral("onsdag"), 3},
        {QStringLiteral("torsdag"), 4}, {QStringLiteral("fredag"), 5}, {QStringLiteral("lørdag"), 6},
        {QStringLiteral("søndag"), 7},
        {QStringLiteral("monday"), 1}, {QStringLiteral("tuesday"), 2}, {QStringLiteral("wednesday"), 3},
        {QStringLiteral("thursday"), 4}, {QStringLiteral("friday"), 5}, {QStringLiteral("saturday"), 6},
        {QStringLiteral("sunday"), 7},
    };
    return map;
}

// First occurrence of `day` (1-31) in `base`'s month or a later one, never
// before `base`. A day the current month lacks rolls into a later month
// instead of being clamped into it ("30" in February = March 30).
static bool dayOfMonthDate(const QDate &base, int day, QDate *out) {
    if (day < 1 || day > 31)
        return false;
    for (int i = 0; i < 12; ++i) {
        const QDate first = QDate(base.year(), base.month(), 1).addMonths(i);
        if (day <= first.daysInMonth()) {
            const QDate candidate = first.addDays(day - 1);
            if (candidate >= base) {
                *out = candidate;
                return true;
            }
        }
    }
    return false;
}

static const QHash<QString, int> &monthNames() {
    static const QHash<QString, int> map = {
        {QStringLiteral("januar"), 1}, {QStringLiteral("februar"), 2}, {QStringLiteral("marts"), 3},
        {QStringLiteral("april"), 4}, {QStringLiteral("maj"), 5}, {QStringLiteral("juni"), 6},
        {QStringLiteral("juli"), 7}, {QStringLiteral("august"), 8}, {QStringLiteral("september"), 9},
        {QStringLiteral("oktober"), 10}, {QStringLiteral("november"), 11}, {QStringLiteral("december"), 12},
        {QStringLiteral("january"), 1}, {QStringLiteral("february"), 2}, {QStringLiteral("march"), 3},
        {QStringLiteral("april"), 4}, {QStringLiteral("may"), 5}, {QStringLiteral("june"), 6},
        {QStringLiteral("july"), 7}, {QStringLiteral("august"), 8}, {QStringLiteral("september"), 9},
        {QStringLiteral("october"), 10}, {QStringLiteral("november"), 11}, {QStringLiteral("december"), 12},
    };
    return map;
}

bool Actions::parseDateTime(const QString &text, const QDate &today, QDateTime *out, bool *hasTime,
                            QString *error) {
    if (hasTime)
        *hasTime = false;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QCoreApplication::translate("Actions", "empty date");
        return false;
    }

    // ISO first: 2026-12-01, 2026-12-01 14:30, 2026-12-01T14:30:00
    static const QRegularExpression iso(
        QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})(?:[T ](\\d{2}):(\\d{2})(?::(\\d{2}))?)?$"));
    const auto isoMatch = iso.match(trimmed);
    if (isoMatch.hasMatch()) {
        const QDate date(isoMatch.captured(1).toInt(), isoMatch.captured(2).toInt(), isoMatch.captured(3).toInt());
        if (!date.isValid()) {
            if (error) *error = QCoreApplication::translate("Actions", "invalid date: %1").arg(trimmed);
            return false;
        }
        if (isoMatch.captured(4).isEmpty()) {
            *out = QDateTime(date, QTime(0, 0));
            *hasTime = false;
            return true;
        }
        const QTime time(isoMatch.captured(4).toInt(), isoMatch.captured(5).toInt(),
                         isoMatch.captured(6).isEmpty() ? 0 : isoMatch.captured(6).toInt());
        if (!time.isValid()) {
            if (error) *error = QCoreApplication::translate("Actions", "invalid time: %1").arg(trimmed);
            return false;
        }
        *out = QDateTime(date, time);
        *hasTime = true;
        return true;
    }

    QString rest = trimmed.toLower();

    // Optional trailing time: "kl. 14:30", "kl 9", "at 14:30", "14:30",
    // "fredag 12" (weekday + 0-23 = hour), "i morgen 10". A trailing number
    // above 23 without minutes is not a time; it stays for the date part
    // ("25" = the 25th).
    static const QRegularExpression timeRe(
        QStringLiteral("(?:^|\\s)(?:\\bkl\\.?|\\bat\\b)?\\s*(\\d{1,2})(?:[:.](\\d{2}))?\\s*$"));
    QTime time(0, 0);
    bool withTime = false;
    const auto timeMatch = timeRe.match(rest);
    if (timeMatch.hasMatch()) {
        const int hour = timeMatch.captured(1).toInt();
        const int minute = timeMatch.captured(2).isEmpty() ? 0 : timeMatch.captured(2).toInt();
        if (hour <= 23) {
            time = QTime(hour, minute);
            if (!time.isValid()) {
                if (error) *error = QCoreApplication::translate("Actions", "invalid time: %1").arg(trimmed);
                return false;
            }
            withTime = true;
            rest = rest.left(timeMatch.capturedStart()).trimmed();
        } else if (!timeMatch.captured(2).isEmpty() || hour > 31) {
            if (error) *error = QCoreApplication::translate("Actions", "invalid time: %1").arg(trimmed);
            return false;
        }
    }

    QDate date;
    if (rest.isEmpty() || rest == QLatin1String("i dag") || rest == QLatin1String("today")) {
        date = today;
    } else if (rest == QLatin1String("i morgen") || rest == QLatin1String("tomorrow")) {
        date = today.addDays(1);
    } else if (rest == QLatin1String("i overmorgen") || rest == QLatin1String("day after tomorrow")) {
        date = today.addDays(2);
    } else if (rest == QStringLiteral("næste uge") || rest == QLatin1String("next week")) {
        date = today.addDays(8 - today.dayOfWeek()); // Monday of next week
    } else if (rest == QLatin1String("i weekenden") || rest == QLatin1String("weekend")
               || rest == QLatin1String("this weekend")) {
        date = today.addDays((6 - today.dayOfWeek() + 7) % 7); // Saturday
    } else if (auto m = QRegularExpression(QStringLiteral("^(?:om|in) (\\d{1,3}) (?:dag(?:e)?|days?)$")).match(rest);
               m.hasMatch()) {
        date = today.addDays(m.captured(1).toInt());
    } else {
        bool strictlyNext = false;
        if (rest.startsWith(QStringLiteral("næste "))) {
            strictlyNext = true;
            rest = rest.mid(QStringLiteral("næste ").size()).trimmed();
        } else if (rest.startsWith(QLatin1String("next "))) {
            strictlyNext = true;
            rest = rest.mid(5).trimmed();
        }

        // "mandag i dag" means today; remember that and drop the phrase
        // before tokenizing (a bare weekday alone means NEXT week when the
        // weekday is today — see below).
        const bool todayMentioned = rest.contains(QStringLiteral("i dag"))
                                    || rest.contains(QLatin1String("today"));
        if (todayMentioned)
            rest = QString(rest).remove(QStringLiteral("i dag")).remove(QLatin1String("today")).trimmed();

        // Tokens: weekday, "den 12.", "12.", "12/12", "12. december", bare "25".
        int weekday = 0, day = 0, month = 0;
        bool hasDen = false, bad = false;
        static const QRegularExpression dayDot(QStringLiteral("^(\\d{1,2})\\.$"));
        static const QRegularExpression daySlash(QStringLiteral("^(\\d{1,2})/(\\d{1,2})$"));
        static const QRegularExpression bareNumber(QStringLiteral("^\\d{1,2}$"));
        const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (weekdays().contains(part)) {
                weekday = weekdays().value(part);
            } else if (part == QLatin1String("den")) {
                hasDen = true;
            } else if (auto m = dayDot.match(part); m.hasMatch()) {
                day = m.captured(1).toInt();
            } else if (auto m = daySlash.match(part); m.hasMatch()) {
                day = m.captured(1).toInt();
                month = m.captured(2).toInt();
            } else if (monthNames().contains(part)) {
                month = monthNames().value(part);
            } else if (bareNumber.match(part).hasMatch()) {
                day = part.toInt();
            } else {
                bad = true;
            }
        }
        if (bad || (weekday == 0 && day == 0)) {
            if (error) *error = QCoreApplication::translate("Actions", "cannot parse date: %1").arg(trimmed);
            return false;
        }
        if (weekday != 0 && day != 0 && month == 0 && !hasDen) {
            // "fredag 25": a number above 23 after a weekday is neither a
            // time nor a date-of-month form.
            if (error) *error = QCoreApplication::translate("Actions", "cannot parse date: %1").arg(trimmed);
            return false;
        }
        if (day != 0 && month != 0) {
            // "12/12" or "12. december": explicit day and month.
            int year = today.year();
            QDate candidate(year, month, 1);
            if (day > candidate.daysInMonth()) {
                if (error) *error = QCoreApplication::translate("Actions", "invalid date: %1").arg(trimmed);
                return false;
            }
            candidate = candidate.addDays(day - 1);
            if (candidate < today)
                candidate = QDate(year + 1, month, 1).addDays(day - 1);
            date = candidate;
            if (weekday != 0 && date.dayOfWeek() != weekday) {
                if (error) *error = QCoreApplication::translate("Actions", "%1 is not a %2")
                                        .arg(date.toString(Qt::ISODate), weekdays().key(weekday));
                return false;
            }
        } else if (day != 0) {
            // "den 12.", "12.", bare "25": day of the month, first occurrence
            // from today on; a day the month lacks rolls to a later month
            // instead of being clamped.
            if (!dayOfMonthDate(today, day, &date)) {
                if (error) *error = QCoreApplication::translate("Actions", "invalid date: %1").arg(trimmed);
                return false;
            }
            if (weekday != 0) {
                if (!hasDen) {
                    if (error) *error = QCoreApplication::translate("Actions", "cannot parse date: %1").arg(trimmed);
                    return false;
                }
                if (date.dayOfWeek() != weekday) {
                    if (error) *error = QCoreApplication::translate("Actions", "%1 is not a %2")
                                            .arg(date.toString(Qt::ISODate), weekdays().key(weekday));
                    return false;
                }
            }
        } else {
            // Weekday only: the next occurrence of that weekday. When the
            // weekday IS today, a bare weekday means next week ("til mandag"
            // on a Monday = +7 days), unless "i dag"/"today" is also present.
            int delta = (weekday - today.dayOfWeek() + 7) % 7;
            if (delta == 0 && (strictlyNext || !todayMentioned))
                delta = 7;
            date = today.addDays(delta);
        }
    }

    *out = QDateTime(date, withTime ? time : QTime(0, 0));
    *hasTime = withTime;
    return true;
}
