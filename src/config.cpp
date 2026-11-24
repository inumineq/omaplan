#include "config.h"
#include "omarchy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

QString AppConfig::defaultPath() {
    const QByteArray override = qgetenv("OMAPLAN_CONFIG");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).filePath("omaplan/config.toml");
}

namespace {

// The 0.1 section and key names, read as their English ones.
QString canonicalSection(const QString &section) {
    if (section == QLatin1String("motor"))
        return QStringLiteral("engine");
    if (section == QLatin1String("motor.lokal"))
        return QStringLiteral("engine.local");
    return section;
}

QString canonicalKey(const QString &section, const QString &key) {
    if (section == QLatin1String("engine") && key == QLatin1String("engine"))
        return QStringLiteral("default");
    return key;
}

// One line of the minimal TOML subset: [section] headers and key = value
// lines. Comments (# ...) and blank lines are Other.
struct Line {
    enum Kind { Other, Section, Value } kind = Other;
    QString name;        // the section of a header, the key of a value line
    QString value;       // unquoted, unescaped
    int valueStart = -1; // where the value is in the raw line
    int valueEnd = -1;   // one past it: a comment after it is kept on write
};

// A quoted string from text[at] (" or '); returns its content and sets *end
// past the closing quote. In "double quotes" \" and \\ are escapes, as in
// TOML ('single' quotes are literal).
QString readQuoted(const QString &text, int at, int *end) {
    const QChar quote = text.at(at);
    QString out;
    int i = at + 1;
    for (; i < text.size() && text.at(i) != quote; ++i) {
        if (quote == QLatin1Char('"') && text.at(i) == QLatin1Char('\\') && i + 1 < text.size()
            && (text.at(i + 1) == QLatin1Char('"') || text.at(i + 1) == QLatin1Char('\\')))
            ++i;
        out += text.at(i);
    }
    *end = qMin(i + 1, int(text.size()));
    return out;
}

Line parseLine(const QString &raw) {
    Line line;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
        return line;
    if (trimmed.startsWith(QLatin1Char('['))) {
        const int close = trimmed.indexOf(QLatin1Char(']'));
        if (close > 0) {
            line.kind = Line::Section;
            line.name = trimmed.mid(1, close - 1).trimmed();
        }
        return line;
    }
    // The key: bare, or quoted ("Client A" = ...).
    int start = 0;
    while (start < raw.size() && raw.at(start).isSpace())
        ++start;
    int eq;
    if (raw.at(start) == QLatin1Char('"') || raw.at(start) == QLatin1Char('\'')) {
        int end = start;
        line.name = readQuoted(raw, start, &end);
        eq = raw.indexOf(QLatin1Char('='), end);
    } else {
        eq = raw.indexOf(QLatin1Char('='), start);
        line.name = raw.mid(start, eq - start).trimmed();
    }
    if (eq < 0 || line.name.isEmpty())
        return line;
    line.kind = Line::Value;
    int v = eq + 1;
    while (v < raw.size() && raw.at(v).isSpace())
        ++v;
    line.valueStart = v;
    // A quoted value ends at its closing quote, so a comment after it
    // ("vibe"   # default engine) is dropped; an unquoted value ends at #.
    if (v < raw.size() && (raw.at(v) == QLatin1Char('"') || raw.at(v) == QLatin1Char('\''))) {
        line.value = readQuoted(raw, v, &line.valueEnd);
    } else {
        const int hash = raw.indexOf(QLatin1Char('#'), v);
        line.valueEnd = hash < 0 ? raw.size() : hash;
        while (line.valueEnd > v && raw.at(line.valueEnd - 1).isSpace())
            --line.valueEnd;
        line.value = raw.mid(v, line.valueEnd - v);
    }
    return line;
}

QStringList splitLines(const QString &text) {
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &l : lines)
        if (l.endsWith(QLatin1Char('\r')))
            l.chop(1);
    return lines;
}

// A TOML string: "..." with \ and " escaped. The timeout is a bare number.
QString formatValue(const QString &key, const QString &value) {
    static const QRegularExpression number(QStringLiteral("^\\d+$"));
    if (key == QLatin1String("timeout") && number.match(value).hasMatch())
        return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString formatKey(const QString &key) {
    static const QRegularExpression bare(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (bare.match(key).hasMatch())
        return key;
    return formatValue(QString(), key);
}

} // namespace

QString AppConfig::defaultEngine() {
    const QString omarchy = Omarchy::defaultEngine();
    return omarchy.isEmpty() ? QStringLiteral("vibe") : omarchy;
}

QString AppConfig::defaultLocalApi(const QString &endpoint) {
    return endpoint.isEmpty() && Omarchy::hasLocalModels() ? QStringLiteral("omarchy") : QStringLiteral("ollama");
}

AppConfig AppConfig::load(const QString &path) {
    AppConfig config;
    bool engineSet = false;
    bool &localApiSet = config.localApiSet;
    // What the file leaves open follows Omarchy: its default agent, and its
    // own local model when no endpoint is given.
    const auto withDefaults = [&] {
        if (!engineSet)
            config.engine = defaultEngine();
        if (!localApiSet)
            config.localApi = defaultLocalApi(config.localEndpoint);
        return config;
    };
    QFile file(path.isEmpty() ? defaultPath() : path);
    if (!file.open(QIODevice::ReadOnly))
        return withDefaults(); // no config file: defaults
    QString section;
    // Engine ids of 0.1: "lokal" is now "local".
    const auto engineId = [](const QString &id) {
        return id == QLatin1String("lokal") ? QStringLiteral("local") : id;
    };
    for (const QString &raw : splitLines(QString::fromUtf8(file.readAll()))) {
        const Line line = parseLine(raw);
        if (line.kind == Line::Section) {
            section = canonicalSection(line.name);
            continue;
        }
        if (line.kind != Line::Value)
            continue;
        const QString key = canonicalKey(section, line.name);
        const QString &value = line.value;
        if (section.isEmpty()) {
            if (key == QLatin1String("language"))
                config.language = value.toLower();
        } else if (section == QLatin1String("engine")) {
            if (key == QLatin1String("default")) {
                config.engine = engineId(value);
                engineSet = !value.isEmpty();
            }
            else if (key == QLatin1String("command"))
                config.command = value;
            else if (key == QLatin1String("compare")) {
                // compare = "vibe, claude": the two engines of Compare.
                const QStringList ids = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (ids.size() == 2)
                    config.compare = {engineId(ids.at(0).trimmed()), engineId(ids.at(1).trimmed())};
            } else if (key == QLatin1String("timeout")) {
                bool ok = false;
                const int seconds = value.toInt(&ok);
                if (ok)
                    config.timeoutSeconds = qBound(minTimeoutSeconds, seconds, maxTimeoutSeconds);
            }
        } else if (section == QLatin1String("engine.vibe")) {
            if (key == QLatin1String("model"))
                config.vibeModel = value;
        } else if (section == QLatin1String("engine.claude")) {
            if (key == QLatin1String("model"))
                config.claudeModel = value;
        } else if (section == QLatin1String("engine.local")) {
            if (key == QLatin1String("api")) {
                config.localApi = value.toLower();
                localApiSet = !value.isEmpty();
            }
            else if (key == QLatin1String("endpoint"))
                config.localEndpoint = value;
            else if (key == QLatin1String("model"))
                config.localModel = value;
        } else if (section == QLatin1String("dates")) {
            const QString v = value.toLower() == QLatin1String("auto") ? QString() : value.toLower();
            if (key == QLatin1String("order"))
                config.dateOrder = v;
            else if (key == QLatin1String("dotted"))
                config.dateDotted = v;
            else if (key == QLatin1String("week_start"))
                config.weekStart = v;
            else if (key == QLatin1String("clock"))
                config.clock = v;
        } else if (section == QLatin1String("projects")) {
            if (const QString slot = ConfigKeys::paletteSlot(value); !slot.isEmpty())
                config.projectColors.insert(key, slot);
        } else if (section == QLatin1String("snapshot")) {
            if (key == QLatin1String("path"))
                config.snapshotPath = value.startsWith(QLatin1String("~/")) ? QDir::homePath() + value.mid(1) : value;
        }
    }
    return withDefaults();
}

namespace ConfigKeys {

const QList<ConfigKey> &all() {
    static const QStringList engines = {QStringLiteral("vibe"), QStringLiteral("claude"), QStringLiteral("local"),
                                        QStringLiteral("command")};
    static const QList<ConfigKey> keys = {
        {QStringLiteral("engine.default"), QStringLiteral("engine"), QStringLiteral("default"), engines,
         QStringLiteral("vibe"),
         QStringLiteral("The AI engine a new input uses; unset follows Omarchy's default agent (claude or local), else vibe.")},
        {QStringLiteral("engine.vibe.model"), QStringLiteral("engine.vibe"), QStringLiteral("model"), {}, {},
         QStringLiteral("Model alias passed to vibe --model; empty uses vibe's own active model.")},
        {QStringLiteral("engine.claude.model"), QStringLiteral("engine.claude"), QStringLiteral("model"), {}, {},
         QStringLiteral("Model passed to claude --model (e.g. sonnet, opus); empty uses claude's default.")},
        {QStringLiteral("engine.local.api"), QStringLiteral("engine.local"), QStringLiteral("api"),
         {QStringLiteral("ollama"), QStringLiteral("openai"), QStringLiteral("omarchy")}, QStringLiteral("ollama"),
         QStringLiteral("The local endpoint's API: Ollama's /api/chat, OpenAI-compatible /v1/chat/completions, or "
                        "omarchy for the local model Omarchy runs (no endpoint needed; the default when it is set up).")},
        {QStringLiteral("engine.local.endpoint"), QStringLiteral("engine.local"), QStringLiteral("endpoint"), {}, {},
         QStringLiteral("URL of the local model server, e.g. http://127.0.0.1:11434.")},
        {QStringLiteral("engine.local.model"), QStringLiteral("engine.local"), QStringLiteral("model"), {}, {},
         QStringLiteral("Model name on the local server.")},
        {QStringLiteral("engine.command"), QStringLiteral("engine"), QStringLiteral("command"), {}, {},
         QStringLiteral("Command line for the command engine: reads the prompt on stdin, writes JSON on stdout.")},
        {QStringLiteral("engine.compare"), QStringLiteral("engine"), QStringLiteral("compare"), {},
         QStringLiteral("vibe, claude"), QStringLiteral("The two engines of Compare, e.g. \"vibe, claude\".")},
        {QStringLiteral("engine.timeout"), QStringLiteral("engine"), QStringLiteral("timeout"), {},
         QString::number(AppConfig::defaultTimeoutSeconds),
         QStringLiteral("Seconds an engine may take before Omaplan gives up (10-600).")},
        {QStringLiteral("language"), QString(), QStringLiteral("language"), {}, {},
         QStringLiteral("Interface language (da, en); empty follows Omarchy and the system.")},
        {QStringLiteral("dates.order"), QStringLiteral("dates"), QStringLiteral("order"),
         {QStringLiteral("auto"), QStringLiteral("dmy"), QStringLiteral("mdy")}, QStringLiteral("auto"),
         QStringLiteral("How 12/11 is read: day/month (dmy) or month/day (mdy); auto follows the language.")},
        {QStringLiteral("dates.dotted"), QStringLiteral("dates"), QStringLiteral("dotted"),
         {QStringLiteral("auto"), QStringLiteral("time"), QStringLiteral("date")}, QStringLiteral("auto"),
         QStringLiteral("Whether 12.12 is a time or a date; auto follows the language.")},
        {QStringLiteral("dates.week_start"), QStringLiteral("dates"), QStringLiteral("week_start"),
         {QStringLiteral("auto"), QStringLiteral("monday"), QStringLiteral("sunday")}, QStringLiteral("auto"),
         QStringLiteral("First day of the week; auto follows the language.")},
        {QStringLiteral("dates.clock"), QStringLiteral("dates"), QStringLiteral("clock"),
         {QStringLiteral("auto"), QStringLiteral("24h"), QStringLiteral("12h")}, QStringLiteral("auto"),
         QStringLiteral("How times are shown: 24h or 12h; auto follows the language.")},
        {QStringLiteral("snapshot.path"), QStringLiteral("snapshot"), QStringLiteral("path"), {}, {},
         QStringLiteral("Where plan.json for the read-only edition is written; empty = next to the database.")},
    };
    return keys;
}

bool find(const QString &name, ConfigKey *out) {
    for (const ConfigKey &key : all())
        if (key.name == name) {
            *out = key;
            return true;
        }
    static const QString prefix = QStringLiteral("projects.");
    if (name.startsWith(prefix) && name.size() > prefix.size()) {
        *out = {name, QStringLiteral("projects"), name.mid(prefix.size()), {}, {},
                QStringLiteral("The project's colour: a slot of the theme palette (red, orange, yellow, green, cyan, "
                               "blue, magenta, brown, or bright_ before red, yellow, green, cyan, blue, magenta).")};
        return true;
    }
    return false;
}

const QStringList &paletteSlots() {
    static const QStringList names = {
        QStringLiteral("red"),        QStringLiteral("orange"),        QStringLiteral("yellow"),
        QStringLiteral("green"),      QStringLiteral("cyan"),          QStringLiteral("blue"),
        QStringLiteral("magenta"),    QStringLiteral("brown"),         QStringLiteral("bright_red"),
        QStringLiteral("bright_yellow"), QStringLiteral("bright_green"), QStringLiteral("bright_cyan"),
        QStringLiteral("bright_blue"), QStringLiteral("bright_magenta")};
    return names;
}

QString paletteSlot(const QString &value) {
    const QString v = value.trimmed().toLower();
    if (paletteSlots().contains(v))
        return v;
    // The terminal's numbering (ANSI): 1-6 and their bright 9-14.
    static const QHash<QString, QString> numbers = {
        {QStringLiteral("color1"), QStringLiteral("red")},           {QStringLiteral("color2"), QStringLiteral("green")},
        {QStringLiteral("color3"), QStringLiteral("yellow")},        {QStringLiteral("color4"), QStringLiteral("blue")},
        {QStringLiteral("color5"), QStringLiteral("magenta")},       {QStringLiteral("color6"), QStringLiteral("cyan")},
        {QStringLiteral("color9"), QStringLiteral("bright_red")},    {QStringLiteral("color10"), QStringLiteral("bright_green")},
        {QStringLiteral("color11"), QStringLiteral("bright_yellow")}, {QStringLiteral("color12"), QStringLiteral("bright_blue")},
        {QStringLiteral("color13"), QStringLiteral("bright_magenta")}, {QStringLiteral("color14"), QStringLiteral("bright_cyan")}};
    return numbers.value(v);
}

bool isPaletteSlot(const QString &value) { return !paletteSlot(value).isEmpty(); }

bool agentMaySet(const ConfigKey &key) {
    static const QStringList keys = {
        QStringLiteral("language"),          QStringLiteral("engine.default"),     QStringLiteral("engine.compare"),
        QStringLiteral("engine.timeout"),    QStringLiteral("engine.vibe.model"),  QStringLiteral("engine.claude.model"),
        QStringLiteral("engine.local.model")};
    return keys.contains(key.name) || key.section == QLatin1String("dates") || key.section == QLatin1String("projects");
}

bool hasControlCharacter(const QString &text) {
    for (const QChar c : text)
        if (c.unicode() < 0x20 || (c.unicode() >= 0x7f && c.unicode() <= 0x9f) // C0, DEL, C1 (with U+0085)
            || c.unicode() == 0x2028 || c.unicode() == 0x2029)                // line and paragraph separator
            return true;
    return false;
}

QString validate(const ConfigKey &key, const QString &value) {
    // A line break would split the line in the file into new keys.
    if (hasControlCharacter(value))
        return QStringLiteral("%1 may not contain a control character (line break, tab, ...)").arg(key.name);
    if (key.section == QLatin1String("projects") && hasControlCharacter(key.key))
        return QStringLiteral("a project name may not contain a control character (line break, tab, ...)");
    if (value.isEmpty())
        return {};
    if (!key.choices.isEmpty() && !key.choices.contains(value))
        return QStringLiteral("%1 must be one of: %2").arg(key.name, key.choices.join(QStringLiteral(", ")));
    if (key.name == QLatin1String("engine.timeout")) {
        bool ok = false;
        const int seconds = value.toInt(&ok);
        if (!ok || seconds < AppConfig::minTimeoutSeconds || seconds > AppConfig::maxTimeoutSeconds)
            return QStringLiteral("engine.timeout must be a whole number of seconds from %1 to %2")
                .arg(AppConfig::minTimeoutSeconds).arg(AppConfig::maxTimeoutSeconds);
    }
    if (key.name == QLatin1String("engine.compare")) {
        const QStringList ids = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        bool ok = ids.size() == 2;
        for (const QString &id : ids)
            ok = ok && all().first().choices.contains(id.trimmed());
        if (!ok)
            return QStringLiteral("engine.compare must name two engines, e.g. \"vibe, claude\"");
    }
    if (key.name == QLatin1String("language")) {
        static const QRegularExpression code(QStringLiteral("^[a-z]{2}$"));
        if (!code.match(value).hasMatch())
            return QStringLiteral("language must be a two-letter code such as da or en");
    }
    if (key.section == QLatin1String("projects") && !isPaletteSlot(value))
        return QStringLiteral("a project colour must be a palette slot: %1").arg(paletteSlots().join(QStringLiteral(", ")));
    return {};
}

} // namespace ConfigKeys

ConfigFile::ConfigFile(const QString &path)
    : m_path(path.isEmpty() ? AppConfig::defaultPath() : path)
{
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        m_lines = splitLines(QString::fromUtf8(file.readAll()));
        if (!m_lines.isEmpty() && m_lines.last().isEmpty())
            m_lines.removeLast(); // the final newline
    }
}

ConfigFile::Location ConfigFile::locate(const QString &section, const QString &key) const {
    Location where;
    QString current;
    bool seen = section.isEmpty(); // inside or past the first matching section
    bool left = false;             // past the end of the first matching section
    int lastContent = -1;          // its header or last key line
    for (int i = 0; i < m_lines.size(); ++i) {
        const Line line = parseLine(m_lines.at(i));
        if (line.kind == Line::Section) {
            if (seen && current == section)
                left = true;
            current = canonicalSection(line.name);
            if (current == section && !seen) {
                seen = true;
                lastContent = i;
            }
            continue;
        }
        if (line.kind != Line::Value || current != section)
            continue;
        if (canonicalKey(section, line.name) == key) {
            where.line = i; // the last one wins, as in AppConfig::load
            where.lines << i;
        }
        if (!left)
            lastContent = i;
    }
    if (seen)
        where.sectionEnd = lastContent + 1;
    return where;
}

QString ConfigFile::value(const QString &section, const QString &key, bool *found) const {
    const Location where = locate(section, key);
    if (found)
        *found = where.line >= 0;
    return where.line >= 0 ? parseLine(m_lines.at(where.line)).value : QString();
}

QStringList ConfigFile::keys(const QString &section) const {
    QStringList out;
    QString current;
    for (const QString &raw : m_lines) {
        const Line line = parseLine(raw);
        if (line.kind == Line::Section)
            current = canonicalSection(line.name);
        else if (line.kind == Line::Value && current == section && !out.contains(line.name))
            out << line.name;
    }
    return out;
}

void ConfigFile::set(const QString &section, const QString &key, const QString &value) {
    const Location where = locate(section, key);
    const QString formatted = formatValue(key, value);
    if (where.line >= 0) {
        QString &raw = m_lines[where.line];
        const Line line = parseLine(raw);
        raw = raw.left(line.valueStart) + formatted + raw.mid(line.valueEnd);
        return;
    }
    const QString newLine = formatKey(key) + QStringLiteral(" = ") + formatted;
    if (section.isEmpty()) {
        // Top-level keys go before the first section.
        int at = 0;
        while (at < m_lines.size() && parseLine(m_lines.at(at)).kind != Line::Section)
            ++at;
        while (at > 0 && m_lines.at(at - 1).trimmed().isEmpty())
            --at;
        m_lines.insert(at, newLine);
        return;
    }
    if (where.sectionEnd >= 0) {
        m_lines.insert(where.sectionEnd, newLine);
        return;
    }
    if (!m_lines.isEmpty() && !m_lines.last().trimmed().isEmpty())
        m_lines << QString();
    m_lines << QLatin1Char('[') + section + QLatin1Char(']') << newLine;
}

void ConfigFile::unset(const QString &section, const QString &key) {
    const Location where = locate(section, key);
    for (int i = where.lines.size() - 1; i >= 0; --i)
        m_lines.removeAt(where.lines.at(i));
}

bool ConfigFile::save(QString *error) const {
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(dir)) {
        if (error) *error = QStringLiteral("cannot create directory %1").arg(dir);
        return false;
    }
    QSaveFile file(m_path);
    // A folder no temp file can be made in fails the save; the old config is
    // never truncated and rewritten in place.
    file.setDirectWriteFallback(false);
    const QByteArray bytes = (text() + QLatin1Char('\n')).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("%1: %2").arg(m_path, file.errorString());
        return false;
    }
    return true;
}
