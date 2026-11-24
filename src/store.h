#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>

#include "task.h"

class QSqlQuery;

// The only place that touches the data. SQLite via QSQLITE in WAL mode with a
// busy timeout, so the GUI and any number of CLI processes can write at the
// same time. Every write is recorded in `changes` with before/after JSON and
// the source (gui/cli/mcp), which is what makes undo work across processes.
class Store : public QObject {
    Q_OBJECT
public:
    // An empty path means the default location; the OMAPLAN_DB environment
    // variable overrides that, so tests and CLI flags can point at a
    // temporary file.
    explicit Store(const QString &path = {}, QObject *parent = nullptr);
    ~Store() override;

    static QString defaultPath();
    // ~/.local/share/omaplan/omaplan.db, whatever OMAPLAN_DB says.
    static QString standardPath();
    // Creates `path` (and missing parents) with the last one 0700; an
    // existing directory is left as it is.
    static bool makePrivateDirectory(const QString &path);
    QString path() const { return m_path; }

    bool open(QString *error = nullptr);
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    int schemaVersion() const;
    // PRAGMA data_version: the value changes only when ANOTHER connection
    // commits — own writes leave it alone. That is exactly what the GUI's
    // live polling needs to detect changes from CLI/agent processes.
    int dataVersion(bool *ok = nullptr) const;
    // Read-only handle for tests that inspect the schema directly.
    QSqlDatabase database() const { return m_db; }

    QString createTask(const Task &task, const QString &source, QString *error = nullptr);
    bool updateTask(const Task &task, const QString &source, QString *error = nullptr);
    bool removeTask(const QString &id, const QString &source, QString *error = nullptr);
    bool task(const QString &id, Task *out, QString *error = nullptr) const;
    QList<Task> tasks(QString *error = nullptr) const;

    // Low-level writes without a change-log entry, for Actions' undo, which
    // combines them with logChange() inside one transaction.
    bool insertTask(const Task &task, QString *error = nullptr);
    bool replaceTask(const Task &task, QString *error = nullptr);
    bool deleteTask(const QString &id, QString *error = nullptr);
    // undoOf > 0 marks this entry as the undo of that change id. Returns the
    // new change id, or 0 on error.
    qlonglong logChange(const QString &source, const QString &kind, const QString &taskId,
                        const Task *before, const Task *after, qlonglong undoOf = 0);
    // Marks an undo entry as redone by `redoId`.
    bool markRedone(qlonglong undoId, qlonglong redoId, QString *error = nullptr);
    // Transactions are BEGIN IMMEDIATE: the write lock is taken up front, so
    // a read-then-write (update, undo) can never fail half-way with a stale
    // WAL snapshot when another process committed in between. They nest:
    // only the outermost begin/commit touch SQLite, and a rollback anywhere
    // rolls back the whole transaction.
    bool begin(QString *error = nullptr);
    bool commit(QString *error = nullptr);
    void rollback();
    // While a batch id is set, every logged change carries it, so undo and
    // redo treat the batch (e.g. all tasks of one approved AI proposal) as
    // one step. Empty clears it.
    void setBatch(const QString &batch) { m_batch = batch; }

    struct Capture {
        QString id;
        QString rawText;
        QString annotatedText; // date-annotated variant sent to the engine
        QString engine;
        QString model;
        QString response;
        QDateTime created;
    };
    QString addCapture(const QString &rawText, const QString &annotatedText, const QString &engine,
                       const QString &model, const QString &response, QString *error = nullptr);
    QList<Capture> captures(QString *error = nullptr) const;

    struct Change {
        qlonglong id = 0;
        QDateTime at;
        QString source;   // gui | cli | mcp
        QString kind;     // create | update | delete | undo
        QString taskId;
        Task before;      // set for update/delete/undo
        Task after;       // set for create/update/undo
        bool hasBefore = false;
        bool hasAfter = false;
        qlonglong undoOf = 0; // set on undo entries: the change they invert
        qlonglong redoOf = 0; // set on undo entries once a redo has re-applied it
        QString batch;        // changes made as one step share a batch id
    };
    QList<Change> changes(int limit = 50, QString *error = nullptr) const;
    // What undo() reverts: the newest change no undo has inverted yet, plus
    // the rest of its batch, newest first. Fails with "Nothing to undo".
    bool undoTargets(QList<Change> *out, QString *error = nullptr) const;
    // What redo() re-applies: the newest undo no redo has re-applied yet,
    // plus the rest of its batch, newest first. An undo is only redoable
    // while no new change (create/update/delete) came after it; otherwise
    // redo would overwrite newer work. Fails with "Nothing to redo".
    bool redoTargets(QList<Change> *out, QString *error = nullptr) const;
    // The newest entry of any kind (for naming the source of a live change).
    bool newestChange(Change *out, QString *error = nullptr) const;

private:
    bool migrate(QString *error);
    bool migrateLocked(QSqlQuery &query, QString *error);
    bool createTables(QSqlQuery &query, QString *error);
    bool writeTask(const Task &task, bool replace, QString *error);
    Task rowToTask(const QSqlQuery &query) const;
    static Change rowToChange(const QSqlQuery &query);
    bool selectChanges(const QString &sql, const QVariantList &binds, QList<Change> *out,
                       QString *error) const;

signals:
    // After every outermost COMMIT, i.e. once per change however many rows
    // it touched. The snapshot (plan.json) hangs off this.
    void committed();

private:
    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_db;
    int m_txDepth = 0;
    QString m_batch;
};
