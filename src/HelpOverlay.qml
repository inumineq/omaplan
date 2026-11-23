import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Genvejsoversigt, vist med `?`.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.72)
    visible: false

    signal closeRequested()

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.closeRequested()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 48)
        height: column.implicitHeight + 48
        radius: Math.min(8, theme.radius)
        color: "#161618"
        border.color: theme.accent
        border.width: 1

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 6

            Label {
                text: qsTr("Keys")
                font.bold: true
                font.pixelSize: 18
                color: theme.accent
            }
            Repeater {
                model: [
                    ["1 / 2 / 3 / 4", qsTr("Switch view: overview, calendar, list, timeline")],
                    ["w / m", qsTr("Calendar: week / month")],
                    ["g", qsTr("List: cycle grouping (none, date, project)")],
                    ["f", qsTr("List: cycle project filter")],
                    ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
                    ["?", qsTr("This help")],
                    ["q", qsTr("Quit")],
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: modelData[0]
                        font.bold: true
                        color: Material.foreground
                        Layout.preferredWidth: 180
                    }
                    Label {
                        text: modelData[1]
                        color: Material.hintTextColor
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Label {
                text: qsTr("Click anywhere to close")
                color: Material.hintTextColor
                font.pixelSize: 12
                Layout.topMargin: 8
            }
        }
    }
}
