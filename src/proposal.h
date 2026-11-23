#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// A single task proposed by an AI engine. This is a pure value type built
// only from docs/ai-schema.json -- it deliberately does not know the core's
// Task type, so the AI layer can merge into the core without dependencies.
struct ProposedTask {
    QString title;                 // "title" (required by the schema)
    QString notes;                 // "notes"
    QString project;               // "project"
    int priority = 0;              // "priority", 1-3; 0 means not set
    QDateTime start;               // "start"; invalid when not set
    bool startHasTime = false;     // true when "start" had a time part
    int durationMin = 0;           // "duration_min"; 0 means not set
    QDateTime deadline;            // "deadline"; invalid when not set
    bool deadlineHasTime = false;  // true when "deadline" had a time part
    QStringList guessed;           // "guessed": schema field names the engine guessed

    bool operator==(const ProposedTask &other) const;
    QJsonObject toJson() const;
};

// The parsed answer from an engine: the tasks plus the text fragments the
// engine could not turn into tasks ("unresolved").
struct Proposal {
    QList<ProposedTask> tasks;
    QStringList unresolved;

    bool operator==(const Proposal &other) const;
    QJsonObject toJson() const;
};

struct ParseResult {
    bool ok = false;
    QString error;      // human-readable reason when ok == false
    Proposal proposal;  // valid when ok == true
};

// Parses and validates a raw engine response against docs/ai-schema.json
// (embedded as the Qt resource ":/ai-schema.json"). Tolerates JSON wrapped
// in prose or ```-fenced blocks, engine envelopes such as claude -p's
// {"result": "..."} or Ollama's {"response": "..."}, and vibe --output json's
// event array (only the last assistant message's text parts count; reasoning
// is never used). When the response is invalid, ParseResult::error contains
// the raw response so the UI can show it instead of failing silently.
//
// Until 0.1 the schema had Danish keys ("opgaver", "titel", "gaet", ...). A
// model that still answers with them is accepted: the keys are mapped to the
// English ones first (see withEnglishKeys), and output is always English.
ParseResult parseProposal(const QString &rawResponse);

// The proposal object with the old Danish keys and guessed-field names
// renamed to the English schema names; an English key wins over its Danish
// twin. Anything else is left alone for the schema check to judge.
QJsonObject withEnglishKeys(const QJsonObject &proposal);

// The schema as a JSON object/text, read from the embedded resource. Used by
// the prompt builder and by the Ollama engine (its "format" parameter).
QJsonObject schemaObject();
QString schemaText();
