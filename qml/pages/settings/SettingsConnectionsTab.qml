import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: connectionsTab

    // Share Log Dialog
    Popup {
        id: shareLogDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: "white"
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Title
            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(15)
                text: TranslationManager.translate("settings.bluetooth.shareLogTitle", "Share Scale Debug Log")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            // Content
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                text: TranslationManager.translate("settings.bluetooth.shareLogInstructions", "Send the debug log to:")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
            }

            // Email address box
            Rectangle {
                id: emailBox
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(10)
                height: Theme.scaled(40)
                color: emailMouseArea.containsMouse
                       ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.25)
                       : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.15)
                radius: Theme.scaled(6)
                border.color: Theme.accentColor
                border.width: 1

                property bool copied: false

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(8)

                    Text {
                        text: emailBox.copied ? "✓ Copied!" : "decenzalogs@kulitorum.com"
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(15)
                        font.bold: true
                    }

                    Text {
                        text: "⧉"
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(16)
                        visible: !emailBox.copied
                    }
                }

                MouseArea {
                    id: emailMouseArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: {
                        // Copy to clipboard
                        copyHelper.text = "decenzalogs@kulitorum.com"
                        copyHelper.selectAll()
                        copyHelper.copy()
                        emailBox.copied = true
                        copyResetTimer.restart()
                    }
                }

                // Hidden text input for clipboard access
                TextInput {
                    id: copyHelper
                    visible: false
                }

                Timer {
                    id: copyResetTimer
                    interval: 2000
                    onTriggered: emailBox.copied = false
                }
            }

            // Instructions
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(12)
                text: TranslationManager.translate("settings.bluetooth.shareLogInclude", "Please include your scale model and describe the issue.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.Wrap
            }

            // Buttons row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(15)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                spacing: Theme.scaled(12)

                // Cancel button
                Text {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    color: Theme.accentColor
                    font.pixelSize: Theme.scaled(14)

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shareLogDialog.close()
                    }
                }

                Item { Layout.fillWidth: true }

                // Share button
                Rectangle {
                    width: Theme.scaled(140)
                    height: Theme.scaled(36)
                    color: Theme.accentColor
                    radius: Theme.scaled(6)

                    Text {
                        anchors.centerIn: parent
                        text: TranslationManager.translate("settings.bluetooth.shareNow", "Share Log File")
                        color: "white"
                        font.pixelSize: Theme.scaled(13)
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            shareLogDialog.close()
                            BLEManager.shareScaleLog()
                        }
                    }
                }
            }
        }
    }

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

                // === USB-C view (shown when USB connected) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: USBManager.de1Connected
                    spacing: Theme.scaled(10)

                    // Title row with status badge
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "USB-C"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            width: usbStatusText.implicitWidth + Theme.scaled(16)
                            height: Theme.scaled(24)
                            radius: Theme.scaled(12)
                            color: DE1Device.connected
                                   ? Qt.rgba(Theme.successColor.r, Theme.successColor.g, Theme.successColor.b, 0.2)
                                   : Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.2)

                            Text {
                                id: usbStatusText
                                anchors.centerIn: parent
                                text: DE1Device.connected ? "Connected" : "Connecting..."
                                color: DE1Device.connected ? Theme.successColor : Theme.warningColor
                                font.pixelSize: Theme.scaled(12)
                                font.bold: true
                            }
                        }
                    }

                    // Connection type indicator
                    Text {
                        text: TranslationManager.translate("settings.connections.transport", "Transport:") + " " + (DE1Device.connectionType || "USB-C")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                    }

                    // Port info
                    Text {
                        text: TranslationManager.translate("settings.connections.port", "Port:") + " " + USBManager.portName
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                    }

                    // Serial number
                    Text {
                        text: TranslationManager.translate("settings.connections.serial", "Serial:") + " " + USBManager.serialNumber
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        visible: USBManager.serialNumber !== ""
                    }

                    // Firmware version
                    Text {
                        text: TranslationManager.translate("settings.bluetooth.firmware", "Firmware:") + " " + (DE1Device.firmwareVersion || TranslationManager.translate("settings.bluetooth.unknown", "Unknown"))
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        visible: DE1Device.connected
                    }

                    // Machine state
                    Text {
                        text: TranslationManager.translate("settings.connections.state", "State:") + " " + DE1Device.stateString
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        visible: DE1Device.connected
                    }

                    // USB-C connection log
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Qt.darker(Theme.surfaceColor, 1.2)
                        radius: Theme.scaled(4)

                        ScrollView {
                            id: usbLogScroll
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)
                            clip: true

                            TextArea {
                                id: usbLogText
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
                            target: USBManager
                            function onLogMessage(message) {
                                usbLogText.text += message + "\n"
                                usbLogScroll.ScrollBar.vertical.position = 1.0 - usbLogScroll.ScrollBar.vertical.size
                            }
                        }

                        // Also show DE1 transport logs (SerialTransport TX/RX) in the USB log panel
                        Connections {
                            target: BLEManager
                            enabled: USBManager.de1Connected
                            function onDe1LogMessage(message) {
                                usbLogText.text += message + "\n"
                                usbLogScroll.ScrollBar.vertical.position = 1.0 - usbLogScroll.ScrollBar.vertical.size
                            }
                        }
                    }
                }

                // === BLE view (shown when no USB connection) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !USBManager.de1Connected
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
                        property bool isFlowScale: ScaleDevice && ScaleDevice.name === "Flow Scale" && Settings.useFlowScale
                        property bool isSimulated: ScaleDevice && ScaleDevice.name === "Simulated Scale"
                        key: {
                            if (ScaleDevice && ScaleDevice.connected) {
                                if (isFlowScale) return "settings.bluetooth.virtualScale"
                                if (isSimulated) return "settings.bluetooth.simulated"
                                return "settings.bluetooth.connected"
                            }
                            return BLEManager.scaleConnectionFailed ? "settings.bluetooth.notFound" : "settings.bluetooth.disconnected"
                        }
                        fallback: {
                            if (ScaleDevice && ScaleDevice.connected) {
                                if (isFlowScale) return "Virtual Scale"
                                if (isSimulated) return "Simulated"
                                return "Connected"
                            }
                            return BLEManager.scaleConnectionFailed ? "Not found" : "Disconnected"
                        }
                        color: {
                            if (ScaleDevice && ScaleDevice.connected) {
                                return (isFlowScale || isSimulated) ? Theme.warningColor : Theme.successColor
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
                    visible: ScaleDevice && ScaleDevice.connected && ScaleDevice.name !== "Flow Scale"

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

                // Virtual scale notice (FlowScale active)
                Rectangle {
                    Layout.fillWidth: true
                    height: flowScaleNotice.implicitHeight + 16
                    radius: Theme.scaled(6)
                    color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                    border.color: Theme.primaryColor
                    border.width: 1
                    visible: ScaleDevice && ScaleDevice.name === "Flow Scale" && Settings.useFlowScale

                    Text {
                        id: flowScaleNotice
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        text: TranslationManager.translate("settings.bluetooth.flowScaleNotice",
                              "Using Virtual Scale — estimating cup weight from flow data. Set your dose weight for best accuracy.")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Simulated scale notice
                Rectangle {
                    Layout.fillWidth: true
                    height: simScaleNotice.implicitHeight + 16
                    radius: Theme.scaled(6)
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                    border.color: Theme.warningColor
                    border.width: 1
                    visible: ScaleDevice && ScaleDevice.name === "Simulated Scale"

                    Text {
                        id: simScaleNotice
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        text: TranslationManager.translate("settings.bluetooth.simulatedScaleNotice", "Using Simulated Scale (simulator mode)")
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Saved scale info (show even in simulator mode so user can forget stale scale)
                RowLayout {
                    Layout.fillWidth: true
                    visible: BLEManager.hasSavedScale

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
                            BLEManager.connectToScale(modelData.address)
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

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        spacing: Theme.scaled(4)

                        ScrollView {
                            id: scaleLogScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
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

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Item { Layout.fillWidth: true }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.clearLog", "Clear")
                                accessibleName: "Clear scale log"
                                onClicked: {
                                    scaleLogText.text = ""
                                    BLEManager.clearScaleLog()
                                }
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.shareLog", "Share Log")
                                accessibleName: "Share scale debug log"
                                onClicked: shareLogDialog.open()
                            }
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
