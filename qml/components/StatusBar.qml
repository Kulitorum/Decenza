import QtQuick
import QtQuick.Layouts
import DE1App

Rectangle {
    color: Theme.surfaceColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 20
        spacing: 15

        // Page title (from root.currentPageTitle)
        Text {
            text: root.currentPageTitle
            color: Theme.textColor
            font.pixelSize: 20
            font.bold: true
            Layout.preferredWidth: implicitWidth
            elide: Text.ElideRight
        }

        // Separator after title (if title exists)
        Rectangle {
            width: 1
            height: 30
            color: Theme.textSecondaryColor
            opacity: 0.3
            visible: root.currentPageTitle !== ""
        }

        // Machine state
        Text {
            text: DE1Device.stateString
            color: Theme.textColor
            font: Theme.bodyFont
        }

        Text {
            text: " - " + DE1Device.subStateString
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
            visible: MachineState.isFlowing
        }

        Item { Layout.fillWidth: true }

        // Temperature
        Text {
            text: DE1Device.temperature.toFixed(1) + "°C"
            color: Theme.temperatureColor
            font: Theme.bodyFont
        }

        // Separator
        Rectangle {
            width: 1
            height: 30
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Water level
        Text {
            text: DE1Device.waterLevel.toFixed(0) + "%"
            color: DE1Device.waterLevel > 20 ? Theme.primaryColor : Theme.warningColor
            font: Theme.bodyFont
        }

        // Separator
        Rectangle {
            width: 1
            height: 30
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Scale warning (clickable to scan)
        Rectangle {
            visible: BLEManager.scaleConnectionFailed || (BLEManager.hasSavedScale && (!ScaleDevice || !ScaleDevice.connected))
            color: BLEManager.scaleConnectionFailed ? Theme.errorColor : "transparent"
            radius: 4
            Layout.preferredHeight: 40
            Layout.preferredWidth: scaleWarningRow.implicitWidth + 16

            Row {
                id: scaleWarningRow
                anchors.centerIn: parent
                spacing: 5

                Text {
                    text: BLEManager.scaleConnectionFailed ? "Scale not found" :
                          (ScaleDevice && ScaleDevice.connected ? "" : "Scale...")
                    color: BLEManager.scaleConnectionFailed ? "white" : Theme.textSecondaryColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: "[Scan]"
                    color: Theme.accentColor
                    font.pixelSize: Theme.bodyFont.pixelSize
                    font.underline: true
                    visible: BLEManager.scaleConnectionFailed
                    anchors.verticalCenter: parent.verticalCenter

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: BLEManager.scanForScales()
                    }
                }
            }
        }

        // Scale connected indicator
        Row {
            spacing: 5
            visible: ScaleDevice && ScaleDevice.connected

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: Theme.weightColor
            }

            Text {
                text: MachineState.scaleWeight.toFixed(1) + "g"
                color: Theme.weightColor
                font: Theme.bodyFont
            }
        }

        // Build number
        Text {
            text: "#" + BuildNumber
            color: Theme.textSecondaryColor
            font.pixelSize: 18
            font.bold: true
            opacity: 0.6
        }

        // Separator before DE1 status
        Rectangle {
            width: 1
            height: 30
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Connection indicator
        Row {
            spacing: 5

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                color: DE1Device.connected ? Theme.successColor : Theme.errorColor
            }

            Text {
                text: DE1Device.connected ? "Online" : "Offline"
                color: DE1Device.connected ? Theme.successColor : Theme.textSecondaryColor
                font: Theme.bodyFont
            }
        }
    }
}
