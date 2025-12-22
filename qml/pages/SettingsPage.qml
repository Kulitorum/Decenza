import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    objectName: "settingsPage"
    background: Rectangle { color: Theme.backgroundColor }

    header: ToolBar {
        background: Rectangle { color: Theme.surfaceColor }

        RowLayout {
            anchors.fill: parent

            ToolButton {
                icon.source: "qrc:/icons/back.svg"
                onClicked: root.goBack()
            }

            Label {
                text: "Settings"
                color: Theme.textColor
                font: Theme.headingFont
                Layout.fillWidth: true
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin

        ColumnLayout {
            width: parent.width
            spacing: 20

            // Machine Connection
            GroupBox {
                Layout.fillWidth: true
                title: "Machine Connection"
                label: Text {
                    text: parent.title
                    color: Theme.textColor
                    font: Theme.bodyFont
                }

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "Status:"
                            color: Theme.textSecondaryColor
                        }

                        Text {
                            text: DE1Device.connected ? "Connected" : "Disconnected"
                            color: DE1Device.connected ? Theme.successColor : Theme.errorColor
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: BLEManager.scanning ? "Stop Scan" : "Scan"
                            onClicked: {
                                if (BLEManager.scanning) {
                                    BLEManager.stopScan()
                                } else {
                                    BLEManager.startScan()
                                }
                            }
                        }
                    }

                    Text {
                        text: "Firmware: " + (DE1Device.firmwareVersion || "Unknown")
                        color: Theme.textSecondaryColor
                        visible: DE1Device.connected
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        clip: true
                        model: BLEManager.discoveredDevices

                        delegate: ItemDelegate {
                            width: parent.width
                            text: modelData.name + " (" + modelData.address + ")"
                            onClicked: DE1Device.connectToDevice(modelData.address)
                        }

                        Label {
                            anchors.centerIn: parent
                            text: "No devices found"
                            visible: parent.count === 0
                            color: Theme.textSecondaryColor
                        }
                    }
                }
            }

            // Steam Settings
            GroupBox {
                Layout.fillWidth: true
                title: "Steam Settings"
                label: Text {
                    text: parent.title
                    color: Theme.textColor
                    font: Theme.bodyFont
                }

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Text {
                            text: "Temperature:"
                            color: Theme.textSecondaryColor
                            Layout.preferredWidth: 120
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 130
                            to: 170
                            stepSize: 1
                            value: Settings.steamTemperature
                            onMoved: Settings.steamTemperature = value
                        }

                        Text {
                            text: Settings.steamTemperature.toFixed(0) + "°C"
                            color: Theme.textColor
                            Layout.preferredWidth: 60
                        }
                    }

                    RowLayout {
                        Text {
                            text: "Timeout:"
                            color: Theme.textSecondaryColor
                            Layout.preferredWidth: 120
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 30
                            to: 180
                            stepSize: 5
                            value: Settings.steamTimeout
                            onMoved: Settings.steamTimeout = value
                        }

                        Text {
                            text: Settings.steamTimeout + "s"
                            color: Theme.textColor
                            Layout.preferredWidth: 60
                        }
                    }
                }
            }

            // Hot Water Settings
            GroupBox {
                Layout.fillWidth: true
                title: "Hot Water Settings"
                label: Text {
                    text: parent.title
                    color: Theme.textColor
                    font: Theme.bodyFont
                }

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Text {
                            text: "Temperature:"
                            color: Theme.textSecondaryColor
                            Layout.preferredWidth: 120
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 60
                            to: 95
                            stepSize: 1
                            value: Settings.waterTemperature
                            onMoved: Settings.waterTemperature = value
                        }

                        Text {
                            text: Settings.waterTemperature.toFixed(0) + "°C"
                            color: Theme.textColor
                            Layout.preferredWidth: 60
                        }
                    }

                    RowLayout {
                        Text {
                            text: "Volume:"
                            color: Theme.textSecondaryColor
                            Layout.preferredWidth: 120
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 50
                            to: 400
                            stepSize: 10
                            value: Settings.waterVolume
                            onMoved: Settings.waterVolume = value
                        }

                        Text {
                            text: Settings.waterVolume + "mL"
                            color: Theme.textColor
                            Layout.preferredWidth: 60
                        }
                    }
                }
            }

            // About
            GroupBox {
                Layout.fillWidth: true
                title: "About"
                label: Text {
                    text: parent.title
                    color: Theme.textColor
                    font: Theme.bodyFont
                }

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 5

                    Text {
                        text: "DE1 Controller"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Text {
                        text: "Version 1.0.0"
                        color: Theme.textSecondaryColor
                    }

                    Text {
                        text: "Built with Qt 6"
                        color: Theme.textSecondaryColor
                    }
                }
            }

            Item { Layout.preferredHeight: 50 }
        }
    }
}
