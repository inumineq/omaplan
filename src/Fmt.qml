pragma Singleton
import QtQuick

// Dates, times and durations in the Omarchy language. Every binding that
// calls these reads `lang`, so it re-renders on a language switch.
// Danish: "mandag 24. november", the week starts on Monday.
QtObject {
    readonly property string lang: translator.language
    readonly property bool danish: lang.indexOf("da") === 0
    readonly property var locale: Qt.locale(danish ? "da_DK" : "en_GB")

    function startOfDay(d) { return new Date(d.getFullYear(), d.getMonth(), d.getDate()) }
    function addDays(d, n) { var x = new Date(d); x.setDate(x.getDate() + n); return x }
    function addMonths(d, n) { return new Date(d.getFullYear(), d.getMonth() + n, 1) }
    function mondayOf(d) { var x = startOfDay(d); x.setDate(x.getDate() - ((x.getDay() + 6) % 7)); return x }
    function sameDay(a, b) {
        return a && b && a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
               && a.getDate() === b.getDate()
    }
    // Whole calendar days from a to b, safe across daylight saving changes.
    function daysBetween(a, b) {
        return Math.round((Date.UTC(b.getFullYear(), b.getMonth(), b.getDate())
                           - Date.UTC(a.getFullYear(), a.getMonth(), a.getDate())) / 86400000)
    }
    function valid(d) { return d instanceof Date && !isNaN(d.getTime()) }
    function isoDay(d) { return Qt.formatDate(d, "yyyy-MM-dd") }
    function fromIso(text) {
        // "2026-11-24" or "2026-11-24T14:30" as local time.
        var m = /^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2}))?/.exec(text || "")
        if (!m) return new Date(NaN)
        return new Date(+m[1], +m[2] - 1, +m[3], m[4] ? +m[4] : 0, m[5] ? +m[5] : 0)
    }
    function isoWeek(d) {
        var x = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()))
        var day = x.getUTCDay() || 7
        x.setUTCDate(x.getUTCDate() + 4 - day)
        var yearStart = new Date(Date.UTC(x.getUTCFullYear(), 0, 1))
        return Math.ceil(((x - yearStart) / 86400000 + 1) / 7)
    }

    function time(d) { return valid(d) ? d.toLocaleTimeString(locale, "HH:mm") : "" }
    // "mandag 24. november" / "Monday 24 November"
    function dayLong(d) { return d.toLocaleDateString(locale, danish ? "dddd d. MMMM" : "dddd d MMMM") }
    // "man. 24." / "Mon 24"
    function dayShort(d) { return d.toLocaleDateString(locale, danish ? "ddd d." : "ddd d") }
    // "24. nov." / "24 Nov"
    function dateShort(d) { return d.toLocaleDateString(locale, danish ? "d. MMM" : "d MMM") }
    function weekdayShort(d) { return d.toLocaleDateString(locale, "ddd") }
    function monthTitle(d) { return d.toLocaleDateString(locale, "MMMM yyyy") }
    function dayNumber(d) { return d.toLocaleDateString(locale, "d") }

    // "I dag", "I morgen", "I går" or the long day.
    function relativeDay(d, today) {
        var n = daysBetween(today, d)
        if (n === 0) return qsTr("Today")
        if (n === 1) return qsTr("Tomorrow")
        if (n === -1) return qsTr("Yesterday")
        return capitalize(dayLong(d))
    }
    // Short relative date for meta lines: "i dag", "i morgen", "fre. 27.", "3. dec."
    function relativeShort(d, today) {
        var n = daysBetween(today, d)
        if (n === 0) return qsTr("today")
        if (n === 1) return qsTr("tomorrow")
        if (n === -1) return qsTr("yesterday")
        if (n > 1 && n < 7) return dayShort(d)
        return dateShort(d)
    }
    function capitalize(s) { return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

    // "1 t 30 min" / "1 h 30 min"; 45 -> "45 min"
    function duration(minutes) {
        if (!minutes || minutes <= 0) return ""
        var h = Math.floor(minutes / 60), m = minutes % 60
        if (h === 0) return qsTr("%1 min").arg(m)
        if (m === 0) return qsTr("%1 h").arg(h)
        return qsTr("%1 h %2 min").arg(h).arg(m)
    }
    // "09:00–10:30" when the task has a time and a duration.
    function timeRange(start, minutes) {
        if (!valid(start)) return ""
        if (!minutes) return time(start)
        return time(start) + "–" + time(new Date(start.getTime() + minutes * 60000))
    }
    // "uge 48 · 23.–29. november" / "Week 48 · 23–29 November"
    function weekTitle(monday) {
        var sunday = addDays(monday, 6)
        var range = monday.getMonth() === sunday.getMonth()
                ? (danish ? dayNumber(monday) + ".–" + sunday.toLocaleDateString(locale, "d. MMMM yyyy")
                          : dayNumber(monday) + "–" + sunday.toLocaleDateString(locale, "d MMMM yyyy"))
                : dateShort(monday) + " – " + sunday.toLocaleDateString(locale, danish ? "d. MMM yyyy" : "d MMM yyyy")
        return qsTr("Week %1").arg(isoWeek(monday)) + " · " + range
    }
    function priorityLabel(p) { return p === 1 ? qsTr("High") : p === 2 ? qsTr("Medium") : "" }
}
