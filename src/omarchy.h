#pragma once

#include <QByteArray>
#include <QString>

// What Omaplan reads from Omarchy about AI: the default coding agent and the
// local model Omarchy runs (the omarchy-local-ai package). Read-only, and
// read again each time it is asked, so a change counts at once. Without
// Omarchy every answer is empty.
namespace Omarchy {

// The home these files are read under. OMAPLAN_OMARCHY_HOME overrides it
// (tests), like OMAPLAN_CONFIG does for the config file.
QString home();

// `omarchy default agent`: the first line of ~/.config/omarchy/defaults/agent
// ("claude", "local", "codex", ...), or empty when none is picked.
QString defaultAgent();

// The Omaplan engine that default agent stands for: "claude" for claude,
// "local" for Omarchy's local model when local models are set up, and empty
// for an agent Omaplan has no engine for (or none picked).
QString defaultEngine();

// Whether Omarchy's local models are set up for this user: their private API
// key exists (~/.config/omarchy/agent-local/key).
bool hasLocalModels();

// A local text model as the engine's status.json describes it
// (~/.local/state/omarchy/agent-local/status.json).
struct LocalModel {
    QString recipe;    // the model id the server answers to
    QString endpoint;  // OpenAI-compatible, including /v1; loopback only
    QString problem;   // translated; empty when the model can be asked
};

// The model to ask: `wanted` when given (a recipe), else the one pinned with
// `omarchy default agent local <recipe>`, else the one started last, else
// Omarchy's best fit. `problem` says what to do when there is none, it is
// not running, or it is not on this machine.
LocalModel localModel(const QString &wanted = {});

// The API key, read from its file for one request and never stored by
// Omaplan. Empty when it cannot be read.
QByteArray localKey();

} // namespace Omarchy
