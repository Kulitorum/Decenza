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
    // waterLevelMm = rawSensorMm + 5mm offset (sensor mounted above intake).
    // waterRefillPoint is in raw sensor mm (sent to firmware as-is).
    // So: margin = waterLevelMm - sensorOffset - waterRefillPoint = rawSensorMm - waterRefillPoint.
    readonly property real sensorOffset: 5.0
    readonly property real margin: DE1Device.waterLevelMm - sensorOffset - Settings.waterRefillPoint
    readonly property string warningState: {
        // No warning when disconnected — waterLevelMm initializes to 0.0 which would
        // falsely trigger "critical" on every startup until the first BLE update arrives.
        if (!DE1Device.connected) return "ok"
        if (margin > 7) return "ok"
        if (margin > 5) return "low"
        if (margin > 3) return "warning"
        return "critical"
    }
    readonly property bool isPulsing: warningState !== "ok"
    readonly property real pulseMinOpacity: {
        if (warningState === "low") return 0.4
        if (warningState === "warning") return 0.3
        return 0.2  // critical
    }
    readonly property int pulseDuration: {
        if (warningState === "low") return 2000
        if (warningState === "warning") return 1000
        return 500  // critical
    }
    readonly property string displayText: {
        if (warningState === "critical")
            return TranslationManager.translate("waterlevel.warning.refill", "Refill")
        if (warningState === "warning")
            return TranslationManager.translate("waterlevel.warning.low", "Low")
        return root.showMl ? DE1Device.waterLevelMl + " ml" : DE1Device.waterLevel.toFixed(0) + "%"
    }

    // Smooth sine-wave opacity pulse — rate and depth increase with urgency
    property real pulseOpacity: 1.0
    SequentialAnimation {
        id: pulseAnimation
        running: root.isPulsing && root.visible
        loops: Animation.Infinite
        NumberAnimation {
            target: root; property: "pulseOpacity"
            from: 1.0; to: root.pulseMinOpacity
            duration: root.pulseDuration / 2
            easing.type: Easing.InOutSine
        }
        NumberAnimation {
            target: root; property: "pulseOpacity"
            from: root.pulseMinOpacity; to: 1.0
            duration: root.pulseDuration / 2
            easing.type: Easing.InOutSine
        }
        onRunningChanged: if (!running) root.pulseOpacity = 1.0
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        var level = root.showMl
            ? DE1Device.waterLevelMl + " " + TranslationManager.translate("waterlevel.accessible.milliliters", "milliliters")
            : DE1Device.waterLevel.toFixed(0) + " " + TranslationManager.translate("waterlevel.accessible.percent", "percent")
        var warning = root.warningState === "critical"
            ? ". " + TranslationManager.translate("waterlevel.accessible.warning.critical", "Warning: water level critically low, refill soon")
            : root.warningState !== "ok"
            ? ". " + TranslationManager.translate("waterlevel.accessible.warning.low", "Warning: water level is low")
            : ""
        return TranslationManager.translate("waterlevel.accessible.label", "Water level:") + " " + level + warning
    }
    Accessible.focusable: true
    Accessible.onPressAction: fullMouseArea.clicked(null)

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
            text: root.displayText
            color: Theme.waterLevelColor
            opacity: root.pulseOpacity
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
                text: root.displayText
                color: Theme.waterLevelColor
                opacity: root.pulseOpacity
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
            id: fullMouseArea
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var warning = root.warningState === "critical"
                        ? ". " + TranslationManager.translate("waterlevel.accessible.warning.critical", "Warning: water level critically low, refill soon")
                        : root.warningState !== "ok"
                        ? ". " + TranslationManager.translate("waterlevel.accessible.warning.low", "Warning: water level is low")
                        : ""
                    var label = TranslationManager.translate("waterlevel.accessible.label", "Water level:")
                    if (root.showMl) {
                        AccessibilityManager.announceLabel(label + " " + DE1Device.waterLevelMl + " " + TranslationManager.translate("waterlevel.accessible.milliliters", "milliliters") + warning)
                    } else {
                        AccessibilityManager.announceLabel(label + " " + DE1Device.waterLevel.toFixed(0) + " " + TranslationManager.translate("waterlevel.accessible.percent", "percent") + warning)
                    }
                }
            }
        }
    }
}
