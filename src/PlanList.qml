import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// Liste: alle opgaver med afkrydsning; gruppér med `g`, filtrér på projekt
// med `f`.
Item {
    id: root

    property int grouping: 0 // 0 = ingen, 1 = dato, 2 = projekt
    property var projects: []
    property int projectIndex: 0 // 0 = alle

    readonly property string statusText: {
        var g = [qsTr("no grouping"), qsTr("grouped by date"), qsTr("grouped by project")][root.grouping]
        var f = root.projectIndex === 0 ? qsTr("all projects") : root.projects[root.projectIndex - 1]
        return qsTr("Group: %1 · Filter: %2").arg(g).arg(f)
    }

    function cycleGrouping() { grouping = (grouping + 1) % 3 }
    function cycleProjectFilter() {
        var n = projects.length + 1
        projectIndex = (projectIndex + 1) % n
    }
    function refreshProjects() {
        var r = actions.guiProjects()
        projects = r.ok ? r.payload.projects.map(function (p) { return p.name }) : []
        if (projectIndex >= projects.length + 1)
            projectIndex = 0
    }
    Component.onCompleted: refreshProjects()
    Connections {
        target: actions
        function onPlanChanged() { root.refreshProjects() }
    }

    PlanListModel {
        id: list
        sourceModel: planModel
        projectFilter: root.projectIndex === 0 ? "" : (root.projects[root.projectIndex - 1] || "")
    }

    ListView {
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        model: list
        section.property: root.grouping === 1 ? "startDate" : root.grouping === 2 ? "project" : ""
        section.delegate: Label {
            required property var section
            topPadding: 10
            bottomPadding: 4
            text: {
                translator.language;
                if (root.grouping === 1) {
                    if (!section || section.toString() === "Invalid Date")
                        return qsTr("No date")
                    return Qt.formatDate(new Date(section), "dddd d. MMMM")
                }
                return section !== "" ? section : qsTr("No project")
            }
            color: theme.accent
            font.bold: true
        }
        delegate: RowLayout {
            width: ListView.view.width
            spacing: 8
            CheckBox {
                checked: model.status === "done"
                onClicked: actions.guiSetDone(model.taskId, checked)
            }
            Label {
                Layout.fillWidth: true
                text: model.title
                elide: Text.ElideRight
                color: model.status === "done" ? Material.hintTextColor : Material.foreground
                font.strikeout: model.status === "done"
            }
            Label {
                text: model.project
                visible: root.grouping !== 2 && model.project !== ""
                color: Material.hintTextColor
                font.pixelSize: 12
            }
            Label {
                text: model.startHasTime ? Qt.formatDateTime(model.start, "d. MMM HH:mm") : Qt.formatDate(model.start, "d. MMM")
                visible: model.start.toString() !== "Invalid Date"
                color: Material.hintTextColor
                font.pixelSize: 12
            }
        }
        Label {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTr("No tasks")
            color: Material.hintTextColor
        }
    }
}
