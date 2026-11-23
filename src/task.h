#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// One task in the plan. Dates and times are local time; a value without a
// time keeps hasTime false, so "Friday" and "Friday 09:00" stay distinct.
struct Task {
    enum class Status { Todo, Done };

    QString id;                  // UUID
    QString title;
    QString notes;
    QString project;
    int priority = 3;            // 1..3, 1 = highest
    Status status = Status::Todo;
    QDateTime start;             // invalid = unscheduled
    bool startHasTime = false;   // false = date only
    int durationMinutes = 0;     // 0 = unset; date-only without duration = all-day task
    QDateTime deadline;          // invalid = none; separate from start
    bool deadlineHasTime = false;
    QStringList guesses;         // field names the AI guessed, e.g. "start"
    QString captureId;           // id of the capture this task came from
    QDateTime created;
    QDateTime modified;

    // Field names allowed in `guesses` (JSON key `guessed`, as in docs/ai-schema.json).
    static const QStringList guessableFields();

    static QString newId();
    static QString statusToString(Status status);
    static Status statusFromString(const QString &text, bool *ok = nullptr);

    // Empty string when valid, otherwise a human-readable reason.
    QString validate() const;

    QJsonObject toJson() const;
    static Task fromJson(const QJsonObject &json, bool *ok = nullptr);
};

// The date a task counts as for lists, filters and summaries: its start, else
// its deadline, else the day it was created.
inline QDate effectiveDate(const Task &task) {
    if (task.start.isValid())
        return task.start.date();
    if (task.deadline.isValid())
        return task.deadline.date();
    return task.created.date();
}

// Overdue = still open and past its deadline, or (with no deadline) past its
// start day. Unscheduled tasks are never overdue.
inline bool isOverdue(const Task &task, const QDate &today) {
    if (task.status != Task::Status::Todo)
        return false;
    if (task.deadline.isValid())
        return task.deadline.date() < today;
    return task.start.isValid() && task.start.date() < today;
}
