import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

// Current-time readout. Like the other status readouts it supports a per-instance
// display mode: "text" (default) or "icon" (a clock icon shown with the time —
// beside it in a bar, above it in center zones).
// Respects the 12/24-hour setting and updates every second.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    readonly property string displayMode: (modelData && modelData.displayMode) ? modelData.displayMode : "text"

    // Per-instance color. Default "white" follows the theme text color; the named
    // choices map to the same semantic chart colors the rest of the page uses, so
    // the clock matches its surroundings and honours custom themes.
    readonly property string clockColor: (modelData && modelData.color) ? modelData.color : "white"
    function colorFor(name) {
        switch (name) {
        case "green":  return Theme.pressureColor
        case "red":    return Theme.temperatureColor
        case "blue":   return Theme.flowColor
        case "orange": return Theme.warningColor
        default:       return Theme.textColor // "white"
        }
    }

    // Re-evaluated each second by the timer below.
    property string timeText: ""
    function _refresh() {
        var now = new Date()
        // 24-hour branch uses HH (explicit 0-23) to match the other dedicated
        // clocks (ScreensaverPage, ShotMapScreensaver). hh without an am/pm
        // specifier is equivalent, but HH is unambiguous.
        timeText = Qt.formatTime(now, Settings.app.use12HourTime ? "h:mmap" : "HH:mm")
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
                color: root.colorFor(root.clockColor)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.timeText
                color: root.colorFor(root.clockColor)
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
                color: root.colorFor(root.clockColor)
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.timeText
                color: root.colorFor(root.clockColor)
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
