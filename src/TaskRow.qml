import QtQuick
import QtQuick.Layouts

import Omaplan

// One task as a row in Overblik and Liste: done box, title, and a quiet meta
// line with time, duration, deadline, project and priority. Click selects,
// double-click opens the editor.
Rectangle {
    id: row
    property string taskId
    property string title
    property string status
    property string project
    property int priority: 3
    property var start
    property bool startHasTime
    property int durationMinutes
    property var deadline
    property bool deadlineHasTime
    property bool showDate: false     // show the start day in the meta line
    property bool showProject: true
    readonly property bool done: status === "done"
    readonly property bool selected: win.selectedId === taskId
    readonly property var today: { planModel.today; return Fmt.startOfDay(new Date()) }
    readonly property bool overdue: !done && (Fmt.valid(deadline) ? Fmt.daysBetween(today, deadline) < 0
                                                                   : Fmt.valid(start) && Fmt.daysBetween(today, start) < 0)

    implicitHeight: content.implicitHeight + 14
    radius: Math.min(4, theme.radius)
    // Selection as in Folio: an accent tint, no frame; hover barely shows.
    color: selected ? Qt.alpha(theme.accent, 0.18) : hover.hovered ? Qt.alpha(theme.foreground, 0.05) : "transparent"

    // Priority as a colored edge: red for high, accent for medium.
    Rectangle {
        visible: row.priority < 3 && !row.done
        width: 3
        radius: 1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 4
        color: row.priority === 1 ? theme.danger : theme.accent
    }

    HoverHandler { id: hover }
    TapHandler {
        onTapped: win.selectedId = row.taskId
        onDoubleTapped: win.editTask(row.taskId)
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 12

        CheckMark {
            checked: row.done
            onToggled: win.check(actions.guiSetDone(row.taskId, !row.done))
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                font.family: theme.fontFamily
                Layout.fillWidth: true
                text: row.title
                elide: Text.ElideRight
                color: row.done ? theme.muted : theme.foreground
                font.strikeout: row.done
                font.pixelSize: 14
            }
            Text {
                font.family: theme.fontFamily
                Layout.fillWidth: true
                visible: text !== ""
                elide: Text.ElideRight
                font.pixelSize: 12
                color: theme.muted
                textFormat: Text.StyledText
                text: {
                    Fmt.lang
                    const parts = []
                    if (row.showDate && Fmt.valid(row.start))
                        parts.push(Fmt.relativeShort(row.start, row.today))
                    if (row.startHasTime)
                        parts.push(Fmt.timeRange(row.start, row.durationMinutes))
                    else if (row.durationMinutes > 0)
                        parts.push(Fmt.duration(row.durationMinutes))
                    if (Fmt.valid(row.deadline)) {
                        let d = qsTr("due %1").arg(Fmt.relativeShort(row.deadline, row.today)
                                                   + (row.deadlineHasTime ? " " + Fmt.time(row.deadline) : ""))
                        if (row.overdue)
                            d = "<font color='" + theme.danger + "'>" + d + "</font>"
                        parts.push(d)
                    }
                    return parts.join("  ·  ")
                }
            }
        }
        Tag {
            visible: row.showProject && row.project !== ""
            text: row.project
            tint: theme.muted
        }
        Tag {
            visible: row.priority < 3 && !row.done
            text: Fmt.priorityLabel(row.priority)
            tint: row.priority === 1 ? theme.danger : theme.accent
        }
    }
}
