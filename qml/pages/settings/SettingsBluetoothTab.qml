import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: bluetoothTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Machine Connection
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.bluetooth.machine"
                    fallback: "Machine"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true

                    Tr {
                        key: "settings.bluetooth.status"
                        fallback: "Status:"
                        color: Theme.textSecondaryColor
                    }

                    Tr {
                        key: DE1Device.connected ? "settings.bluetooth.connected" : "settings.bluetooth.disconnected"
                        fallback: DE1Device.connected ? "Connected" : "Disconnected"
                        color: DE1Device.connected ? Theme.successColor : Theme.errorColor
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: BLEManager.scanning ? TranslationManager.translate("settings.bluetooth.stopScan", "Stop Scan") : TranslationManager.translate("settings.bluetooth.scanForDE1", "Scan for DE1")
                        accessibleName: BLEManager.scanning ? "Stop scanning for DE1" : "Scan for DE1 machine"
                        onClicked: {
                            console.log("DE1 scan button clicked! scanning=" + BLEManager.scanning)
                            if (BLEManager.scanning) {
                                BLEManager.stopScan()
                            } else {
                                BLEManager.startScan()
                            }
                        }
                    }
                }

                Text {
                    text: TranslationManager.translate("settings.bluetooth.firmware", "Firmware:") + " " + (DE1Device.firmwareVersion || TranslationManager.translate("settings.bluetooth.unknown", "Unknown"))
                    color: Theme.textSecondaryColor
                    visible: DE1Device.connected
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(60)
                    clip: true
                    model: BLEManager.discoveredDevices

                    delegate: ItemDelegate {
                        width: ListView.view.width
                        contentItem: Text {
                            text: modelData.name + " (" + modelData.address + ")"
                            color: Theme.textColor
                        }
                        background: Rectangle {
                            color: parent.hovered ? Theme.accentColor : "transparent"
                            radius: Theme.scaled(4)
                        }
                        onClicked: DE1Device.connectToDevice(modelData.address)
                    }

                    Tr {
                        anchors.centerIn: parent
                        key: "settings.bluetooth.noDevices"
                        fallback: "No devices found"
                        visible: parent.count === 0
                        color: Theme.textSecondaryColor
                    }
                }

                // DE1 scan log
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Qt.darker(Theme.surfaceColor, 1.2)
                    radius: Theme.scaled(4)

                    ScrollView {
                        id: de1LogScroll
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        clip: true

                        TextArea {
                            id: de1LogText
                            readOnly: true
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            font.family: "monospace"
                            wrapMode: Text.Wrap
                            background: null
                            text: ""
                        }
                    }

                    Connections {
                        target: BLEManager
                        function onDe1LogMessage(message) {
                            de1LogText.text += message + "\n"
                            de1LogScroll.ScrollBar.vertical.position = 1.0 - de1LogScroll.ScrollBar.vertical.size
                        }
                    }
                }
            }
        }

        // Scale Connection
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.bluetooth.scale"
                    fallback: "Scale"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true

                    Tr {
                        key: "settings.bluetooth.status"
                        fallback: "Status:"
                        color: Theme.textSecondaryColor
                    }

                    Tr {
                        property bool isSimulated: ScaleDevice && (ScaleDevice.name === "Flow Scale" || ScaleDevice.name === "Simulated Scale")
                        key: {
                            if (ScaleDevice && ScaleDevice.connected) {
                                return isSimulated ? "settings.bluetooth.simulated" : "settings.bluetooth.connected"
                            }
                            return BLEManager.scaleConnectionFailed ? "settings.bluetooth.notFound" : "settings.bluetooth.disconnected"
                        }
                        fallback: {
                            if (ScaleDevice && ScaleDevice.connected) {
                                return isSimulated ? "Simulated" : "Connected"
                            }
                            return BLEManager.scaleConnectionFailed ? "Not found" : "Disconnected"
                        }
                        color: {
                            if (ScaleDevice && ScaleDevice.connected) {
                                return isSimulated ? Theme.warningColor : Theme.successColor
                            }
                            return BLEManager.scaleConnectionFailed ? Theme.errorColor : Theme.textSecondaryColor
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: BLEManager.scanning ? TranslationManager.translate("settings.bluetooth.scanning", "Scanning...") : TranslationManager.translate("settings.bluetooth.scanForScales", "Scan for Scales")
                        accessibleName: BLEManager.scanning ? "Scanning for scales" : "Scan for Bluetooth scales"
                        enabled: !BLEManager.scanning
                        onClicked: BLEManager.scanForScales()
                    }
                }

                // Connected scale name
                RowLayout {
                    Layout.fillWidth: true
                    visible: ScaleDevice && ScaleDevice.connected

                    Tr {
                        key: "settings.bluetooth.connectedScale"
                        fallback: "Connected:"
                        color: Theme.textSecondaryColor
                    }

                    Text {
                        text: ScaleDevice ? ScaleDevice.name : ""
                        color: Theme.textColor
                    }
                }

                // Simulated scale notice (Flow Scale or Simulated Scale)
                Rectangle {
                    Layout.fillWidth: true
                    height: simScaleNotice.implicitHeight + 16
                    radius: Theme.scaled(6)
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                    border.color: Theme.warningColor
                    border.width: Theme.scaled(1)
                    visible: ScaleDevice && (ScaleDevice.name === "Flow Scale" || ScaleDevice.name === "Simulated Scale")

                    Text {
                        id: simScaleNotice
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        text: ScaleDevice && ScaleDevice.name === "Simulated Scale"
                              ? TranslationManager.translate("settings.bluetooth.simulatedScaleNotice", "Using Simulated Scale (simulator mode)")
                              : TranslationManager.translate("settings.bluetooth.flowScaleNotice", "Using Flow Scale (estimated weight from DE1 flow data)")
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Saved scale info (hidden in simulator mode)
                RowLayout {
                    Layout.fillWidth: true
                    visible: BLEManager.hasSavedScale && !BLEManager.disabled

                    Tr {
                        key: "settings.bluetooth.savedScale"
                        fallback: "Saved scale:"
                        color: Theme.textSecondaryColor
                    }

                    Text {
                        text: Settings.scaleType || "Unknown"
                        color: Theme.textColor
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.bluetooth.forget", "Forget")
                        accessibleName: "Forget saved scale"
                        onClicked: {
                            Settings.scaleAddress = ""
                            Settings.scaleType = ""
                            BLEManager.clearSavedScale()
                        }
                    }
                }

                // Show weight when connected
                RowLayout {
                    Layout.fillWidth: true
                    visible: ScaleDevice && ScaleDevice.connected

                    Tr {
                        key: "settings.bluetooth.weight"
                        fallback: "Weight:"
                        color: Theme.textSecondaryColor
                    }

                    Text {
                        text: MachineState.scaleWeight.toFixed(1) + " g"
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.bluetooth.tare", "Tare")
                        accessibleName: "Tare scale to zero"
                        onClicked: {
                            if (ScaleDevice) ScaleDevice.tare()
                        }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(60)
                    clip: true
                    visible: !ScaleDevice || !ScaleDevice.connected
                    model: BLEManager.discoveredScales

                    delegate: ItemDelegate {
                        width: ListView.view.width
                        contentItem: RowLayout {
                            Text {
                                text: modelData.name
                                color: Theme.textColor
                                Layout.fillWidth: true
                            }
                            Text {
                                text: modelData.type
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }
                        }
                        background: Rectangle {
                            color: parent.hovered ? Theme.accentColor : "transparent"
                            radius: Theme.scaled(4)
                        }
                        onClicked: {
                            console.log("Connect to scale:", modelData.name, modelData.type)
                        }
                    }

                    Tr {
                        anchors.centerIn: parent
                        key: "settings.bluetooth.noScales"
                        fallback: "No scales found"
                        visible: parent.count === 0
                        color: Theme.textSecondaryColor
                    }
                }

                // Scale scan log
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Qt.darker(Theme.surfaceColor, 1.2)
                    radius: Theme.scaled(4)

                    ScrollView {
                        id: scaleLogScroll
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        clip: true

                        TextArea {
                            id: scaleLogText
                            readOnly: true
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            font.family: "monospace"
                            wrapMode: Text.Wrap
                            background: null
                            text: ""
                        }
                    }

                    Connections {
                        target: BLEManager
                        function onScaleLogMessage(message) {
                            scaleLogText.text += message + "\n"
                            scaleLogScroll.ScrollBar.vertical.position = 1.0 - scaleLogScroll.ScrollBar.vertical.size
                        }
                    }
                }
            }
        }
    }
}
