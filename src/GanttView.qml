import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Omaplan

// Timeline: the tasks as bars over the days, placed by day and time and as
// long as their duration. All-day tasks fill the whole day, overlaps get a
// lane each, and a red line shows now. Drag a bar to move it (a quarter hour
// at a time), drag its right edge to change the duration. h/l pages, t goes
// to today, + / - zooms.
Item {
    id: gantt

    readonly property bool canPage: true
    readonly property var zoomLevels: [1, 3, 7, 14]
    property int zoomIndex: 2
    readonly property int dayCount: zoomLevels[zoomIndex]
    readonly property var today: { planModel.today; return Fmt.startOfDay(new Date()) }
    property var anchorDate: Fmt.startOfDay(new Date())
    readonly property var rangeStart: dayCount >= 7 ? Fmt.weekStartOf(anchorDate) : anchorDate

    function page(step) { anchorDate = Fmt.addDays(anchorDate, step * Math.min(dayCount, 7)) }
    function goToday() { anchorDate = Fmt.startOfDay(new Date()) }
    function zoom(step) { zoomIndex = Math.max(0, Math.min(zoomLevels.length - 1, zoomIndex + step)) }
    function taskIds() { return bars.map(b => b.task.taskId) }
    // A task's project colour (a palette slot from the settings), "" without one.
    function projectColorOf(t) { return theme.palette[settings.projectColors[t.project] || ""] || "" }

    CalendarModel {
        id: rangeModel
        sourceModel: planModel
        from: gantt.rangeStart
        to: Fmt.addDays(gantt.rangeStart, gantt.dayCount - 1)
    }

    // The hours each day shows: 07-19 by default, widened to fit the tasks.
    readonly property var rows: { rangeModel.revision; return rangeModel.rows().filter(t => Fmt.valid(t.start)) }
    readonly property int firstHour: {
        let h = 7
        for (const t of rows) if (t.startHasTime) h = Math.min(h, t.start.getHours())
        return h
    }
    readonly property int lastHour: {
        let h = 19
        for (const t of rows)
            if (t.startHasTime) {
                const end = t.start.getHours() * 60 + t.start.getMinutes() + Math.max(30, t.durationMinutes)
                h = Math.max(h, Math.min(24, Math.ceil(end / 60)))
            }
        return h
    }
    readonly property int spanMinutes: (lastHour - firstHour) * 60
    readonly property real dayWidth: (area.width - 2) / dayCount

    // x of a moment inside the visible days.
    function xOf(dayIndex, minutes) {
        return dayIndex * dayWidth + Math.max(0, Math.min(1, (minutes - firstHour * 60) / spanMinutes)) * dayWidth
    }

    // Bars with lanes: each bar takes its own width plus room for its label,
    // and goes in the first lane where it fits.
    readonly property var bars: {
        const out = []
        for (const t of rows) {
            const day = Fmt.daysBetween(rangeStart, t.start)
            if (day < 0 || day >= dayCount)
                continue
            let x, w
            if (t.startHasTime) {
                const m = t.start.getHours() * 60 + t.start.getMinutes()
                x = xOf(day, m)
                w = Math.max(6, xOf(day, m + Math.max(15, t.durationMinutes || 30)) - x)
            } else {
                x = day * dayWidth + 2
                w = dayWidth - 4
            }
            const label = Math.min(220, t.title.length * 7 + 60)
            out.push({ task: t, x: x, w: w, reach: x + Math.max(w, label) + 8, day: day })
        }
        out.sort((a, b) => a.x - b.x)
        const lanes = []
        for (const b of out) {
            let lane = lanes.findIndex(end => end <= b.x)
            if (lane < 0) { lane = lanes.length; lanes.push(0) }
            lanes[lane] = b.reach
            b.lane = lane
        }
        return out
    }
    readonly property int laneCount: bars.reduce((n, b) => Math.max(n, b.lane + 1), 0)

    // Snaps a dragged bar back to a start: quarter hours, or whole days for
    // all-day tasks.
    function startAt(x, allDay) {
        const day = Math.max(0, Math.min(dayCount - 1, Math.floor((x + 1) / dayWidth)))
        const date = Fmt.addDays(rangeStart, day)
        if (allDay)
            return date
        const within = (x - day * dayWidth) / dayWidth
        const minutes = Math.max(0, Math.min(24 * 60 - 15, Math.round((firstHour * 60 + within * spanMinutes) / 15) * 15))
        date.setHours(Math.floor(minutes / 60), minutes % 60)
        return date
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
                    Fmt.lang
                    if (gantt.dayCount === 7)
                        return Fmt.weekTitle(gantt.rangeStart)
                    if (gantt.dayCount === 1)
                        return Fmt.capitalize(Fmt.dayLong(gantt.rangeStart))
                    return Fmt.dateShort(gantt.rangeStart) + " – " + Fmt.dateShort(Fmt.addDays(gantt.rangeStart, gantt.dayCount - 1))
                }
                color: theme.foreground
                font.pixelSize: 17
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
                        text: { Fmt.lang; return gantt.dayCount > 7 ? Fmt.dayShort(parent.date) : Fmt.capitalize(Fmt.dayLong(parent.date)) }
                        color: parent.isToday ? theme.accentText : theme.foreground
                        font.pixelSize: 12
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
                            text: String(gantt.firstHour + index).padStart(2, "0")
                            color: theme.muted
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        Flickable {
            id: area
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: Math.max(height, gantt.laneCount * 34 + 12)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Item {
                id: lanesArea
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

                Repeater {
                    model: gantt.bars
                    delegate: Item {
                        id: barItem
                        required property var modelData
                        readonly property var task: modelData.task
                        readonly property bool done: task.status === "done"
                        readonly property bool selected: win.selectedId === task.taskId
                        readonly property string projectColor: gantt.projectColorOf(task)
                        readonly property color tint: projectColor !== "" ? projectColor : theme.accent
                        x: modelData.x
                        y: 8 + modelData.lane * 34
                        width: modelData.w
                        height: 26

                        Rectangle {
                            id: bar
                            anchors.fill: parent
                            radius: Math.min(4, theme.radius)
                            color: barItem.done ? theme.surface
                                 : barItem.task.startHasTime ? barItem.tint : Qt.alpha(barItem.tint, 0.28)
                            border.color: barItem.selected ? theme.foreground : barItem.done ? theme.border : "transparent"
                            border.width: barItem.selected ? 2 : 1
                            Rectangle {
                                visible: barItem.task.priority === 1 && !barItem.done
                                width: 4
                                height: parent.height
                                radius: 2
                                color: theme.danger
                            }
                        }
                        // The label sits inside a wide bar and beside a narrow one.
                        Text {
                            font.family: theme.fontFamily
                            readonly property bool inside: barItem.width > implicitWidth + 16
                            x: inside ? 8 : barItem.width + 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: inside ? barItem.width - 12 : implicitWidth
                            elide: Text.ElideRight
                            text: {
                                Fmt.lang
                                const t = barItem.task
                                return (t.startHasTime ? Fmt.time(t.start) + "  " : "") + t.title
                            }
                            // On a project colour (lifted to read on the background) the
                            // background reads in turn.
                            color: inside && barItem.task.startHasTime && !barItem.done
                                   ? (barItem.projectColor !== "" ? theme.background : theme.accentForeground)
                                 : barItem.done ? theme.muted : theme.foreground
                            font.strikeout: barItem.done
                            font.pixelSize: 12
                        }

                        // Move: drag the bar; it snaps to a quarter hour (or a day).
                        MouseArea {
                            anchors.fill: parent
                            anchors.rightMargin: barItem.task.startHasTime ? 8 : 0
                            preventStealing: true
                            cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            drag.target: barItem
                            drag.axis: Drag.XAxis
                            drag.threshold: 6
                            // Follow the pointer from the press, not from where
                            // the threshold was crossed, or the bar lands early.
                            drag.smoothed: false
                            onPressed: win.selectedId = barItem.task.taskId
                            onDoubleClicked: win.editTask(barItem.task.taskId)
                            onReleased: {
                                if (Math.abs(barItem.x - barItem.modelData.x) < 3) {
                                    barItem.x = barItem.modelData.x
                                    return
                                }
                                const when = gantt.startAt(barItem.x, !barItem.task.startHasTime)
                                if (!win.check(actions.guiMove(barItem.task.taskId, when, barItem.task.startHasTime)))
                                    barItem.x = barItem.modelData.x
                            }
                        }
                        // Resize: drag the right edge; duration in quarter hours.
                        MouseArea {
                            visible: barItem.task.startHasTime
                            anchors.right: parent.right
                            width: 10
                            height: parent.height
                            preventStealing: true
                            cursorShape: Qt.SizeHorCursor
                            property real pressX
                            property real startWidth
                            onPressed: mouse => {
                                pressX = mapToItem(lanesArea, mouse.x, 0).x
                                startWidth = barItem.width
                                win.selectedId = barItem.task.taskId
                            }
                            onPositionChanged: mouse => {
                                if (pressed)
                                    barItem.width = Math.max(6, startWidth + mapToItem(lanesArea, mouse.x, 0).x - pressX)
                            }
                            onReleased: {
                                // A click on the edge is no resize: it must not give a
                                // task without a duration one.
                                const minutes = Math.max(15, Math.round(barItem.width / gantt.dayWidth * gantt.spanMinutes / 15) * 15)
                                if (Math.abs(barItem.width - startWidth) >= 3 && minutes !== barItem.task.durationMinutes)
                                    win.check(actions.guiEdit(barItem.task.taskId, { "duration": String(minutes) }))
                                barItem.width = Qt.binding(() => barItem.modelData.w)
                            }
                        }
                    }
                }

                // Now.
                Rectangle {
                    readonly property int day: Fmt.daysBetween(gantt.rangeStart, gantt.today)
                    visible: day >= 0 && day < gantt.dayCount && gantt.nowMinutes >= gantt.firstHour * 60
                             && gantt.nowMinutes <= gantt.lastHour * 60
                    x: gantt.xOf(day, gantt.nowMinutes) - 1
                    width: 2
                    height: lanesArea.height
                    color: theme.danger
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: gantt.bars.length === 0
        title: qsTr("No scheduled tasks in this period")
        hint: qsTr("Tasks with a start day show up here. h / l pages, + / - zooms.")
    }
}
