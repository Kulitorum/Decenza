import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    property bool showMl: Settings.waterLevelDisplayUnit === "ml"

    // Margin = mm of water remaining before firmware halts the machine.
    // Firmware halts at raw sensor <= waterRefillPoint; waterLevelMm includes 5mm sensor offset.
    readonly property real sensorOffset: 5.0
    readonly property real margin: DE1Device.waterLevelMm - (Settings.waterRefillPoint + sensorOffset)
    readonly property string warningState: {
        if (margin > 7) return "ok"
        if (margin > 5) return "low"
        if (margin > 3) return "warning"
        return "critical"
    }
    readonly property bool isBlinking: warningState !== "ok"
    readonly property color stateWarningColor: warningState === "critical" ? Theme.errorColor : Theme.warningColor
    readonly property color displayColor: {
        if (!isBlinking) return Theme.waterLevelColor
        return blinkTimer.blinkOn ? stateWarningColor : Theme.waterLevelColor
    }

    // Progressive blink animation — rate increases as water approaches halt threshold
    Timer {
        id: blinkTimer
        running: root.isBlinking && root.visible
        repeat: true
        interval: root.warningState === "low" ? 2000
                : root.warningState === "warning" ? 1000 : 500
        property bool blinkOn: true
        onTriggered: blinkOn = !blinkOn
        onRunningChanged: if (!running) blinkOn = true
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        var level = root.showMl
            ? DE1Device.waterLevelMl + " milliliters"
            : DE1Device.waterLevel.toFixed(0) + " percent"
        var warning = root.warningState === "critical" ? ". Warning: water level critically low, refill soon"
                    : root.warningState !== "ok" ? ". Warning: water level is low" : ""
        return "Water level: " + level + warning
    }
    Accessible.focusable: true

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactWater.implicitWidth
        implicitHeight: compactWater.implicitHeight

        Text {
            id: compactWater
            anchors.centerIn: parent
            text: root.showMl ? DE1Device.waterLevelMl + " ml" : DE1Device.waterLevel.toFixed(0) + "%"
            color: root.displayColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }
    }

    // --- FULL MODE ---
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

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.showMl
                    ? DE1Device.waterLevelMl + " ml"
                    : DE1Device.waterLevel.toFixed(0) + "%"
                color: root.displayColor
                font: Theme.valueFont
                Accessible.ignored: true
            }
            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.waterlevel"
                fallback: "Water Level"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var warning = root.warningState === "critical" ? ". Warning: water level critically low, refill soon"
                               : root.warningState !== "ok" ? ". Warning: water level is low" : ""
                    if (root.showMl) {
                        AccessibilityManager.announceLabel("Water level: " + DE1Device.waterLevelMl + " milliliters" + warning)
                    } else {
                        AccessibilityManager.announceLabel("Water level: " + DE1Device.waterLevel.toFixed(0) + " percent" + warning)
                    }
                }
            }
        }
    }
}
