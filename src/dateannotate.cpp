#include "dateannotate.h"

#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

#include "actions.h"

namespace {

const QHash<QString, int> &numberWords() {
    static const QHash<QString, int> map = {
        {QStringLiteral("en"), 1}, {QStringLiteral("et"), 1}, {QStringLiteral("to"), 2},
        {QStringLiteral("tre"), 3}, {QStringLiteral("fire"), 4}, {QStringLiteral("fem"), 5},
        {QStringLiteral("seks"), 6}, {QStringLiteral("syv"), 7}, {QStringLiteral("otte"), 8},
        {QStringLiteral("ni"), 9}, {QStringLiteral("ti"), 10}, {QStringLiteral("elleve"), 11},
        {QStringLiteral("tolv"), 12},
        {QStringLiteral("one"), 1}, {QStringLiteral("two"), 2}, {QStringLiteral("three"), 3},
        {QStringLiteral("four"), 4}, {QStringLiteral("five"), 5}, {QStringLiteral("six"), 6},
        {QStringLiteral("seven"), 7}, {QStringLiteral("eight"), 8}, {QStringLiteral("nine"), 9},
        {QStringLiteral("ten"), 10}, {QStringLiteral("eleven"), 11}, {QStringLiteral("twelve"), 12},
    };
    return map;
}

// "halv tre" = 14:30, "kvart i tre" = 14:45, "kvart over tre" = 15:15.
bool parseNumberWordTime(const QString &phrase, QTime *time) {
    static const QRegularExpression re(
        QStringLiteral("^(halv|kvart i|kvart over|quarter to|quarter past)\\s+(\\w+)$"));
    const auto m = re.match(phrase.toLower());
    if (!m.hasMatch() || !numberWords().contains(m.captured(2)))
        return false;
    const int hour = numberWords().value(m.captured(2));
    const QString kind = m.captured(1);
    // Danish 12-hour convention: "halv tre" = 14:30, "kvart over fire" =
    // 15:15. The named hour is read as PM unless it is 12 (noon).
    int h = hour - 1;
    if (h < 0)
        h = 23;
    if (h < 12 && hour != 12)
        h += 12;
    if (kind == QLatin1String("halv"))
        *time = QTime(h, 30);
    else if (kind == QLatin1String("kvart i") || kind == QLatin1String("quarter to"))
        *time = QTime(h, 45);
    else
        *time = QTime(h, 15);
    return time->isValid();
}

bool isBareNumber(const QString &word) {
    static const QRegularExpression re(QStringLiteral("^\\d{1,2}$"));
    return re.match(word).hasMatch();
}

bool isTimeOnlyPhrase(const QString &phrase) {
    static const QRegularExpression re(
        QStringLiteral("^(?:kl\\.?|at)?\\s*\\d{1,2}(?:[:.]\\d{2})?$"), QRegularExpression::CaseInsensitiveOption);
    return re.match(phrase.trimmed()).hasMatch();
}

QString annotationFor(const QString &phrase, const QDate &today) {
    // halv/kvart i/over with number words first -- parseDateTime does not know those.
    QTime wordTime;
    if (parseNumberWordTime(phrase, &wordTime))
        return QStringLiteral("kl. %1").arg(wordTime.toString(QStringLiteral("HH:mm")));

    if (isTimeOnlyPhrase(phrase)) {
        QDateTime when;
        bool hasTime = false;
        if (Actions::parseDateTime(phrase, today, &when, &hasTime) && hasTime)
            return QStringLiteral("kl. %1").arg(when.time().toString(QStringLiteral("HH:mm")));
        return {};
    }

    QDateTime when;
    bool hasTime = false;
    if (!Actions::parseDateTime(phrase, today, &when, &hasTime))
        return {};
    const QLocale da(QLocale::Danish);
    QString out = da.standaloneDayName(when.date().dayOfWeek(), QLocale::LongFormat)
                  + QLatin1Char(' ') + when.date().toString(Qt::ISODate);
    if (hasTime)
        out += QStringLiteral(" kl. %1").arg(when.time().toString(QStringLiteral("HH:mm")));
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
        // Longest phrase first (up to 4 words), so "fredag 12" wins over "fredag".
        for (int len = qMin(4, words.size() - i); len >= 1 && !annotated; --len) {
            QStringList parts;
            for (int k = 0; k < len; ++k)
                parts << words[i + k].text;
            const QString phrase = parts.join(QLatin1Char(' '));
            // A bare number alone is usually a duration ("3 timer"), not a date.
            if (len == 1 && isBareNumber(words[i].text))
                break;
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
