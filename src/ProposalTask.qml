import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

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

    function guessed(key) { return (task.gaet || []).indexOf(key) >= 0 }
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
        property alias text: input.text
        property alias input: input
        readonly property bool guess: card.guessed(key)
        readonly property var parsed: { Fmt.lang; return isDate && input.text.trim() !== "" ? actions.guiParseDate(input.text) : null }
        spacing: 2
        Layout.alignment: Qt.AlignTop
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: field.label + (field.guess ? " · " + qsTr("guess") : "")
            color: field.guess ? theme.accent : theme.muted
            font.pixelSize: 10
            font.bold: field.guess
        }
        TextField {
            id: input
            Layout.fillWidth: true
            text: card.task[field.key] || ""
            font.pixelSize: 13
            color: theme.foreground
            selectByMouse: true
            topPadding: 4
            bottomPadding: 4
            leftPadding: 6
            background: Rectangle {
                radius: Math.min(3, theme.radius)
                color: theme.background
                border.color: input.activeFocus ? theme.foreground : field.guess ? theme.accent : theme.border
                border.width: field.guess || input.activeFocus ? 1.5 : 1
            }
            onTextChanged: card.view.setField(card.col, card.taskIndex, field.key, text)
            onActiveFocusChanged: if (activeFocus) card.focused()
        }
        Text {
            visible: field.isDate
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: 10
            color: field.parsed && !field.parsed.ok ? theme.danger : theme.muted
            text: {
                Fmt.lang
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
                Layout.fillWidth: true
                text: card.task.titel
                placeholderText: text === "" ? qsTr("Title") : ""
                font.pixelSize: 15
                font.bold: true
                color: theme.foreground
                selectByMouse: true
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: titleField.activeFocus ? theme.accent : theme.border
                    }
                }
                onTextChanged: card.view.setField(card.col, card.taskIndex, "titel", text)
                onActiveFocusChanged: if (activeFocus) card.focused()
            }
            AbstractButton {
                focusPolicy: Qt.NoFocus
                implicitWidth: 26
                implicitHeight: 26
                onClicked: card.view.removeTask(card.col, card.taskIndex)
                contentItem: Text {
                    text: "×"
                    color: parent.hovered ? theme.danger : theme.muted
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: null
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: card.width > 560 ? 5 : 3
            columnSpacing: 8
            rowSpacing: 4
            Field { Layout.fillWidth: true; Layout.preferredWidth: 3; key: "start"; label: qsTr("Start"); isDate: true }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 1; key: "varighed_min"; label: qsTr("Minutes") }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 3; key: "frist"; label: qsTr("Deadline"); isDate: true }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 2; key: "projekt"; label: qsTr("Project") }
            Field { Layout.fillWidth: true; Layout.preferredWidth: 1; key: "prioritet"; label: qsTr("Priority 1-3") }
        }
    }
}
