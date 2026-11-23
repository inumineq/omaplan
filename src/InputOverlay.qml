import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// Fri tekst, åbnet med n eller Space. Ctrl+Enter sender til AI-motoren; med
// Sammenlign (Alt+S) går teksten til to motorer på én gang. Mens motoren
// arbejder, står vinduet åbent med en tæller, og Esc afbryder. Teksten
// bevares, til et forslag er godkendt, så en fejl eller et afbrud aldrig
// koster det, man har skrevet.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.6)
    visible: false

    property bool busy: false
    property bool compare: false
    property string draft: ""
    property int seconds: 0
    property var engines: ({ "default": "", "compare": ["", ""] })

    signal compareRequested(string text)
    signal proposalReady(string text, var proposal, string captureId)
    signal proposalFailed(string text, string message)

    function open(text) {
        engines = actions.guiEngineNames()
        if (text !== undefined)
            draft = text
        input.text = draft
        busy = false
        visible = true
        input.forceActiveFocus()
        input.cursorPosition = input.length
    }
    function close() {
        if (busy)
            actions.guiCancelProposal()
        draft = input.text
        busy = false
        visible = false
    }
    function clearDraft() { draft = "" }

    function submit() {
        const text = input.text.trim()
        if (text === "" || busy)
            return
        draft = input.text
        if (compare) {
            visible = false
            compareRequested(text)
            return
        }
        busy = true
        seconds = 0
        actions.guiPropose(text, "")
    }

    Connections {
        target: actions
        enabled: overlay.busy
        function onProposalReady(proposal, captureId) {
            overlay.busy = false
            overlay.visible = false
            overlay.proposalReady(overlay.draft, proposal, captureId)
        }
        function onProposalFailed(message) {
            overlay.busy = false
            overlay.visible = false
            overlay.proposalFailed(overlay.draft, message)
        }
    }
    Timer {
        interval: 1000
        repeat: true
        running: overlay.busy
        onTriggered: overlay.seconds += 1
    }

    // Clicking outside closes (keeps the draft); not while the engine works.
    MouseArea {
        anchors.fill: parent
        onClicked: if (!overlay.busy) overlay.close()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(680, parent.width - 48)
        height: column.implicitHeight + 40
        radius: Math.min(8, theme.radius)
        color: theme.surface
        border.color: theme.accent
        border.width: 1
        MouseArea { anchors.fill: parent } // keeps clicks inside from closing

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("New tasks")
                    font.bold: true
                    font.pixelSize: 18
                    color: theme.foreground
                }
                Item { Layout.fillWidth: true }
                Segmented {
                    keyHint: "Alt+S"
                    options: [["single", qsTr("One engine")], ["compare", qsTr("Compare")]]
                    value: overlay.compare ? "compare" : "single"
                    onPicked: v => { overlay.compare = v === "compare"; input.forceActiveFocus() }
                    opacity: overlay.busy ? 0.5 : 1
                    enabled: !overlay.busy
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                radius: Math.min(4, theme.radius)
                color: theme.background
                border.color: input.activeFocus ? theme.accent : theme.border
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 2
                    clip: true
                    TextArea {
                        id: input
                        placeholderText: qsTr("Write or dictate freely, e.g. “blog post on Friday 14:30, 1 hour, project Blog; call Customer A Tuesday morning”")
                        placeholderTextColor: theme.muted
                        wrapMode: TextArea.Wrap
                        color: theme.foreground
                        font.pixelSize: 15
                        readOnly: overlay.busy
                        background: null
                        Keys.onPressed: event => {
                            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                            if (enter && (event.modifiers & Qt.ControlModifier)) {
                                overlay.submit()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape) {
                                overlay.close()
                                event.accepted = true
                            } else if (event.key === Qt.Key_S && (event.modifiers & Qt.AltModifier) && !overlay.busy) {
                                overlay.compare = !overlay.compare
                                event.accepted = true
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                BusyIndicator {
                    visible: overlay.busy
                    running: overlay.busy
                    implicitWidth: 28
                    implicitHeight: 28
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: overlay.busy ? theme.foreground : theme.muted
                    text: overlay.busy
                          ? qsTr("%1 is working… %2 s · Esc cancels").arg(overlay.engines["default"]).arg(overlay.seconds)
                          : overlay.compare
                            ? qsTr("Compare: %1 and %2 side by side").arg(overlay.engines.compare[0]).arg(overlay.engines.compare[1])
                            : qsTr("Engine: %1").arg(overlay.engines["default"])
                }
                Text {
                    visible: !overlay.busy
                    text: qsTr("Ctrl+Enter send · Esc close")
                    color: theme.muted
                    font.pixelSize: 12
                }
            }
        }
    }
}
