#include "task.h"

#include <QJsonArray>
#include <QUuid>

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
    if (ok) *ok = true;
    return Status::Todo;
}

QString Task::validate() const {
    const QUuid uuid(id);
    if (uuid.isNull())
        return QStringLiteral("id is not a UUID: %1").arg(id);
    if (title.trimmed().isEmpty())
        return QStringLiteral("title is empty");
    if (title.size() > 200)
        return QStringLiteral("title is longer than 200 characters");
    if (priority < 1 || priority > 3)
        return QStringLiteral("priority must be 1..3, got %1").arg(priority);
    if (durationMinutes < 0)
        return QStringLiteral("duration must not be negative, got %1").arg(durationMinutes);
    if (start.isValid() && startHasTime && !start.time().isValid())
        return QStringLiteral("start has a time flag but no valid time");
    for (const QString &guess : guesses)
        if (!guessableFields().contains(guess))
            return QStringLiteral("unknown guessed field: %1").arg(guess);
    if (!captureId.isEmpty() && QUuid(captureId).isNull())
        return QStringLiteral("capture id is not a UUID: %1").arg(captureId);
    return {};
}

// Dates are stored as ISO text; date-only values keep hasTime false so the
// two never collapse into each other.
static QString toIso(const QDateTime &dateTime, bool hasTime) {
    if (!dateTime.isValid())
        return {};
    return hasTime ? dateTime.toString(Qt::ISODateWithMs) : dateTime.date().toString(Qt::ISODate);
}

static QDateTime fromIso(const QString &text, bool hasTime) {
    if (text.isEmpty())
        return {};
    if (hasTime)
        return QDateTime::fromString(text, Qt::ISODateWithMs);
    return QDateTime(QDate::fromString(text, Qt::ISODate), QTime(0, 0));
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
    json["guesses"] = guessArray;
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
    const QJsonArray guessArray = json["guesses"].toArray();
    for (const auto &value : guessArray)
        task.guesses.append(value.toString());
    task.captureId = json["capture_id"].toString();
    task.created = fromIso(json["created"].toString(), true);
    task.modified = fromIso(json["modified"].toString(), true);

    if (ok)
        *ok = statusOk && task.validate().isEmpty();
    return task;
}
