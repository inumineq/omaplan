#include "task.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUuid>

// Validation messages are user-facing (the CLI prints them), so they go through
// the translator like everything else.
static QString tr(const char *text) {
    return QCoreApplication::translate("Task", text);
}

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
        return tr("id is not a UUID: %1").arg(id);
    if (title.trimmed().isEmpty())
        return tr("title is empty");
    if (title.size() > 200)
        return tr("title is longer than 200 characters");
    if (priority < 1 || priority > 3)
        return tr("priority must be 1..3, got %1").arg(priority);
    if (durationMinutes < 0)
        return tr("duration must not be negative, got %1").arg(durationMinutes);
    if (start.isValid() && startHasTime && !start.time().isValid())
        return tr("start has a time flag but no valid time");
    for (const QString &guess : guesses)
        if (!guessableFields().contains(guess))
            return tr("unknown guessed field: %1").arg(guess);
    if (!captureId.isEmpty() && QUuid(captureId).isNull())
        return tr("capture id is not a UUID: %1").arg(captureId);
    return {};
}

// JSON dates follow docs/ai-skema.json: YYYY-MM-DD, or YYYY-MM-DDTHH:MM with
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
    if (!hasTime)
        return QDateTime(QDate::fromString(text, Qt::ISODate), QTime(0, 0));
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
