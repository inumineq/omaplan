import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// One engine's answer inside ProposalView: waiting, an error with the raw
// response, or the editable tasks.
Rectangle {
    id: column
    property int col: 0
    property var view
    readonly property var info: { view.colRevision[col]; view.revision; return view.columns[col] || ({ state: "waiting", tasks: [], unclear: [] }) }
    readonly property bool chosen: view.comparing && view.selected === col

    radius: Math.min(6, theme.radius)
    color: theme.background
    border.color: chosen ? theme.accent : theme.border
    border.width: chosen ? 2 : 1

    TapHandler { onTapped: if (column.view.comparing) column.view.selected = column.col }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            visible: column.view.comparing || column.info.state === "error"
            Text {
                font.family: theme.fontFamily
                text: column.info.engine || ""
                color: column.chosen ? theme.accent : theme.foreground
                font.bold: true
                font.pixelSize: 14
            }
            Text {
                font.family: theme.fontFamily
                text: column.info.state === "ready" ? qsTr("%n tasks", "", column.info.tasks.length)
                    : column.info.state === "error" ? qsTr("failed") : qsTr("working…")
                color: column.info.state === "error" ? theme.danger : theme.muted
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: column.view.comparing && column.info.state === "ready"
                text: qsTr("Ctrl+%1 approve").arg(column.col + 1)
                color: theme.muted
                font.pixelSize: 11
                font.family: theme.fontFamily
            }
        }

        // Waiting for the engine.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: column.info.state === "waiting"
            Item { Layout.fillHeight: true }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: parent.visible }
            Text {
                font.family: theme.fontFamily
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Waiting for %1…").arg(column.info.engine)
                color: theme.muted
            }
            Item { Layout.fillHeight: true }
        }

        // The engine failed: what went wrong, including its raw answer.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: column.info.state === "error"
            clip: true
            TextArea {
                text: column.info.error || ""
                readOnly: true
                selectByMouse: true
                wrapMode: TextArea.Wrap
                color: theme.foreground
                font.family: theme.fontFamily
                font.pixelSize: 12
                background: null
            }
        }

        // The tasks, editable.
        ListView {
            id: tasks
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: column.info.state === "ready"
            clip: true
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: { column.view.colRevision[column.col]; column.view.revision; return column.info.tasks.slice() }
            delegate: ProposalTask {
                required property int index
                required property var modelData
                width: tasks.width - 8
                view: column.view
                col: column.col
                taskIndex: index
                task: modelData
            }
            footer: Item {
                width: tasks.width
                height: 44
                HeaderButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("+ Add task")
                    keyHint: "Ctrl+N"
                    keyInline: true
                    onClicked: column.view.addTask(column.col)
                }
            }
        }

        Text {
            font.family: theme.fontFamily
            Layout.fillWidth: true
            visible: column.info.state === "ready" && (column.info.unclear || []).length > 0
            text: qsTr("Not understood: %1").arg((column.info.unclear || []).join(" · "))
            color: theme.muted
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
    }
}
