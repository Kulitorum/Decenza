import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    property bool showMl: Settings.waterLevelDisplayUnit === "ml"

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        var level = root.showMl
            ? DE1Device.waterLevelMl + " milliliters"
            : DE1Device.waterLevel.toFixed(0) + " percent"
        var warning = DE1Device.waterLevel <= 20 ? ". Warning: water level is low" : ""
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
            color: DE1Device.waterLevelMl < 200 ? Theme.errorColor :
                   DE1Device.waterLevelMl < 400 ? Theme.warningColor : Theme.primaryColor
            font: Theme.bodyFont
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
                color: DE1Device.waterLevel > 20 ? Theme.primaryColor : Theme.warningColor
                font: Theme.valueFont
            }
            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.waterlevel"
                fallback: "Water Level"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var warning = DE1Device.waterLevel <= 20 ? ". Warning: water level is low" : ""
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
