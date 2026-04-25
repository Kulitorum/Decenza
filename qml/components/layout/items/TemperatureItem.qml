import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    readonly property double effectiveTargetTemp: Settings.brew.hasTemperatureOverride
        ? Settings.brew.temperatureOverride
        : ProfileManager.profileTargetTemperature
    readonly property bool isRealOverride: Settings.brew.hasTemperatureOverride &&
        Math.abs(Settings.brew.temperatureOverride - ProfileManager.profileTargetTemperature) > 0.1

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        var text = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + root.effectiveTargetTemp.toFixed(0) + " degrees"
        if (root.isRealOverride) text += " (override active)"
        return text
    }
    Accessible.focusable: true

    // --- COMPACT MODE (bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactTemp.implicitWidth
        implicitHeight: compactTemp.implicitHeight

        Text {
            id: compactTemp
            anchors.centerIn: parent
            text: DE1Device.temperature.toFixed(1) + "\u00B0C"
            color: Theme.temperatureColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            onClicked: {
                MachineState.tareScale()
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var announcement = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + root.effectiveTargetTemp.toFixed(0) + " degrees"
                    if (root.isRealOverride) announcement += " (override active)"
                    AccessibilityManager.announceLabel(announcement)
                }
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

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(4)
                Text {
                    text: DE1Device.temperature.toFixed(1) + "\u00B0C"
                    color: Theme.temperatureColor
                    font: Theme.valueFont
                    Accessible.ignored: true
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "/ " + root.effectiveTargetTemp.toFixed(1) + "\u00B0C"
                    color: root.isRealOverride ? Theme.accentColor : Theme.textSecondaryColor
                    font.family: Theme.valueFont.family
                    font.pixelSize: Theme.valueFont.pixelSize / 2
                    Accessible.ignored: true
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(4)
                Tr {
                    key: "idle.label.grouptemp"
                    fallback: "Group Temp"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                    Accessible.ignored: true
                }
                Text {
                    visible: root.isRealOverride
                    text: "(override)"
                    color: Theme.accentColor
                    font: Theme.labelFont
                    Accessible.ignored: true
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var announcement = "Group temperature: " + DE1Device.temperature.toFixed(1) + " degrees, target: " + root.effectiveTargetTemp.toFixed(0) + " degrees"
                    if (root.isRealOverride) announcement += " (override active)"
                    AccessibilityManager.announceLabel(announcement)
                }
            }
        }
    }
}
