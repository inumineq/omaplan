import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// List: every task with a checkbox. Grouping (g), project filter (f) and
// status filter (s) are visible in the toolbar and can be clicked too.
Item {
    id: root
    readonly property bool canPage: false
    function taskIds() { return list.ids() }
    function page(step) {}
    function goToday() {}

    property string grouping: "date"   // "date" | "project" | "none"
    property string status: "todo"     // "todo" | "done" | ""
    property string project: ""        // "" = all
    property var projects: []

    readonly property var today: { planModel.today; return Fmt.startOfDay(new Date()) }

    function cycleGrouping() {
        const order = ["date", "project", "none"]
        grouping = order[(order.indexOf(grouping) + 1) % order.length]
    }
    function cycleStatusFilter() {
        const order = ["todo", "done", ""]
        status = order[(order.indexOf(status) + 1) % order.length]
    }
    function cycleProjectFilter() {
        const all = [""].concat(projects)
        project = all[(all.indexOf(project) + 1) % all.length]
    }
    function refreshProjects() {
        const r = actions.guiProjects()
        projects = r.ok ? r.payload.projects.map(p => p.name) : []
        if (project !== "" && projects.indexOf(project) < 0)
            project = ""
    }
    Component.onCompleted: refreshProjects()
    Connections {
        target: planModel
        function onRevisionChanged() { root.refreshProjects() }
    }

    PlanListModel {
        id: list
        sourceModel: planModel
        projectFilter: root.project
        statusFilter: root.status
        grouping: root.grouping === "project" ? "project" : ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        anchors.topMargin: 14
        spacing: 10

        // Toolbar: every choice is visible and clickable; its key is in the tooltip and in ?.
        Flow {
            Layout.fillWidth: true
            Layout.maximumWidth: 960
            Layout.alignment: Qt.AlignHCenter
            spacing: 18

            Segmented {
                label: qsTr("Group")
                keyHint: "g"
                options: [["date", qsTr("Date")], ["project", qsTr("Project")], ["none", qsTr("None")]]
                value: root.grouping
                onPicked: v => root.grouping = v
            }
            Segmented {
                label: qsTr("Show")
                keyHint: "s"
                options: [["todo", qsTr("Open")], ["done", qsTr("Done")], ["", qsTr("All")]]
                value: root.status
                onPicked: v => root.status = v
            }
            Row {
                spacing: 8
                height: 32 // as tall as the Segmented rows beside it
                Text {
                    font.family: theme.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Project")
                    color: theme.muted
                    font.pixelSize: 12
                }
                ComboBox {
                    id: projectBox
                    anchors.verticalCenter: parent.verticalCenter
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 180
                    implicitHeight: 28
                    topInset: 0
                    bottomInset: 0
                    font.pixelSize: 12
                    // Flat like the choices beside it, not Material's outlined field.
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: projectBox.hovered ? theme.raised : theme.surface
                        border.color: theme.border
                    }
                    model: [qsTr("All projects")].concat(root.projects)
                    currentIndex: root.project === "" ? 0 : root.projects.indexOf(root.project) + 1
                    onActivated: index => root.project = index === 0 ? "" : root.projects[index - 1]
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Project") + " (f)"
                }
            }
            Text {
                font.family: theme.fontFamily
                height: 32
                verticalAlignment: Text.AlignVCenter
                text: qsTr("%n tasks", "", list.count)
                color: theme.muted
                font.pixelSize: 12
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 960
            Layout.alignment: Qt.AlignHCenter
            clip: true
            spacing: 2
            model: list
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            highlightFollowsCurrentItem: false
            currentIndex: { list.revision; return list.ids().indexOf(win.selectedId) }
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

            section.property: root.grouping === "date" ? "dayKey" : root.grouping === "project" ? "project" : ""
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                id: header
                required property var section
                width: ListView.view.width
                height: label.implicitHeight + 20
                Text {
                    id: label
                    font.family: theme.fontFamily
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    font.pixelSize: 13
                    font.bold: true
                    color: theme.accent
                    text: {
                        Fmt.lang
                        if (root.grouping === "project")
                            return header.section !== "" ? header.section : qsTr("No project")
                        const d = Fmt.fromIso(header.section)
                        return Fmt.valid(d) ? Fmt.relativeDay(d, root.today) : qsTr("No date")
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.border }
            }

            delegate: TaskRow {
                width: ListView.view.width
                taskId: model.taskId
                title: model.title
                status: model.status
                project: model.project
                priority: model.priority
                start: model.start
                startHasTime: model.startHasTime
                durationMinutes: model.durationMinutes
                deadline: model.deadline
                deadlineHasTime: model.deadlineHasTime
                showDate: root.grouping !== "date"
                showProject: root.grouping !== "project"
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: list.count === 0
        title: root.status === "done" ? qsTr("No finished tasks yet")
             : root.project !== "" ? qsTr("No tasks in %1").arg(root.project)
             : qsTr("No open tasks")
        hint: root.status !== "" || root.project !== "" ? qsTr("Press s or f to change the filter.")
                                                         : qsTr("Press n to write tasks in plain text, or a to add one by hand.")
    }
}
