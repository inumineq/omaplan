#pragma once

#include <QString>

// ~/.config/omaplan/config.toml, parsed by hand (no TOML dependency). Only
// what Omaplan needs:
//
//   [motor]
//   engine = "vibe"            # standardmotor: vibe | claude | lokal | command
//   command = "..."            # for engine = "command"
//
//   [motor.lokal]
//   api = "ollama"             # ollama | openai (OpenAI-kompatibelt endpoint)
//   endpoint = "http://127.0.0.1:11434"
//   model = "..."              # brugerens valg -- intet er hårdkodet
struct AppConfig {
    QString engine = QStringLiteral("vibe"); // standardmotor
    QString command;                          // [motor] command
    QString localApi = QStringLiteral("ollama");
    QString localEndpoint;
    QString localModel;

    // Empty path = default location; OMAPLAN_CONFIG overrides it (tests).
    static QString defaultPath();
    static AppConfig load(const QString &path = {});
};
