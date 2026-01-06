import QtQuick
import QtQuick.Layouts
import DecenzaDE1

Rectangle {
    color: Theme.surfaceColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Page title (from root.currentPageTitle)
        // Orange when in simulation mode to indicate simulated machine
        Text {
            text: root.currentPageTitle
            color: DE1Device.simulationMode ? "#E65100" : Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
            Layout.preferredWidth: implicitWidth
            elide: Text.ElideRight
        }

        // Sub state (when actively flowing)
        Text {
            text: "- " + DE1Device.subStateString
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
            visible: MachineState.isFlowing
        }

        Item { Layout.fillWidth: true }

        // Temperature (tap to tare scale)
        Item {
            implicitWidth: tempText.implicitWidth
            implicitHeight: tempText.implicitHeight

            Accessible.role: Accessible.Button
            Accessible.name: temperatureAccessible.text + DE1Device.temperature.toFixed(1) + " " + degreesCelsiusAccessible.text + ". " + tapToTareAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: temperatureAccessible; key: "statusbar.temperature"; fallback: "Temperature: "; visible: false }
            Tr { id: degreesCelsiusAccessible; key: "statusbar.degrees_celsius"; fallback: "degrees Celsius"; visible: false }
            Tr { id: tapToTareAccessible; key: "statusbar.tap_to_tare"; fallback: "Tap to tare scale"; visible: false }

            Text {
                id: tempText
                text: DE1Device.temperature.toFixed(1) + "°C"
                color: tempMouseArea.pressed ? Theme.accentColor : Theme.temperatureColor
                font: Theme.bodyFont
            }

            MouseArea {
                id: tempMouseArea
                anchors.fill: parent
                anchors.margins: -Theme.spacingSmall  // Expand touch target
                cursorShape: Qt.PointingHandCursor
                onClicked: MachineState.tareScale()
            }
        }

        // Separator
        Rectangle {
            width: Theme.scaled(1)
            height: Theme.scaled(30)
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Water level
        Text {
            property bool showMl: Settings.waterLevelDisplayUnit === "ml"
            text: showMl ? DE1Device.waterLevelMl + " ml" : DE1Device.waterLevel.toFixed(0) + "%"
            color: DE1Device.waterLevel > 20 ? Theme.primaryColor : Theme.warningColor
            font: Theme.bodyFont

            Accessible.role: Accessible.StaticText
            Accessible.name: showMl
                ? waterLevelAccessible.text + DE1Device.waterLevelMl + " " + mlAccessible.text
                : waterLevelAccessible.text + DE1Device.waterLevel.toFixed(0) + " " + percentAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: waterLevelAccessible; key: "statusbar.water_level"; fallback: "Water level: "; visible: false }
            Tr { id: percentAccessible; key: "statusbar.percent"; fallback: "percent"; visible: false }
            Tr { id: mlAccessible; key: "statusbar.ml"; fallback: "milliliters"; visible: false }
        }

        // Separator
        Rectangle {
            width: Theme.scaled(1)
            height: Theme.scaled(30)
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Scale warning (clickable button to scan)
        Rectangle {
            visible: BLEManager.scaleConnectionFailed || (BLEManager.hasSavedScale && (!ScaleDevice || !ScaleDevice.connected))
            color: BLEManager.scaleConnectionFailed ? Theme.errorColor : "transparent"
            radius: Theme.scaled(4)
            Layout.preferredHeight: Theme.touchTargetMin
            Layout.preferredWidth: scaleWarningRow.implicitWidth + Theme.spacingMedium

            Accessible.role: Accessible.Button
            Accessible.name: BLEManager.scaleConnectionFailed ? scaleNotFoundAccessible.text + ". " + scanAccessible.text : scaleConnectingAccessible.text
            Accessible.focusable: true

            // Hidden Tr elements for accessible names
            Tr { id: scaleNotFoundAccessible; key: "statusbar.scale_not_found"; fallback: "Scale not found"; visible: false }
            Tr { id: scanAccessible; key: "statusbar.tap_to_scan"; fallback: "Tap to scan"; visible: false }
            Tr { id: scaleConnectingAccessible; key: "statusbar.scale_connecting"; fallback: "Scale connecting"; visible: false }

            // Make entire area clickable
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: BLEManager.scanForScales()
            }

            Row {
                id: scaleWarningRow
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Tr {
                    key: BLEManager.scaleConnectionFailed ? "statusbar.scale_not_found" : "statusbar.scale_ellipsis"
                    fallback: BLEManager.scaleConnectionFailed ? "Scale not found" : "Scale..."
                    color: BLEManager.scaleConnectionFailed ? "white" : Theme.textSecondaryColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Scale connected indicator (tap to tare)
        Item {
            visible: ScaleDevice && ScaleDevice.connected
            implicitWidth: scaleRow.implicitWidth
            implicitHeight: scaleRow.implicitHeight

            Accessible.role: Accessible.Button
            Accessible.name: scaleWeightAccessible.text + MachineState.scaleWeight.toFixed(1) + " " + gramsAccessible.text + ". " + tapToTareAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: scaleWeightAccessible; key: "statusbar.scale_weight"; fallback: "Scale weight: "; visible: false }
            Tr { id: gramsAccessible; key: "statusbar.grams"; fallback: "grams"; visible: false }

            Row {
                id: scaleRow
                spacing: Theme.spacingSmall

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scaled(8)
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    color: scaleMouseArea.pressed ? Theme.accentColor : Theme.weightColor
                }

                Text {
                    text: MachineState.scaleWeight.toFixed(1) + "g"
                    color: scaleMouseArea.pressed ? Theme.accentColor : Theme.weightColor
                    font: Theme.bodyFont
                }
            }

            MouseArea {
                id: scaleMouseArea
                anchors.fill: parent
                anchors.margins: -Theme.spacingSmall  // Expand touch target
                cursorShape: Qt.PointingHandCursor
                onClicked: MachineState.tareScale()
            }
        }

        // Separator
        Rectangle {
            width: Theme.scaled(1)
            height: Theme.scaled(30)
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Connection indicator
        Row {
            spacing: Theme.spacingSmall

            Accessible.role: Accessible.Indicator
            Accessible.name: DE1Device.connected ? machineConnectedAccessible.text : machineDisconnectedAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: machineConnectedAccessible; key: "statusbar.machine_connected"; fallback: "Machine connected"; visible: false }
            Tr { id: machineDisconnectedAccessible; key: "statusbar.machine_disconnected"; fallback: "Machine disconnected"; visible: false }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scaled(10)
                height: Theme.scaled(10)
                radius: Theme.scaled(5)
                color: DE1Device.connected ? Theme.successColor : Theme.errorColor
            }

            Tr {
                key: DE1Device.connected ? "statusbar.online" : "statusbar.offline"
                fallback: DE1Device.connected ? "Online" : "Offline"
                color: DE1Device.connected ? Theme.successColor : Theme.textSecondaryColor
                font: Theme.bodyFont
            }
        }
    }
}
