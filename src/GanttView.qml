import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The models are registered from C++ (registerQmlTypes), so qmllint cannot see them.
import Omaplan as Plan // qmllint disable import

// Timeline: a Gantt chart. Each task is a bar from its start to its end
// (taskSpan in task.h: a duration, or a deadline on a later day, can run over
// several days); a deadline after the bar is a mark at the end of a thin line,
// and a task with only a deadline is just the mark. Every title can be read:
// inside its bar when it fits, otherwise beside it, and the tasks are packed
// into lanes by bar, title and mark together, so nothing overlaps
// (Timeline::layout). Drag a bar to move it, its right edge to change where
// it ends. h/l pages, t goes to today, + / - zooms.
Item {
    id: gantt

    readonly property bool canPage: true
    readonly property var zoomLevels: [1, 3, 7, 14]
    property int zoomIndex: 2
    readonly property int dayCount: zoomLevels[zoomIndex]
    readonly property var today: Fmt.today
    property var anchorDate: Fmt.startOfDay(new Date())
    readonly property var rangeStart: dayCount >= 7 ? Fmt.weekStartOf(anchorDate) : anchorDate
    readonly property var rangeEnd: Fmt.addDays(rangeStart, dayCount) // midnight after the last day

    function page(step) { anchorDate = Fmt.addDays(anchorDate, step * Math.min(dayCount, 7)) }
    function goToday() { anchorDate = Fmt.startOfDay(new Date()) }
    function zoom(step) { zoomIndex = Math.max(0, Math.min(zoomLevels.length - 1, zoomIndex + step)) }
    function taskIds() { return bars.map(b => b.task.taskId) }
    // A task's project colour (a palette slot from the settings), "" without one.
    function projectColorOf(t) { void settings.projectColors; return theme.palette[settings.projectColor(t.project)] || "" }

    Plan.GanttModel { // qmllint disable import
        id: rangeModel
        sourceModel: planModel
        from: gantt.rangeStart
        to: Fmt.addDays(gantt.rangeStart, gantt.dayCount - 1)
    }
    readonly property var rows: { void rangeModel.revision; return rangeModel.rows() }

    // The hours each day shows: 07-19 by default, widened to fit the tasks
    // with a time that start in view (and their end, when it is the same day).
    function timedStarts() {
        return rows.filter(t => Fmt.valid(t.span.start) && !t.span.allDay && t.span.start >= rangeStart && t.span.start < rangeEnd)
    }
    readonly property int firstHour: {
        let h = 7
        for (const t of timedStarts()) h = Math.min(h, t.span.start.getHours())
        return h
    }
    readonly property int lastHour: {
        let h = 19
        for (const t of timedStarts()) {
            const s = t.span.start, e = t.span.end
            const end = Fmt.sameDay(s, e) ? e.getHours() * 60 + e.getMinutes() : s.getHours() * 60 + s.getMinutes() + 30
            h = Math.max(h, Math.min(24, Math.ceil(end / 60)))
        }
        return h
    }
    readonly property int spanMinutes: (lastHour - firstHour) * 60
    readonly property real dayWidth: (area.width - 2) / dayCount
    readonly property real totalWidth: dayCount * dayWidth

    readonly property int fontSize: Math.round(12 * theme.textScale)
    readonly property int barHeight: fontSize + 14
    readonly property int laneHeight: barHeight + 10
    readonly property int inset: 2      // whole-day bars stay off the day lines
    readonly property int gap: 8        // bar to its own title
    readonly property int pad: 8        // inside a bar, on each side of the title
    readonly property int spacing: 22   // between neighbours in a lane, so a title sits nearest its own bar
    readonly property int markSize: 10  // the deadline diamond; 14 across

    FontMetrics {
        id: metrics
        font.family: theme.fontFamily
        font.pixelSize: Math.round(12 * theme.textScale) // as the titles
    }

    // x of a moment inside the visible days.
    function xOf(dayIndex, minutes) {
        return dayIndex * dayWidth + Math.max(0, Math.min(1, (minutes - firstHour * 60) / spanMinutes)) * dayWidth
    }
    // x of a moment, clamped to the view; `edge`: a whole-day boundary (midnight).
    function xAt(d, edge) {
        const day = Fmt.daysBetween(rangeStart, d)
        if (day < 0)
            return 0
        if (day >= dayCount)
            return totalWidth
        return edge ? day * dayWidth : xOf(day, d.getHours() * 60 + d.getMinutes())
    }
    function labelOf(t) {
        return (Fmt.valid(t.span.start) && !t.span.allDay ? Fmt.time(t.span.start) + "  " : "") + t.title
    }
    // What the tooltip says: the title and when.
    function whenOf(t) {
        const s = t.span
        const at = (d, allDay) => Fmt.dateShort(d) + (allDay ? "" : " " + Fmt.time(d))
        let text = ""
        if (Fmt.valid(s.start)) {
            const last = s.allDay ? Fmt.addDays(s.end, -1) : s.end
            text = at(s.start, s.allDay) + (s.allDay && Fmt.sameDay(s.start, last) ? "" : " – " + at(last, s.allDay))
        }
        if (Fmt.valid(s.mark))
            text += (text ? "  " : "") + "◆ " + at(s.mark, s.markAllDay)
        return t.title + "\n" + text
    }

    // Each task: where its bar, mark and title go, and its lane.
    readonly property var bars: {
        const total = totalWidth
        void theme.fontFamily
        void fontSize
        if (!(total > 0))
            return []
        const out = [], items = []
        for (const t of rows) {
            const s = t.span
            const b = { task: t, text: labelOf(t), hasBar: false, hasMark: false, cutStart: false, cutEnd: false }
            if (Fmt.valid(s.start) && s.start < rangeEnd && s.end > rangeStart) {
                b.hasBar = true
                b.cutStart = s.start < rangeStart
                b.cutEnd = s.end > rangeEnd
                b.barStart = b.cutStart ? 0 : xAt(s.start, s.allDay) + (s.allDay ? inset : 0)
                b.barEnd = b.cutEnd ? total : xAt(s.end, s.allDay) - (s.allDay ? inset : 0)
                b.barEnd = Math.max(b.barEnd, Math.min(total, b.barStart + 6))
            }
            if (Fmt.valid(s.mark) && s.mark >= rangeStart && s.mark < rangeEnd) {
                b.hasMark = true
                let c = s.markAllDay ? xAt(Fmt.addDays(s.mark, 1), true) - 8 : xAt(s.mark, false)
                c = Math.max(7, Math.min(total - 7, c))
                if (b.hasBar)
                    c = Math.max(c, b.barEnd + 7)
                b.markX = c
                b.markStart = c - 7
                b.markEnd = c + 7
            }
            if (!b.hasBar && !b.hasMark)
                continue
            if (!b.hasBar)
                b.barStart = b.barEnd = b.markStart
            out.push(b)
            items.push({ barStart: b.barStart, barEnd: b.barEnd, hasMark: b.hasMark, markStart: b.markStart || 0,
                         markEnd: b.markEnd || 0, labelWidth: Math.ceil(metrics.advanceWidth(b.text)) + 2 })
        }
        const placed = rangeModel.pack(items, total, gap, pad, spacing)
        for (let i = 0; i < out.length; ++i)
            Object.assign(out[i], placed[i])
        return out
    }
    readonly property int laneCount: bars.reduce((n, b) => Math.max(n, b.lane + 1), 0)

    // Where a dragged bar starts: quarter hours, or whole days for all-day tasks.
    function startAt(x, allDay) {
        if (allDay) {
            const day = Math.max(0, Math.min(dayCount - 1, Math.round((x - inset) / dayWidth)))
            return Fmt.addDays(rangeStart, day)
        }
        const day = Math.max(0, Math.min(dayCount - 1, Math.floor((x + 1) / dayWidth)))
        const date = Fmt.addDays(rangeStart, day)
        const within = (x - day * dayWidth) / dayWidth
        const minutes = Math.max(0, Math.min(24 * 60 - 15, Math.round((firstHour * 60 + within * spanMinutes) / 15) * 15))
        date.setHours(Math.floor(minutes / 60), minutes % 60)
        return date
    }
    // Where a resized bar ends, in quarter hours: the right edge of a day is
    // the end of its hours, not the start of the next day's.
    function endAt(x) {
        const day = Math.max(0, Math.min(dayCount - 1, Math.ceil(x / dayWidth) - 1))
        const date = Fmt.addDays(rangeStart, day)
        const within = Math.max(0, Math.min(1, (x - day * dayWidth) / dayWidth))
        const minutes = Math.round((firstHour * 60 + within * spanMinutes) / 15) * 15
        date.setHours(0, minutes)
        return date
    }
    function dateText(d, hasTime) { return Qt.formatDateTime(d, hasTime ? "yyyy-MM-dd HH:mm" : "yyyy-MM-dd") }

    // A bar dragged dx pixels. A task that runs to its deadline takes the
    // deadline along; a separate deadline stays where it is. Only a deadline
    // in view (no bar): it moves by whole days.
    function moveBy(b, dx) {
        const t = b.task
        if (!b.hasBar) {
            const days = Math.round(dx / dayWidth)
            if (days !== 0)
                win.check(actions.guiEdit(t.taskId, { "deadline": dateText(Fmt.addDays(t.deadline, days), t.deadlineHasTime) }))
            return
        }
        const when = b.cutStart ? Fmt.addDays(t.start, Math.round(dx / dayWidth)) : startAt(b.barStart + dx, t.span.allDay)
        if (when.getTime() === t.start.getTime())
            return
        const fields = { "start": dateText(when, t.startHasTime) }
        if (t.span.toDeadline) {
            const deadline = t.startHasTime && t.deadlineHasTime ? new Date(t.deadline.getTime() + (when - t.start))
                                                                 : Fmt.addDays(t.deadline, Fmt.daysBetween(t.start, when))
            fields["deadline"] = dateText(deadline, t.deadlineHasTime)
        }
        win.check(actions.guiEdit(t.taskId, fields))
    }
    // A bar's right edge dragged to x: a task running to its deadline gets a
    // new deadline, any other a new duration (whole days without a time).
    function resizeTo(b, x) {
        const t = b.task, s = t.span
        if (s.allDay) {
            const days = Math.max(1, Math.round((x + inset) / dayWidth) - Fmt.daysBetween(rangeStart, s.start))
            if (days === Fmt.daysBetween(s.start, s.end))
                return
            if (s.toDeadline) {
                const deadline = Fmt.addDays(s.start, days - 1)
                if (t.deadlineHasTime)
                    deadline.setHours(t.deadline.getHours(), t.deadline.getMinutes())
                win.check(actions.guiEdit(t.taskId, { "deadline": dateText(deadline, t.deadlineHasTime) }))
            } else {
                const minutes = days > 1 ? days * 24 * 60 : t.durationMinutes < 24 * 60 ? t.durationMinutes : 0
                win.check(actions.guiEdit(t.taskId, { "duration": String(minutes) }))
            }
            return
        }
        const end = endAt(x)
        if (s.toDeadline) {
            if (end.getTime() !== s.end.getTime() && end > t.start)
                win.check(actions.guiEdit(t.taskId, { "deadline": dateText(end, true) }))
            return
        }
        const minutes = Math.max(15, Math.round((end - t.start) / 60000 / 15) * 15)
        if (minutes !== t.durationMinutes)
            win.check(actions.guiEdit(t.taskId, { "duration": String(minutes) }))
    }

    property int nowMinutes: new Date().getHours() * 60 + new Date().getMinutes()
    Timer {
        interval: 60000
        running: true
        repeat: true
        onTriggered: gantt.nowMinutes = new Date().getHours() * 60 + new Date().getMinutes()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            HeaderButton { text: "‹"; toolTip: qsTr("Previous"); keyHint: "h"; onClicked: gantt.page(-1) }
            HeaderButton { text: qsTr("Today"); keyHint: "t"; onClicked: gantt.goToday() }
            HeaderButton { text: "›"; toolTip: qsTr("Next"); keyHint: "l"; onClicked: gantt.page(1) }
            Text {
                font.family: theme.fontFamily
                Layout.leftMargin: 10
                text: {
                    if (gantt.dayCount === 7)
                        return Fmt.weekTitle(gantt.rangeStart)
                    if (gantt.dayCount === 1)
                        return Fmt.capitalize(Fmt.dayLong(gantt.rangeStart))
                    return Fmt.dateShort(gantt.rangeStart) + " – " + Fmt.dateShort(Fmt.addDays(gantt.rangeStart, gantt.dayCount - 1))
                }
                color: theme.foreground
                font.pixelSize: Math.round(17 * theme.textScale)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Segmented {
                label: qsTr("Zoom")
                keyHint: "+ / -"
                options: [["1", qsTr("Day")], ["3", qsTr("3 days")], ["7", qsTr("Week")], ["14", qsTr("2 weeks")]]
                value: String(gantt.dayCount)
                onPicked: v => gantt.zoomIndex = gantt.zoomLevels.indexOf(parseInt(v))
            }
        }

        // Day headers with hour ticks.
        Item {
            Layout.fillWidth: true
            implicitHeight: 40
            Repeater {
                model: gantt.dayCount
                delegate: Item {
                    required property int index
                    readonly property var date: Fmt.addDays(gantt.rangeStart, index)
                    readonly property bool isToday: Fmt.sameDay(date, gantt.today)
                    x: index * gantt.dayWidth
                    width: gantt.dayWidth
                    height: parent.height
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: Math.min(4, theme.radius)
                        color: parent.isToday ? Qt.alpha(theme.accent, 0.16) : theme.surface
                    }
                    Text {
                        font.family: theme.fontFamily
                        x: 8
                        y: 4
                        width: parent.width - 12
                        elide: Text.ElideRight
                        text: gantt.dayCount > 7 ? Fmt.dayShort(parent.date) : Fmt.capitalize(Fmt.dayLong(parent.date))
                        color: parent.isToday ? theme.accentText : theme.foreground
                        font.pixelSize: Math.round(12 * theme.textScale)
                        font.bold: parent.isToday
                    }
                    // Hour ticks where there is room for them.
                    Repeater {
                        model: gantt.dayWidth / (gantt.lastHour - gantt.firstHour) > 26 ? gantt.lastHour - gantt.firstHour : 0
                        delegate: Text {
                            font.family: theme.fontFamily
                            required property int index
                            visible: index > 0 && (gantt.dayWidth / (gantt.lastHour - gantt.firstHour) > 44 || index % 2 === 0)
                            x: index / (gantt.lastHour - gantt.firstHour) * gantt.dayWidth - width / 2
                            y: 22
                            text: Fmt.hour(gantt.firstHour + index, true)
                            color: theme.muted
                            font.pixelSize: Math.round(10 * theme.textScale)
                        }
                    }
                }
            }
        }

        Flickable {
            id: area
            objectName: "timelineArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: Math.max(height, gantt.laneCount * gantt.laneHeight + 12)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Item {
                id: lanesArea
                objectName: "timelineLanes"
                width: area.width
                height: area.contentHeight

                // Day separators and today's tint.
                Repeater {
                    model: gantt.dayCount
                    delegate: Rectangle {
                        required property int index
                        x: index * gantt.dayWidth
                        width: gantt.dayWidth
                        height: lanesArea.height
                        color: Fmt.sameDay(Fmt.addDays(gantt.rangeStart, index), gantt.today) ? Qt.alpha(theme.accent, 0.05) : "transparent"
                        Rectangle { width: 1; height: parent.height; color: theme.border }
                    }
                }

                // Now: under the bars and titles, so it never crosses text.
                Rectangle {
                    readonly property int day: Fmt.daysBetween(gantt.rangeStart, gantt.today)
                    visible: day >= 0 && day < gantt.dayCount && gantt.nowMinutes >= gantt.firstHour * 60
                             && gantt.nowMinutes <= gantt.lastHour * 60
                    x: gantt.xOf(day, gantt.nowMinutes) - 1
                    width: 2
                    height: lanesArea.height
                    color: theme.danger
                }

                // One item per task, as wide as all it occupies (bar, line,
                // mark and title), so lanes never overlap and a press anywhere
                // on it picks the task.
                Repeater {
                    model: gantt.bars
                    delegate: Item {
                        id: row
                        objectName: "timelineTask"
                        required property var modelData
                        readonly property var task: modelData.task
                        readonly property real origin: modelData.start
                        readonly property bool done: task.status === "done"
                        readonly property bool overdue: !done && (Fmt.valid(task.deadline) ? Fmt.daysBetween(task.deadline, gantt.today) > 0
                                                                                            : Fmt.daysBetween(task.start, gantt.today) > 0)
                        readonly property bool selected: win.selectedId === task.taskId
                        readonly property string projectColor: gantt.projectColorOf(task)
                        readonly property color tint: projectColor !== "" ? projectColor : theme.accent
                        readonly property bool solid: !task.span.allDay && !done
                        readonly property bool inside: modelData.side === "inside"
                        // Drag offsets while moving the task or its right edge.
                        property real moveDx: 0
                        property real resizeDx: 0

                        x: origin + moveDx
                        y: 8 + modelData.lane * gantt.laneHeight
                        width: Math.max(modelData.end - origin, (modelData.barEnd + resizeDx) - origin)
                        height: gantt.barHeight

                        // The line from the bar to its deadline mark.
                        Rectangle {
                            visible: row.modelData.hasBar && row.modelData.hasMark
                            x: row.modelData.barEnd - row.origin
                            width: row.modelData.markX - row.modelData.barEnd
                            height: 1
                            anchors.verticalCenter: parent.verticalCenter
                            color: row.done ? theme.border : theme.muted
                        }
                        Rectangle {
                            id: bar
                            objectName: "timelineBar"
                            visible: row.modelData.hasBar
                            x: row.modelData.barStart - row.origin
                            width: Math.max(6, row.modelData.barEnd - row.modelData.barStart + row.resizeDx)
                            height: parent.height
                            radius: Math.min(4, theme.radius)
                            color: row.done ? theme.surface : row.solid ? row.tint : Qt.tint(theme.background, Qt.alpha(row.tint, 0.28))
                            border.color: row.overdue ? theme.danger : row.done ? theme.border : "transparent"
                            border.width: 1
                            Rectangle {
                                visible: row.task.priority === 1 && !row.done && !row.modelData.cutStart
                                width: 4
                                height: parent.height
                                radius: 2
                                color: theme.danger
                            }
                        }
                        // Selection: a frame around the bar, in the lane's spare room.
                        Rectangle {
                            visible: row.selected && row.modelData.hasBar
                            x: bar.x - 3
                            y: -3
                            width: bar.width + 6
                            height: parent.height + 6
                            radius: bar.radius + 2
                            color: "transparent"
                            border.color: theme.foreground
                            border.width: 2
                        }
                        Rectangle {
                            objectName: "timelineMark"
                            visible: row.modelData.hasMark
                            x: row.modelData.markX - row.origin - width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: gantt.markSize
                            height: gantt.markSize
                            rotation: 45
                            color: row.done ? theme.muted : theme.danger
                            border.color: row.selected && !row.modelData.hasBar ? theme.foreground : "transparent"
                            border.width: 2
                        }

                        // A title outside its bar sits on the colour of its day, so
                        // day lines and the now-line stop at its edges.
                        Rectangle {
                            readonly property bool onToday: Math.floor((row.modelData.labelStart + row.modelData.labelWidth / 2) / gantt.dayWidth)
                                                            === Fmt.daysBetween(gantt.rangeStart, gantt.today)
                            visible: !row.inside && row.modelData.labelWidth > 0
                            x: row.modelData.labelStart - row.origin - 3
                            width: row.modelData.labelWidth + 6
                            height: parent.height
                            radius: Math.min(4, theme.radius)
                            color: row.selected ? theme.raised
                                 : onToday ? Qt.tint(theme.background, Qt.alpha(theme.accent, 0.05)) : theme.background
                        }
                        Text {
                            objectName: "timelineLabel"
                            readonly property bool inside: row.inside
                            readonly property bool shortened: row.modelData.shortened
                            x: row.modelData.labelStart - row.origin
                            width: row.modelData.labelWidth
                            anchors.verticalCenter: parent.verticalCenter
                            font.family: theme.fontFamily
                            font.pixelSize: Math.round(12 * theme.textScale)
                            font.strikeout: row.done
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: row.modelData.text
                            // On a project colour (lifted to read on the background)
                            // the background reads in turn; on the accent, its own
                            // foreground; on the pale whole-day fill, the text colour.
                            color: row.done ? theme.muted
                                 : row.inside && row.solid ? (row.projectColor !== "" ? theme.background : theme.accentForeground)
                                 : theme.foreground
                        }

                        // Move: drag the task; it snaps to a quarter hour, or a day.
                        MouseArea {
                            objectName: "timelineGrip"
                            property real pressX
                            property bool dragging: false
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            ToolTip.visible: containsMouse && !pressed
                            ToolTip.delay: 600
                            ToolTip.text: gantt.whenOf(row.task)
                            onPressed: mouse => {
                                pressX = mapToItem(lanesArea, mouse.x, 0).x
                                dragging = false
                                win.selectedId = row.task.taskId
                            }
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return
                                const dx = mapToItem(lanesArea, mouse.x, 0).x - pressX
                                dragging = dragging || Math.abs(dx) >= 6
                                if (dragging)
                                    row.moveDx = dx
                            }
                            onReleased: {
                                const dx = row.moveDx
                                const moved = dragging
                                row.moveDx = 0
                                dragging = false
                                if (moved)
                                    gantt.moveBy(row.modelData, dx)
                            }
                            onDoubleClicked: win.editTask(row.task.taskId)
                        }
                        // Resize: drag the bar's right edge.
                        MouseArea {
                            objectName: "timelineResize"
                            visible: row.modelData.hasBar && !row.modelData.cutEnd
                            x: bar.x + bar.width - width
                            width: Math.min(10, bar.width / 2)
                            height: parent.height
                            preventStealing: true
                            cursorShape: Qt.SizeHorCursor
                            property real pressX
                            onPressed: mouse => {
                                pressX = mapToItem(lanesArea, mouse.x, 0).x
                                win.selectedId = row.task.taskId
                            }
                            onPositionChanged: mouse => {
                                if (pressed)
                                    row.resizeDx = mapToItem(lanesArea, mouse.x, 0).x - pressX
                            }
                            onReleased: {
                                const dx = row.resizeDx
                                row.resizeDx = 0
                                // A click on the edge is no resize: it must not give a
                                // task without a duration one.
                                if (Math.abs(dx) >= 3)
                                    gantt.resizeTo(row.modelData, row.modelData.barEnd + dx)
                            }
                        }
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: gantt.bars.length === 0
        title: qsTr("No scheduled tasks in this period")
        hint: qsTr("Tasks with a start day or a deadline show up here. h / l pages, + / - zooms.")
    }
}
