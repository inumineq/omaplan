#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// ~/.config/omaplan/config.toml, parsed by hand (no TOML dependency). Only
// what Omaplan needs:
//
//   language = "da"            # UI language: da | en (default: the system's)
//
//   [engine]
//   default = "vibe"           # default engine: vibe | claude | local | command
//   command = "..."            # for default = "command"
//   compare = "vibe, claude"   # the two engines of Compare
//   timeout = 90               # seconds an engine may take
//
//   [engine.vibe]
//   model = "..."              # passed as vibe --model; empty = vibe's own
//
//   [engine.claude]
//   model = "..."              # passed as claude --model; empty = claude's own
//
//   [engine.local]
//   api = "ollama"             # ollama | openai (OpenAI-compatible endpoint)
//   endpoint = "http://127.0.0.1:11434"
//   model = "..."              # the user's choice -- nothing is hardcoded
//
//   [dates]                    # empty or "auto": the language's convention
//   order = "dmy"              # dmy | mdy: how 12/11 is read
//   dotted = "time"            # time | date: whether 12.12 is a time or a date
//   week_start = "monday"      # monday | sunday
//   clock = "24h"              # 24h | 12h
//
//   [projects]                 # a project's colour: a slot of the theme palette
//   Blog = "blue"              # (colors.toml's names; color4 etc. as aliases)
//   "Client A" = "green"
//
//   [snapshot]
//   path = "~/Sync/omaplan/plan.json"  # plan.json for the read-only edition (default: next to the database)
//
// The names of 0.1 are read as aliases: [motor] for [engine], its
// key `engine` for `default`, [motor.lokal] for [engine.local] and the engine
// id "lokal" for "local".
struct AppConfig {
    static constexpr int defaultTimeoutSeconds = 90;
    static constexpr int minTimeoutSeconds = 10;
    static constexpr int maxTimeoutSeconds = 600;

    QString language;                         // top-level language; empty = not set
    // [engine] default. Unset, it follows `omarchy default agent` when that is
    // claude or local, and is vibe otherwise.
    QString engine = QStringLiteral("vibe");
    QString command;                          // [engine] command
    QStringList compare = {QStringLiteral("vibe"), QStringLiteral("claude")}; // [engine] compare
    int timeoutSeconds = defaultTimeoutSeconds; // [engine] timeout
    QString vibeModel;                        // [engine.vibe] model
    QString claudeModel;                      // [engine.claude] model
    // [engine.local] api: ollama, openai or omarchy (the local model Omarchy
    // runs). Unset, it is omarchy when Omarchy's local models are set up and
    // no endpoint is given, and ollama otherwise.
    QString localApi = QStringLiteral("ollama");
    QString localEndpoint;
    QString localModel;
    // [dates]; empty = the interface language's convention.
    QString dateOrder, dateDotted, weekStart, clock;
    QMap<QString, QString> projectColors;     // [projects] name -> palette slot ("blue")
    QString snapshotPath;                     // [snapshot] path, ~ expanded; empty = default

    // What an unset engine.default and engine.local.api stand for.
    static QString defaultEngine();
    static QString defaultLocalApi(const QString &endpoint);

    // Empty path = default location; OMAPLAN_CONFIG overrides it (tests).
    static QString defaultPath();
    static AppConfig load(const QString &path = {});
};

// One setting as the CLI, MCP and the settings panel name it
// ("engine.claude.model") and where it lives in the file.
struct ConfigKey {
    QString name;          // dotted name; "projects.<name>" for a project colour
    QString section;       // canonical section ("" = top level)
    QString key;           // key inside the section
    QStringList choices;   // allowed values; empty = free text
    QString defaultValue;  // shown when the file does not set it
    QString description;   // English, for --help, MCP and `config list`
};

namespace ConfigKeys {
// Every fixed key, in the order the panel and `config list` show them.
const QList<ConfigKey> &all();
// The key for a dotted name, including "projects.<name>"; false when unknown.
bool find(const QString &name, ConfigKey *out);
// The palette slots a project colour may name, as Omarchy's colors.toml
// names them: red, orange, ..., bright_magenta (not black or white).
const QStringList &paletteSlots();
// The slot a value names: a slot name, or a terminal colour number as an
// alias (color4 = blue, color9 = bright_red); empty when it names none.
QString paletteSlot(const QString &value);
bool isPaletteSlot(const QString &value);
// Checks a value for a key; returns an English error, or empty when fine.
// An empty value passes (it means "unset"); a control character never does,
// in the value or in a project name.
QString validate(const ConfigKey &key, const QString &value);
// C0 and C1 controls, DEL, U+2028 and U+2029: anything a reader may break a line at.
bool hasControlCharacter(const QString &text);
// Whether an agent (MCP) may set the key: language, dates.*, projects.*, the
// model names, timeout, default engine and compare. Not what runs
// (engine.command), where text goes (engine.local.endpoint/api) or where a
// file is written (snapshot.path).
bool agentMaySet(const ConfigKey &key);
} // namespace ConfigKeys

// The file as lines, for writing: set() and unset() change one line and
// leave every comment, blank line, unknown key and the old 0.1 section names
// alone. save() writes atomically.
class ConfigFile {
public:
    explicit ConfigFile(const QString &path = {});

    QString path() const { return m_path; }
    // The raw value of section/key (aliases included), and whether it is set.
    QString value(const QString &section, const QString &key, bool *found = nullptr) const;
    // Replaces the value on its line, keeping a comment after it; otherwise
    // adds the line at the end of the section (a new section at the end).
    void set(const QString &section, const QString &key, const QString &value);
    void unset(const QString &section, const QString &key);
    // Every key set in a section, in file order (for [projects]).
    QStringList keys(const QString &section) const;
    QString text() const { return m_lines.join(QLatin1Char('\n')); }
    bool save(QString *error = nullptr) const;

private:
    // line: the last occurrence, the one AppConfig::load uses; lines: all of them.
    struct Location { int line = -1; QList<int> lines; int sectionEnd = -1; };
    Location locate(const QString &section, const QString &key) const;

    QString m_path;
    QStringList m_lines;
};
