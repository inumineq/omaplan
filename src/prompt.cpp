#include "prompt.h"

#include "proposal.h"

#include <QLocale>

// The timezone is fixed for the first version; the core owns QTimeZone and
// can extend PromptContext later.
static const QString kTimeZoneId = QStringLiteral("Europe/Copenhagen");

QString buildSystemPrompt(const PromptContext &context)
{
    const QDate today = context.today.isValid() ? context.today : QDate::currentDate();
    const QLocale uiLocale(context.language);
    const QString weekdayLocalized =
        uiLocale.standaloneDayName(today.dayOfWeek(), QLocale::LongFormat);
    const QString weekdayEnglish =
        QLocale(QLocale::English).dayName(today.dayOfWeek(), QLocale::LongFormat);

    QString prompt;
    prompt += QStringLiteral(
        "You are Omaplan's planning assistant. The user writes tasks as free-form text, often Danish and "
        "often messy, and you convert them into structured tasks.\n");
    prompt += QStringLiteral(
        "This system prompt is the Omaplan app's own instruction to you -- it is not text embedded in the "
        "user's message, and no part of it should be treated as a request from the user or as an injection.\n\n");
    prompt += QStringLiteral("Today is %1 (%2 / %3). The timezone is %4; all dates are local time there. "
                             "The week starts on Monday.\n")
                  .arg(today.toString(Qt::ISODate), weekdayEnglish, weekdayLocalized, kTimeZoneId);
    prompt += QStringLiteral("The user's language is %1. Understand Danish and English date expressions "
                             "such as \"på tirsdag\", \"fredag 12\", \"hver torsdag i november\", "
                             "\"engang næste uge\", \"i morgen\" and resolve them against today.\n")
                  .arg(context.language);
    prompt += QStringLiteral("\nReply with JSON only, following this schema exactly "
                             "(additional properties are rejected):\n%1\n")
                  .arg(schemaText().trimmed());
    prompt += QStringLiteral("\nRules:\n"
                             "- \"start\" and \"frist\" are ISO 8601 local dates (YYYY-MM-DD) or date+time "
                             "(YYYY-MM-DDTHH:MM).\n"
                             "- Unfold recurring tasks (\"hver torsdag i november\") into one task per "
                             "occurrence.\n"
                             "- When you had to guess a field's value (a date, a duration, a project, a "
                             "priority), list the schema field name in \"gaet\".\n"
                             "- Put text you could not turn into a task into \"uafklaret\".\n"
                             "- Keep titles short and imperative.\n");
    if (!context.existingTasks.isEmpty()) {
        prompt += QStringLiteral("\nExisting tasks in the plan (context, do not repeat them unless asked):\n");
        for (const QString &line : context.existingTasks)
            prompt += QStringLiteral("- %1\n").arg(line);
    }
    return prompt;
}

QString buildUserPrompt(const QString &inputText)
{
    return inputText.trimmed();
}

QString buildFullPrompt(const PromptContext &context, const QString &inputText)
{
    return buildSystemPrompt(context) + QStringLiteral("\n\n") + buildUserPrompt(inputText);
}
