#include "actions.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

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

// The date a task counts as, for list filters and summaries: its start, else
// its deadline, else the day it was created.
static QDate effectiveDate(const Task &task) {
    if (task.start.isValid())
        return task.start.date();
    if (task.deadline.isValid())
        return task.deadline.date();
    return task.created.date();
}

ActionResult Actions::create(const Task &task, const QString &source) {
    QString error;
    const QString id = m_store->createTask(task, source, &error);
    if (id.isEmpty())
        return {false, error, {}, {}};
    Task stored;
    m_store->task(id, &stored, &error);
    QJsonObject payload;
    payload["task"] = stored.toJson();
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

    bool ok = false;
    Task beforeUndo, afterUndo;
    bool hasBefore = false, hasAfter = false;
    if (target.kind == QLatin1String("create") && target.hasAfter) {
        ok = m_store->deleteTask(target.after.id, &error);
        beforeUndo = target.after;
        hasBefore = true;
    } else if (target.kind == QLatin1String("delete") && target.hasBefore) {
        ok = m_store->insertTask(target.before, &error);
        afterUndo = target.before;
        hasAfter = true;
    } else if (target.kind == QLatin1String("update") && target.hasBefore && target.hasAfter) {
        ok = m_store->replaceTask(target.before, &error);
        beforeUndo = target.after;
        afterUndo = target.before;
        hasBefore = hasAfter = true;
    } else {
        error = tr("Change %1 cannot be undone").arg(target.id);
    }
    if (ok)
        ok = m_store->logChange(source, QStringLiteral("undo"), target.taskId,
                                hasBefore ? &beforeUndo : nullptr, hasAfter ? &afterUndo : nullptr, target.id);
    if (!ok) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    if (!m_store->commit(&error)) {
        m_store->rollback();
        return {false, error, {}, {}};
    }
    QJsonObject payload;
    payload["change"] = changeToJson(target);
    return {true, {}, tr("Undone %1 on task %2").arg(target.kind, target.taskId), payload};
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

// Day-of-month inside the month of `base`, clamped to the month's length;
// rolls into the next month when already past.
static QDate dayOfMonth(const QDate &base, int day, bool future) {
    QDate candidate(base.year(), base.month(), 1);
    candidate = candidate.addDays(qMin(day, candidate.daysInMonth()) - 1);
    if (future && candidate < base)
        candidate = QDate(base.year(), base.month(), 1).addMonths(1).addDays(qMin(day, QDate(base.year(), base.month(), 1).addMonths(1).daysInMonth()) - 1);
    return candidate;
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

    // Optional trailing time: "kl. 14:30", "kl 9.00", "at 14:30", "14:30".
    static const QRegularExpression timeRe(
        QStringLiteral("(?:\\bkl\\.?|\\bat\\b)?\\s*(\\d{1,2})[:.](\\d{2})\\s*$"));
    QTime time(0, 0);
    bool withTime = false;
    const auto timeMatch = timeRe.match(rest);
    if (timeMatch.hasMatch()) {
        time = QTime(timeMatch.captured(1).toInt(), timeMatch.captured(2).toInt());
        if (!time.isValid()) {
            if (error) *error = QCoreApplication::translate("Actions", "invalid time: %1").arg(trimmed);
            return false;
        }
        withTime = true;
        rest = rest.left(timeMatch.capturedStart()).trimmed();
    }

    QDate date;
    if (rest.isEmpty() || rest == QLatin1String("i dag") || rest == QLatin1String("today")) {
        date = today;
    } else if (rest == QLatin1String("i morgen") || rest == QLatin1String("tomorrow")) {
        date = today.addDays(1);
    } else if (rest == QLatin1String("i overmorgen") || rest == QLatin1String("day after tomorrow")) {
        date = today.addDays(2);
    } else {
        bool strictlyNext = false;
        if (rest.startsWith(QStringLiteral("næste "))) {
            strictlyNext = true;
            rest = rest.mid(QStringLiteral("næste ").size()).trimmed();
        } else if (rest.startsWith(QLatin1String("next "))) {
            strictlyNext = true;
            rest = rest.mid(5).trimmed();
        }
        int weekday = 0, dayNumber = 0;
        bool bad = false;
        const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (weekdays().contains(part))
                weekday = weekdays().value(part);
            else if (part.contains(QRegularExpression(QStringLiteral("^\\d+$"))))
                dayNumber = part.toInt();
            else
                bad = true;
        }
        if (bad || (weekday == 0 && dayNumber == 0)) {
            if (error) *error = QCoreApplication::translate("Actions", "cannot parse date: %1").arg(trimmed);
            return false;
        }
        if (weekday != 0) {
            int delta = (weekday - today.dayOfWeek() + 7) % 7;
            if (strictlyNext && delta == 0)
                delta = 7;
            date = today.addDays(delta);
            if (dayNumber != 0)
                date = dayOfMonth(today, dayNumber, true); // "fredag 12" = the 12th, not the Friday
        } else {
            date = dayOfMonth(today, dayNumber, true);
        }
    }

    *out = QDateTime(date, withTime ? time : QTime(0, 0));
    *hasTime = withTime;
    return true;
}
