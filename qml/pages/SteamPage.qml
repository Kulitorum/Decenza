import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "steamPage"
    background: Rectangle { color: Theme.backgroundColor }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 60
        spacing: 30

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Steaming"
            color: Theme.textColor
            font: Theme.headingFont
        }

        // Timer
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: MachineState.shotTime.toFixed(1) + "s"
            color: Theme.textColor
            font: Theme.timerFont
        }

        // Temperature display
        CircularGauge {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 200
            Layout.preferredHeight: 200
            value: DE1Device.temperature
            minValue: 100
            maxValue: 180
            unit: "°C"
            color: Theme.temperatureColor
            label: "Steam Temp"
        }

        Item { Layout.fillHeight: true }

        // Stop button
        ActionButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 300
            Layout.preferredHeight: 100
            text: "STOP"
            backgroundColor: Theme.accentColor
            onClicked: {
                DE1Device.stopOperation()
                root.goToIdle()
            }
        }

        Item { Layout.fillHeight: true }
    }

    // Tap anywhere to stop
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }
}
