import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// AI-motorens forslag som en redigerbar liste, før noget gemmes. Felter, som
// motoren har gættet, er markeret. Man retter direkte i felterne (datoer som i
// CLI'en: "fredag 10"), sletter med Ctrl+Delete og tilføjer med Ctrl+N.
// Ctrl+Enter godkender, Esc kasserer. I Sammenlign står to motorers forslag
// side om side; Ctrl+1 / Ctrl+2 godkender det ene (Alt+←/→ vælger).
// Motorfejl vises her med motorens råsvar.
Rectangle {
    id: overlay
    color: Qt.alpha(theme.background, 0.82) // a scrim in the theme's own colour, light or dark
    visible: false

    // One entry per engine: {engine, state: "waiting"|"ready"|"error",
    // tasks: [...], unclear: [...], captureId, error}.
    property var columns: []
    property int revision: 0            // the set of columns changed
    property var colRevision: [0, 0]    // one column's content changed
    property int selected: 0
    property string text: ""            // the raw input, for retry / create yourself
    property int currentCol: 0
    property int currentTask: -1
    property int focusCol: -1
    property int focusTask: -1
    property string problem: ""         // why approve was refused
    readonly property bool comparing: columns.length === 2

    signal approved(int count)
    signal retryRequested(string text)
    signal createYourselfRequested(string text)

    // "2026-11-27T14:30:00" -> "2026-11-27 14:30", the form the fields read.
    function editable(iso) {
        const m = /^(\d{4}-\d\d-\d\d)(?:T(\d\d:\d\d))?/.exec(iso || "")
        return m ? m[1] + (m[2] ? " " + m[2] : "") : (iso || "")
    }
    function columnFrom(engine, proposal, captureId) {
        const tasks = (proposal.tasks || []).map(t => ({
            "title": t.title || "",
            "start": overlay.editable(t.start),
            "duration_min": t.duration_min ? String(t.duration_min) : "",
            "deadline": overlay.editable(t.deadline),
            "project": t.project || "",
            "priority": t.priority ? String(t.priority) : "",
            "notes": t.notes || "",
            "guessed": t.guessed || []
        }))
        return { engine: engine, state: "ready", tasks: tasks, unclear: proposal.unresolved || [],
                 captureId: captureId, error: "" }
    }
    function bump(col) {
        const r = colRevision.slice()
        r[col] += 1
        colRevision = r
    }
    function show() {
        problem = ""
        currentTask = -1
        revision += 1
        visible = true
        overlay.forceActiveFocus()
    }

    function openSingle(text, proposal, captureId) {
        overlay.text = text
        columns = [columnFrom(actions.guiEngineNames()["default"], proposal, captureId)]
        selected = 0
        show()
    }
    function showSingleError(text, message) {
        overlay.text = text
        columns = [{ engine: actions.guiEngineNames()["default"], state: "error", tasks: [], unclear: [],
                     captureId: "", error: message }]
        selected = 0
        show()
    }
    function startCompare(text) {
        overlay.text = text
        const names = actions.guiEngineNames().compare
        columns = [0, 1].map(i => ({ engine: names[i], state: "waiting", tasks: [], unclear: [], captureId: "", error: "" }))
        selected = 0
        show()
        actions.guiCompare(text)
    }
    Connections {
        target: actions
        enabled: overlay.visible && overlay.comparing
        function onCompareReady(side, proposal, captureId, engine) {
            const cols = overlay.columns
            cols[side] = overlay.columnFrom(engine, proposal, captureId)
            if (cols[overlay.selected].state !== "ready")
                overlay.selected = side
            overlay.bump(side)
        }
        function onCompareFailed(side, message, engine) {
            const cols = overlay.columns
            cols[side] = { engine: engine, state: "error", tasks: [], unclear: [], captureId: "", error: message }
            overlay.bump(side)
        }
    }

    function close() {
        actions.guiCancelProposal()
        visible = false
    }

    function setField(col, task, key, value) {
        columns[col].tasks[task][key] = value
    }
    function addTask(col) {
        if (!columns[col] || columns[col].state !== "ready")
            return
        columns[col].tasks.push({ "title": "", "start": "", "duration_min": "", "deadline": "", "project": "",
                                  "priority": "", "notes": "", "guessed": [] })
        focusCol = col
        focusTask = columns[col].tasks.length - 1
        bump(col)
    }
    function removeTask(col, task) {
        if (!columns[col] || task < 0 || task >= columns[col].tasks.length)
            return
        columns[col].tasks.splice(task, 1)
        focusCol = col
        focusTask = Math.min(task, columns[col].tasks.length - 1)
        currentTask = -1
        bump(col)
    }

    function approve(col) {
        const c = columns[col]
        if (!c || c.state !== "ready")
            return
        const out = { "tasks": [] }
        for (const t of c.tasks) {
            if (t.title.trim() === "")
                continue
            const o = { "title": t.title.trim() }
            if (t.notes) o.notes = t.notes
            if (t.project.trim()) o.project = t.project.trim()
            if (t.start.trim()) o.start = t.start.trim()
            if (t.deadline.trim()) o.deadline = t.deadline.trim()
            if (t.duration_min.trim()) {
                if (!/^\d+$/.test(t.duration_min.trim())) {
                    problem = qsTr("“%1”: duration must be whole minutes").arg(t.title)
                    return
                }
                o.duration_min = parseInt(t.duration_min)
            }
            if (t.priority.trim()) {
                if (!/^[123]$/.test(t.priority.trim())) {
                    problem = qsTr("“%1”: priority must be 1, 2 or 3").arg(t.title)
                    return
                }
                o.priority = parseInt(t.priority)
            }
            if (t.guessed.length) o.guessed = t.guessed
            out.tasks.push(o)
        }
        if (out.tasks.length === 0) {
            problem = qsTr("Nothing to approve: every task was removed")
            return
        }
        const result = actions.guiApprove(out, c.captureId)
        if (!result.ok) {
            problem = result.error
            return
        }
        actions.guiCancelProposal() // the other engine may still be working
        visible = false
        approved(result.payload.count)
    }

    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: overlay.visible; onActivated: overlay.approve(overlay.selected) }
    Shortcut { sequence: "Ctrl+1"; enabled: overlay.visible && overlay.comparing; onActivated: overlay.approve(0) }
    Shortcut { sequence: "Ctrl+2"; enabled: overlay.visible && overlay.comparing; onActivated: overlay.approve(1) }
    Shortcut { sequence: "Alt+Left"; enabled: overlay.visible && overlay.comparing; onActivated: overlay.selected = 0 }
    Shortcut { sequence: "Alt+Right"; enabled: overlay.visible && overlay.comparing; onActivated: overlay.selected = 1 }
    Shortcut { sequence: "Ctrl+N"; enabled: overlay.visible; onActivated: overlay.addTask(overlay.selected) }
    Shortcut { sequence: "Ctrl+Delete"; enabled: overlay.visible && overlay.currentTask >= 0
        onActivated: overlay.removeTask(overlay.currentCol, overlay.currentTask) }
    Shortcut { sequence: "Esc"; enabled: overlay.visible; onActivated: overlay.close() }
    Shortcut { sequence: "Ctrl+R"; enabled: overlay.visible
        onActivated: { overlay.close(); overlay.retryRequested(overlay.text) } }
    Shortcut { sequence: "Ctrl+E"; enabled: overlay.visible
        onActivated: { overlay.close(); overlay.createYourselfRequested(overlay.text) } }

    MouseArea { anchors.fill: parent } // modal: clicks do not reach the views

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(overlay.comparing ? 1240 : 780, parent.width - 32)
        height: parent.height - 48
        radius: Math.min(8, theme.radius)
        color: theme.surface
        border.color: theme.accent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: overlay.comparing ? qsTr("Compare proposals") : qsTr("Proposal")
                    font.bold: true
                    font.pixelSize: 18
                    color: theme.foreground
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    text: "“" + overlay.text + "”"
                    elide: Text.ElideRight
                    color: theme.muted
                    font.pixelSize: 13
                    font.italic: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                Repeater {
                    model: { overlay.revision; return overlay.columns.length }
                    delegate: ProposalColumn {
                        required property int index
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        col: index
                        view: overlay
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: overlay.problem !== ""
                text: overlay.problem
                color: theme.danger
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: 12
                text: {
                    const anyReady = overlay.columns.some(c => c.state === "ready")
                    if (!anyReady && overlay.columns.every(c => c.state === "error"))
                        return qsTr("Ctrl+R try again · Ctrl+E create the task yourself · Esc close")
                    return overlay.comparing
                        ? qsTr("Ctrl+1 / Ctrl+2 approve left / right · Alt+←/→ choose, Ctrl+Enter approves it · Ctrl+N add · Ctrl+Delete remove · Esc discard")
                        : qsTr("Ctrl+Enter approve · Tab next field · Ctrl+N add · Ctrl+Delete remove · Esc discard")
                }
            }
        }
    }
}
