#include "cli.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "actions.h"
#include "mcp.h"
#include "server.h"
#include "snapshot.h"
#include "skill.h"
#include "store.h"
#include "task.h"
#include "uilanguage.h"

static void printJson(QTextStream &out, const QJsonObject &object) {
    out << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) << "\n";
}

static void printHelp(QTextStream &out) {
    out << QObject::tr(
        "Usage: omaplan [--db <path>] [--lang da|en] <command> [options] [--json]\n"
        "       omaplan --version | --help\n"
        "\n"
        "Commands:\n"
        "  open                  Open the window (default without arguments)\n"
        "  new --title T [--notes N] [--project P] [--priority 1-3] [--start D] [--duration MIN] [--deadline D]\n"
        "  list [--from D] [--to D] [--project P] [--status todo|done]\n"
        "  show <id>\n"
        "  edit <id> [--title T] [--notes N] [--project P] [--priority N] [--start D] [--duration MIN] [--deadline D] [--status S]\n"
        "  move <id> <when>\n"
        "  done <id> | undone <id>\n"
        "  rm <id>\n"
        "  undo | redo           undo the newest change, or redo the newest undo\n"
        "  projects\n"
        "  summary [--week|--month]\n"
        "  add <text> [--engine ...] [--yes] [--model M] [--endpoint URL]   AI proposal; --yes stores it\n"
        "  skill install [--force]|uninstall   install/remove the bundled agent skill\n"
        "  mcp                   MCP server over stdio (claude mcp add omaplan -- omaplan mcp)\n"
        "  snapshot [path]       write plan.json for the read-only edition (also automatic after every change)\n"
        "  serve [--port N] [--bind ADDR] [--host NAME,...] [--snapshot PATH]   read-only web page (default 127.0.0.1:8787)\n"
        "\n"
        "Dates accept ISO (2026-12-01, 2026-12-01 14:30) and English/Danish expressions\n"
        "(\"tomorrow\", \"friday 12\", \"next monday\", optional \"at 14:30\"; \"i morgen\", \"kl. 14:30\").\n"
        "An empty value clears a field: --project \"\".\n"
        "\n"
        "Examples:\n"
        "  omaplan new --title \"Write blog post\" --project Blog --start friday\n"
        "  omaplan list --from today --to \"next friday\" --json\n"
        "  omaplan move <id> \"tuesday at 14:00\"\n"
        "  omaplan done <id>\n"
        "  omaplan undo --json\n"
        "  omaplan skill install --force   replace customized skill files\n");
}

// The flag names of 0.1, still accepted so habits and older skill
// files keep working. They are not in --help.
static const QHash<QString, QString> &legacyFlags() {
    static const QHash<QString, QString> names = [] {
        QHash<QString, QString> map = Actions::legacyFieldNames();
        map.insert(QStringLiteral("ja"), QStringLiteral("yes"));
        map.insert(QStringLiteral("motor"), QStringLiteral("engine"));
        map.insert(QStringLiteral("fra"), QStringLiteral("from"));
        map.insert(QStringLiteral("til"), QStringLiteral("to"));
        map.insert(QStringLiteral("uge"), QStringLiteral("week"));
        map.insert(QStringLiteral("maaned"), QStringLiteral("month"));
        return map;
    }();
    return names;
}

// Splits "--flag value" pairs from positional arguments. A flag at the end, or
// followed by another flag, gets an empty value; the switches (--yes,
// --force, --week, --month) never take a value, so `add --yes buy milk`
// keeps "buy". Old Danish flag names are stored under their English names.
static QHash<QString, QString> parseFlags(const QStringList &args, QStringList *positional) {
    static const QStringList switches = {QStringLiteral("yes"), QStringLiteral("force"), QStringLiteral("week"),
                                         QStringLiteral("month")};
    QHash<QString, QString> flags;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i].startsWith(QLatin1String("--"))) {
            const QString key = legacyFlags().value(args[i].mid(2), args[i].mid(2));
            if (!switches.contains(key) && i + 1 < args.size() && !args[i + 1].startsWith(QLatin1String("--")))
                flags[key] = args[++i];
            else
                flags[key] = QString();
        } else {
            positional->append(args[i]);
        }
    }
    return flags;
}

static bool parseDateFlag(const QString &text, QDateTime *when, bool *hasTime, QTextStream &err) {
    if (text.isEmpty()) { // an empty value clears the field
        *when = QDateTime();
        *hasTime = false;
        return true;
    }
    QString error;
    if (!Actions::parseDateTime(text, QDateTime::currentDateTime().date(), when, hasTime, &error)) {
        err << error << "\n";
        return false;
    }
    return true;
}

static int emitResult(QTextStream &out, bool json, const ActionResult &result) {
    if (json)
        printJson(out, result.payload);
    else if (!result.text.isEmpty())
        out << result.text << "\n";
    return 0;
}

int runCli(const QStringList &args, Store *store, QTextStream &out, QTextStream &err) {
    bool json = false;
    QStringList rest;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--json"))
            json = true;
        else if (args[i] == QLatin1String("--db") || args[i] == QLatin1String("--lang"))
            ++i; // handled by the caller: the store and the language are already set
        else
            rest.append(args[i]);
    }

    if (rest.contains(QLatin1String("--version"))) {
        out << "omaplan " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }

    // --help or -h anywhere shows the help and runs nothing: an agent that
    // asks for the help of `undo` must not undo.
    if (rest.isEmpty() || rest.first() == QLatin1String("help") || rest.contains(QLatin1String("--help"))
        || rest.contains(QLatin1String("-h"))) {
        printHelp(out);
        return 0;
    }

    const QString command = rest.takeFirst();

    // Agent skill: no database needed, works off the home directory.
    if (command == QLatin1String("skill")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        const QString sub = positional.isEmpty() ? QString() : positional.first();
        if (sub == QLatin1String("install") || sub == QLatin1String("uninstall")) {
            const Skill::Result r = sub == QLatin1String("install")
                                        ? Skill::install(QDir::homePath(), flags.contains(QStringLiteral("force")))
                                        : Skill::uninstall(QDir::homePath());
            if (json) {
                QJsonObject obj;
                obj.insert(QStringLiteral("ok"), r.ok);
                auto toArray = [](const QStringList &l) {
                    QJsonArray a;
                    for (const QString &s : l)
                        a.append(s);
                    return a;
                };
                obj.insert(QStringLiteral("installed"), toArray(r.installed));
                obj.insert(QStringLiteral("removed"), toArray(r.removed));
                obj.insert(QStringLiteral("already_there"), toArray(r.alreadyThere));
                obj.insert(QStringLiteral("conflicts"), toArray(r.conflicts));
                if (!r.error.isEmpty())
                    obj.insert(QStringLiteral("error"), r.error);
                printJson(out, obj);
            } else {
                for (const QString &dir : r.installed)
                    out << QObject::tr("Installed skill in %1").arg(dir) << "\n";
                for (const QString &dir : r.removed)
                    out << QObject::tr("Removed skill from %1").arg(dir) << "\n";
                for (const QString &dir : r.alreadyThere)
                    out << QObject::tr("Skill already present in %1").arg(dir) << "\n";
                for (const QString &path : r.conflicts)
                    err << QObject::tr("Customized skill preserved in %1; use --force to replace it").arg(path) << "\n";
                if (!r.error.isEmpty())
                    err << r.error << "\n";
            }
            return r.ok ? 0 : 1;
        }
        err << QObject::tr("Usage: omaplan skill install [--force]|uninstall [--json]") << "\n";
        return 1;
    }

    // The read-only edition's web server reads plan.json only, never the
    // database, so it starts without opening the store.
    if (command == QLatin1String("serve")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        PlanServer::Options options;
        // Visitors get their browser's language; this is the fallback.
        options.language = UiLanguage::current();
        options.snapshotPath = flags.contains(QStringLiteral("snapshot")) ? flags.value(QStringLiteral("snapshot"))
                                                                          : Snapshot::path(store->path());
        if (flags.contains(QStringLiteral("port"))) {
            bool ok = false;
            const uint port = flags.value(QStringLiteral("port")).toUInt(&ok);
            if (!ok || port > 65535) {
                err << QObject::tr("Invalid port: %1").arg(flags.value(QStringLiteral("port"))) << "\n";
                return 1;
            }
            options.port = quint16(port);
        }
        options.hosts = flags.value(QStringLiteral("host")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (flags.contains(QStringLiteral("bind")) && !options.address.setAddress(flags.value(QStringLiteral("bind")))) {
            err << QObject::tr("Invalid address: %1").arg(flags.value(QStringLiteral("bind"))) << "\n";
            return 1;
        }
        if (options.snapshotPath.isEmpty()) {
            err << QObject::tr("No snapshot path; use --snapshot") << "\n";
            return 1;
        }
        PlanServer server(options);
        QString error;
        if (!server.listen(&error)) {
            err << error << "\n";
            return 1;
        }
        const QString host = options.address.protocol() == QAbstractSocket::IPv6Protocol
                                 ? QLatin1Char('[') + options.address.toString() + QLatin1Char(']')
                                 : options.address.toString();
        out << QObject::tr("Serving http://%1:%2/ from %3").arg(host).arg(server.port()).arg(options.snapshotPath)
            << "\n";
        out.flush();
        return QCoreApplication::exec();
    }

    QString error;
    if (!store->open(&error)) {
        err << error << "\n";
        return 1;
    }
    Actions actions(store);
    const QString source = QStringLiteral("cli");
    // plan.json follows every change made here (CLI and MCP alike).
    Snapshotter snapshotter(store, Snapshot::path(store->path()));
    snapshotter.setLanguage(UiLanguage::current());

    if (command == QLatin1String("snapshot")) {
        QStringList positional;
        parseFlags(rest, &positional);
        const QString path = positional.isEmpty() ? snapshotter.path() : positional.first();
        if (path.isEmpty()) {
            err << QObject::tr("No snapshot path") << "\n";
            return 1;
        }
        if (!Snapshot::write(store, path, &error, UiLanguage::current())) {
            err << error << "\n";
            return 1;
        }
        if (json) {
            QJsonObject obj;
            obj.insert(QStringLiteral("ok"), true);
            obj.insert(QStringLiteral("path"), path);
            printJson(out, obj);
        } else {
            out << QObject::tr("Snapshot written to %1").arg(path) << "\n";
        }
        return 0;
    }

    if (command == QLatin1String("mcp")) {
        // stdout carries the protocol only; anything else goes to stderr.
        QFile in, outFile;
        if (!in.open(stdin, QIODevice::ReadOnly) || !outFile.open(stdout, QIODevice::WriteOnly)) {
            err << QObject::tr("Cannot open stdin/stdout for MCP") << "\n";
            return 1;
        }
        McpServer server(&actions);
        return server.serve(&in, &outFile);
    }

    if (command == QLatin1String("add")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        const QString text = positional.join(QLatin1Char(' ')).trimmed();
        if (text.isEmpty()) {
            err << QObject::tr("Usage: omaplan add \"<text>\" [--engine vibe|claude|local|command] [--yes] "
                               "[--model M] [--endpoint URL] [--json]") << "\n";
            return 1;
        }
        QString captureId;
        ActionResult result = actions.propose(text, flags.value(QStringLiteral("engine")),
                                               flags.value(QStringLiteral("model")),
                                               flags.value(QStringLiteral("endpoint")), &captureId);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        QJsonObject payload = result.payload;
        if (flags.contains(QStringLiteral("yes"))) {
            ActionResult approved = actions.approve(payload["proposal"].toObject(), captureId,
                                                    QStringLiteral("cli"));
            if (!approved.ok) {
                err << approved.error << "\n";
                return 1;
            }
            payload["stored"] = approved.payload["stored"];
            if (json)
                printJson(out, payload);
            else if (!approved.text.isEmpty())
                out << approved.text << "\n";
            return 0;
        }
        if (json) {
            printJson(out, payload);
        } else {
            // Human-readable proposal: one line per task plus unclear text.
            const QJsonArray tasks = payload["proposal"].toObject()["tasks"].toArray();
            for (const QJsonValue &t : tasks) {
                const QJsonObject o = t.toObject();
                QString line = o["title"].toString();
                if (o.contains("start"))
                    line += QStringLiteral("  (%1)").arg(o["start"].toString());
                if (o.contains("deadline"))
                    line += QStringLiteral("  ") + QObject::tr("Deadline: %1").arg(o["deadline"].toString());
                const QJsonArray guessed = o["guessed"].toArray();
                if (!guessed.isEmpty()) {
                    QStringList g;
                    for (const QJsonValue &gv : guessed)
                        g << gv.toString();
                    line += QStringLiteral("  ") + QObject::tr("[guessed: %1]").arg(g.join(QStringLiteral(", ")));
                }
                out << line << "\n";
            }
            const QJsonArray unresolved = payload["proposal"].toObject()["unresolved"].toArray();
            for (const QJsonValue &u : unresolved)
                out << QObject::tr("Unresolved: %1").arg(u.toString()) << "\n";
        }
        return 0;
    }

    if (command == QLatin1String("new")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        if (flags.value(QStringLiteral("title")).trimmed().isEmpty()) {
            err << QObject::tr("Missing --title") << "\n";
            return 1;
        }
        Task task;
        if (!Actions::taskFromFields(flags, &task, &error)) {
            err << error << "\n";
            return 1;
        }
        ActionResult result = actions.create(task, source);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("list")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        ListFilter filter;
        const QDate today = QDateTime::currentDateTime().date();
        for (const QString &key : {QStringLiteral("from"), QStringLiteral("to")}) {
            if (!flags.contains(key) || flags.value(key).isEmpty())
                continue;
            QDate *date = key == QLatin1String("from") ? &filter.from : &filter.to;
            if (!Actions::parseFilterDate(flags.value(key), today, date, &error)) {
                err << error << "\n";
                return 1;
            }
            (key == QLatin1String("from") ? filter.hasFrom : filter.hasTo) = true;
        }
        if (flags.contains(QStringLiteral("project")))
            filter.project = flags[QStringLiteral("project")];
        if (flags.contains(QStringLiteral("status")))
            filter.status = flags[QStringLiteral("status")];
        ActionResult result = actions.list(filter);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("show")) {
        QStringList positional;
        parseFlags(rest, &positional);
        if (positional.isEmpty()) {
            err << QObject::tr("Usage: omaplan show <id>") << "\n";
            return 1;
        }
        ActionResult result = actions.show(positional.first());
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("edit")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        if (positional.isEmpty()) {
            err << QObject::tr("Usage: omaplan edit <id> [fields]") << "\n";
            return 1;
        }
        QJsonObject patch;
        if (!Actions::patchFromFields(flags, &patch, &error)) {
            err << error << "\n";
            return 1;
        }
        ActionResult result = actions.update(positional.first(), patch, source);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("move")) {
        QStringList positional;
        parseFlags(rest, &positional);
        if (positional.size() < 2) {
            err << QObject::tr("Usage: omaplan move <id> <when>") << "\n";
            return 1;
        }
        // `move <id> tuesday 14` without quotes is the same as "tuesday 14".
        ActionResult result = actions.moveTo(positional.first(), positional.mid(1).join(QLatin1Char(' ')), source);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("done") || command == QLatin1String("undone")) {
        QStringList positional;
        parseFlags(rest, &positional);
        if (positional.isEmpty()) {
            err << QObject::tr("Usage: omaplan %1 <id>").arg(command) << "\n";
            return 1;
        }
        ActionResult result = actions.setDone(positional.first(), command == QLatin1String("done"), source);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("rm")) {
        QStringList positional;
        parseFlags(rest, &positional);
        if (positional.isEmpty()) {
            err << QObject::tr("Usage: omaplan rm <id>") << "\n";
            return 1;
        }
        ActionResult result = actions.remove(positional.first(), source);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("undo") || command == QLatin1String("redo")) {
        ActionResult result = command == QLatin1String("undo") ? actions.undo(source) : actions.redo(source);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("projects")) {
        ActionResult result = actions.projects();
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    if (command == QLatin1String("summary")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        const bool week = flags.contains(QStringLiteral("week"));
        const bool month = flags.contains(QStringLiteral("month"));
        if (week && month) {
            err << QObject::tr("Choose either --week or --month") << "\n";
            return 1;
        }
        ActionResult result = actions.summary(!month); // default: this week
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        return emitResult(out, json, result);
    }

    err << QObject::tr("Unknown command: %1").arg(command) << "\n";
    printHelp(err);
    return 1;
}
