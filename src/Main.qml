import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: "Omaplan"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    // 0 = Overblik, 1 = Kalender, 2 = Liste, 3 = Tidslinje
    property int currentView: 0
    property bool helpVisible: false
    property string calendarMode: "week" // "week" | "month"
    property bool inputOpen: false
    property bool proposalOpen: false
    property bool proposing: false

    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; onActivated: win.currentView = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; onActivated: win.currentView = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; onActivated: win.currentView = 2 }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; onActivated: win.currentView = 3 }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "n"; context: Qt.ApplicationShortcut; enabled: !win.inputOpen && !win.proposalOpen && !win.helpVisible
        onActivated: { win.inputOpen = true; inputOverlay.open() } }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.inputOpen && !win.proposalOpen && !win.helpVisible
        onActivated: { win.inputOpen = true; inputOverlay.open() } }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: actions.guiUndo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; onActivated: actions.guiRedo() }
    Shortcut { sequence: "w"; context: Qt.ApplicationShortcut; enabled: win.currentView === 1; onActivated: win.calendarMode = "week" }
    Shortcut { sequence: "m"; context: Qt.ApplicationShortcut; enabled: win.currentView === 1; onActivated: win.calendarMode = "month" }
    Shortcut { sequence: "g"; context: Qt.ApplicationShortcut; enabled: win.currentView === 2; onActivated: listView.cycleGrouping() }
    Shortcut { sequence: "f"; context: Qt.ApplicationShortcut; enabled: win.currentView === 2; onActivated: listView.cycleProjectFilter() }

    header: ToolBar {
        Material.background: "#161618"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Label {
                text: [qsTr("Overview"), qsTr("Calendar"), qsTr("List"), qsTr("Timeline")][win.currentView]
                font.bold: true
                color: Material.foreground
            }
            Label {
                visible: win.currentView === 1
                text: win.calendarMode === "week" ? qsTr("Week") : qsTr("Month")
                color: Material.hintTextColor
            }
            Label {
                visible: win.currentView === 2
                text: listView.statusText
                color: Material.hintTextColor
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("1-4 views · ? help · q quit · Ctrl+Z undo")
                color: Material.hintTextColor
                font.pixelSize: 12
            }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        StackLayout {
            anchors.fill: parent
            currentIndex: win.currentView
            OverviewView {}
            PlanCalendar { mode: win.calendarMode }
            PlanList { id: listView }
            GanttView {}
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }

        InputOverlay {
            id: inputOverlay
            anchors.fill: parent
            busy: win.proposing
            onSubmitted: text => {
                win.proposing = true
                win.inputOpen = false
                inputOverlay.close()
                actions.guiPropose(text, "")
            }
            onCloseRequested: {
                win.inputOpen = false
                inputOverlay.close()
            }
        }

        ProposalView {
            id: proposalView
            anchors.fill: parent
            onApproved: win.proposalOpen = false
            onCloseRequested: win.proposalOpen = false
        }

        Connections {
            target: actions
            function onProposalReady(proposalJson, captureId) {
                win.proposing = false
                win.proposalOpen = true
                proposalView.open(proposalJson, captureId)
            }
            function onProposalFailed(message) {
                win.proposing = false
                win.proposalOpen = true
                proposalView.showError(message)
            }
        }
    }
}
