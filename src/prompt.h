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
// resolved), the timezone, the UI language, the response schema and the rules.
// It depends only on the date, so it is identical all day (prompt caching);
// existing tasks go into the user prompt instead. Written in English; it
// instructs the engine to understand Danish date expressions.
QString buildSystemPrompt(const PromptContext &context);

// The user prompt: the raw, unedited input text, plus the existing tasks as
// context. Existing tasks live here (not in the system prompt) so the system
// prompt depends only on the date and stays identical all day -- that is what
// makes prompt caching hit.
QString buildUserPrompt(const QString &inputText, const QStringList &existingTasks = {});

// System and user prompt joined, for engines that take a single prompt
// (claude -p argument, command engines' stdin).
QString buildFullPrompt(const PromptContext &context, const QString &inputText);
