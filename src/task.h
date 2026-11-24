#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <algorithm>

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

// A task's extent on the timeline. The duration is the work: with one, the
// bar is start + duration (whole days for a start without a time), and a
// deadline after that is a separate mark. Without one, a deadline on a later
// day is the end ("from Monday until Friday"); otherwise the bar is the whole
// day, or 30 minutes from a start with a time. A task with only a deadline is
// just the mark. web/app.js (taskSpan) and src/render.cpp follow the same rule.
struct TaskSpan {
    QDateTime start, end;     // invalid without a start; end is exclusive (midnight after a whole day)
    bool allDay = false;      // the start has no time: the bar covers whole days
    bool toDeadline = false;  // the end is the deadline
    QDateTime mark;           // a deadline after the end (or the only date): invalid = no mark
    bool markAllDay = false;  // the deadline has no time: the mark sits at the end of its day
};

inline TaskSpan taskSpan(const Task &task) {
    TaskSpan span;
    // The moment a deadline falls due: its time, or the end of its day.
    const QDateTime due = !task.deadline.isValid() ? QDateTime()
                          : task.deadlineHasTime ? task.deadline
                                                 : QDateTime(task.deadline.date().addDays(1), QTime(0, 0));
    if (task.start.isValid()) {
        span.allDay = !task.startHasTime;
        span.start = span.allDay ? QDateTime(task.start.date(), QTime(0, 0)) : task.start;
        if (task.durationMinutes > 0) {
            span.end = span.allDay ? span.start.addDays(std::max(1, (task.durationMinutes + 1439) / 1440))
                                   : span.start.addSecs(qint64(task.durationMinutes) * 60);
        } else if (due.isValid() && task.deadline.date() > task.start.date()) {
            // Whole days run to the end of the deadline's day, whatever its time.
            span.end = span.allDay ? QDateTime(task.deadline.date().addDays(1), QTime(0, 0)) : due;
            span.toDeadline = true;
        } else {
            span.end = span.allDay ? span.start.addDays(1) : span.start.addSecs(30 * 60);
        }
    }
    if (due.isValid() && (!span.end.isValid() || due > span.end)) {
        span.mark = task.deadlineHasTime ? task.deadline : QDateTime(task.deadline.date(), QTime(0, 0));
        span.markAllDay = !task.deadlineHasTime;
    }
    return span;
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
