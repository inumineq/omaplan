#include "cli.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "actions.h"
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
        "  snapshot | serve | mcp | skill ...   (not implemented yet)\n"
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
        "  omaplan undo --json\n");
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

static void setDateField(QJsonObject *patch, const QString &key, const QDateTime &when, bool hasTime) {
    (*patch)[key] = hasTime ? when.toString(Qt::ISODateWithMs) : when.date().toString(Qt::ISODate);
    (*patch)[key + "_has_time"] = hasTime;
}

// Builds the edit patch from CLI flags; returns false (and prints) on bad input.
static bool buildPatch(const QHash<QString, QString> &flags, QJsonObject *patch, QTextStream &err) {
    if (flags.contains(QStringLiteral("titel"))) (*patch)["title"] = flags["titel"];
    if (flags.contains(QStringLiteral("noter"))) (*patch)["notes"] = flags["noter"];
    if (flags.contains(QStringLiteral("projekt"))) (*patch)["project"] = flags["projekt"];
    if (flags.contains(QStringLiteral("prioritet"))) (*patch)["priority"] = flags["prioritet"].toInt();
    if (flags.contains(QStringLiteral("status"))) (*patch)["status"] = flags["status"];
    if (flags.contains(QStringLiteral("varighed"))) (*patch)["duration_minutes"] = flags["varighed"].toInt();
    QDateTime when;
    bool hasTime;
    if (flags.contains(QStringLiteral("start"))) {
        if (!parseDateFlag(flags["start"], &when, &hasTime, err))
            return false;
        if (when.isValid())
            setDateField(patch, QStringLiteral("start"), when, hasTime);
        else {
            (*patch)["start"] = QString();
            (*patch)["start_has_time"] = false;
        }
    }
    if (flags.contains(QStringLiteral("frist"))) {
        if (!parseDateFlag(flags["frist"], &when, &hasTime, err))
            return false;
        if (when.isValid())
            setDateField(patch, QStringLiteral("deadline"), when, hasTime);
        else {
            (*patch)["deadline"] = QString();
            (*patch)["deadline_has_time"] = false;
        }
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

    // Later phases; they answer "not implemented yet" with exit 1.
    if (command == QLatin1String("snapshot") || command == QLatin1String("serve")
        || command == QLatin1String("mcp") || command == QLatin1String("skill")) {
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
        task.title = flags[QStringLiteral("titel")];
        if (flags.contains(QStringLiteral("noter")))
            task.notes = flags[QStringLiteral("noter")];
        if (flags.contains(QStringLiteral("projekt")))
            task.project = flags[QStringLiteral("projekt")];
        if (flags.contains(QStringLiteral("prioritet")))
            task.priority = flags[QStringLiteral("prioritet")].toInt();
        if (flags.contains(QStringLiteral("varighed")))
            task.durationMinutes = flags[QStringLiteral("varighed")].toInt();
        QDateTime when;
        bool hasTime;
        if (flags.contains(QStringLiteral("start"))) {
            if (!parseDateFlag(flags[QStringLiteral("start")], &when, &hasTime, err))
                return 1;
            task.start = when;
            task.startHasTime = hasTime;
        }
        if (flags.contains(QStringLiteral("frist"))) {
            if (!parseDateFlag(flags[QStringLiteral("frist")], &when, &hasTime, err))
                return 1;
            task.deadline = when;
            task.deadlineHasTime = hasTime;
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
        if (!buildPatch(flags, &patch, err))
            return 1;
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
        if (!parseDateFlag(positional[1], &when, &hasTime, err))
            return 1;
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
        ActionResult result = actions.summary(week && !month); // default: week
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
