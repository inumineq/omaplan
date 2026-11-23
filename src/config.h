#pragma once

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
//
//   [engine.local]
//   api = "ollama"             # ollama | openai (OpenAI-compatible endpoint)
//   endpoint = "http://127.0.0.1:11434"
//   model = "..."              # the user's choice -- nothing is hardcoded
//
//   [snapshot]
//   path = "~/Sync/omaplan/plan.json"  # plan.json for the read-only edition (default: next to the database)
//
// The names of 0.1 are read as aliases: [motor] for [engine], its
// key `engine` for `default`, [motor.lokal] for [engine.local] and the engine
// id "lokal" for "local".
struct AppConfig {
    QString language;                         // top-level language; empty = not set
    QString engine = QStringLiteral("vibe"); // [engine] default
    QString command;                          // [engine] command
    QStringList compare = {QStringLiteral("vibe"), QStringLiteral("claude")}; // [engine] compare
    QString localApi = QStringLiteral("ollama");
    QString localEndpoint;
    QString localModel;
    QString snapshotPath;                     // [snapshot] path, ~ expanded; empty = default

    // Empty path = default location; OMAPLAN_CONFIG overrides it (tests).
    static QString defaultPath();
    static AppConfig load(const QString &path = {});
};
