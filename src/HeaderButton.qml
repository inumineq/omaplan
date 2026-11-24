import QtQuick
import QtQuick.Controls

// Flat button. Its key shows in a tooltip ("Today (t)"), as in Omarchy's
// apps; dialogs, where the keys are the main way in, show it beside the
// label instead ("Save  Enter").
AbstractButton {
    id: button
    property string keyHint: ""
    property bool keyInline: false
    property string toolTip: ""      // what the tooltip names; the label by default
    property bool primary: false
    property bool active: false
    // In the settings panel, Tab reaches the button and Space/Enter presses it.
    property bool focusable: false
    focusPolicy: focusable ? Qt.StrongFocus : Qt.NoFocus
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    implicitHeight: 30
    leftPadding: 12
    rightPadding: 12
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600
    ToolTip.text: keyHint === "" || keyInline ? toolTip
                : (toolTip !== "" ? toolTip : text) + " (" + keyHint + ")"
    contentItem: Row {
        spacing: 8
        Text {
            font.family: theme.fontFamily
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: button.primary ? theme.accentForeground : (button.active ? theme.foreground : theme.muted)
            font.pixelSize: 13
            font.bold: button.primary || button.active
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.keyInline && button.keyHint !== ""
            text: button.keyHint
            color: button.primary ? theme.accentForeground : theme.muted
            opacity: 0.75
            font.pixelSize: 11
            font.family: theme.fontFamily
        }
    }
    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.primary ? theme.accent
             : button.active ? theme.raised
             : button.hovered ? theme.raised : "transparent"
        border.color: button.visualFocus ? theme.foreground : button.primary || button.active ? "transparent" : theme.border
        border.width: button.visualFocus ? 2 : 1
        opacity: button.primary && button.hovered ? 0.88 : 1
    }
}
