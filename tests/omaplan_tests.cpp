#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

#include "actions.h"
#include "cli.h"
#include "config.h"
#include "dateannotate.h"
#include "planmodel.h"
#include "skill.h"
#include "store.h"
#include "task.h"
#include "theme.h"

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
            QCOMPARE(store.schemaVersion(), 5);
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
            QCOMPARE(store.schemaVersion(), 5);
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
        QCOMPARE(store.schemaVersion(), 5);
        QSqlQuery q(store.database());
        QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(changes)")));
        QStringList columns;
        while (q.next())
            columns << q.value(1).toString();
        QVERIFY(columns.contains(QStringLiteral("undo_of")));
        QVERIFY(columns.contains(QStringLiteral("redo_of")));
        QVERIFY(columns.contains(QStringLiteral("batch")));
        QVERIFY(q.exec(QStringLiteral("PRAGMA table_info(captures)")));
        QStringList captureColumns;
        while (q.next())
            captureColumns << q.value(1).toString();
        QVERIFY(captureColumns.contains(QStringLiteral("annotated_text")));
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
                                            QStringLiteral("podcast-klip tirsdag [tirsdag 2026-11-24]"),
                                            QStringLiteral("vibe"),
                                            QStringLiteral("mistral-large"),
                                            QStringLiteral("{\"opgaver\": []}"), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        const QList<Store::Capture> list = store.captures(&error);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].id, id);
        QCOMPARE(list[0].rawText, QStringLiteral("podcast-klip tirsdag"));
        QCOMPARE(list[0].annotatedText, QStringLiteral("podcast-klip tirsdag [tirsdag 2026-11-24]"));
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
        QVERIFY(!hasTime);

        // A bare weekday that IS today means next week (eval-vibe-2, punkt 1).
        QVERIFY(Actions::parseDateTime(QStringLiteral("mandag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(7)); // today is Monday -> next Monday
        QVERIFY(Actions::parseDateTime(QStringLiteral("monday"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(7));
        // ...unless "i dag"/"today" is also in the text.
        QVERIFY(Actions::parseDateTime(QStringLiteral("mandag i dag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today);
        QVERIFY(Actions::parseDateTime(QStringLiteral("mandag 9"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(7));
        QVERIFY(hasTime);
        QCOMPARE(out.time(), QTime(9, 0));

        QVERIFY(Actions::parseDateTime(QStringLiteral("next monday"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(7));

        QVERIFY(Actions::parseDateTime(QStringLiteral("næste fredag"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(4));

        // Weekday + number 0-23 is a time of day, not a day of the month.
        QVERIFY(Actions::parseDateTime(QStringLiteral("fredag 12"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(4)); // 2026-11-27, a Friday
        QCOMPARE(out.time(), QTime(12, 0));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("fredag 12.30"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(4));
        QCOMPARE(out.time(), QTime(12, 30));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("mandag 9.30"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(7)); // mandag == i dag -> næste mandag
        QCOMPARE(out.time(), QTime(9, 30));
        QVERIFY(hasTime);

        // Keyword + bare tal = date + time.
        QVERIFY(Actions::parseDateTime(QStringLiteral("i morgen 10"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(1));
        QCOMPARE(out.time(), QTime(10, 0));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("i dag 14"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today);
        QCOMPARE(out.time(), QTime(14, 0));
        QVERIFY(hasTime);

        QVERIFY(Actions::parseDateTime(QStringLiteral("tomorrow 9"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), today.addDays(1));
        QCOMPARE(out.time(), QTime(9, 0));
        QVERIFY(hasTime);

        // Day of the month only with explicit forms: "den 12.", "12.", "12/12", "12. december".
        for (const char *form : {"den 12.", "12.", "12/12", "12. december", "12 december"}) {
            QVERIFY2(Actions::parseDateTime(QString::fromUtf8(form), today, &out, &hasTime, &error),
                     qPrintable(QString::fromUtf8(form) + ": " + error));
            QCOMPARE(out.date(), QDate(2026, 12, 12)); // November's 12th is past
            QVERIFY(!hasTime);
        }

        // Weekday + "den 12." must agree: 2026-12-12 is a Saturday.
        QVERIFY(!Actions::parseDateTime(QStringLiteral("fredag den 12."), today, &out, &hasTime, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(Actions::parseDateTime(QStringLiteral("lørdag den 12."), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 12, 12));
        QVERIFY(!hasTime);

        // Weekday + number above 23 is neither a time nor a date form: error.
        QVERIFY(!Actions::parseDateTime(QStringLiteral("fredag 25"), today, &out, &hasTime, &error));

        // Bare tal alone stays a day of the month.
        QVERIFY(Actions::parseDateTime(QStringLiteral("25"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 25));
        QVERIFY(!hasTime);

        // A day the current month does not have must not be clamped into it.
        const QDate february(2026, 2, 10);
        QVERIFY(Actions::parseDateTime(QStringLiteral("30"), february, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 3, 30));
        QVERIFY(Actions::parseDateTime(QStringLiteral("31"), february, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 3, 31));
        QVERIFY(!Actions::parseDateTime(QStringLiteral("32"), february, &out, &hasTime, &error));

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
        // --json dates follow docs/ai-skema.json: no milliseconds.
        QCOMPARE(moved["task"].toObject()["start"].toString(), QStringLiteral("2026-12-02T10:00"));
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
        for (const char *cmd : {"snapshot", "serve"}) {
            err.clear();
            QCOMPARE(cli(store, {QString::fromLatin1(cmd), QStringLiteral("x")}, &out, &err), 1);
            QVERIFY2(err.contains(QStringLiteral("Not implemented yet")), qPrintable(err));
        }
    }

    void cliAddWithFakeCommandEngine()
    {
        // No real AI: the "command" engine runs `cat <fixture>` via a temp
        // config file, so `add` is tested end to end offscreen.
        QTemporaryDir dir;
        Store store(dir.filePath("cli.db"));
        QString error;
        QVERIFY(store.open(&error));

        const QString fixture = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("../../tests/ai-fixtures/envelopes/command-valid.json"));
        QVERIFY(QFile::exists(fixture));
        QTemporaryDir cfgDir;
        const QString cfgPath = cfgDir.filePath("config.toml");
        {
            QFile f(cfgPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[motor]\nengine = \"command\"\ncommand = \"cat " + fixture.toUtf8() + "\"\n");
        }
        qputenv("OMAPLAN_CONFIG", cfgPath.toLocal8Bit());

        QString out, err;
        int code = 0;
        // Without --ja the proposal is printed, nothing stored.
        const QJsonObject proposal = cliJson(store, {QStringLiteral("add"), QStringLiteral("køb mælk i morgen"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(proposal["proposal"].toObject()["opgaver"].toArray().size(), 1);
        QCOMPARE(proposal["proposal"].toObject()["opgaver"].toArray().first().toObject()["titel"].toString(),
                 QStringLiteral("Køb mælk"));
        QVERIFY(!proposal["capture_id"].toString().isEmpty());
        QCOMPARE(cliJson(store, {QStringLiteral("list"), QStringLiteral("--json")}, &err, &code)["tasks"].toArray().size(), 0);
        // The capture keeps raw text, annotated text, engine and model.
        const auto captures = store.captures(&error);
        QCOMPARE(captures.size(), 1);
        QCOMPARE(captures[0].rawText, QStringLiteral("køb mælk i morgen"));
        QVERIFY(captures[0].annotatedText.contains(QStringLiteral("i morgen [tirsdag 2026-11-24]")));
        QCOMPARE(captures[0].engine, QStringLiteral("command"));

        // With --ja the tasks are stored and linked to the capture.
        const QJsonObject stored = cliJson(store, {QStringLiteral("add"), QStringLiteral("køb mælk fredag"), QStringLiteral("--ja"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(stored["stored"].toArray().size(), 1);
        const auto tasks = cliJson(store, {QStringLiteral("list"), QStringLiteral("--json")}, &err, &code)["tasks"].toArray();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().toObject()["capture_id"].toString(), stored["capture_id"].toString());
        // gaet from the schema maps to Task guess keys.
        QCOMPARE(tasks.first().toObject()["guesses"].toArray().first().toString(), QStringLiteral("project"));

        // Unknown motor: exit 1 with a clear error.
        err.clear();
        QCOMPARE(cli(store, {QStringLiteral("add"), QStringLiteral("x"), QStringLiteral("--motor"), QStringLiteral("ingen")}, &out, &err), 1);
        QVERIFY(err.contains(QStringLiteral("Unknown engine")));

        qputenv("OMAPLAN_CONFIG", "");
    }

    void dateAnnotation()
    {
        const QDate today(2026, 11, 23); // Monday
        // Ugedage, nøgleord, tal+måned, tider, halv/kvart.
        QCOMPARE(annotateDates(QStringLiteral("podcast-klip til linkedin tirsdag"), today),
                 QStringLiteral("podcast-klip til linkedin tirsdag [tirsdag 2026-11-24]"));
        // Ugedag == i dag (mandag) betyder næste mandag.
        QCOMPARE(annotateDates(QStringLiteral("til mandag"), today),
                 QStringLiteral("til mandag [mandag 2026-11-30]"));
        // Longest phrase wins: "i morgen kl. 14:30" is annotated as one unit.
        QCOMPARE(annotateDates(QStringLiteral("møde i morgen kl. 14:30"), today),
                 QStringLiteral("møde i morgen kl. 14:30 [tirsdag 2026-11-24 kl. 14:30]"));
        QCOMPARE(annotateDates(QStringLiteral("frist fredag 12"), today),
                 QStringLiteral("frist fredag 12 [fredag 2026-11-27 kl. 12:00]"));
        QCOMPARE(annotateDates(QStringLiteral("deadline den 12. december"), today),
                 QStringLiteral("deadline den 12. december [lørdag 2026-12-12]"));
        QCOMPARE(annotateDates(QStringLiteral("næste uge"), today),
                 QStringLiteral("næste uge [mandag 2026-11-30]"));
        QCOMPARE(annotateDates(QStringLiteral("om 3 dage"), today),
                 QStringLiteral("om 3 dage [torsdag 2026-11-26]"));
        QCOMPARE(annotateDates(QStringLiteral("halv tre"), today),
                 QStringLiteral("halv tre [kl. 14:30]"));
        QCOMPARE(annotateDates(QStringLiteral("kvart over fire"), today),
                 QStringLiteral("kvart over fire [kl. 15:15]"));
        // Bare tal (varighed) annoteres ikke.
        QCOMPARE(annotateDates(QStringLiteral("research 3 timer onsdag"), today),
                 QStringLiteral("research 3 timer onsdag [onsdag 2026-11-25]"));
        // Tekst uden datoer står urørt.
        QCOMPARE(annotateDates(QStringLiteral("skriv noget"), today),
                 QStringLiteral("skriv noget"));
    }

    void parseDateTimeWeekendAndWeek()
    {
        const QDate today(2026, 11, 23); // Monday
        QDateTime out;
        bool hasTime = false;
        QString error;
        QVERIFY(Actions::parseDateTime(QStringLiteral("næste uge"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 30));
        QVERIFY(Actions::parseDateTime(QStringLiteral("next week"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 30));
        QVERIFY(Actions::parseDateTime(QStringLiteral("i weekenden"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 28)); // Saturday
        QVERIFY(Actions::parseDateTime(QStringLiteral("om 5 dage"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 28));
        QVERIFY(Actions::parseDateTime(QStringLiteral("in 10 days"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 12, 3));
    }

    void jsonDatesHaveNoMilliseconds()
    {
        // docs/ai-skema.json: YYYY-MM-DD or YYYY-MM-DDTHH:MM — never ms.
        Task task;
        task.id = Task::newId();
        task.title = QStringLiteral("Format");
        task.start = QDateTime(QDate(2026, 12, 1), QTime(14, 30, 45, 123));
        task.startHasTime = true;
        task.deadline = QDateTime(QDate(2026, 12, 5), QTime(0, 0));
        task.deadlineHasTime = false;
        task.created = QDateTime::currentDateTime();
        task.modified = task.created;
        const QJsonObject json = task.toJson();
        QCOMPARE(json["start"].toString(), QStringLiteral("2026-12-01T14:30"));
        QCOMPARE(json["deadline"].toString(), QStringLiteral("2026-12-05"));
        static const QRegularExpression withMs(QStringLiteral("\\d{3}$"));
        for (const char *key : {"start", "deadline", "created", "modified"})
            QVERIFY2(!withMs.match(json[QString::fromLatin1(key)].toString()).hasMatch(),
                     qPrintable(QString::fromLatin1(key) + ": " + json[QString::fromLatin1(key)].toString()));

        // Round trip still works, with and without milliseconds.
        bool ok = false;
        const Task copy = Task::fromJson(json, &ok);
        QVERIFY(ok);
        QCOMPARE(copy.start, QDateTime(QDate(2026, 12, 1), QTime(14, 30))); // ms dropped
        QJsonObject legacy;
        legacy["id"] = task.id;
        legacy["title"] = task.title;
        legacy["start"] = QStringLiteral("2026-12-01T14:30:00.000");
        legacy["start_has_time"] = true;
        const Task old = Task::fromJson(legacy, &ok);
        QVERIFY(ok);
        QCOMPARE(old.start.time(), QTime(14, 30));
    }

    void undoAndRedoWalkTheHistory()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);

        QVERIFY(!actions.redo(QStringLiteral("gui")).ok); // nothing to redo yet

        Task task;
        task.title = QStringLiteral("Titel");
        const QString id = actions.create(task, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();

        QJsonObject patch;
        patch["title"] = QStringLiteral("Ny titel");
        QVERIFY(actions.update(id, patch, QStringLiteral("gui")).ok);
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("Ny titel"));

        // undo, undo, redo, redo, undo — walks back and forth.
        QVERIFY(actions.undo(QStringLiteral("gui")).ok);
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("Titel"));
        QVERIFY(actions.undo(QStringLiteral("gui")).ok);
        QVERIFY(!actions.show(id).ok);
        QVERIFY(actions.redo(QStringLiteral("gui")).ok);
        QVERIFY(actions.show(id).ok);
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("Titel"));
        QVERIFY(actions.redo(QStringLiteral("gui")).ok);
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("Ny titel"));
        QVERIFY(!actions.redo(QStringLiteral("gui")).ok); // nothing left to redo
        QVERIFY(actions.undo(QStringLiteral("gui")).ok); // undo the redo
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("Titel"));
    }

    void planModelAndProxies()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        PlanModel model(&store);
        OverviewModel overview;
        CalendarModel calendar;
        PlanListModel list;
        GanttModel gantt;
        overview.setSourceModel(&model);
        calendar.setSourceModel(&model);
        list.setSourceModel(&model);
        gantt.setSourceModel(&model);

        const QDate today = QDateTime::currentDateTime().date();
        Task overdue;
        overdue.title = QStringLiteral("Forfalden");
        overdue.deadline = QDateTime(today.addDays(-2), QTime(0, 0));
        overdue.status = Task::Status::Todo;
        Task todayTask;
        todayTask.title = QStringLiteral("I dag");
        todayTask.start = QDateTime(today, QTime(9, 0));
        todayTask.startHasTime = true;
        todayTask.durationMinutes = 60;
        Task later;
        later.title = QStringLiteral("Om 10 dage");
        later.start = QDateTime(today.addDays(10), QTime(0, 0));
        Task unscheduled;
        unscheduled.title = QStringLiteral("Uden dato");
        overdue.id = actions.create(overdue, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();
        QVERIFY(actions.create(todayTask, QStringLiteral("cli")).ok);
        QVERIFY(actions.create(later, QStringLiteral("cli")).ok);
        QVERIFY(actions.create(unscheduled, QStringLiteral("cli")).ok);
        QVERIFY(!overdue.id.isEmpty());

        model.reload();
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(model.data(model.index(0, 0), PlanModel::TitleRole).toString().isEmpty(), false);
        QVERIFY(model.data(model.index(0, 0), PlanModel::IdRole).toString().length() > 0);

        // Overview: overdue first, then today..+7d; the task 10 days out is excluded.
        QCOMPARE(overview.rowCount(), 3);
        QCOMPARE(overview.data(overview.index(0, 0), PlanModel::TitleRole).toString(), QStringLiteral("Forfalden"));

        // Calendar range filters by effective date.
        calendar.setRange(today, today.addDays(7));
        // Only "I dag": the overdue task is two days back, and the undated
        // task (whose effective date is its creation day) is not a
        // calendar entry.
        QCOMPARE(calendar.rowCount(), 1);
        calendar.setRange(today.addDays(8), today.addDays(12));
        QCOMPARE(calendar.rowCount(), 1);
        QCOMPARE(calendar.data(calendar.index(0, 0), PlanModel::TitleRole).toString(), QStringLiteral("Om 10 dage"));

        // List has everything, undone first.
        QCOMPARE(list.rowCount(), 4);
        QCOMPARE(list.data(list.index(0, 0), PlanModel::StatusRole).toString(), QStringLiteral("todo"));

        // Gantt only scheduled tasks, sorted by start.
        QCOMPARE(gantt.rowCount(), 2);
        QVERIFY(gantt.data(gantt.index(0, 0), PlanModel::StartRole).toDateTime()
                < gantt.data(gantt.index(1, 0), PlanModel::StartRole).toDateTime());

        // Mutating through actions emits planChanged and a reload sees it.
        QSignalSpy spy(&actions, &Actions::planChanged);
        QVERIFY(actions.setDone(overdue.id, true, QStringLiteral("gui")).ok);
        QCOMPARE(spy.count(), 1);
        model.reload();
        // A done task from the past has left the agenda (it is no longer
        // overdue, and its day is gone).
        QCOMPARE(overview.rowCount(), 2);
        QCOMPARE(actions.show(overdue.id).payload["task"].toObject()["status"].toString(), QStringLiteral("done"));
        // A still-todo task with a past deadline sorts before everything else.
        QVERIFY(actions.setDone(overdue.id, false, QStringLiteral("gui")).ok);
        model.reload();
        QCOMPARE(overview.data(overview.index(0, 0), PlanModel::TitleRole).toString(), QStringLiteral("Forfalden"));
    }

    // ---- Fase 2: regression tests for the review fixes -------------------

    void redoIsRefusedAfterANewChange()
    {
        // Undo, then a new edit, then redo: the redo would overwrite the new
        // edit with stale data, so it must be refused.
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        Task task;
        task.title = QStringLiteral("A");
        const QString id = actions.create(task, QStringLiteral("gui")).payload["task"].toObject()["id"].toString();
        QJsonObject patch;
        patch["title"] = QStringLiteral("B");
        QVERIFY(actions.update(id, patch, QStringLiteral("gui")).ok);
        QVERIFY(actions.undo(QStringLiteral("gui")).ok); // back to A
        patch["title"] = QStringLiteral("C");
        QVERIFY(actions.update(id, patch, QStringLiteral("cli")).ok);
        const ActionResult redo = actions.redo(QStringLiteral("gui"));
        QVERIFY(!redo.ok);
        QCOMPARE(redo.error, QStringLiteral("Nothing to redo"));
        QCOMPARE(actions.show(id).payload["task"].toObject()["title"].toString(), QStringLiteral("C"));
    }

    void approveIsAtomicParsesEditedDatesAndUndoesInOneStep()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);

        // The user typed "fredag 10" over the engine's ISO date: it is parsed.
        const QJsonObject good = QJsonDocument::fromJson(R"({"opgaver": [
            {"titel": "Skriv blogindlæg", "start": "fredag 10", "projekt": "Blog"},
            {"titel": "Ring til Kunde A", "start": "2026-12-01T09:30", "gaet": ["start"]},
            {"titel": "Læs rapport", "frist": "2026-12-03"}]})").object();
        const ActionResult ok = actions.approve(good, QString(), QStringLiteral("gui"));
        QVERIFY2(ok.ok, qPrintable(ok.error));
        QCOMPARE(ok.payload["count"].toInt(), 3);
        const QJsonObject friday = actions.show(ok.payload["stored"].toArray().at(0).toString()).payload["task"].toObject();
        QVERIFY(friday["start"].toString().endsWith(QStringLiteral("T10:00")));
        QCOMPARE(QDate::fromString(friday["start"].toString().left(10), Qt::ISODate).dayOfWeek(), 5);

        // One undo takes the whole approval back; one redo restores it.
        QVERIFY(actions.undo(QStringLiteral("gui")).ok);
        QCOMPARE(store.tasks(&error).size(), 0);
        QVERIFY(actions.redo(QStringLiteral("gui")).ok);
        QCOMPARE(store.tasks(&error).size(), 3);
        QVERIFY(actions.undo(QStringLiteral("gui")).ok); // and the redo is one step too
        QCOMPARE(store.tasks(&error).size(), 0);

        // An unreadable date in the last task stores nothing at all and
        // names the task and the problem.
        const QJsonObject bad = QJsonDocument::fromJson(R"({"opgaver": [
            {"titel": "Første"}, {"titel": "Anden", "start": "engang"}]})").object();
        const ActionResult refused = actions.approve(bad, QString(), QStringLiteral("gui"));
        QVERIFY(!refused.ok);
        QVERIFY2(refused.error.contains(QStringLiteral("Anden")) && refused.error.contains(QStringLiteral("engang")),
                 qPrintable(refused.error));
        QCOMPARE(store.tasks(&error).size(), 0);
    }

    void transactionsTakeTheWriteLockUpFront()
    {
        // BEGIN IMMEDIATE: while one connection is inside a transaction (an
        // undo reading what to invert), another cannot commit underneath it.
        QTemporaryDir dir;
        const QString path = dir.filePath("t.db");
        Store a(path), b(path);
        QString error;
        QVERIFY(a.open(&error));
        QVERIFY(b.open(&error));
        QSqlQuery(b.database()).exec(QStringLiteral("PRAGMA busy_timeout=50"));
        QVERIFY(a.begin(&error));
        Task task;
        task.title = QStringLiteral("Fra B");
        QVERIFY(b.createTask(task, QStringLiteral("cli"), &error).isEmpty()); // locked
        QVERIFY(a.commit(&error));
        QVERIFY2(!b.createTask(task, QStringLiteral("cli"), &error).isEmpty(), qPrintable(error));
    }

    void cliFixes()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("cli.db"));
        QString error;
        QVERIFY(store.open(&error));
        QString out, err;
        int code = 0;

        // summary without a flag is this week (as the help says), not the month.
        const QJsonObject summary = cliJson(store, {QStringLiteral("summary"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        const QDate today = QDate::currentDate();
        QCOMPARE(summary["summary"].toObject()["from"].toString(),
                 today.addDays(1 - today.dayOfWeek()).toString(Qt::ISODate));

        const QString id = cliJson(store, {QStringLiteral("new"), QStringLiteral("--titel"), QStringLiteral("X"),
                                           QStringLiteral("--json")}, &err, &code)["task"].toObject()["id"].toString();
        // move without quotes: `move <id> 2026-12-01 14:30` keeps the time.
        const QJsonObject moved = cliJson(store, {QStringLiteral("move"), id, QStringLiteral("2026-12-01"),
                                                   QStringLiteral("14:30"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(moved["task"].toObject()["start"].toString(), QStringLiteral("2026-12-01T14:30"));

        // Garbage numbers and statuses are errors, not silent defaults.
        err.clear();
        QCOMPARE(cli(store, {QStringLiteral("edit"), id, QStringLiteral("--varighed"), QStringLiteral("1t")}, &out, &err), 1);
        QVERIFY(err.contains(QStringLiteral("varighed")));
        QCOMPARE(cli(store, {QStringLiteral("new"), QStringLiteral("--titel"), QStringLiteral("Y"),
                             QStringLiteral("--prioritet"), QStringLiteral("høj")}, &out, &err), 1);
        QCOMPARE(cli(store, {QStringLiteral("edit"), id, QStringLiteral("--status"), QStringLiteral("færdig")}, &out, &err), 1);
        QCOMPARE(cliJson(store, {QStringLiteral("show"), id, QStringLiteral("--json")}, &err, &code)["task"].toObject()["status"].toString(),
                 QStringLiteral("todo"));
    }

    void updateRejectsUnknownFieldsAndKeepsDateOnlyDates()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        Task task;
        task.title = QStringLiteral("X");
        const QString id = actions.create(task, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();
        QJsonObject typo;
        typo["titel"] = QStringLiteral("Y");
        QVERIFY(!actions.update(id, typo, QStringLiteral("mcp")).ok);
        // A date-only start whose text carries a time keeps its date instead
        // of becoming unscheduled.
        QJsonObject patch;
        patch["start"] = QStringLiteral("2026-12-01T10:00");
        const ActionResult r = actions.update(id, patch, QStringLiteral("mcp"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.payload["task"].toObject()["start"].toString(), QStringLiteral("2026-12-01"));
        // An unreadable date is an error.
        patch["start"] = QStringLiteral("fredag");
        QVERIFY(!actions.update(id, patch, QStringLiteral("mcp")).ok);
    }

    void overdueMeansPastStartOrDeadline()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        PlanModel model(&store);
        OverviewModel overview;
        overview.setSourceModel(&model);
        const QDate today = QDate::currentDate();

        Task pastStart; // started last month, never done: overdue
        pastStart.title = QStringLiteral("Glemt");
        pastStart.start = QDateTime(today.addDays(-30), QTime(0, 0));
        Task oldDone; // done long ago: not on the agenda
        oldDone.title = QStringLiteral("Klaret");
        oldDone.start = QDateTime(today.addDays(-20), QTime(0, 0));
        oldDone.status = Task::Status::Done;
        Task future; // deadline ahead, start in the past: not overdue
        future.title = QStringLiteral("I gang");
        future.start = QDateTime(today.addDays(-3), QTime(0, 0));
        future.deadline = QDateTime(today.addDays(3), QTime(0, 0));
        for (const Task &t : {pastStart, oldDone, future})
            QVERIFY(actions.create(t, QStringLiteral("cli")).ok);
        model.reload();

        QVERIFY(isOverdue(pastStart, today));
        QVERIFY(!isOverdue(future, today));
        QCOMPARE(overview.rowCount(), 2);
        QCOMPARE(overview.data(overview.index(0, 0), PlanModel::TitleRole).toString(), QStringLiteral("Glemt"));
        // summary counts overdue tasks from earlier periods too.
        QCOMPARE(actions.summary(true).payload["summary"].toObject()["overdue"].toInt(), 1);
    }

    void planModelReportsStoreError()
    {
        Store store(QStringLiteral("/proc/omaplan-kan-ikke/plan.db"));
        QString error;
        QVERIFY(!store.open(&error));
        PlanModel model(&store);
        QSignalSpy spy(&model, &PlanModel::errorChanged);
        model.setStoreError(error);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.today(), QDate::currentDate());
    }

    void promptContextSkipsOldAndDoneTasks()
    {
        // The command engine runs `cat`, which echoes the prompt; it fails
        // validation and the error carries the raw response = the prompt.
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        const QDate today = QDate::currentDate();
        Task old, done, near;
        old.title = QStringLiteral("Gammel opgave");
        old.start = QDateTime(today.addDays(-40), QTime(0, 0));
        done.title = QStringLiteral("Afsluttet opgave");
        done.start = QDateTime(today, QTime(0, 0));
        done.status = Task::Status::Done;
        near.title = QStringLiteral("Nær opgave");
        near.start = QDateTime(today.addDays(2), QTime(0, 0));
        for (const Task &t : {old, done, near})
            QVERIFY(actions.create(t, QStringLiteral("cli")).ok);

        QTemporaryDir cfgDir;
        const QString cfgPath = cfgDir.filePath("config.toml");
        {
            QFile f(cfgPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[motor]\nengine = \"command\"\ncommand = \"cat\"\n");
        }
        qputenv("OMAPLAN_CONFIG", cfgPath.toLocal8Bit());
        const ActionResult r = actions.propose(QStringLiteral("noget nyt"));
        qputenv("OMAPLAN_CONFIG", "");
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("Nær opgave")), qPrintable(r.error));
        QVERIFY(!r.error.contains(QStringLiteral("Gammel opgave")));
        QVERIFY(!r.error.contains(QStringLiteral("Afsluttet opgave")));
    }

    void configReadsTheReadmeExampleWithComments()
    {
        // The README's own example puts comments after quoted values; they
        // used to end up in the value ("Unknown engine '"vibe"   # ...'").
        QTemporaryDir dir;
        const QString path = dir.filePath("config.toml");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[motor]\nengine = \"claude\"            # standardmotor: vibe | claude\n"
                "compare = \"claude, lokal\"  # sammenlign\n"
                "[motor.lokal]\napi = 'openai' # endpoint-type\nendpoint = http://127.0.0.1:8080 # uden citat\n"
                "model = \"navn#med#hegn\"\n");
        f.close();
        const AppConfig config = AppConfig::load(path);
        QCOMPARE(config.engine, QStringLiteral("claude"));
        QCOMPARE(config.compare, (QStringList{QStringLiteral("claude"), QStringLiteral("lokal")}));
        QCOMPARE(config.localApi, QStringLiteral("openai"));
        QCOMPARE(config.localEndpoint, QStringLiteral("http://127.0.0.1:8080"));
        QCOMPARE(config.localModel, QStringLiteral("navn#med#hegn"));
        // Defaults without a file: vibe, and Sammenlign = vibe vs claude.
        const AppConfig none = AppConfig::load(dir.filePath("findes-ikke.toml"));
        QCOMPARE(none.compare, (QStringList{QStringLiteral("vibe"), QStringLiteral("claude")}));
    }

    // Runs `omaplan mcp` (the real binary) with the given JSON-RPC lines on
    // stdin and returns the response lines from stdout.
    static QList<QJsonObject> mcpSession(const QString &db, const QList<QJsonObject> &messages, QString *stderrText)
    {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OMAPLAN_DB"), db);
        env.insert(QStringLiteral("OMAPLAN_CONFIG"), QFileInfo(db).absolutePath() + QStringLiteral("/config.toml"));
        process.setProcessEnvironment(env);
        process.start(QCoreApplication::applicationDirPath() + QStringLiteral("/../omaplan"), {QStringLiteral("mcp")});
        if (!process.waitForStarted(5000))
            return {};
        for (const QJsonObject &message : messages)
            process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
        process.write("dette er ikke json\n");
        process.closeWriteChannel();
        process.waitForFinished(20000);
        *stderrText = QString::fromUtf8(process.readAllStandardError());
        QList<QJsonObject> responses;
        for (const QByteArray &line : process.readAllStandardOutput().split('\n'))
            if (!line.trimmed().isEmpty())
                responses << QJsonDocument::fromJson(line).object();
        return responses;
    }

    static QJsonObject rpc(int id, const QString &method, const QJsonObject &params = {})
    {
        QJsonObject m{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
                      {QStringLiteral("method"), method}};
        if (!params.isEmpty())
            m.insert(QStringLiteral("params"), params);
        return m;
    }

    static QJsonObject call(int id, const QString &tool, const QJsonObject &arguments = {})
    {
        return rpc(id, QStringLiteral("tools/call"), {{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}});
    }

    void mcpSpeaksTheProtocolOverStdio()
    {
        QTemporaryDir dir;
        const QString db = dir.filePath("mcp.db");
        // Never a real AI: "lokal" without a model fails before any request.
        QFile cfg(dir.filePath("config.toml"));
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[motor]\nengine = \"lokal\"\n");
        cfg.close();
        // Seed one task through the CLI, so the MCP side reads shared data.
        Task seeded;
        {
            Store store(db);
            QString error;
            QVERIFY(store.open(&error));
            Actions actions(&store);
            seeded.title = QStringLiteral("Fra CLI");
            seeded.project = QStringLiteral("Kunde A");
            seeded.id = actions.create(seeded, QStringLiteral("cli")).payload["task"].toObject()["id"].toString();
        }
        const QJsonObject notification{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                       {QStringLiteral("method"), QStringLiteral("notifications/initialized")}};
        QString stderrText;
        const QList<QJsonObject> r = mcpSession(db, {
            rpc(1, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")},
                                                  {QStringLiteral("capabilities"), QJsonObject{}},
                                                  {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("test")}}}}),
            notification,
            rpc(2, QStringLiteral("tools/list")),
            call(3, QStringLiteral("new"), {{QStringLiteral("titel"), QStringLiteral("Skriv blogindlæg")},
                                            {QStringLiteral("start"), QStringLiteral("2026-12-04 14:00")},
                                            {QStringLiteral("varighed"), 60}, {QStringLiteral("projekt"), QStringLiteral("Blog")}}),
            call(4, QStringLiteral("list"), {{QStringLiteral("projekt"), QStringLiteral("Blog")}}),
            call(5, QStringLiteral("done"), {{QStringLiteral("id"), seeded.id}}),
            call(6, QStringLiteral("undo")),
            call(7, QStringLiteral("show"), {{QStringLiteral("id"), QStringLiteral("findes-ikke")}}),
            call(8, QStringLiteral("new"), {{QStringLiteral("titel"), QStringLiteral("X")}, {QStringLiteral("start"), QStringLiteral("engang")}}),
            call(9, QStringLiteral("frobnicate")),
            rpc(10, QStringLiteral("resources/list")),
            call(11, QStringLiteral("summary"), {{QStringLiteral("periode"), QStringLiteral("uge")}}),
            call(12, QStringLiteral("propose"), {{QStringLiteral("tekst"), QStringLiteral("køb mælk")}}),
        }, &stderrText);
        // One answer per request (not for the notification), plus a parse error.
        QCOMPARE(r.size(), 13);

        const QJsonObject init = r[0]["result"].toObject();
        QCOMPARE(r[0]["id"].toInt(), 1);
        QCOMPARE(init["protocolVersion"].toString(), QStringLiteral("2025-03-26")); // the client's version
        QCOMPARE(init["serverInfo"].toObject()["name"].toString(), QStringLiteral("omaplan"));
        QVERIFY(init["capabilities"].toObject().contains(QStringLiteral("tools")));

        QStringList names;
        for (const QJsonValue &t : r[1]["result"].toObject()["tools"].toArray()) {
            names << t.toObject()["name"].toString();
            QCOMPARE(t.toObject()["inputSchema"].toObject()["type"].toString(), QStringLiteral("object"));
        }
        for (const char *expected : {"list", "show", "new", "edit", "move", "done", "undone", "rm", "undo",
                                     "projects", "summary", "propose", "approve", "add"})
            QVERIFY2(names.contains(QString::fromLatin1(expected)), expected);

        const QJsonObject created = r[2]["result"].toObject();
        QCOMPARE(created["isError"].toBool(), false);
        const QJsonObject task = created["structuredContent"].toObject()["task"].toObject();
        QCOMPARE(task["start"].toString(), QStringLiteral("2026-12-04T14:00"));
        QCOMPARE(task["duration_minutes"].toInt(), 60);
        // The text content is the same JSON, for clients without structured output.
        QCOMPARE(QJsonDocument::fromJson(created["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                     .object()["task"].toObject()["id"], task["id"]);

        QCOMPARE(r[3]["result"].toObject()["structuredContent"].toObject()["tasks"].toArray().size(), 1);
        QCOMPARE(r[4]["result"].toObject()["isError"].toBool(), false);
        QCOMPARE(r[5]["result"].toObject()["structuredContent"].toObject()["change"].toObject()["kind"].toString(),
                 QStringLiteral("update")); // undid the done
        // Tool failures are results with isError, protocol failures are errors.
        QCOMPARE(r[6]["result"].toObject()["isError"].toBool(), true);
        QCOMPARE(r[7]["result"].toObject()["isError"].toBool(), true);
        QVERIFY(r[7]["result"].toObject()["content"].toArray()[0].toObject()["text"].toString().contains(QStringLiteral("engang")));
        QCOMPARE(r[8]["error"].toObject()["code"].toInt(), -32602);
        QCOMPARE(r[9]["error"].toObject()["code"].toInt(), -32601);
        QCOMPARE(r[10]["result"].toObject()["isError"].toBool(), false);
        // An engine that is not set up → a clear error, not a hang.
        QCOMPARE(r[11]["result"].toObject()["isError"].toBool(), true);
        QCOMPARE(r[12]["error"].toObject()["code"].toInt(), -32700);

        // Every change went through actions with the source "mcp".
        Store store(db);
        QString error;
        QVERIFY(store.open(&error));
        const auto log = store.changes(10, &error);
        QCOMPARE(log.first().kind, QStringLiteral("undo"));
        QCOMPARE(log.first().source, QStringLiteral("mcp"));
        Task stillTodo;
        QVERIFY(store.task(seeded.id, &stillTodo, &error));
        QCOMPARE(stillTodo.status, Task::Status::Todo);
    }

    void mcpProposeAndApproveWithAFakeEngine()
    {
        QTemporaryDir dir;
        const QString db = dir.filePath("mcp.db");
        const QString fixture = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("../../tests/ai-fixtures/envelopes/command-valid.json"));
        QFile cfg(dir.filePath("config.toml"));
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[motor]\nengine = \"command\"\ncommand = \"cat " + fixture.toUtf8() + "\"\n");
        cfg.close();
        QString stderrText;
        QList<QJsonObject> r = mcpSession(db, {
            call(1, QStringLiteral("propose"), {{QStringLiteral("tekst"), QStringLiteral("køb mælk i morgen")}}),
            call(2, QStringLiteral("list")),
        }, &stderrText);
        QCOMPARE(r.size(), 3);
        const QJsonObject proposed = r[0]["result"].toObject()["structuredContent"].toObject();
        QVERIFY2(!proposed.isEmpty(), qPrintable(r[0]["result"].toObject()["content"].toArray()[0].toObject()["text"].toString()));
        QCOMPARE(r[1]["result"].toObject()["structuredContent"].toObject()["tasks"].toArray().size(), 0); // nothing stored yet

        r = mcpSession(db, {
            call(1, QStringLiteral("approve"), {{QStringLiteral("forslag"), proposed["proposal"]},
                                                {QStringLiteral("capture_id"), proposed["capture_id"]}}),
            call(2, QStringLiteral("add"), {{QStringLiteral("tekst"), QStringLiteral("køb mælk fredag")}}),
            call(3, QStringLiteral("list")),
        }, &stderrText);
        QCOMPARE(r[0]["result"].toObject()["isError"].toBool(), false);
        QCOMPARE(r[1]["result"].toObject()["structuredContent"].toObject()["stored"].toArray().size(), 1);
        QCOMPARE(r[2]["result"].toObject()["structuredContent"].toObject()["tasks"].toArray().size(), 2);
    }

    void compareSendsTheSameTextToTwoEnginesAndKeepsBothCaptures()
    {
        // Fake engines only: "command" runs `cat <fixture>`, and "lokal"
        // without a model fails at once with a clear message.
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        const QString fixture = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("../../tests/ai-fixtures/envelopes/command-valid.json"));
        const QString cfgPath = dir.filePath("config.toml");
        const auto writeConfig = [&](const QByteArray &compare) {
            QFile f(cfgPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[motor]\nengine = \"command\"\ncommand = \"cat " + fixture.toUtf8() + "\"\ncompare = \"" + compare + "\"\n");
        };
        qputenv("OMAPLAN_CONFIG", cfgPath.toLocal8Bit());

        writeConfig("command, command");
        QCOMPARE(actions.guiEngineNames()["compare"].toArray().size(), 2);
        QSignalSpy ready(&actions, &Actions::compareReady);
        QSignalSpy failed(&actions, &Actions::compareFailed);
        actions.guiCompare(QStringLiteral("køb mælk i morgen"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
        QCOMPARE(failed.count(), 0);
        QSet<int> sides{ready.at(0).at(0).toInt(), ready.at(1).at(0).toInt()};
        QCOMPARE(sides, (QSet<int>{0, 1}));
        // Both answers are stored as captures with the same raw text.
        const auto captures = store.captures(&error);
        QCOMPARE(captures.size(), 2);
        QCOMPARE(captures[0].rawText, captures[1].rawText);
        QVERIFY(ready.at(0).at(2).toString() != ready.at(1).at(2).toString());
        // Approving one side stores its tasks, linked to its own capture.
        const QString chosenCapture = ready.at(1).at(2).toString();
        const QJsonObject approved = actions.guiApprove(ready.at(1).at(1).toJsonObject(), chosenCapture);
        QVERIFY(approved["ok"].toBool());
        QCOMPARE(store.tasks(&error).first().captureId, chosenCapture);

        // One side failing does not block the other.
        writeConfig("command, lokal");
        ready.clear();
        failed.clear();
        actions.guiCompare(QStringLiteral("køb mælk i morgen"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(ready.at(0).at(0).toInt(), 0);
        QCOMPARE(failed.at(0).at(0).toInt(), 1);
        QVERIFY(!failed.at(0).at(1).toString().isEmpty());

        // Cancelling (Esc) reports nothing as an error.
        writeConfig("command, command");
        {
            QFile f(cfgPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[motor]\nengine = \"command\"\ncommand = \"sleep 5\"\ncompare = \"command, command\"\n");
        }
        ready.clear();
        failed.clear();
        QSignalSpy single(&actions, &Actions::proposalFailed);
        actions.guiCompare(QStringLiteral("x"));
        actions.guiCancelProposal();
        actions.guiPropose(QStringLiteral("x"));
        actions.guiCancelProposal();
        QTest::qWait(200);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(single.count(), 0);
        qputenv("OMAPLAN_CONFIG", "");
    }

    // Every string the window or the CLI shows has a Danish translation: a
    // missing key shows English in the middle of the Danish window.
    void everyUserStringHasADanishTranslation() {
        const QDir src(QStringLiteral(OMAPLAN_SRC));
        QFile daFile(src.filePath(QStringLiteral("i18n/da.json")));
        QVERIFY(daFile.open(QIODevice::ReadOnly));
        const QJsonObject da = QJsonDocument::fromJson(daFile.readAll()).object();
        QVERIFY(!da.isEmpty());

        // qsTr("...") with escaped quotes allowed inside.
        const QRegularExpression qml(QStringLiteral("qsTr\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
        QStringList missing;
        for (const QString &name : src.entryList({QStringLiteral("*.qml")})) {
            QFile f(src.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(f.readAll());
            for (auto it = qml.globalMatch(text); it.hasNext();) {
                QString key = it.next().captured(1);
                key.replace(QStringLiteral("\\\""), QStringLiteral("\""));
                if (!da.contains(key))
                    missing << name + QStringLiteral(": ") + key;
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QLatin1Char('\n'))));
    }

    void themeReadsOmarchyColorsWithFallbacks()
    {
        // Views take every color from the theme; a theme without a key (or
        // no Omarchy at all) still gives a complete dark palette.
        QTemporaryDir dir;
        Theme fallback(dir.path());
        QCOMPARE(fallback.accent(), QStringLiteral("#FFD60A"));
        QVERIFY(fallback.dark());
        for (const QString &c : {fallback.background(), fallback.surface(), fallback.raised(), fallback.border(),
                                 fallback.foreground(), fallback.muted(), fallback.danger(), fallback.success()})
            QVERIFY2(QColor::isValidColorName(c), qPrintable(c));
        QVERIFY(QColor(fallback.surface()).lightness() > QColor(fallback.background()).lightness());

        QDir().mkpath(dir.filePath("theme"));
        QFile f(dir.filePath("theme/colors.toml"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("mode = \"light\"\naccent = \"#123456\"\nbackground = \"#fafafa\"\nforeground = \"#111111\"\nred = \"#cc0000\"\n");
        f.close();
        Theme light(dir.path());
        QCOMPARE(light.accent(), QStringLiteral("#123456"));
        QVERIFY(!light.dark());
        QCOMPARE(light.background(), QStringLiteral("#fafafa"));
        QCOMPARE(light.danger(), QStringLiteral("#cc0000"));
        QVERIFY(QColor(light.muted()).lightness() > QColor(light.foreground()).lightness());
    }

    void guiHelpersForTheKeyboardViews()
    {
        QTemporaryDir dir;
        Store store(dir.filePath("t.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        PlanModel model(&store);
        PlanListModel list;
        CalendarModel calendar;
        list.setSourceModel(&model);
        calendar.setSourceModel(&model);
        QObject::connect(&actions, &Actions::planChanged, &model, &PlanModel::reload);

        // The editor creates and edits with text fields, dates as in the CLI.
        const QDate monday = QDate(2026, 12, 7);
        QJsonObject r = actions.guiNew({{QStringLiteral("titel"), QStringLiteral("Møde")},
                                        {QStringLiteral("start"), QStringLiteral("2026-12-07 10:00")},
                                        {QStringLiteral("projekt"), QStringLiteral("Kunde A")}});
        QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
        const QString meeting = r["payload"].toObject()["task"].toObject()["id"].toString();
        r = actions.guiNew({{QStringLiteral("titel"), QStringLiteral("Heldag")}, {QStringLiteral("start"), QStringLiteral("2026-12-07")}});
        const QString allDay = r["payload"].toObject()["task"].toObject()["id"].toString();
        r = actions.guiNew({{QStringLiteral("titel"), QStringLiteral("Blog")}, {QStringLiteral("projekt"), QStringLiteral("Blog")}});
        const QString blog = r["payload"].toObject()["task"].toObject()["id"].toString();
        QVERIFY(!actions.guiNew({{QStringLiteral("titel"), QStringLiteral("X")}, {QStringLiteral("frist"), QStringLiteral("snart")}})["ok"].toBool());

        r = actions.guiEdit(meeting, {{QStringLiteral("varighed"), QStringLiteral("90")}, {QStringLiteral("frist"), QStringLiteral("2026-12-08")}});
        QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
        QCOMPARE(actions.guiTask(meeting)["payload"].toObject()["task"].toObject()["duration_minutes"].toInt(), 90);
        QVERIFY(!actions.guiEdit(meeting, {{QStringLiteral("varighed"), QStringLiteral("halvanden time")}})["ok"].toBool());

        // Date preview while typing.
        const QJsonObject parsed = actions.guiParseDate(QStringLiteral("2026-12-09 kl. 14"));
        QVERIFY(!parsed["ok"].toBool()); // ISO with "kl." is not a supported form
        const QJsonObject friday = actions.guiParseDate(QStringLiteral("fredag 10"));
        QVERIFY(friday["ok"].toBool());
        QVERIFY(friday["hasTime"].toBool());
        QCOMPARE(QDateTime::fromString(friday["date"].toString(), QStringLiteral("yyyy-MM-ddTHH:mm")).date().dayOfWeek(), 5);
        QVERIFY(!actions.guiParseDate(QStringLiteral("blabla"))["error"].toString().isEmpty());

        // Calendar order within a day: all-day first, then by time.
        calendar.setRange(monday, monday);
        QCOMPARE(calendar.ids(), (QStringList{allDay, meeting}));
        const QVariantList rows = calendar.rows();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Møde"));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("durationMinutes")).toInt(), 90);
        QVERIFY(model.revision() > 0);
        // List: dated by day, undated last; filters and project grouping.
        QCOMPARE(list.ids(), (QStringList{meeting, allDay, blog}));
        list.setGrouping(QStringLiteral("project"));
        QCOMPARE(list.ids(), (QStringList{blog, meeting, allDay}));
        list.setGrouping(QString());
        QVERIFY(actions.setDone(allDay, true, QStringLiteral("gui")).ok);
        list.setStatusFilter(QStringLiteral("done"));
        QCOMPARE(list.ids(), QStringList{allDay});
        list.setStatusFilter(QStringLiteral("todo"));
        list.setProjectFilter(QStringLiteral("Kunde A"));
        QCOMPARE(list.ids(), QStringList{meeting});
    }

    void dataVersionDetectsOtherConnections()
    {
        // PRAGMA data_version changes only when ANOTHER connection commits,
        // which is what the GUI's live polling relies on.
        QTemporaryDir dir;
        Store a(dir.filePath("test.db"));
        Store b(dir.filePath("test.db"));
        QString error;
        QVERIFY(a.open(&error));
        QVERIFY(b.open(&error));
        bool okA = false, okB = false;
        const int va = a.dataVersion(&okA);
        const int vb = b.dataVersion(&okB);
        QVERIFY(okA && okB);
        QCOMPARE(va, vb);

        // A write from connection A is visible in B's data_version...
        Task t;
        t.title = QStringLiteral("Fra CLI");
        QVERIFY(!a.createTask(t, QStringLiteral("cli"), &error).isEmpty());
        const int vb2 = b.dataVersion(&okB);
        QVERIFY(okB);
        QVERIFY2(vb2 != vb, qPrintable(QStringLiteral("%1 -> %2").arg(vb).arg(vb2)));

        // ...but B's own write does not change B's own data_version.
        QVERIFY(!b.createTask(t, QStringLiteral("gui"), &error).isEmpty());
        const int vb3 = b.dataVersion(&okB);
        QVERIFY(okB);
        QCOMPARE(vb3, vb2);
    }

    void planModelLivePolling()
    {
        // The GUI polls data_version: a commit from another process reloads
        // the model and emits externalChange with the source from `changes`.
        QTemporaryDir dir;
        Store guiStore(dir.filePath("test.db"));
        QString error;
        QVERIFY(guiStore.open(&error));
        PlanModel model(&guiStore);
        model.reload();
        QCOMPARE(model.rowCount(), 0);
        QSignalSpy spy(&model, &PlanModel::externalChange);
        model.startLivePolling(20);

        Store cliStore(dir.filePath("test.db")); // a second process, same file
        QVERIFY(cliStore.open(&error));
        Task t;
        t.title = QStringLiteral("Fra CLI");
        QVERIFY(!cliStore.createTask(t, QStringLiteral("cli"), &error).isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("cli"));
        QCOMPARE(model.data(model.index(0, 0), PlanModel::TitleRole).toString(), QStringLiteral("Fra CLI"));
    }

    void cliCommandsRunHeadlessWithGtkPlatformTheme()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString db = dir.filePath(QStringLiteral("plan.db"));
        const QString binary = QCoreApplication::applicationDirPath() + QStringLiteral("/../omaplan");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORMTHEME"), QStringLiteral("gtk3"));
        env.remove(QStringLiteral("DISPLAY"));
        env.remove(QStringLiteral("WAYLAND_DISPLAY"));
        env.insert(QStringLiteral("OMAPLAN_DB"), db);

        QProcess list;
        list.setProcessEnvironment(env);
        list.start(binary, {QStringLiteral("list"), QStringLiteral("--json")});
        QVERIFY(list.waitForStarted(5000));
        QVERIFY(list.waitForFinished(10000));
        QCOMPARE(list.exitStatus(), QProcess::NormalExit);
        QCOMPARE(list.exitCode(), 0);
        QVERIFY(QJsonDocument::fromJson(list.readAllStandardOutput()).isObject());

        QProcess mcp;
        mcp.setProcessEnvironment(env);
        mcp.start(binary, {QStringLiteral("mcp")});
        QVERIFY(mcp.waitForStarted(5000));
        mcp.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\"}}\n");
        mcp.closeWriteChannel();
        QVERIFY(mcp.waitForFinished(10000));
        QCOMPARE(mcp.exitStatus(), QProcess::NormalExit);
        QCOMPARE(mcp.exitCode(), 0);
        const QJsonObject response = QJsonDocument::fromJson(mcp.readAllStandardOutput().split('\n').first()).object();
        QCOMPARE(response.value(QStringLiteral("id")).toInt(), 1);
        QVERIFY(response.value(QStringLiteral("result")).isObject());
    }

    void skillInstallUninstallInTempHome()
    {
        QTemporaryDir homeDir;
        QVERIFY(homeDir.isValid());
        const QString home = homeDir.path();
        // Two of the candidate agent dirs exist; the others do not.
        QVERIFY(QDir().mkpath(home + QStringLiteral("/.claude/skills")));
        QVERIFY(QDir().mkpath(home + QStringLiteral("/.vibe/skills")));

        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", home.toLocal8Bit());

        // The bundled skill is embedded in the binary.
        QVERIFY(!Skill::markdown().isEmpty());
        QVERIFY(Skill::markdown().contains(QStringLiteral("name: omaplan")));
        QCOMPARE(Skill::candidateRoots(home).size(), 2);

        // Install writes <root>/omaplan/SKILL.md atomically.
        const Skill::Result inst = Skill::install(home);
        QVERIFY2(inst.ok, qPrintable(inst.error));
        QCOMPARE(inst.installed.size(), 2);
        for (const QString &root : inst.installed) {
            QFile f(QDir(root).filePath(QStringLiteral("omaplan/SKILL.md")));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(f.readAll()), Skill::markdown());
        }
        // Installing again is a no-op ("already there").
        const Skill::Result again = Skill::install(home);
        QVERIFY(again.ok);
        QCOMPARE(again.installed.size(), 0);
        QCOMPARE(again.alreadyThere.size(), 2);

        // Customized skill files survive an ordinary install and require --force.
        const QString customizedPath = home + QStringLiteral("/.claude/skills/omaplan/SKILL.md");
        QFile customized(customizedPath);
        QVERIFY(customized.open(QIODevice::WriteOnly | QIODevice::Truncate));
        customized.write("customized skill\n");
        customized.close();
        const Skill::Result conflict = Skill::install(home);
        QVERIFY(!conflict.ok);
        QCOMPARE(conflict.conflicts.size(), 1);
        QVERIFY(conflict.error.contains(QStringLiteral("--force")));
        QVERIFY(customized.open(QIODevice::ReadOnly));
        QCOMPARE(customized.readAll(), QByteArray("customized skill\n"));
        customized.close();
        QString cliOut, cliErr;
        Store cliStore(homeDir.filePath(QStringLiteral("skill-cli.db")));
        QCOMPARE(cli(cliStore, {QStringLiteral("skill"), QStringLiteral("install")}, &cliOut, &cliErr), 1);
        QVERIFY(cliErr.contains(QStringLiteral("--force")));
        QCOMPARE(cli(cliStore, {QStringLiteral("skill"), QStringLiteral("install"), QStringLiteral("--force")},
                     &cliOut, &cliErr), 0);
        const Skill::Result forced = Skill::install(home, true);
        QVERIFY2(forced.ok, qPrintable(forced.error));
        QVERIFY(customized.open(QIODevice::ReadOnly));
        QCOMPARE(customized.readAll(), Skill::markdown().toUtf8());
        customized.close();

        // Uninstall removes the files and the now-empty skill dirs.
        const Skill::Result un = Skill::uninstall(home);
        QVERIFY(un.ok);
        QCOMPARE(un.removed.size(), 2);
        for (const QString &root : un.removed)
            QVERIFY(!QFile::exists(QDir(root).filePath(QStringLiteral("omaplan/SKILL.md"))));
        // Idempotent.
        QVERIFY(Skill::uninstall(home).ok);

        // The agent home exists but skills/ does not: install creates it.
        QTemporaryDir baseOnlyHome;
        QVERIFY(QDir().mkpath(baseOnlyHome.path() + QStringLiteral("/.config/opencode")));
        const Skill::Result created = Skill::install(baseOnlyHome.path());
        QVERIFY2(created.ok, qPrintable(created.error));
        QCOMPARE(created.installed.size(), 1);
        QVERIFY(QFile::exists(baseOnlyHome.path()
                              + QStringLiteral("/.config/opencode/skills/omaplan/SKILL.md")));
        QVERIFY(Skill::uninstall(baseOnlyHome.path()).ok);
        QVERIFY(!QFile::exists(baseOnlyHome.path()
                              + QStringLiteral("/.config/opencode/skills/omaplan/SKILL.md")));

        // No agent dirs at all: clear error, not ok.
        QTemporaryDir emptyHome;
        const Skill::Result none = Skill::install(emptyHome.path());
        QVERIFY(!none.ok);
        QVERIFY(none.error.contains(QStringLiteral("~/.claude")));

        // Through the CLI with the overridden HOME: --json and exit codes.
        Store store(homeDir.filePath(QStringLiteral("cli.db")));
        QString out, err;
        QCOMPARE(cli(store, {QStringLiteral("skill"), QStringLiteral("install"), QStringLiteral("--json")}, &out, &err), 0);
        const QJsonObject obj = QJsonDocument::fromJson(out.toUtf8()).object();
        QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(obj.value(QStringLiteral("installed")).toArray().size(), 2);
        QCOMPARE(cli(store, {QStringLiteral("skill"), QStringLiteral("uninstall"), QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cli(store, {QStringLiteral("skill"), QStringLiteral("bogus")}, &out, &err), 1);
        QVERIFY(err.contains(QStringLiteral("Usage: omaplan skill")));

        qputenv("HOME", oldHome);
    }
};

QTEST_MAIN(OmaplanTests)
#include "omaplan_tests.moc"
