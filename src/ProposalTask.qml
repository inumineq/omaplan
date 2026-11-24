import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts


// One proposed task with its fields. A field the engine guessed has an
// accent frame and says "guess". Dates show how they will be read.
Rectangle {
    id: card
    property var view
    property int col
    property int taskIndex
    property var task
    readonly property bool current: view.currentCol === col && view.currentTask === taskIndex

    implicitHeight: content.implicitHeight + 20
    radius: Math.min(4, theme.radius)
    color: theme.surface
    border.color: current ? theme.accent : theme.border

    function guessed(key) { return (task.guessed || []).indexOf(key) >= 0 }
    function focused() {
        view.currentCol = col
        view.currentTask = taskIndex
        view.selected = col
    }
    Component.onCompleted: {
        if (view.focusCol === col && view.focusTask === taskIndex) {
            view.focusCol = -1
            titleField.forceActiveFocus()
        }
    }

    component Field: ColumnLayout {
        id: field
        property string key
        property string label
        property bool isDate: false
        property alias text: textField.text
        readonly property bool guess: card.guessed(key)
        readonly property var parsed: isDate && textField.text.trim() !== "" ? Fmt.parseDate(textField.text) : null
        spacing: 2
        Layout.alignment: Qt.AlignTop
        Text {
            font.family: theme.fontFamily
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: field.label + (field.guess ? " · " + qsTr("guess") : "")
            color: field.guess ? theme.accentText : theme.muted
            font.pixelSize: Math.round(10 * theme.textScale)
            font.bold: field.guess
        }
        TextField {
            id: textField
            selectedTextColor: theme.accentForeground
            Layout.fillWidth: true
            text: card.task[field.key] || ""
            font.pixelSize: Math.round(13 * theme.textScale)
            color: theme.foreground
            selectByMouse: true
            topPadding: 4
            bottomPadding: 4
            leftPadding: 6
            background: Rectangle {
                radius: Math.min(3, theme.radius)
                color: theme.background
                border.color: textField.activeFocus ? theme.foreground : field.guess ? theme.accent : theme.border
                border.width: field.guess || textField.activeFocus ? 1.5 : 1
            }
            onTextChanged: card.view.setField(card.col, card.taskIndex, field.key, text)
            onActiveFocusChanged: if (activeFocus) card.focused()
        }
        Text {
            font.family: theme.fontFamily
            visible: field.isDate
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: Math.round(10 * theme.textScale)
            color: field.parsed && !field.parsed.ok ? theme.danger : theme.muted
            text: {
                if (!field.parsed)
                    return " "
                if (!field.parsed.ok)
                    return qsTr("not a date")
                const d = Fmt.fromIso(field.parsed.date)
                return Fmt.dayLong(d) + (field.parsed.hasTime ? " " + Fmt.time(d) : "")
            }
        }
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: titleField
                selectedTextColor: theme.accentForeground
                Layout.fillWidth: true
                text: card.task.title
                placeholderText: text === "" ? qsTr("Title") : ""
                font.pixelSize: Math.round(14 * theme.textScale)
                font.weight: Font.DemiBold
                color: theme.foreground
                selectByMouse: true
                leftPadding: 0  // flush with the fields below
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: titleField.activeFocus ? theme.accent : theme.border
                    }
                }
                onTextChanged: card.view.setField(card.col, card.taskIndex, "title", text)
                onActiveFocusChanged: if (activeFocus) card.focused()
            }
            AbstractButton {
                id: removeButton
                focusPolicy: Qt.NoFocus
                implicitWidth: 26
                implicitHeight: 26
                onClicked: card.view.removeTask(card.col, card.taskIndex)
                contentItem: Text {
                    font.family: theme.fontFamily
                    text: "×"
                    color: removeButton.hovered ? theme.danger : theme.muted
                    font.pixelSize: Math.round(18 * theme.textScale)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: null
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: card.width > 640 ? 5 : 3
            columnSpacing: 8
            rowSpacing: 4
            // Shares of the row. The short fields still hold their label in a
            // monospace font (Omarchy's), "· guess" included.
            Field { Layout.fillWidth: true; Layout.preferredWidth: 4; key: "start"; label: qsTr("Start"); isDate: true }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 2; key: "duration_min"; label: qsTr("Minutes") }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 4; key: "deadline"; label: qsTr("Deadline"); isDate: true }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 2; key: "project"; label: qsTr("Project") }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 2; key: "priority"; label: qsTr("Priority 1-3") }
        }
    }
}
