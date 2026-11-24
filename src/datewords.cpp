#include "datewords.h"
#include "config.h"

#include <QRegularExpression>
#include <algorithm>

static DateWords danish() {
    DateWords w;
    w.code = QStringLiteral("da");
    // 12/11 is 12 November, 12.12 is a time (kl. 12.12), Monday, 24 hours.
    w.convention = {true, true, 1, true};
    w.today = {QStringLiteral("i dag")};
    w.tomorrow = {QStringLiteral("i morgen")};
    w.dayAfterTomorrow = {QStringLiteral("i overmorgen")};
    w.nextWeek = {QStringLiteral("næste uge")};
    w.weekend = {QStringLiteral("i weekenden")};
    w.nextPrefixes = {QStringLiteral("næste")};
    w.inDaysPattern = QStringLiteral("^om (\\d{1,3}) dag(?:e)?$");
    w.timePrefixes = {QStringLiteral("kl\\.?")};
    w.dayArticles = {QStringLiteral("den")};
    w.durationUnits = {QStringLiteral("min"),    QStringLiteral("min."),    QStringLiteral("minut"),
                       QStringLiteral("minutter"), QStringLiteral("t"),     QStringLiteral("t."),
                       QStringLiteral("time"),   QStringLiteral("timer"),   QStringLiteral("timers"),
                       QStringLiteral("dag"),    QStringLiteral("dage"),    QStringLiteral("dages"),
                       QStringLiteral("uge"),    QStringLiteral("uger"),    QStringLiteral("ugers")};
    w.weekdays = {{QStringLiteral("mandag"), 1}, {QStringLiteral("tirsdag"), 2}, {QStringLiteral("onsdag"), 3},
                  {QStringLiteral("torsdag"), 4}, {QStringLiteral("fredag"), 5}, {QStringLiteral("lørdag"), 6},
                  {QStringLiteral("søndag"), 7}};
    w.months = {{QStringLiteral("januar"), 1}, {QStringLiteral("februar"), 2}, {QStringLiteral("marts"), 3},
                {QStringLiteral("april"), 4},  {QStringLiteral("maj"), 5},      {QStringLiteral("juni"), 6},
                {QStringLiteral("juli"), 7},   {QStringLiteral("august"), 8},   {QStringLiteral("september"), 9},
                {QStringLiteral("oktober"), 10}, {QStringLiteral("november"), 11}, {QStringLiteral("december"), 12}};
    w.numberWords = {{QStringLiteral("en"), 1},    {QStringLiteral("et"), 1},     {QStringLiteral("to"), 2},
                     {QStringLiteral("tre"), 3},   {QStringLiteral("fire"), 4},   {QStringLiteral("fem"), 5},
                     {QStringLiteral("seks"), 6},  {QStringLiteral("syv"), 7},    {QStringLiteral("otte"), 8},
                     {QStringLiteral("ni"), 9},    {QStringLiteral("ti"), 10},    {QStringLiteral("elleve"), 11},
                     {QStringLiteral("tolv"), 12}};
    w.clockPhrases = {{QStringLiteral("halv"), -30}, {QStringLiteral("kvart i"), -15},
                      {QStringLiteral("kvart over"), 15}};
    return w;
}

static DateWords english() {
    DateWords w;
    w.code = QStringLiteral("en");
    // British, as the interface's English dates (en_GB): 12/11 is 12
    // November, 12.12 a time, Monday, 24 hours. US habits are one setting away.
    w.convention = {true, true, 1, true};
    w.today = {QStringLiteral("today")};
    w.tomorrow = {QStringLiteral("tomorrow")};
    w.dayAfterTomorrow = {QStringLiteral("day after tomorrow"), QStringLiteral("the day after tomorrow")};
    w.nextWeek = {QStringLiteral("next week")};
    w.weekend = {QStringLiteral("weekend"), QStringLiteral("this weekend"), QStringLiteral("at the weekend"),
                 QStringLiteral("on the weekend")};
    w.nextPrefixes = {QStringLiteral("next")};
    w.inDaysPattern = QStringLiteral("^in (\\d{1,3}) days?$");
    w.timePrefixes = {QStringLiteral("at")};
    w.dayArticles = {QStringLiteral("the")};
    w.durationUnits = {QStringLiteral("m"),      QStringLiteral("min"),     QStringLiteral("mins"),
                       QStringLiteral("minute"), QStringLiteral("minutes"), QStringLiteral("h"),
                       QStringLiteral("hr"),     QStringLiteral("hrs"),     QStringLiteral("hour"),
                       QStringLiteral("hours"),  QStringLiteral("day"),     QStringLiteral("days"),
                       QStringLiteral("week"),   QStringLiteral("weeks")};
    w.weekdays = {{QStringLiteral("monday"), 1}, {QStringLiteral("tuesday"), 2}, {QStringLiteral("wednesday"), 3},
                  {QStringLiteral("thursday"), 4}, {QStringLiteral("friday"), 5}, {QStringLiteral("saturday"), 6},
                  {QStringLiteral("sunday"), 7}};
    w.months = {{QStringLiteral("january"), 1}, {QStringLiteral("february"), 2}, {QStringLiteral("march"), 3},
                {QStringLiteral("april"), 4},   {QStringLiteral("may"), 5},      {QStringLiteral("june"), 6},
                {QStringLiteral("july"), 7},    {QStringLiteral("august"), 8},   {QStringLiteral("september"), 9},
                {QStringLiteral("october"), 10}, {QStringLiteral("november"), 11}, {QStringLiteral("december"), 12}};
    w.numberWords = {{QStringLiteral("one"), 1},   {QStringLiteral("two"), 2},    {QStringLiteral("three"), 3},
                     {QStringLiteral("four"), 4},  {QStringLiteral("five"), 5},   {QStringLiteral("six"), 6},
                     {QStringLiteral("seven"), 7}, {QStringLiteral("eight"), 8},  {QStringLiteral("nine"), 9},
                     {QStringLiteral("ten"), 10},  {QStringLiteral("eleven"), 11}, {QStringLiteral("twelve"), 12}};
    w.clockPhrases = {{QStringLiteral("quarter to"), -15}, {QStringLiteral("quarter past"), 15},
                      {QStringLiteral("half past"), 30}};
    return w;
}

const QList<DateWords> &dateLanguages() {
    static const QList<DateWords> languages = {danish(), english()};
    return languages;
}

DateConvention DateConvention::forLanguage(const QString &language) {
    const QString code = language.left(2).toLower();
    for (const DateWords &w : dateLanguages())
        if (w.code == code)
            return w.convention;
    return dateLanguages().first().convention;
}

DateConvention DateConvention::resolve(const AppConfig &config, const QString &language) {
    DateConvention c = forLanguage(language);
    if (!config.dateOrder.isEmpty())
        c.dayFirst = config.dateOrder != QLatin1String("mdy");
    if (!config.dateDotted.isEmpty())
        c.dottedIsTime = config.dateDotted != QLatin1String("date");
    if (!config.weekStart.isEmpty())
        c.weekStart = config.weekStart == QLatin1String("sunday") ? 7 : 1;
    if (!config.clock.isEmpty())
        c.clock24 = config.clock != QLatin1String("12h");
    return c;
}

static DateConvention &activeConvention() {
    static DateConvention convention;
    return convention;
}

const DateConvention &DateConvention::active() { return activeConvention(); }
void DateConvention::setActive(const DateConvention &convention) { activeConvention() = convention; }

QDate DateConvention::weekStartOf(const QDate &date) const {
    return date.addDays(-((date.dayOfWeek() - weekStart + 7) % 7));
}

namespace DateVocabulary {

template <typename Pick>
static bool anyContains(const QString &phrase, Pick pick) {
    for (const DateWords &w : dateLanguages())
        if (pick(w).contains(phrase))
            return true;
    return false;
}

template <typename Pick>
static int anyValue(const QString &word, Pick pick) {
    for (const DateWords &w : dateLanguages())
        if (const int value = pick(w).value(word, 0))
            return value;
    return 0;
}

bool isToday(const QString &phrase) { return anyContains(phrase, [](const DateWords &w) { return w.today; }); }
bool isTomorrow(const QString &phrase) { return anyContains(phrase, [](const DateWords &w) { return w.tomorrow; }); }
bool isDayAfterTomorrow(const QString &phrase) {
    return anyContains(phrase, [](const DateWords &w) { return w.dayAfterTomorrow; });
}
bool isNextWeek(const QString &phrase) { return anyContains(phrase, [](const DateWords &w) { return w.nextWeek; }); }
bool isWeekend(const QString &phrase) { return anyContains(phrase, [](const DateWords &w) { return w.weekend; }); }
bool isDayArticle(const QString &word) { return anyContains(word, [](const DateWords &w) { return w.dayArticles; }); }
bool isDurationUnit(const QString &word) {
    static const QRegularExpression trailingPunctuation(QStringLiteral("[,;:!?)]+$"));
    const QString bare = word.toLower().remove(trailingPunctuation);
    return anyContains(bare, [](const DateWords &w) { return w.durationUnits; });
}

int weekday(const QString &word) { return anyValue(word, [](const DateWords &w) { return w.weekdays; }); }
int month(const QString &word) { return anyValue(word, [](const DateWords &w) { return w.months; }); }
int numberWord(const QString &word) { return anyValue(word, [](const DateWords &w) { return w.numberWords; }); }

bool stripNextPrefix(QString *phrase) {
    for (const DateWords &w : dateLanguages())
        for (const QString &prefix : w.nextPrefixes)
            if (phrase->startsWith(prefix + QLatin1Char(' '))) {
                *phrase = phrase->mid(prefix.size() + 1).trimmed();
                return true;
            }
    return false;
}

int inDays(const QString &phrase) {
    for (const DateWords &w : dateLanguages())
        if (const auto m = QRegularExpression(w.inDaysPattern).match(phrase); m.hasMatch())
            return m.captured(1).toInt();
    return -1;
}

QString timePrefixPattern() {
    QStringList parts;
    for (const DateWords &w : dateLanguages())
        parts << w.timePrefixes;
    return parts.join(QLatin1Char('|'));
}

QStringList todayPhrases() {
    QStringList phrases;
    for (const DateWords &w : dateLanguages())
        phrases << w.today;
    return phrases;
}

QString clockPhrasePattern() {
    QStringList phrases;
    for (const DateWords &w : dateLanguages())
        phrases << w.clockPhrases.keys();
    // Longest first, so "kvart over" is tried before "kvart".
    std::sort(phrases.begin(), phrases.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (QString &p : phrases)
        p = QRegularExpression::escape(p);
    return phrases.join(QLatin1Char('|'));
}

int clockPhraseOffset(const QString &phrase) {
    for (const DateWords &w : dateLanguages())
        if (w.clockPhrases.contains(phrase))
            return w.clockPhrases.value(phrase);
    return 0;
}

} // namespace DateVocabulary
