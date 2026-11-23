#pragma once

#include <QString>
#include <QStringList>

// ~/.config/omaplan/config.toml, parsed by hand (no TOML dependency). Only
// what Omaplan needs:
//
//   [motor]
//   engine = "vibe"            # standardmotor: vibe | claude | lokal | command
//   command = "..."            # for engine = "command"
//   compare = "vibe, claude"   # the two engines of Sammenlign
//
//   [motor.lokal]
//   api = "ollama"             # ollama | openai (OpenAI-kompatibelt endpoint)
//   endpoint = "http://127.0.0.1:11434"
//   model = "..."              # brugerens valg -- intet er hårdkodet
struct AppConfig {
    QString engine = QStringLiteral("vibe"); // standardmotor
    QString command;                          // [motor] command
    QStringList compare = {QStringLiteral("vibe"), QStringLiteral("claude")}; // [motor] compare
    QString localApi = QStringLiteral("ollama");
    QString localEndpoint;
    QString localModel;

    // Empty path = default location; OMAPLAN_CONFIG overrides it (tests).
    static QString defaultPath();
    static AppConfig load(const QString &path = {});
};
