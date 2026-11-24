#pragma once

#include <QList>

// Where the timeline's titles go and which lane each task takes, so that every
// title can be read: inside its bar when it fits; else after the bar (up to
// its deadline mark); else before the bar; else after the mark; else
// shortened on the side with the most room. The first three keep a title
// next to its bar. Tasks are then packed into lanes by
// everything they occupy (bar, title, mark), so no two of them overlap.
//
// One axis in any unit: pixels in the window (GanttView.qml), rem at a week's
// minimum width on the read-only page (render.cpp, and layoutTimeline in
// web/app.js, kept in step with this).
namespace Timeline {

enum class Side { Inside, AfterBar, AfterMark, Before };

struct Item {
    double barStart = 0, barEnd = 0;   // equal when there is no bar (only a deadline)
    bool hasMark = false;              // a deadline mark after the bar
    double markStart = 0, markEnd = 0;
    double labelWidth = 0;             // the title's full width
};

struct Geometry {
    double total = 0; // the axis runs from 0 to total
    double gap = 0;     // between a bar and its own title (and its mark)
    double pad = 0;     // inside a bar, on each side of the title
    double spacing = 0; // between neighbours in a lane: wider than gap, so a title is nearest its own bar
};

struct Placement {
    int lane = 0;
    Side side = Side::Inside;
    double labelStart = 0, labelWidth = 0; // labelWidth is below the item's when shortened
    bool shortened = false;
    double start = 0, end = 0;             // everything the task occupies
};

QList<Placement> layout(const QList<Item> &items, const Geometry &geometry);

} // namespace Timeline
