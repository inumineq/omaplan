import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every key in the window, shown with ?. Esc, ? or a click closes it.
// Laid out like Omacut's and Folio's: keys in accent monospace, actions in the
// foreground, small capital group headings.
Rectangle {
    id: overlay
    color: Qt.alpha(theme.background, 0.82) // a scrim in the theme's own colour, light or dark
    visible: false

    signal closeRequested()

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.closeRequested()
    }

    readonly property var groups: [
        [qsTr("Everywhere"), [
            ["1  2  3  4", qsTr("Overview, calendar, list, timeline")],
            ["n / Space", qsTr("New tasks in plain text (AI)")],
            ["a", qsTr("New task by hand")],
            ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo (also changes from the CLI or an agent)")],
            ["?", qsTr("This overview")],
            ["q", qsTr("Quit")]]],
        [qsTr("Selected task"), [
            ["j / k  ↓ / ↑", qsTr("Select next / previous")],
            ["x", qsTr("Done / not done")],
            ["e / Enter", qsTr("Edit (or double-click)")],
            ["Shift+← / →", qsTr("Move a day earlier / later")],
            ["Shift+↑ / ↓", qsTr("Move a quarter hour earlier / later")],
            ["Delete / d", qsTr("Delete")]]],
        [qsTr("Calendar and timeline"), [
            ["h / l  ← / →", qsTr("Previous / next week or month")],
            ["t", qsTr("Go to today")],
            ["w / m", qsTr("Calendar: week / month")],
            ["+ / -", qsTr("Timeline: zoom in / out")],
            [qsTr("drag"), qsTr("Move to another day or time; drag the right edge of a bar for duration")]]],
        [qsTr("List"), [
            ["g", qsTr("Group by date, project or none")],
            ["s", qsTr("Show open, done or all")],
            ["f", qsTr("Filter by project")]]],
        [qsTr("Writing and proposals"), [
            ["Ctrl+Enter", qsTr("Send / approve / save")],
            ["Alt+S", qsTr("Compare two engines")],
            ["Ctrl+1 / Ctrl+2", qsTr("Approve the left / right proposal")],
            ["Alt+← / →", qsTr("Choose the left / right proposal")],
            ["Ctrl+N / Ctrl+Delete", qsTr("Add / remove a task in the proposal")],
            ["Tab", qsTr("Next field in the proposal")],
            ["Ctrl+R / Ctrl+E", qsTr("After an engine error: try again / create it yourself")],
            ["Esc", qsTr("Close / discard / cancel the engine")]]],
        [qsTr("Task editor"), [
            ["Ctrl+Enter / Enter", qsTr("Save")],
            ["Alt+1 / Alt+2 / Alt+3", qsTr("Priority: high, medium, normal")],
            ["Alt+X", qsTr("Done / not done")],
            ["Ctrl+Delete", qsTr("Delete the task")],
            ["Esc", qsTr("Cancel")]]]
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(900, parent.width - 48)
        height: Math.min(content.implicitHeight + 48, parent.height - 48)
        radius: theme.radius
        color: theme.surface
        border.color: theme.accent

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        font.family: theme.fontFamily
                        text: qsTr("Keyboard shortcuts")
                        font.weight: Font.DemiBold
                        font.pixelSize: 16
                        color: theme.foreground
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        font.family: theme.fontFamily
                        text: qsTr("Esc closes")
                        color: theme.muted
                        font.pixelSize: 12
                    }
                }
                // The groups lie in two columns so every key stays on one
                // screen; the Flickable still takes over on shorter windows.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 28
                    Repeater {
                        model: [overlay.groups.slice(0, 3), overlay.groups.slice(3)]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 2
                            Repeater {
                                model: parent.modelData
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.topMargin: 8
                                    spacing: 3
                                    Text {
                                        font.family: theme.fontFamily
                                        text: parent.modelData[0].toUpperCase()
                                        color: theme.muted
                                        font.weight: Font.DemiBold
                                        font.pixelSize: 11
                                        font.letterSpacing: 1.2
                                    }
                                    Repeater {
                                        model: parent.modelData[1]
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            spacing: 12
                                            Text {
                                                Layout.preferredWidth: 160
                                                text: parent.modelData[0]
                                                color: theme.accent
                                                font.family: theme.fontFamily
                                                font.pixelSize: 12
                                            }
                                            Text {
                                                font.family: theme.fontFamily
                                                Layout.fillWidth: true
                                                text: parent.modelData[1]
                                                color: theme.foreground
                                                font.pixelSize: 12
                                                wrapMode: Text.Wrap
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
