import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

Item {
    id: connectionsTab

    // USB is not supported on iOS (no USB host mode)
    readonly property bool usbAvailable: Qt.platform.os !== "ios"

    // Share Log Dialog
    Dialog {
        id: shareLogDialog
        modal: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.primaryContrastColor
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

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("settings.bluetooth.copyemail", "Copy email address") + " decenzalogs@kulitorum.com"
                Accessible.focusable: true
                Accessible.onPressAction: emailMouseArea.clicked(null)

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(8)

                    Text {
                        text: emailBox.copied ? "✓ Copied!" : "decenzalogs@kulitorum.com"
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(15)
                        font.bold: true
                        Accessible.ignored: true
                    }

                    Text {
                        text: "⧉"
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(16)
                        visible: !emailBox.copied
                        Accessible.ignored: true
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

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("settings.bluetooth.shareNow", "Share Log File")
                    Accessible.focusable: true
                    Accessible.onPressAction: shareLogArea.clicked(null)

                    Text {
                        anchors.centerIn: parent
                        text: TranslationManager.translate("settings.bluetooth.shareNow", "Share Log File")
                        color: Theme.primaryContrastColor
                        font.pixelSize: Theme.scaled(13)
                        font.bold: true
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: shareLogArea
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

    // Add WiFi Scale Dialog — enter an IP address or mDNS name to connect a
    // WiFi scale that isn't being advertised/discovered right now.
    Dialog {
        id: addWifiScaleDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min((parent ? parent.width : Theme.scaled(420)) * 0.85, Theme.scaled(420))
        padding: 0

        // mDNS lookup state while the dialog is open. Set by the BLEManager
        // signals below. The hostname is shown as a one-tap shortcut so the
        // user doesn't have to type it (or guess between "hds.local" and an IP).
        property bool mdnsSearching: false
        property string mdnsHostname: ""
        property string mdnsIp: ""
        readonly property bool mdnsFound: mdnsHostname.length > 0

        onOpened: {
            wifiScaleHostField.text = ""
            wifiScaleHostField.forceActiveFocus()
            // Reset prior probe state and kick off a fresh mDNS lookup. If the
            // scale is on the LAN we offer it as a one-tap option above the
            // text field, sidestepping the typo-and-bad-IP failure mode entirely.
            mdnsHostname = ""
            mdnsIp = ""
            mdnsSearching = true
            BLEManager.probeMdnsForManualEntry()
        }

        function submitWifiScale() {
            Qt.inputMethod.commit()  // flush in-progress IME word before reading text
            var host = wifiScaleHostField.text.trim()
            if (host.length === 0)
                return
            addWifiScaleDialog.close()
            BLEManager.connectToWifiScale(host)
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.primaryContrastColor
            border.width: 1
        }

        contentItem: KeyboardAwareContainer {
            id: wifiScaleKbContainer
            inOverlay: true
            textFields: [wifiScaleHostField]
            targetFlickable: wifiScaleFlick
            implicitWidth: addWifiScaleDialog.availableWidth
            implicitHeight: Math.min(wifiScaleCol.implicitHeight,
                                     addWifiScaleDialog.parent ? addWifiScaleDialog.parent.height * 0.9
                                                               : wifiScaleCol.implicitHeight)

            Flickable {
                id: wifiScaleFlick
                anchors.fill: parent
                contentHeight: wifiScaleCol.implicitHeight + wifiScaleKbContainer.estimatedKeyboardHeight
                contentWidth: parent.width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                ColumnLayout {
                    id: wifiScaleCol
                    width: parent.width
                    spacing: 0

                    // Title
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(15)
                        text: TranslationManager.translate("settings.bluetooth.addWifiScaleTitle", "Add WiFi Scale")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // Instructions
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        text: TranslationManager.translate("settings.bluetooth.addWifiScaleInstructions",
                              "Enter the scale's IP address or name.")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        wrapMode: Text.Wrap
                    }

                    // "Searching the network..." indicator shown while the
                    // mDNS probe is in flight and we haven't found anything
                    // yet. Replaced by the discovered-scale banner below once
                    // mdnsFound flips true; hidden entirely once the probe
                    // finishes without a result.
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(12)
                        visible: addWifiScaleDialog.mdnsSearching && !addWifiScaleDialog.mdnsFound
                        text: TranslationManager.translate("settings.bluetooth.mdnsSearching",
                                                           "Searching the network for a scale...")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        font.italic: true
                    }

                    // mDNS-discovered scale suggestion. Saves the user from
                    // typing (and from typos that would fail validation) when
                    // the scale is responding to mDNS on the LAN.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(12)
                        Layout.preferredHeight: visible ? mdnsRow.implicitHeight + Theme.scaled(16) : 0
                        visible: addWifiScaleDialog.mdnsFound
                        color: Qt.darker(Theme.accentColor, 1.4)
                        radius: Theme.scaled(6)
                        border.color: Theme.accentColor
                        border.width: 1

                        RowLayout {
                            id: mdnsRow
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)
                            spacing: Theme.scaled(8)

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(2)

                                Text {
                                    Layout.fillWidth: true
                                    text: TranslationManager.translate("settings.bluetooth.mdnsFoundTitle",
                                                                       "Scale found on this network")
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: addWifiScaleDialog.mdnsHostname
                                        + (addWifiScaleDialog.mdnsIp.length > 0
                                           ? " (" + addWifiScaleDialog.mdnsIp + ")"
                                           : "")
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(12)
                                    wrapMode: Text.Wrap
                                }
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.mdnsFoundUse", "Use")
                                accessibleName: TranslationManager.translate("settings.bluetooth.mdnsFoundUseAccessible",
                                                                              "Use the discovered WiFi scale")
                                primary: true
                                onClicked: {
                                    var host = addWifiScaleDialog.mdnsHostname
                                    addWifiScaleDialog.close()
                                    BLEManager.connectToWifiScale(host)
                                }
                            }
                        }
                    }

                    // IP / name input
                    StyledTextField {
                        id: wifiScaleHostField
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(12)
                        placeholder: TranslationManager.translate("settings.bluetooth.addWifiScalePlaceholder", "192.168.1.50 or hds.local")
                        accessibleName: TranslationManager.translate("settings.bluetooth.addWifiScaleField", "WiFi scale IP address or name")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        Keys.onReturnPressed: addWifiScaleDialog.submitWifiScale()
                        Keys.onEnterPressed: addWifiScaleDialog.submitWifiScale()
                    }

                    // Buttons row
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(15)
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        spacing: Theme.scaled(12)

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: TranslationManager.translate("common.cancel", "Cancel")
                            accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                            subtle: true
                            onClicked: addWifiScaleDialog.close()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.bluetooth.connect", "Connect")
                            accessibleName: TranslationManager.translate("settings.bluetooth.addWifiScaleConnect", "Connect to WiFi scale")
                            primary: true
                            enabled: wifiScaleHostField.text.trim().length > 0
                            onClicked: addWifiScaleDialog.submitWifiScale()
                        }
                    }
                }
            }
        }
    }

    // Validation-result dialog for the manual "Add WiFi Scale" flow. Shown when
    // BLEManager fails to verify that the typed address actually hosts an HDS
    // scale (timeout without HDS recognition, or socket error). Without this
    // the user would silently end up with a poisoned saved primary that the
    // app keeps redialing on every reconnect cycle (see #1281).
    property string lastManualWifiHost: ""
    Dialog {
        id: manualWifiFailedDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min((parent ? parent.width : Theme.scaled(420)) * 0.85, Theme.scaled(420))
        padding: Theme.scaled(20)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.primaryContrastColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.bluetooth.manualWifiFailed.title",
                                                   "No scale found")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.bluetooth.manualWifiFailed.body",
                                                   "Couldn't verify a Decent WiFi scale at %1. Check the address and that the scale is on the same network, then try again.").arg(connectionsTab.lastManualWifiHost)
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.ok", "OK")
                    accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                    primary: true
                    onClicked: manualWifiFailedDialog.close()
                }
            }
        }
    }

    Connections {
        target: BLEManager
        function onManualWifiValidationFailed(hostnameOrIp) {
            connectionsTab.lastManualWifiHost = hostnameOrIp
            // Don't stack on top of the entry dialog if the user reopened it
            if (addWifiScaleDialog.opened) addWifiScaleDialog.close()
            manualWifiFailedDialog.open()
        }
        function onManualWifiValidationSucceeded(hostnameOrIp) {
            // No UX needed — the normal scale-connected indicator handles it.
            // Close any open entry dialog defensively in case the user reopened it.
            if (addWifiScaleDialog.opened) addWifiScaleDialog.close()
        }
        function onManualWifiMdnsDiscovered(hostname, ip) {
            // Only surface the suggestion if the user still has the dialog
            // open AND hasn't started typing — otherwise the banner would
            // pop in under their fingers, distracting from the address they
            // were entering. (The field is empty on open, so the common
            // "opened-and-waiting" case still gets the banner.)
            if (!addWifiScaleDialog.opened) return
            if (wifiScaleHostField.text.length > 0) {
                addWifiScaleDialog.mdnsSearching = false
                return
            }
            addWifiScaleDialog.mdnsHostname = hostname
            addWifiScaleDialog.mdnsIp = ip
            addWifiScaleDialog.mdnsSearching = false
        }
        function onManualWifiMdnsProbeFinished() {
            addWifiScaleDialog.mdnsSearching = false
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Machine Connection
        Rectangle {
            objectName: "machineConnection"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                // === USB-C view (shown when USB connected, not available on iOS) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: usbAvailable && USBManager.de1Connected
                    spacing: Theme.scaled(10)

                    // Title row with status badge
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: TranslationManager.translate("connections.usb.de1Title", "DE1 Machine (USB)")
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
                                text: DE1Device.connected ? TranslationManager.translate("connections.connected", "Connected") : TranslationManager.translate("connections.connecting", "Connecting...")
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
                        text: TranslationManager.translate("settings.connections.port", "Port:") + " " + (typeof USBManager !== "undefined" ? USBManager.portName : "")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                    }

                    // Serial number
                    Text {
                        text: TranslationManager.translate("settings.connections.serial", "Serial:") + " " + (typeof USBManager !== "undefined" ? USBManager.serialNumber : "")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        visible: typeof USBManager !== "undefined" && USBManager.serialNumber !== ""
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

                    // Disconnect button
                    AccessibleButton {
                        text: TranslationManager.translate("settings.connections.disconnect", "Disconnect USB")
                        accessibleName: "Disconnect DE1 USB connection"
                        Layout.alignment: Qt.AlignLeft
                        onClicked: USBManager.disconnectUsb()
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

                                Accessible.role: Accessible.EditableText
                                Accessible.name: TranslationManager.translate("settings.connections.usbLog", "USB connection log")
                                Accessible.description: Theme.capAccessibleText(text)
                                Accessible.focusable: true
                                activeFocusOnTab: true
                            }
                        }

                        Connections {
                            target: usbAvailable ? USBManager : null
                            function onLogMessage(message) {
                                usbLogText.text += message + "\n"
                                usbLogScroll.ScrollBar.vertical.position = 1.0 - usbLogScroll.ScrollBar.vertical.size
                            }
                        }

                        // Also show DE1 transport logs (SerialTransport TX/RX) in the USB log panel
                        Connections {
                            target: BLEManager
                            enabled: usbAvailable && USBManager.de1Connected
                            function onDe1LogMessage(message) {
                                usbLogText.text += message + "\n"
                                usbLogScroll.ScrollBar.vertical.position = 1.0 - usbLogScroll.ScrollBar.vertical.size
                            }
                        }
                    }
                }

                // === BLE view (shown when no USB connection, or always on iOS) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !usbAvailable || !USBManager.de1Connected
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.bluetooth.machine"
                        fallback: "Machine"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    // Bluetooth off warning
                    Rectangle {
                        Layout.fillWidth: true
                        height: btOffRow.implicitHeight + Theme.scaled(16)
                        color: Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.15)
                        radius: Theme.scaled(6)
                        border.color: Theme.errorColor
                        border.width: 1
                        visible: !BLEManager.bluetoothAvailable

                        RowLayout {
                            id: btOffRow
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                margins: Theme.scaled(10)
                            }
                            spacing: Theme.scaled(8)

                            Image {
                                source: "qrc:/icons/bluetooth.svg"
                                // sourceSize is load-bearing here: bluetooth.svg has a
                                // 649×649 viewBox, so without it the Image's implicit
                                // size (what RowLayout uses) is 649×649, which on Linux
                                // renders as a giant icon overflowing the banner (#830).
                                sourceSize.width: Theme.scaled(18)
                                sourceSize.height: Theme.scaled(18)
                                Layout.preferredWidth: Theme.scaled(18)
                                Layout.preferredHeight: Theme.scaled(18)
                                fillMode: Image.PreserveAspectFit
                            }

                            Tr {
                                Layout.fillWidth: true
                                key: "settings.bluetooth.btOff"
                                fallback: "Bluetooth is turned off. Enable Bluetooth to connect to your DE1."
                                color: Theme.errorColor
                                font.pixelSize: Theme.scaled(13)
                                wrapMode: Text.Wrap
                                Accessible.ignored: true
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.openSettings", "Open Settings")
                                accessibleName: TranslationManager.translate("settings.bluetooth.btOff", "Bluetooth is turned off. Enable Bluetooth to connect to your DE1.") + " " + TranslationManager.translate("settings.bluetooth.openBtSettings", "Open Bluetooth settings")
                                onClicked: BLEManager.openBluetoothSettings()
                            }
                        }

                        Accessible.ignored: true
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Tr {
                            key: "settings.bluetooth.status"
                            fallback: "Status:"
                            color: Theme.textSecondaryColor
                        }

                        Tr {
                            key: "settings.bluetooth.connected"
                            fallback: "Connected"
                            visible: DE1Device.connected && !DE1Device.simulationMode
                            color: Theme.successColor
                        }

                        Tr {
                            key: "settings.bluetooth.simulated"
                            fallback: "Simulated"
                            visible: DE1Device.connected && DE1Device.simulationMode
                            color: Theme.warningColor
                        }

                        Tr {
                            key: "settings.bluetooth.disconnected"
                            fallback: "Disconnected"
                            visible: !DE1Device.connected
                            color: Theme.errorColor
                        }

                    }

                    Text {
                        text: TranslationManager.translate("settings.bluetooth.firmware", "Firmware:") + " " + (DE1Device.firmwareVersion || TranslationManager.translate("settings.bluetooth.unknown", "Unknown"))
                        color: Theme.textSecondaryColor
                        visible: DE1Device.connected && !DE1Device.simulationMode
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(60)
                        clip: true
                        visible: !DE1Device.connected
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

                                Accessible.role: Accessible.EditableText
                                Accessible.name: TranslationManager.translate("settings.connections.de1Log", "DE1 connection log")
                                Accessible.description: Theme.capAccessibleText(text)
                                Accessible.focusable: true
                                activeFocusOnTab: true
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

                // Serial USB toggle — not available on iOS
                RowLayout {
                    visible: usbAvailable
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: TranslationManager.translate("connections.usb.serialLabel", "Serial USB (DE1 USB-C)")
                            font.pixelSize: Theme.scaled(14)
                            color: Theme.textColor
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("connections.usb.serialDesc", "Poll for USB-connected DE1. Disable to save battery.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.usbSerialEnabled
                        accessibleName: "Enable serial USB connection for DE1"
                        onToggled: Settings.usbSerialEnabled = checked
                    }
                }

            }
        }

        // Scale Connection
        Rectangle {
            objectName: "scaleConnection"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            clip: true

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                contentHeight: scaleColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: scaleColumn
                width: parent.width
                spacing: Theme.scaled(10)

                // === Scale view (BLE / WiFi / USB — all transports share this
                // unified view; USB shows up as the connected/selected scale and
                // stays switchable to other transports via Known Devices) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.bluetooth.scalesRefractometer"
                        fallback: "Scales / Refractometer"
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

                        QtObject {
                            id: scaleStatusHelper
                            property bool isFlowScale: ScaleDevice && ScaleDevice.isFlowScale && Settings.useFlowScale
                            property bool isSimulated: ScaleDevice && ScaleDevice.isSimulated
                            // FlowScale fallback after physical disconnect — treat as disconnected
                            property bool isDisconnectedFallback: ScaleDevice && ScaleDevice.isFlowScale && !Settings.useFlowScale
                            property bool isConnected: ScaleDevice && ScaleDevice.connected && !isDisconnectedFallback
                        }

                        Tr {
                            key: "settings.bluetooth.connected"
                            fallback: "Connected"
                            visible: scaleStatusHelper.isConnected && !scaleStatusHelper.isFlowScale && !scaleStatusHelper.isSimulated
                            color: Theme.successColor
                        }

                        Tr {
                            key: "settings.bluetooth.virtualScale"
                            fallback: "Virtual Scale"
                            visible: scaleStatusHelper.isConnected && scaleStatusHelper.isFlowScale
                            color: Theme.warningColor
                        }

                        Tr {
                            key: "settings.bluetooth.simulated"
                            fallback: "Simulated"
                            visible: scaleStatusHelper.isConnected && scaleStatusHelper.isSimulated && !scaleStatusHelper.isFlowScale
                            color: Theme.warningColor
                        }

                        Tr {
                            key: "settings.bluetooth.notFound"
                            fallback: "Not found"
                            visible: !scaleStatusHelper.isConnected && BLEManager.scaleConnectionFailed
                            color: Theme.errorColor
                        }

                        Tr {
                            key: "settings.bluetooth.disconnected"
                            fallback: "Disconnected"
                            visible: !scaleStatusHelper.isConnected && !BLEManager.scaleConnectionFailed
                            color: Theme.textSecondaryColor
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: BLEManager.scanning ? TranslationManager.translate("settings.bluetooth.scanning", "Scanning...") : TranslationManager.translate("settings.bluetooth.scanForDevices", "Scan for Devices")
                            accessibleName: BLEManager.scanning ? "Scanning for devices" : "Scan for Bluetooth DE1, scales, and refractometers"
                            enabled: !BLEManager.scanning
                            onClicked: BLEManager.scanForDevices()
                        }
                    }

                    // Connected scale name + battery (transport-agnostic; the
                    // header above already routes BLE/WiFi/USB through this same row).
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale

                        Tr {
                            key: "settings.bluetooth.connectedScale"
                            fallback: "Connected:"
                            color: Theme.textSecondaryColor
                        }

                        Text {
                            text: ScaleDevice ? ScaleDevice.name : ""
                            color: Theme.textColor
                        }

                        Item { Layout.fillWidth: true }

                        Row {
                            spacing: Theme.scaled(4)
                            visible: ScaleDevice && ScaleDevice.batteryLevel >= 0 && ScaleDevice.batteryLevel <= 100

                            // While charging, the BT path reports a 0xFF sentinel that our parser
                            // maps to batteryLevel=100, and the WiFi percent is similarly unreliable
                            // when plugged in. Mirror ScaleBatteryItem.qml's treatment: swap the
                            // icon to battery-charging.svg and replace the percent with "Charging".
                            readonly property bool charging: ScaleDevice && ScaleDevice.charging

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                source: {
                                    if (parent.charging) return "qrc:/icons/battery-charging.svg"
                                    var level = ScaleDevice ? ScaleDevice.batteryLevel : 0
                                    if (level <= 10) return "qrc:/icons/battery-0.svg"
                                    if (level <= 37) return "qrc:/icons/battery-25.svg"
                                    if (level <= 62) return "qrc:/icons/battery-50.svg"
                                    if (level <= 87) return "qrc:/icons/battery-75.svg"
                                    return "qrc:/icons/battery-100.svg"
                                }
                                sourceSize.width: Theme.scaled(14)
                                sourceSize.height: Theme.scaled(14)
                                Accessible.ignored: true
                            }

                            Text {
                                text: parent.charging
                                      ? TranslationManager.translate("scaleBattery.display.charging", "Charging")
                                      : (ScaleDevice ? ScaleDevice.batteryLevel : 0) + "%"
                                color: {
                                    if (parent.charging) return Theme.successColor
                                    var level = ScaleDevice ? ScaleDevice.batteryLevel : 0
                                    if (level > 50) return Theme.successColor
                                    if (level > 20) return Theme.warningColor
                                    return Theme.errorColor
                                }
                                font.pixelSize: Theme.scaled(13)
                                Accessible.ignored: true
                            }
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
                        visible: ScaleDevice && ScaleDevice.isFlowScale && Settings.useFlowScale

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
                        visible: ScaleDevice && ScaleDevice.isSimulated

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

                    // Known Devices picker (scales + refractometer)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: Settings.knownScales.length > 0 || Settings.savedRefractometerAddress !== ""
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.bluetooth.knownDevices"
                            fallback: "Known Devices"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(13)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            // Primary scale picker \u2014 proper Qt ComboBox so the
                            // affordance always matches the behavior. The popup
                            // is capped at 3 visible rows; longer lists scroll
                            // inline instead of opening a separate dialog.
                            ComboBox {
                                id: scalePicker
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(36)

                                readonly property int rowHeight: Theme.scaled(44)
                                readonly property int visibleRows: 3

                                function indexOfPrimary() {
                                    var scales = Settings.knownScales
                                    for (var i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary) return i
                                    }
                                    return -1
                                }
                                function primaryLabel() {
                                    var scales = Settings.knownScales
                                    for (var i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary)
                                            return scales[i].name || scales[i].type
                                    }
                                    return TranslationManager.translate("settings.bluetooth.noScale", "No scale selected")
                                }
                                function transportLabel(typeId) {
                                    if (!typeId) return ""
                                    if (typeId === "decent-wifi") return "WiFi"
                                    if (typeId === "decent-usb") return "USB"
                                    return "BLE"
                                }

                                model: Settings.knownScales
                                currentIndex: indexOfPrimary()

                                // Re-sync when the underlying list (or which one is primary) changes.
                                Connections {
                                    target: Settings
                                    function onKnownScalesChanged() {
                                        scalePicker.currentIndex = scalePicker.indexOfPrimary()
                                    }
                                }

                                Accessible.role: Accessible.ComboBox
                                Accessible.name: primaryLabel()
                                Accessible.focusable: true
                                // TalkBack double-tap "activate" → open the
                                // dropdown. Mirrors the StyledComboBox pattern.
                                Accessible.onPressAction: if (!scalePicker.popup.visible) scalePicker.popup.open()

                                onActivated: function(index) {
                                    var scales = Settings.knownScales
                                    if (index < 0 || index >= scales.length) return
                                    var scale = scales[index]
                                    if (scale.isPrimary) return
                                    Settings.setPrimaryScale(scale.address)
                                    BLEManager.setSavedScaleAddress(scale.address, scale.type, scale.name)
                                    BLEManager.connectToSavedScale()
                                }

                                background: Rectangle {
                                    radius: Theme.scaled(6)
                                    color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.12)
                                    border.color: Theme.accentColor
                                    border.width: 1
                                }

                                // Suppress the default style indicator entirely
                                // and draw a single chevron at the end of the
                                // contentItem layout, so there's only one arrow.
                                indicator: Item { width: 0; height: 0 }

                                // Closed-state content: live status dot + name + transport badge + chevron.
                                contentItem: RowLayout {
                                    spacing: Theme.scaled(6)

                                    Rectangle {
                                        Layout.leftMargin: Theme.scaled(10)
                                        width: Theme.scaled(8)
                                        height: Theme.scaled(8)
                                        radius: Theme.scaled(4)
                                        color: (ScaleDevice && ScaleDevice.connected)
                                               ? Theme.successColor
                                               : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: scalePicker.primaryLabel()
                                        color: Theme.textColor
                                        font.pixelSize: Theme.scaled(13)
                                        font.bold: true
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        Accessible.ignored: true
                                    }

                                    Rectangle {
                                        property string badge: {
                                            var scales = Settings.knownScales
                                            for (var i = 0; i < scales.length; i++) {
                                                if (scales[i].isPrimary)
                                                    return scalePicker.transportLabel(scales[i].type)
                                            }
                                            return ""
                                        }
                                        visible: badge.length > 0
                                        width: badgeText.implicitWidth + Theme.scaled(10)
                                        height: Theme.scaled(18)
                                        radius: Theme.scaled(9)
                                        color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.22)
                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: parent.badge
                                            color: Theme.accentColor
                                            font.pixelSize: Theme.scaled(10)
                                            font.bold: true
                                        }
                                    }

                                    Text {
                                        Layout.rightMargin: Theme.scaled(10)
                                        text: "\u25BC"
                                        font.pixelSize: Theme.scaled(10)
                                        color: Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                }

                                // Per-row delegate: star (filled on primary) + name + transport badge.
                                delegate: ItemDelegate {
                                    id: scaleRowDelegate
                                    width: scalePicker.width
                                    height: scalePicker.rowHeight
                                    highlighted: scalePicker.highlightedIndex === index

                                    Accessible.role: Accessible.Button
                                    Accessible.name: (modelData.name || modelData.type)
                                                     + " " + scalePicker.transportLabel(modelData.type)
                                                     + (modelData.isPrimary
                                                        ? ", " + TranslationManager.translate("connections.primary", "primary")
                                                        : "")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: scaleRowDelegate.clicked()

                                    contentItem: RowLayout {
                                        spacing: Theme.scaled(8)

                                        Image {
                                            source: "qrc:/icons/star.svg"
                                            sourceSize.width: Theme.scaled(14)
                                            sourceSize.height: Theme.scaled(14)
                                            opacity: modelData.isPrimary ? 1.0 : 0.25
                                            Accessible.ignored: true
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name || modelData.type
                                            color: Theme.textColor
                                            font.pixelSize: Theme.scaled(13)
                                            font.bold: modelData.isPrimary
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            Accessible.ignored: true
                                        }

                                        Rectangle {
                                            property string badge: scalePicker.transportLabel(modelData.type)
                                            visible: badge.length > 0
                                            width: rowBadgeText.implicitWidth + Theme.scaled(10)
                                            height: Theme.scaled(18)
                                            radius: Theme.scaled(9)
                                            color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.2)
                                            Accessible.ignored: true
                                            Text {
                                                id: rowBadgeText
                                                anchors.centerIn: parent
                                                text: parent.badge
                                                color: Theme.accentColor
                                                font.pixelSize: Theme.scaled(10)
                                                font.bold: true
                                            }
                                        }
                                    }

                                    background: Rectangle {
                                        color: highlighted
                                               ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.18)
                                               : "transparent"
                                    }
                                }

                                // Use a Dialog (not Popup) so TalkBack can trap
                                // focus inside the dropdown list and navigate
                                // rows. Dialog inherits from Popup, so it's a
                                // drop-in for ComboBox.popup. We position it
                                // exactly where the Popup used to render, with
                                // no header/footer/buttons, so the visual is
                                // unchanged — only the accessibility tree
                                // changes (TalkBack sees a modal it can enter).
                                // See ACCESSIBILITY.md "Popup for selection lists".
                                popup: Dialog {
                                    y: scalePicker.height
                                    width: scalePicker.width
                                    modal: true
                                    header: null
                                    footer: null
                                    standardButtons: Dialog.NoButton
                                    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
                                    // Pin paddings so the style can't override them
                                    // and silently eat into the visible row count.
                                    leftPadding: 0
                                    rightPadding: 0
                                    topPadding: Theme.scaled(4)
                                    bottomPadding: Theme.scaled(4)
                                    // Cap visible height at `visibleRows`; longer lists scroll.
                                    // Drive sizing off the model length directly — ComboBox.count
                                    // can lag behind a QVariantList model after refresh.
                                    height: Math.min(Settings.knownScales.length, scalePicker.visibleRows) * scalePicker.rowHeight
                                            + topPadding + bottomPadding

                                    // Move keyboard focus into the list when the
                                    // dropdown opens so TalkBack/VoiceOver lands
                                    // on the highlighted row (the primary on
                                    // first open) instead of leaving focus on
                                    // the ComboBox. Defer via Qt.callLater so
                                    // the delegate items are guaranteed to be
                                    // realized before focus is requested.
                                    onOpened: Qt.callLater(scaleDropdownList.forceActiveFocus)

                                    contentItem: ListView {
                                        id: scaleDropdownList
                                        clip: true
                                        focus: true
                                        keyNavigationEnabled: true
                                        model: scalePicker.popup.visible ? scalePicker.delegateModel : null
                                        currentIndex: scalePicker.highlightedIndex
                                        ScrollIndicator.vertical: ScrollIndicator {}
                                    }

                                    background: Rectangle {
                                        radius: Theme.scaled(6)
                                        color: Theme.surfaceColor
                                        border.color: Theme.accentColor
                                        border.width: 1
                                    }
                                }
                            }

                            // Forget button — forgets the currently selected (primary) scale
                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.forget", "Forget")
                                accessibleName: {
                                    var scales = Settings.knownScales
                                    for (var i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary)
                                            return TranslationManager.translate("connections.forgetScale", "Forget scale") + " " + scales[i].name
                                    }
                                    return TranslationManager.translate("settings.bluetooth.forget", "Forget")
                                }
                                onClicked: {
                                    var scales = Settings.knownScales
                                    for (var i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary) {
                                            BLEManager.clearSavedScale()
                                            Settings.removeKnownScale(scales[i].address)
                                            // Reconnect BLEManager to whichever scale is now primary.
                                            var remaining = Settings.knownScales
                                            for (var j = 0; j < remaining.length; j++) {
                                                if (remaining[j].isPrimary) {
                                                    BLEManager.setSavedScaleAddress(remaining[j].address,
                                                                                    remaining[j].type,
                                                                                    remaining[j].name)
                                                    BLEManager.connectToSavedScale()
                                                    break
                                                }
                                            }
                                            return
                                        }
                                    }
                                }
                            }
                        }

                        // Saved refractometer display (below scale picker)
                        RowLayout {
                            Layout.fillWidth: true
                            visible: Settings.savedRefractometerAddress !== ""
                            spacing: Theme.scaled(8)

                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(36)
                                radius: Theme.scaled(6)
                                color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12)
                                border.color: Theme.primaryColor
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.scaled(10)
                                    anchors.rightMargin: Theme.scaled(10)
                                    spacing: Theme.scaled(6)

                                    // Status dot
                                    Rectangle {
                                        width: Theme.scaled(8)
                                        height: Theme.scaled(8)
                                        radius: Theme.scaled(4)
                                        color: BLEManager.refractometerConnected ? Theme.successColor : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }

                                    Text {
                                        text: Settings.savedRefractometerName || TranslationManager.translate("connections.refractometer", "Refractometer")
                                        color: Theme.textColor
                                        font.pixelSize: Theme.scaled(13)
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        Accessible.ignored: true
                                    }

                                    // Type badge
                                    Rectangle {
                                        width: refBadgeText.implicitWidth + Theme.scaled(8)
                                        height: Theme.scaled(18)
                                        radius: Theme.scaled(9)
                                        color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2)

                                        Text {
                                            id: refBadgeText
                                            anchors.centerIn: parent
                                            text: TranslationManager.translate("connections.refractometer", "Refractometer")
                                            color: Theme.primaryColor
                                            font.pixelSize: Theme.scaled(10)
                                            font.bold: true
                                            Accessible.ignored: true
                                        }
                                    }
                                }
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.forget", "Forget")
                                accessibleName: TranslationManager.translate("connections.forgetRefractometer", "Forget refractometer")
                                onClicked: {
                                    BLEManager.clearSavedRefractometer()
                                    Settings.savedRefractometerAddress = ""
                                    Settings.savedRefractometerName = ""
                                }
                            }
                        }
                    }

                    // Read TDS Now button (only when connected)
                    AccessibleButton {
                        Layout.fillWidth: true
                        visible: BLEManager.refractometerConnected
                        text: (typeof Refractometer !== "undefined" && Refractometer && Refractometer.measuring)
                            ? TranslationManager.translate("settings.refractometer.measuring", "Measuring...")
                            : TranslationManager.translate("settings.refractometer.readNow", "Read TDS Now")
                        accessibleName: TranslationManager.translate("settings.refractometer.readTdsNow", "Read TDS from refractometer now")
                        enabled: typeof Refractometer !== "undefined" && Refractometer && !Refractometer.measuring
                        onClicked: {
                            if (typeof Refractometer !== "undefined" && Refractometer)
                                Refractometer.requestMeasurement()
                        }
                    }

                    // Last TDS reading (only when connected and has reading)
                    RowLayout {
                        Layout.fillWidth: true
                        visible: BLEManager.refractometerConnected && typeof Refractometer !== "undefined" && Refractometer && Refractometer.tds > 0

                        Text {
                            text: TranslationManager.translate("settings.refractometer.lastReading", "Last TDS:")
                            color: Theme.textSecondaryColor
                        }

                        Text {
                            text: (typeof Refractometer !== "undefined" && Refractometer) ? Refractometer.tds.toFixed(2) + "%" : ""
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                    }

                    // Scale connection alert toggle
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Settings.knownScales.length > 0
                        spacing: Theme.scaled(15)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Tr {
                                key: "settings.bluetooth.scaleDialogs"
                                fallback: "Scale connection alerts"
                                font.pixelSize: Theme.scaled(14)
                                color: Theme.textColor
                                Accessible.ignored: true
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.bluetooth.scaleDialogsDesc"
                                fallback: "Show popup when scale disconnects or is not found"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: Settings.showScaleDialogs
                            accessibleName: TranslationManager.translate("connections.scaleConnectionAlerts", "Scale connection alerts")
                            onToggled: Settings.showScaleDialogs = checked
                        }
                    }

                    // Keep scale connected when DE1 sleeps
                    RowLayout {
                        Layout.fillWidth: true
                        visible: Settings.knownScales.length > 0
                        spacing: Theme.scaled(15)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: TranslationManager.translate("settings.bluetooth.keepScaleOn",
                                                                   "Keep scale connected when DE1 sleeps")
                                font.pixelSize: Theme.scaled(14)
                                color: Theme.textColor
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: TranslationManager.translate(
                                    "settings.bluetooth.keepScaleOnDesc",
                                    "Turn off to power down and disconnect the scale when the DE1 sleeps. Recommended for battery-only scales; reconnects automatically when the DE1 wakes.")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            id: keepScaleOnSwitch
                            checked: Settings.keepScaleOn
                            accessibleName: TranslationManager.translate(
                                "settings.bluetooth.keepScaleOn",
                                "Keep scale connected when DE1 sleeps")
                            onToggled: Settings.keepScaleOn = checked
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
                            accessibleName: TranslationManager.translate("connections.tareScaleToZero", "Tare scale to zero")
                            onClicked: {
                                if (ScaleDevice) ScaleDevice.tare()
                            }
                        }
                    }

                    // Unified discovered devices list (scales + refractometers).
                    // Height scales with the number of items so rows aren't
                    // clipped below the fold (notably the WiFi-scale row when
                    // both BLE and WiFi entries are present after a scan).
                    ListView {
                        id: discoveredDevicesList
                        Layout.fillWidth: true
                        // Min 80px, grow up to ~160px so 3-4 rows fit without scrolling.
                        Layout.preferredHeight: Math.max(Theme.scaled(80),
                                                          Math.min(count, 4) * Theme.scaled(40))
                        clip: true
                        visible: !ScaleDevice || !ScaleDevice.connected || ScaleDevice.isFlowScale || !BLEManager.refractometerConnected

                        // Combine scales + refractometers. The model is rebuilt
                        // explicitly via Connections handlers below — relying on
                        // QML binding tracking (`property var foo: { ...read... }`
                        // or `: helper(BLEManager.discoveredScales)`) proved
                        // unreliable for QVariantList properties in Qt 6.11:
                        // scalesChanged fired (verified via appendScaleLog) but
                        // the binding didn't re-evaluate and the list stayed
                        // empty in the UI. Explicit assignment from a Connections
                        // signal handler is the bulletproof path.
                        function buildCombinedModel(scales, refractometers) {
                            // Dedup against Known Devices: a scale or refractometer
                            // that's already saved as known shouldn't appear in the
                            // newly-discovered list — the user manages it from the
                            // Known Devices section above. Keys by address (which
                            // for WiFi is "wifi:hostname" — matches what we save).
                            var known = {}
                            var knownScales = Settings.knownScales
                            for (var k = 0; k < knownScales.length; k++) {
                                known[knownScales[k].address] = true
                            }
                            var savedRefAddr = Settings.savedRefractometerAddress || ""

                            var items = []
                            var skippedScales = 0
                            for (var i = 0; i < scales.length; i++) {
                                if (known[scales[i].address]) { skippedScales++; continue }
                                items.push({ deviceName: scales[i].name, address: scales[i].address,
                                             deviceType: scales[i].type, deviceClass: "scale" })
                            }
                            var skippedRefs = 0
                            for (var j = 0; j < refractometers.length; j++) {
                                if (savedRefAddr && refractometers[j].address === savedRefAddr) {
                                    skippedRefs++; continue
                                }
                                items.push({ deviceName: refractometers[j].name, address: refractometers[j].address,
                                             deviceType: refractometers[j].type, deviceClass: "refractometer" })
                            }
                            console.log("[QML] discoveredDevicesList combinedModel rebuilt:",
                                        "scales=" + scales.length + "(-" + skippedScales + " known)",
                                        "refractometers=" + refractometers.length + "(-" + skippedRefs + " known)",
                                        "→ items=" + items.length)
                            return items
                        }
                        property var combinedModel: []
                        model: combinedModel

                        Component.onCompleted: {
                            combinedModel = buildCombinedModel(BLEManager.discoveredScales,
                                                               BLEManager.discoveredRefractometers)
                        }

                        Connections {
                            target: BLEManager
                            function onScalesChanged() {
                                discoveredDevicesList.combinedModel =
                                    discoveredDevicesList.buildCombinedModel(BLEManager.discoveredScales,
                                                                              BLEManager.discoveredRefractometers)
                            }
                            function onRefractometersChanged() {
                                discoveredDevicesList.combinedModel =
                                    discoveredDevicesList.buildCombinedModel(BLEManager.discoveredScales,
                                                                              BLEManager.discoveredRefractometers)
                            }
                        }

                        // Rebuild when the Known Devices set changes — a scale
                        // moving from discovered → known should disappear from
                        // this list since it's now managed above.
                        Connections {
                            target: Settings
                            function onKnownScalesChanged() {
                                discoveredDevicesList.combinedModel =
                                    discoveredDevicesList.buildCombinedModel(BLEManager.discoveredScales,
                                                                              BLEManager.discoveredRefractometers)
                            }
                        }

                        delegate: ItemDelegate {
                            width: ListView.view.width

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.deviceName + ", " + (modelData.deviceClass === "refractometer"
                                ? TranslationManager.translate("connections.refractometer", "Refractometer")
                                : modelData.deviceType)
                            Accessible.focusable: true
                            Accessible.onPressAction: {
                                if (modelData.deviceClass === "refractometer")
                                    BLEManager.connectToRefractometer(modelData.address)
                                else
                                    BLEManager.connectToScale(modelData.address)
                            }

                            contentItem: RowLayout {
                                Text {
                                    text: modelData.deviceName
                                    color: Theme.textColor
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                                // Type badge
                                Rectangle {
                                    width: badgeText.implicitWidth + Theme.scaled(8)
                                    height: Theme.scaled(18)
                                    radius: Theme.scaled(9)
                                    color: modelData.deviceClass === "refractometer"
                                        ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2)
                                        : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.2)

                                    Text {
                                        id: badgeText
                                        anchors.centerIn: parent
                                        text: modelData.deviceClass === "refractometer"
                                            ? TranslationManager.translate("connections.refractometer", "Refractometer")
                                            : modelData.deviceType
                                        color: modelData.deviceClass === "refractometer" ? Theme.primaryColor : Theme.accentColor
                                        font.pixelSize: Theme.scaled(10)
                                        font.bold: true
                                        Accessible.ignored: true
                                    }
                                }
                            }
                            background: Rectangle {
                                color: parent.hovered ? Theme.accentColor : "transparent"
                                radius: Theme.scaled(4)
                            }
                            onClicked: {
                                if (modelData.deviceClass === "refractometer")
                                    BLEManager.connectToRefractometer(modelData.address)
                                else
                                    BLEManager.connectToScale(modelData.address)
                            }
                        }

                        Tr {
                            anchors.centerIn: parent
                            key: "settings.bluetooth.noDevices"
                            fallback: "No devices found"
                            visible: parent.count === 0
                            color: Theme.textSecondaryColor
                        }
                    }

                    // Manual WiFi-scale entry — a quiet fallback for when mDNS
                    // discovery doesn't surface the scale. Rarely needed, so it's
                    // a low-emphasis link rather than a button next to Scan.
                    Tr {
                        Layout.fillWidth: true
                        key: "settings.bluetooth.addWifiScaleLink"
                        fallback: "Scale not found? Add a WiFi scale by IP…"
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(12)
                        horizontalAlignment: Text.AlignHCenter
                        topPadding: Theme.scaled(8)
                        bottomPadding: Theme.scaled(4)
                        Accessible.ignored: true  // AccessibleMouseArea carries the a11y node

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("settings.bluetooth.addWifiScaleAccessible", "Add a WiFi scale by IP address or name")
                            onAccessibleClicked: addWifiScaleDialog.open()
                        }
                    }

                    // Scale scan log
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(150)
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

                                    Accessible.role: Accessible.EditableText
                                    Accessible.name: TranslationManager.translate("settings.connections.bleScaleLog", "Bluetooth scale connection log")
                                    Accessible.description: Theme.capAccessibleText(text)
                                    Accessible.focusable: true
                                    activeFocusOnTab: true
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)

                                Item { Layout.fillWidth: true }

                                AccessibleButton {
                                    text: TranslationManager.translate("settings.bluetooth.clearLog", "Clear")
                                    accessibleName: TranslationManager.translate("connections.clearScaleLog", "Clear scale log")
                                    onClicked: {
                                        scaleLogText.text = ""
                                        BLEManager.clearScaleLog()
                                    }
                                }

                                AccessibleButton {
                                    text: TranslationManager.translate("settings.bluetooth.shareLog", "Share Log")
                                    accessibleName: TranslationManager.translate("connections.shareScaleDebugLog", "Share scale debug log")
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
    }

}
