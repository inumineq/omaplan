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

// Kept in step with web/app.js function by function, under the same names.
// Page texts come from the dictionary (web/i18n/<language>.json).

namespace Render {

namespace {

const double kCharsPerDay = 64; // CHARS_PER_DAY: characters across a whole-day bar at the column's minimum width

// One dictionary, web/i18n/<language>.json.
struct Texts {
    QJsonObject json;
    QString operator[](const char *key) const { return json.value(QLatin1String(key)).toString(); }
    QString item(const char *list, int i) const { return json.value(QLatin1String(list)).toArray().at(i).toString(); }
    QString language() const { return (*this)["language"]; }
};

const Texts &texts(const QString &language) {
    static QHash<QString, Texts> cache;
    const QString lang = supportedLanguage(language).isEmpty() ? QStringLiteral("en") : supportedLanguage(language);
    auto it = cache.find(lang);
    if (it == cache.end()) {
        QFile file(QStringLiteral(":/web/i18n/%1.json").arg(lang));
        file.open(QIODevice::ReadOnly);
        it = cache.insert(lang, Texts{QJsonDocument::fromJson(file.readAll()).object()});
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
    return w.time.isEmpty() ? date : fill(t["atTime"], {{QStringLiteral("date"), date}, {QStringLiteral("time"), w.time}});
}
// formatTimestamp: "23 November 2026 at 13:57", wall time as written
QString formatTimestamp(const Texts &t, const QString &iso) {
    const When w = parseWhen(iso.left(16));
    if (w.date.isEmpty())
        return iso;
    Vars vars = dateVars(t, w.date);
    vars.insert(QStringLiteral("time"), w.time);
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
double hours(const QString &time) { return time.left(2).toInt() + time.mid(3, 2).toInt() / 60.0; }

// The rule of src/task.h (effectiveDate, isOverdue), as planDate/isOverdue/
// agendaDate in app.js.
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
QString agendaDate(const Item &o, const QString &today) {
    const QString d = planDate(o);
    return !d.isEmpty() && d < today && o.status == QLatin1String("todo") ? today : d;
}
QString startDate(const Item &o) { return parseWhen(o.start).date; }

// sortTasks: dated first (by date, timed before all-day, by time), then
// undated; then priority, then title (collated in the page's language).
QList<Item> sortTasks(QList<Item> items, const Texts &t) {
    const QCollator &coll = collator(t.language());
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

QString renderTask(const Item &o, const Texts &t) {
    const When s = parseWhen(o.start);
    QString timeText, timeClass = QStringLiteral("time");
    if (s.date.isEmpty()) {
        timeText = t["timeNone"];
        timeClass += QStringLiteral(" time-none");
    } else if (!s.time.isEmpty()) {
        timeText = o.duration ? s.time + u("–") + endTime(s.time, o.duration) : s.time;
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
           + projectAttr + ">\n"
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
            groups[planDate(o)].append(o);
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
    const QCollator &coll = collator(t.language());
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

struct Bar {
    Item o;
    When s;
    double start = 0, length = 0;
    bool allDay = false;
    int track = 0;
};

QString renderTimeline(const QList<Item> &items, const QString &today, const Texts &t, const QString &active) {
    QStringList dates;
    for (const Item &o : items) {
        if (!startDate(o).isEmpty())
            dates << o.start.left(10);
        if (!parseWhen(o.deadline).date.isEmpty())
            dates << o.deadline.left(10);
    }
    dates.removeDuplicates();
    std::sort(dates.begin(), dates.end());
    if (dates.isEmpty())
        dates << today;
    QStringList days;
    for (QString d = dates.first(); d <= dates.last(); d = addDays(d, 1))
        days << d;

    QHash<QString, QList<Bar>> raw;
    for (const QString &day : days) {
        QList<Bar> bars;
        for (const Item &o : items) {
            if (startDate(o) != day)
                continue;
            Bar b{o, parseWhen(o.start)};
            if (b.s.time.isEmpty()) {
                b.allDay = true;
            } else {
                b.start = hours(b.s.time);
                b.length = o.duration ? o.duration / 60.0 : 0.5;
            }
            bars << b;
        }
        raw.insert(day, bars);
    }

    // Window from the timed tasks: earliest start to latest end, whole hours, at least 08-18.
    int from = 8, to = 18;
    bool any = false;
    double minStart = 0, maxEnd = 0;
    for (const auto &bars : std::as_const(raw))
        for (const Bar &b : bars)
            if (!b.allDay) {
                minStart = any ? std::min(minStart, b.start) : b.start;
                maxEnd = any ? std::max(maxEnd, b.start + b.length) : b.start + b.length;
                any = true;
            }
    if (any) {
        from = std::min(8, int(std::floor(minStart)));
        to = std::max(18, int(std::ceil(maxEnd)));
    }
    for (auto &bars : raw)
        for (Bar &b : bars)
            if (b.allDay) {
                b.start = from;
                b.length = to - from;
            }

    const int step = to - from > 12 ? 3 : to - from > 8 ? 2 : 1;
    QString hourLabels;
    for (int h = int(std::ceil(double(from) / step)) * step; h <= to; h += step)
        hourLabels += QStringLiteral("<span style=\"--hour:%1\">%1</span>").arg(h);

    const QCollator &coll = collator(t.language());
    QStringList dayParts;
    for (const QString &day : days) {
        QList<Bar> bars = raw.value(day);
        std::stable_sort(bars.begin(), bars.end(), [&](const Bar &a, const Bar &b) {
            if (a.start != b.start)
                return a.start < b.start;
            if (a.length != b.length)
                return a.length > b.length;
            return coll.compare(a.o.title, b.o.title) < 0;
        });
        QList<double> trackEnd;
        for (Bar &b : bars) {
            int track = -1;
            for (int i = 0; i < trackEnd.size(); ++i)
                if (trackEnd[i] <= b.start + 1e-9) {
                    track = i;
                    break;
                }
            if (track == -1) {
                trackEnd << 0;
                track = trackEnd.size() - 1;
            }
            trackEnd[track] = b.start + b.length;
            b.track = track;
        }
        const int tracks = std::max<int>(1, trackEnd.size());
        QStringList lines;
        for (const Bar &b : bars) {
            const QString title = b.o.title + u(" – ") + formatDateShort(t, day)
                                  + (b.s.time.isEmpty() ? " (" + t["allDay"] + ")"
                                                        : ' ' + b.s.time
                                                              + (b.o.duration ? u("–") + endTime(b.s.time, b.o.duration)
                                                                              : QString()));
            const QString pos = "--start:" + num(b.start) + ";--length:" + num(b.length) + ";--track:"
                                + QString::number(b.track);
            const QString bar = "        <div class=\"bar" + QString(b.allDay ? " bar-all-day" : "")
                                + QString(b.o.status == QLatin1String("done") ? " status-done" : "") + "\" style=\""
                                + pos + "\" data-id=\"" + esc(b.o.id) + "\" title=\"" + esc(title) + "\">";
            // The title goes beside the bar when it cannot fit inside it. A
            // bar's pixel width shrinks with the window (to - from); at the
            // column's minimum width (34rem) about 64 characters fit across a
            // whole-day bar. Estimate conservatively: a title behind the
            // estimate would be clipped by overflow:hidden.
            const double len = b.o.title.size();
            const bool fitsIn = len <= b.length * kCharsPerDay / (to - from);
            if (fitsIn) {
                lines << bar + "<span class=\"bar-title\">" + esc(b.o.title) + "</span></div>";
            } else {
                lines << bar + "</div>";
                lines << "        <span class=\"bar-label\" style=\"" + pos + "\" data-id=\"" + esc(b.o.id)
                             + "\" title=\"" + esc(title) + "\">" + esc(b.o.title) + "</span>";
            }
        }
        for (const Item &o : items) {
            const When f = parseWhen(o.deadline);
            if (f.date != day || f.date.isEmpty())
                continue;
            const double start = f.time.isEmpty() ? to : hours(f.time);
            const bool inWindow = !f.time.isEmpty() && start < to;
            const QString title = fill(t["deadlineTitle"], {{QStringLiteral("date"), formatDeadline(t, o.deadline)},
                                                            {QStringLiteral("title"), o.title}});
            lines << "        <div class=\"deadline-mark" + QString(inWindow ? "" : " deadline-mark-edge")
                         + "\" style=\"--start:" + num(std::min(start, double(to))) + "\" data-id=\"" + esc(o.id)
                         + "\" title=\"" + esc(title) + "\"></div>";
        }
        dayParts << "    <section class=\"gantt-day" + QString(day == today ? " today" : "") + "\" data-date=\"" + day
                        + "\">\n"
                        + "      <h3 class=\"gantt-date\"><time datetime=\"" + day + "\">" + esc(formatDateShort(t, day))
                        + "</time></h3>\n"
                        + "      <div class=\"tracks\" style=\"--tracks:" + QString::number(tracks) + "\">\n"
                        + lines.join('\n') + "\n"
                        + "      </div>\n"
                        + "    </section>";
    }
    const QString description = fill(t["timelineDescription"], {{QStringLiteral("from"), QString::number(from)},
                                                                {QStringLiteral("to"), QString::number(to)}});
    return "  <section id=\"timeline\" class=\"" + viewClass("timeline", active) + "\" aria-label=\"" + esc(t["timeline"])
           + "\">\n"
           + "    <h2>" + esc(t["timeline"]) + "</h2>\n"
           + "    <p class=\"description\">" + esc(description) + "</p>\n"
           + "    <div class=\"gantt\" style=\"--from:" + QString::number(from) + ";--to:" + QString::number(to) + "\">\n"
           + "      <div class=\"gantt-head\" aria-hidden=\"true\">\n"
           + "        <div class=\"gantt-date gantt-date-empty\"></div>\n"
           + "        <div class=\"gantt-hours\">" + hourLabels + "</div>\n"
           + "      </div>\n"
           + "      <div class=\"gantt-rows\">\n"
           + dayParts.join('\n') + "\n"
           + "      </div>\n"
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
    const Texts &t = texts(language);
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
