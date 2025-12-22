import QtQuick
import QtQuick.Layouts
import DE1App

ColumnLayout {
    property bool machineConnected: false

    spacing: 5

    Row {
        Layout.alignment: Qt.AlignHCenter
        spacing: 8

        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: machineConnected ? Theme.successColor : Theme.errorColor
        }

        Text {
            text: machineConnected ? "Connected" : "Disconnected"
            color: machineConnected ? Theme.successColor : Theme.errorColor
            font: Theme.bodyFont
        }
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: "Machine"
        color: Theme.textSecondaryColor
        font: Theme.labelFont
    }
}
