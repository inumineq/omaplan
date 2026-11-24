#include "render.h"

#include <QCollator>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>

#include "timeline.h"

// Kept in step with web/app.js function by function, under the same names.
// Page texts come from the dictionary (web/i18n/<language>.json).

namespace Render {

namespace {

// One dictionary, web/i18n/<language>.json.
struct Texts {
    QJsonObject json;
    QString operator[](const char *key) const { return json.value(QLatin1String(key)).toString(); }
    QString item(const char *list, int i) const { return json.value(QLatin1String(list)).toArray().at(i).toString(); }
    QString language() const { return (*this)["language"]; }
    // pageTexts: the plan's clock and project colours on top of the dictionary.
    bool clock12 = false;
    QJsonObject projectColors;
};

Texts pageTexts(const QJsonObject &plan, const Texts &dictionary) {
    Texts t = dictionary;
    t.clock12 = plan.value("clock").toString() == QLatin1String("12h");
    t.projectColors = plan.value("project_colors").toObject();
    return t;
}

// clockTime: "14:30", or "2:30 PM" when the plan's clock is 12h
QString clockTime(const Texts &t, const QString &time) {
    if (!t.clock12)
        return time;
    const int h = time.left(2).toInt();
    return QString::number((h + 11) % 12 + 1) + ':' + time.mid(3, 2) + (h < 12 ? QStringLiteral(" AM") : QStringLiteral(" PM"));
}

const Texts &texts(const QString &language) {
    static QHash<QString, Texts> cache;
    const QString lang = supportedLanguage(language).isEmpty() ? QStringLiteral("en") : supportedLanguage(language);
    auto it = cache.find(lang);
    if (it == cache.end()) {
        QFile file(QStringLiteral(":/web/i18n/%1.json").arg(lang));
        // A compiled-in resource: it opens, or the dictionary is empty.
        Texts texts;
        if (file.open(QIODevice::ReadOnly))
            texts.json = QJsonDocument::fromJson(file.readAll()).object();
        it = cache.insert(lang, texts);
    }
    return *it;
}

const QCollator &collator(const QString &language) {
    static QHash<QString, QCollator> cache;
    auto it = cache.find(language);
    if (it == cache.end())
        it = cache.insert(language, QCollator(QLocale(language)));
    return *it;
}

using Vars = QHash<QString, QString>;

// fill: "{d} {month}" with {d: 23, month: November} -> "23 November";
// unknown placeholders stay as written.
QString fill(const QString &tpl, const Vars &vars) {
    static const QRegularExpression re(QStringLiteral("\\{(\\w+)\\}"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(tpl);
    while (it.hasNext()) {
        const auto m = it.next();
        out += tpl.mid(last, m.capturedStart() - last);
        const auto value = vars.constFind(m.captured(1));
        out += value != vars.cend() ? *value : m.captured(0);
        last = m.capturedEnd();
    }
    return out + tpl.mid(last);
}

struct Item {
    QString id, title, notes, project, status, start, deadline;
    int priority = 3;
    int duration = 0;
    QStringList guessed;
};

// parseStart: "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM" -> date and time; anything
// else counts as unscheduled.
struct When {
    QString date, time;
};
When parseWhen(const QString &text) {
    static const QRegularExpression re(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})(?:T(\\d{2}):(\\d{2}))?$"));
    const auto m = re.match(text);
    if (text.isEmpty() || !m.hasMatch())
        return {};
    return {m.captured(1), m.captured(2).isEmpty() ? QString() : m.captured(2) + ':' + m.captured(3)};
}

QString u(const char *text) { return QString::fromUtf8(text); }

QDate dateOf(const QString &iso) { return QDate::fromString(iso, Qt::ISODate); }
QString addDays(const QString &iso, int n) { return dateOf(iso).addDays(n).toString(Qt::ISODate); }
QString capital(const QString &s) { return s.left(1).toUpper() + s.mid(1); }

// dateVars: the placeholders a date offers the dictionary's formats.
Vars dateVars(const Texts &t, const QString &date) {
    const int wd = dateOf(date).dayOfWeek() - 1; // the week starts on Monday
    return {{QStringLiteral("weekday"), t.item("weekdays", wd)},
            {QStringLiteral("Weekday"), capital(t.item("weekdays", wd))},
            {QStringLiteral("wd"), t.item("weekdaysShort", wd)},
            {QStringLiteral("Wd"), capital(t.item("weekdaysShort", wd))},
            {QStringLiteral("d"), QString::number(date.mid(8, 2).toInt())},
            {QStringLiteral("mm"), date.mid(5, 2)},
            {QStringLiteral("month"), t.item("months", date.mid(5, 2).toInt() - 1)},
            {QStringLiteral("yyyy"), date.left(4)}};
}

// formatDateLong: "Monday 23 November 2026"
QString formatDateLong(const Texts &t, const QString &date) { return fill(t["dateLong"], dateVars(t, date)); }
// formatDateShort: "Mon 23/11"
QString formatDateShort(const Texts &t, const QString &date) { return fill(t["dateShort"], dateVars(t, date)); }
// formatDeadline: "Friday 27 November 2026 at 12:00"
QString formatDeadline(const Texts &t, const QString &deadline) {
    const When w = parseWhen(deadline);
    const QString date = fill(t["dateInText"], dateVars(t, w.date));
    return w.time.isEmpty() ? date
                            : fill(t["atTime"], {{QStringLiteral("date"), date}, {QStringLiteral("time"), clockTime(t, w.time)}});
}
// formatTimestamp: "23 November 2026 at 13:57", wall time as written
QString formatTimestamp(const Texts &t, const QString &iso) {
    const When w = parseWhen(iso.left(16));
    if (w.date.isEmpty())
        return iso;
    Vars vars = dateVars(t, w.date);
    vars.insert(QStringLiteral("time"), clockTime(t, w.time));
    return fill(t["timestamp"], vars);
}
// formatDuration: "45 min", "2 h", "2 h 30 min"
QString formatDuration(const Texts &t, int min) {
    if (min < 60)
        return fill(t["minutes"], {{QStringLiteral("m"), QString::number(min)}});
    const QString h = QString::number(min / 60);
    const int m = min % 60;
    return m ? fill(t["hoursMinutes"], {{QStringLiteral("h"), h}, {QStringLiteral("m"), QString::number(m)}})
             : fill(t["hours"], {{QStringLiteral("h"), h}});
}
QString endTime(const QString &time, int minutes) {
    const int end = time.left(2).toInt() * 60 + time.mid(3, 2).toInt() + minutes;
    return QStringLiteral("%1:%2").arg((end / 60) % 24, 2, 10, QLatin1Char('0')).arg(end % 60, 2, 10, QLatin1Char('0'));
}
QString countTasks(const Texts &t, int n) {
    return n == 1 ? t["taskOne"] : fill(t["taskMany"], {{QStringLiteral("n"), QString::number(n)}});
}
// num: at most two decimals, no trailing zeros (9, 1.5, 0.25)
QString num(double x) {
    QString s = QString::number(x, 'f', 2);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
        s.chop(1);
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

// The rule of src/task.h (effectiveDate, isOverdue), as planDate/isOverdue/
// overdueDate/agendaDate in app.js.
QString planDate(const Item &o) {
    if (!parseWhen(o.start).date.isEmpty())
        return o.start.left(10);
    if (!parseWhen(o.deadline).date.isEmpty())
        return o.deadline.left(10);
    return {};
}
bool isOverdue(const Item &o, const QString &today) {
    if (o.status != QLatin1String("todo"))
        return false;
    if (!parseWhen(o.deadline).date.isEmpty())
        return o.deadline.left(10) < today;
    return !parseWhen(o.start).date.isEmpty() && o.start.left(10) < today;
}
// The overview day of an overdue task: the deadline it missed, else its start.
QString overdueDate(const Item &o) {
    if (!parseWhen(o.deadline).date.isEmpty())
        return o.deadline.left(10);
    return planDate(o);
}
QString agendaDate(const Item &o, const QString &today) {
    const QString d = planDate(o);
    return !d.isEmpty() && d < today && o.status == QLatin1String("todo") ? today : d;
}

// sortTasks: dated first (by date, timed before all-day, by time), then
// undated; then priority, then title (collated in the page's language).
QList<Item> sortTasks(QList<Item> items, const Texts &t) {
    const QCollator coll = collator(t.language());
    std::stable_sort(items.begin(), items.end(), [&](const Item &a, const Item &b) {
        const When sa = parseWhen(a.start), sb = parseWhen(b.start);
        if (sa.date.isEmpty() != sb.date.isEmpty())
            return !sa.date.isEmpty();
        if (!sa.date.isEmpty() && sa.date != sb.date)
            return sa.date < sb.date;
        if (!sa.date.isEmpty() && sa.time.isEmpty() != sb.time.isEmpty())
            return !sa.time.isEmpty();
        if (!sa.time.isEmpty() && !sb.time.isEmpty() && sa.time != sb.time)
            return sa.time < sb.time;
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return coll.compare(a.title, b.title) < 0;
    });
    return items;
}

// colorAttr: ' data-color="blue"' when the task's project has a colour (a
// slot of the theme palette), else ''
QString colorAttr(const Texts &t, const Item &o) {
    static const QRegularExpression slot(
        QStringLiteral("^(red|orange|yellow|green|cyan|blue|magenta|brown|bright_(red|yellow|green|cyan|blue|magenta))$"));
    const QString value = o.project.isEmpty() ? QString() : t.projectColors.value(o.project).toString();
    return slot.match(value).hasMatch() ? " data-color=\"" + value + "\"" : QString();
}

QString renderTask(const Item &o, const Texts &t) {
    const When s = parseWhen(o.start);
    QString timeText, timeClass = QStringLiteral("time");
    if (s.date.isEmpty()) {
        timeText = t["timeNone"];
        timeClass += QStringLiteral(" time-none");
    } else if (!s.time.isEmpty()) {
        timeText = o.duration ? clockTime(t, s.time) + u("–") + clockTime(t, endTime(s.time, o.duration))
                              : clockTime(t, s.time);
    } else {
        timeText = t["allDay"];
        timeClass += QStringLiteral(" time-all-day");
    }
    QString meta;
    if (!o.project.isEmpty())
        meta += "<span class=\"project-tag\">" + esc(o.project) + "</span>";
    if (!parseWhen(o.deadline).date.isEmpty())
        meta += "<span class=\"deadline\">"
                + esc(fill(t["deadline"], {{QStringLiteral("date"), formatDeadline(t, o.deadline)}})) + "</span>";
    if (o.duration)
        meta += "<span class=\"duration\">" + esc(formatDuration(t, o.duration)) + "</span>";
    if (!o.guessed.isEmpty()) {
        const QJsonObject names = t.json.value(QLatin1String("guessNames")).toObject();
        QStringList fields;
        for (const QString &g : o.guessed)
            fields << names.value(g).toString(g);
        meta += "<span class=\"guessed\">" + esc(fill(t["guessed"], {{QStringLiteral("fields"), fields.join(", ")}}))
                + "</span>";
    }
    const QString p = QString::number(o.priority);
    meta += "<span class=\"badge badge-p" + p + "\">P" + p + "</span>";
    if (o.status == QLatin1String("done"))
        meta += "<span class=\"badge badge-done\">" + esc(t["done"]) + "</span>";
    const QString projectAttr = o.project.isEmpty() ? QString() : " data-project=\"" + esc(o.project) + "\"";
    const QString notes = o.notes.isEmpty() ? QString()
                                            : "\n    <details class=\"notes\"><summary>" + esc(t["notes"])
                                                  + "</summary><p>" + esc(o.notes) + "</p></details>";
    return "<li class=\"task status-" + esc(o.status) + "\" data-id=\"" + esc(o.id) + "\" data-priority=\"" + p + "\""
           + projectAttr + colorAttr(t, o) + ">\n"
           + "    <p class=\"task-row\"><span class=\"" + timeClass + "\">" + esc(timeText)
           + "</span><span class=\"task-title\">" + esc(o.title) + "</span></p>\n"
           + "    <p class=\"task-meta\">" + meta + "</p>" + notes + "\n"
           + "  </li>";
}

QString renderTaskList(const QList<Item> &items, const Texts &t) {
    QStringList lines;
    for (const Item &o : items)
        lines << "    " + renderTask(o, t).replace('\n', QStringLiteral("\n    "));
    return "<ul class=\"tasks\">\n" + lines.join('\n') + "\n  </ul>";
}

QString renderDay(const QString &date, const QList<Item> &items, const QString &extraClass, const QString &badgeClass,
                  const QString &badgeText, const Texts &t) {
    const QString badge = badgeClass.isEmpty()
                              ? QString()
                              : "<span class=\"badge " + badgeClass + "\">" + esc(badgeText) + "</span>";
    const QString body = items.isEmpty() ? "    <p class=\"empty\">" + esc(t["noTasks"]) + "</p>" : renderTaskList(items, t);
    return "  <section class=\"day" + (extraClass.isEmpty() ? QString() : ' ' + extraClass) + "\" data-date=\"" + date
           + "\">\n"
           + "    <h3><time datetime=\"" + date + "\">" + esc(formatDateLong(t, date)) + "</time>" + badge + "</h3>\n"
           + body + "\n"
           + "  </section>";
}

QString viewClass(const QString &id, const QString &active) {
    return id == active ? QStringLiteral("view active") : QStringLiteral("view");
}

QString renderOverview(const QList<Item> &items, const QString &today, const Texts &t, const QString &active) {
    QMap<QString, QList<Item>> groups;
    for (const Item &o : items)
        if (isOverdue(o, today))
            groups[overdueDate(o)].append(o);
    QStringList parts;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        parts << renderDay(it.key(), sortTasks(it.value(), t), QStringLiteral("overdue"), QStringLiteral("badge-overdue"),
                           t["overdue"], t);
    for (int k = 0; k < 8; ++k) {
        const QString date = addDays(today, k);
        QList<Item> onDay;
        for (const Item &o : items)
            if (!isOverdue(o, today) && agendaDate(o, today) == date) // undated tasks are not in the overview
                onDay << o;
        parts << renderDay(date, sortTasks(onDay, t), k == 0 ? QStringLiteral("today") : QString(),
                           k == 0 ? QStringLiteral("badge-today") : QString(), t["today"], t);
    }
    return "  <section id=\"overview\" class=\"" + viewClass("overview", active) + "\" aria-label=\"" + esc(t["overview"])
           + "\">\n"
           + "    <h2>" + esc(t["overview"]) + "</h2>\n"
           + "    <p class=\"description\">" + esc(t["overviewDescription"]) + "</p>\n"
           + parts.join('\n') + "\n"
           + "  </section>";
}

QString renderGroup(const QString &dataGroup, const QString &heading, const QList<Item> &items, const Texts &t) {
    return "  <section class=\"group\" data-group=\"" + dataGroup + "\">\n"
           + "    <h3>" + heading + "<span class=\"count\">" + esc(countTasks(t, items.size())) + "</span></h3>\n"
           + renderTaskList(items, t) + "\n"
           + "  </section>";
}

QString renderListByDate(const QList<Item> &items, const Texts &t, const QString &active) {
    QMap<QString, QList<Item>> groups;
    QList<Item> undated;
    for (const Item &o : items) {
        const QString d = planDate(o);
        if (d.isEmpty())
            undated << o; // the group "No date" comes last
        else
            groups[d].append(o);
    }
    QStringList parts;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        parts << renderGroup("date:" + it.key(),
                             "<time datetime=\"" + it.key() + "\">" + esc(formatDateLong(t, it.key())) + "</time>",
                             sortTasks(it.value(), t), t);
    if (!undated.isEmpty())
        parts << renderGroup(QStringLiteral("no-date"), "<span class=\"group-title\">" + esc(t["noDate"]) + "</span>",
                             sortTasks(undated, t), t);
    return "  <section id=\"list\" class=\"" + viewClass("list", active) + "\" aria-label=\"" + esc(t["listByDate"])
           + "\">\n"
           + "    <h2>" + esc(t["list"]) + "</h2>\n"
           + "    <p class=\"description\">" + esc(t["listDescription"]) + " <a href=\"#list-project\">"
           + esc(t["groupByProject"]) + "</a></p>\n"
           + parts.join('\n') + "\n"
           + "  </section>";
}

QString renderListByProject(const QList<Item> &items, const Texts &t, const QString &active) {
    const QString none = t["noProject"];
    QStringList names;
    QHash<QString, QList<Item>> groups;
    for (const Item &o : items) {
        const QString p = o.project.isEmpty() ? none : o.project;
        if (!groups.contains(p))
            names << p;
        groups[p].append(o);
    }
    const QCollator coll = collator(t.language());
    std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) {
        if (a == none || b == none)
            return b == none && a != none;
        return coll.compare(a, b) < 0;
    });
    QStringList parts;
    for (const QString &name : names)
        parts << renderGroup(name == none ? QStringLiteral("no-project") : "project:" + esc(name),"<span class=\"group-title\">" + esc(name) + "</span>",
                             sortTasks(groups.value(name), t), t);
    return "  <section id=\"list-project\" class=\"" + viewClass("list-project", active) + "\" aria-label=\""
           + esc(t["listByProject"]) + "\">\n"
           + "    <h2>" + esc(t["listByProject"]) + "</h2>\n"
           + "    <p class=\"description\">" + esc(t["listProjectDescription"]) + " <a href=\"#list\">"
           + esc(t["groupByDate"]) + "</a></p>\n"
           + parts.join('\n') + "\n"
           + "  </section>";
}

// ---------- timeline (Gantt) ----------

// A moment as written in plan.json: a date and minutes into the day (0-1439), wall time.
struct Moment {
    QString date;
    int min = 0;
    bool valid() const { return !date.isEmpty(); }
};
int minutesOf(const QString &time) { return time.left(2).toInt() * 60 + time.mid(3, 2).toInt(); }
Moment addMinutes(const Moment &m, int n) {
    const int total = m.min + n;
    return {addDays(m.date, int(std::floor(total / 1440.0))), total % 1440};
}
int compareMoments(const Moment &a, const Moment &b) {
    return a.date < b.date ? -1 : a.date > b.date ? 1 : a.min - b.min;
}
int daysBetween(const QString &a, const QString &b) { return int(dateOf(a).daysTo(dateOf(b))); }
QString mondayOf(const QString &date) { return addDays(date, 1 - dateOf(date).dayOfWeek()); }
QString timeOf(int min) {
    return QStringLiteral("%1:%2").arg(min / 60, 2, 10, QLatin1Char('0')).arg(min % 60, 2, 10, QLatin1Char('0'));
}

// taskSpan: the rule of taskSpan in task.h, over plan.json's fields.
struct Span {
    Moment start, end;
    bool allDay = false, toDeadline = false;
    Moment mark;
    bool markAllDay = false;
};
Span taskSpan(const Item &o) {
    const When s = parseWhen(o.start);
    const When d = parseWhen(o.deadline);
    const Moment due = d.date.isEmpty() ? Moment{}
                       : !d.time.isEmpty() ? Moment{d.date, minutesOf(d.time)}
                                           : Moment{addDays(d.date, 1), 0};
    Span span;
    if (!s.date.isEmpty()) {
        span.allDay = s.time.isEmpty();
        span.start = {s.date, span.allDay ? 0 : minutesOf(s.time)};
        if (o.duration > 0) {
            span.end = span.allDay ? Moment{addDays(s.date, std::max(1, int(std::ceil(o.duration / 1440.0)))), 0}
                                   : addMinutes(span.start, o.duration);
        } else if (due.valid() && d.date > s.date) {
            span.end = span.allDay ? Moment{addDays(d.date, 1), 0} : due;
            span.toDeadline = true;
        } else {
            span.end = span.allDay ? Moment{addDays(s.date, 1), 0} : addMinutes(span.start, 30);
        }
    }
    if (due.valid() && (!span.end.valid() || compareMoments(due, span.end) > 0)) {
        span.mark = {d.date, d.time.isEmpty() ? 0 : minutesOf(d.time)};
        span.markAllDay = d.time.isEmpty();
    }
    return span;
}

// WEEK_REM, CHAR_REM, BAR_MIN_REM, MARK_REM and TIMELINE_GEOMETRY in app.js.
const double kWeekRem = 48;
const double kCharRem = 0.47;
const double kBarMinRem = 0.35;
const double kMarkRem = 0.6;
const Timeline::Geometry kTimelineGeometry{kWeekRem, 0.3, 0.3, 0.9};

// spanText: "Mon 23/11 14:00–15:30", "Mon 23/11 (all day)", "Mon 23/11 – Fri 27/11", ...
QString spanText(const Texts &t, const Span &s) {
    if (s.allDay) {
        const QString last = addDays(s.end.date, -1);
        return last == s.start.date ? formatDateShort(t, s.start.date) + " (" + t["allDay"] + ")"
                                    : formatDateShort(t, s.start.date) + u(" – ") + formatDateShort(t, last);
    }
    const auto at = [&](const Moment &m) { return formatDateShort(t, m.date) + ' ' + clockTime(t, timeOf(m.min)); };
    const bool sameDay = s.end.date == s.start.date || (s.end.min == 0 && s.end.date == addDays(s.start.date, 1));
    return sameDay ? at(s.start) + u("–") + clockTime(t, timeOf(s.end.min)) : at(s.start) + u(" – ") + at(s.end);
}

struct Shown {
    const Item *o = nullptr;
    Span span;
    bool hasBar = false, hasMark = false, cutStart = false, cutEnd = false;
    double barStart = 0, barEnd = 0, markX = 0;
};

QString renderTimeline(const QList<Item> &items, const QString &today, const Texts &t, const QString &active) {
    QList<QPair<const Item *, Span>> tasks;
    for (const Item &o : items) {
        const Span span = taskSpan(o);
        if (span.start.valid() || span.mark.valid())
            tasks.append({&o, span});
    }
    const QCollator coll = collator(t.language());
    std::stable_sort(tasks.begin(), tasks.end(), [&](const auto &a, const auto &b) {
        const int c = compareMoments(a.second.start.valid() ? a.second.start : a.second.mark,
                                     b.second.start.valid() ? b.second.start : b.second.mark);
        if (c != 0)
            return c < 0;
        return coll.compare(a.first->title, b.first->title) < 0;
    });

    // The hours of a day: the tasks with a time, from their start to their end on the same day, at least 08-18.
    int from = 8, to = 18;
    for (const auto &x : std::as_const(tasks)) {
        const Span &s = x.second;
        if (!s.start.valid() || s.allDay)
            continue;
        from = std::min(from, s.start.min / 60);
        const int end = s.end.date == s.start.date ? s.end.min : s.start.min + 30;
        to = std::max(to, std::min(24, int(std::ceil(end / 60.0))));
    }

    QStringList dates;
    for (const auto &x : std::as_const(tasks)) {
        const Span &s = x.second;
        if (s.start.valid())
            dates << s.start.date << (s.end.min == 0 ? addDays(s.end.date, -1) : s.end.date);
        if (s.mark.valid())
            dates << s.mark.date;
    }
    std::sort(dates.begin(), dates.end());
    const QString first = mondayOf(dates.isEmpty() ? today : dates.first());
    const QString last = mondayOf(dates.isEmpty() ? today : dates.last());

    const auto pct = [](double rem) { return num(rem / kWeekRem * 100); };
    QStringList weeks;
    for (QString week = first; week <= last; week = addDays(week, 7)) {
        const Moment weekStart{week, 0}, weekEnd{addDays(week, 7), 0};
        // rem from the week's start; `edge`: a whole-day boundary
        const auto at = [&](const Moment &m, bool edge) {
            const int day = daysBetween(week, m.date);
            if (day < 0)
                return 0.0;
            if (day >= 7)
                return kWeekRem;
            const double within = edge ? 0 : std::max(0.0, std::min(1.0, (m.min / 60.0 - from) / (to - from)));
            return (day + within) / 7 * kWeekRem;
        };
        QList<Shown> shown;
        QList<Timeline::Item> layoutItems;
        for (const auto &x : std::as_const(tasks)) {
            const Span &s = x.second;
            Shown b;
            b.o = x.first;
            b.span = s;
            if (s.start.valid() && compareMoments(s.start, weekEnd) < 0 && compareMoments(s.end, weekStart) > 0) {
                b.hasBar = true;
                b.cutStart = compareMoments(s.start, weekStart) < 0;
                b.cutEnd = compareMoments(s.end, weekEnd) > 0;
                b.barStart = b.cutStart ? 0 : at(s.start, s.allDay);
                b.barEnd = b.cutEnd ? kWeekRem : at(s.end, s.allDay);
                b.barEnd = std::max(b.barEnd, std::min(kWeekRem, b.barStart + kBarMinRem));
            }
            if (s.mark.valid() && s.mark.date >= week && s.mark.date < weekEnd.date) {
                b.hasMark = true;
                double c = s.markAllDay ? at({addDays(s.mark.date, 1), 0}, true) - 0.4 : at(s.mark, false);
                c = std::max(kMarkRem / 2, std::min(kWeekRem - kMarkRem / 2, c));
                if (b.hasBar)
                    c = std::max(c, b.barEnd + kMarkRem / 2);
                b.markX = c;
            }
            if (!b.hasBar && !b.hasMark)
                continue;
            if (!b.hasBar)
                b.barStart = b.barEnd = b.markX - kMarkRem / 2;
            shown << b;
            layoutItems << Timeline::Item{b.barStart, b.barEnd, b.hasMark, b.markX - kMarkRem / 2, b.markX + kMarkRem / 2,
                                          b.o->title.size() * kCharRem};
        }
        const QList<Timeline::Placement> placed = Timeline::layout(layoutItems, kTimelineGeometry);
        QStringList lines;
        int lanes = 1;
        for (int i = 0; i < shown.size(); ++i) {
            const Shown &b = shown[i];
            const Timeline::Placement &p = placed[i];
            const Item &o = *b.o;
            const Span &s = b.span;
            lanes = std::max(lanes, p.lane + 1);
            const QString lane = ";--lane:" + QString::number(p.lane);
            const QString done = o.status == QLatin1String("done") ? QStringLiteral(" status-done") : QString();
            const QString markTitle = s.mark.valid() ? fill(t["deadlineTitle"], {{QStringLiteral("date"), formatDeadline(t, o.deadline)},
                                                                                {QStringLiteral("title"), o.title}})
                                                     : QString();
            const QString title = s.start.valid() ? o.title + u(" – ") + spanText(t, s) : markTitle;
            if (b.hasBar)
                lines << "          <div class=\"bar" + QString(s.allDay ? " bar-all-day" : "") + done
                             + QString(b.cutStart ? " bar-cut-start" : "") + QString(b.cutEnd ? " bar-cut-end" : "")
                             + "\" style=\"--x:" + pct(b.barStart) + ";--w:" + pct(b.barEnd - b.barStart) + lane
                             + "\" data-id=\"" + esc(o.id) + "\"" + colorAttr(t, o) + " title=\"" + esc(title) + "\">"
                             + (p.side == Timeline::Side::Inside ? "<span class=\"bar-title\">" + esc(o.title) + "</span>" : QString())
                             + "</div>";
            if (b.hasBar && b.hasMark)
                lines << "          <div class=\"deadline-line\" style=\"--x:" + pct(b.barEnd) + ";--w:" + pct(b.markX - b.barEnd)
                             + lane + "\"></div>";
            if (b.hasMark)
                lines << "          <div class=\"deadline-mark" + done + "\" style=\"--x:" + pct(b.markX) + lane + "\" data-id=\""
                             + esc(o.id) + "\" title=\"" + esc(markTitle) + "\"></div>";
            // A title before its bar is anchored by its right edge, so it stays at the bar on a wider page.
            if (p.side != Timeline::Side::Inside && p.labelWidth > 0) {
                const bool before = p.side == Timeline::Side::Before;
                lines << "          <span class=\"bar-label" + QString(before ? " bar-label-before" : "") + done
                             + "\" style=\"--x:" + pct(before ? p.labelStart + p.labelWidth : p.labelStart) + lane
                             + (p.shortened ? ";--max:" + num(p.labelWidth) : QString()) + "\" data-id=\"" + esc(o.id)
                             + "\" title=\"" + esc(title) + "\">" + esc(o.title) + "</span>";
            }
        }
        QStringList days;
        for (int i = 0; i < 7; ++i) {
            const QString day = addDays(week, i);
            days << "          <h3 class=\"gantt-date" + QString(day == today ? " today" : "") + "\"><time datetime=\"" + day
                        + "\">" + esc(formatDateShort(t, day)) + "</time></h3>";
        }
        weeks << "      <section class=\"gantt-week\" data-week=\"" + week + "\">\n"
                     + "        <div class=\"gantt-days\">\n"
                     + days.join('\n') + "\n"
                     + "        </div>\n"
                     + "        <div class=\"lanes\" style=\"--lanes:" + QString::number(lanes) + "\">\n"
                     + (lines.isEmpty() ? QString() : lines.join('\n') + "\n")
                     + "        </div>\n"
                     + "      </section>";
    }
    const QString description = fill(t["timelineDescription"], {{QStringLiteral("from"), QString::number(from)},
                                                                {QStringLiteral("to"), QString::number(to)}});
    return "  <section id=\"timeline\" class=\"" + viewClass("timeline", active) + "\" aria-label=\"" + esc(t["timeline"])
           + "\">\n"
           + "    <h2>" + esc(t["timeline"]) + "</h2>\n"
           + "    <p class=\"description\">" + esc(description) + "</p>\n"
           + "    <div class=\"gantt\">\n"
           + weeks.join('\n') + "\n"
           + "    </div>\n"
           + "  </section>";
}

QList<Item> itemsOf(const QJsonObject &plan) {
    QList<Item> items;
    for (const QJsonValue &value : plan.value("tasks").toArray()) {
        const QJsonObject json = value.toObject();
        Item o;
        o.id = json.value("id").toString();
        o.title = json.value("title").toString();
        o.notes = json.value("notes").toString();
        o.project = json.value("project").toString();
        o.priority = json.value("priority").toInt(3);
        o.status = json.value("status").toString(QStringLiteral("todo"));
        o.start = json.value("start").toString();
        o.deadline = json.value("deadline").toString();
        o.duration = std::max(0, json.value("duration_min").toInt(0));
        for (const QJsonValue &g : json.value("guessed").toArray())
            o.guessed << g.toString();
        items << o;
    }
    return items;
}

// renderPage without the main content: renderNav, renderFooter and the frame.
QString frame(const Texts &t, const QString &htmlStyle, const QString &meta, const QString &mainAttrs,
              const QString &body) {
    const QString style = htmlStyle.isEmpty() ? QString() : " style=\"" + esc(htmlStyle) + "\"";
    return "<!doctype html>\n"
           "<html lang=\"" + esc(t.language()) + "\"" + style + ">\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>" + esc(t["title"]) + "</title>\n"
           "<link rel=\"stylesheet\" href=\"style.css\">\n"
           "</head>\n"
           "<body>\n"
           "<header class=\"top\">\n"
           "  <h1>Omaplan</h1>\n"
           "  <nav class=\"views\" aria-label=\"" + esc(t["views"]) + "\">\n"
           "    <a href=\"#overview\">" + esc(t["overview"]) + "</a>\n"
           "    <a href=\"#list\">" + esc(t["list"]) + "</a>\n"
           "    <a href=\"#timeline\">" + esc(t["timeline"]) + "</a>\n"
           "  </nav>\n"
           "  <p class=\"meta\">" + meta + "</p>\n"
           "</header>\n"
           "<main id=\"plan\" class=\"plan\"" + mainAttrs + ">\n"
           + body + "\n"
           "</main>\n"
           "<footer class=\"footer\">\n"
           "  <p>" + esc(t["footer"]) + u(" · <a href=\"plan.json\">plan.json</a></p>\n")
           + "</footer>\n"
             "<script src=\"app.js\"></script>\n"
             "</body>\n"
             "</html>\n";
}

} // namespace

bool isView(const QString &id) {
    for (const char *view : kViews)
        if (id == QLatin1String(view))
            return true;
    return false;
}

QString supportedLanguage(const QString &tag) {
    const QString lower = tag.trimmed().toLower();
    for (const char *language : kLanguages)
        if (lower.startsWith(QLatin1String(language)))
            return QLatin1String(language);
    return {};
}

QString esc(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        case '\'': out += QStringLiteral("&#39;"); break;
        default: out += c;
        }
    }
    return out;
}

QString page(const QJsonObject &plan, const QDate &today, const QString &language, const QString &htmlStyle,
             const QString &activeView) {
    const Texts t = pageTexts(plan, texts(language));
    const QList<Item> items = itemsOf(plan);
    const QString todayIso = today.toString(Qt::ISODate);
    const QString active = isView(activeView) ? activeView : QString();
    const QString exported = plan.value("exported").toString();
    // renderMeta
    const QString meta = esc(t["exported"]) + " <time datetime=\"" + esc(exported) + "\">"
                         + esc(formatTimestamp(t, exported)) + "</time>"
                         + u(" <span class=\"dot\">·</span> <span class=\"timezone\">")
                         + esc(plan.value("timezone").toString()) + "</span>";
    // renderMain
    const QString body = QStringList{renderOverview(items, todayIso, t, active), renderListByDate(items, t, active),
                                     renderListByProject(items, t, active), renderTimeline(items, todayIso, t, active)}
                             .join('\n');
    return frame(t, htmlStyle, meta, " data-today=\"" + todayIso + "\"", body);
}

QString messagePage(Message message, const QString &language, const QString &htmlStyle) {
    const Texts &t = texts(language);
    const bool noPlan = message == Message::NoPlan;
    const QString body = "  <section id=\"overview\" class=\"view\" aria-label=\"" + esc(t["overview"]) + "\">\n"
                         "    <h2>" + esc(t[noPlan ? "noPlanHeading" : "unreadableHeading"]) + "</h2>\n"
                         "    <p class=\"description\">" + esc(t[noPlan ? "noPlanText" : "unreadableText"]) + "</p>\n"
                         "  </section>";
    return frame(t, htmlStyle, esc(t["noSnapshot"]), QString(), body);
}

} // namespace Render
