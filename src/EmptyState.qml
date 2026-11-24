import QtQuick
import QtQuick.Layouts

// What a view says when it has nothing to show, and what to do about it.
ColumnLayout {
    property string title: ""
    property string hint: qsTr("Press n to write tasks in plain text, or a to add one by hand.")
    spacing: 6
    Text {
        font.family: theme.fontFamily
        Layout.alignment: Qt.AlignHCenter
        text: parent.title
        color: theme.foreground
        font.pixelSize: Math.round(16 * theme.textScale)
    }
    Text {
        font.family: theme.fontFamily
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 420
        text: parent.hint
        color: theme.muted
        font.pixelSize: Math.round(13 * theme.textScale)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }
}
