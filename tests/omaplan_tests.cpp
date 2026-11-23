#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

#include "actions.h"
#include "cli.h"
#include "store.h"
#include "task.h"

// Runs the CLI in-process against an already-constructed store, capturing
// stdout and stderr.
static int cli(Store &store, const QStringList &args, QString *out, QString *err) {
    QTextStream outStream(out, QIODevice::WriteOnly);
    QTextStream errStream(err, QIODevice::WriteOnly);
    return runCli(args, &store, outStream, errStream);
}

static QJsonObject cliJson(Store &store, const QStringList &args, QString *err, int *code) {
    QString out;
    *code = cli(store, args, &out, err);
    return QJsonDocument::fromJson(out.toUtf8()).object();
}

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
            QCOMPARE(store.schemaVersion(), 2);
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
            QCOMPARE(store.schemaVersion(), 2);
            QCOMPARE(store.tasks(&error).size(), 1);
        }
    }

    void migrationFromV1Database()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("v1.db");
        {
            // A database as trin 1's schema v1 left it: no undo_of column.
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1test"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE tasks (id TEXT PRIMARY KEY, title TEXT NOT NULL,"
                                          " notes TEXT NOT NULL DEFAULT '', project TEXT NOT NULL DEFAULT '',"
                                          " priority INTEGER NOT NULL DEFAULT 3, status TEXT NOT NULL DEFAULT 'todo',"
                                          " start TEXT, start_has_time INTEGER NOT NULL DEFAULT 0,"
                                          " duration_min INTEGER NOT NULL DEFAULT 0, deadline TEXT,"
                                          " deadline_has_time INTEGER NOT NULL DEFAULT 0,"
                                          " guesses TEXT NOT NULL DEFAULT '[]', capture_id TEXT NOT NULL DEFAULT '',"
                                          " created TEXT NOT NULL, modified TEXT NOT NULL)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE captures (id TEXT PRIMARY KEY, raw_text TEXT NOT NULL,"
                                          " engine TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '',"
                                          " response TEXT NOT NULL DEFAULT '', created TEXT NOT NULL)")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE changes (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                          " at TEXT NOT NULL, source TEXT NOT NULL, kind TEXT NOT NULL,"
                                          " task_id TEXT NOT NULL, before_json TEXT, after_json TEXT)")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version=1")));
            q.finish();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(QStringLiteral("v1test"));
        }
        Store store(path);
        QString error;
        QVERIFY2(store.open(&error), qPrintable(error));
        QCOMPARE(store.schemaVersion(), 2);
        QSqlQuery q(store.database());
        QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(changes)")));
        QStringList columns;
        while (q.next())
            columns << q.value(1).toString();
        QVERIFY(columns.contains(QStringLiteral("undo_of")));
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

    void actionsCrud()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);

        Task task;
        task.title = QStringLiteral("Skriv blogindlæg");
        task.project = QStringLiteral("Blog");
        ActionResult created = actions.create(task, QStringLiteral("cli"));
        QVERIFY2(created.ok, qPrintable(created.error));
        const QString id = created.payload["task"].toObject()["id"].toString();
        QVERIFY(!id.isEmpty());

        ActionResult shown = actions.show(id);
        QVERIFY(shown.ok);
        QCOMPARE(shown.payload["task"].toObject()["title"].toString(), task.title);

        QJsonObject patch;
        patch["title"] = QStringLiteral("Skriv blogindlæg om budgettet");
        patch["priority"] = 1;
        ActionResult edited = actions.update(id, patch, QStringLiteral("gui"));
        QVERIFY2(edited.ok, qPrintable(edited.error));
        QCOMPARE(edited.payload["task"].toObject()["title"].toString(), QStringLiteral("Skriv blogindlæg om budgettet"));
        QCOMPARE(edited.payload["task"].toObject()["priority"].toInt(), 1);

        ActionResult moved = actions.move(id, QDateTime(QDate(2026, 12, 2), QTime(10, 0)), true, QStringLiteral("cli"));
        QVERIFY2(moved.ok, qPrintable(moved.error));
        QCOMPARE(moved.payload["task"].toObject()["start_has_time"].toBool(), true);

        ActionResult done = actions.setDone(id, true, QStringLiteral("cli"));
        QVERIFY(done.ok);
        QCOMPARE(done.payload["task"].toObject()["status"].toString(), QStringLiteral("done"));
        ActionResult undone = actions.setDone(id, false, QStringLiteral("cli"));
        QVERIFY(undone.ok);
        QCOMPARE(undone.payload["task"].toObject()["status"].toString(), QStringLiteral("todo"));

        ActionResult removed = actions.remove(id, QStringLiteral("cli"));
        QVERIFY2(removed.ok, qPrintable(removed.error));
        QVERIFY(!actions.show(id).ok);
    }

    void actionsUndoWalksTheChangeLog()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);

        // Nothing to undo yet.
        QVERIFY(!actions.undo(QStringLiteral("cli")).ok);

        Task task;
        task.title = QStringLiteral("Første titel");
        const QString id = actions.create(task, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();

        QJsonObject patch;
        patch["title"] = QStringLiteral("Anden titel");
        QVERIFY(actions.update(id, patch, QStringLiteral("gui")).ok);

        // Undo the update: the original title is back.
        ActionResult undone = actions.undo(QStringLiteral("cli"));
        QVERIFY2(undone.ok, qPrintable(undone.error));
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("Første titel"));

        // Undo the create: the task is gone.
        QVERIFY(actions.undo(QStringLiteral("cli")).ok);
        QVERIFY(!actions.show(id).ok);
        QCOMPARE(actions.list({}).payload["tasks"].toArray().size(), 0);

        // Undo the delete of a removed task: it comes back with the same id.
        const QString id2 = actions.create(task, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();
        QVERIFY(actions.remove(id2, QStringLiteral("mcp")).ok);
        QVERIFY(actions.undo(QStringLiteral("gui")).ok);
        QVERIFY(actions.show(id2).ok);
    }

    void undoWorksAcrossProcesses()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("shared.db");
        QString error;
        Store first(path);
        QVERIFY(first.open(&error));
        Actions actionsFirst(&first);
        Task task;
        task.title = QStringLiteral("Opret i proces A");
        const QString id = actionsFirst.create(task, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();

        // A second connection to the same file, like a separate CLI process.
        Store second(path);
        QVERIFY(second.open(&error));
        Actions actionsSecond(&second);
        ActionResult undone = actionsSecond.undo(QStringLiteral("gui"));
        QVERIFY2(undone.ok, qPrintable(undone.error));

        // The first connection sees the undo too.
        QVERIFY(!actionsFirst.show(id).ok);
    }

    void actionsListFiltersProjectsSummary()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);

        // Dates far enough out that "this week" never contains them.
        const QDate base = QDateTime::currentDateTime().date().addDays(70);
        Task a;
        a.title = QStringLiteral("Blogindlæg");
        a.project = QStringLiteral("Blog");
        a.start = QDateTime(base, QTime(0, 0));
        a.startHasTime = false;
        Task b = a;
        b.title = QStringLiteral("Kundemøde");
        b.project = QStringLiteral("Kunde A");
        b.start = QDateTime(base.addDays(4), QTime(0, 0));
        b.status = Task::Status::Done;
        QVERIFY(actions.create(a, QStringLiteral("cli")).ok);
        QVERIFY(actions.create(b, QStringLiteral("cli")).ok);

        ListFilter all;
        QCOMPARE(actions.list(all).payload["tasks"].toArray().size(), 2);

        ListFilter blog;
        blog.project = QStringLiteral("Blog");
        QCOMPARE(actions.list(blog).payload["tasks"].toArray().size(), 1);

        ListFilter done;
        done.status = QStringLiteral("done");
        const auto doneList = actions.list(done).payload["tasks"].toArray();
        QCOMPARE(doneList.size(), 1);
        QCOMPARE(doneList.first().toObject()["title"].toString(), QStringLiteral("Kundemøde"));

        ListFilter range;
        range.hasFrom = true;
        range.from = base.addDays(1);
        range.hasTo = true;
        range.to = base.addDays(30);
        QCOMPARE(actions.list(range).payload["tasks"].toArray().size(), 1);

        const auto projects = actions.projects().payload["projects"].toArray();
        QCOMPARE(projects.size(), 2);

        const QDate today = QDateTime::currentDateTime().date();
        Task inWeek;
        inWeek.title = QStringLiteral("Denne uge");
        inWeek.start = QDateTime(today, QTime(0, 0));
        QVERIFY(actions.create(inWeek, QStringLiteral("cli")).ok);
        const auto week = actions.summary(true).payload["summary"].toObject();
        QCOMPARE(week["tasks"].toInt(), 1);
        QCOMPARE(week["todo"].toInt(), 1);
        const auto month = actions.summary(false).payload["summary"].toObject();
        QVERIFY(month["tasks"].toInt() >= 1);
    }

    void dateParsing()
    {
        const QDate today(2026, 11, 23); // a Monday
        QCOMPARE(today.dayOfWeek(), 1);
        QDateTime out;
        bool hasTime = false;
        QString error;

        QVERIFY(Actions::parseDateTime(QStringLiteral("2026-12-01"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 12, 1));
        QVERIFY(!hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("2026-12-01 14:30"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 12, 1));
        QCOMPARE(out.time(), QTime(14, 30));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("2026-12-01T09:05"), today, &out, &hasTime, &error));
        QCOMPARE(out.time(), QTime(9, 5));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("i dag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today);
        QVERIFY(!hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("tomorrow"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(1));

        QVERIFY(Actions::parseDateTime(QStringLiteral("i morgen"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(1));

        QVERIFY(Actions::parseDateTime(QStringLiteral("fredag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date().dayOfWeek(), 5);
        QCOMPARE(out.date(), today.addDays(4));

        QVERIFY(Actions::parseDateTime(QStringLiteral("mandag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today); // today is Monday

        QVERIFY(Actions::parseDateTime(QStringLiteral("next monday"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(7));

        QVERIFY(Actions::parseDateTime(QStringLiteral("næste fredag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(4));

        // "fredag 12": the 12th; November's is past, so December's.
        QVERIFY(Actions::parseDateTime(QStringLiteral("fredag 12"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 12, 12));

        QVERIFY(Actions::parseDateTime(QStringLiteral("25"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 25));

        QVERIFY(Actions::parseDateTime(QStringLiteral("fredag kl. 14:30"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(4));
        QCOMPARE(out.time(), QTime(14, 30));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("i morgen 09:00"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(1));
        QCOMPARE(out.time(), QTime(9, 0));
        QVERIFY(hasTime);

        QVERIFY(!Actions::parseDateTime(QStringLiteral("bogus"), today, &out, &hasTime, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Actions::parseDateTime(QStringLiteral("2026-13-40"), today, &out, &hasTime, &error));
    }

    void cliExitCodesAndJson()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("cli.db"));
        QString error;
        QVERIFY(store.open(&error));
        QString out, err;
        int code = 0;

        // --help exits 0 and shows examples.
        QCOMPARE(cli(store, {QStringLiteral("--help")}, &out, &err), 0);
        QVERIFY(out.contains(QStringLiteral("Usage:")));
        QVERIFY(out.contains(QStringLiteral("omaplan new")));

        // new --json exits 0 with the task as JSON on stdout.
        const QJsonObject created = cliJson(store, {QStringLiteral("new"), QStringLiteral("--titel"),
                                                     QStringLiteral("Test opgave"), QStringLiteral("--projekt"),
                                                     QStringLiteral("Blog"), QStringLiteral("--start"),
                                                     QStringLiteral("2026-12-01"), QStringLiteral("--json")},
                                            &err, &code);
        QCOMPARE(code, 0);
        const QJsonObject task = created["task"].toObject();
        QCOMPARE(task["title"].toString(), QStringLiteral("Test opgave"));
        QCOMPARE(task["project"].toString(), QStringLiteral("Blog"));
        QCOMPARE(task["start"].toString(), QStringLiteral("2026-12-01"));
        QCOMPARE(task["start_has_time"].toBool(), false);
        const QString id = task["id"].toString();
        QVERIFY(!QUuid(id).isNull());

        // Without --json the human text goes to stdout.
        out.clear();
        QCOMPARE(cli(store, {QStringLiteral("new"), QStringLiteral("--titel"), QStringLiteral("Anden opgave")}, &out, &err), 0);
        QVERIFY(out.contains(QStringLiteral("Created")));

        // list --json, show, edit, move, done, undone, rm, undo.
        const QJsonObject listed = cliJson(store, {QStringLiteral("list"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(listed["tasks"].toArray().size(), 2);

        const QJsonObject shown = cliJson(store, {QStringLiteral("show"), id, QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(shown["task"].toObject()["id"].toString(), id);

        const QJsonObject edited = cliJson(store, {QStringLiteral("edit"), id, QStringLiteral("--titel"),
                                                    QStringLiteral("Rettet"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(edited["task"].toObject()["title"].toString(), QStringLiteral("Rettet"));

        const QJsonObject moved = cliJson(store, {QStringLiteral("move"), id, QStringLiteral("2026-12-02 10:00"),
                                                   QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(QDateTime::fromString(moved["task"].toObject()["start"].toString(), Qt::ISODateWithMs),
                 QDateTime(QDate(2026, 12, 2), QTime(10, 0)));
        QCOMPARE(moved["task"].toObject()["start_has_time"].toBool(), true);

        QCOMPARE(cli(store, {QStringLiteral("done"), id, QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cliJson(store, {QStringLiteral("show"), id, QStringLiteral("--json")}, &err, &code)["task"].toObject()["status"].toString(),
                 QStringLiteral("done"));
        QCOMPARE(cli(store, {QStringLiteral("undone"), id}, &out, &err), 0);

        // list with filters.
        const QJsonObject filtered = cliJson(store, {QStringLiteral("list"), QStringLiteral("--projekt"), QStringLiteral("Blog"),
                                                      QStringLiteral("--status"), QStringLiteral("todo"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(filtered["tasks"].toArray().size(), 1);

        // projects and summary work over --json.
        QCOMPARE(cli(store, {QStringLiteral("projects"), QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cli(store, {QStringLiteral("summary"), QStringLiteral("--uge"), QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cli(store, {QStringLiteral("summary"), QStringLiteral("--maaned")}, &out, &err), 0);

        // undo removes the newest change; rm + undo brings the task back.
        QCOMPARE(cli(store, {QStringLiteral("rm"), id, QStringLiteral("--json")}, &out, &err), 0);
        const QJsonObject undone = cliJson(store, {QStringLiteral("undo"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(undone["change"].toObject()["kind"].toString(), QStringLiteral("delete"));
        QCOMPARE(cliJson(store, {QStringLiteral("show"), id, QStringLiteral("--json")}, &err, &code)["task"].toObject()["id"].toString(), id);

        // Errors: exit 1, message on stderr, nothing required on stdout.
        out.clear(); err.clear();
        QCOMPARE(cli(store, {QStringLiteral("show"), QStringLiteral("ikke-et-id")}, &out, &err), 1);
        QVERIFY(!err.isEmpty());
        QCOMPARE(cli(store, {QStringLiteral("new")}, &out, &err), 1); // mangler --titel
        QCOMPARE(cli(store, {QStringLiteral("frobnicate")}, &out, &err), 1);
        QCOMPARE(cli(store, {QStringLiteral("new"), QStringLiteral("--titel"), QStringLiteral("X"), QStringLiteral("--start"), QStringLiteral("bogus")}, &out, &err), 1);

        // Later-phase commands answer "not implemented yet" with exit 1.
        for (const char *cmd : {"add", "snapshot", "serve", "mcp", "skill"}) {
            err.clear();
            QCOMPARE(cli(store, {QString::fromLatin1(cmd), QStringLiteral("x")}, &out, &err), 1);
            QVERIFY2(err.contains(QStringLiteral("Not implemented yet")), qPrintable(err));
        }
    }
};

QTEST_MAIN(OmaplanTests)
#include "omaplan_tests.moc"
