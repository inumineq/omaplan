#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "store.h"
#include "task.h"

struct ActionResult {
    bool ok = false;
    QString error;
    QString text;         // human-readable output (already translated)
    QJsonObject payload;  // machine-readable output for --json
};

struct ListFilter {
    bool hasFrom = false;
    QDate from;
    bool hasTo = false;
    QDate to;
    QString project;      // empty = all
    QString status;       // empty = all, "todo"/"done"
};

// Every change to the plan goes through here — CLI, MCP and QML alike — so the
// three frontends can never behave differently.
class Actions : public QObject {
    Q_OBJECT
public:
    explicit Actions(Store *store, QObject *parent = nullptr) : QObject(parent), m_store(store) {}

    ActionResult create(const Task &task, const QString &source);
    ActionResult update(const QString &id, const QJsonObject &patch, const QString &source);
    ActionResult move(const QString &id, const QDateTime &when, bool hasTime, const QString &source);
    ActionResult setDone(const QString &id, bool done, const QString &source);
    ActionResult remove(const QString &id, const QString &source);
    // Inverts the newest change that no undo has inverted yet, so undo works
    // across processes (GUI, CLI, MCP share the database).
    ActionResult undo(const QString &source);
    // Re-applies the newest undo that no redo has re-applied yet.
    ActionResult redo(const QString &source);

    ActionResult list(const ListFilter &filter);
    ActionResult show(const QString &id);
    ActionResult projects();
    ActionResult summary(bool week); // true = this week (Monday start), false = this month

    // Pure date parsing, no I/O: ISO dates and datetimes plus Danish/English
    // expressions ("i morgen", "fredag 12", "tomorrow", "next monday", with an
    // optional time like "kl. 14:30"). `today` is the reference date, so tests
    // can pass a fixed one.
    static bool parseDateTime(const QString &text, const QDate &today, QDateTime *out, bool *hasTime,
                              QString *error = nullptr);

    // QML-friendly wrappers (source "gui"); each returns
    // {"ok": bool, "error": string, "text": string, "payload": object}.
    Q_INVOKABLE QJsonObject guiUpdate(const QString &id, const QVariantMap &patch);
    Q_INVOKABLE QJsonObject guiMove(const QString &id, const QDateTime &when, bool hasTime);
    Q_INVOKABLE QJsonObject guiSetDone(const QString &id, bool done);
    Q_INVOKABLE QJsonObject guiRemove(const QString &id);
    Q_INVOKABLE QJsonObject guiUndo();
    Q_INVOKABLE QJsonObject guiRedo();
    Q_INVOKABLE QJsonObject guiProjects();

signals:
    // Emitted after every mutation, so models and views can reload.
    void planChanged();

private:
    Store *m_store;
};
