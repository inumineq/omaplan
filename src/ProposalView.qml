import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The AI engine's proposal: an editable list of tasks with guessed fields
// clearly marked. Ctrl+Enter approves (actions.guiApprove), Esc discards.
// Engine errors are shown in-window, including the raw response.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.72)
    visible: false

    // Set by open(): {opgaver: [...], uafklaret: [...]}, plus captureId/error.
    property var proposal: ({})
    property string captureId: ""
    property string error: ""

    signal approved()
    signal closeRequested()

    function open(proposalJson, capture) {
        error = ""
        captureId = capture
        const p = proposalJson
        const tasks = []
        const arr = p.opgaver || []
        for (let i = 0; i < arr.length; ++i)
            tasks.push(arr[i])
        proposal = { "opgaver": tasks, "uafklaret": p.uafklaret || [] }
        list.model = tasks
        overlay.visible = true
    }

    function showError(message) {
        error = message
        overlay.visible = true
    }

    function close() {
        overlay.visible = false
        actions.guiCancelProposal()
        overlay.closeRequested()
    }

    function fieldGuessed(task, field) {
        const g = task.gaet || []
        return g.indexOf(field) >= 0
    }

    function approve() {
        const out = { "opgaver": [], "uafklaret": proposal.uafklaret || [] }
        for (let i = 0; i < list.model.length; ++i) {
            const t = list.model[i]
            const o = { "titel": t.titel }
            if (t.noter) o.noter = t.noter
            if (t.projekt) o.projekt = t.projekt
            if (t.prioritet) o.prioritet = parseInt(t.prioritet)
            if (t.start) o.start = t.start
            if (t.varighed_min) o.varighed_min = parseInt(t.varighed_min)
            if (t.frist) o.frist = t.frist
            if ((t.gaet || []).length > 0) o.gaet = t.gaet
            out.opgaver.push(o)
        }
        const result = actions.guiApprove(out, captureId)
        if (!result.ok) {
            error = result.error
            return
        }
        overlay.visible = false
        overlay.approved()
    }

    Shortcut {
        sequence: "Ctrl+Return"
        context: Qt.WindowShortcut
        onActivated: if (overlay.visible && error === "") overlay.approve()
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        onActivated: if (overlay.visible) overlay.close()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: if (error !== "") overlay.close()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(720, parent.width - 48)
        height: Math.min(560, parent.height - 48)
        radius: Math.min(8, theme.radius)
        color: "#161618"
        border.color: theme.accent
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 10

            Label {
                text: error !== "" ? qsTr("Engine error") : qsTr("Proposal")
                font.bold: true
                font.pixelSize: 18
                color: error !== "" ? "#ff6b6b" : theme.accent
            }

            // Engine error with the raw response, shown in-window.
            ScrollView {
                visible: error !== ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    text: error
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: Material.foreground
                    font.family: "monospace"
                    background: Rectangle { color: "#0e0e10"; radius: 4 }
                }
            }

            // Editable proposal list; guessed fields are accent-colored.
            ListView {
                id: list
                visible: error === ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: []
                delegate: Rectangle {
                    id: taskRect
                    property var task: modelData
                    width: list.width
                    height: row.implicitHeight + 16
                    color: "#0e0e10"
                    radius: 4
                    border.color: (task.gaet || []).length > 0 ? theme.accent : "#2a2a2e"
                    border.width: 1

                    ColumnLayout {
                        id: row
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 4

                        TextField {
                            Layout.fillWidth: true
                            text: taskRect.task.titel
                            placeholderText: qsTr("title")
                            color: Material.foreground
                            onEditingFinished: taskRect.task.titel = text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    ["projekt", qsTr("project")],
                                    ["start", qsTr("start")],
                                    ["frist", qsTr("deadline")],
                                    ["varighed_min", qsTr("duration (min)")],
                                    ["prioritet", qsTr("priority (1-3)")],
                                ]
                                delegate: TextField {
                                    property string key: modelData[0]
                                    Layout.preferredWidth: 110
                                    text: taskRect.task[key] !== undefined ? String(taskRect.task[key]) : ""
                                    placeholderText: modelData[1]
                                    font.pixelSize: 12
                                    color: overlay.fieldGuessed(taskRect.task, key) ? theme.accent : Material.foreground
                                    onEditingFinished: taskRect.task[key] = text
                                }
                            }
                            Label {
                                visible: (taskRect.task.gaet || []).length > 0
                                text: qsTr("guessed: %1").arg((taskRect.task.gaet || []).join(", "))
                                color: theme.accent
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            Label {
                visible: error === "" && (proposal.uafklaret || []).length > 0
                text: qsTr("Unclear: %1").arg((proposal.uafklaret || []).join(" · "))
                color: Material.hintTextColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: error === "" && list.model.length === 0
                text: qsTr("No tasks proposed")
                color: Material.hintTextColor
            }
            Label {
                text: error !== "" ? qsTr("Esc close") : qsTr("Ctrl+Enter approve · Esc discard")
                color: Material.hintTextColor
                font.pixelSize: 12
            }
        }
    }
}
