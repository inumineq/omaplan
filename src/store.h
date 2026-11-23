#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

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

    bool open(QString *error = nullptr);
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    int schemaVersion() const;
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
    // undoOf > 0 marks this entry as the undo of that change id.
    bool logChange(const QString &source, const QString &kind, const QString &taskId,
                   const Task *before, const Task *after, qlonglong undoOf = 0);
    bool begin(QString *error = nullptr);
    bool commit(QString *error = nullptr);
    void rollback();

    struct Capture {
        QString id;
        QString rawText;
        QString engine;
        QString model;
        QString response;
        QDateTime created;
    };
    QString addCapture(const QString &rawText, const QString &engine, const QString &model,
                       const QString &response, QString *error = nullptr);
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
    };
    QList<Change> changes(int limit = 50, QString *error = nullptr) const;
    // The newest change that no undo has inverted yet — what undo() reverts.
    bool lastChange(Change *out, QString *error = nullptr) const;

private:
    bool migrate(QString *error);
    bool createTables(QSqlQuery &query, QString *error);
    bool writeTask(const Task &task, bool replace, QString *error);
    Task rowToTask(const QSqlQuery &query) const;

    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_db;
};
