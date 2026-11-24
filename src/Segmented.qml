import QtQuick
import QtQuick.Controls

// A labelled row of choices: "Group [Date|Project|None]". The key shows in a
// tooltip ("Group (g)"), or beside the choices in dialogs (keyInline).
Row {
    id: seg
    property string label: ""
    property string keyHint: ""
    property bool keyInline: false
    property var options: []   // [[value, text], ...]
    property string value: ""
    signal picked(string value)
    spacing: 8
    height: 32

    HoverHandler { id: rowHover }
    ToolTip.visible: rowHover.hovered && ToolTip.text !== ""
    ToolTip.delay: 600
    ToolTip.text: keyHint === "" || keyInline ? "" : (label !== "" ? label + " (" + keyHint + ")" : keyHint)

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: seg.label
        color: theme.muted
        font.pixelSize: 12
    }
    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        Repeater {
            model: seg.options
            delegate: Rectangle {
                id: choice
                required property var modelData
                required property int index
                readonly property bool active: seg.value === modelData[0]
                width: text.implicitWidth + 20
                height: 28
                color: active ? theme.accent : hover.hovered ? theme.raised : theme.surface
                border.color: active ? theme.accent : theme.border
                radius: Math.min(4, theme.radius)
                Text {
                    id: text
                    anchors.centerIn: parent
                    text: choice.modelData[1]
                    color: choice.active ? theme.accentForeground : theme.foreground
                    font.pixelSize: 12
                    font.bold: choice.active
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: seg.picked(choice.modelData[0]) }
            }
        }
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        visible: seg.keyInline && seg.keyHint !== ""
        text: seg.keyHint
        color: theme.muted
        font.pixelSize: 11
        font.family: "monospace"
    }
}
