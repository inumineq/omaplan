import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// Edit a task (e, Enter or double-click) or create one yourself without AI
// (a). Dates are written as in the CLI ("friday 10", "2026-12-01 14:30") and
// shown while typing. Enter or Ctrl+Enter saves, Esc cancels,
// Ctrl+Delete deletes.
Rectangle {
    id: editor
    color: Qt.alpha(theme.background, 0.82) // a scrim in the theme's own colour, light or dark
    visible: false

    property string taskId: ""          // "" = new task
    property var original: ({})
    property string problem: ""
    property bool done: false
    property int priority: 3

    signal saved(string message)
    signal deleted(string title)

    function isoText(text, hasTime) {
        return text ? (hasTime ? text.replace("T", " ") : text.slice(0, 10)) : ""
    }
    function openTask(id) {
        const r = actions.guiTask(id)
        if (!r.ok) {
            win.notify(r.error, true)
            return
        }
        const t = r.payload.task
        taskId = id
        original = t
        titleField.text = t.title
        startField.text = isoText(t.start, t.start_has_time)
        durationField.text = t.duration_minutes > 0 ? String(t.duration_minutes) : ""
        deadlineField.text = isoText(t.deadline, t.deadline_has_time)
        projectField.text = t.project
        notes.text = t.notes
        priority = t.priority
        done = t.status === "done"
        show()
    }
    function openNew(fields) {
        taskId = ""
        original = {}
        titleField.text = fields.title || ""
        startField.text = fields.start || ""
        durationField.text = fields.duration || ""
        deadlineField.text = fields.deadline || ""
        projectField.text = fields.project || ""
        notes.text = ""
        priority = 3
        done = false
        show()
    }
    function show() {
        problem = ""
        visible = true
        titleField.input.forceActiveFocus()
        titleField.input.selectAll()
    }
    function close() { visible = false }

    function fields() {
        return {
            "title": titleField.text, "start": startField.text, "duration": durationField.text.trim() === "" ? "0" : durationField.text,
            "deadline": deadlineField.text, "project": projectField.text, "notes": notes.text,
            "priority": String(priority), "status": done ? "done" : "todo"
        }
    }
    function save() {
        const r = taskId === "" ? actions.guiNew(fields()) : actions.guiEdit(taskId, fields())
        if (!r.ok) {
            problem = r.error
            return
        }
        win.selectedId = r.payload.task.id
        visible = false
        saved(taskId === "" ? qsTr("Added “%1”").arg(r.payload.task.title) : qsTr("Saved"))
    }
    function remove() {
        if (taskId === "")
            return
        const r = actions.guiRemove(taskId)
        if (!r.ok) {
            problem = r.error
            return
        }
        visible = false
        deleted(original.title)
    }

    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: editor.visible; onActivated: editor.save() }
    Shortcut { sequence: "Esc"; enabled: editor.visible; onActivated: editor.close() }
    Shortcut { sequence: "Ctrl+Delete"; enabled: editor.visible && editor.taskId !== ""; onActivated: editor.remove() }
    Shortcut { sequence: "Alt+1"; enabled: editor.visible; onActivated: editor.priority = 1 }
    Shortcut { sequence: "Alt+2"; enabled: editor.visible; onActivated: editor.priority = 2 }
    Shortcut { sequence: "Alt+3"; enabled: editor.visible; onActivated: editor.priority = 3 }
    Shortcut { sequence: "Alt+X"; enabled: editor.visible; onActivated: editor.done = !editor.done }

    MouseArea { anchors.fill: parent; onClicked: editor.close() }

    // A labelled field; date fields show how the text will be read.
    component Field: ColumnLayout {
        id: fieldBox
        property string label
        property bool isDate: false
        property alias input: input
        property alias text: input.text
        property string placeholder: ""
        readonly property var parsed: { Fmt.lang; return isDate && input.text.trim() !== "" ? actions.guiParseDate(input.text) : null }
        Layout.alignment: Qt.AlignTop
        spacing: 3
        Text { text: fieldBox.label; color: theme.muted; font.pixelSize: 11 }
        TextField {
            id: input
            Layout.fillWidth: true
            font.pixelSize: 14
            color: theme.foreground
            // Only while empty: Material would float it above the text.
            placeholderText: text === "" ? fieldBox.placeholder : ""
            placeholderTextColor: theme.muted
            selectByMouse: true
            leftPadding: 8
            topPadding: 6
            bottomPadding: 6
            background: Rectangle {
                radius: Math.min(3, theme.radius)
                color: theme.background
                border.color: input.activeFocus ? theme.accent : fieldBox.parsed && !fieldBox.parsed.ok ? theme.danger : theme.border
            }
            Keys.onReturnPressed: editor.save()
            Keys.onEnterPressed: editor.save()
        }
        Text {
            visible: fieldBox.isDate
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: 11
            color: fieldBox.parsed && !fieldBox.parsed.ok ? theme.danger : theme.muted
            text: {
                Fmt.lang
                if (!fieldBox.parsed)
                    return " "
                if (!fieldBox.parsed.ok)
                    return fieldBox.parsed.error
                const d = Fmt.fromIso(fieldBox.parsed.date)
                return Fmt.capitalize(Fmt.dayLong(d)) + (fieldBox.parsed.hasTime ? " " + Fmt.time(d) : "")
            }
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        height: form.implicitHeight + 40
        radius: Math.min(8, theme.radius)
        color: theme.surface
        border.color: theme.accent
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: form
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text {
                text: editor.taskId === "" ? qsTr("New task") : qsTr("Edit task")
                font.bold: true
                font.pixelSize: 18
                color: theme.foreground
            }
            Field {
                id: titleField
                Layout.fillWidth: true
                label: qsTr("Title")
                input.font.pixelSize: 16
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Field { id: startField; Layout.fillWidth: true; label: qsTr("Start"); isDate: true; placeholder: qsTr("e.g. friday 10") }
                Field {
                    id: durationField
                    Layout.preferredWidth: 120
                    label: qsTr("Minutes")
                    placeholder: "60"
                    input.validator: IntValidator { bottom: 0; top: 100000 }
                }
                Field { id: deadlineField; Layout.fillWidth: true; label: qsTr("Deadline"); isDate: true; placeholder: qsTr("e.g. 2026-12-01") }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Field { id: projectField; Layout.fillWidth: true; label: qsTr("Project") }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 3
                    Text { text: qsTr("Priority (Alt+1/2/3)"); color: theme.muted; font.pixelSize: 11 }
                    Segmented {
                        options: [["1", qsTr("High")], ["2", qsTr("Medium")], ["3", qsTr("Normal")]]
                        value: String(editor.priority)
                        onPicked: v => editor.priority = parseInt(v)
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text { text: qsTr("Notes"); color: theme.muted; font.pixelSize: 11 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    radius: Math.min(3, theme.radius)
                    color: theme.background
                    border.color: notes.activeFocus ? theme.accent : theme.border
                    ScrollView {
                        anchors.fill: parent
                        TextArea {
                            id: notes
                            wrapMode: TextArea.Wrap
                            color: theme.foreground
                            font.pixelSize: 13
                            background: null
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                CheckMark { checked: editor.done; onToggled: editor.done = !editor.done }
                Text { text: qsTr("Done (Alt+X)"); color: theme.foreground; font.pixelSize: 13 }
                Item { Layout.fillWidth: true }
                HeaderButton {
                    visible: editor.taskId !== ""
                    text: qsTr("Delete")
                    keyHint: "Ctrl+Del"
                    keyInline: true
                    onClicked: editor.remove()
                }
                HeaderButton { text: qsTr("Cancel"); keyHint: "Esc"; keyInline: true; onClicked: editor.close() }
                HeaderButton { text: qsTr("Save"); keyHint: "Enter"; keyInline: true; primary: true; onClicked: editor.save() }
            }
            Text {
                Layout.fillWidth: true
                visible: editor.problem !== ""
                text: editor.problem
                color: theme.danger
                wrapMode: Text.Wrap
            }
        }
    }
}
