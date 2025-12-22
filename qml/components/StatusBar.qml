import QtQuick
import QtQuick.Layouts
import DE1App

Rectangle {
    color: Theme.surfaceColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

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
        Row {
            spacing: 5
            Text {
                text: DE1Device.temperature.toFixed(1) + "°C"
                color: Theme.temperatureColor
                font: Theme.bodyFont
            }
        }

        // Separator
        Rectangle {
            width: 1
            height: parent.height * 0.6
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Water level
        Row {
            spacing: 5
            Text {
                text: DE1Device.waterLevel.toFixed(0) + "%"
                color: DE1Device.waterLevel > 20 ? Theme.primaryColor : Theme.warningColor
                font: Theme.bodyFont
            }
        }

        // Separator
        Rectangle {
            width: 1
            height: parent.height * 0.6
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
