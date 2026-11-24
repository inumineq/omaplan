#include "mcp.h"

#include <QCoreApplication>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>

#include "actions.h"

static const QStringList kProtocolVersions = {
    QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};

static QJsonObject prop(const QString &type, const QString &description) {
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

static QJsonObject tool(const QString &name, const QString &description, const QJsonObject &properties,
                        const QStringList &required = {}, bool readOnly = false) {
    QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty())
        schema.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    return {{QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("inputSchema"), schema},
            {QStringLiteral("annotations"), QJsonObject{{QStringLiteral("readOnlyHint"), readOnly}}}};
}

// Field descriptions shared by new and edit. Dates accept the same text as
// the CLI, so agents can pass "friday 14" as well as ISO.
static QJsonObject taskFields() {
    const QString date = QStringLiteral(
        "ISO (2026-12-01, 2026-12-01 14:30) or English/Danish text (\"tomorrow\", \"friday 12\", "
        "\"next monday\", \"at 14:30\", \"i morgen\"). Empty clears it.");
    return {
        {QStringLiteral("title"), prop(QStringLiteral("string"), QStringLiteral("Short title, imperative or noun."))},
        {QStringLiteral("notes"), prop(QStringLiteral("string"), QStringLiteral("Notes."))},
        {QStringLiteral("project"), prop(QStringLiteral("string"), QStringLiteral("Project, e.g. \"Blog\". Empty clears it."))},
        {QStringLiteral("priority"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                  {QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), 3},
                                                  {QStringLiteral("description"), QStringLiteral("1 = high, 3 = normal.")}}},
        {QStringLiteral("start"), prop(QStringLiteral("string"), QStringLiteral("When to work on it. ") + date)},
        {QStringLiteral("duration"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                 {QStringLiteral("minimum"), 0},
                                                 {QStringLiteral("description"), QStringLiteral("Duration in minutes.")}}},
        {QStringLiteral("deadline"), prop(QStringLiteral("string"), QStringLiteral("Deadline, separate from start. ") + date)},
    };
}

QJsonObject McpServer::toolList() {
    const QJsonObject id{{QStringLiteral("id"), prop(QStringLiteral("string"), QStringLiteral("Task id (UUID) from list/show."))}};
    QJsonObject editFields = taskFields();
    editFields.insert(QStringLiteral("id"), id.value(QStringLiteral("id")));
    editFields.insert(QStringLiteral("status"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                           {QStringLiteral("enum"), QJsonArray{QStringLiteral("todo"), QStringLiteral("done")}}});
    QJsonObject proposeFields{
        {QStringLiteral("text"), prop(QStringLiteral("string"),
                                      QStringLiteral("The user's free text in any language, e.g. \"write blog post "
                                                     "friday at 14, 1 hour, project Blog\"."))},
        {QStringLiteral("engine"), prop(QStringLiteral("string"), QStringLiteral("AI engine: vibe, claude, local or command. "
                                                                                 "Default: the user's config."))}};

    QJsonArray tools;
    tools << tool(QStringLiteral("list"), QStringLiteral("List tasks, optionally filtered by date range, project and status."),
                  {{QStringLiteral("from"), prop(QStringLiteral("string"), QStringLiteral("From date (inclusive), e.g. \"today\"."))},
                   {QStringLiteral("to"), prop(QStringLiteral("string"), QStringLiteral("To date (inclusive), e.g. \"next friday\"."))},
                   {QStringLiteral("project"), prop(QStringLiteral("string"), QStringLiteral("Only this project."))},
                   {QStringLiteral("status"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("todo"), QStringLiteral("done")}}}}},
                  {}, true)
          << tool(QStringLiteral("show"), QStringLiteral("Show one task."), id, {QStringLiteral("id")}, true)
          << tool(QStringLiteral("new"), QStringLiteral("Create a task directly, without AI."), taskFields(),
                  {QStringLiteral("title")})
          << tool(QStringLiteral("edit"), QStringLiteral("Change fields of a task; only the given fields change."),
                  editFields, {QStringLiteral("id")})
          << tool(QStringLiteral("move"), QStringLiteral("Move a task to a new start day or time."),
                  {{QStringLiteral("id"), id.value(QStringLiteral("id"))},
                   {QStringLiteral("when"), prop(QStringLiteral("string"), QStringLiteral("New start, e.g. \"tuesday at 14:00\" or 2026-12-01."))}},
                  {QStringLiteral("id"), QStringLiteral("when")})
          << tool(QStringLiteral("done"), QStringLiteral("Mark a task done."), id, {QStringLiteral("id")})
          << tool(QStringLiteral("undone"), QStringLiteral("Mark a task not done."), id, {QStringLiteral("id")})
          << tool(QStringLiteral("rm"), QStringLiteral("Delete a task (undo brings it back)."), id, {QStringLiteral("id")})
          << tool(QStringLiteral("undo"), QStringLiteral("Undo the newest change, whoever made it (GUI, CLI or MCP)."), {})
          << tool(QStringLiteral("redo"), QStringLiteral("Redo the newest undo."), {})
          << tool(QStringLiteral("projects"), QStringLiteral("List projects with task counts."), {}, {}, true)
          << tool(QStringLiteral("summary"), QStringLiteral("Short summary of this week or month, with overdue count: "
                                                            "good context before planning."),
                  {{QStringLiteral("period"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                          {QStringLiteral("enum"), QJsonArray{QStringLiteral("week"), QStringLiteral("month")}}}}},
                  {}, true)
          << tool(QStringLiteral("propose"), QStringLiteral("Ask the AI engine to turn free text into tasks. Stores nothing: "
                                                            "show the proposal to the user, then call approve."),
                  proposeFields, {QStringLiteral("text")})
          << tool(QStringLiteral("approve"), QStringLiteral("Store a proposal from propose (optionally edited)."),
                  {{QStringLiteral("proposal"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object"),},
                                                            {QStringLiteral("description"), QStringLiteral("The \"proposal\" object from propose: {\"tasks\": [...]}.")}}},
                   {QStringLiteral("capture_id"), prop(QStringLiteral("string"), QStringLiteral("capture_id from propose."))}},
                  {QStringLiteral("proposal")})
          << tool(QStringLiteral("add"), QStringLiteral("Free text to tasks via AI and store them at once. Only when the user "
                                                        "asked to save directly; otherwise use propose."),
                  proposeFields, {QStringLiteral("text")});
    return {{QStringLiteral("tools"), tools}};
}

static QJsonObject textResult(const ActionResult &result) {
    const QString text = result.ok ? QString::fromUtf8(QJsonDocument(result.payload).toJson(QJsonDocument::Compact))
                                   : result.error;
    QJsonObject out{{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                       {QStringLiteral("text"), text}}}},
                    {QStringLiteral("isError"), !result.ok}};
    if (result.ok)
        out.insert(QStringLiteral("structuredContent"), result.payload);
    return out;
}

static ActionResult failure(const QString &message) {
    return {false, message, {}, {}};
}

// JSON arguments as the CLI's text fields (numbers become "2").
static Actions::Fields fieldsOf(const QJsonObject &arguments, const QStringList &skip) {
    Actions::Fields fields;
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (skip.contains(it.key()))
            continue;
        fields.insert(it.key(), it.value().isDouble() ? QString::number(it.value().toDouble())
                                                      : it.value().toVariant().toString());
    }
    return fields;
}

// Holds the arguments to the tool's inputSchema, in the subset the tools use:
// required keys, no unknown keys, types, enums and integer bounds. Integer
// fields also take text ("2", or "" to clear), as the CLI does; `aliases`
// are the 0.1 field names, still accepted.
static QString schemaError(const QJsonObject &schema, const QJsonObject &args, const QHash<QString, QString> &aliases) {
    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    for (const QJsonValue &required : schema.value(QStringLiteral("required")).toArray()) {
        const QString key = required.toString();
        if (!args.contains(key) && !args.contains(aliases.key(key)))
            return QStringLiteral("Missing %1").arg(key);
    }
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!properties.contains(it.key())) {
            if (aliases.contains(it.key()))
                continue;
            return QStringLiteral("Unknown argument: %1 (expected one of: %2)")
                .arg(it.key(), properties.keys().join(QStringLiteral(", ")));
        }
        const QJsonObject property = properties.value(it.key()).toObject();
        const QString type = property.value(QStringLiteral("type")).toString();
        const QJsonValue value = it.value();
        bool typeOk = true;
        if (type == QLatin1String("string"))
            typeOk = value.isString();
        else if (type == QLatin1String("integer"))
            typeOk = value.isString() || (value.isDouble() && value.toDouble() == double(qint64(value.toDouble())));
        else if (type == QLatin1String("object"))
            typeOk = value.isObject();
        if (!typeOk)
            return QStringLiteral("%1 must be a%2 %3").arg(it.key(), type == QLatin1String("integer") ? QStringLiteral("n") : QString(), type);
        const QJsonArray allowed = property.value(QStringLiteral("enum")).toArray();
        if (!allowed.isEmpty() && !allowed.contains(value)) {
            QStringList names;
            for (const QJsonValue &v : allowed)
                names << v.toString();
            return QStringLiteral("%1 must be one of: %2").arg(it.key(), names.join(QStringLiteral(", ")));
        }
        if (value.isDouble()) {
            if (property.contains(QStringLiteral("minimum")) && value.toDouble() < property.value(QStringLiteral("minimum")).toDouble())
                return QStringLiteral("%1 must be at least %2").arg(it.key()).arg(property.value(QStringLiteral("minimum")).toInt());
            if (property.contains(QStringLiteral("maximum")) && value.toDouble() > property.value(QStringLiteral("maximum")).toDouble())
                return QStringLiteral("%1 must be at most %2").arg(it.key()).arg(property.value(QStringLiteral("maximum")).toInt());
        }
    }
    return {};
}

QJsonObject McpServer::callTool(const QString &name, const QJsonObject &args) {
    QJsonObject schema;
    for (const QJsonValue &tool : toolList().value(QStringLiteral("tools")).toArray())
        if (tool.toObject().value(QStringLiteral("name")).toString() == name)
            schema = tool.toObject().value(QStringLiteral("inputSchema")).toObject();
    if (schema.isEmpty())
        return {};
    const bool taskFields = name == QLatin1String("new") || name == QLatin1String("edit");
    const QString invalid = schemaError(schema, args, taskFields ? Actions::legacyFieldNames() : QHash<QString, QString>());
    if (!invalid.isEmpty())
        return textResult(failure(invalid));

    const QString source = QStringLiteral("mcp");
    const QString id = args.value(QStringLiteral("id")).toString();
    const auto needId = [&]() { return id.isEmpty() ? failure(QStringLiteral("Missing id")) : ActionResult{true, {}, {}, {}}; };
    QString error;

    if (name == QLatin1String("list")) {
        ListFilter filter;
        for (const QString &key : {QStringLiteral("from"), QStringLiteral("to")}) {
            if (!args.contains(key))
                continue;
            QDate *date = key == QLatin1String("from") ? &filter.from : &filter.to;
            if (!Actions::parseFilterDate(args.value(key).toString(), QDate::currentDate(), date, &error))
                return textResult(failure(key + QStringLiteral(": ") + error));
            (key == QLatin1String("from") ? filter.hasFrom : filter.hasTo) = true;
        }
        filter.project = args.value(QStringLiteral("project")).toString();
        filter.status = args.value(QStringLiteral("status")).toString();
        return textResult(m_actions->list(filter));
    }
    if (name == QLatin1String("show"))
        return textResult(id.isEmpty() ? needId() : m_actions->show(id));
    if (name == QLatin1String("new")) {
        Task task;
        if (!Actions::taskFromFields(fieldsOf(args, {}), &task, &error))
            return textResult(failure(error));
        return textResult(m_actions->create(task, source));
    }
    if (name == QLatin1String("edit")) {
        if (id.isEmpty())
            return textResult(needId());
        QJsonObject patch;
        if (!Actions::patchFromFields(fieldsOf(args, {QStringLiteral("id")}), &patch, &error))
            return textResult(failure(error));
        if (patch.isEmpty())
            return textResult(failure(QStringLiteral("Nothing to change: give at least one field")));
        return textResult(m_actions->update(id, patch, source));
    }
    if (name == QLatin1String("move")) {
        if (id.isEmpty())
            return textResult(needId());
        return textResult(m_actions->moveTo(id, args.value(QStringLiteral("when")).toString(), source));
    }
    if (name == QLatin1String("done") || name == QLatin1String("undone"))
        return textResult(id.isEmpty() ? needId() : m_actions->setDone(id, name == QLatin1String("done"), source));
    if (name == QLatin1String("rm"))
        return textResult(id.isEmpty() ? needId() : m_actions->remove(id, source));
    if (name == QLatin1String("undo"))
        return textResult(m_actions->undo(source));
    if (name == QLatin1String("redo"))
        return textResult(m_actions->redo(source));
    if (name == QLatin1String("projects"))
        return textResult(m_actions->projects());
    if (name == QLatin1String("summary"))
        return textResult(m_actions->summary(args.value(QStringLiteral("period")).toString() != QLatin1String("month")));
    if (name == QLatin1String("propose") || name == QLatin1String("add")) {
        const QString text = args.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty())
            return textResult(failure(QStringLiteral("Missing text")));
        QString captureId;
        ActionResult proposed = m_actions->propose(text, args.value(QStringLiteral("engine")).toString(), {}, {}, &captureId);
        if (!proposed.ok || name == QLatin1String("propose"))
            return textResult(proposed);
        const ActionResult approved = m_actions->approve(proposed.payload.value(QStringLiteral("proposal")).toObject(),
                                                         captureId, source);
        if (!approved.ok)
            return textResult(approved);
        proposed.payload.insert(QStringLiteral("stored"), approved.payload.value(QStringLiteral("stored")));
        return textResult(proposed);
    }
    if (name == QLatin1String("approve")) {
        const QJsonObject proposal = args.value(QStringLiteral("proposal")).toObject();
        if (proposal.isEmpty())
            return textResult(failure(QStringLiteral("Missing proposal")));
        return textResult(m_actions->approve(proposal, args.value(QStringLiteral("capture_id")).toString(), source));
    }
    return {};
}

static QJsonObject errorResponse(const QJsonValue &id, int code, const QString &message) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

static QJsonObject resultResponse(const QJsonValue &id, const QJsonObject &result) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
}

QJsonValue McpServer::handle(const QJsonValue &message) {
    if (!message.isObject())
        return errorResponse(QJsonValue::Null, -32600, QStringLiteral("Invalid Request: expected one JSON object"));
    const QJsonObject request = message.toObject();
    const QString method = request.value(QStringLiteral("method")).toString();
    // Notifications (no id) never get a response.
    if (!request.contains(QStringLiteral("id")))
        return QJsonValue::Undefined;
    const QJsonValue id = request.value(QStringLiteral("id"));
    if (request.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0") || method.isEmpty())
        return errorResponse(id, -32600, QStringLiteral("Invalid Request"));
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (method == QLatin1String("initialize")) {
        const QString wanted = params.value(QStringLiteral("protocolVersion")).toString();
        return resultResponse(id, {
            {QStringLiteral("protocolVersion"), kProtocolVersions.contains(wanted) ? wanted : kProtocolVersions.first()},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}}}},
            {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("omaplan")},
                                                      {QStringLiteral("version"), QCoreApplication::applicationVersion()}}},
            {QStringLiteral("instructions"),
             QStringLiteral("Omaplan is the user's task plan. Use summary or list for context. For free text, "
                            "call propose and show the proposal before approve, unless the user asked to save "
                            "directly (then add). Every change can be undone with undo.")}});
    }
    if (method == QLatin1String("ping"))
        return resultResponse(id, {});
    if (method == QLatin1String("tools/list"))
        return resultResponse(id, toolList());
    if (method == QLatin1String("tools/call")) {
        const QString name = params.value(QStringLiteral("name")).toString();
        const QJsonObject result = callTool(name, params.value(QStringLiteral("arguments")).toObject());
        if (result.isEmpty())
            return errorResponse(id, -32602, QStringLiteral("Unknown tool: %1").arg(name));
        return resultResponse(id, result);
    }
    return errorResponse(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

int McpServer::serve(QIODevice *in, QIODevice *out) {
    while (true) {
        // readLine blocks until a full line or EOF; only EOF gives nothing.
        const QByteArray line = in->readLine();
        if (line.isEmpty())
            break;
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        QJsonValue response;
        if (parseError.error != QJsonParseError::NoError)
            response = errorResponse(QJsonValue::Null, -32700, QStringLiteral("Parse error: %1").arg(parseError.errorString()));
        else
            response = handle(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()));
        if (response.isUndefined())
            continue;
        out->write(QJsonDocument(response.toObject()).toJson(QJsonDocument::Compact));
        out->write("\n");
        if (auto *file = qobject_cast<QFileDevice *>(out))
            file->flush(); // the client waits for each answer
        else
            out->waitForBytesWritten(-1);
    }
    return 0;
}
