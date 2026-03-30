import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

Dialog {
    id: migrationDialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.9, Theme.scaled(500))
    height: Math.min(parent.height * 0.85, Theme.scaled(600))
    modal: true
    dim: true
    padding: Theme.scaled(20)
    closePolicy: Dialog.CloseOnEscape

    property bool searchPerformed: false

    Connections {
        target: MainController.dataMigration
        function onDiscoveryComplete() {
            migrationDialog.searchPerformed = true
        }
        function onAuthenticationSucceeded() {
            migrationTotpField.text = ""
        }
        function onAuthenticationFailed(error) {
            migrationTotpField.text = ""
            migrationAuthError.text = error
            migrationTotpField.forceActiveFocus()
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: KeyboardAwareContainer {
        textFields: [manualIpField, migrationTotpField]
        targetFlickable: migrationFlickable

        Flickable {
            id: migrationFlickable
            anchors.fill: parent
            contentHeight: migrationContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: migrationContent
                width: parent.width
                spacing: Theme.scaled(10)

        Tr {
            key: "settings.data.importfrom"
            fallback: "Import from Another Device"
            color: Theme.textColor
            font.pixelSize: Theme.scaled(14)
            font.bold: true
        }

        Tr {
            key: "settings.data.importdesc"
            fallback: "Connect to another Decenza device on your WiFi to import settings, profiles, and shot history."
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Item { height: Theme.scaled(5) }

        // Device discovery and selection
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: !MainController.dataMigration.isImporting &&
                     !(MainController.dataMigration.manifest && MainController.dataMigration.manifest.deviceName !== undefined)

            // Search button and status
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                AccessibleButton {
                    text: MainController.dataMigration.isSearching ?
                          TranslationManager.translate("settings.data.searching", "Searching...") :
                          TranslationManager.translate("settings.data.searchdevices", "Search for Devices")
                    accessibleName: MainController.dataMigration.isSearching ?
                          TranslationManager.translate("settings.data.searchingAccessible", "Searching for devices on your network") :
                          TranslationManager.translate("settings.data.searchAccessible", "Search for other Decenza devices on your network")
                    primary: true
                    enabled: !MainController.dataMigration.isConnecting && !MainController.dataMigration.isImporting &&
                             !MainController.dataMigration.isSearching
                    onClicked: MainController.dataMigration.startDiscovery()
                }

                BusyIndicator {
                    running: MainController.dataMigration.isSearching
                    visible: MainController.dataMigration.isSearching
                    Layout.preferredWidth: Theme.scaled(20)
                    Layout.preferredHeight: Theme.scaled(20)
                }

                Item { Layout.fillWidth: true }
            }

            // Single device - show as clickable card
            Rectangle {
                Layout.fillWidth: true
                height: singleDeviceRow.height + Theme.scaled(16)
                color: singleDeviceMouseArea.containsMouse ? Theme.surfaceColor : Theme.backgroundColor
                radius: Theme.scaled(6)
                border.width: 1
                border.color: Theme.borderColor
                visible: MainController.dataMigration.discoveredDevices.length === 1

                Accessible.role: Accessible.Button
                Accessible.name: {
                    if (MainController.dataMigration.discoveredDevices.length > 0) {
                        var dev = MainController.dataMigration.discoveredDevices[0]
                        return (dev.deviceName || "Unknown Device") + ", " + dev.ipAddress
                    }
                    return ""
                }
                Accessible.focusable: true
                Accessible.onPressAction: singleDeviceMouseArea.clicked(null)

                RowLayout {
                    id: singleDeviceRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(8)
                    spacing: Theme.scaled(10)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                  (MainController.dataMigration.discoveredDevices[0].deviceName || "Unknown Device") : ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        Text {
                            text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                  (MainController.dataMigration.discoveredDevices[0].platform + " • v" +
                                   MainController.dataMigration.discoveredDevices[0].appVersion) : ""
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        text: MainController.dataMigration.discoveredDevices.length > 0 ?
                              MainController.dataMigration.discoveredDevices[0].ipAddress : ""
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }
                }

                MouseArea {
                    id: singleDeviceMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (MainController.dataMigration.discoveredDevices.length > 0) {
                            MainController.dataMigration.connectToServer(
                                MainController.dataMigration.discoveredDevices[0].serverUrl)
                        }
                    }
                }
            }

            // Multiple devices - show combobox with connect button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: MainController.dataMigration.discoveredDevices.length > 1

                StyledComboBox {
                    id: deviceComboBox
                    Layout.fillWidth: true
                    model: MainController.dataMigration.discoveredDevices
                    textRole: "deviceName"
                    accessibleLabel: TranslationManager.translate("settings.data.selectdevice", "Select a device")
                    displayText: currentIndex >= 0 && model.length > 0 ?
                                 model[currentIndex].deviceName + " (" + model[currentIndex].ipAddress + ")" :
                                 TranslationManager.translate("settings.data.selectdevice", "Select a device")
                    textFunction: function(i) {
                        var d = model[i]
                        return d.deviceName + " (" + d.platform + " \u2022 " + d.ipAddress + ")"
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.connect", "Connect")
                    accessibleName: TranslationManager.translate("settings.data.connectAccessible", "Connect to selected device")
                    enabled: deviceComboBox.currentIndex >= 0
                    onClicked: {
                        var device = MainController.dataMigration.discoveredDevices[deviceComboBox.currentIndex]
                        MainController.dataMigration.connectToServer(device.serverUrl)
                    }
                }
            }

            // No devices found message
            Text {
                visible: !MainController.dataMigration.isSearching &&
                         MainController.dataMigration.discoveredDevices.length === 0 &&
                         migrationDialog.searchPerformed
                text: TranslationManager.translate("settings.data.nodeviceshint", "Make sure the other device has Remote Access enabled in History & Data settings.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(11)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // Manual IP entry (shown after search completes, whether devices found or not)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: !MainController.dataMigration.isSearching &&
                         migrationDialog.searchPerformed

                Item { height: Theme.scaled(5) }

                Text {
                    text: TranslationManager.translate("settings.data.manualconnect", "Manual Connection")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.manualconnecthint", "Enter the IP address and port of the device (example: 192.168.1.100:8888)")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    StyledTextField {
                        id: manualIpField
                        Layout.fillWidth: true
                        placeholder: "192.168.1.100:8888"

                        // Auto-focus when visible
                        onVisibleChanged: {
                            if (visible) forceActiveFocus()
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.data.connect", "Connect")
                        accessibleName: TranslationManager.translate("settings.data.connectmanual", "Connect to manually entered address")
                        primary: true
                        enabled: manualIpField.text.trim().length > 0
                        onClicked: {
                            var address = manualIpField.text.trim()
                            // If user didn't include http://, add it
                            if (!address.startsWith("http://") && !address.startsWith("https://")) {
                                address = "http://" + address
                            }
                            MainController.dataMigration.connectToServer(address)
                        }
                    }
                }
            }
        }

        // Connection status
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: MainController.dataMigration.isConnecting

            BusyIndicator {
                running: true
                Layout.preferredWidth: Theme.scaled(20)
                Layout.preferredHeight: Theme.scaled(20)
            }

            Tr {
                key: "settings.data.connecting"
                fallback: "Connecting..."
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
            }
        }

        // Error message
        Text {
            Layout.fillWidth: true
            visible: MainController.dataMigration.errorMessage !== "" &&
                     !MainController.dataMigration.needsAuthentication
            text: MainController.dataMigration.errorMessage
            color: Theme.errorColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.WordWrap
        }

        // Authentication prompt (when server requires TOTP)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: MainController.dataMigration.needsAuthentication

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.data.authneeded",
                    "This device requires authentication. Enter the 6-digit code from your authenticator app.")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(11)
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                StyledTextField {
                    id: migrationTotpField
                    Layout.preferredWidth: Theme.scaled(140)
                    maximumLength: 6
                    inputMethodHints: Qt.ImhDigitsOnly
                    horizontalAlignment: TextInput.AlignHCenter
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                    font.letterSpacing: Theme.scaled(3)
                    placeholder: ""
                    accessibleName: TranslationManager.translate("settings.data.migrationTotpFieldAccessible",
                        "Six digit authenticator code for remote device")

                    onTextChanged: migrationAuthError.text = ""

                    Keys.onReturnPressed: {
                        if (text.length === 6) migrationAuthButton.clicked()
                    }

                    onVisibleChanged: {
                        if (visible) forceActiveFocus()
                    }
                }

                AccessibleButton {
                    id: migrationAuthButton
                    primary: true
                    text: MainController.dataMigration.isConnecting ?
                          TranslationManager.translate("settings.data.authenticating", "Verifying...") :
                          TranslationManager.translate("settings.data.authenticate", "Verify")
                    accessibleName: TranslationManager.translate("settings.data.authenticateAccessible",
                        "Submit authenticator code to connect to remote device")
                    enabled: migrationTotpField.text.length === 6 && !MainController.dataMigration.isConnecting
                    onClicked: {
                        MainController.dataMigration.authenticate(migrationTotpField.text)
                    }
                }
            }

            Text {
                id: migrationAuthError
                Layout.fillWidth: true
                visible: text !== ""
                color: Theme.errorColor
                font.pixelSize: Theme.scaled(11)
                wrapMode: Text.WordWrap
            }
        }

        // Manifest display (when connected)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: manifestColumn.height + Theme.scaled(20)
            color: Theme.backgroundColor
            radius: Theme.scaled(8)
            visible: MainController.dataMigration.manifest && MainController.dataMigration.manifest.deviceName !== undefined

            ColumnLayout {
                id: manifestColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.scaled(10)
                spacing: Theme.scaled(8)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Rectangle {
                        width: Theme.scaled(10)
                        height: Theme.scaled(10)
                        radius: Theme.scaled(5)
                        color: Theme.successColor
                    }

                    Text {
                        text: TranslationManager.translate("settings.data.connectedto", "Connected to:") + " " +
                              (MainController.dataMigration.manifest.deviceName || "Unknown Device")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.data.disconnect", "Disconnect")
                        accessibleName: TranslationManager.translate("settings.data.disconnectAccessible", "Disconnect from remote device")
                        onClicked: MainController.dataMigration.disconnect()
                    }
                }

                // Data available - compact single line
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.profiles", "Profiles") + ": " +
                          (MainController.dataMigration.manifest.profileCount || 0) + " • " +
                          TranslationManager.translate("settings.data.shots", "Shots") + ": " +
                          (MainController.dataMigration.manifest.shotCount || 0) + " • " +
                          TranslationManager.translate("settings.data.media", "Media") + ": " +
                          (MainController.dataMigration.manifest.mediaCount || 0) + " • " +
                          TranslationManager.translate("settings.data.settings", "Settings") + ": " +
                          (MainController.dataMigration.manifest.hasSettings ?
                           TranslationManager.translate("common.yes", "Yes") :
                           TranslationManager.translate("common.no", "No")) +
                          ((MainController.dataMigration.manifest.aiConversationCount || 0) > 0 ?
                           " • " + TranslationManager.translate("settings.data.aiconversations", "AI Conversations") + ": " +
                           MainController.dataMigration.manifest.aiConversationCount : "")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Import progress
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)
            visible: MainController.dataMigration.isImporting

            RowLayout {
                spacing: Theme.scaled(8)

                Text {
                    text: MainController.dataMigration.currentOperation
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                }

                Text {
                    text: Math.round(MainController.dataMigration.progress * 100) + "%"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: MainController.dataMigration.progress
            }
        }

        // Action buttons
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: MainController.dataMigration.manifest && MainController.dataMigration.manifest.deviceName !== undefined

            // Import All button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("settings.data.importall", "Import All")
                    accessibleName: TranslationManager.translate("settings.data.importAllAccessible", "Import all data from remote device including settings, profiles, shots, and media")
                    visible: !MainController.dataMigration.isImporting
                    enabled: !MainController.dataMigration.isImporting
                    onClicked: MainController.dataMigration.importAll()
                }

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.data.cancelImport", "Cancel import operation")
                    visible: MainController.dataMigration.isImporting
                    onClicked: MainController.dataMigration.cancel()
                }

                Item { Layout.fillWidth: true }
            }

            // Individual import buttons
            Flow {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: !MainController.dataMigration.isImporting

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importsettings", "Import Settings")
                    accessibleName: TranslationManager.translate("settings.data.importSettingsAccessible", "Import only settings from remote device")
                    enabled: MainController.dataMigration.manifest.hasSettings === true
                    onClicked: MainController.dataMigration.importOnlySettings()
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importprofiles", "Import Profiles") +
                          " (" + (MainController.dataMigration.manifest.profileCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importProfilesAccessible", "Import only profiles from remote device")
                    enabled: (MainController.dataMigration.manifest.profileCount || 0) > 0
                    onClicked: MainController.dataMigration.importOnlyProfiles()
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importshots", "Import Shots") +
                          " (" + (MainController.dataMigration.manifest.shotCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importShotsAccessible", "Import only shot history from remote device")
                    enabled: (MainController.dataMigration.manifest.shotCount || 0) > 0
                    onClicked: MainController.dataMigration.importOnlyShots()
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importmedia", "Import Media") +
                          " (" + (MainController.dataMigration.manifest.mediaCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importMediaAccessible", "Import only media files from remote device")
                    enabled: (MainController.dataMigration.manifest.mediaCount || 0) > 0
                    onClicked: MainController.dataMigration.importOnlyMedia()
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importaiconversations", "Import AI Conversations") +
                          " (" + (MainController.dataMigration.manifest.aiConversationCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importAIConversationsAccessible", "Import only AI conversations from remote device")
                    enabled: (MainController.dataMigration.manifest.aiConversationCount || 0) > 0
                    onClicked: MainController.dataMigration.importOnlyAIConversations()
                }
            }
        }

        Item { Layout.fillHeight: true }

            // Close button
            AccessibleButton {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.scaled(10)
                text: TranslationManager.translate("common.button.close", "Close")
                accessibleName: TranslationManager.translate("settings.data.closeMigration", "Close device migration")
                onClicked: migrationDialog.close()
            }
        }
        }
    }
}
