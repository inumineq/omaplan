import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Free-text input, opened with `n` or Space. Ctrl+Enter sends the text to the
// AI engine (actions.guiPropose); Esc closes without sending.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.72)
    visible: false

    property bool busy: false

    signal submitted(string text)
    signal closeRequested()

    function open() {
        busy = false
        input.text = ""
        overlay.visible = true
        input.forceActiveFocus()
    }

    function close() {
        overlay.visible = false
    }

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.closeRequested()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(640, parent.width - 48)
        height: column.implicitHeight + 48
        radius: Math.min(8, theme.radius)
        color: "#161618"
        border.color: theme.accent
        border.width: 1

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 10

            Label {
                text: qsTr("New task (AI)")
                font.bold: true
                font.pixelSize: 18
                color: theme.accent
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                clip: true
                TextArea {
                    id: input
                    placeholderText: qsTr("e.g. write blog post on Friday 14:30, 30 min, project Blog")
                    wrapMode: TextArea.Wrap
                    color: Material.foreground
                    enabled: !overlay.busy
                    background: Rectangle { color: "#0e0e10"; radius: 4 }
                    Keys.onEscapePressed: overlay.closeRequested()
                    Keys.onReturnPressed: event => {
                        if (event.modifiers & Qt.ControlModifier && text.trim().length > 0 && !overlay.busy) {
                            overlay.submitted(text)
                            event.accepted = true
                        }
                    }
                }
            }
            Label {
                text: overlay.busy ? qsTr("Thinking…") : qsTr("Ctrl+Enter send · Esc close")
                color: Material.hintTextColor
                font.pixelSize: 12
            }
        }
    }
}
