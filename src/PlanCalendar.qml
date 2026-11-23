import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Omaplan

// Calendar: week (w) as a time grid and month (m) as cells. Tasks are dragged
// to a new day; in the week also to a new time (a quarter hour at a time) or up
// into the all-day row. h/l or the arrows page, t goes to today. Double-click
// on an empty spot creates a task there.
Item {
    id: cal
    property string mode: "week"

    readonly property bool canPage: true
    readonly property var today: { planModel.today; return Fmt.startOfDay(new Date()) }
    property var anchorDate: Fmt.startOfDay(new Date())
    readonly property var weekStart: Fmt.mondayOf(anchorDate)
    readonly property var monthStart: new Date(anchorDate.getFullYear(), anchorDate.getMonth(), 1)
    readonly property var gridStart: Fmt.mondayOf(monthStart)
    readonly property var rangeStart: mode === "week" ? weekStart : gridStart
    readonly property int rangeDays: mode === "week" ? 7 : 42

    function page(step) {
        anchorDate = mode === "week" ? Fmt.addDays(anchorDate, 7 * step) : Fmt.addMonths(anchorDate, step)
    }
    function goToday() { anchorDate = Fmt.startOfDay(new Date()) }
    function taskIds() { return rangeModel.ids() }

    CalendarModel {
        id: rangeModel
        sourceModel: planModel
        from: cal.rangeStart
        to: Fmt.addDays(cal.rangeStart, cal.rangeDays - 1)
    }

    // The tasks of the visible range, grouped by day index (0 = rangeStart).
    readonly property var days: {
        rangeModel.revision
        const out = []
        for (let i = 0; i < rangeDays; ++i)
            out.push({ allDay: [], timed: [] })
        for (const t of rangeModel.rows()) {
            const day = Fmt.valid(t.start) ? t.start : t.deadline
            const i = Fmt.daysBetween(rangeStart, day)
            if (i < 0 || i >= rangeDays)
                continue
            t.deadlineOnly = !Fmt.valid(t.start)
            if (t.startHasTime)
                out[i].timed.push(t)
            else
                out[i].allDay.push(t)
        }
        return out
    }

    function chipText(t) {
        return (t.startHasTime ? Fmt.time(t.start) + "  " : "") + (t.deadlineOnly ? "⚑ " : "") + t.title
    }

    // --- Drag and drop ------------------------------------------------------
    // The task stays in place (dimmed) while a ghost follows the pointer; on
    // release the drop position decides the new day, and in the week grid
    // also the time.
    function drop(task, pointInCal, grabOffset) {
        if (mode === "week") {
            const inGrid = cal.mapToItem(gridContent, pointInCal.x, pointInCal.y)
            const inAllDay = cal.mapToItem(allDayArea, pointInCal.x, pointInCal.y)
            const col = Math.floor((inGrid.x - gutter) / weekCol)
            if (col < 0 || col > 6)
                return
            const date = Fmt.addDays(weekStart, col)
            if (inAllDay.y < allDayArea.height + 4) {
                win.check(actions.guiMove(task.taskId, date, false))
            } else {
                const top = inGrid.y - grabOffset.y
                const minutes = Math.max(0, Math.min(24 * 60 - 15, Math.round(top / hourHeight * 4) * 15))
                date.setHours(Math.floor(minutes / 60), minutes % 60)
                win.check(actions.guiMove(task.taskId, date, true))
            }
        } else {
            const p = cal.mapToItem(monthGrid, pointInCal.x, pointInCal.y)
            const col = Math.floor(p.x / (monthGrid.width / 7))
            const row = Math.floor(p.y / (monthGrid.height / 6))
            if (col < 0 || col > 6 || row < 0 || row > 5)
                return
            const date = Fmt.addDays(gridStart, row * 7 + col)
            if (task.startHasTime)
                date.setHours(task.start.getHours(), task.start.getMinutes())
            win.check(actions.guiMove(task.taskId, date, task.startHasTime))
        }
    }

    component Draggable: MouseArea {
        id: dragArea
        property var task
        property point grab
        property bool dragging: false
        preventStealing: true
        hoverEnabled: true
        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => {
            grab = Qt.point(mouse.x, mouse.y)
            win.selectedId = task.taskId
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            if (!dragging && Math.abs(mouse.x - grab.x) + Math.abs(mouse.y - grab.y) > 6) {
                dragging = true
                cal.dragActive = true
                ghost.text = cal.chipText(task)
                ghost.width = Math.max(90, Math.min(parent.width, 220))
                ghost.height = Math.min(parent.height, 60)
            }
            if (dragging) {
                const p = mapToItem(cal, mouse.x, mouse.y)
                ghost.x = p.x - Math.min(grab.x, ghost.width - 10)
                ghost.y = p.y - Math.min(grab.y, ghost.height - 6)
            }
        }
        onReleased: mouse => {
            if (!dragging)
                return
            dragging = false
            cal.dragActive = false
            cal.drop(task, mapToItem(cal, mouse.x, mouse.y), grab)
        }
        onCanceled: { dragging = false; cal.dragActive = false }
        onDoubleClicked: win.editTask(task.taskId)
    }

    component Chip: Rectangle {
        id: chip
        property var task
        readonly property bool done: task.status === "done"
        readonly property bool selected: win.selectedId === task.taskId
        height: 20
        radius: Math.min(3, theme.radius)
        color: done ? theme.surface : task.deadlineOnly ? Qt.alpha(theme.danger, 0.16) : Qt.alpha(theme.accent, 0.22)
        border.color: selected ? theme.accent : done ? theme.border : "transparent"
        opacity: drag.dragging ? 0.35 : 1
        Rectangle {
            width: 3
            height: parent.height
            radius: 1
            color: chip.task.priority === 1 ? theme.danger : theme.accent
            visible: !chip.done
        }
        Text {
            anchors.fill: parent
            anchors.leftMargin: 7
            anchors.rightMargin: 4
            verticalAlignment: Text.AlignVCenter
            text: cal.chipText(chip.task)
            elide: Text.ElideRight
            font.pixelSize: 11
            font.strikeout: chip.done
            color: chip.done ? theme.muted : theme.foreground
        }
        Draggable { id: drag; anchors.fill: parent; task: chip.task }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // Toolbar: paging, title, week/month.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            HeaderButton { text: "‹"; onClicked: cal.page(-1) }
            HeaderButton { text: qsTr("Today"); keyHint: "t"; onClicked: cal.goToday() }
            HeaderButton { text: "›"; onClicked: cal.page(1) }
            Text {
                Layout.leftMargin: 10
                text: { Fmt.lang; return cal.mode === "week" ? Fmt.weekTitle(cal.weekStart) : Fmt.capitalize(Fmt.monthTitle(cal.monthStart)) }
                color: theme.foreground
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                text: "h / l"
                color: theme.muted
                font.pixelSize: 11
                font.family: "monospace"
            }
            Item { Layout.fillWidth: true }
            HeaderButton { text: qsTr("Week"); keyHint: "w"; active: cal.mode === "week"; onClicked: win.calendarMode = "week" }
            HeaderButton { text: qsTr("Month"); keyHint: "m"; active: cal.mode === "month"; onClicked: win.calendarMode = "month" }
        }

        // ---- Week: day headers, all-day row, hour grid --------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: cal.mode === "week"
            spacing: 0

            Row {
                Layout.fillWidth: true
                Item { width: cal.gutter; height: 1 }
                Repeater {
                    model: 7
                    delegate: Item {
                        required property int index
                        readonly property var date: Fmt.addDays(cal.weekStart, index)
                        readonly property bool isToday: Fmt.sameDay(date, cal.today)
                        width: cal.weekCol
                        height: 34
                        Row {
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: { Fmt.lang; return Fmt.weekdayShort(parent.parent.date) }
                                color: parent.parent.isToday ? theme.accent : theme.muted
                                font.pixelSize: 12
                            }
                            Rectangle {
                                width: 26
                                height: 26
                                radius: 13
                                color: parent.parent.isToday ? theme.accent : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: parent.parent.parent.date.getDate()
                                    color: parent.parent.parent.isToday ? theme.accentForeground : theme.foreground
                                    font.pixelSize: 14
                                    font.bold: parent.parent.parent.isToday
                                }
                            }
                        }
                    }
                }
            }

            // All-day tasks (and tasks with only a deadline, marked ⚑).
            Item {
                id: allDayArea
                Layout.fillWidth: true
                readonly property int rowsShown: {
                    let n = 1
                    for (const d of cal.days) n = Math.max(n, d.allDay.length)
                    return Math.min(n, 4)
                }
                implicitHeight: rowsShown * 22 + 8
                Rectangle { anchors.fill: parent; color: theme.surface; radius: Math.min(4, theme.radius) }
                Text {
                    width: cal.gutter - 6
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("all day")
                    color: theme.muted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: cal.mode === "week" ? 7 : 0
                    delegate: Item {
                        id: allDayCol
                        required property int index
                        x: cal.gutter + index * cal.weekCol
                        y: 4
                        width: cal.weekCol
                        height: allDayArea.height - 8
                        readonly property var items: cal.days[index] ? cal.days[index].allDay : []
                        Column {
                            spacing: 2
                            Repeater {
                                model: allDayCol.items.slice(0, allDayArea.rowsShown - (allDayCol.items.length > allDayArea.rowsShown ? 1 : 0))
                                delegate: Chip {
                                    required property var modelData
                                    task: modelData
                                    width: cal.weekCol - 6
                                    x: 3
                                }
                            }
                            Text {
                                visible: allDayCol.items.length > allDayArea.rowsShown
                                text: qsTr("+%1 more").arg(allDayCol.items.length - allDayArea.rowsShown + 1)
                                color: theme.muted
                                font.pixelSize: 11
                                leftPadding: 6
                            }
                        }
                    }
                }
            }

            Flickable {
                id: flick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 6
                clip: true
                contentHeight: gridContent.height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                // Start at 07:00, or earlier when the week has an earlier task.
                function scrollToWorkday() {
                    let hour = 7
                    for (const b of cal.weekBlocks)
                        hour = Math.min(hour, Math.floor(b.top / 60))
                    contentY = Math.min(hour * cal.hourHeight, Math.max(0, contentHeight - height))
                }
                Component.onCompleted: Qt.callLater(scrollToWorkday)
                onHeightChanged: if (contentY === 0) Qt.callLater(scrollToWorkday)

                Item {
                    id: gridContent
                    width: flick.width
                    height: 24 * cal.hourHeight

                    // Hour lines and labels.
                    Repeater {
                        model: 24
                        delegate: Item {
                            required property int index
                            y: index * cal.hourHeight
                            width: gridContent.width
                            height: cal.hourHeight
                            Rectangle { x: cal.gutter; width: parent.width - cal.gutter; height: 1; color: theme.border; opacity: 0.6 }
                            Text {
                                width: cal.gutter - 8
                                y: -7
                                visible: parent.index > 0
                                horizontalAlignment: Text.AlignRight
                                text: (parent.index < 10 ? "0" : "") + parent.index + ":00"
                                color: theme.muted
                                font.pixelSize: 10
                            }
                        }
                    }

                    // Day columns: today tinted; double-click adds a task there.
                    Repeater {
                        model: 7
                        delegate: Rectangle {
                            required property int index
                            readonly property var date: Fmt.addDays(cal.weekStart, index)
                            x: cal.gutter + index * cal.weekCol
                            width: cal.weekCol
                            height: gridContent.height
                            color: Fmt.sameDay(date, cal.today) ? Qt.alpha(theme.accent, 0.05) : "transparent"
                            Rectangle { width: 1; height: parent.height; color: theme.border; opacity: 0.6 }
                            TapHandler {
                                onDoubleTapped: point => {
                                    const minutes = Math.floor(point.position.y / cal.hourHeight * 2) * 30
                                    win.newTask({ "start": Fmt.isoDay(parent.date) + " "
                                                  + String(Math.floor(minutes / 60)).padStart(2, "0") + ":"
                                                  + String(minutes % 60).padStart(2, "0"), "duration": "60" })
                                }
                            }
                        }
                    }

                    // Timed tasks: top = start, height = duration, overlaps side by side.
                    Repeater {
                        model: cal.mode === "week" ? cal.weekBlocks : []
                        delegate: Rectangle {
                            id: block
                            required property var modelData
                            readonly property var task: modelData.task
                            readonly property bool done: task.status === "done"
                            readonly property bool selected: win.selectedId === task.taskId
                            x: cal.gutter + modelData.day * cal.weekCol + 2 + modelData.lane * (cal.weekCol - 4) / modelData.lanes
                            y: modelData.top / 60 * cal.hourHeight + 1
                            width: (cal.weekCol - 4) / modelData.lanes - 2
                            height: Math.max(20, modelData.minutes / 60 * cal.hourHeight - 2)
                            radius: Math.min(4, theme.radius)
                            color: done ? theme.surface : Qt.alpha(theme.accent, 0.24)
                            border.color: selected ? theme.accent : done ? theme.border : Qt.alpha(theme.accent, 0.5)
                            border.width: selected ? 2 : 1
                            opacity: blockDrag.dragging ? 0.35 : 1
                            clip: true
                            Rectangle {
                                width: 3
                                height: parent.height
                                color: block.task.priority === 1 ? theme.danger : theme.accent
                                visible: !block.done
                            }
                            Column {
                                x: 7
                                y: 3
                                width: parent.width - 10
                                Text {
                                    width: parent.width
                                    text: block.task.title
                                    color: block.done ? theme.muted : theme.foreground
                                    font.strikeout: block.done
                                    font.pixelSize: 12
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    visible: block.height > 34
                                    text: { Fmt.lang; return Fmt.timeRange(block.task.start, block.task.durationMinutes)
                                            + (block.task.project !== "" ? " · " + block.task.project : "") }
                                    color: theme.muted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            Draggable { id: blockDrag; anchors.fill: parent; task: block.task }
                        }
                    }

                    // Now.
                    Rectangle {
                        readonly property int day: Fmt.daysBetween(cal.weekStart, cal.today)
                        visible: day >= 0 && day < 7
                        x: cal.gutter + day * cal.weekCol - 4
                        y: cal.nowMinutes / 60 * cal.hourHeight - 1
                        width: cal.weekCol + 4
                        height: 2
                        color: theme.danger
                        Rectangle { width: 8; height: 8; radius: 4; y: -3; color: theme.danger }
                    }
                }
            }
        }

        // ---- Month: 6 x 7 cells with tasks --------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: cal.mode === "month"
            spacing: 4

            Row {
                Layout.fillWidth: true
                Repeater {
                    model: 7
                    delegate: Text {
                        required property int index
                        width: monthGrid.width / 7
                        horizontalAlignment: Text.AlignHCenter
                        text: { Fmt.lang; return Fmt.weekdayShort(Fmt.addDays(cal.gridStart, index)) }
                        color: theme.muted
                        font.pixelSize: 12
                    }
                }
            }
            Item {
                id: monthGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                Repeater {
                    model: cal.mode === "month" ? 42 : 0
                    delegate: Rectangle {
                        id: cell
                        required property int index
                        readonly property var date: Fmt.addDays(cal.gridStart, index)
                        readonly property bool inMonth: date.getMonth() === cal.monthStart.getMonth()
                        readonly property bool isToday: Fmt.sameDay(date, cal.today)
                        readonly property var items: cal.days[index] ? cal.days[index].allDay.concat(cal.days[index].timed) : []
                        readonly property int fits: Math.max(0, Math.floor((height - 28) / 22))
                        x: (index % 7) * monthGrid.width / 7
                        y: Math.floor(index / 7) * monthGrid.height / 6
                        width: monthGrid.width / 7 - 3
                        height: monthGrid.height / 6 - 3
                        radius: Math.min(4, theme.radius)
                        color: inMonth ? theme.surface : Qt.alpha(theme.surface, 0.4)
                        border.color: isToday ? theme.accent : "transparent"
                        TapHandler {
                            onDoubleTapped: win.newTask({ "start": Fmt.isoDay(cell.date) })
                        }
                        Rectangle {
                            x: 4
                            y: 4
                            width: 22
                            height: 22
                            radius: 11
                            color: cell.isToday ? theme.accent : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: cell.date.getDate()
                                color: cell.isToday ? theme.accentForeground : cell.inMonth ? theme.foreground : theme.muted
                                font.pixelSize: 12
                                font.bold: cell.isToday
                            }
                        }
                        Column {
                            x: 3
                            y: 28
                            width: parent.width - 6
                            spacing: 2
                            Repeater {
                                model: cell.items.slice(0, cell.items.length > cell.fits ? Math.max(0, cell.fits - 1) : cell.fits)
                                delegate: Chip {
                                    required property var modelData
                                    task: modelData
                                    width: parent.width
                                }
                            }
                            Text {
                                visible: cell.items.length > cell.fits
                                text: qsTr("+%1 more").arg(cell.items.length - Math.max(0, cell.fits - 1))
                                color: theme.muted
                                font.pixelSize: 11
                                leftPadding: 4
                            }
                        }
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 40
        visible: rangeModel.count === 0 && cal.mode === "week"
        title: qsTr("Nothing planned this week")
        hint: qsTr("Press n to write tasks, or double-click a free spot. h / l pages between weeks.")
    }
    // The month grid shows dates even without tasks; a friendly hint names the
    // month, so an empty month says so too. Text does not take clicks, so
    // double-clicking a cell to create a task still works.
    EmptyState {
        anchors.centerIn: parent
        visible: rangeModel.count === 0 && cal.mode === "month"
        title: qsTr("Nothing planned this month")
        hint: qsTr("Press n to write tasks, or double-click a free spot. h / l pages between months.")
    }

    // Geometry.
    readonly property int gutter: 52
    readonly property real weekCol: (width - 28 - gutter) / 7
    readonly property int hourHeight: 46
    property int nowMinutes: new Date().getHours() * 60 + new Date().getMinutes()
    Timer {
        interval: 60000
        running: true
        repeat: true
        onTriggered: cal.nowMinutes = new Date().getHours() * 60 + new Date().getMinutes()
    }

    // Timed tasks as blocks with lanes: overlapping tasks share the column.
    readonly property var weekBlocks: {
        const blocks = []
        if (mode !== "week")
            return blocks
        for (let d = 0; d < 7; ++d) {
            const items = (days[d] ? days[d].timed : []).map(t => ({
                task: t,
                top: t.start.getHours() * 60 + t.start.getMinutes(),
                minutes: Math.max(30, t.durationMinutes || 30)
            })).sort((a, b) => a.top - b.top)
            let cluster = [], clusterEnd = -1
            const flush = () => {
                const lanes = []
                for (const b of cluster) {
                    let lane = lanes.findIndex(end => end <= b.top)
                    if (lane < 0) { lane = lanes.length; lanes.push(0) }
                    lanes[lane] = b.top + b.minutes
                    b.lane = lane
                }
                for (const b of cluster) { b.lanes = lanes.length; b.day = d; blocks.push(b) }
                cluster = []
            }
            for (const b of items) {
                if (cluster.length && b.top >= clusterEnd) flush()
                cluster.push(b)
                clusterEnd = Math.max(clusterEnd, b.top + b.minutes)
            }
            if (cluster.length) flush()
        }
        return blocks
    }

    // The ghost that follows the pointer while dragging.
    Rectangle {
        id: ghost
        property string text: ""
        visible: (cal.mode === "week" || cal.mode === "month") && text !== "" && cal.dragActive
        z: 100
        radius: Math.min(4, theme.radius)
        color: theme.accent
        opacity: 0.9
        Text {
            anchors.fill: parent
            anchors.margins: 5
            text: ghost.text
            color: theme.accentForeground
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
            wrapMode: Text.Wrap
        }
    }
    // True while any Draggable is dragging (bound through the ghost's position updates).
    property bool dragActive: false
}
