import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// Tidslinje: Gantt over den aktuelle uge. Træk en bjælke til en anden dag for
// at flytte opgaven; træk i højre kant for at ændre varigheden.
Item {
    id: gantt

    function mondayOf(d) {
        var x = new Date(d.getFullYear(), d.getMonth(), d.getDate())
        x.setDate(x.getDate() - ((x.getDay() + 6) % 7))
        return x
    }
    function addDays(d, n) {
        var x = new Date(d)
        x.setDate(x.getDate() + n)
        return x
    }

    property var weekStart: mondayOf(new Date())
    readonly property real labelWidth: 220
    readonly property real dayWidth: (width - labelWidth - 16) / 7

    GanttModel {
        id: ganttModel
        sourceModel: planModel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // Dagsoverskrift
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Item { Layout.preferredWidth: gantt.labelWidth }
            Repeater {
                model: 7
                delegate: Label {
                    required property int index
                    Layout.preferredWidth: gantt.dayWidth
                    horizontalAlignment: Text.AlignHCenter
                    text: {
                        translator.language;
                        return Qt.formatDate(gantt.addDays(gantt.weekStart, index), "ddd d.")
                    }
                    color: gantt.addDays(gantt.weekStart, index).toDateString() === new Date().toDateString()
                           ? theme.accent : Material.hintTextColor
                    font.pixelSize: 12
                }
            }
        }

        ListView {
            id: rows
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: ganttModel
            delegate: RowLayout {
                id: row
                width: rows.width
                spacing: 0
                readonly property var start: model.start
                readonly property int dayOffset: Math.max(0, Math.floor((new Date(start.getFullYear(), start.getMonth(), start.getDate()) - gantt.weekStart) / 86400000))
                readonly property real barX: gantt.labelWidth + dayOffset * gantt.dayWidth
                readonly property real barW: Math.max(24, (model.durationMinutes > 0 ? model.durationMinutes / 60 / 24 : 1 / 6) * gantt.dayWidth)

                Label {
                    Layout.preferredWidth: gantt.labelWidth
                    text: model.title
                    elide: Text.ElideRight
                    color: model.status === "done" ? Material.hintTextColor : Material.foreground
                    font.pixelSize: 13
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    Rectangle {
                        id: bar
                        x: row.barX - gantt.labelWidth
                        width: row.barW
                        height: 20
                        radius: Math.min(4, theme.radius)
                        color: model.status === "done" ? "#3a3a3e" : theme.accent

                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            property real pressX
                            onPressed: pressX = mouseX
                            onReleased: {
                                var deltaDays = Math.round((mouseX - pressX) / gantt.dayWidth)
                                if (deltaDays !== 0) {
                                    var d = gantt.addDays(row.start, deltaDays)
                                    actions.guiMove(model.taskId, d, model.startHasTime)
                                }
                            }
                        }
                        MouseArea {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 12
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            property real pressX
                            onPressed: pressX = mouseX
                            onReleased: {
                                var deltaMin = Math.round((mouseX - pressX) / gantt.dayWidth * 24 * 60 / 30) * 30
                                var dur = Math.max(30, (model.durationMinutes > 0 ? model.durationMinutes : 60) + deltaMin)
                                actions.guiUpdate(model.taskId, { "duration_minutes": dur })
                            }
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: rows.count === 0
                text: qsTr("No scheduled tasks this week")
                color: Material.hintTextColor
            }
        }
    }
}
