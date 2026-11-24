#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// The words of one language that the date parser (Actions::parseDateTime) and
// the date annotation (annotateDates) understand. User input may be in any
// language, so every language here is understood at once; adding one is a
// matter of adding a DateWords entry in datewords.cpp and a test.
struct DateWords {
    QString code; // ISO 639-1, e.g. "da"

    // Whole phrases, lower case.
    QStringList today;            // "i dag", "today"
    QStringList tomorrow;
    QStringList dayAfterTomorrow;
    QStringList nextWeek;         // = Monday of next week
    QStringList weekend;          // = the coming Saturday

    // "næste fredag", "next friday": the prefix before a weekday.
    QStringList nextPrefixes;
    // "om 3 dage", "in 3 days": a regex over the whole phrase with the
    // number as capture group 1.
    QString inDaysPattern;
    // "kl. 14:30", "at 14:30": words before a time, as regex alternatives
    // (already escaped).
    QStringList timePrefixes;
    // "den 12.": a word before a day of the month that makes a following
    // weekday unambiguous ("fredag den 12.").
    QStringList dayArticles;
    // "3 timer", "2 hours": words after a number that make it a duration,
    // never a clock time.
    QStringList durationUnits;

    QHash<QString, int> weekdays; // name -> 1 (Monday) .. 7
    QHash<QString, int> months;   // name -> 1 .. 12

    // Spoken clock times: number words and the phrases before them with the
    // minute offset from the named hour. "halv tre" (Danish) is 14:30, half
    // an hour BEFORE three; "half past three" (English) is 15:30.
    QHash<QString, int> numberWords;
    QHash<QString, int> clockPhrases; // phrase -> minutes relative to the hour
};

// Every language the parser understands, Danish first.
const QList<DateWords> &dateLanguages();

// Lookups over all languages at once.
namespace DateVocabulary {
bool isToday(const QString &phrase);
bool isTomorrow(const QString &phrase);
bool isDayAfterTomorrow(const QString &phrase);
bool isNextWeek(const QString &phrase);
bool isWeekend(const QString &phrase);
int weekday(const QString &word);     // 0 when not a weekday name
int month(const QString &word);       // 0 when not a month name
int numberWord(const QString &word);  // 0 when not a number word
bool isDayArticle(const QString &word);
bool isDurationUnit(const QString &word);
// Strips a "next" prefix; true when there was one.
bool stripNextPrefix(QString *phrase);
// Days from a whole "in N days" phrase, or -1.
int inDays(const QString &phrase);
// Regex alternatives of every time prefix, for building a time pattern.
QString timePrefixPattern();
// Every "today" phrase, for removing it from a longer phrase.
QStringList todayPhrases();
// Regex alternatives of every clock phrase, longest first.
QString clockPhrasePattern();
int clockPhraseOffset(const QString &phrase);
} // namespace DateVocabulary
