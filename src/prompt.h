#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

// Everything the prompt builder needs. Kept independent of the core's types:
// existing tasks are passed as plain strings (text or JSON lines), so the AI
// layer never includes core headers.
struct PromptContext {
    QDate today = QDate::currentDate();
    QString language = QStringLiteral("da"); // Omarchy UI language
    QStringList existingTasks;                // one line per task, for context
};

// The system prompt: today's date and weekday (so "på tirsdag" can be
// resolved), the timezone, the UI language, the response schema, the rules,
// and any existing tasks as context. Written in English; it instructs the
// engine to understand Danish date expressions.
QString buildSystemPrompt(const PromptContext &context);

// The user prompt: the raw, unedited input text.
QString buildUserPrompt(const QString &inputText);

// System and user prompt joined, for engines that take a single prompt
// (claude -p argument, command engines' stdin).
QString buildFullPrompt(const PromptContext &context, const QString &inputText);
