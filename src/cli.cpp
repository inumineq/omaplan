#include "cli.h"

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
#include "skill.h"
#include "store.h"
#include "task.h"

static void printJson(QTextStream &out, const QJsonObject &object) {
    out << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) << "\n";
}

static void printHelp(QTextStream &out) {
    out << QObject::tr(
        "Usage: omaplan [--db <path>] <command> [options] [--json]\n"
        "\n"
        "Commands:\n"
        "  open                  Open the window (default without arguments)\n"
        "  new --titel T [--noter N] [--projekt P] [--prioritet 1-3] [--start D] [--varighed MIN] [--frist D]\n"
        "  list [--fra D] [--til D] [--projekt P] [--status todo|done]\n"
        "  show <id>\n"
        "  edit <id> [--titel T] [--noter N] [--projekt P] [--prioritet N] [--start D] [--varighed MIN] [--frist D] [--status S]\n"
        "  move <id> <tid>\n"
        "  done <id> | undone <id>\n"
        "  rm <id>\n"
        "  undo\n"
        "  projects\n"
        "  summary [--uge|--maaned]\n"
        "  add <text> [--motor ...] [--ja] [--model M] [--endpoint URL]   AI proposal; --ja stores it\n"
        "  skill install [--force]|uninstall   install/remove the bundled agent skill\n"
        "  mcp                   MCP server over stdio (claude mcp add omaplan -- omaplan mcp)\n"
        "  snapshot | serve ...  (not implemented yet)\n"
        "\n"
        "Dates accept ISO (2026-12-01, 2026-12-01 14:30) and Danish/English expressions\n"
        "(\"i morgen\", \"fredag 12\", \"tomorrow\", \"next monday\", optional \"kl. 14:30\").\n"
        "An empty value clears a field: --projekt \"\".\n"
        "\n"
        "Examples:\n"
        "  omaplan new --titel \"Write blog post\" --projekt Blog --start \"fredag\"\n"
        "  omaplan list --fra \"i dag\" --til \"næste fredag\" --json\n"
        "  omaplan move <id> \"tirsdag kl. 14:00\"\n"
        "  omaplan done <id>\n"
        "  omaplan undo --json\n"
        "  omaplan skill install --force   replace customized skill files\n");
}

// Splits "--flag value" pairs from positional arguments. A flag at the end, or
// followed by another flag, gets an empty value.
static QHash<QString, QString> parseFlags(const QStringList &args, QStringList *positional) {
    QHash<QString, QString> flags;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i].startsWith(QLatin1String("--"))) {
            const QString key = args[i].mid(2);
            if (i + 1 < args.size() && !args[i + 1].startsWith(QLatin1String("--")))
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
        else if (args[i] == QLatin1String("--db"))
            ++i; // the store was already built from this by the caller
        else
            rest.append(args[i]);
    }

    if (rest.isEmpty() || rest.first() == QLatin1String("--help") || rest.first() == QLatin1String("-h")
        || rest.first() == QLatin1String("help")) {
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

    // Later phases; they answer "not implemented yet" with exit 1.
    if (command == QLatin1String("snapshot") || command == QLatin1String("serve")) {
        err << QObject::tr("Not implemented yet: %1").arg(command) << "\n";
        return 1;
    }

    QString error;
    if (!store->open(&error)) {
        err << error << "\n";
        return 1;
    }
    Actions actions(store);
    const QString source = QStringLiteral("cli");

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
            err << QObject::tr("Usage: omaplan add \"<text>\" [--motor vibe|claude|lokal|command] [--ja] "
                               "[--model M] [--endpoint URL] [--json]") << "\n";
            return 1;
        }
        QString captureId;
        ActionResult result = actions.propose(text, flags.value(QStringLiteral("motor")),
                                               flags.value(QStringLiteral("model")),
                                               flags.value(QStringLiteral("endpoint")), &captureId);
        if (!result.ok) {
            err << result.error << "\n";
            return 1;
        }
        QJsonObject payload = result.payload;
        if (flags.contains(QStringLiteral("ja"))) {
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
            const QJsonArray tasks = payload["proposal"].toObject()["opgaver"].toArray();
            for (const QJsonValue &t : tasks) {
                const QJsonObject o = t.toObject();
                QString line = o["titel"].toString();
                if (o.contains("start"))
                    line += QStringLiteral("  (%1)").arg(o["start"].toString());
                if (o.contains("frist"))
                    line += QStringLiteral("  Frist: %1").arg(o["frist"].toString());
                const QJsonArray gaet = o["gaet"].toArray();
                if (!gaet.isEmpty()) {
                    QStringList g;
                    for (const QJsonValue &gv : gaet)
                        g << gv.toString();
                    line += QStringLiteral("  [gæt: %1]").arg(g.join(QStringLiteral(", ")));
                }
                out << line << "\n";
            }
            const QJsonArray unclear = payload["proposal"].toObject()["uafklaret"].toArray();
            for (const QJsonValue &u : unclear)
                out << QObject::tr("Unclear: %1").arg(u.toString()) << "\n";
        }
        return 0;
    }

    if (command == QLatin1String("new")) {
        QStringList positional;
        const auto flags = parseFlags(rest, &positional);
        if (!flags.contains(QStringLiteral("titel")) || flags[QStringLiteral("titel")].trimmed().isEmpty()) {
            err << QObject::tr("Missing --titel") << "\n";
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
        QDateTime when;
        bool hasTime;
        if (flags.contains(QStringLiteral("fra"))) {
            if (!parseDateFlag(flags["fra"], &when, &hasTime, err))
                return 1;
            filter.hasFrom = true;
            filter.from = when.date();
        }
        if (flags.contains(QStringLiteral("til"))) {
            if (!parseDateFlag(flags["til"], &when, &hasTime, err))
                return 1;
            filter.hasTo = true;
            filter.to = when.date();
        }
        if (flags.contains(QStringLiteral("projekt")))
            filter.project = flags[QStringLiteral("projekt")];
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
            err << QObject::tr("Usage: omaplan move <id> <tid>") << "\n";
            return 1;
        }
        QDateTime when;
        bool hasTime;
        // `move <id> tirsdag 14` without quotes is the same as "tirsdag 14".
        if (!parseDateFlag(positional.mid(1).join(QLatin1Char(' ')), &when, &hasTime, err))
            return 1;
        if (!when.isValid()) {
            err << QObject::tr("Usage: omaplan move <id> <tid>") << "\n";
            return 1;
        }
        ActionResult result = actions.move(positional.first(), when, hasTime, source);
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

    if (command == QLatin1String("undo")) {
        ActionResult result = actions.undo(source);
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
        const bool week = flags.contains(QStringLiteral("uge"));
        const bool month = flags.contains(QStringLiteral("maaned"));
        if (week && month) {
            err << QObject::tr("Choose either --uge or --maaned") << "\n";
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
