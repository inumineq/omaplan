import QtQuick
import QtQuick.Controls

// Flat button with its key shown beside the label ("New task  n").
AbstractButton {
    id: button
    property string keyHint: ""
    property bool primary: false
    property bool active: false
    focusPolicy: Qt.NoFocus
    implicitHeight: 30
    leftPadding: 12
    rightPadding: 12
    contentItem: Row {
        spacing: 8
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: button.primary ? theme.accentForeground : (button.active ? theme.foreground : theme.muted)
            font.pixelSize: 13
            font.bold: button.primary || button.active
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.keyHint !== ""
            text: button.keyHint
            color: button.primary ? theme.accentForeground : theme.muted
            opacity: 0.75
            font.pixelSize: 11
            font.family: "monospace"
        }
    }
    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.primary ? theme.accent
             : button.active ? theme.raised
             : button.hovered ? theme.raised : "transparent"
        border.color: button.primary || button.active ? "transparent" : theme.border
        opacity: button.primary && button.hovered ? 0.88 : 1
    }
}
