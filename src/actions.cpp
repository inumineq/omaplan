#include "actions.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
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
    bool ok = false;
    const Task patched = Task::fromJson(json, &ok);
    if (!ok)
        return {false, patched.validate().isEmpty() ? tr("Invalid fields") : patched.validate(), {}, {}};
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

ActionResult Actions::undo(const QString &source) {
    QString error;
    // lastChange() skips changes that an undo already inverted, so repeated
    // undo walks back through the history one step at a time.
    Store::Change target;
    if (!m_store->lastChange(&target, &error))
        return {false, error, {}, {}};

    if (!m_store->begin(&error))
        return {false, error, {}, {}};

    // The inverse depends only on which side of the change is set: after-only
    // was a create (delete it), before-only was a delete (re-insert it), both
    // was an update (restore before).
    bool ok = false;
    Task beforeUndo, afterUndo;
    bool hasBefore = false, hasAfter = false;
    if (target.hasAfter && !target.hasBefore) {
        ok = m_store->deleteTask(target.after.id, &error);
        beforeUndo = target.after;
        hasBefore = true;
    } else if (target.hasBefore && !target.hasAfter) {
        ok = m_store->insertTask(target.before, &error);
        afterUndo = target.before;
        hasAfter = true;
    } else if (target.hasBefore && target.hasAfter) {
        ok = m_store->replaceTask(target.before, &error);
        beforeUndo = target.after;
        afterUndo = target.before;
        hasBefore = hasAfter = true;
    } else {
        error = tr("Change %1 cannot be undone").arg(target.id);
    }
    if (ok)
        ok = m_store->logChange(source, QStringLiteral("undo"), target.taskId,
                                hasBefore ? &beforeUndo : nullptr, hasAfter ? &afterUndo : nullptr, target.id) > 0;
    if (!ok) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    if (!m_store->commit(&error)) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    emit planChanged();
    QJsonObject payload;
    payload["change"] = changeToJson(target);
    return {true, {}, tr("Undone %1 on task %2").arg(target.kind, target.taskId), payload};
}

ActionResult Actions::redo(const QString &source) {
    QString error;
    Store::Change target;
    if (!m_store->lastUndoChange(&target, &error))
        return {false, error, {}, {}};

    if (!m_store->begin(&error))
        return {false, error, {}, {}};

    // Redo re-applies the state that existed before the undo: before-only was
    // an undo of a create (insert it again), after-only an undo of a delete
    // (delete it again), both an undo of an update (restore the newer side).
    bool ok = false;
    Task beforeRedo, afterRedo;
    bool hasBefore = false, hasAfter = false;
    if (target.hasBefore && !target.hasAfter) {
        ok = m_store->insertTask(target.before, &error);
        afterRedo = target.before;
        hasAfter = true;
    } else if (!target.hasBefore && target.hasAfter) {
        ok = m_store->deleteTask(target.after.id, &error);
        beforeRedo = target.after;
        hasBefore = true;
    } else if (target.hasBefore && target.hasAfter) {
        ok = m_store->replaceTask(target.before, &error);
        beforeRedo = target.after;
        afterRedo = target.before;
        hasBefore = hasAfter = true;
    } else {
        error = tr("Change %1 cannot be redone").arg(target.id);
    }
    qlonglong redoId = 0;
    if (ok)
        redoId = m_store->logChange(source, QStringLiteral("redo"), target.taskId,
                                    hasBefore ? &beforeRedo : nullptr, hasAfter ? &afterRedo : nullptr);
    if (ok && redoId > 0)
        ok = m_store->markRedone(target.id, redoId, &error);
    if (!ok || redoId <= 0) {
        m_store->rollback();
        return {false, ok ? error : error, {}, {}};
    }
    if (!m_store->commit(&error)) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    emit planChanged();
    QJsonObject payload;
    payload["change"] = changeToJson(target);
    return {true, {}, tr("Redone %1 on task %2").arg(target.kind, target.taskId), payload};
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

// ---------------------------------------------------------------------------
// AI proposals

// Maps the schema's guessed-field names (gaet) to Task's guess keys.
static QString guessKeyFromSchema(const QString &schemaField) {
    static const QHash<QString, QString> map = {
        {QStringLiteral("start"), QStringLiteral("start")},
        {QStringLiteral("varighed_min"), QStringLiteral("duration_minutes")},
        {QStringLiteral("frist"), QStringLiteral("deadline")},
        {QStringLiteral("projekt"), QStringLiteral("project")},
        {QStringLiteral("prioritet"), QStringLiteral("priority")},
    };
    return map.value(schemaField, schemaField);
}

static QStringList existingTaskLines(Store *store) {
    QString error;
    QStringList lines;
    for (const Task &task : store->tasks(&error)) {
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
    const QJsonArray tasks = proposalJson.value(QStringLiteral("opgaver")).toArray();
    QJsonArray stored;
    QStringList lines;
    for (const QJsonValue &value : tasks) {
        const QJsonObject o = value.toObject();
        Task task;
        task.title = o.value(QStringLiteral("titel")).toString();
        task.notes = o.value(QStringLiteral("noter")).toString();
        task.project = o.value(QStringLiteral("projekt")).toString();
        task.priority = o.value(QStringLiteral("prioritet")).toInt(0);
        if (task.priority == 0)
            task.priority = 3;
        if (o.contains(QStringLiteral("start"))) {
            const QString s = o.value(QStringLiteral("start")).toString();
            task.startHasTime = s.contains(QLatin1Char('T'));
            task.start = QDateTime::fromString(s, Qt::ISODate);
        }
        task.durationMinutes = o.value(QStringLiteral("varighed_min")).toInt(0);
        if (o.contains(QStringLiteral("frist"))) {
            const QString s = o.value(QStringLiteral("frist")).toString();
            task.deadlineHasTime = s.contains(QLatin1Char('T'));
            task.deadline = QDateTime::fromString(s, Qt::ISODate);
        }
        const QJsonArray gaet = o.value(QStringLiteral("gaet")).toArray();
        for (const QJsonValue &g : gaet)
            task.guesses.append(guessKeyFromSchema(g.toString()));
        task.captureId = captureId;

        QString error;
        const QString id = m_store->createTask(task, source, &error);
        if (id.isEmpty())
            return {false, error, {}, {}};
        stored.append(id);
        lines << tr("Created %1: %2").arg(id, task.title);
    }
    QJsonObject payload;
    payload["stored"] = stored;
    payload["count"] = stored.size();
    emit planChanged();
    return {true, {}, lines.join(QLatin1Char('\n')), payload};
}

void Actions::guiPropose(const QString &text, const QString &motorId) {
    guiCancelProposal();
    // Reuse runEngine in async mode: it stores the capture and fills the
    // result via the finished/failed lambdas... but those are local, so for
    // the GUI path we wire the signals here instead.
    QString error;
    const QString motor = motorId.isEmpty() ? AppConfig::load().engine : motorId;
    auto engine = createEngine(motor, QString(), QString(), &error);
    if (!engine) {
        emit proposalFailed(error);
        return;
    }
    QString whyNot;
    if (!engine->available(&whyNot)) {
        emit proposalFailed(whyNot);
        return;
    }
    const QDate today = QDateTime::currentDateTime().date();
    const QString annotated = annotateDates(text, today);
    PromptContext context;
    context.today = today;
    context.language = Translator::readLanguage(QDir::homePath() + QStringLiteral("/.config/omarchy/language"));
    context.existingTasks = existingTaskLines(m_store);

    m_engine = std::move(engine);
    const QString engineId = m_engine->id();
    const QString model = m_engine->modelInfo().isEmpty() ? m_engine->displayName() : m_engine->modelInfo();
    connect(m_engine.get(), &AiEngine::finished, this, [this, text, annotated, engineId, model](const Proposal &p, const QString &raw) {
        QString error;
        const QString captureId = m_store->addCapture(text, annotated, engineId, model, raw, &error);
        emit proposalReady(p.toJson(), captureId);
    });
    connect(m_engine.get(), &AiEngine::failed, this, [this](const QString &message) {
        emit proposalFailed(message);
    });
    m_engine->propose(annotated, context);
}

void Actions::guiCancelProposal() {
    if (m_engine && m_engine->isRunning())
        m_engine->cancel();
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
        if (task.status == Task::Status::Todo && task.deadline.isValid() && task.deadline.date() < today)
            overdue += 1;
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
            // Weekday only: the next occurrence, today included.
            int delta = (weekday - today.dayOfWeek() + 7) % 7;
            if (strictlyNext && delta == 0)
                delta = 7;
            date = today.addDays(delta);
        }
    }

    *out = QDateTime(date, withTime ? time : QTime(0, 0));
    *hasTime = withTime;
    return true;
}
