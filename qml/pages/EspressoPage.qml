import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    id: espressoPage
    objectName: "espressoPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Local weight property - updated directly in signal handler for immediate display
    property real currentWeight: 0.0

    Component.onCompleted: root.currentPageTitle = MainController.currentProfileName

    // Force immediate weight update on signal (bypasses lazy binding evaluation)
    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            espressoPage.currentWeight = MachineState.scaleWeight
        }
    }

    // Full-screen shot graph
    ShotGraph {
        id: shotGraph
        anchors.fill: parent
        anchors.topMargin: Theme.scaled(50)
        anchors.bottomMargin: Theme.scaled(100)
    }

    // Status indicator for preheating
    Rectangle {
        id: statusBanner
        anchors.top: parent.top
        anchors.topMargin: 100
        anchors.horizontalCenter: parent.horizontalCenter
        width: statusText.width + 40
        height: 36
        radius: 18
        color: MachineState.phase === MachineStateType.Phase.EspressoPreheating ?
               Theme.accentColor : "transparent"
        visible: MachineState.phase === MachineStateType.Phase.EspressoPreheating

        Text {
            id: statusText
            anchors.centerIn: parent
            text: "PREHEATING..."
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }

    // Bottom info bar with live values
    Rectangle {
        id: infoBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 100
        color: Qt.darker(Theme.surfaceColor, 1.3)

        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 15

            // Back button (large hitbox, icon aligned left)
            RoundButton {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 70
                flat: true
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                icon.color: Theme.textColor
                display: AbstractButton.IconOnly
                leftPadding: 0
                rightPadding: 52
                onClicked: {
                    DE1Device.stopOperation()
                    root.goToIdle()
                }
            }

            // Timer
            ColumnLayout {
                Layout.preferredWidth: 100
                spacing: 2

                Text {
                    text: MachineState.shotTime.toFixed(1) + "s"
                    color: Theme.textColor
                    font.pixelSize: 36
                    font.weight: Font.Bold
                }
                Text {
                    text: "Time"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Divider
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: 10
                Layout.bottomMargin: 10
                color: Theme.textSecondaryColor
                opacity: 0.3
            }

            // Pressure
            ColumnLayout {
                Layout.preferredWidth: 80
                spacing: 2

                Text {
                    text: DE1Device.pressure.toFixed(1)
                    color: Theme.pressureColor
                    font.pixelSize: 28
                    font.weight: Font.Medium
                }
                Text {
                    text: "bar"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Flow
            ColumnLayout {
                Layout.preferredWidth: 80
                spacing: 2

                Text {
                    text: DE1Device.flow.toFixed(1)
                    color: Theme.flowColor
                    font.pixelSize: 28
                    font.weight: Font.Medium
                }
                Text {
                    text: "mL/s"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Temperature
            ColumnLayout {
                Layout.preferredWidth: 80
                spacing: 2

                Text {
                    text: DE1Device.temperature.toFixed(1)
                    color: Theme.temperatureColor
                    font.pixelSize: 28
                    font.weight: Font.Medium
                }
                Text {
                    text: "°C"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Divider
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: 10
                Layout.bottomMargin: 10
                color: Theme.textSecondaryColor
                opacity: 0.3
            }

            // Weight with progress
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    spacing: 8

                    Text {
                        text: espressoPage.currentWeight.toFixed(1)
                        color: Theme.weightColor
                        font.pixelSize: 28
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignBaseline
                    }
                    Text {
                        text: "/ " + MainController.targetWeight.toFixed(0) + " g"
                        color: Theme.textSecondaryColor
                        font.pixelSize: 18
                        Layout.alignment: Qt.AlignBaseline
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    from: 0
                    to: MainController.targetWeight
                    value: espressoPage.currentWeight

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
            }
        }
    }

    // Tap anywhere on chart to stop
    MouseArea {
        anchors.fill: shotGraph
        onClicked: {
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }
}
