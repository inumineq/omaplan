#pragma once

#include <QDate>
#include <QString>

// Deterministic date annotation before an AI call: Danish/English date and
// time expressions in free text are resolved against `today` and written into
// the text in brackets, e.g. "til torsdag" -> "til torsdag [fredag
// 2026-11-27]". Small local models then only have to copy the ISO date.
// Pure function; reuses Actions::parseDateTime for the resolving.
//
// Covered: i dag/i morgen/i overmorgen, weekdays (with "næste"/"next" and an
// optional time), "næste uge", "i weekenden", "om N dage", "den 12." /
// "12. december" / "12/12", times ("kl. 14:30", "14:30"), and halv/kvart
// i/over with number words. A bare number ("3 timer") is deliberately NOT
// annotated -- in free text it is usually a duration, not a date.
QString annotateDates(const QString &text, const QDate &today);
