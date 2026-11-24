#pragma once

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct AppConfig;

// How dates and times are written in a language: the order of day and month
// in "12/11", whether "12.12" is a time or a date, the first day of the week
// and the clock. Each DateWords carries its language's convention; [dates]
// in config.toml overrides it key by key.
struct DateConvention {
    bool dayFirst = true;      // 12/11 = 12 November (false: December 11)
    bool dottedIsTime = true;  // 12.12 = 12:12 (false: a date, in the order above)
    int weekStart = 1;         // 1 = Monday, 7 = Sunday
    bool clock24 = true;       // 14:30 (false: 2:30 PM)

    bool operator==(const DateConvention &o) const {
        return dayFirst == o.dayFirst && dottedIsTime == o.dottedIsTime && weekStart == o.weekStart
               && clock24 == o.clock24;
    }
    bool operator!=(const DateConvention &o) const { return !(*this == o); }

    // The convention of a language code ("da", "en"); Danish for an unknown one.
    static DateConvention forLanguage(const QString &language);
    // The language's convention with config.toml's [dates] on top.
    static DateConvention resolve(const AppConfig &config, const QString &language);
    // The one the parser and annotation use in this process: set at start
    // (main.cpp) and again when config.toml changes while the window is open.
    static const DateConvention &active();
    static void setActive(const DateConvention &convention);

    // Whether "a.b" (two numbers with a dot) is a time: always when dotted
    // values are times; otherwise only when a time prefix ("kl.", "at") comes
    // before it, am/pm after it, or it cannot be a date in this order
    // ("14.30": there is no month 30).
    bool dottedReadsAsTime(int a, int b, bool prefixed, bool meridiem) const {
        if (dottedIsTime || prefixed || meridiem)
            return true;
        const int day = dayFirst ? a : b, month = dayFirst ? b : a;
        return day < 1 || day > 31 || month < 1 || month > 12;
    }
    // The first day of the week `date` lies in.
    QDate weekStartOf(const QDate &date) const;
    // The config values ("dmy", "time", "monday", "24h") of each setting.
    QString orderName() const { return dayFirst ? QStringLiteral("dmy") : QStringLiteral("mdy"); }
    QString dottedName() const { return dottedIsTime ? QStringLiteral("time") : QStringLiteral("date"); }
    QString weekStartName() const { return weekStart == 7 ? QStringLiteral("sunday") : QStringLiteral("monday"); }
    QString clockName() const { return clock24 ? QStringLiteral("24h") : QStringLiteral("12h"); }
};

// The words of one language that the date parser (Actions::parseDateTime) and
// the date annotation (annotateDates) understand. User input may be in any
// language, so every language here is understood at once; adding one is a
// matter of adding a DateWords entry in datewords.cpp and a test.
struct DateWords {
    QString code; // ISO 639-1, e.g. "da"
    DateConvention convention; // how the language writes dates and times

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
