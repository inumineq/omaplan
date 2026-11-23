import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// Kalender: uge eller måned. I ugevisning kan opgaver trækkes til en anden dag
// (enkel træk-og-slip: markér måldagen mens du trækker).
Item {
    id: cal
    property string mode: "week"

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
    property var monthStart: new Date(new Date().getFullYear(), new Date().getMonth(), 1)

    CalendarModel {
        id: weekModel
        sourceModel: planModel
        from: cal.weekStart
        to: cal.addDays(cal.weekStart, 6)
    }
    CalendarModel {
        id: monthModel
        sourceModel: planModel
        from: cal.monthStart
        to: cal.addDays(cal.monthStart, 41)
    }

    // Ugevisning: syv kolonner.
    RowLayout {
        id: weekRow
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        visible: cal.mode === "week"

        property int hoverDay: -1
        readonly property real colWidth: (width - 6 * spacing) / 7

        Repeater {
            model: 7
            delegate: Rectangle {
                id: dayCol
                required property int index
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Math.min(4, theme.radius)
                color: weekRow.hoverDay === index ? "#232327" : "#141416"
                border.color: "#26262a"
                property var dayDate: cal.addDays(cal.weekStart, index)
                property bool isToday: dayDate.toDateString() === new Date().toDateString()

                CalendarModel {
                    id: dayModel
                    sourceModel: planModel
                    from: dayCol.dayDate
                    to: dayCol.dayDate
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: {
                            translator.language;
                            return Qt.formatDate(dayCol.dayDate, "ddd d.") 
                        }
                        color: dayCol.isToday ? theme.accent : Material.foreground
                        font.bold: dayCol.isToday
                        horizontalAlignment: Text.AlignHCenter
                    }
                    ListView {
                        id: dayList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: dayModel
                        delegate: Rectangle {
                            id: chip
                            width: dayList.width
                            implicitHeight: chipText.implicitHeight + 10
                            radius: Math.min(4, theme.radius)
                            color: model.status === "done" ? "#1d1d20" : theme.accent
                            border.color: "#26262a"

                            Label {
                                id: chipText
                                anchors.fill: parent
                                anchors.margins: 5
                                text: (model.startHasTime ? Qt.formatTime(model.start, "HH:mm") + " " : "") + model.title
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                color: model.status === "done" ? Material.hintTextColor : theme.accentForeground
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                onPressed: weekRow.hoverDay = -1
                                onPositionChanged: {
                                    if (!pressed) return
                                    var p = mapToItem(weekRow, mouseX, mouseY)
                                    var col = Math.floor(p.x / weekRow.colWidth)
                                    weekRow.hoverDay = Math.max(0, Math.min(6, col))
                                }
                                onReleased: {
                                    var p = mapToItem(weekRow, mouseX, mouseY)
                                    var col = Math.floor(p.x / weekRow.colWidth)
                                    weekRow.hoverDay = -1
                                    if (col >= 0 && col < 7 && col !== dayCol.index) {
                                        var d = cal.addDays(cal.weekStart, col)
                                        var when = model.startHasTime
                                                ? new Date(d.getFullYear(), d.getMonth(), d.getDate(), model.start.getHours(), model.start.getMinutes())
                                                : new Date(d.getFullYear(), d.getMonth(), d.getDate())
                                        actions.guiMove(model.taskId, when, model.startHasTime)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Månedsvisning: 6 x 7 celler.
    GridLayout {
        anchors.fill: parent
        anchors.margins: 8
        columns: 7
        rowSpacing: 4
        columnSpacing: 4
        visible: cal.mode === "month"

        Repeater {
            model: 42
            delegate: Rectangle {
                id: cell
                required property int index
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Math.min(4, theme.radius)
                color: "#141416"
                border.color: "#26262a"
                property var cellDate: cal.addDays(cal.mondayOf(cal.monthStart), index)
                property bool inMonth: cellDate.getMonth() === cal.monthStart.getMonth()
                property bool isToday: cellDate.toDateString() === new Date().toDateString()

                CalendarModel {
                    id: cellModel
                    sourceModel: planModel
                    from: cell.cellDate
                    to: cell.cellDate
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2
                    Label {
                        text: cell.cellDate.getDate()
                        color: cell.inMonth ? (cell.isToday ? theme.accent : Material.foreground) : Material.hintTextColor
                        font.bold: cell.isToday
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: cellModel
                        delegate: Label {
                            Layout.fillWidth: true
                            text: model.title
                            elide: Text.ElideRight
                            font.pixelSize: 10
                            color: model.status === "done" ? Material.hintTextColor : theme.accent
                        }
                    }
                }
            }
        }
    }
}
