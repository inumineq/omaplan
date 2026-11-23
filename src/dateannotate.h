#pragma once

#include <QDate>
#include <QString>

// Deterministic date annotation before an AI call: date and time expressions
// in free text are resolved against `today` and written into the text in
// brackets, e.g. "til torsdag" -> "til torsdag [Thursday 2026-11-26]" and
// "call Anna tomorrow at 14" -> "... tomorrow at 14 [Tuesday 2026-11-24 14:00]".
// Small local models then only have to copy the ISO date. Pure function;
// resolves through Actions::parseDateTime, so it understands every language
// in datewords.h (Danish and English today).
//
// Covered: today/tomorrow/the day after, weekdays (with "next" and an
// optional time), "next week", the weekend, "in N days", "den 12." /
// "12. december" / "12/12", times ("kl. 14:30", "at 14:30", "14:30"), and
// spoken clock times with number words ("halv tre", "quarter past four"). A
// bare number ("3 hours") is deliberately NOT annotated -- in free text it is
// usually a duration, not a date. The annotation itself is language-neutral:
// an English weekday, the ISO date and a 24-hour time.
QString annotateDates(const QString &text, const QDate &today);
