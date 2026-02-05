import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    readonly property real currentTemp: DE1Device.steamTemperature
    readonly property real targetTemp: Settings.steamTemperature

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // --- COMPACT MODE (bar / status bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactTemp.implicitWidth
        implicitHeight: compactTemp.implicitHeight

        Text {
            id: compactTemp
            anchors.centerIn: parent
            text: DE1Device.connected ? root.currentTemp.toFixed(0) + "\u00B0C\u2009\u2668" : "\u2014"
            color: Theme.warningColor
            font: Theme.bodyFont
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    AccessibilityManager.announceLabel(
                        "Steam temperature: " + root.currentTemp.toFixed(0) +
                        " degrees, target: " + root.targetTemp.toFixed(0) + " degrees")
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
                    text: DE1Device.connected ? root.currentTemp.toFixed(0) + "\u00B0C" : "\u2014"
                    color: Theme.warningColor
                    font: Theme.valueFont
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "/ " + root.targetTemp.toFixed(0) + "\u00B0C"
                    color: Theme.textSecondaryColor
                    font.family: Theme.valueFont.family
                    font.pixelSize: Theme.valueFont.pixelSize / 2
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Steam Temp"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    AccessibilityManager.announceLabel(
                        "Steam temperature: " + root.currentTemp.toFixed(0) +
                        " degrees, target: " + root.targetTemp.toFixed(0) + " degrees")
                }
            }
        }
    }
}
