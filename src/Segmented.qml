import QtQuick
import QtQuick.Controls

// A labelled row of choices: "Group [Date|Project|None]". The key shows in a
// tooltip ("Group (g)"), or beside the choices in dialogs (keyInline).
// With focusable, Tab reaches it and ← / → pick the previous / next choice.
Row {
    id: seg
    property string label: ""
    property string keyHint: ""
    property bool keyInline: false
    property var options: []   // [[value, text], ...]
    property string value: ""
    property bool focusable: false
    signal picked(string value)
    spacing: 8
    height: 32
    activeFocusOnTab: focusable

    function step(delta) {
        let i = options.findIndex(o => o[0] === value)
        i = Math.max(0, Math.min(options.length - 1, (i < 0 ? 0 : i + delta)))
        if (options.length && options[i][0] !== value)
            picked(options[i][0])
    }
    Keys.onLeftPressed: step(Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1)
    Keys.onRightPressed: step(Qt.application.layoutDirection === Qt.RightToLeft ? -1 : 1)

    HoverHandler { id: rowHover }
    ToolTip.visible: rowHover.hovered && ToolTip.text !== ""
    ToolTip.delay: 600
    ToolTip.text: keyHint === "" || keyInline ? "" : (label !== "" ? label + " (" + keyHint + ")" : keyHint)

    Text {
        font.family: theme.fontFamily
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
                // Keyboard focus: the chosen one gets a foreground frame.
                border.color: active && seg.activeFocus ? theme.foreground : active ? theme.accent : theme.border
                border.width: active && seg.activeFocus ? 2 : 1
                radius: Math.min(4, theme.radius)
                Text {
                    id: text
                    font.family: theme.fontFamily
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
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
        font.family: theme.fontFamily
    }
}
