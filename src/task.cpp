#include "task.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUuid>

// Validation messages are user-facing (the CLI prints them), so they go through
// Qt's translators like everything else.

const QStringList Task::guessableFields() {
    return {"start", "duration_minutes", "deadline", "project", "priority"};
}

QString Task::newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString Task::statusToString(Status status) {
    return status == Status::Done ? QStringLiteral("done") : QStringLiteral("todo");
}

Task::Status Task::statusFromString(const QString &text, bool *ok) {
    if (text == QLatin1String("done")) {
        if (ok) *ok = true;
        return Status::Done;
    }
    // Empty means the default; anything else ("finished", "x") is rejected
    // instead of silently becoming todo.
    if (ok) *ok = text.isEmpty() || text == QLatin1String("todo");
    return Status::Todo;
}

QString Task::validate() const {
    const QUuid uuid(id);
    if (uuid.isNull())
        return QCoreApplication::translate("Task", "id is not a UUID: %1").arg(id);
    if (title.trimmed().isEmpty())
        return QCoreApplication::translate("Task", "title is empty");
    if (title.size() > 200)
        return QCoreApplication::translate("Task", "title is longer than 200 characters");
    if (priority < 1 || priority > 3)
        return QCoreApplication::translate("Task", "priority must be 1..3, got %1").arg(priority);
    if (durationMinutes < 0)
        return QCoreApplication::translate("Task", "duration must not be negative, got %1").arg(durationMinutes);
    if (start.isValid() && startHasTime && !start.time().isValid())
        return QCoreApplication::translate("Task", "start has a time flag but no valid time");
    for (const QString &guess : guesses)
        if (!guessableFields().contains(guess))
            return QCoreApplication::translate("Task", "unknown guessed field: %1").arg(guess);
    if (!captureId.isEmpty() && QUuid(captureId).isNull())
        return QCoreApplication::translate("Task", "capture id is not a UUID: %1").arg(captureId);
    return {};
}

// JSON dates follow docs/ai-schema.json: YYYY-MM-DD, or YYYY-MM-DDTHH:MM with
// a time — never milliseconds. Date-only values keep hasTime false so the two
// never collapse into each other.
static QString toIso(const QDateTime &dateTime, bool hasTime) {
    if (!dateTime.isValid())
        return {};
    return hasTime ? dateTime.toString(QStringLiteral("yyyy-MM-ddTHH:mm"))
                   : dateTime.date().toString(Qt::ISODate);
}

static QDateTime fromIso(const QString &text, bool hasTime) {
    if (text.isEmpty())
        return {};
    // A date-only field keeps only the date, even when the text carries a
    // time ("2026-11-24T10:00" with has_time false) — never an invalid date.
    if (!hasTime)
        return QDateTime(QDate::fromString(text.left(10), Qt::ISODate), QTime(0, 0));
    QDateTime dateTime = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dateTime.isValid())
        dateTime = QDateTime::fromString(text, Qt::ISODate);
    return dateTime;
}

QJsonObject Task::toJson() const {
    QJsonArray guessArray;
    for (const QString &guess : guesses)
        guessArray.append(guess);

    QJsonObject json;
    json["id"] = id;
    json["title"] = title;
    json["notes"] = notes;
    json["project"] = project;
    json["priority"] = priority;
    json["status"] = statusToString(status);
    json["start"] = toIso(start, startHasTime);
    json["start_has_time"] = startHasTime;
    json["duration_minutes"] = durationMinutes;
    json["deadline"] = toIso(deadline, deadlineHasTime);
    json["deadline_has_time"] = deadlineHasTime;
    json["guessed"] = guessArray;
    json["capture_id"] = captureId;
    json["created"] = toIso(created, true);
    json["modified"] = toIso(modified, true);
    return json;
}

Task Task::fromJson(const QJsonObject &json, bool *ok) {
    Task task;
    task.id = json["id"].toString();
    task.title = json["title"].toString();
    task.notes = json["notes"].toString();
    task.project = json["project"].toString();
    task.priority = json["priority"].toInt(3);
    bool statusOk = false;
    task.status = statusFromString(json["status"].toString(), &statusOk);
    task.startHasTime = json["start_has_time"].toBool(false);
    task.start = fromIso(json["start"].toString(), task.startHasTime);
    task.durationMinutes = json["duration_minutes"].toInt(0);
    task.deadlineHasTime = json["deadline_has_time"].toBool(false);
    task.deadline = fromIso(json["deadline"].toString(), task.deadlineHasTime);
    // "guesses" is the key before 0.2 (still in older undo history).
    const QJsonArray guessArray =
        (json.contains("guessed") ? json["guessed"] : json["guesses"]).toArray();
    for (const auto &value : guessArray)
        task.guesses.append(value.toString());
    task.captureId = json["capture_id"].toString();
    task.created = fromIso(json["created"].toString(), true);
    task.modified = fromIso(json["modified"].toString(), true);

    // A date text that does not parse makes the task invalid rather than
    // quietly unscheduling it.
    const bool datesOk = (json["start"].toString().isEmpty() || task.start.isValid())
                         && (json["deadline"].toString().isEmpty() || task.deadline.isValid());
    if (ok)
        *ok = statusOk && datesOk && task.validate().isEmpty();
    return task;
}
