#include "render.h"

#include <QCollator>
#include <QJsonArray>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>

// Kept in step with web/app.js function by function; the names in comments
// are the JS originals. User-facing text is Danish by contract (MARKUP.md).

namespace Render {

namespace {

const char *const kWeekdays[] = {"mandag", "tirsdag", "onsdag", "torsdag", "fredag", "lørdag", "søndag"};
const char *const kWeekdaysShort[] = {"man", "tir", "ons", "tor", "fre", "lør", "søn"};
const char *const kMonths[] = {"januar", "februar", "marts", "april", "maj", "juni", "juli",
                               "august", "september", "oktober", "november", "december"};
const double kCharsPerDay = 64; // TEGN_PR_DAG: characters across a whole-day bar at the column's minimum width

struct Item {
    QString id, titel, noter, projekt, status, start, frist;
    int prioritet = 3;
    int varighed = 0;
    QStringList gaet;
};

// parseStart: "YYYY-MM-DD" or "YYYY-MM-DDTHH:MM" -> date and time; anything
// else counts as unscheduled.
struct When {
    QString dato, tid;
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
QString weekday(const QString &iso) { return u(kWeekdays[dateOf(iso).dayOfWeek() - 1]); }
QString weekdayShort(const QString &iso) { return u(kWeekdaysShort[dateOf(iso).dayOfWeek() - 1]); }
QString month(const QString &iso) { return u(kMonths[iso.mid(5, 2).toInt() - 1]); }
QString dayNr(const QString &iso) { return QString::number(iso.mid(8, 2).toInt()); }
QString capital(const QString &s) { return s.left(1).toUpper() + s.mid(1); }

// formaterDatoLang: "Mandag 23. november 2026"
QString dateLong(const QString &iso) {
    return capital(weekday(iso)) + ' ' + dayNr(iso) + ". " + month(iso) + ' ' + iso.left(4);
}
// formaterDatoKort: "Man 23/11"
QString dateShort(const QString &iso) { return capital(weekdayShort(iso)) + ' ' + dayNr(iso) + '/' + iso.mid(5, 2); }
// formaterFrist: "fredag 27. november 2026 kl. 12:00"
QString deadlineText(const QString &frist) {
    const When w = parseWhen(frist);
    return weekday(w.dato) + ' ' + dayNr(w.dato) + ". " + month(w.dato) + ' ' + w.dato.left(4)
           + (w.tid.isEmpty() ? QString() : " kl. " + w.tid);
}
// formaterTidsstempel: "23. november 2026 kl. 13:57", wall time as written
QString timestamp(const QString &iso) {
    const When w = parseWhen(iso.left(16));
    if (w.dato.isEmpty())
        return esc(iso);
    return dayNr(w.dato) + ". " + month(w.dato) + ' ' + w.dato.left(4) + " kl. " + w.tid;
}
// formaterVarighed: "45 min", "2 t.", "2 t. 30 min"
QString durationText(int min) {
    if (min < 60)
        return QString::number(min) + " min";
    const int t = min / 60, m = min % 60;
    return m ? QStringLiteral("%1 t. %2 min").arg(t).arg(m) : QStringLiteral("%1 t.").arg(t);
}
QString endTime(const QString &tid, int minutes) {
    const int end = tid.left(2).toInt() * 60 + tid.mid(3, 2).toInt() + minutes;
    return QStringLiteral("%1:%2").arg((end / 60) % 24, 2, 10, QLatin1Char('0')).arg(end % 60, 2, 10, QLatin1Char('0'));
}
QString count(int n) { return n == 1 ? QStringLiteral("1 opgave") : QString::number(n) + " opgaver"; }
// num: at most two decimals, no trailing zeros (9, 1.5, 0.25)
QString num(double x) {
    QString s = QString::number(x, 'f', 2);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
        s.chop(1);
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}
double hours(const QString &tid) { return tid.left(2).toInt() + tid.mid(3, 2).toInt() / 60.0; }

const QCollator &danish() {
    static const QCollator collator(QLocale(QLocale::Danish, QLocale::Denmark));
    return collator;
}

// tidligsteDato
QString earliestDate(const Item &o) {
    QStringList dates;
    if (!parseWhen(o.start).dato.isEmpty())
        dates << o.start.left(10);
    if (!parseWhen(o.frist).dato.isEmpty())
        dates << o.frist.left(10);
    std::sort(dates.begin(), dates.end());
    return dates.isEmpty() ? QString() : dates.first();
}
// erForfalden
bool overdue(const Item &o, const QString &today) {
    if (o.status != QLatin1String("todo"))
        return false;
    const QString d = earliestDate(o);
    return !d.isEmpty() && d < today;
}
QString startDate(const Item &o) { return parseWhen(o.start).dato; }

// sorterOpgaver: dated first (by date, timed before all-day, by time), then
// undated; then priority, then title (Danish collation).
QList<Item> sorted(QList<Item> items) {
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const When sa = parseWhen(a.start), sb = parseWhen(b.start);
        if (sa.dato.isEmpty() != sb.dato.isEmpty())
            return !sa.dato.isEmpty();
        if (!sa.dato.isEmpty() && sa.dato != sb.dato)
            return sa.dato < sb.dato;
        if (!sa.dato.isEmpty() && sa.tid.isEmpty() != sb.tid.isEmpty())
            return !sa.tid.isEmpty();
        if (!sa.tid.isEmpty() && !sb.tid.isEmpty() && sa.tid != sb.tid)
            return sa.tid < sb.tid;
        if (a.prioritet != b.prioritet)
            return a.prioritet < b.prioritet;
        return danish().compare(a.titel, b.titel) < 0;
    });
    return items;
}

QString guessName(const QString &g) { return g == QLatin1String("varighed_min") ? QStringLiteral("varighed") : g; }

// renderOpgave
QString renderTask(const Item &o) {
    const When s = parseWhen(o.start);
    QString tidTekst, tidKlasse = QStringLiteral("tid");
    if (s.dato.isEmpty()) {
        tidTekst = QStringLiteral("ingen dato");
        tidKlasse += QStringLiteral(" tid-ingen");
    } else if (!s.tid.isEmpty()) {
        tidTekst = o.varighed ? s.tid + u("–") + endTime(s.tid, o.varighed) : s.tid;
    } else {
        tidTekst = QStringLiteral("heldag");
        tidKlasse += QStringLiteral(" tid-heldag");
    }
    QString meta;
    if (!o.projekt.isEmpty())
        meta += "<span class=\"projekt-tag\">" + esc(o.projekt) + "</span>";
    if (!parseWhen(o.frist).dato.isEmpty())
        meta += "<span class=\"frist\">frist " + esc(deadlineText(o.frist)) + "</span>";
    if (o.varighed)
        meta += "<span class=\"varighed\">" + esc(durationText(o.varighed)) + "</span>";
    if (!o.gaet.isEmpty()) {
        QStringList names;
        for (const QString &g : o.gaet)
            names << guessName(g);
        meta += u("<span class=\"gaet\">gæt: ") + esc(names.join(", ")) + "</span>";
    }
    const QString p = QString::number(o.prioritet);
    meta += "<span class=\"badge badge-p" + p + "\">P" + p + "</span>";
    if (o.status == QLatin1String("done"))
        meta += u("<span class=\"badge badge-done\">færdig</span>");
    const QString projektAttr = o.projekt.isEmpty() ? QString() : " data-projekt=\"" + esc(o.projekt) + "\"";
    const QString noter = o.noter.isEmpty()
                              ? QString()
                              : "\n    <details class=\"noter\"><summary>Noter</summary><p>" + esc(o.noter) + "</p></details>";
    return "<li class=\"opgave status-" + esc(o.status) + "\" data-id=\"" + esc(o.id) + "\" data-prioritet=\"" + p + "\""
           + projektAttr + ">\n"
           + "    <p class=\"opgave-rad\"><span class=\"" + tidKlasse + "\">" + esc(tidTekst)
           + "</span><span class=\"titel\">" + esc(o.titel) + "</span></p>\n"
           + "    <p class=\"opgave-meta\">" + meta + "</p>" + noter + "\n"
           + "  </li>";
}

// renderOpgaveListe
QString renderTaskList(const QList<Item> &items) {
    QStringList lines;
    for (const Item &o : items)
        lines << "    " + renderTask(o).replace('\n', QStringLiteral("\n    "));
    return "<ul class=\"opgaver\">\n" + lines.join('\n') + "\n  </ul>";
}

// renderDagSektion
QString renderDay(const QString &dato, const QList<Item> &items, const QString &extraClass, const QString &badgeClass,
                  const QString &badgeText) {
    const QString badge = badgeClass.isEmpty() ? QString()
                                               : "<span class=\"badge " + badgeClass + "\">" + badgeText + "</span>";
    const QString body = items.isEmpty() ? QStringLiteral("    <p class=\"tom\">Ingen opgaver.</p>") : renderTaskList(items);
    return "  <section class=\"dag" + (extraClass.isEmpty() ? QString() : ' ' + extraClass) + "\" data-dato=\"" + dato
           + "\">\n"
           + "    <h3><time datetime=\"" + dato + "\">" + esc(dateLong(dato)) + "</time>" + badge + "</h3>\n"
           + body + "\n"
           + "  </section>";
}

QString viewClass(const QString &id, const QString &active) {
    return id == active ? QStringLiteral("view aktiv") : QStringLiteral("view");
}

// renderOverblik
QString renderOverview(const QList<Item> &items, const QString &today, const QString &active) {
    QMap<QString, QList<Item>> groups;
    for (const Item &o : items)
        if (overdue(o, today))
            groups[earliestDate(o)].append(o);
    QStringList parts;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        parts << renderDay(it.key(), sorted(it.value()), QStringLiteral("forfalden"), QStringLiteral("badge-forfalden"),
                           QStringLiteral("forfalden"));
    for (int k = 0; k < 8; ++k) {
        const QString dato = addDays(today, k);
        QList<Item> onDay;
        for (const Item &o : items)
            if (startDate(o) == dato) // undated tasks are not in the overview
                onDay << o;
        parts << renderDay(dato, sorted(onDay), k == 0 ? QStringLiteral("i-dag") : QString(),
                           k == 0 ? QStringLiteral("badge-i-dag") : QString(), QStringLiteral("i dag"));
    }
    return "  <section id=\"overblik\" class=\"" + viewClass("overblik", active) + "\" aria-label=\"Overblik\">\n"
           + "    <h2>Overblik</h2>\n"
           + u("    <p class=\"beskrivelse\">I dag og de næste 7 dage – forfaldne opgaver øverst.</p>\n")
           + parts.join('\n') + "\n"
           + "  </section>";
}

QString renderGroup(const QString &dataGroup, const QString &heading, const QList<Item> &items) {
    return "  <section class=\"gruppe\" data-gruppe=\"" + dataGroup + "\">\n"
           + "    <h3>" + heading + "<span class=\"antal\">" + count(items.size()) + "</span></h3>\n"
           + renderTaskList(items) + "\n"
           + "  </section>";
}

// renderListeDato
QString renderListByDate(const QList<Item> &items, const QString &active) {
    QMap<QString, QList<Item>> groups;
    QList<Item> undated;
    for (const Item &o : items) {
        const QString d = startDate(o);
        if (d.isEmpty())
            undated << o; // "Uden dato" comes last
        else
            groups[d].append(o);
    }
    QStringList parts;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        parts << renderGroup("dato:" + it.key(),
                             "<time datetime=\"" + it.key() + "\">" + esc(dateLong(it.key())) + "</time>",
                             sorted(it.value()));
    if (!undated.isEmpty())
        parts << renderGroup(QStringLiteral("uden-dato"), QStringLiteral("<span class=\"gruppe-titel\">Uden dato</span>"),
                             sorted(undated));
    return "  <section id=\"liste\" class=\"" + viewClass("liste", active) + "\" aria-label=\"Liste efter dato\">\n"
           + "    <h2>Liste</h2>\n"
           + u("    <p class=\"beskrivelse\">Alle opgaver grupperet efter dato. <a href=\"#liste-projekt\">Gruppér efter projekt</a></p>\n")
           + parts.join('\n') + "\n"
           + "  </section>";
}

// renderListeProjekt
QString renderListByProject(const QList<Item> &items, const QString &active) {
    const QString none = QStringLiteral("Ingen projekt");
    QStringList names;
    QHash<QString, QList<Item>> groups;
    for (const Item &o : items) {
        const QString p = o.projekt.isEmpty() ? none : o.projekt;
        if (!groups.contains(p))
            names << p;
        groups[p].append(o);
    }
    std::stable_sort(names.begin(), names.end(), [&](const QString &a, const QString &b) {
        if (a == none || b == none)
            return b == none && a != none;
        return danish().compare(a, b) < 0;
    });
    QStringList parts;
    for (const QString &name : names)
        parts << renderGroup("projekt:" + esc(name), "<span class=\"gruppe-titel\">" + esc(name) + "</span>",
                             sorted(groups.value(name)));
    return "  <section id=\"liste-projekt\" class=\"" + viewClass("liste-projekt", active)
           + "\" aria-label=\"Liste efter projekt\">\n"
           + "    <h2>Liste efter projekt</h2>\n"
           + u("    <p class=\"beskrivelse\">Alle opgaver grupperet efter projekt. <a href=\"#liste\">Gruppér efter dato</a></p>\n")
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

// renderTidslinje
QString renderTimeline(const QList<Item> &items, const QString &today, const QString &active) {
    QStringList dates;
    for (const Item &o : items) {
        if (!startDate(o).isEmpty())
            dates << o.start.left(10);
        if (!parseWhen(o.frist).dato.isEmpty())
            dates << o.frist.left(10);
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
            if (b.s.tid.isEmpty()) {
                b.allDay = true;
            } else {
                b.start = hours(b.s.tid);
                b.length = o.varighed ? o.varighed / 60.0 : 0.5;
            }
            bars << b;
        }
        raw.insert(day, bars);
    }

    // Window from the timed tasks: earliest start to latest end, whole hours, at least 08-18.
    int vs = 8, vu = 18;
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
        vs = std::min(8, int(std::floor(minStart)));
        vu = std::max(18, int(std::ceil(maxEnd)));
    }
    for (auto &bars : raw)
        for (Bar &b : bars)
            if (b.allDay) {
                b.start = vs;
                b.length = vu - vs;
            }

    const int step = vu - vs > 12 ? 3 : vu - vs > 8 ? 2 : 1;
    QString hourLabels;
    for (int t = int(std::ceil(double(vs) / step)) * step; t <= vu; t += step)
        hourLabels += QStringLiteral("<span style=\"--t:%1\">%1</span>").arg(t);

    QStringList dayParts;
    for (const QString &day : days) {
        QList<Bar> bars = raw.value(day);
        std::stable_sort(bars.begin(), bars.end(), [](const Bar &a, const Bar &b) {
            if (a.start != b.start)
                return a.start < b.start;
            if (a.length != b.length)
                return a.length > b.length;
            return danish().compare(a.o.titel, b.o.titel) < 0;
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
            const QString title = b.o.titel + u(" – ") + dateShort(day)
                                  + (b.s.tid.isEmpty() ? QStringLiteral(" (heldag)")
                                                       : ' ' + b.s.tid
                                                             + (b.o.varighed ? u("–") + endTime(b.s.tid, b.o.varighed)
                                                                             : QString()));
            const QString pos = "--start:" + num(b.start) + ";--laengde:" + num(b.length) + ";--spor-index:"
                                + QString::number(b.track);
            const QString bar = "        <div class=\"stang" + QString(b.allDay ? " stang-heldag" : "")
                                + QString(b.o.status == QLatin1String("done") ? " status-done" : "") + "\" style=\""
                                + pos + "\" data-id=\"" + esc(b.o.id) + "\" title=\"" + esc(title) + "\">";
            // The title goes beside the bar when it cannot fit inside it. A
            // bar's pixel width shrinks with the window (vu - vs); at the
            // column's minimum width (34rem) about 64 characters fit across a
            // whole-day bar. Estimate conservatively: a title behind the
            // estimate would be clipped by overflow:hidden.
            const double len = b.o.titel.size();
            const bool fitsIn = len <= b.length * kCharsPerDay / (vu - vs);
            if (!fitsIn) {
                lines << bar + "</div>";
                lines << "        <span class=\"stang-label\" style=\"" + pos + "\" data-id=\"" + esc(b.o.id)
                             + "\" title=\"" + esc(title) + "\">" + esc(b.o.titel) + "</span>";
            } else {
                lines << bar + "<span class=\"stang-titel\">" + esc(b.o.titel) + "</span></div>";
            }
        }
        for (const Item &o : items) {
            const When f = parseWhen(o.frist);
            if (f.dato != day || f.dato.isEmpty())
                continue;
            const double start = f.tid.isEmpty() ? vu : hours(f.tid);
            const bool inWindow = !f.tid.isEmpty() && start < vu;
            lines << "        <div class=\"frist-maerke" + QString(inWindow ? "" : " frist-maerke-heldag")
                         + "\" style=\"--start:" + num(std::min(start, double(vu))) + "\" data-id=\"" + esc(o.id)
                         + "\" title=\"frist: " + esc(deadlineText(o.frist)) + u(" – ") + esc(o.titel) + "\"></div>";
        }
        dayParts << "    <section class=\"gantt-dag" + QString(day == today ? " i-dag" : "") + "\" data-dato=\"" + day
                        + "\">\n"
                        + "      <h3 class=\"gantt-dato\"><time datetime=\"" + day + "\">" + esc(dateShort(day))
                        + "</time></h3>\n"
                        + "      <div class=\"spor\" style=\"--spor:" + QString::number(tracks) + "\">\n"
                        + lines.join('\n') + "\n"
                        + "      </div>\n"
                        + "    </section>";
    }
    return "  <section id=\"tidslinje\" class=\"" + viewClass("tidslinje", active) + "\" aria-label=\"Tidslinje\">\n"
           + "    <h2>Tidslinje</h2>\n"
           + u("    <p class=\"beskrivelse\">Gantt over alle datoer med opgaver eller frister – vindue ")
           + QString::number(vs) + u("–") + QString::number(vu) + ", overlap ligger i hver sit spor.</p>\n"
           + "    <div class=\"gantt\" style=\"--vs:" + QString::number(vs) + ";--vu:" + QString::number(vu) + "\">\n"
           + "      <div class=\"gantt-hoved\" aria-hidden=\"true\">\n"
           + "        <div class=\"gantt-dato gantt-dato-tom\"></div>\n"
           + "        <div class=\"gantt-timer\">" + hourLabels + "</div>\n"
           + "      </div>\n"
           + "      <div class=\"gantt-raekker\">\n"
           + dayParts.join('\n') + "\n"
           + "      </div>\n"
           + "    </div>\n"
           + "  </section>";
}

QList<Item> itemsOf(const QJsonObject &plan) {
    QList<Item> items;
    for (const QJsonValue &value : plan.value("opgaver").toArray()) {
        const QJsonObject json = value.toObject();
        Item o;
        o.id = json.value("id").toString();
        o.titel = json.value("titel").toString();
        o.noter = json.value("noter").toString();
        o.projekt = json.value("projekt").toString();
        o.prioritet = json.value("prioritet").toInt(3);
        o.status = json.value("status").toString(QStringLiteral("todo"));
        o.start = json.value("start").toString();
        o.frist = json.value("frist").toString();
        o.varighed = std::max(0, json.value("varighed_min").toInt(0));
        for (const QJsonValue &g : json.value("gaet").toArray())
            o.gaet << g.toString();
        items << o;
    }
    return items;
}

QString frame(const QString &lang, const QString &htmlStyle, const QString &meta, const QString &mainAttrs,
              const QString &body) {
    const QString style = htmlStyle.isEmpty() ? QString() : " style=\"" + esc(htmlStyle) + "\"";
    return "<!doctype html>\n"
           "<html lang=\"" + esc(lang) + "\"" + style + ">\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           + u("<title>Omaplan – læseudgave</title>\n")
           + "<link rel=\"stylesheet\" href=\"style.css\">\n"
             "</head>\n"
             "<body>\n"
             "<header class=\"top\">\n"
             "  <h1>Omaplan</h1>\n"
             "  <nav class=\"visninger\" aria-label=\"Visninger\">\n"
             "    <a href=\"#overblik\">Overblik</a>\n"
             "    <a href=\"#liste\">Liste</a>\n"
             "    <a href=\"#tidslinje\">Tidslinje</a>\n"
             "  </nav>\n"
             "  <p class=\"meta\">" + meta + "</p>\n"
           + "</header>\n"
             "<main id=\"plan\" class=\"plan\"" + mainAttrs + ">\n"
           + body + "\n"
           + "</main>\n"
             "<footer class=\"sidefod\">\n"
           + u("  <p>Læseudgave (kun læsning) · <a href=\"plan.json\">plan.json</a></p>\n")
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

QString page(const QJsonObject &plan, const QDate &today, const QString &htmlStyle, const QString &activeView) {
    const QList<Item> items = itemsOf(plan);
    const QString iDag = today.toString(Qt::ISODate);
    const QString active = isView(activeView) ? activeView : QString();
    const QString exported = plan.value("eksporteret").toString();
    // renderMeta
    const QString meta = "Eksporteret <time datetime=\"" + esc(exported) + "\">" + esc(timestamp(exported)) + "</time>"
                         + u(" <span class=\"prik\">·</span> <span class=\"tidszone\">")
                         + esc(plan.value("tidszone").toString()) + "</span>"
                         + u(" <span class=\"prik\">·</span> <span class=\"sprog\">")
                         + esc(plan.value("sprog").toString()) + "</span>";
    // renderMain
    const QString body = QStringList{renderOverview(items, iDag, active), renderListByDate(items, active),
                                     renderListByProject(items, active), renderTimeline(items, iDag, active)}
                             .join('\n');
    const QString lang = plan.value("sprog").toString();
    return frame(lang.isEmpty() ? QStringLiteral("da") : lang, htmlStyle, meta, " data-idag=\"" + iDag + "\"", body);
}

QString messagePage(const QString &heading, const QString &text, const QString &htmlStyle) {
    const QString body = "  <section id=\"overblik\" class=\"view\" aria-label=\"Overblik\">\n"
                         "    <h2>" + esc(heading) + "</h2>\n"
                         "    <p class=\"beskrivelse\">" + esc(text) + "</p>\n"
                         "  </section>";
    return frame(QStringLiteral("da"), htmlStyle, u("Intet øjebliksbillede endnu"), QString(), body);
}

} // namespace Render
