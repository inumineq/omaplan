import QtQuick

// A small label: project, priority, "guess".
Rectangle {
    id: tag
    property string text: ""
    property color tint: theme.muted
    implicitWidth: label.implicitWidth + 12
    implicitHeight: label.implicitHeight + 4
    radius: Math.min(3, theme.radius)
    color: Qt.alpha(tag.tint, 0.14)
    border.color: Qt.alpha(tag.tint, 0.5)
    clip: true
    // User text (a project name) is shown as it is, never as markup.
    Text {
        id: label
        objectName: "tagLabel"
        font.family: theme.fontFamily
        anchors.centerIn: parent
        width: Math.min(implicitWidth, tag.width - 12)
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: tag.text
        color: tag.tint
        font.pixelSize: 11
    }
}
