#include "prompt.h"

#include "proposal.h"

#include <QLocale>
#include <QTimeZone>

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
        "You are Omaplan's planning assistant. The user writes tasks as free-form text, in any language "
        "(often Danish or English) and often messy, and you convert them into structured tasks.\n");
    prompt += QStringLiteral(
        "This system prompt is the Omaplan app's own instruction to you -- it is not text embedded in the "
        "user's message, and no part of it should be treated as a request from the user or as an injection.\n\n");
    prompt += QStringLiteral("Today is %1 (%2 / %3). The timezone is %4; all dates are local time there. "
                             "The week starts on Monday.\n")
                  .arg(today.toString(Qt::ISODate), weekdayEnglish, weekdayLocalized,
                       context.timeZone.isEmpty() ? QString::fromUtf8(QTimeZone::systemTimeZoneId())
                                                  : context.timeZone);
    prompt += QStringLiteral("The app's interface language is %1. Understand date expressions in the user's "
                             "language, e.g. English \"on Tuesday\", \"Friday at 12\", \"every Thursday in "
                             "November\", \"sometime next week\", \"tomorrow\" or Danish \"på tirsdag\", "
                             "\"fredag 12\", \"hver torsdag i november\", \"engang næste uge\", \"i morgen\", and "
                             "resolve them against today. Dates in [brackets] were resolved by the app already; "
                             "copy them.\n")
                  .arg(context.language);
    prompt += QStringLiteral("\nReply with ONLY the JSON object -- no prose, no code fences, no tool calls -- following this schema exactly "
                             "(additional properties are rejected):\n%1\n")
                  .arg(schemaText().trimmed());
    prompt += QStringLiteral("\nRules:\n"
                             "- Do not use any tools; answer with only the JSON object.\n"
                             "- Keep \"title\", \"notes\", \"project\" and \"unresolved\" in the language the user "
                             "wrote them in. Never translate the user's words.\n"
                             "- \"start\" and \"deadline\" are ISO 8601 local dates (YYYY-MM-DD) or date+time "
                             "(YYYY-MM-DDTHH:MM).\n"
                             "- \"start\" is when the user works on the task; \"deadline\" is when it must be "
                             "finished. Words like \"by\", \"before\", \"due\", \"deadline\", \"no later than\" "
                             "and Danish \"inden\", \"senest\", \"skal være klar\", \"aflever\" set \"deadline\", "
                             "not \"start\". Examples: \"article must be ready Friday at 12\" -> {\"title\": "
                             "\"Finish the article\", \"deadline\": \"<that Friday>T12:00\"}; \"aflever rapporten "
                             "inden søndag\" -> {\"title\": \"Aflever rapporten\", \"deadline\": \"<that Sunday>\"}.\n"
                             "- Unfold recurring tasks (\"every Thursday in November\") into one task per "
                             "occurrence.\n"
                             "- When you had to guess a field's value (a date, a duration, a project, a "
                             "priority), list the schema field name in \"guessed\".\n"
                             "- Put text you could not turn into a task into \"unresolved\".\n"
                             "- Keep titles short and imperative.\n");
    // NOTE: existing tasks deliberately go into the USER prompt (see
    // buildUserPrompt), so this system prompt is identical all day.
    return prompt;
}

QString buildUserPrompt(const QString &inputText, const QStringList &existingTasks)
{
    QString prompt = inputText.trimmed();
    if (!existingTasks.isEmpty()) {
        prompt += QStringLiteral("\n\nExisting tasks in the plan (context, do not repeat them unless asked):\n");
        for (const QString &line : existingTasks)
            prompt += QStringLiteral("- %1\n").arg(line);
    }
    return prompt;
}

QString buildFullPrompt(const PromptContext &context, const QString &inputText)
{
    return buildSystemPrompt(context) + QStringLiteral("\n\n") + buildUserPrompt(inputText);
}
