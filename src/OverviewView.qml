import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Omaplan

// Overview: overdue tasks first, then today and the next 7 days, then open
// tasks without a date.
Item {
    id: view
    readonly property bool canPage: false
    function taskIds() { return overview.ids() }
    function page(step) {}
    function goToday() {}

    readonly property var today: { planModel.today; return Fmt.startOfDay(new Date()) }

    OverviewModel {
        id: overview
        sourceModel: planModel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        anchors.topMargin: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 860
            Layout.alignment: Qt.AlignHCenter
            Text {
                text: { Fmt.lang; return Fmt.capitalize(Fmt.dayLong(view.today)) }
                color: theme.foreground
                font.pixelSize: 22
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: { Fmt.lang; return qsTr("Week %1").arg(Fmt.isoWeek(view.today)) }
                color: theme.muted
                font.pixelSize: 13
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 860
            Layout.alignment: Qt.AlignHCenter
            clip: true
            spacing: 2
            model: overview
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            currentIndex: overview.ids().indexOf(win.selectedId)
            highlightFollowsCurrentItem: false
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

            section.property: "agendaSection"
            section.delegate: Item {
                id: header
                required property string section
                width: ListView.view.width
                height: label.implicitHeight + 22
                readonly property bool overdue: section === "overdue"
                readonly property var day: Fmt.fromIso(section)
                Text {
                    id: label
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    textFormat: Text.StyledText
                    font.pixelSize: 13
                    font.bold: true
                    color: header.overdue ? theme.danger : theme.accent
                    text: {
                        Fmt.lang
                        if (header.overdue)
                            return qsTr("Overdue")
                        if (header.section === "none")
                            return qsTr("No date")
                        const rel = Fmt.relativeDay(header.day, view.today)
                        const n = Fmt.daysBetween(view.today, header.day)
                        return n <= 1 ? rel + "  <font color='" + theme.muted + "'>" + Fmt.dayLong(header.day) + "</font>"
                                      : rel
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: header.overdue ? Qt.alpha(theme.danger, 0.4) : theme.border
                }
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
                // Overdue rows say which day they belonged to.
                showDate: model.agendaSection === "overdue"
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: list.count === 0
        title: qsTr("Nothing planned for the next 7 days")
    }
}
