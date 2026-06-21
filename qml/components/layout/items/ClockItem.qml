import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Current-time readout. Like the other status readouts it supports a per-instance
// display mode: "text" (default) or "icon" (a clock icon ahead of the time).
// Respects the 12/24-hour setting and updates every second.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"

    // Re-evaluated each second by the timer below.
    property string timeText: ""
    function _refresh() {
        var now = new Date()
        timeText = Qt.formatTime(now, Settings.app.use12HourTime ? "h:mmap" : "hh:mm")
    }

    Timer {
        interval: 1000
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: root._refresh()
    }
    Component.onCompleted: root._refresh()

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: TranslationManager.translate("clock.accessible.label", "Time:") + " " + root.timeText
    Accessible.focusable: true

    // --- COMPACT MODE (bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: compactRow.implicitHeight

        Row {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.scaled(6)

            ThemedIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/clock.svg"
                iconSize: Theme.scaled(20)
                color: Theme.textColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.timeText
                color: Theme.textColor
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }
    }

    // --- FULL MODE (center rendering) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            ThemedIcon {
                Layout.alignment: Qt.AlignHCenter
                visible: root.displayMode === "icon"
                source: "qrc:/icons/clock.svg"
                iconSize: Theme.scaled(28)
                color: Theme.textColor
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.timeText
                color: Theme.textColor
                font: Theme.valueFont
                Accessible.ignored: true
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.clock"
                fallback: "Time"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }
    }
}
