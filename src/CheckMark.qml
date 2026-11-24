import QtQuick

// The done box on a task. Clicking toggles; `x` does the same from the keyboard.
Rectangle {
    id: box
    property bool checked: false
    signal toggled()
    implicitWidth: 18
    implicitHeight: 18
    radius: Math.min(3, theme.radius)
    color: checked ? theme.accent : "transparent"
    border.color: checked ? theme.accent : (hover.hovered ? theme.foreground : theme.muted)
    border.width: 1.5
    Text {
        font.family: theme.fontFamily
        anchors.centerIn: parent
        visible: box.checked
        text: "✓"
        color: theme.accentForeground
        font.pixelSize: Math.round(13 * theme.textScale)
        font.bold: true
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: box.toggled() }
}
