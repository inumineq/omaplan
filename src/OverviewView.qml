import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

import Omaplan

// Overblik: agenda for i dag og de næste 7 dage, forfaldne øverst.
Item {
    OverviewModel {
        id: overview
        sourceModel: planModel
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        model: overview
        section.property: "startDate"
        section.delegate: Label {
            required property var section
            topPadding: 10
            bottomPadding: 4
            text: {
                translator.language; // re-render on language switch
                if (!section || section.toString() === "Invalid Date")
                    return qsTr("No date")
                var today = new Date()
                var s = new Date(section)
                if (s.toDateString() === today.toDateString())
                    return qsTr("Today")
                return Qt.formatDate(s, "dddd d. MMMM")
            }
            color: theme.accent
            font.bold: true
        }
        delegate: RowLayout {
            width: list.width
            spacing: 8
            CheckBox {
                checked: model.status === "done"
                onClicked: actions.guiSetDone(model.taskId, checked)
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: model.title
                    elide: Text.ElideRight
                    color: model.status === "done" ? Material.hintTextColor : Material.foreground
                    font.strikeout: model.status === "done"
                }
                Label {
                    text: (model.startHasTime ? Qt.formatTime(model.start, "HH:mm") + " · " : "")
                          + (model.project !== "" ? model.project : "")
                    visible: text !== ""
                    color: Material.hintTextColor
                    font.pixelSize: 12
                }
            }
            Label {
                text: model.priority === 1 ? "!" : ""
                color: "#ff5252"
                font.bold: true
                visible: model.priority === 1
            }
        }
        Label {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTr("No tasks in the next 7 days")
            color: Material.hintTextColor
        }
    }
}
