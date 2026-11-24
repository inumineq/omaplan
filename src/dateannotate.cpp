#include "dateannotate.h"

#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include "actions.h"
#include "datewords.h"

namespace {

// Spoken clock times: "halv tre" = 14:30, "kvart i tre" = 14:45, "kvart over
// tre" = 15:15, "quarter past four" = 16:15, "half past three" = 15:30. The
// named hour is read as afternoon unless it is 12, the usual meaning when
// planning a working day.
bool parseNumberWordTime(const QString &phrase, QTime *time) {
    static const QRegularExpression re(
        QStringLiteral("^(%1)\\s+(\\w+)$").arg(DateVocabulary::clockPhrasePattern()));
    const auto m = re.match(phrase.toLower());
    if (!m.hasMatch())
        return false;
    const int hour = DateVocabulary::numberWord(m.captured(2));
    if (hour == 0)
        return false;
    const int h = (hour == 12 ? 12 : hour + 12) % 24;
    const int minutes = h * 60 + DateVocabulary::clockPhraseOffset(m.captured(1));
    *time = QTime(0, 0).addSecs(((minutes + 24 * 60) % (24 * 60)) * 60);
    return time->isValid();
}

bool isBareNumber(const QString &word) {
    static const QRegularExpression re(QStringLiteral("^\\d{1,2}$"));
    return re.match(word).hasMatch();
}

bool isTimeOnlyPhrase(const QString &phrase) {
    static const QRegularExpression re(
        QStringLiteral("^(?:%1)?\\s*\\d{1,2}(?:[:.]\\d{2})?$").arg(DateVocabulary::timePrefixPattern()),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(phrase.trimmed()).hasMatch();
}

QString annotationFor(const QString &phrase, const QDate &today) {
    // Spoken clock times first -- parseDateTime does not know those.
    QTime wordTime;
    if (parseNumberWordTime(phrase, &wordTime))
        return wordTime.toString(QStringLiteral("HH:mm"));

    if (isTimeOnlyPhrase(phrase)) {
        QDateTime when;
        bool hasTime = false;
        if (Actions::parseDateTime(phrase, today, &when, &hasTime) && hasTime)
            return when.time().toString(QStringLiteral("HH:mm"));
        return {};
    }

    QDateTime when;
    bool hasTime = false;
    if (!Actions::parseDateTime(phrase, today, &when, &hasTime))
        return {};
    QString out = QLocale(QLocale::English).dayName(when.date().dayOfWeek(), QLocale::LongFormat)
                  + QLatin1Char(' ') + when.date().toString(Qt::ISODate);
    if (hasTime)
        out += QLatin1Char(' ') + when.time().toString(QStringLiteral("HH:mm"));
    return out;
}

} // namespace

QString annotateDates(const QString &text, const QDate &today) {
    // Tokenize into words with their positions, keeping everything else.
    static const QRegularExpression wordRe(QStringLiteral("\\S+"));
    struct Word { int pos; int len; QString text; };
    QList<Word> words;
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        words.append({m.capturedStart(), m.capturedLength(), m.captured(0)});
    }

    QString out;
    int cursor = 0;
    int i = 0;
    while (i < words.size()) {
        bool annotated = false;
        // Longest phrase first (up to 4 words), so "friday 12" wins over "friday".
        for (int len = qMin(4, words.size() - i); len >= 1 && !annotated; --len) {
            QStringList parts;
            for (int k = 0; k < len; ++k)
                parts << words[i + k].text;
            const QString phrase = parts.join(QLatin1Char(' '));
            // A bare number alone is usually a duration ("3 hours"), not a date.
            if (len == 1 && isBareNumber(words[i].text))
                break;
            // A number followed by a duration unit is never a clock time:
            // "onsdag 3 timer" is Wednesday, not Wednesday 03:00.
            if (isBareNumber(words[i + len - 1].text) && i + len < words.size()
                && DateVocabulary::isDurationUnit(words[i + len].text))
                continue;
            const QString annotation = annotationFor(phrase.toLower(), today);
            if (annotation.isEmpty())
                continue;
            out += text.mid(cursor, words[i].pos - cursor);
            out += phrase + QStringLiteral(" [%1]").arg(annotation);
            cursor = words[i + len - 1].pos + words[i + len - 1].len;
            i += len;
            annotated = true;
        }
        if (!annotated)
            ++i;
    }
    out += text.mid(cursor);
    return out;
}
