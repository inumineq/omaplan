#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "store.h"
#include "task.h"

class OmaplanTests : public QObject {
    Q_OBJECT

private slots:
    void taskValidationRejectsBadInput()
    {
        Task task;
        QVERIFY(!task.validate().isEmpty()); // no id, no title

        task.id = Task::newId();
        QVERIFY(!task.validate().isEmpty()); // still no title

        task.title = QStringLiteral("Write blog post");
        QCOMPARE(task.validate(), QString());

        task.priority = 0;
        QVERIFY(!task.validate().isEmpty());
        task.priority = 4;
        QVERIFY(!task.validate().isEmpty());
        task.priority = 1;
        QCOMPARE(task.validate(), QString());

        task.guesses = {"start", "nonsense"};
        QVERIFY(!task.validate().isEmpty());
        task.guesses = {"start", "duration_minutes", "deadline", "project", "priority"};
        QCOMPARE(task.validate(), QString());

        task.durationMinutes = -5;
        QVERIFY(!task.validate().isEmpty());
    }

    void taskJsonRoundTrip()
    {
        Task task;
        task.id = Task::newId();
        task.title = QStringLiteral("Kunde A-møde");
        task.notes = QStringLiteral("Forbered kontraktudkast");
        task.project = QStringLiteral("Kunde A");
        task.priority = 1;
        task.status = Task::Status::Done;
        task.start = QDateTime(QDate(2026, 11, 25), QTime(9, 30));
        task.startHasTime = true;
        task.durationMinutes = 90;
        task.deadline = QDateTime(QDate(2026, 11, 27), QTime(0, 0));
        task.deadlineHasTime = false;
        task.guesses = {"start", "priority"};
        task.captureId = Task::newId();
        task.created = QDateTime::currentDateTime();
        task.modified = task.created;

        bool ok = false;
        const Task copy = Task::fromJson(task.toJson(), &ok);
        QVERIFY(ok);
        QCOMPARE(copy.id, task.id);
        QCOMPARE(copy.title, task.title);
        QCOMPARE(copy.notes, task.notes);
        QCOMPARE(copy.project, task.project);
        QCOMPARE(copy.priority, task.priority);
        QCOMPARE(copy.status, task.status);
        QCOMPARE(copy.start, task.start);
        QCOMPARE(copy.startHasTime, true);
        QCOMPARE(copy.durationMinutes, 90);
        QCOMPARE(copy.deadline.date(), task.deadline.date());
        QCOMPARE(copy.deadlineHasTime, false);
        QCOMPARE(copy.guesses, task.guesses);
        QCOMPARE(copy.captureId, task.captureId);
    }

    void createReadUpdateDelete()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY2(store.open(&error), qPrintable(error));

        Task task;
        task.title = QStringLiteral("Skriv blogindlæg");
        task.project = QStringLiteral("Blog");
        task.priority = 2;
        task.start = QDateTime(QDate(2026, 11, 24), QTime(0, 0));
        task.startHasTime = false;
        const QString id = store.createTask(task, QStringLiteral("cli"), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        QVERIFY(!QUuid(id).isNull());

        Task read;
        QVERIFY2(store.task(id, &read, &error), qPrintable(error));
        QCOMPARE(read.title, task.title);
        QCOMPARE(read.project, task.project);
        QCOMPARE(read.priority, 2);
        QCOMPARE(read.status, Task::Status::Todo);
        QCOMPARE(read.start.date(), QDate(2026, 11, 24));
        QCOMPARE(read.startHasTime, false);
        QVERIFY(read.created.isValid());
        QVERIFY(read.modified.isValid());

        read.status = Task::Status::Done;
        read.title = QStringLiteral("Skriv blogindlæg om budgettet");
        QVERIFY2(store.updateTask(read, QStringLiteral("gui"), &error), qPrintable(error));
        Task updated;
        QVERIFY(store.task(id, &updated, &error));
        QCOMPARE(updated.title, QStringLiteral("Skriv blogindlæg om budgettet"));
        QCOMPARE(updated.status, Task::Status::Done);
        QCOMPARE(updated.created, read.created); // created survives an update

        QVERIFY2(store.removeTask(id, QStringLiteral("cli"), &error), qPrintable(error));
        QVERIFY(!store.task(id, nullptr, &error));
        QCOMPARE(store.tasks().size(), 0);
    }

    void invalidTaskIsNotStored()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Task task; // empty title, no id gets assigned but validation still fails on title
        QCOMPARE(store.createTask(task, QStringLiteral("cli"), &error), QString());
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.tasks().size(), 0);
        QCOMPARE(store.changes().size(), 0); // rejected writes leave no change behind
    }

    void migrationFromEmptyDatabase()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("fresh.db");
        {
            Store store(path);
            QString error;
            QVERIFY2(store.open(&error), qPrintable(error));
            QCOMPARE(store.schemaVersion(), 1);
            // The three tables exist.
            QSqlQuery query(store.database());
            QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"));
            QStringList names;
            while (query.next())
                names.append(query.value(0).toString());
            QVERIFY(names.contains("tasks"));
            QVERIFY(names.contains("captures"));
            QVERIFY(names.contains("changes"));

            Task task;
            task.title = QStringLiteral("Overlev migration");
            QVERIFY(!store.createTask(task, QStringLiteral("test"), &error).isEmpty());
        }
        {
            // Reopening keeps the schema version and the data.
            Store store(path);
            QString error;
            QVERIFY(store.open(&error));
            QCOMPARE(store.schemaVersion(), 1);
            QCOMPARE(store.tasks(&error).size(), 1);
        }
    }

    void changeLogRecordsBeforeAfterAndSource()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));

        Task task;
        task.title = QStringLiteral("Oprindelig titel");
        const QString id = store.createTask(task, QStringLiteral("cli"), &error);
        QVERIFY(!id.isEmpty());

        Task updated;
        QVERIFY(store.task(id, &updated, &error));
        updated.title = QStringLiteral("Rettet titel");
        QVERIFY(store.updateTask(updated, QStringLiteral("gui"), &error));

        QVERIFY(store.removeTask(id, QStringLiteral("mcp"), &error));

        const QList<Store::Change> log = store.changes(10, &error);
        QCOMPARE(log.size(), 3);
        // Newest first.
        QCOMPARE(log[0].kind, QStringLiteral("delete"));
        QCOMPARE(log[0].source, QStringLiteral("mcp"));
        QCOMPARE(log[0].taskId, id);
        QVERIFY(log[0].hasBefore);
        QVERIFY(!log[0].hasAfter);
        QCOMPARE(log[0].before.title, QStringLiteral("Rettet titel"));

        QCOMPARE(log[1].kind, QStringLiteral("update"));
        QCOMPARE(log[1].source, QStringLiteral("gui"));
        QVERIFY(log[1].hasBefore);
        QVERIFY(log[1].hasAfter);
        QCOMPARE(log[1].before.title, QStringLiteral("Oprindelig titel"));
        QCOMPARE(log[1].after.title, QStringLiteral("Rettet titel"));

        QCOMPARE(log[2].kind, QStringLiteral("create"));
        QCOMPARE(log[2].source, QStringLiteral("cli"));
        QVERIFY(!log[2].hasBefore);
        QVERIFY(log[2].hasAfter);
        QCOMPARE(log[2].after.id, id);
    }

    void capturesAreStoredWithEngineAndModel()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        const QString id = store.addCapture(QStringLiteral("podcast-klip tirsdag"),
                                            QStringLiteral("vibe"),
                                            QStringLiteral("mistral-large"),
                                            QStringLiteral("{\"opgaver\": []}"), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        const QList<Store::Capture> list = store.captures(&error);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].id, id);
        QCOMPARE(list[0].rawText, QStringLiteral("podcast-klip tirsdag"));
        QCOMPARE(list[0].engine, QStringLiteral("vibe"));
        QCOMPARE(list[0].model, QStringLiteral("mistral-large"));
        QCOMPARE(list[0].response, QStringLiteral("{\"opgaver\": []}"));
        QVERIFY(list[0].created.isValid());
    }

    void pathCanBeOverriddenByEnv()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("env.db");
        qputenv("OMAPLAN_DB", path.toLocal8Bit());
        QCOMPARE(Store::defaultPath(), path);
        Store store; // no argument: picks up the env override
        QString error;
        QVERIFY2(store.open(&error), qPrintable(error));
        QVERIFY(QFile::exists(path));
        qputenv("OMAPLAN_DB", "");
    }
};

QTEST_MAIN(OmaplanTests)
#include "omaplan_tests.moc"
