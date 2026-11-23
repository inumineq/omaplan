#include "proposal.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <cmath>

// ---------------------------------------------------------------------------
// Schema access (docs/ai-schema.json is embedded as a Qt resource)
// ---------------------------------------------------------------------------

QJsonObject schemaObject()
{
    static const QJsonObject schema = [] {
        QFile f(QStringLiteral(":/ai-schema.json"));
        if (!f.open(QIODevice::ReadOnly))
            return QJsonObject();
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return schema;
}

QString schemaText()
{
    QFile f(QStringLiteral(":/ai-schema.json"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// ---------------------------------------------------------------------------
// Minimal JSON Schema validator for the subset used by ai-schema.json:
// type, required, properties, additionalProperties, items, minLength,
// maxLength, minimum, maximum, enum. Reading the rules from the embedded
// schema (instead of hardcoding them) means the schema file stays the single
// contract for every engine.
// ---------------------------------------------------------------------------

static bool isIntegerValue(const QJsonValue &v)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    return std::floor(d) == d && d >= -2147483648.0 && d <= 2147483647.0;
}

static bool typeMatches(const QJsonValue &v, const QString &type)
{
    if (type == QLatin1String("object"))
        return v.isObject();
    if (type == QLatin1String("array"))
        return v.isArray();
    if (type == QLatin1String("string"))
        return v.isString();
    if (type == QLatin1String("integer"))
        return isIntegerValue(v);
    if (type == QLatin1String("number"))
        return v.isDouble();
    if (type == QLatin1String("boolean"))
        return v.isBool();
    if (type == QLatin1String("null"))
        return v.isNull();
    return false;
}

static void validate(const QJsonValue &value, const QJsonValue &schemaValue,
                     const QString &path, QStringList &errors)
{
    const QJsonObject schema = schemaValue.toObject();
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (!type.isEmpty() && !typeMatches(value, type)) {
        errors.append(QStringLiteral("%1: expected type %2").arg(path, type));
        return; // deeper checks are meaningless on a type mismatch
    }

    if (const QJsonArray enumValues = schema.value(QStringLiteral("enum")).toArray();
        !enumValues.isEmpty()) {
        bool found = false;
        for (const QJsonValue &e : enumValues)
            if (e == value)
                found = true;
        if (!found)
            errors.append(QStringLiteral("%1: value not in enum").arg(path));
    }

    if (value.isString()) {
        const QString s = value.toString();
        const int minLength = schema.value(QStringLiteral("minLength")).toInt(-1);
        const int maxLength = schema.value(QStringLiteral("maxLength")).toInt(-1);
        if (minLength >= 0 && s.size() < minLength)
            errors.append(QStringLiteral("%1: shorter than minLength %2").arg(path).arg(minLength));
        if (maxLength >= 0 && s.size() > maxLength)
            errors.append(QStringLiteral("%1: longer than maxLength %2").arg(path).arg(maxLength));
        const QString pattern = schema.value(QStringLiteral("pattern")).toString();
        if (!pattern.isEmpty()
            && !QRegularExpression(pattern).match(s).hasMatch())
            errors.append(QStringLiteral("%1: does not match pattern %2").arg(path, pattern));
    }

    if (value.isDouble()) {
        const double d = value.toDouble();
        if (schema.contains(QStringLiteral("minimum")) && d < schema.value(QStringLiteral("minimum")).toDouble())
            errors.append(QStringLiteral("%1: below minimum").arg(path));
        if (schema.contains(QStringLiteral("maximum")) && d > schema.value(QStringLiteral("maximum")).toDouble())
            errors.append(QStringLiteral("%1: above maximum").arg(path));
    }

    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
        for (const QJsonValue &r : required) {
            const QString key = r.toString();
            if (!obj.contains(key))
                errors.append(QStringLiteral("%1: missing required property '%2'").arg(path, key));
        }
        const QJsonValue additional = schema.value(QStringLiteral("additionalProperties"));
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString key = it.key();
            if (properties.contains(key)) {
                validate(it.value(), properties.value(key), path + QLatin1Char('.') + key, errors);
            } else if (additional.isBool() && !additional.toBool()) {
                errors.append(QStringLiteral("%1: additional property '%2' not allowed").arg(path, key));
            } else if (additional.isObject()) {
                validate(it.value(), additional, path + QLatin1Char('.') + key, errors);
            }
        }
    }

    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        const int maxItems = schema.value(QStringLiteral("maxItems")).toInt(-1);
        if (maxItems >= 0 && arr.size() > maxItems)
            errors.append(QStringLiteral("%1: more than maxItems %2").arg(path).arg(maxItems));
        const QJsonValue items = schema.value(QStringLiteral("items"));
        if (items.isObject()) {
            for (int i = 0; i < arr.size(); ++i)
                validate(arr.at(i), items, QStringLiteral("%1[%2]").arg(path).arg(i), errors);
        }
    }
}

// ---------------------------------------------------------------------------
// The Danish keys of the schema in 0.1, mapped to the English ones.
// ---------------------------------------------------------------------------

static QJsonObject renameKeys(const QJsonObject &obj, const QHash<QString, QString> &names)
{
    QJsonObject out = obj;
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (!out.contains(it.key()))
            continue;
        const QJsonValue value = out.take(it.key());
        if (!out.contains(it.value()))
            out.insert(it.value(), value);
    }
    return out;
}

static QJsonArray englishGuesses(const QJsonArray &guesses)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("varighed_min"), QStringLiteral("duration_min")},
        {QStringLiteral("frist"), QStringLiteral("deadline")},
        {QStringLiteral("projekt"), QStringLiteral("project")},
        {QStringLiteral("prioritet"), QStringLiteral("priority")},
    };
    QJsonArray out;
    for (const QJsonValue &g : guesses)
        out.append(g.isString() ? QJsonValue(names.value(g.toString(), g.toString())) : g);
    return out;
}

QJsonObject withEnglishKeys(const QJsonObject &proposal)
{
    static const QHash<QString, QString> topNames = {
        {QStringLiteral("opgaver"), QStringLiteral("tasks")},
        {QStringLiteral("uafklaret"), QStringLiteral("unresolved")},
        {QStringLiteral("gaet"), QStringLiteral("guessed")},
        {QStringLiteral("guesses"), QStringLiteral("guessed")},
    };
    static const QHash<QString, QString> taskNames = {
        {QStringLiteral("titel"), QStringLiteral("title")},
        {QStringLiteral("noter"), QStringLiteral("notes")},
        {QStringLiteral("projekt"), QStringLiteral("project")},
        {QStringLiteral("prioritet"), QStringLiteral("priority")},
        {QStringLiteral("varighed_min"), QStringLiteral("duration_min")},
        {QStringLiteral("frist"), QStringLiteral("deadline")},
        {QStringLiteral("gaet"), QStringLiteral("guessed")},
        {QStringLiteral("guesses"), QStringLiteral("guessed")}, // the CLI's key before 0.2
        {QStringLiteral("uafklaret"), QStringLiteral("unresolved")},
    };
    QJsonObject out = renameKeys(proposal, topNames);
    if (out.value(QStringLiteral("guessed")).isArray())
        out.insert(QStringLiteral("guessed"), englishGuesses(out.value(QStringLiteral("guessed")).toArray()));
    if (out.value(QStringLiteral("tasks")).isArray()) {
        QJsonArray tasks;
        for (const QJsonValue &t : out.value(QStringLiteral("tasks")).toArray()) {
            if (!t.isObject()) {
                tasks.append(t);
                continue;
            }
            QJsonObject task = renameKeys(t.toObject(), taskNames);
            if (task.value(QStringLiteral("guessed")).isArray())
                task.insert(QStringLiteral("guessed"), englishGuesses(task.value(QStringLiteral("guessed")).toArray()));
            tasks.append(task);
        }
        out.insert(QStringLiteral("tasks"), tasks);
    }
    return out;
}

// ---------------------------------------------------------------------------
// JSON extraction: engines wrap the schema JSON in prose, ``` fences, or
// their own envelope objects ({"result": "..."} etc.).
// ---------------------------------------------------------------------------

static bool parseJsonObject(const QString &text, QJsonObject *out)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

// Returns the first candidate substring that parses as a JSON object.
static QJsonObject extractJsonObject(const QString &text, bool *found)
{
    const QString trimmed = text.trimmed();
    QJsonObject obj;

    // 1. The whole text is already a JSON object.
    if (parseJsonObject(trimmed, &obj)) {
        *found = true;
        return obj;
    }

    // 2. ```-fenced blocks (with or without a language tag).
    int searchFrom = 0;
    while (true) {
        const int open = trimmed.indexOf(QStringLiteral("```"), searchFrom);
        if (open < 0)
            break;
        const int close = trimmed.indexOf(QStringLiteral("```"), open + 3);
        if (close < 0)
            break;
        QString block = trimmed.mid(open + 3, close - open - 3);
        const int newline = block.indexOf(QLatin1Char('\n'));
        if (newline >= 0)
            block = block.mid(newline + 1); // drop a language tag like "json"
        if (parseJsonObject(block.trimmed(), &obj)) {
            *found = true;
            return obj;
        }
        searchFrom = close + 3;
    }

    // 3. First '{' to last '}' -- JSON embedded in surrounding prose.
    const int first = trimmed.indexOf(QLatin1Char('{'));
    const int last = trimmed.lastIndexOf(QLatin1Char('}'));
    if (first >= 0 && last > first && parseJsonObject(trimmed.mid(first, last - first + 1), &obj)) {
        *found = true;
        return obj;
    }

    *found = false;
    return QJsonObject();
}

// Unwraps engine envelopes: {"result": "..."} (claude -p, vibe --output json),
// {"response": "..."} (Ollama), {"message": {"content": "..."}} (chat format).
// The envelope's string payload may itself be fenced or wrapped, so extraction
// runs recursively. Stops as soon as an object shaped like the schema appears.
static QJsonObject unwrapEnvelopes(const QJsonObject &obj, bool *ok, int depth = 0)
{
    if (obj.contains(QStringLiteral("tasks")) || obj.contains(QStringLiteral("opgaver")) || depth > 8) {
        *ok = obj.contains(QStringLiteral("tasks")) || obj.contains(QStringLiteral("opgaver"));
        return obj;
    }
    QString payload;
    if (obj.value(QStringLiteral("result")).isString())
        payload = obj.value(QStringLiteral("result")).toString();
    else if (obj.value(QStringLiteral("response")).isString())
        payload = obj.value(QStringLiteral("response")).toString();
    else if (obj.value(QStringLiteral("choices")).isArray()
             && !obj.value(QStringLiteral("choices")).toArray().isEmpty()) {
        // OpenAI-compatible chat envelope: choices[0].message.content
        const QJsonObject choice = obj.value(QStringLiteral("choices")).toArray().first().toObject();
        const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("content")).isString())
            payload = message.value(QStringLiteral("content")).toString();
        else if (choice.value(QStringLiteral("text")).isString())
            payload = choice.value(QStringLiteral("text")).toString();
    }
    else if (obj.value(QStringLiteral("message")).isObject()
             && obj.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).isString())
        payload = obj.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    else if (obj.value(QStringLiteral("content")).isString())
        payload = obj.value(QStringLiteral("content")).toString();
    else if (obj.value(QStringLiteral("text")).isString())
        payload = obj.value(QStringLiteral("text")).toString();
    else if (obj.value(QStringLiteral("output")).isString())
        payload = obj.value(QStringLiteral("output")).toString();

    if (payload.isEmpty()) {
        *ok = false;
        return obj;
    }
    bool found = false;
    const QJsonObject inner = extractJsonObject(payload, &found);
    if (!found) {
        *ok = false;
        return obj;
    }
    return unwrapEnvelopes(inner, ok, depth + 1);
}

// ---------------------------------------------------------------------------
// Semantic mapping: schema-valid JSON -> ProposedTask. Dates must be ISO
// 8601 local date or date+time, otherwise the response is rejected.
// ---------------------------------------------------------------------------

static bool parseIsoDateTime(const QString &text, QDateTime *out, bool *hasTime)
{
    const QString trimmed = text.trimmed();
    if (QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODate); dt.isValid()) {
        *out = dt;
        *hasTime = trimmed.contains(QLatin1Char('T'));
        return true;
    }
    return false;
}

static QStringList validateAgainstSchema(const QJsonValue &value)
{
    QStringList errors;
    validate(value, schemaObject(), QStringLiteral("$"), errors);
    return errors;
}

// vibe --output json answers with a JSON array of events (user messages,
// reasoning, assistant messages). Only the LAST assistant message is the
// answer; its text parts are concatenated. Reasoning text is never used --
// in particular, the generic "find JSON in the text" fallback below must
// never see it, so arrays are unwrapped here before any text scanning.
static bool unwrapVibeEventArray(const QString &trimmed, QString *assistantText, QString *error)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        *error = QStringLiteral("Expected a JSON array of events, got: %1").arg(err.errorString());
        return false;
    }
    const QJsonArray events = doc.array();
    for (const QJsonValue &e : events) {
        const QJsonObject o = e.toObject();
        if (o.value(QStringLiteral("type")).toString() == QLatin1String("message")
            && o.value(QStringLiteral("role")).toString() == QLatin1String("assistant")) {
            QString text;
            const QJsonArray content = o.value(QStringLiteral("content")).toArray();
            for (const QJsonValue &c : content) {
                const QJsonObject part = c.toObject();
                if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                    text += part.value(QStringLiteral("text")).toString();
            }
            *assistantText = text; // a later assistant message replaces earlier ones
        }
    }
    if (assistantText->isEmpty()) {
        *error = QStringLiteral("The event array contains no assistant message.");
        return false;
    }
    return true;
}

ParseResult parseProposal(const QString &rawResponse)
{
    ParseResult result;
    const QString trimmed = rawResponse.trimmed();
    if (trimmed.isEmpty()) {
        result.error = QStringLiteral("Engine returned an empty response.\n\nRaw response:\n%1")
                           .arg(rawResponse);
        return result;
    }

    // Unwrap vibe's event array first; everything after this works on the
    // assistant text (or, for other engines, on the raw response).
    QString payload = trimmed;
    if (trimmed.startsWith(QLatin1Char('['))) {
        QString assistantText;
        QString unwrapError;
        if (!unwrapVibeEventArray(trimmed, &assistantText, &unwrapError)) {
            result.error = QStringLiteral("%1\n\nRaw response:\n%2").arg(unwrapError, rawResponse);
            return result;
        }
        payload = assistantText;
    }

    bool found = false;
    QJsonObject obj = extractJsonObject(payload, &found);
    if (!found) {
        result.error = QStringLiteral("No JSON object found in the engine response.\n\nRaw response:\n%1")
                           .arg(rawResponse);
        return result;
    }
    bool schemaShaped = false;
    obj = unwrapEnvelopes(obj, &schemaShaped);
    if (!schemaShaped) {
        result.error = QStringLiteral("No object matching the 'tasks' schema found in the engine response.\n\nRaw response:\n%1")
                           .arg(rawResponse);
        return result;
    }
    obj = withEnglishKeys(obj);

    // Some engines echo the schema's own metadata at the top level ($schema,
    // title, description, type). Ignore exactly those four keys when a valid
    // "tasks" array is present; anything else is still rejected.
    if (obj.contains(QStringLiteral("tasks"))) {
        for (const char *key : {"$schema", "title", "description", "type"})
            obj.remove(QString::fromLatin1(key));
        // Vibe sometimes puts "guessed" next to "tasks" instead of inside the
        // task. With exactly one task the meaning is unambiguous, so move it
        // in; with several it cannot be attributed and is dropped rather than
        // failing the whole proposal.
        if (obj.contains(QStringLiteral("guessed"))) {
            const QJsonValue topGuess = obj.take(QStringLiteral("guessed"));
            QJsonArray tasks = obj.value(QStringLiteral("tasks")).toArray();
            if (tasks.size() == 1 && topGuess.isArray()) {
                QJsonObject only = tasks.first().toObject();
                QJsonArray merged = only.value(QStringLiteral("guessed")).toArray();
                for (const QJsonValue &g : topGuess.toArray())
                    if (!merged.contains(g))
                        merged.append(g);
                only.insert(QStringLiteral("guessed"), merged);
                tasks.replace(0, only);
                obj.insert(QStringLiteral("tasks"), tasks);
            }
        }
        // The opposite slip: "unresolved" inside a task. It lists what was not
        // understood in the whole input, so it moves up to the top level.
        QJsonArray tasks = obj.value(QStringLiteral("tasks")).toArray();
        QJsonArray unresolved = obj.value(QStringLiteral("unresolved")).toArray();
        bool moved = false;
        for (qsizetype i = 0; i < tasks.size(); ++i) {
            QJsonObject task = tasks.at(i).toObject();
            if (!task.contains(QStringLiteral("unresolved")))
                continue;
            for (const QJsonValue &u : task.take(QStringLiteral("unresolved")).toArray())
                if (!unresolved.contains(u))
                    unresolved.append(u);
            tasks.replace(i, task);
            moved = true;
        }
        if (moved) {
            obj.insert(QStringLiteral("tasks"), tasks);
            if (!unresolved.isEmpty() || obj.contains(QStringLiteral("unresolved")))
                obj.insert(QStringLiteral("unresolved"), unresolved);
        }
    }

    const QStringList errors = validateAgainstSchema(obj);
    if (!errors.isEmpty()) {
        result.error = QStringLiteral("Response does not match docs/ai-schema.json: %1\n\nRaw response:\n%2")
                           .arg(errors.join(QStringLiteral("; ")), rawResponse);
        return result;
    }

    Proposal proposal;
    const QJsonArray tasks = obj.value(QStringLiteral("tasks")).toArray();
    for (const QJsonValue &t : tasks) {
        const QJsonObject o = t.toObject();
        ProposedTask task;
        task.title = o.value(QStringLiteral("title")).toString();
        task.notes = o.value(QStringLiteral("notes")).toString();
        task.project = o.value(QStringLiteral("project")).toString();
        if (o.contains(QStringLiteral("priority")))
            task.priority = o.value(QStringLiteral("priority")).toInt();
        if (o.contains(QStringLiteral("duration_min")))
            task.durationMin = o.value(QStringLiteral("duration_min")).toInt();
        if (o.contains(QStringLiteral("start"))) {
            if (!parseIsoDateTime(o.value(QStringLiteral("start")).toString(), &task.start, &task.startHasTime)) {
                result.error = QStringLiteral("Invalid ISO date/datetime in 'start': '%1'\n\nRaw response:\n%2")
                                   .arg(o.value(QStringLiteral("start")).toString(), rawResponse);
                return result;
            }
        }
        if (o.contains(QStringLiteral("deadline"))) {
            if (!parseIsoDateTime(o.value(QStringLiteral("deadline")).toString(), &task.deadline, &task.deadlineHasTime)) {
                result.error = QStringLiteral("Invalid ISO date/datetime in 'deadline': '%1'\n\nRaw response:\n%2")
                                   .arg(o.value(QStringLiteral("deadline")).toString(), rawResponse);
                return result;
            }
        }
        const QJsonArray guessed = o.value(QStringLiteral("guessed")).toArray();
        for (const QJsonValue &g : guessed)
            task.guessed.append(g.toString());
        proposal.tasks.append(task);
    }
    const QJsonArray unresolved = obj.value(QStringLiteral("unresolved")).toArray();
    for (const QJsonValue &u : unresolved)
        proposal.unresolved.append(u.toString());

    result.ok = true;
    result.proposal = proposal;
    return result;
}

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------

bool ProposedTask::operator==(const ProposedTask &other) const
{
    return title == other.title && notes == other.notes && project == other.project
        && priority == other.priority && start == other.start && startHasTime == other.startHasTime
        && durationMin == other.durationMin && deadline == other.deadline
        && deadlineHasTime == other.deadlineHasTime && guessed == other.guessed;
}

QJsonObject ProposedTask::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("title"), title);
    if (!notes.isEmpty())
        o.insert(QStringLiteral("notes"), notes);
    if (!project.isEmpty())
        o.insert(QStringLiteral("project"), project);
    if (priority != 0)
        o.insert(QStringLiteral("priority"), priority);
    if (start.isValid())
        o.insert(QStringLiteral("start"),
                 startHasTime ? start.toString(Qt::ISODate) : start.date().toString(Qt::ISODate));
    if (durationMin > 0)
        o.insert(QStringLiteral("duration_min"), durationMin);
    if (deadline.isValid())
        o.insert(QStringLiteral("deadline"),
                 deadlineHasTime ? deadline.toString(Qt::ISODate) : deadline.date().toString(Qt::ISODate));
    if (!guessed.isEmpty()) {
        QJsonArray g;
        for (const QString &field : guessed)
            g.append(field);
        o.insert(QStringLiteral("guessed"), g);
    }
    return o;
}

bool Proposal::operator==(const Proposal &other) const
{
    return tasks == other.tasks && unresolved == other.unresolved;
}

QJsonObject Proposal::toJson() const
{
    QJsonObject o;
    QJsonArray arr;
    for (const ProposedTask &t : tasks)
        arr.append(t.toJson());
    o.insert(QStringLiteral("tasks"), arr);
    if (!unresolved.isEmpty()) {
        QJsonArray u;
        for (const QString &s : unresolved)
            u.append(s);
        o.insert(QStringLiteral("unresolved"), u);
    }
    return o;
}
