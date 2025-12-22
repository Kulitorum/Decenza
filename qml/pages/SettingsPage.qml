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
