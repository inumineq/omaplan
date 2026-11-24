#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTcpSocket>
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
#include "render.h"
#include "server.h"
#include "snapshot.h"
#include "skill.h"
#include "store.h"
#include "task.h"
#include "theme.h"
#include "uilanguage.h"

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


// The subset of JSON Schema that docs/export-schema.json uses (as web/test.mjs).
static void validateSchema(const QJsonValue &node, const QJsonObject &schema, const QString &at, QStringList *errors) {
    if (schema.contains("const") && node != schema.value("const"))
        *errors << at + ": const";
    if (schema.contains("enum") && !schema.value("enum").toArray().contains(node))
        *errors << at + ": enum";
    if (schema.contains("type")) {
        QJsonArray types = schema.value("type").isArray() ? schema.value("type").toArray() : QJsonArray{schema.value("type")};
        bool ok = false;
        for (const QJsonValue &t : types) {
            const QString type = t.toString();
            ok = ok || (type == "string" && node.isString()) || (type == "null" && node.isNull())
                 || (type == "object" && node.isObject()) || (type == "array" && node.isArray())
                 || (type == "integer" && node.isDouble() && node.toDouble() == std::floor(node.toDouble()));
        }
        if (!ok)
            *errors << at + ": type";
    }
    if (node.isString()) {
        const QString text = node.toString();
        if (schema.contains("minLength") && text.size() < schema.value("minLength").toInt())
            *errors << at + ": minLength";
        if (schema.contains("maxLength") && text.size() > schema.value("maxLength").toInt())
            *errors << at + ": maxLength";
        if (schema.contains("pattern") && !QRegularExpression(schema.value("pattern").toString()).match(text).hasMatch())
            *errors << at + ": pattern " + text;
    }
    if (node.isDouble()) {
        if (schema.contains("minimum") && node.toDouble() < schema.value("minimum").toDouble())
            *errors << at + ": minimum";
        if (schema.contains("maximum") && node.toDouble() > schema.value("maximum").toDouble())
            *errors << at + ": maximum";
    }
    if (node.isObject()) {
        const QJsonObject object = node.toObject();
        for (const QJsonValue &key : schema.value("required").toArray())
            if (!object.contains(key.toString()))
                *errors << at + ": missing " + key.toString();
        const QJsonObject properties = schema.value("properties").toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (properties.contains(it.key()))
                validateSchema(it.value(), properties.value(it.key()).toObject(), at + "." + it.key(), errors);
            else if (schema.value("additionalProperties") == QJsonValue(false))
                *errors << at + ": extra " + it.key();
        }
    }
    if (node.isArray() && schema.contains("items")) {
        const QJsonArray array = node.toArray();
        for (int i = 0; i < array.size(); ++i)
            validateSchema(array[i], schema.value("items").toObject(), at + "[" + QString::number(i) + "]", errors);
    }
}

static QJsonObject readJsonFile(const QString &path) {
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(file.readAll()).object();
}

static QString repoFile(const QString &relative) {
    return QDir(QStringLiteral(OMAPLAN_SRC "/..")).filePath(relative);
}

struct HttpReply {
    int status = 0;
    QMap<QByteArray, QByteArray> headers; // lower-case names
    QByteArray body;
};

// One raw request over loopback. The server lives on this thread, so wait
// with an event loop instead of the blocking waitFor* calls.
static HttpReply http(quint16 port, const QByteArray &request) {
    QTcpSocket socket;
    QByteArray data;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::connected, [&] { socket.write(request); });
    QObject::connect(&socket, &QTcpSocket::readyRead, [&] { data += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    socket.connectToHost(QHostAddress::LocalHost, port);
    loop.exec();
    data += socket.readAll();
    HttpReply reply;
    const int split = data.indexOf("\r\n\r\n");
    const QList<QByteArray> lines = data.left(split).split('\n');
    if (lines.isEmpty() || split < 0)
        return reply;
    reply.status = lines.first().split(' ').value(1).toInt();
    for (int i = 1; i < lines.size(); ++i) {
        const int colon = lines[i].indexOf(':');
        if (colon > 0)
            reply.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }
    reply.body = data.mid(split + 4);
    return reply;
}

static HttpReply httpGet(quint16 port, const QByteArray &target, const QByteArray &method = "GET") {
    return http(port, method + " " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
}

class OmaplanTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_scratch;

private slots:

    // Nothing a test run writes may land in the user's real plan.json: the
    // default path follows the database, but config.toml or the user's
    // environment could point elsewhere.
    void initTestCase() {
        QVERIFY(m_scratch.isValid());
        qputenv("OMAPLAN_SNAPSHOT", m_scratch.filePath(QStringLiteral("plan.json")).toLocal8Bit());
    }

    void snapshotPathPrecedence() {
        QTemporaryDir dir;
        const QString db = dir.filePath(QStringLiteral("omaplan.db"));
        const QByteArray saved = qgetenv("OMAPLAN_SNAPSHOT");
        qputenv("OMAPLAN_SNAPSHOT", "");
        qputenv("OMAPLAN_CONFIG", dir.filePath(QStringLiteral("none.toml")).toLocal8Bit());
        QCOMPARE(Snapshot::path(db), dir.filePath(QStringLiteral("plan.json"))); // beside the database
        QCOMPARE(Snapshot::path(QStringLiteral(":memory:")), QString());
        {
            QFile cfg(dir.filePath(QStringLiteral("c.toml")));
            QVERIFY(cfg.open(QIODevice::WriteOnly));
            cfg.write("[snapshot]\npath = \"~/x/plan.json\"  # comment\n");
        }
        qputenv("OMAPLAN_CONFIG", dir.filePath(QStringLiteral("c.toml")).toLocal8Bit());
        QCOMPARE(Snapshot::path(db), QDir::homePath() + QStringLiteral("/x/plan.json"));
        qputenv("OMAPLAN_SNAPSHOT", "/elsewhere/plan.json");
        QCOMPARE(Snapshot::path(db), QStringLiteral("/elsewhere/plan.json"));
        qputenv("OMAPLAN_CONFIG", "");
        qputenv("OMAPLAN_SNAPSHOT", saved);
    }

    // Every change rewrites plan.json, valid against docs/export-schema.json.
    void snapshotFollowsSchemaAfterEveryChange() {
        QTemporaryDir dir;
        Store store(dir.filePath(QStringLiteral("t.db")));
        QVERIFY(store.open());
        Actions actions(&store);
        const QString path = dir.filePath(QStringLiteral("out/plan.json")); // the directory is created
        Snapshotter snapshotter(&store, path);
        QVERIFY(!QFile::exists(path));

        Task timed;
        timed.id = Task::newId();
        timed.title = QStringLiteral("Meeting with Client A <at 9>");
        timed.project = QStringLiteral("Client A");
        timed.priority = 1;
        timed.start = QDateTime(QDate(2026, 11, 24), QTime(9, 0));
        timed.startHasTime = true;
        timed.durationMinutes = 90;
        timed.deadline = QDateTime(QDate(2026, 11, 27), QTime(12, 0));
        timed.deadlineHasTime = true;
        timed.guesses = {QStringLiteral("duration_minutes"), QStringLiteral("deadline")};
        QVERIFY(actions.create(timed, QStringLiteral("cli")).ok);
        QVERIFY(QFile::exists(path));

        Task undated;
        undated.id = Task::newId();
        undated.title = QStringLiteral("Tidy up notes");
        undated.notes = QStringLiteral("When there is time");
        QVERIFY(actions.create(undated, QStringLiteral("cli")).ok);
        Task allDay;
        allDay.id = Task::newId();
        allDay.title = QStringLiteral("Blog");
        allDay.start = QDateTime(QDate(2026, 11, 25), QTime(0, 0));
        QVERIFY(actions.create(allDay, QStringLiteral("cli")).ok);
        QVERIFY(actions.setDone(allDay.id, true, QStringLiteral("cli")).ok);

        const QJsonObject plan = readJsonFile(path);
        QStringList errors;
        validateSchema(plan, readJsonFile(repoFile(QStringLiteral("docs/export-schema.json"))), QStringLiteral("$"), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(plan.value("version").toInt(), 2);
        QCOMPARE(plan.value("language").toString(), QStringLiteral("en"));
        QCOMPARE(plan.value("tasks").toArray().size(), 3);
        QHash<QString, QJsonObject> byId;
        for (const QJsonValue &v : plan.value("tasks").toArray())
            byId.insert(v.toObject().value("id").toString(), v.toObject());
        const QJsonObject t = byId.value(timed.id);
        QCOMPARE(t.value("start").toString(), QStringLiteral("2026-11-24T09:00"));
        QCOMPARE(t.value("deadline").toString(), QStringLiteral("2026-11-27T12:00"));
        QCOMPARE(t.value("duration_min").toInt(), 90);
        QCOMPARE(t.value("guessed").toArray(), (QJsonArray{QStringLiteral("duration_min"), QStringLiteral("deadline")}));
        QVERIFY(byId.value(undated.id).value("start").isNull());
        QCOMPARE(byId.value(undated.id).value("notes").toString(), QStringLiteral("When there is time"));
        QCOMPARE(byId.value(allDay.id).value("start").toString(), QStringLiteral("2026-11-25"));
        QCOMPARE(byId.value(allDay.id).value("status").toString(), QStringLiteral("done"));

        // Undo is a change too.
        QVERIFY(actions.undo(QStringLiteral("cli")).ok);
        bool stillDone = false;
        for (const QJsonValue &v : readJsonFile(path).value("tasks").toArray())
            stillDone = stillDone || (v.toObject().value("id") == allDay.id && v.toObject().value("status") == "done");
        QVERIFY(!stillDone);

        // The language field follows the app's language.
        snapshotter.setLanguage(QStringLiteral("da"));
        QVERIFY(actions.setDone(allDay.id, true, QStringLiteral("cli")).ok);
        QCOMPARE(readJsonFile(path).value("language").toString(), QStringLiteral("da"));
        QCOMPARE(Snapshot::build({}, QDateTime::currentDateTime(), {}, QStringLiteral("da")).value("language").toString(),
                 QStringLiteral("da"));
    }

    // One write per change, however many rows the change touches.
    void snapshotOncePerCommit() {
        QTemporaryDir dir;
        Store store(dir.filePath(QStringLiteral("t.db")));
        QVERIFY(store.open());
        QSignalSpy spy(&store, &Store::committed);
        QVERIFY(store.begin());
        for (int i = 0; i < 3; ++i) {
            Task task;
            task.id = Task::newId();
            task.title = QStringLiteral("Task %1").arg(i);
            QVERIFY(!store.createTask(task, QStringLiteral("cli")).isEmpty()); // nested transactions
        }
        QCOMPARE(spy.count(), 0);
        QVERIFY(store.commit());
        QCOMPARE(spy.count(), 1);
        QVERIFY(store.begin());
        store.rollback();
        QCOMPARE(spy.count(), 1);
    }

    // Atomic: the old file stays whole when a write fails, and nothing is
    // left beside it after a successful one.
    void snapshotWriteIsAtomic() {
        QTemporaryDir dir;
        Store store(dir.filePath(QStringLiteral("t.db")));
        QVERIFY(store.open());
        const QString path = dir.filePath(QStringLiteral("snap/plan.json"));
        QVERIFY(Snapshot::write(&store, path));
        Task task;
        task.id = Task::newId();
        task.title = QStringLiteral("First");
        QVERIFY(!store.createTask(task, QStringLiteral("cli")).isEmpty());
        QVERIFY(Snapshot::write(&store, path));
        QCOMPARE(QDir(dir.filePath(QStringLiteral("snap"))).entryList(QDir::Files | QDir::Hidden),
                 QStringList{QStringLiteral("plan.json")});
        QFile before(path);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray old = before.readAll();
        before.close();

        // A read-only directory: QSaveFile cannot create its temporary file.
        QFile::setPermissions(dir.filePath(QStringLiteral("snap")), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        task.id = Task::newId();
        task.title = QStringLiteral("Second");
        QVERIFY(!store.createTask(task, QStringLiteral("cli")).isEmpty());
        QString error;
        QVERIFY(!Snapshot::write(&store, path, &error));
        QVERIFY(!error.isEmpty());
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), old);
        QFile::setPermissions(dir.filePath(QStringLiteral("snap")), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        // The CLI command writes to a given path and says where.
        QString out, err;
        const QString other = dir.filePath(QStringLiteral("other.json"));
        QCOMPARE(cli(store, {QStringLiteral("snapshot"), other, QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(QJsonDocument::fromJson(out.toUtf8()).object().value("path").toString(), other);
        QCOMPARE(readJsonFile(other).value("tasks").toArray().size(), 2);
    }

    void snapshotThemeAsCssVariables() {
        QTemporaryDir dir;
        QVERIFY(Snapshot::omarchyTheme(dir.path()).isEmpty()); // no Omarchy: no theme
        QDir(dir.path()).mkpath(QStringLiteral("theme"));
        QFile colors(dir.filePath(QStringLiteral("theme/colors.toml")));
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#7aa2f7\"\nbackground = \"#1a1b26\"\nforeground = \"#c0caf5\"\n");
        colors.close();
        const QJsonObject theme = Snapshot::omarchyTheme(dir.path());
        QCOMPARE(theme.value("accent").toString(), QStringLiteral("#7aa2f7"));
        QCOMPARE(theme.value("bg").toString(), QStringLiteral("#1a1b26"));
        QCOMPARE(theme.size(), 9);
        const QString style = Snapshot::themeStyle(theme);
        QVERIFY(style.contains(QStringLiteral("--accent:#7aa2f7")));
        QVERIFY(style.contains(QStringLiteral("--bg:#1a1b26")));
        // Anything but #rrggbb stays out of the style attribute.
        QCOMPARE(Snapshot::themeStyle(QJsonObject{{"bg", "red;}</style>"}, {"fg", "#FFFFFF"}}), QStringLiteral("--fg:#ffffff"));
    }

    // The C++ rendering is the JS reference rendering, byte for byte, in
    // every language (web/test.mjs writes the references from app.js).
    void renderMatchesWebReference_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("reference");
        QTest::newRow("en") << QStringLiteral("en") << QStringLiteral("web/index.html");
        QTest::newRow("da") << QStringLiteral("da") << QStringLiteral("web/index.da.html");
    }
    void renderMatchesWebReference() {
        QFETCH(QString, language);
        QFETCH(QString, reference);
        const QJsonObject plan = readJsonFile(repoFile(QStringLiteral("web/example/plan.json")));
        QVERIFY(!plan.isEmpty());
        QFile file(repoFile(reference));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString expected = QString::fromUtf8(file.readAll());
        const QString actual = Render::page(plan, QDate(2026, 11, 23), language);
        if (actual != expected) {
            const QStringList a = actual.split('\n'), e = expected.split('\n');
            for (int i = 0; i < std::max(a.size(), e.size()); ++i)
                if (a.value(i) != e.value(i))
                    QFAIL(qPrintable(QStringLiteral("line %1:\n  C++: %2\n  JS:  %3").arg(i + 1).arg(a.value(i), e.value(i))));
        }
        QCOMPARE(actual, expected);
    }

    void serveOverLoopback() {
        QTemporaryDir dir;
        const QString snapshot = dir.filePath(QStringLiteral("plan.json"));
        PlanServer::Options options;
        options.snapshotPath = snapshot;
        options.port = 0; // any free port
        options.themeDirectory = dir.filePath(QStringLiteral("no-theme"));
        options.today = [] { return QDate(2026, 11, 23); };
        PlanServer server(options);
        QVERIFY(server.listen());
        const quint16 port = server.port();

        auto get = [port](const QByteArray &target, const QByteArray &acceptLanguage = {}) {
            return http(port, "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n"
                                  + (acceptLanguage.isEmpty() ? QByteArray() : "Accept-Language: " + acceptLanguage + "\r\n")
                                  + "\r\n");
        };

        // No snapshot yet: a friendly page, not an error, in the browser's language.
        HttpReply r = httpGet(port, "/");
        QCOMPARE(r.status, 200);
        QVERIFY(r.body.contains("No plan yet"));
        QVERIFY(get("/", "da").body.contains("Ingen plan endnu"));
        QCOMPARE(httpGet(port, "/plan.json").status, 404);

        QVERIFY(QFile::copy(repoFile(QStringLiteral("web/example/plan.json")), snapshot));
        r = httpGet(port, "/");
        QCOMPARE(r.status, 200);
        QCOMPARE(r.headers.value("content-type"), QByteArray("text/html; charset=utf-8"));
        QCOMPARE(r.headers.value("cache-control"), QByteArray("no-cache"));
        QCOMPARE(r.headers.value("vary"), QByteArray("Accept-Language"));
        QCOMPARE(r.headers.value("content-length").toInt(), r.body.size());
        // Theme from plan.json when this machine has none.
        QVERIFY(r.body.contains("<html lang=\"en\" style=\"--bg:#0e0e10;"));
        const QByteArray expectedElements[] = {
            "<main id=\"plan\" class=\"plan\" data-today=\"2026-11-23\">",
            // Overview: overdue first, today, empty days
            "<section id=\"overview\" class=\"view\" aria-label=\"Overview\">",
            "<section class=\"day overdue\" data-date=\"2026-11-18\">",
            "<span class=\"badge badge-overdue\">overdue</span>",
            "<section class=\"day today\" data-date=\"2026-11-23\">",
            "<span class=\"badge badge-today\">today</span>",
            "<li class=\"task status-todo\"",
            "<span class=\"time time-all-day\">all day</span>",
            // List by date, No date last
            "<section id=\"list\" class=\"view\" aria-label=\"List by date\">",
            "data-group=\"date:2026-11-18\"",
            "<section class=\"group\" data-group=\"no-date\">",
            "<span class=\"time time-none\">no date</span>",
            // List by project
            "<section id=\"list-project\" class=\"view\" aria-label=\"List by project\">",
            "data-group=\"project:Blog\"",
            "<span class=\"group-title\">No project</span>",
            // Timeline
            "<section id=\"timeline\" class=\"view\" aria-label=\"Timeline\">",
            "<div class=\"gantt\" style=\"--from:8;--to:18\">",
            "<section class=\"gantt-day today\" data-date=\"2026-11-23\">",
            "<div class=\"bar bar-all-day\"",
            "class=\"bar-label\"",
            "class=\"deadline-mark\"",
            "<a href=\"#timeline\">Timeline</a>",
        };
        for (const QByteArray &element : expectedElements)
            QVERIFY2(r.body.contains(element), element.constData());
        const QByteArray body = r.body;
        QVERIFY(body.indexOf("data-group=\"no-date\"") > body.lastIndexOf("data-group=\"date:"));
        QVERIFY(body.indexOf("day overdue") < body.indexOf("day today"));

        // The page language follows Accept-Language, and each rendering is the
        // JS reference rendering of that language (web/index*.html).
        auto withoutStyle = [](const QByteArray &html) {
            static const QRegularExpression style(QStringLiteral("^(<!doctype html>\\n<html lang=\"\\w+\") style=\"[^\"]*\">"));
            return QString::fromUtf8(html).replace(style, QStringLiteral("\\1>"));
        };
        auto reference = [](const char *file) {
            QFile f(repoFile(QString::fromLatin1(file)));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        QCOMPARE(withoutStyle(body), reference("web/index.html"));
        const QByteArray danish = get("/", "da").body;
        QVERIFY(danish.contains("<html lang=\"da\""));
        QVERIFY(danish.contains("<a href=\"#overview\">Overblik</a>"));
        QCOMPARE(withoutStyle(danish), reference("web/index.da.html"));
        const struct {
            QByteArray header;
            QByteArray lang;
        } negotiation[] = {
            {"da-DK,da;q=0.9,en;q=0.8", "da"},
            {"en-US,en;q=0.9,da;q=0.8", "en"},
            {"fr-FR, da;q=0.5", "da"},       // the first supported language
            {"en;q=0.3, da-DK;q=0.7", "da"}, // by q, not by order
            {"da;q=0, en", "en"},            // q=0 means not acceptable
            {"fr, de", "en"},                // nothing supported: the default
        };
        for (const auto &n : negotiation)
            QVERIFY2(get("/", n.header).body.contains("<html lang=\"" + n.lang + "\""), n.header.constData());
        // The fallback when the header names no supported language.
        options.language = QStringLiteral("da");
        PlanServer danishServer(options);
        QCOMPARE(danishServer.language(""), QStringLiteral("da"));
        QCOMPARE(danishServer.language("fr"), QStringLiteral("da"));
        QCOMPARE(danishServer.language("en-GB"), QStringLiteral("en"));
        QVERIFY(danishServer.respond("GET", "/").body.contains("<html lang=\"da\""));

        // A view chosen by link without JavaScript.
        r = httpGet(port, "/?view=timeline");
        QVERIFY(r.body.contains("<section id=\"timeline\" class=\"view active\""));
        QVERIFY(r.body.contains("<section id=\"overview\" class=\"view\""));
        QVERIFY(!httpGet(port, "/?view=<x>").body.contains("active\""));

        r = httpGet(port, "/plan.json");
        QCOMPARE(r.status, 200);
        QCOMPARE(r.headers.value("content-type"), QByteArray("application/json; charset=utf-8"));
        QCOMPARE(QJsonDocument::fromJson(r.body).object().value("format").toString(), QStringLiteral("omaplan-plan"));
        r = httpGet(port, "/style.css");
        QCOMPARE(r.status, 200);
        QCOMPARE(r.headers.value("content-type"), QByteArray("text/css; charset=utf-8"));
        QVERIFY(r.body.contains(".gantt"));
        r = httpGet(port, "/app.js");
        QCOMPARE(r.headers.value("content-type"), QByteArray("text/javascript; charset=utf-8"));
        QVERIFY(r.body.contains("renderPage"));
        // The dictionaries app.js fetches, the same files the server renders from.
        for (const char *lang : Render::kLanguages) {
            r = httpGet(port, "/i18n/" + QByteArray(lang) + ".json");
            QCOMPARE(r.status, 200);
            QCOMPARE(r.headers.value("content-type"), QByteArray("application/json; charset=utf-8"));
            QCOMPARE(QJsonDocument::fromJson(r.body).object().value("language").toString(), QString::fromLatin1(lang));
        }
        QCOMPARE(httpGet(port, "/i18n/fr.json").status, 404);

        // HEAD: headers only.
        r = httpGet(port, "/", "HEAD");
        QCOMPARE(r.status, 200);
        QVERIFY(r.body.isEmpty());
        QVERIFY(r.headers.value("content-length").toInt() > 1000);

        // Read-only: everything but GET/HEAD is 405.
        for (const QByteArray method : {QByteArray("POST"), QByteArray("PUT"), QByteArray("DELETE")}) {
            r = httpGet(port, "/", method);
            QCOMPARE(r.status, 405);
            QCOMPARE(r.headers.value("allow"), QByteArray("GET, HEAD"));
        }
        // No way out of the routes.
        QCOMPARE(httpGet(port, "/../plan.json").status, 400);
        QCOMPARE(httpGet(port, "/%2e%2e/%2e%2e/etc/passwd").status, 400);
        QCOMPARE(httpGet(port, "/example/plan.json").status, 404);
        QCOMPARE(httpGet(port, "/nope").status, 404);
        QCOMPARE(http(port, "GARBAGE\r\n\r\n").status, 400);
        QCOMPARE(http(port, "GET / HTTP/1.1\r\nX: " + QByteArray(PlanServer::kMaxRequestBytes + 10, 'a') + "\r\n\r\n").status, 431);

        // A broken snapshot gives a page that says so.
        QFile broken(snapshot);
        QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
        broken.write("{not json");
        broken.close();
        r = httpGet(port, "/");
        QCOMPARE(r.status, 500);
        QVERIFY(r.body.contains("The plan could not be read"));
        QVERIFY(get("/", "da").body.contains("Planen kunne ikke"));
    }
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

    // A database as schema v1 left it: no undo_of column.
    static bool makeV1Database(const QString &path)
    {
        bool ok = true;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1test"));
            db.setDatabaseName(path);
            ok = ok && (db.open());
            QSqlQuery q(db);
            ok = ok && (q.exec(QStringLiteral("CREATE TABLE tasks (id TEXT PRIMARY KEY, title TEXT NOT NULL,"
                                          " notes TEXT NOT NULL DEFAULT '', project TEXT NOT NULL DEFAULT '',"
                                          " priority INTEGER NOT NULL DEFAULT 3, status TEXT NOT NULL DEFAULT 'todo',"
                                          " start TEXT, start_has_time INTEGER NOT NULL DEFAULT 0,"
                                          " duration_min INTEGER NOT NULL DEFAULT 0, deadline TEXT,"
                                          " deadline_has_time INTEGER NOT NULL DEFAULT 0,"
                                          " guesses TEXT NOT NULL DEFAULT '[]', capture_id TEXT NOT NULL DEFAULT '',"
                                          " created TEXT NOT NULL, modified TEXT NOT NULL)")));
            ok = ok && (q.exec(QStringLiteral("CREATE TABLE captures (id TEXT PRIMARY KEY, raw_text TEXT NOT NULL,"
                                          " engine TEXT NOT NULL DEFAULT '', model TEXT NOT NULL DEFAULT '',"
                                          " response TEXT NOT NULL DEFAULT '', created TEXT NOT NULL)")));
            ok = ok && (q.exec(QStringLiteral("CREATE TABLE changes (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                          " at TEXT NOT NULL, source TEXT NOT NULL, kind TEXT NOT NULL,"
                                          " task_id TEXT NOT NULL, before_json TEXT, after_json TEXT)")));
            ok = ok && (q.exec(QStringLiteral("PRAGMA user_version=1")));
            q.finish();
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(QStringLiteral("v1test"));
        }
        return ok;
    }

    void migrationFromV1Database()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("v1.db");
        QVERIFY(makeV1Database(path));
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

    // Starts the real binary on `db` without waiting for it.
    static QProcess *startOmaplan(const QString &db, const QStringList &args, QObject *parent) {
        auto *process = new QProcess(parent);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OMAPLAN_DB"), db);
        env.insert(QStringLiteral("OMAPLAN_CONFIG"), QFileInfo(db).absolutePath() + QStringLiteral("/config.toml"));
        env.insert(QStringLiteral("OMAPLAN_SNAPSHOT"), QFileInfo(db).absolutePath() + QStringLiteral("/plan.json"));
        process->setProcessEnvironment(env);
        process->start(QCoreApplication::applicationDirPath() + QStringLiteral("/../omaplan"), args);
        return process;
    }

    // The window and an `omaplan mcp` often open a new or older database at
    // the same moment. Only one of them may migrate; the others must see the
    // finished schema instead of failing half-way and leaving user_version
    // behind the columns (which made every later open fail).
    void concurrentFirstOpenMigratesOnce_data()
    {
        QTest::addColumn<bool>("v1");
        QTest::newRow("fresh") << false;
        QTest::newRow("from v1") << true;
    }

    void concurrentFirstOpenMigratesOnce()
    {
        QFETCH(bool, v1);
        for (int round = 0; round < 3; ++round) {
            QTemporaryDir dir;
            const QString db = dir.filePath(QStringLiteral("race.db"));
            if (v1)
                QVERIFY(makeV1Database(db));
            QObject owner;
            QList<QProcess *> processes;
            for (int i = 0; i < 12; ++i)
                processes << startOmaplan(db, {QStringLiteral("new"), QStringLiteral("--title"),
                                               QStringLiteral("Task %1").arg(i)}, &owner);
            for (QProcess *process : processes) {
                QVERIFY(process->waitForFinished(30000));
                QVERIFY2(process->exitCode() == 0, process->readAllStandardError().constData());
            }
            int code = -1;
            const QString listed = runOmaplan(db, {QStringLiteral("list"), QStringLiteral("--json")}, &code);
            QVERIFY2(code == 0, qPrintable(listed));
            QCOMPARE(QJsonDocument::fromJson(listed.toUtf8()).object()[QStringLiteral("tasks")].toArray().size(), 12);
            Store store(db);
            QVERIFY(store.open());
            QCOMPARE(store.schemaVersion(), 5);
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
        const QJsonObject created = cliJson(store, {QStringLiteral("new"), QStringLiteral("--title"),
                                                     QStringLiteral("Test opgave"), QStringLiteral("--project"),
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
        QCOMPARE(cli(store, {QStringLiteral("new"), QStringLiteral("--title"), QStringLiteral("Anden opgave")}, &out, &err), 0);
        QVERIFY(out.contains(QStringLiteral("Created")));

        // list --json, show, edit, move, done, undone, rm, undo.
        const QJsonObject listed = cliJson(store, {QStringLiteral("list"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(listed["tasks"].toArray().size(), 2);

        const QJsonObject shown = cliJson(store, {QStringLiteral("show"), id, QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(shown["task"].toObject()["id"].toString(), id);

        const QJsonObject edited = cliJson(store, {QStringLiteral("edit"), id, QStringLiteral("--title"),
                                                    QStringLiteral("Rettet"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(edited["task"].toObject()["title"].toString(), QStringLiteral("Rettet"));

        const QJsonObject moved = cliJson(store, {QStringLiteral("move"), id, QStringLiteral("2026-12-02 10:00"),
                                                   QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        // --json dates follow docs/ai-schema.json: no milliseconds.
        QCOMPARE(moved["task"].toObject()["start"].toString(), QStringLiteral("2026-12-02T10:00"));
        QCOMPARE(moved["task"].toObject()["start_has_time"].toBool(), true);

        QCOMPARE(cli(store, {QStringLiteral("done"), id, QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cliJson(store, {QStringLiteral("show"), id, QStringLiteral("--json")}, &err, &code)["task"].toObject()["status"].toString(),
                 QStringLiteral("done"));
        QCOMPARE(cli(store, {QStringLiteral("undone"), id}, &out, &err), 0);

        // list with filters.
        const QJsonObject filtered = cliJson(store, {QStringLiteral("list"), QStringLiteral("--project"), QStringLiteral("Blog"),
                                                      QStringLiteral("--status"), QStringLiteral("todo"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(filtered["tasks"].toArray().size(), 1);

        // projects and summary work over --json.
        QCOMPARE(cli(store, {QStringLiteral("projects"), QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cli(store, {QStringLiteral("summary"), QStringLiteral("--week"), QStringLiteral("--json")}, &out, &err), 0);
        QCOMPARE(cli(store, {QStringLiteral("summary"), QStringLiteral("--month")}, &out, &err), 0);

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
        QCOMPARE(cli(store, {QStringLiteral("new")}, &out, &err), 1); // missing --title
        QCOMPARE(cli(store, {QStringLiteral("frobnicate")}, &out, &err), 1);
        QCOMPARE(cli(store, {QStringLiteral("new"), QStringLiteral("--title"), QStringLiteral("X"), QStringLiteral("--start"), QStringLiteral("bogus")}, &out, &err), 1);

        // serve rejects bad arguments before it listens.
        QCOMPARE(cli(store, {QStringLiteral("serve"), QStringLiteral("--port"), QStringLiteral("abc")}, &out, &err), 1);
        QCOMPARE(cli(store, {QStringLiteral("serve"), QStringLiteral("--bind"), QStringLiteral("ikke-en-adresse")}, &out, &err), 1);
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
            f.write("[engine]\ndefault = \"command\"\ncommand = \"cat " + fixture.toUtf8() + "\"\n");
        }
        qputenv("OMAPLAN_CONFIG", cfgPath.toLocal8Bit());

        QString out, err;
        int code = 0;
        // Without --yes the proposal is printed, nothing stored.
        const QJsonObject proposal = cliJson(store, {QStringLiteral("add"), QStringLiteral("køb mælk i morgen"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(proposal["proposal"].toObject()["tasks"].toArray().size(), 1);
        QCOMPARE(proposal["proposal"].toObject()["tasks"].toArray().first().toObject()["title"].toString(),
                 QStringLiteral("Køb mælk"));
        QVERIFY(!proposal["capture_id"].toString().isEmpty());
        QCOMPARE(cliJson(store, {QStringLiteral("list"), QStringLiteral("--json")}, &err, &code)["tasks"].toArray().size(), 0);
        // The capture keeps raw text, annotated text, engine and model.
        const auto captures = store.captures(&error);
        QCOMPARE(captures.size(), 1);
        QCOMPARE(captures[0].rawText, QStringLiteral("køb mælk i morgen"));
        const QDate tomorrow = QDate::currentDate().addDays(1);
        QVERIFY(captures[0].annotatedText.contains(
            QStringLiteral("i morgen [%1 %2]")
                .arg(QLocale(QLocale::English).dayName(tomorrow.dayOfWeek()), tomorrow.toString(Qt::ISODate))));
        QCOMPARE(captures[0].engine, QStringLiteral("command"));

        // With --yes the tasks are stored and linked to the capture.
        const QJsonObject stored = cliJson(store, {QStringLiteral("add"), QStringLiteral("køb mælk fredag"), QStringLiteral("--yes"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(stored["stored"].toArray().size(), 1);
        const auto tasks = cliJson(store, {QStringLiteral("list"), QStringLiteral("--json")}, &err, &code)["tasks"].toArray();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().toObject()["capture_id"].toString(), stored["capture_id"].toString());
        // The schema's guessed maps to Task guess keys, under the same name.
        QCOMPARE(tasks.first().toObject()["guessed"].toArray().first().toString(), QStringLiteral("project"));
        QVERIFY(!tasks.first().toObject().contains(QStringLiteral("guesses")));

        // Unknown engine: exit 1 with a clear error.
        err.clear();
        QCOMPARE(cli(store, {QStringLiteral("add"), QStringLiteral("x"), QStringLiteral("--engine"), QStringLiteral("ingen")}, &out, &err), 1);
        QVERIFY(err.contains(QStringLiteral("Unknown engine")));

        qputenv("OMAPLAN_CONFIG", "");
    }

    void dateAnnotation()
    {
        const QDate today(2026, 11, 23); // Monday
        // Danish input: weekdays, key words, day + month, times, halv/kvart.
        QCOMPARE(annotateDates(QStringLiteral("podcast-klip til linkedin tirsdag"), today),
                 QStringLiteral("podcast-klip til linkedin tirsdag [Tuesday 2026-11-24]"));
        // A weekday that is today (Monday) means next Monday.
        QCOMPARE(annotateDates(QStringLiteral("til mandag"), today),
                 QStringLiteral("til mandag [Monday 2026-11-30]"));
        // Longest phrase wins: "i morgen kl. 14:30" is annotated as one unit.
        QCOMPARE(annotateDates(QStringLiteral("møde i morgen kl. 14:30"), today),
                 QStringLiteral("møde i morgen kl. 14:30 [Tuesday 2026-11-24 14:30]"));
        QCOMPARE(annotateDates(QStringLiteral("frist fredag 12"), today),
                 QStringLiteral("frist fredag 12 [Friday 2026-11-27 12:00]"));
        QCOMPARE(annotateDates(QStringLiteral("deadline den 12. december"), today),
                 QStringLiteral("deadline den 12. december [Saturday 2026-12-12]"));
        QCOMPARE(annotateDates(QStringLiteral("næste uge"), today),
                 QStringLiteral("næste uge [Monday 2026-11-30]"));
        QCOMPARE(annotateDates(QStringLiteral("om 3 dage"), today),
                 QStringLiteral("om 3 dage [Thursday 2026-11-26]"));
        // "halv tre" is half an hour before three: 14:30.
        QCOMPARE(annotateDates(QStringLiteral("halv tre"), today),
                 QStringLiteral("halv tre [14:30]"));
        QCOMPARE(annotateDates(QStringLiteral("kvart over fire"), today),
                 QStringLiteral("kvart over fire [16:15]"));
        QCOMPARE(annotateDates(QStringLiteral("kvart i tre"), today),
                 QStringLiteral("kvart i tre [14:45]"));
        // Bare numbers (durations) are not annotated.
        QCOMPARE(annotateDates(QStringLiteral("research 3 timer onsdag"), today),
                 QStringLiteral("research 3 timer onsdag [Wednesday 2026-11-25]"));
        // Text without dates is left alone.
        QCOMPARE(annotateDates(QStringLiteral("skriv noget"), today),
                 QStringLiteral("skriv noget"));
    }

    void dateAnnotationInEnglish()
    {
        const QDate today(2026, 11, 23); // Monday
        QCOMPARE(annotateDates(QStringLiteral("buy milk today"), today),
                 QStringLiteral("buy milk today [Monday 2026-11-23]"));
        QCOMPARE(annotateDates(QStringLiteral("call the client tomorrow at 14"), today),
                 QStringLiteral("call the client tomorrow at 14 [Tuesday 2026-11-24 14:00]"));
        QCOMPARE(annotateDates(QStringLiteral("blog post on Friday"), today),
                 QStringLiteral("blog post on Friday [Friday 2026-11-27]"));
        QCOMPARE(annotateDates(QStringLiteral("review next Monday 9:30"), today),
                 QStringLiteral("review next Monday 9:30 [Monday 2026-11-30 09:30]"));
        QCOMPARE(annotateDates(QStringLiteral("plan the trip next week"), today),
                 QStringLiteral("plan the trip next week [Monday 2026-11-30]"));
        QCOMPARE(annotateDates(QStringLiteral("send the report in 3 days"), today),
                 QStringLiteral("send the report in 3 days [Thursday 2026-11-26]"));
        QCOMPARE(annotateDates(QStringLiteral("dentist the day after tomorrow"), today),
                 QStringLiteral("dentist the day after tomorrow [Wednesday 2026-11-25]"));
        // "half past three" is 15:30 -- not the Danish "halv tre".
        QCOMPARE(annotateDates(QStringLiteral("meet half past three"), today),
                 QStringLiteral("meet half past three [15:30]"));
        QCOMPARE(annotateDates(QStringLiteral("quarter to five"), today),
                 QStringLiteral("quarter to five [16:45]"));
        QCOMPARE(annotateDates(QStringLiteral("write for 2 hours"), today),
                 QStringLiteral("write for 2 hours"));
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
        QVERIFY(Actions::parseDateTime(QStringLiteral("this weekend"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 28));
        QVERIFY(Actions::parseDateTime(QStringLiteral("the day after tomorrow"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 11, 25));
        QVERIFY(Actions::parseDateTime(QStringLiteral("12th"), today, &out, &hasTime, &error));
        QCOMPARE(out.date(), QDate(2026, 12, 12));
        QVERIFY(Actions::parseDateTime(QStringLiteral("friday the 27th at 10:15"), today, &out, &hasTime, &error));
        QCOMPARE(out, QDateTime(QDate(2026, 11, 27), QTime(10, 15)));
        QVERIFY(hasTime);
        QVERIFY(!Actions::parseDateTime(QStringLiteral("friday the 28th"), today, &out, &hasTime, &error));
    }

    void jsonDatesHaveNoMilliseconds()
    {
        // docs/ai-schema.json: YYYY-MM-DD or YYYY-MM-DDTHH:MM — never ms.
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
        const QJsonObject good = QJsonDocument::fromJson(R"({"tasks": [
            {"title": "Skriv blogindlæg", "start": "fredag 10", "project": "Blog"},
            {"title": "Ring til Kunde A", "start": "2026-12-01T09:30", "guessed": ["start"]},
            {"title": "Læs rapport", "deadline": "2026-12-03"}]})").object();
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
        const QJsonObject bad = QJsonDocument::fromJson(R"({"tasks": [
            {"title": "Første"}, {"title": "Anden", "start": "engang"}]})").object();
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

    void cliAcceptsTheDanishFlagsAsHiddenAliases()
    {
        // The flags of 0.1 keep working (habits, older skill files),
        // but --help shows only the English ones.
        QTemporaryDir dir;
        Store store(dir.filePath("cli.db"));
        QString error;
        QVERIFY(store.open(&error));
        QString out, err;
        int code = 0;
        const QJsonObject created = cliJson(store, {QStringLiteral("new"), QStringLiteral("--titel"), QStringLiteral("Old habit"),
                                                     QStringLiteral("--noter"), QStringLiteral("n"), QStringLiteral("--projekt"),
                                                     QStringLiteral("Blog"), QStringLiteral("--prioritet"), QStringLiteral("2"),
                                                     QStringLiteral("--start"), QStringLiteral("2026-12-01 09:00"),
                                                     QStringLiteral("--varighed"), QStringLiteral("30"), QStringLiteral("--frist"),
                                                     QStringLiteral("2026-12-02"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        const QJsonObject task = created["task"].toObject();
        QCOMPARE(task["title"].toString(), QStringLiteral("Old habit"));
        QCOMPARE(task["notes"].toString(), QStringLiteral("n"));
        QCOMPARE(task["project"].toString(), QStringLiteral("Blog"));
        QCOMPARE(task["priority"].toInt(), 2);
        QCOMPARE(task["duration_minutes"].toInt(), 30);
        QCOMPARE(task["deadline"].toString(), QStringLiteral("2026-12-02"));
        const QJsonObject listed = cliJson(store, {QStringLiteral("list"), QStringLiteral("--fra"), QStringLiteral("2026-12-01"),
                                                    QStringLiteral("--til"), QStringLiteral("2026-12-01"), QStringLiteral("--json")},
                                           &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(listed["tasks"].toArray().size(), 1);
        QCOMPARE(cli(store, {QStringLiteral("summary"), QStringLiteral("--maaned")}, &out, &err), 0);
        QCOMPARE(cli(store, {QStringLiteral("summary"), QStringLiteral("--uge"), QStringLiteral("--month")}, &out, &err), 1);

        QCOMPARE(cli(store, {QStringLiteral("--help")}, &out, &err), 0);
        for (const char *flag : {"--titel", "--noter", "--projekt", "--prioritet", "--varighed", "--frist", "--fra", "--til",
                                 "--motor", "--ja", "--uge", "--maaned"})
            QVERIFY2(!out.contains(QString::fromLatin1(flag) + QLatin1Char(' '))
                         && !out.contains(QString::fromLatin1(flag) + QLatin1Char('|'))
                         && !out.contains(QString::fromLatin1(flag) + QLatin1Char(']')), flag);
        QVERIFY(out.contains(QStringLiteral("--title")));
        QVERIFY(out.contains(QStringLiteral("--yes")));
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

        const QString id = cliJson(store, {QStringLiteral("new"), QStringLiteral("--title"), QStringLiteral("X"),
                                           QStringLiteral("--json")}, &err, &code)["task"].toObject()["id"].toString();
        // move without quotes: `move <id> 2026-12-01 14:30` keeps the time.
        const QJsonObject moved = cliJson(store, {QStringLiteral("move"), id, QStringLiteral("2026-12-01"),
                                                   QStringLiteral("14:30"), QStringLiteral("--json")}, &err, &code);
        QCOMPARE(code, 0);
        QCOMPARE(moved["task"].toObject()["start"].toString(), QStringLiteral("2026-12-01T14:30"));

        // Garbage numbers and statuses are errors, not silent defaults.
        err.clear();
        QCOMPARE(cli(store, {QStringLiteral("edit"), id, QStringLiteral("--duration"), QStringLiteral("1t")}, &out, &err), 1);
        QVERIFY(err.contains(QStringLiteral("duration")));
        QCOMPARE(cli(store, {QStringLiteral("new"), QStringLiteral("--title"), QStringLiteral("Y"),
                             QStringLiteral("--priority"), QStringLiteral("høj")}, &out, &err), 1);
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
        typo["tilte"] = QStringLiteral("Y");
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
            f.write("[engine]\ndefault = \"command\"\ncommand = \"cat\"\n");
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
        f.write("language = \"DA\"  # ui\n"
                "[engine]\ndefault = \"claude\"            # default engine: vibe | claude\n"
                "compare = \"claude, local\"  # compare\n"
                "[engine.local]\napi = 'openai' # endpoint type\nendpoint = http://127.0.0.1:8080 # unquoted\n"
                "model = \"name#with#hash\"\n");
        f.close();
        const AppConfig config = AppConfig::load(path);
        QCOMPARE(config.language, QStringLiteral("da"));
        QCOMPARE(config.engine, QStringLiteral("claude"));
        QCOMPARE(config.compare, (QStringList{QStringLiteral("claude"), QStringLiteral("local")}));
        QCOMPARE(config.localApi, QStringLiteral("openai"));
        QCOMPARE(config.localEndpoint, QStringLiteral("http://127.0.0.1:8080"));
        QCOMPARE(config.localModel, QStringLiteral("name#with#hash"));
        // Defaults without a file: vibe, and Compare = vibe vs claude.
        const AppConfig none = AppConfig::load(dir.filePath("missing.toml"));
        QCOMPARE(none.compare, (QStringList{QStringLiteral("vibe"), QStringLiteral("claude")}));
        QVERIFY(none.language.isEmpty());
    }

    void configReadsTheNamesFromOfZeroPointOne()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.toml");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[motor]\nengine = \"lokal\"\ncompare = \"claude, lokal\"\n"
                "[motor.lokal]\napi = 'openai'\nendpoint = http://127.0.0.1:8080\nmodel = old\n");
        f.close();
        const AppConfig config = AppConfig::load(path);
        QCOMPARE(config.engine, QStringLiteral("local"));
        QCOMPARE(config.compare, (QStringList{QStringLiteral("claude"), QStringLiteral("local")}));
        QCOMPARE(config.localApi, QStringLiteral("openai"));
        QCOMPARE(config.localEndpoint, QStringLiteral("http://127.0.0.1:8080"));
        QCOMPARE(config.localModel, QStringLiteral("old"));
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
        // Never a real AI: "local" without a model fails before any request.
        QFile cfg(dir.filePath("config.toml"));
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[engine]\ndefault = \"local\"\n");
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
            call(3, QStringLiteral("new"), {{QStringLiteral("title"), QStringLiteral("Skriv blogindlæg")},
                                            {QStringLiteral("start"), QStringLiteral("2026-12-04 14:00")},
                                            {QStringLiteral("duration"), 60}, {QStringLiteral("project"), QStringLiteral("Blog")}}),
            call(4, QStringLiteral("list"), {{QStringLiteral("project"), QStringLiteral("Blog")}}),
            call(5, QStringLiteral("done"), {{QStringLiteral("id"), seeded.id}}),
            call(6, QStringLiteral("undo")),
            call(7, QStringLiteral("show"), {{QStringLiteral("id"), QStringLiteral("findes-ikke")}}),
            call(8, QStringLiteral("new"), {{QStringLiteral("title"), QStringLiteral("X")}, {QStringLiteral("start"), QStringLiteral("engang")}}),
            call(9, QStringLiteral("frobnicate")),
            rpc(10, QStringLiteral("resources/list")),
            call(11, QStringLiteral("summary"), {{QStringLiteral("period"), QStringLiteral("week")}}),
            call(12, QStringLiteral("propose"), {{QStringLiteral("text"), QStringLiteral("køb mælk")}}),
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
        cfg.write("[engine]\ndefault = \"command\"\ncommand = \"cat " + fixture.toUtf8() + "\"\n");
        cfg.close();
        QString stderrText;
        QList<QJsonObject> r = mcpSession(db, {
            call(1, QStringLiteral("propose"), {{QStringLiteral("text"), QStringLiteral("køb mælk i morgen")}}),
            call(2, QStringLiteral("list")),
        }, &stderrText);
        QCOMPARE(r.size(), 3);
        const QJsonObject proposed = r[0]["result"].toObject()["structuredContent"].toObject();
        QVERIFY2(!proposed.isEmpty(), qPrintable(r[0]["result"].toObject()["content"].toArray()[0].toObject()["text"].toString()));
        QCOMPARE(r[1]["result"].toObject()["structuredContent"].toObject()["tasks"].toArray().size(), 0); // nothing stored yet

        r = mcpSession(db, {
            call(1, QStringLiteral("approve"), {{QStringLiteral("proposal"), proposed["proposal"]},
                                                {QStringLiteral("capture_id"), proposed["capture_id"]}}),
            call(2, QStringLiteral("add"), {{QStringLiteral("text"), QStringLiteral("køb mælk fredag")}}),
            call(3, QStringLiteral("list")),
        }, &stderrText);
        QCOMPARE(r[0]["result"].toObject()["isError"].toBool(), false);
        QCOMPARE(r[1]["result"].toObject()["structuredContent"].toObject()["stored"].toArray().size(), 1);
        QCOMPARE(r[2]["result"].toObject()["structuredContent"].toObject()["tasks"].toArray().size(), 2);
    }

    void compareSendsTheSameTextToTwoEnginesAndKeepsBothCaptures()
    {
        // Fake engines only: "command" runs `cat <fixture>`, and "local"
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
            f.write("[engine]\ndefault = \"command\"\ncommand = \"cat " + fixture.toUtf8() + "\"\ncompare = \"" + compare + "\"\n");
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
        writeConfig("command, local");
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
            f.write("[engine]\ndefault = \"command\"\ncommand = \"sleep 5\"\ncompare = \"command, command\"\n");
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
    void guessedIsTheKeyEverywhereAndGuessesStillReads() {
        QTemporaryDir dir;
        Store store(dir.filePath("test.db"));
        QString error;
        QVERIFY(store.open(&error));
        Actions actions(&store);
        Task task;
        task.title = QStringLiteral("Write");
        task.guesses = {QStringLiteral("start")};
        const ActionResult created = actions.create(task, QStringLiteral("cli"));
        QVERIFY2(created.ok, qPrintable(created.error));
        const QJsonObject json = created.payload["task"].toObject();
        QCOMPARE(json["guessed"].toArray(), QJsonArray{QStringLiteral("start")});
        QVERIFY(!json.contains(QStringLiteral("guesses")));

        // A patch and stored history from before 0.2 say "guesses".
        const QString id = json["id"].toString();
        const ActionResult patched = actions.update(id, {{QStringLiteral("guesses"), QJsonArray{QStringLiteral("project")}}},
                                                    QStringLiteral("cli"));
        QVERIFY2(patched.ok, qPrintable(patched.error));
        QCOMPARE(patched.payload["task"].toObject()["guessed"].toArray(), QJsonArray{QStringLiteral("project")});
        QJsonObject old = json;
        old.remove(QStringLiteral("guessed"));
        old["guesses"] = QJsonArray{QStringLiteral("deadline")};
        bool ok = false;
        QCOMPARE(Task::fromJson(old, &ok).guesses, QStringList{QStringLiteral("deadline")});
        QVERIFY(ok);
    }

    void languagePrecedence() {
        const QStringList available{QStringLiteral("en"), QStringLiteral("da")};
        using S = UiLanguage::Sources;
        const QStringList danishSystem{QStringLiteral("da-DK"), QStringLiteral("en-GB")};
        // --lang > config > Omarchy > system > English.
        QCOMPARE(UiLanguage::resolve(S{QStringLiteral("en"), QStringLiteral("da"), QStringLiteral("da"), danishSystem},
                                     available), QStringLiteral("en"));
        QCOMPARE(UiLanguage::resolve(S{{}, QStringLiteral("da"), QStringLiteral("en"), {}}, available),
                 QStringLiteral("da"));
        QCOMPARE(UiLanguage::resolve(S{{}, {}, QStringLiteral("da"), {QStringLiteral("en-US")}}, available),
                 QStringLiteral("da"));
        QCOMPARE(UiLanguage::resolve(S{{}, {}, {}, danishSystem}, available), QStringLiteral("da"));
        QCOMPARE(UiLanguage::resolve(S{{}, {}, {}, {}}, available), QStringLiteral("en"));
        // A language without a translation passes to the next source.
        QCOMPARE(UiLanguage::resolve(S{QStringLiteral("fr"), {}, QStringLiteral("pt-BR"), danishSystem}, available),
                 QStringLiteral("da"));
        QCOMPARE(UiLanguage::resolve(S{QStringLiteral("fr"), {}, {}, {QStringLiteral("de-DE")}}, available),
                 QStringLiteral("en"));
        // Locale spellings name their language.
        QCOMPARE(UiLanguage::normalize(QStringLiteral("da_DK.UTF-8")), QStringLiteral("da"));
        QCOMPARE(UiLanguage::normalize(QStringLiteral("DA")), QStringLiteral("da"));
        QCOMPARE(UiLanguage::normalize(QStringLiteral("not a language")), QString());
    }

    void danishTranslationIsEmbeddedAndInstalled() {
        // The .qm files come from i18n/*.ts through lrelease, as resources.
        QVERIFY(UiLanguage::available().contains(QStringLiteral("da")));
        QVERIFY(UiLanguage::available().contains(QStringLiteral("en")));
        QVERIFY(QFile::exists(QStringLiteral(":/i18n/omaplan_da.qm")));

        QTemporaryDir dir;
        qputenv("OMAPLAN_CONFIG", dir.filePath(QStringLiteral("none.toml")).toLocal8Bit());
        const QString omarchy = dir.filePath(QStringLiteral("language"));
        {
            UiLanguage language(QCoreApplication::instance(), QStringLiteral("da"), false, omarchy);
            QCOMPARE(language.language(), QStringLiteral("da"));
            QCOMPARE(UiLanguage::current(), QStringLiteral("da"));
            QCOMPARE(QCoreApplication::translate("Actions", "%n tasks", nullptr, 1), QStringLiteral("1 opgave"));
            QCOMPARE(QCoreApplication::translate("Actions", "%n tasks", nullptr, 3), QStringLiteral("3 opgaver"));
            QCOMPARE(QLocale().language(), QLocale::Danish);
        }
        {
            // English has its plural forms too.
            UiLanguage language(QCoreApplication::instance(), QStringLiteral("en"), false, omarchy);
            QCOMPARE(QCoreApplication::translate("Actions", "%n tasks", nullptr, 1), QStringLiteral("1 task"));
            QCOMPARE(QCoreApplication::translate("Actions", "%n tasks", nullptr, 3), QStringLiteral("3 tasks"));
        }
        // Without a translator, the source text.
        QCOMPARE(QCoreApplication::translate("Actions", "Nothing to undo"), QStringLiteral("Nothing to undo"));
        qunsetenv("OMAPLAN_CONFIG");
        QLocale::setDefault(QLocale::c());
    }

    void everyStringHasADanishTranslation() {
        // bin/test also runs lupdate against the sources (bin/i18n-check), so
        // a new qsTr/tr string without a translation fails there; this checks
        // the committed file.
        QFile ts(QStringLiteral(OMAPLAN_I18N "/omaplan_da.ts"));
        QVERIFY(ts.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(ts.readAll());
        QVERIFY(text.count(QStringLiteral("<message")) > 200);
        QCOMPARE(text.count(QStringLiteral("type=\"unfinished\"")), 0);
        QCOMPARE(text.count(QStringLiteral("type=\"vanished\"")), 0);
    }

    // Runs the real binary with a private database and config.
    static QString runOmaplan(const QString &db, const QStringList &args, int *code = nullptr) {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OMAPLAN_DB"), db);
        env.insert(QStringLiteral("OMAPLAN_CONFIG"), QFileInfo(db).absolutePath() + QStringLiteral("/config.toml"));
        env.insert(QStringLiteral("OMAPLAN_SNAPSHOT"), QFileInfo(db).absolutePath() + QStringLiteral("/plan.json"));
        env.insert(QStringLiteral("HOME"), QFileInfo(db).absolutePath());
        env.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
        env.remove(QStringLiteral("LANGUAGE"));
        env.remove(QStringLiteral("LC_ALL"));
        process.setProcessEnvironment(env);
        process.start(QCoreApplication::applicationDirPath() + QStringLiteral("/../omaplan"), args);
        process.waitForFinished(20000);
        if (code)
            *code = process.exitCode();
        return QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    }

    void cliSpeaksTheChosenLanguageButJsonStaysEnglish() {
        QTemporaryDir dir;
        const QString db = dir.filePath(QStringLiteral("lang.db"));
        int code = 0;
        runOmaplan(db, {QStringLiteral("new"), QStringLiteral("--title"), QStringLiteral("Write"), QStringLiteral("--project"),
                        QStringLiteral("Blog"), QStringLiteral("--start"), QStringLiteral("2026-12-01")}, &code);
        QCOMPARE(code, 0);

        QCOMPARE(runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("da"), QStringLiteral("projects")}),
                 QStringLiteral("Blog (1 opgave)\n"));
        QCOMPARE(runOmaplan(db, {QStringLiteral("projects"), QStringLiteral("--lang"), QStringLiteral("en")}),
                 QStringLiteral("Blog (1 task)\n"));
        // Without --lang (no config, no Omarchy file, C locale): English.
        QCOMPARE(runOmaplan(db, {QStringLiteral("projects")}), QStringLiteral("Blog (1 task)\n"));
        // config.toml's language, overridden by --lang.
        QFile config(dir.filePath(QStringLiteral("config.toml")));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("language = \"da\"\n");
        config.close();
        QCOMPARE(runOmaplan(db, {QStringLiteral("projects")}), QStringLiteral("Blog (1 opgave)\n"));
        QCOMPARE(runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("projects")}),
                 QStringLiteral("Blog (1 task)\n"));
        // Error messages too.
        QVERIFY(runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("da"), QStringLiteral("new")}, &code)
                    .contains(QStringLiteral("Mangler --title")));
        QCOMPARE(code, 1);
        QVERIFY(runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("da"), QStringLiteral("--help")})
                    .startsWith(QStringLiteral("Brug: omaplan")));

        // --json is the same, field for field, in every language.
        const QString jsonDa = runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("da"), QStringLiteral("list"),
                                               QStringLiteral("--json")});
        const QString jsonEn = runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("list"),
                                               QStringLiteral("--json")});
        QCOMPARE(jsonDa, jsonEn);
        QVERIFY(jsonDa.contains(QStringLiteral("\"title\":\"Write\"")));
        QCOMPARE(runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("da"), QStringLiteral("summary"),
                                 QStringLiteral("--month"), QStringLiteral("--json")}),
                 runOmaplan(db, {QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("summary"),
                                 QStringLiteral("--month"), QStringLiteral("--json")}));
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
        QJsonObject r = actions.guiNew({{QStringLiteral("title"), QStringLiteral("Møde")},
                                        {QStringLiteral("start"), QStringLiteral("2026-12-07 10:00")},
                                        {QStringLiteral("project"), QStringLiteral("Kunde A")}});
        QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
        const QString meeting = r["payload"].toObject()["task"].toObject()["id"].toString();
        r = actions.guiNew({{QStringLiteral("title"), QStringLiteral("Heldag")}, {QStringLiteral("start"), QStringLiteral("2026-12-07")}});
        const QString allDay = r["payload"].toObject()["task"].toObject()["id"].toString();
        r = actions.guiNew({{QStringLiteral("title"), QStringLiteral("Blog")}, {QStringLiteral("project"), QStringLiteral("Blog")}});
        const QString blog = r["payload"].toObject()["task"].toObject()["id"].toString();
        QVERIFY(!actions.guiNew({{QStringLiteral("title"), QStringLiteral("X")}, {QStringLiteral("deadline"), QStringLiteral("snart")}})["ok"].toBool());

        r = actions.guiEdit(meeting, {{QStringLiteral("duration"), QStringLiteral("90")}, {QStringLiteral("deadline"), QStringLiteral("2026-12-08")}});
        QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
        QCOMPARE(actions.guiTask(meeting)["payload"].toObject()["task"].toObject()["duration_minutes"].toInt(), 90);
        QVERIFY(!actions.guiEdit(meeting, {{QStringLiteral("duration"), QStringLiteral("halvanden time")}})["ok"].toBool());

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
