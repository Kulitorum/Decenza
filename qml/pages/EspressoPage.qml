import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "espressoPage"
    background: Rectangle { color: Theme.backgroundColor }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 60
        spacing: 20

        // Left side - Shot graph
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ShotGraph {
                id: shotGraph
                anchors.fill: parent
            }
        }

        // Right side - Live values and controls
        ColumnLayout {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            spacing: 20

            // Timer
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                Text {
                    anchors.centerIn: parent
                    text: MachineState.shotTime.toFixed(1) + "s"
                    color: Theme.textColor
                    font: Theme.timerFont
                }
            }

            // Current values
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 15
                columnSpacing: 15

                CircularGauge {
                    Layout.fillWidth: true
                    value: DE1Device.pressure
                    maxValue: 12
                    unit: "bar"
                    color: Theme.pressureColor
                    label: "Pressure"
                }

                CircularGauge {
                    Layout.fillWidth: true
                    value: DE1Device.flow
                    maxValue: 8
                    unit: "mL/s"
                    color: Theme.flowColor
                    label: "Flow"
                }

                CircularGauge {
                    Layout.fillWidth: true
                    value: DE1Device.temperature
                    minValue: 80
                    maxValue: 100
                    unit: "°C"
                    color: Theme.temperatureColor
                    label: "Temp"
                }

                CircularGauge {
                    Layout.fillWidth: true
                    value: 0  // Would come from scale
                    maxValue: MainController.targetWeight * 1.5
                    unit: "g"
                    color: Theme.weightColor
                    label: "Weight"
                }
            }

            // Weight progress
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: MainController.targetWeight
                value: 0  // Would come from scale

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: 4
                }

                contentItem: Rectangle {
                    width: parent.visualPosition * parent.width
                    height: parent.height
                    radius: 4
                    color: Theme.weightColor
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "0 / " + MainController.targetWeight.toFixed(0) + " g"
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
            }

            Item { Layout.fillHeight: true }

            // Stop button
            ActionButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 200
                Layout.preferredHeight: 80
                text: "STOP"
                backgroundColor: Theme.accentColor
                onClicked: {
                    DE1Device.stopOperation()
                    root.goToIdle()
                }
            }
        }
    }

    // Tap anywhere to stop (full screen touch area)
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }
}
