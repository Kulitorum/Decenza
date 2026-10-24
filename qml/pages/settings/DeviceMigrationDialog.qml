pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

DecenzaDialog {
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
    property string importResult: ""
    property string migrationError: ""
    property string pendingMigrationId: ""
    property string pendingMigrationOperation: ""
    QtObject {
        id: migrationState
        readonly property var snapshot: DataMigrationService.snapshot
        readonly property bool isConnecting: snapshot.isConnecting === true
        readonly property bool isImporting: snapshot.isImporting === true
        readonly property bool isSearching: snapshot.isSearching === true
        readonly property real progress: snapshot.progress || 0
        readonly property string currentOperation: snapshot.currentOperation || ""
        readonly property string errorMessage: snapshot.errorMessage || ""
        readonly property string aiConversationNote: snapshot.aiConversationNote || ""
        readonly property var manifest: snapshot.manifest || ({})
        readonly property string serverUrl: snapshot.serverUrl || ""
        readonly property var discoveredDevices: snapshot.discoveredDevices || []
        readonly property bool needsAuthentication: snapshot.needsAuthentication === true
    }

    function submitMigration(operation, args) {
        migrationError = ""
        pendingMigrationOperation = operation
        pendingMigrationId = DataMigrationService.execute(operation, args || ({}))
    }

    onOpened: {
        searchPerformed = false
        importResult = ""
        migrationError = ""
        DataMigrationService.refresh()
    }

    Connections {
        target: DataMigrationService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId !== migrationDialog.pendingMigrationId ||
                    operation !== migrationDialog.pendingMigrationOperation)
                return
            migrationDialog.pendingMigrationId = ""
            migrationDialog.pendingMigrationOperation = ""
            if (!success) {
                migrationDialog.migrationError = reason
                if (operation === "authenticate") {
                    migrationTotpField.text = ""
                    migrationAuthError.text = reason
                    migrationTotpField.forceActiveFocus()
                }
                return
            }
            migrationDialog.migrationError = ""
            if (operation === "startDiscovery")
                migrationDialog.searchPerformed = true
            if (operation === "authenticate")
                migrationTotpField.text = ""
            if (operation.indexOf("import") === 0) {
                const parts = []
                if ((result.settingsImported || 0) > 0) parts.push(result.settingsImported + " settings")
                if ((result.profilesImported || 0) > 0) parts.push(result.profilesImported + " profiles")
                if ((result.shotsImported || 0) > 0) parts.push(result.shotsImported + " shots")
                if ((result.mediaImported || 0) > 0) parts.push(result.mediaImported + " media")
                if ((result.aiConversationsImported || 0) > 0) parts.push(result.aiConversationsImported + " conversations")
                const note = result.aiConversationNote || migrationState.aiConversationNote || ""
                const summary = parts.length > 0
                    ? TranslationManager.translate("settings.data.importComplete", "Import complete:") + " " + parts.join(", ")
                    : TranslationManager.translate("settings.data.importNothingNew", "Import complete — nothing new to import")
                migrationDialog.importResult = note ? summary + "\n" + note : summary
                ProfileService.refresh()
            }
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: KeyboardAwareContainer {
        id: migrationKeyboardContainer
        inOverlay: true
        textFields: [manualIpField, migrationTotpField]
        targetFlickable: migrationFlickable

        Flickable {
            id: migrationFlickable
            anchors.fill: parent
            contentHeight: migrationContent.implicitHeight
                           + migrationKeyboardContainer.estimatedKeyboardHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: migrationContent
                width: parent.width
                spacing: Theme.scaled(10)

        RowLayout {
            Layout.fillWidth: true

            Tr {
                key: "settings.data.importfrom"
                fallback: "Import from Another Device"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
                font.bold: true
                Layout.fillWidth: true
                Accessible.ignored: true
            }

            HideKeyboardButton {}
        }

        Tr {
            key: "settings.data.importdesc"
            fallback: "Connect to another Decenza device on your WiFi to import settings, profiles, and shot history."
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Item { Layout.preferredHeight: Theme.scaled(5) }

        // Device discovery and selection
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: !migrationState.isImporting &&
                     !(migrationState.manifest && migrationState.manifest.deviceName !== undefined)

            // Search button and status
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                AccessibleButton {
                    text: migrationState.isSearching ?
                          TranslationManager.translate("settings.data.searching", "Searching...") :
                          TranslationManager.translate("settings.data.searchdevices", "Search for Devices")
                    accessibleName: migrationState.isSearching ?
                          TranslationManager.translate("settings.data.searchingAccessible", "Searching for devices on your network") :
                          TranslationManager.translate("settings.data.searchAccessible", "Search for other Decenza devices on your network")
                    primary: true
                    enabled: !migrationState.isConnecting && !migrationState.isImporting &&
                             !migrationState.isSearching
                    onClicked: migrationDialog.submitMigration("startDiscovery")
                }

                BusyIndicator {
                    running: migrationState.isSearching
                    visible: migrationState.isSearching
                    Layout.preferredWidth: Theme.scaled(20)
                    Layout.preferredHeight: Theme.scaled(20)
                }

                Item { Layout.fillWidth: true }
            }

            // Single device - show as clickable card
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: singleDeviceRow.height + Theme.scaled(16)
                color: singleDeviceMouseArea.containsMouse ? Theme.surfaceColor : Theme.backgroundColor
                radius: Theme.scaled(6)
                border.width: 1
                border.color: Theme.borderColor
                visible: migrationState.discoveredDevices.length === 1

                Accessible.role: Accessible.Button
                Accessible.name: {
                    if (migrationState.discoveredDevices.length > 0) {
                        let dev = migrationState.discoveredDevices[0]
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
                            text: migrationState.discoveredDevices.length > 0 ?
                                  (migrationState.discoveredDevices[0].deviceName || "Unknown Device") : ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        Text {
                            text: migrationState.discoveredDevices.length > 0 ?
                                  (migrationState.discoveredDevices[0].platform + " • v" +
                                   migrationState.discoveredDevices[0].appVersion) : ""
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        text: migrationState.discoveredDevices.length > 0 ?
                              migrationState.discoveredDevices[0].ipAddress : ""
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
                        if (migrationState.discoveredDevices.length > 0) {
                            migrationDialog.submitMigration("connectToServer", { serverUrl: migrationState.discoveredDevices[0].serverUrl })
                        }
                    }
                }
            }

            // Multiple devices - show combobox with connect button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: migrationState.discoveredDevices.length > 1

                StyledComboBox {
                    id: deviceComboBox
                    Layout.fillWidth: true
                    model: migrationState.discoveredDevices
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
                        var device = migrationState.discoveredDevices[deviceComboBox.currentIndex]
                        migrationDialog.submitMigration("connectToServer", { serverUrl: device.serverUrl })
                    }
                }
            }

            // No devices found message
            Text {
                visible: !migrationState.isSearching &&
                         migrationState.discoveredDevices.length === 0 &&
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
                visible: !migrationState.isSearching &&
                         migrationDialog.searchPerformed

                Item { Layout.preferredHeight: Theme.scaled(5) }

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
                            Keyboard.commit()
                            var address = manualIpField.text.trim()
                            // If user didn't include http://, add it
                            if (!address.startsWith("http://") && !address.startsWith("https://")) {
                                address = "http://" + address
                            }
                            migrationDialog.submitMigration("connectToServer", { serverUrl: address })
                        }
                    }
                }
            }
        }

        // Connection status
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: migrationState.isConnecting

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
            visible: (migrationDialog.migrationError !== "" || migrationState.errorMessage !== "") &&
                     !migrationState.needsAuthentication
            text: migrationDialog.migrationError !== "" ? migrationDialog.migrationError : migrationState.errorMessage
            color: Theme.errorColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.WordWrap
        }

        // What the AI-conversation step could not do. Separate from the error
        // above and from importResult below, because it has to appear in BOTH
        // states: a refused shot history ends the run with an error AND holds
        // every conversation back, and the import-complete summary never renders
        // on that path.
        Text {
            Layout.fillWidth: true
            visible: text.length > 0 && (migrationState.errorMessage !== "" || migrationDialog.migrationError !== "")
            text: migrationState.aiConversationNote
            color: Theme.warningColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // Authentication prompt (when server requires TOTP)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: migrationState.needsAuthentication

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
                    text: migrationState.isConnecting ?
                          TranslationManager.translate("settings.data.authenticating", "Verifying...") :
                          TranslationManager.translate("settings.data.authenticate", "Verify")
                    accessibleName: TranslationManager.translate("settings.data.authenticateAccessible",
                        "Submit authenticator code to connect to remote device")
                    enabled: migrationTotpField.text.length === 6 && !migrationState.isConnecting
                    onClicked: {
                        Keyboard.commit()
                        migrationDialog.submitMigration("authenticate", { totpCode: migrationTotpField.text })
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
            visible: migrationState.manifest && migrationState.manifest.deviceName !== undefined

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
                        Layout.preferredWidth: Theme.scaled(10)
                        Layout.preferredHeight: Theme.scaled(10)
                        radius: Theme.scaled(5)
                        color: Theme.successColor
                    }

                    Text {
                        text: TranslationManager.translate("settings.data.connectedto", "Connected to:") + " " +
                              (migrationState.manifest.deviceName || "Unknown Device")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.data.disconnect", "Disconnect")
                        accessibleName: TranslationManager.translate("settings.data.disconnectAccessible", "Disconnect from remote device")
                        onClicked: migrationDialog.submitMigration("disconnect")
                    }
                }

                // Data available - compact single line
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.profiles", "Profiles") + ": " +
                          (migrationState.manifest.profileCount || 0) + " • " +
                          TranslationManager.translate("settings.data.shots", "Shots") + ": " +
                          (migrationState.manifest.shotCount || 0) + " • " +
                          TranslationManager.translate("settings.data.media", "Media") + ": " +
                          (migrationState.manifest.mediaCount || 0) + " • " +
                          TranslationManager.translate("settings.data.settings", "Settings") + ": " +
                          (migrationState.manifest.hasSettings ?
                           TranslationManager.translate("common.yes", "Yes") :
                           TranslationManager.translate("common.no", "No")) +
                          ((migrationState.manifest.aiConversationCount || 0) > 0 ?
                           " • " + TranslationManager.translate("settings.data.aiconversations", "AI Conversations") + ": " +
                           migrationState.manifest.aiConversationCount : "")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                }

                // Recipes, bags, and equipment ride inside the Shots import —
                // surface them so they aren't invisible (finish-recipes-first-class).
                Text {
                    Layout.fillWidth: true
                    property int recipeCount: migrationState.manifest.recipeCount || 0
                    property int bagCount: migrationState.manifest.bagCount || 0
                    property int equipmentCount: migrationState.manifest.equipmentCount || 0
                    visible: recipeCount > 0 || bagCount > 0 || equipmentCount > 0
                    text: TranslationManager.translate("settings.data.withShots", "With shots") + ": " +
                          TranslationManager.translate("settings.data.recipes", "Recipes") + ": " + recipeCount + " • " +
                          TranslationManager.translate("settings.data.bags", "Bags") + ": " + bagCount + " • " +
                          TranslationManager.translate("settings.data.equipment", "Equipment") + ": " + equipmentCount
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
            visible: migrationState.isImporting

            RowLayout {
                spacing: Theme.scaled(8)

                Text {
                    text: migrationState.currentOperation
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                }

                Text {
                    text: Math.round(migrationState.progress * 100) + "%"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: migrationState.progress
            }
        }

        // Import result
        Text {
            Layout.fillWidth: true
            visible: migrationDialog.importResult !== ""
            text: migrationDialog.importResult
            color: Theme.successColor
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WordWrap
        }

        // Action buttons
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: migrationState.manifest && migrationState.manifest.deviceName !== undefined

            // Import All button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("settings.data.importall", "Import All")
                    accessibleName: TranslationManager.translate("settings.data.importAllAccessible", "Import all data from remote device including settings, profiles, shots, and media")
                    visible: !migrationState.isImporting
                    enabled: !migrationState.isImporting
                    onClicked: migrationDialog.submitMigration("importAll")
                }

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.data.cancelImport", "Cancel import operation")
                    visible: migrationState.isImporting
                    onClicked: migrationDialog.submitMigration("cancel")
                }

                Item { Layout.fillWidth: true }
            }

            // Individual import buttons
            Flow {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: !migrationState.isImporting

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importsettings", "Import Settings")
                    accessibleName: TranslationManager.translate("settings.data.importSettingsAccessible", "Import only settings from remote device")
                    enabled: migrationState.manifest.hasSettings === true
                    onClicked: migrationDialog.submitMigration("importOnlySettings")
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importprofiles", "Import Profiles") +
                          " (" + (migrationState.manifest.profileCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importProfilesAccessible", "Import only profiles from remote device")
                    enabled: (migrationState.manifest.profileCount || 0) > 0
                    onClicked: migrationDialog.submitMigration("importOnlyProfiles")
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importshots", "Import Shots") +
                          " (" + (migrationState.manifest.shotCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importShotsAccessible", "Import only shot history from remote device")
                    enabled: (migrationState.manifest.shotCount || 0) > 0
                    onClicked: migrationDialog.submitMigration("importOnlyShots")
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importmedia", "Import Media") +
                          " (" + (migrationState.manifest.mediaCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importMediaAccessible", "Import only media files from remote device")
                    enabled: (migrationState.manifest.mediaCount || 0) > 0
                    onClicked: migrationDialog.submitMigration("importOnlyMedia")
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.data.importaiconversations", "Import AI Conversations") +
                          " (" + (migrationState.manifest.aiConversationCount || 0) + ")"
                    accessibleName: TranslationManager.translate("settings.data.importAIConversationsAccessible", "Import only AI conversations from remote device")
                    enabled: (migrationState.manifest.aiConversationCount || 0) > 0
                    onClicked: migrationDialog.submitMigration("importOnlyAIConversations")
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
