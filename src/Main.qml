import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "Omaplan"
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.foreground: theme.foreground
    Material.background: theme.surface
    Material.roundedScale: theme.radius

    // 0 = Overblik, 1 = Kalender, 2 = Liste, 3 = Tidslinje
    property int currentView: 0
    property string calendarMode: "week" // "week" | "month"
    property string selectedId: ""
    property bool helpVisible: false
    // Any overlay that takes the keyboard: the view keys are off meanwhile,
    // so typing "q" or "2" in a text field never quits or switches view.
    readonly property bool modal: helpVisible || inputOverlay.visible || proposalView.visible || editor.visible
    // When an overlay closes, the keyboard goes back to the window. A hidden
    // text field that kept focus would otherwise swallow j, x, n, Ctrl+Z.
    onModalChanged: if (!modal) keySink.forceActiveFocus()

    readonly property var views: [overviewView, calendarView, listView, ganttView]
    readonly property var currentViewItem: views[currentView]

    // Short message at the bottom: live changes, undo, and every error from
    // an action, so nothing fails silently.
    function notify(text, isError) {
        toast.text = text
        toast.isError = !!isError
        toastTimer.interval = isError ? 7000 : 3000
        toastTimer.restart()
    }
    // Shows the error of a failed action; returns whether it succeeded.
    function check(result) {
        if (!result.ok)
            notify(result.error, true)
        return result.ok
    }

    function openInput() {
        if (!modal)
            inputOverlay.open()
    }
    function editTask(id) {
        if (id !== "")
            editor.openTask(id)
    }
    function newTask(fields) {
        editor.openNew(fields || {})
    }

    // Keyboard selection walks the current view's tasks in display order.
    function selectStep(step) {
        const ids = currentViewItem.taskIds()
        if (ids.length === 0)
            return
        let i = ids.indexOf(selectedId)
        i = i < 0 ? (step > 0 ? 0 : ids.length - 1) : Math.max(0, Math.min(ids.length - 1, i + step))
        selectedId = ids[i]
    }
    function selectedTask() {
        if (selectedId === "")
            return null
        const r = actions.guiTask(selectedId)
        return r.ok ? r.payload.task : null
    }
    function toggleDone() {
        const t = selectedTask()
        if (t)
            check(actions.guiSetDone(t.id, t.status !== "done"))
    }
    function deleteSelected() {
        const t = selectedTask()
        if (t && check(actions.guiRemove(t.id)))
            notify(qsTr("Deleted “%1” · Ctrl+Z brings it back").arg(t.title))
    }
    // Shift+arrows: a day left/right, or a quarter hour up/down for a task
    // with a time. An unscheduled task lands on today.
    function nudge(days, minutes) {
        const t = selectedTask()
        if (!t)
            return
        const base = t.start !== "" ? Fmt.fromIso(t.start) : Fmt.startOfDay(new Date())
        let hasTime = t.start_has_time
        if (minutes !== 0 && !hasTime) {
            base.setHours(9, 0)
            hasTime = true
            minutes = 0
        }
        const when = t.start === "" && days !== 0 ? base : Fmt.addDays(base, days)
        when.setMinutes(when.getMinutes() + minutes)
        check(actions.guiMove(t.id, when, hasTime))
    }
    function undo() {
        const r = actions.guiUndo()
        if (check(r))
            notify(r.payload.count > 1 ? qsTr("Undone (%1 changes)").arg(r.payload.count) : qsTr("Undone"))
    }
    function redo() {
        const r = actions.guiRedo()
        if (check(r))
            notify(r.payload.count > 1 ? qsTr("Redone (%1 changes)").arg(r.payload.count) : qsTr("Redone"))
    }

    // --- Keys -------------------------------------------------------------
    // Every key also shows in the ? overlay (HelpOverlay.qml).
    Shortcut { sequence: "1"; enabled: !win.modal; onActivated: win.currentView = 0 }
    Shortcut { sequence: "2"; enabled: !win.modal; onActivated: win.currentView = 1 }
    Shortcut { sequence: "3"; enabled: !win.modal; onActivated: win.currentView = 2 }
    Shortcut { sequence: "4"; enabled: !win.modal; onActivated: win.currentView = 3 }
    Shortcut { sequences: ["?", "Shift+?"]; enabled: !inputOverlay.visible && !proposalView.visible && !editor.visible
        onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Esc"; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "q"; enabled: !win.modal; onActivated: Qt.quit() }
    Shortcut { sequences: ["n", "Space"]; enabled: !win.modal; onActivated: win.openInput() }
    Shortcut { sequence: "a"; enabled: !win.modal; onActivated: win.newTask({}) }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !win.modal; onActivated: win.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !win.modal; onActivated: win.redo() }

    Shortcut { sequences: ["j", "Down"]; enabled: !win.modal; onActivated: win.selectStep(1) }
    Shortcut { sequences: ["k", "Up"]; enabled: !win.modal; onActivated: win.selectStep(-1) }
    Shortcut { sequence: "x"; enabled: !win.modal && win.selectedId !== ""; onActivated: win.toggleDone() }
    Shortcut { sequences: ["e", "Return", "Enter"]; enabled: !win.modal && win.selectedId !== ""
        onActivated: win.editTask(win.selectedId) }
    Shortcut { sequences: ["Delete", "d"]; enabled: !win.modal && win.selectedId !== ""; onActivated: win.deleteSelected() }
    Shortcut { sequence: "Shift+Left"; enabled: !win.modal && win.selectedId !== ""; onActivated: win.nudge(-1, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: !win.modal && win.selectedId !== ""; onActivated: win.nudge(1, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: !win.modal && win.selectedId !== ""; onActivated: win.nudge(0, -15) }
    Shortcut { sequence: "Shift+Down"; enabled: !win.modal && win.selectedId !== ""; onActivated: win.nudge(0, 15) }

    Shortcut { sequences: ["h", "Left"]; enabled: !win.modal && win.currentViewItem.canPage; onActivated: win.currentViewItem.page(-1) }
    Shortcut { sequences: ["l", "Right"]; enabled: !win.modal && win.currentViewItem.canPage; onActivated: win.currentViewItem.page(1) }
    Shortcut { sequence: "t"; enabled: !win.modal && win.currentViewItem.canPage; onActivated: win.currentViewItem.goToday() }
    Shortcut { sequence: "w"; enabled: !win.modal && win.currentView === 1; onActivated: win.calendarMode = "week" }
    Shortcut { sequence: "m"; enabled: !win.modal && win.currentView === 1; onActivated: win.calendarMode = "month" }
    Shortcut { sequence: "g"; enabled: !win.modal && win.currentView === 2; onActivated: listView.cycleGrouping() }
    Shortcut { sequence: "f"; enabled: !win.modal && win.currentView === 2; onActivated: listView.cycleProjectFilter() }
    Shortcut { sequence: "s"; enabled: !win.modal && win.currentView === 2; onActivated: listView.cycleStatusFilter() }
    Shortcut { sequences: ["+", "="]; enabled: !win.modal && win.currentView === 3; onActivated: ganttView.zoom(-1) }
    Shortcut { sequence: "-"; enabled: !win.modal && win.currentView === 3; onActivated: ganttView.zoom(1) }

    // --- Header: the four views as tabs, the main action and a quiet ? -------
    // One bar in the window's own colour with a 1 px divider, as in Folio.
    header: Rectangle {
        height: 48
        color: theme.background
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.border }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 4

            Repeater {
                model: [qsTr("Overview"), qsTr("Calendar"), qsTr("List"), qsTr("Timeline")]
                delegate: AbstractButton {
                    id: tab
                    required property int index
                    required property string modelData
                    Layout.fillHeight: true
                    focusPolicy: Qt.NoFocus
                    onClicked: win.currentView = index
                    contentItem: RowLayout {
                        spacing: 6
                        Text {
                            text: tab.index + 1
                            color: theme.muted
                            font.pixelSize: 11
                            font.family: "monospace"
                        }
                        Text {
                            text: tab.modelData
                            color: win.currentView === tab.index ? theme.foreground : theme.muted
                            font.pixelSize: 14
                            font.bold: win.currentView === tab.index
                        }
                    }
                    leftPadding: 10
                    rightPadding: 10
                    background: Rectangle {
                        color: tab.hovered ? theme.raised : "transparent"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: theme.accent
                            visible: win.currentView === tab.index
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: qsTr("New task")
                keyHint: "n"
                primary: true
                onClicked: win.openInput()
            }
            // The shortcuts behind a small ?, like Omacut's and Folio's.
            AbstractButton {
                id: helpButton
                Layout.leftMargin: 6
                focusPolicy: Qt.NoFocus
                implicitWidth: 24
                implicitHeight: 24
                onClicked: win.helpVisible = !win.helpVisible
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Keyboard shortcuts") + " (?)"
                contentItem: Text {
                    text: "?"
                    color: helpButton.hovered ? theme.foreground : theme.muted
                    font.pixelSize: 13
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: width / 2
                    color: helpButton.hovered ? theme.raised : "transparent"
                    border.color: theme.border
                }
            }
        }
    }

    // Right-to-left languages mirror the window; main.cpp sets the
    // application's layout direction.
    Item {
        id: keySink
        focus: true
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // The plan could not be read: say so instead of showing an empty plan.
            Rectangle {
                Layout.fillWidth: true
                visible: planModel.error !== ""
                implicitHeight: errorText.implicitHeight + 20
                color: Qt.alpha(theme.danger, 0.18)
                border.color: theme.danger
                Text {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: planModel.error
                    color: theme.foreground
                    wrapMode: Text.Wrap
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.currentView
                OverviewView { id: overviewView }
                PlanCalendar { id: calendarView; mode: win.calendarMode }
                PlanList { id: listView }
                GanttView { id: ganttView }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }

        InputOverlay {
            id: inputOverlay
            anchors.fill: parent
            onCompareRequested: text => proposalView.startCompare(text)
            onProposalReady: (text, proposal, captureId) => proposalView.openSingle(text, proposal, captureId)
            onProposalFailed: (text, message) => proposalView.showSingleError(text, message)
        }

        ProposalView {
            id: proposalView
            anchors.fill: parent
            onApproved: count => {
                inputOverlay.clearDraft()
                win.notify(count === 1 ? qsTr("1 task added · Ctrl+Z takes it back")
                                       : qsTr("%1 tasks added · Ctrl+Z takes them back").arg(count))
            }
            onRetryRequested: text => inputOverlay.open(text)
            onCreateYourselfRequested: text => win.newTask({ "title": text })
        }

        TaskEditor {
            id: editor
            anchors.fill: parent
            onSaved: message => win.notify(message)
            onDeleted: title => win.notify(qsTr("Deleted “%1” · Ctrl+Z brings it back").arg(title))
        }

        // Brief notice when another process (CLI, agent) changed the plan.
        Connections {
            target: planModel
            function onExternalChange(source) {
                const names = { "cli": qsTr("the command line"), "mcp": qsTr("an agent (MCP)"), "gui": qsTr("another window") }
                win.notify(qsTr("Changed from %1").arg(names[source] || source))
            }
        }

        Rectangle {
            id: toast
            property string text: ""
            property bool isError: false
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 18
            visible: text !== ""
            color: theme.surface
            border.color: isError ? theme.danger : theme.accent
            border.width: 1
            radius: Math.min(6, theme.radius)
            width: Math.min(toastLabel.implicitWidth + 28, parent.width - 48)
            height: toastLabel.implicitHeight + 16
            Text {
                id: toastLabel
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 14
                text: toast.text
                color: theme.foreground
                font.pixelSize: 13
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            MouseArea { anchors.fill: parent; onClicked: toast.text = "" }
        }
        Timer {
            id: toastTimer
            onTriggered: toast.text = ""
        }
    }
}
