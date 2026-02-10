import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

KeyboardAwareContainer {
    id: dataTab
    textFields: [manualIpField]

    // Track backup operation state
    property bool backupInProgress: false

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Server status (source device)
        Rectangle {
            Layout.preferredWidth: Theme.scaled(280)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.data.sharedata"
                    fallback: "Share Data"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                // Server status
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(50)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    Tr {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        key: MainController.shotServer.running ? "settings.data.serverready" : "settings.data.enableinhistory"
                        fallback: MainController.shotServer.running ? "Server is ready for connections." : "Enable 'Remote Access' in the Shot History tab to share data."
                        color: MainController.shotServer.running ? Theme.successColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }
                }

                Item { Layout.fillHeight: true }

                // Data summary
                Tr {
                    key: "settings.data.yourdata"
                    fallback: "Your Data"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: Theme.scaled(4)
                    columnSpacing: Theme.scaled(10)

                    Tr {
                        key: "settings.data.shots"
                        fallback: "Shots:"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }
                    Text {
                        text: MainController.shotHistory ? MainController.shotHistory.totalShots : 0
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    Tr {
                        key: "settings.data.profiles"
                        fallback: "Profiles:"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }
                    Text {
                        text: MainController.availableProfiles.length
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }
            }
        }

        // Middle column: Daily Backup
        Rectangle {
            Layout.preferredWidth: Theme.scaled(280)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.data.dailybackup"
                    fallback: "Daily Backup"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                Tr {
                    key: "settings.data.dailybackupdesc"
                    fallback: "Automatically backup your shot history database once per day. Backups are saved to your Documents folder and kept for 5 days."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item { height: Theme.scaled(5) }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.data.backuptime"
                        fallback: "Backup Time:"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    StyledComboBox {
                        id: backupTimeCombo
                        Layout.fillWidth: true
                        model: {
                            var times = [TranslationManager.translate("settings.data.backupoff", "Off")];
                            for (var hour = 0; hour < 24; hour++) {
                                var hourStr = hour.toString().padStart(2, '0');
                                times.push(hourStr + ":00");
                            }
                            return times;
                        }
                        currentIndex: Settings.dailyBackupHour + 1  // +1 because "Off" is index 0
                        onCurrentIndexChanged: {
                            Settings.dailyBackupHour = currentIndex - 1;  // -1 to map back to hour (-1 = off)
                        }
                    }
                }

                // Status text
                Text {
                    Layout.fillWidth: true
                    visible: Settings.dailyBackupHour >= 0
                    text: {
                        var hour = Settings.dailyBackupHour.toString().padStart(2, '0');
                        return TranslationManager.translate("settings.data.nextbackup",
                            "Next backup: today at %1:00").replace("%1", hour);
                    }
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    wrapMode: Text.WordWrap
                }

                // Backup location
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.backuplocation",
                        "Backups are saved to:") + "\nDocuments/Decenza Backups/"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    wrapMode: Text.WordWrap
                }

                // Permission warning (Android only)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(60)
                    visible: Qt.platform.os === "android" &&
                             MainController.backupManager &&
                             !MainController.backupManager.hasStoragePermission()
                    color: Theme.warningColor
                    opacity: 0.1
                    radius: Theme.scaled(4)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        spacing: Theme.scaled(4)

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.data.permissionneeded",
                                "⚠ Storage permission required")
                            color: Theme.warningColor
                            font.pixelSize: Theme.scaled(11)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.data.permissiondesc",
                                "To save backups to your Documents folder, grant storage access.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Permission request button (Android only)
                AccessibleButton {
                    Layout.alignment: Qt.AlignLeft
                    visible: Qt.platform.os === "android" &&
                             MainController.backupManager &&
                             !MainController.backupManager.hasStoragePermission()
                    text: TranslationManager.translate("settings.data.grantpermission", "Grant Storage Permission")
                    accessibleName: TranslationManager.translate("settings.data.grantpermissionAccessible",
                        "Open settings to grant storage permission")
                    onClicked: {
                        if (MainController.backupManager) {
                            MainController.backupManager.requestStoragePermission();
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Manual backup button with loading indicator
                RowLayout {
                    Layout.alignment: Qt.AlignLeft
                    spacing: Theme.scaled(8)

                    AccessibleButton {
                        id: backupNowButton
                        enabled: (!Qt.platform.os === "android" ||
                                 (MainController.backupManager && MainController.backupManager.hasStoragePermission())) &&
                                 !dataTab.backupInProgress
                        text: dataTab.backupInProgress ?
                              TranslationManager.translate("settings.data.backingup", "Creating Backup...") :
                              TranslationManager.translate("settings.data.backupnow", "Backup Now")
                        accessibleName: TranslationManager.translate("settings.data.backupnowAccessible",
                            "Create a manual backup of shot history database")
                        onClicked: {
                            if (MainController.backupManager) {
                                dataTab.backupInProgress = true;
                                MainController.backupManager.createBackup(true); // force=true for manual backup
                            }
                        }
                    }

                    BusyIndicator {
                        visible: dataTab.backupInProgress
                        running: dataTab.backupInProgress
                        implicitWidth: Theme.scaled(20)
                        implicitHeight: Theme.scaled(20)
                    }
                }

                Item { height: Theme.scaled(10) }

                // Restore from backup section
                Tr {
                    key: "settings.data.restorefrombackup"
                    fallback: "Restore from Backup:"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(12)
                }

                StyledComboBox {
                    id: restoreBackupCombo
                    Layout.fillWidth: true
                    enabled: MainController.backupManager && availableBackups.length > 0
                    model: availableBackups.length > 0 ? availableBackups : [TranslationManager.translate("settings.data.nobackups", "No backups available")]
                    currentIndex: 0

                    property var availableBackups: MainController.backupManager ? getBackupList() : []

                    function getBackupList() {
                        var backups = MainController.backupManager.getAvailableBackups();
                        var displayList = [];
                        backupFilenames = [];  // Store actual filenames separately

                        for (var i = 0; i < backups.length; i++) {
                            var parts = backups[i].split("|");
                            if (parts.length === 2) {
                                displayList.push(parts[0]);  // Display name
                                backupFilenames.push(parts[1]);  // Actual filename
                            }
                        }

                        return displayList.length > 0 ? displayList : [];
                    }

                    property var backupFilenames: []

                    // Refresh list when backup is created
                    Connections {
                        target: MainController.backupManager
                        function onBackupCreated() {
                            restoreBackupCombo.availableBackups = restoreBackupCombo.getBackupList();
                            restoreBackupCombo.model = restoreBackupCombo.availableBackups.length > 0 ?
                                restoreBackupCombo.availableBackups :
                                [TranslationManager.translate("settings.data.nobackups", "No backups available")];
                        }
                    }
                }

                AccessibleButton {
                    Layout.alignment: Qt.AlignLeft
                    text: TranslationManager.translate("settings.data.restorebutton", "Restore Selected Backup")
                    enabled: MainController.backupManager &&
                             restoreBackupCombo.availableBackups.length > 0 &&
                             restoreBackupCombo.currentIndex >= 0
                    accessibleName: TranslationManager.translate("settings.data.restorebuttonAccessible",
                        "Restore selected backup and replace current shot history")
                    onClicked: {
                        if (MainController.backupManager && restoreBackupCombo.currentIndex >= 0) {
                            restoreConfirmDialog.selectedBackup = restoreBackupCombo.backupFilenames[restoreBackupCombo.currentIndex];
                            restoreConfirmDialog.displayName = restoreBackupCombo.availableBackups[restoreBackupCombo.currentIndex];
                            restoreConfirmDialog.open();
                        }
                    }
                }
            }
        }

        // Right column: Import from another device
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
                                }

                                Text {
                                    text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                          (MainController.dataMigration.discoveredDevices[0].platform + " • v" +
                                           MainController.dataMigration.discoveredDevices[0].appVersion) : ""
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(11)
                                }
                            }

                            Text {
                                text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                      MainController.dataMigration.discoveredDevices[0].ipAddress : ""
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
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

                        ComboBox {
                            id: deviceComboBox
                            Layout.fillWidth: true
                            model: MainController.dataMigration.discoveredDevices
                            textRole: "deviceName"
                            displayText: currentIndex >= 0 && model.length > 0 ?
                                         model[currentIndex].deviceName + " (" + model[currentIndex].ipAddress + ")" :
                                         TranslationManager.translate("settings.data.selectdevice", "Select a device")

                            delegate: ItemDelegate {
                                width: deviceComboBox.width
                                contentItem: ColumnLayout {
                                    spacing: 2
                                    Text {
                                        text: modelData.deviceName || "Unknown Device"
                                        color: Theme.textColor
                                        font.pixelSize: Theme.scaled(13)
                                    }
                                    Text {
                                        text: modelData.platform + " • " + modelData.ipAddress
                                        color: Theme.textSecondaryColor
                                        font.pixelSize: Theme.scaled(11)
                                    }
                                }
                                highlighted: deviceComboBox.highlightedIndex === index
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
                                 MainController.dataMigration.currentOperation === TranslationManager.translate("settings.data.nodevices", "No devices found")
                        text: TranslationManager.translate("settings.data.nodeviceshint", "Make sure the other device has Remote Access enabled in Shot History settings.")
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
                                 MainController.dataMigration.currentOperation !== "" &&
                                 MainController.dataMigration.currentOperation !== TranslationManager.translate("settings.data.searchdevices", "Search for Devices")

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
                                placeholderText: "192.168.1.100:8888"

                                // Auto-focus when visible
                                Component.onCompleted: {
                                    if (visible) {
                                        forceActiveFocus()
                                    }
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
                    visible: MainController.dataMigration.errorMessage !== ""
                    text: MainController.dataMigration.errorMessage
                    color: Theme.errorColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
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
                                   TranslationManager.translate("common.no", "No"))
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
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Import complete notification
    Connections {
        target: MainController.dataMigration

        function onImportComplete(settingsImported, profilesImported, shotsImported, mediaImported) {
            importCompletePopup.settingsCount = settingsImported
            importCompletePopup.profilesCount = profilesImported
            importCompletePopup.shotsCount = shotsImported
            importCompletePopup.mediaCount = mediaImported
            importCompletePopup.open()

            // Refresh profiles list
            MainController.refreshProfiles()
        }

        function onConnectionFailed(error) {
            // Error is already shown via errorMessage property
        }
    }

    // Import complete popup
    Popup {
        id: importCompletePopup
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.scaled(300)
        padding: Theme.scaled(20)

        property int settingsCount: 0
        property int profilesCount: 0
        property int shotsCount: 0
        property int mediaCount: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Rectangle {
                    width: Theme.scaled(24)
                    height: Theme.scaled(24)
                    radius: Theme.scaled(12)
                    color: Theme.successColor

                    Text {
                        anchors.centerIn: parent
                        text: "\u2713"
                        color: "white"
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }
                }

                Tr {
                    key: "settings.data.importcomplete"
                    fallback: "Import Complete"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }
            }

            GridLayout {
                Layout.alignment: Qt.AlignHCenter
                columns: 2
                rowSpacing: Theme.scaled(6)
                columnSpacing: Theme.scaled(15)

                Tr {
                    key: "settings.data.settings"
                    fallback: "Settings:"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                    visible: importCompletePopup.settingsCount > 0
                }
                Text {
                    text: importCompletePopup.settingsCount
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    visible: importCompletePopup.settingsCount > 0
                }

                Tr {
                    key: "settings.data.profiles"
                    fallback: "Profiles:"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                }
                Text {
                    text: importCompletePopup.profilesCount
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                }

                Tr {
                    key: "settings.data.shots"
                    fallback: "Shots:"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                }
                Text {
                    text: importCompletePopup.shotsCount
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                }

                Tr {
                    key: "settings.data.media"
                    fallback: "Media:"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                    visible: importCompletePopup.mediaCount > 0
                }
                Text {
                    text: importCompletePopup.mediaCount
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    visible: importCompletePopup.mediaCount > 0
                }
            }

            AccessibleButton {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("common.ok", "OK")
                accessibleName: TranslationManager.translate("settings.data.closeImportDialog", "Close import complete dialog")
                onClicked: importCompletePopup.close()
            }
        }
    }

    // Backup manager signal handlers
    Connections {
        target: MainController.backupManager

        function onBackupCreated(path) {
            console.log("Backup created:", path);
            dataTab.backupInProgress = false;
            backupStatusText.text = TranslationManager.translate("settings.data.backupsuccess", "✓ Backup created successfully");
            backupStatusText.color = Theme.successColor;
            backupStatusBackground.visible = true;
            backupStatusTimer.restart();

            // TTS announcement for accessibility
            if (MainController.accessibilityManager) {
                MainController.accessibilityManager.announce(
                    TranslationManager.translate("settings.data.backupcreatedAccessible",
                        "Backup created successfully")
                );
            }
        }

        function onBackupFailed(error) {
            console.error("Backup failed:", error);
            dataTab.backupInProgress = false;
            backupStatusText.text = "✗ " + error;
            backupStatusText.color = Theme.errorColor;
            backupStatusBackground.visible = true;
            backupStatusTimer.restart();

            // TTS announcement for accessibility
            if (MainController.accessibilityManager) {
                MainController.accessibilityManager.announce(
                    TranslationManager.translate("settings.data.backupfailedAccessible",
                        "Backup failed: ") + error
                );
            }
        }

        function onStoragePermissionNeeded() {
            console.log("Storage permission needed - user should grant access");
            dataTab.backupInProgress = false;
        }
    }

    // Status message for backup operations
    Rectangle {
        id: backupStatusBackground
        visible: false
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.scaled(20)
        width: backupStatusText.implicitWidth + Theme.scaled(20)
        height: backupStatusText.implicitHeight + Theme.scaled(20)
        color: Theme.surfaceColor
        radius: Theme.scaled(4)
        border.color: Theme.borderColor
        border.width: 1
        z: 100

        Text {
            id: backupStatusText
            anchors.centerIn: parent
            font.pixelSize: Theme.scaled(12)
        }
    }

    // Timer to auto-hide status message
    Timer {
        id: backupStatusTimer
        interval: 5000
        onTriggered: backupStatusBackground.visible = false
    }

    // Restore confirmation dialog
    Dialog {
        id: restoreConfirmDialog
        anchors.centerIn: parent
        modal: true
        title: TranslationManager.translate("settings.data.restoredialog", "Restore Backup?")

        Accessible.role: Accessible.Dialog
        Accessible.name: title + ". " + TranslationManager.translate("settings.data.dialogdesc",
            "Choose restore mode and confirm operation")

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        property string selectedBackup: ""
        property string displayName: ""
        property bool mergeMode: true  // Default to merge

        contentItem: FocusScope {
            implicitWidth: Theme.scaled(400)
            implicitHeight: dialogColumn.implicitHeight

            Component.onCompleted: {
                // Set initial focus to first radio button
                mergeRadio.forceActiveFocus()
            }

            ColumnLayout {
                id: dialogColumn
                anchors.fill: parent
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.restorewarning",
                        "Are you sure you want to restore this backup?")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                    wrapMode: Text.WordWrap

                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Text {
                    Layout.fillWidth: true
                    text: restoreConfirmDialog.displayName
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap

                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("settings.data.backupfile", "Backup file: ") + text
                }

            // Restore mode selection
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: restoreModeColumn.implicitHeight + Theme.scaled(16)
                color: Theme.backgroundColor
                radius: Theme.scaled(6)

                ColumnLayout {
                    id: restoreModeColumn
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(8)
                    spacing: Theme.scaled(8)

                    Text {
                        text: TranslationManager.translate("settings.data.restoremode", "Restore Mode:")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        font.bold: true
                    }

                    RadioButton {
                        id: mergeRadio
                        text: TranslationManager.translate("settings.data.mergemode", "Merge with existing shots")
                        checked: true
                        font.pixelSize: Theme.scaled(11)
                        onCheckedChanged: {
                            if (checked) restoreConfirmDialog.mergeMode = true
                        }

                        Accessible.role: Accessible.RadioButton
                        Accessible.name: text + ". " + TranslationManager.translate("settings.data.mergemodedesc",
                            "Adds shots from backup to your current history. Existing shots are preserved.")
                        Accessible.focusable: true
                        Accessible.onPressAction: { checked = true }

                        KeyNavigation.tab: replaceRadio
                        KeyNavigation.backtab: confirmButton

                        contentItem: Text {
                            text: mergeRadio.text
                            font: mergeRadio.font
                            color: Theme.textColor
                            leftPadding: mergeRadio.indicator.width + mergeRadio.spacing
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(28)
                        text: TranslationManager.translate("settings.data.mergemodedesc",
                            "Adds shots from backup to your current history. Existing shots are preserved.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                    }

                    RadioButton {
                        id: replaceRadio
                        text: TranslationManager.translate("settings.data.replacemode", "Replace all shots")
                        checked: false
                        font.pixelSize: Theme.scaled(11)
                        onCheckedChanged: {
                            if (checked) restoreConfirmDialog.mergeMode = false
                        }

                        Accessible.role: Accessible.RadioButton
                        Accessible.name: text + ". " + TranslationManager.translate("settings.data.replacewarning",
                            "Warning: Deletes ALL current shots and replaces with backup. Cannot be undone!")
                        Accessible.focusable: true
                        Accessible.onPressAction: { checked = true }

                        KeyNavigation.tab: cancelButton
                        KeyNavigation.backtab: mergeRadio

                        contentItem: Text {
                            text: replaceRadio.text
                            font: replaceRadio.font
                            color: Theme.textColor
                            leftPadding: replaceRadio.indicator.width + replaceRadio.spacing
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(28)
                        text: TranslationManager.translate("settings.data.replacemodedesc",
                            "⚠️ Deletes ALL current shots and replaces with backup. Cannot be undone!")
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.scaled(10)

                AccessibleButton {
                    id: cancelButton
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.data.cancelrestore", "Cancel restore operation")

                        KeyNavigation.tab: confirmButton
                        KeyNavigation.backtab: replaceRadio

                        onClicked: {
                            // Reset to merge mode for next time
                            mergeRadio.checked = true
                            restoreConfirmDialog.close()
                        }
                    }

                    AccessibleButton {
                        id: confirmButton
                        text: TranslationManager.translate("common.restore", "Restore")
                        primary: true
                        accessibleName: TranslationManager.translate("settings.data.confirmrestore", "Confirm restore backup")

                        KeyNavigation.tab: mergeRadio
                        KeyNavigation.backtab: cancelButton

                        onClicked: {
                            if (MainController.backupManager) {
                                MainController.backupManager.restoreBackup(
                                    restoreConfirmDialog.selectedBackup,
                                    restoreConfirmDialog.mergeMode
                                );
                            }
                            // Reset to merge mode for next time
                            mergeRadio.checked = true
                            restoreConfirmDialog.close();
                        }
                    }
                }
            }
        }
    }

    // Restore result handlers
    Connections {
        target: MainController.backupManager

        function onRestoreCompleted(filename) {
            console.log("Restore completed:", filename);
            restartDialog.open();

            // TTS announcement for accessibility
            if (MainController.accessibilityManager) {
                MainController.accessibilityManager.announce(
                    TranslationManager.translate("settings.data.restorecompletedAccessible",
                        "Backup restored successfully. Restart required.")
                );
            }
        }

        function onRestoreFailed(error) {
            console.error("Restore failed:", error);
            backupStatusText.text = "✗ " + error;
            backupStatusText.color = Theme.errorColor;
            backupStatusBackground.visible = true;
            backupStatusTimer.restart();

            // TTS announcement for accessibility
            if (MainController.accessibilityManager) {
                MainController.accessibilityManager.announce(
                    TranslationManager.translate("settings.data.restorefailedAccessible",
                        "Restore failed: ") + error
                );
            }
        }
    }

    // Restart dialog after successful restore
    Dialog {
        id: restartDialog
        anchors.centerIn: parent
        modal: true
        title: TranslationManager.translate("settings.data.restartneeded", "Restart Required")
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.data.restartmessage",
                    "Backup restored successfully! The app needs to restart to load the imported shots.")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                wrapMode: Text.WordWrap
            }

            AccessibleButton {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("settings.data.restartnow", "Restart Now")
                primary: true
                accessibleName: TranslationManager.translate("settings.data.restartnowAccessible", "Restart application")
                onClicked: {
                    Qt.quit();
                }
            }
        }
    }
}
