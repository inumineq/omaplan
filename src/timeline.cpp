#include "timeline.h"

#include <algorithm>
#include <numeric>

namespace Timeline {

namespace {

Placement place(const Item &item, const Geometry &g) {
    const double w = item.labelWidth;
    const double markEnd = item.hasMark ? item.markEnd : item.barEnd;
    // The title may run up to the mark (with a gap), or to the end of the axis.
    const double afterLimit = item.hasMark ? item.markStart - g.gap : g.total;
    Placement p;
    p.labelWidth = w;
    if (w + 2 * g.pad <= item.barEnd - item.barStart) {
        p.side = Side::Inside;
        p.labelStart = item.barStart + g.pad;
    } else if (item.barEnd + g.gap + w <= afterLimit) {
        p.side = Side::AfterBar;
        p.labelStart = item.barEnd + g.gap;
    } else if (item.barStart - g.gap - w >= 0) {
        p.side = Side::Before;
        p.labelStart = item.barStart - g.gap - w;
    } else if (item.hasMark && item.markEnd + g.gap + w <= g.total) {
        p.side = Side::AfterMark;
        p.labelStart = item.markEnd + g.gap;
    } else {
        // Shortened, on the side with the most room (the first of equals).
        struct Room {
            Side side;
            double start, width;
        };
        const Room rooms[] = {
            {Side::Inside, item.barStart + g.pad, item.barEnd - item.barStart - 2 * g.pad},
            {Side::AfterBar, item.barEnd + g.gap, afterLimit - item.barEnd - g.gap},
            {Side::AfterMark, item.markEnd + g.gap, item.hasMark ? g.total - item.markEnd - g.gap : -1},
            {Side::Before, 0, item.barStart - g.gap},
        };
        const Room *best = std::max_element(std::begin(rooms), std::end(rooms),
                                            [](const Room &a, const Room &b) { return a.width < b.width; });
        p.side = best->side;
        p.labelWidth = std::max(0.0, best->width);
        p.labelStart = best->side == Side::Before ? best->start + best->width - p.labelWidth : best->start;
        p.shortened = true;
    }
    p.start = std::min(item.barStart, p.side == Side::Before ? p.labelStart : item.barStart);
    p.end = std::max(markEnd, p.side == Side::Inside || p.side == Side::Before ? item.barEnd
                                                                              : p.labelStart + p.labelWidth);
    return p;
}

} // namespace

QList<Placement> layout(const QList<Item> &items, const Geometry &geometry) {
    QList<Placement> placements;
    for (const Item &item : items)
        placements << place(item, geometry);
    // First free lane, in order of where each task begins (the longer first).
    QList<int> order(placements.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (placements[a].start != placements[b].start)
            return placements[a].start < placements[b].start;
        return placements[a].end > placements[b].end;
    });
    QList<double> laneEnd;
    for (int i : order) {
        Placement &p = placements[i];
        int lane = 0;
        while (lane < laneEnd.size() && laneEnd[lane] + geometry.spacing > p.start + 1e-9)
            ++lane;
        if (lane == laneEnd.size())
            laneEnd << 0;
        laneEnd[lane] = p.end;
        p.lane = lane;
    }
    return placements;
}

} // namespace Timeline
