import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: historyDataTab
    textFields: [totpCodeField]

    // Track backup operation state
    property bool backupInProgress: false
    property bool restoreInProgress: false
    // Cache hasStoragePermission()
    property bool hasStoragePerm: Qt.platform.os !== "android" ||
        (MainController.backupManager ? MainController.backupManager.hasStoragePermission() : false)
    function recheckStoragePermission() {
        hasStoragePerm = Qt.platform.os !== "android" ||
            (MainController.backupManager ? MainController.backupManager.hasStoragePermission() : false)
    }
    onVisibleChanged: {
        if (visible) recheckStoragePermission()
    }

    // Hidden helper for clipboard copy
    TextEdit {
        id: clipboardHelper
        visible: false
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Shot History stats and import
        Rectangle {
            objectName: "shotHistory"
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(12)
                spacing: Theme.scaled(6)

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.history.title", "Shot History") + " →"
                    accessibleName: TranslationManager.translate("settings.history.openShotHistory", "Open Shot History")
                    primary: true
                    onClicked: pageStack.push(Qt.resolvedUrl("../ShotHistoryPage.qml"))
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.history.storedlocally"
                    fallback: "All shots are stored locally on your device"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                }

                // Stats - single line
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Tr {
                        key: "settings.history.totalshots"
                        fallback: "Total Shots:"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Text {
                        text: MainController.shotHistory ? MainController.shotHistory.totalShots : "0"
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(12)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Import section
                Text {
                    text: TranslationManager.translate("settings.history.importFromDE1", "Import from DE1 App")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                }

                // Overwrite toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: TranslationManager.translate("settings.history.overwriteExisting", "Overwrite existing")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Layout.fillWidth: true
                    }

                    StyledSwitch {
                        id: overwriteSwitch
                        checked: false
                        accessibleName: TranslationManager.translate("settings.history.overwriteExisting", "Overwrite existing")
                    }
                }

                // Progress bar (visible during import)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)
                    visible: MainController.shotImporter && MainController.shotImporter.isImporting

                    Text {
                        text: MainController.shotImporter ? MainController.shotImporter.statusMessage : ""
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: MainController.shotImporter ? MainController.shotImporter.totalFiles : 1
                        value: MainController.shotImporter ? MainController.shotImporter.processedFiles : 0

                        background: Rectangle {
                            implicitHeight: Theme.scaled(6)
                            color: Theme.backgroundColor
                            radius: Theme.scaled(3)
                        }

                        contentItem: Item {
                            implicitHeight: Theme.scaled(6)
                            Rectangle {
                                width: parent.width * (MainController.shotImporter && MainController.shotImporter.totalFiles > 0 ?
                                       MainController.shotImporter.processedFiles / MainController.shotImporter.totalFiles : 0)
                                height: parent.height
                                radius: Theme.scaled(3)
                                color: Theme.primaryColor
                            }
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.shotHistory.accessibility.cancelImport", "Cancel import")
                        Layout.alignment: Qt.AlignRight
                        onClicked: {
                            if (MainController.shotImporter) {
                                MainController.shotImporter.cancel()
                            }
                        }
                    }
                }

                // Import buttons (visible when not importing)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)
                    visible: !MainController.shotImporter || !MainController.shotImporter.isImporting

                    // DE1 App detection info
                    Text {
                        id: de1AppStatus
                        Layout.fillWidth: true
                        property string detectedPath: MainController.shotImporter ? MainController.shotImporter.detectDE1AppHistoryPath() : ""
                        text: detectedPath ? (TranslationManager.translate("settings.history.found", "Found") + ": " + detectedPath) : TranslationManager.translate("settings.history.de1AppNotFound", "DE1 app not found on device")
                        color: detectedPath ? Theme.successColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(9)
                        wrapMode: Text.Wrap
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.history.importFromDE1", "Import from DE1 App")
                        accessibleName: TranslationManager.translate("settings.history.importFromDE1Desc", "Auto-detect and import from DE1 tablet app")
                        visible: de1AppStatus.detectedPath !== ""
                        onClicked: {
                            if (MainController.shotImporter) {
                                MainController.shotImporter.importFromDE1App(overwriteSwitch.checked)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.history.zip", "ZIP...")
                            accessibleName: TranslationManager.translate("settings.history.importFromZip", "Import shot history from ZIP archive")
                            onClicked: {
                                shotZipDialog.overwrite = overwriteSwitch.checked
                                shotZipDialog.open()
                            }
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.history.folder", "Folder...")
                            accessibleName: TranslationManager.translate("settings.history.importFromFolder", "Import shot history from folder")
                            onClicked: {
                                shotFolderDialog.overwrite = overwriteSwitch.checked
                                shotFolderDialog.open()
                            }
                        }
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Import from another device
                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.importfrom", "Import from Another Device") + "..."
                    accessibleName: TranslationManager.translate("settings.data.importfromAccessible", "Import data from another Decenza device on your network")
                    onClicked: deviceMigrationDialog.open()
                }
            }
        }

        // Middle column: Daily Backup
        Rectangle {
            objectName: "dailyBackup"
            Layout.preferredWidth: Theme.scaled(280)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                id: backupColumn
                anchors.fill: parent
                anchors.margins: Theme.scaled(10)
                spacing: Theme.scaled(4)

                Tr {
                    key: "settings.data.dailybackup"
                    fallback: "Daily Backup"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                }

                Tr {
                    key: "settings.data.dailybackupdesc"
                    fallback: "Auto-backup shots, settings, profiles, and media daily. Saved to Documents folder, kept for 5 days."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

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
                        accessibleLabel: TranslationManager.translate("settings.data.backuptime", "Backup time")
                        model: {
                            var times = [TranslationManager.translate("settings.data.backupoff", "Off")];
                            for (var hour = 0; hour < 24; hour++) {
                                var hourStr = hour.toString().padStart(2, '0');
                                times.push(hourStr + ":00");
                            }
                            return times;
                        }
                        currentIndex: Settings.dailyBackupHour + 1  // +1 because "Off" is index 0
                        onActivated: {
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
                    Layout.preferredHeight: Theme.scaled(50)
                    visible: Qt.platform.os === "android" &&
                             MainController.backupManager &&
                             !historyDataTab.hasStoragePerm
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.1)
                    radius: Theme.scaled(4)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)

                            Image {
                                source: Theme.emojiToImage("\u26A0")
                                sourceSize.width: Theme.scaled(11)
                                sourceSize.height: Theme.scaled(11)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: TranslationManager.translate("settings.data.permissionneeded",
                                    "Storage permission required")
                                color: Theme.warningColor
                                font.pixelSize: Theme.scaled(11)
                                font.bold: true
                            }
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
                             !historyDataTab.hasStoragePerm
                    text: TranslationManager.translate("settings.data.grantpermission", "Grant Storage Permission")
                    accessibleName: TranslationManager.translate("settings.data.grantpermissionAccessible",
                        "Open settings to grant storage permission")
                    onClicked: {
                        if (MainController.backupManager) {
                            MainController.backupManager.requestStoragePermission();
                            historyDataTab.recheckStoragePermission();
                        }
                    }
                }

                // Manual backup button with loading indicator
                RowLayout {
                    Layout.alignment: Qt.AlignLeft
                    spacing: Theme.scaled(8)

                    AccessibleButton {
                        id: backupNowButton
                        enabled: historyDataTab.hasStoragePerm && !historyDataTab.backupInProgress
                        text: historyDataTab.backupInProgress ?
                              TranslationManager.translate("settings.data.backingup", "Creating Backup...") :
                              TranslationManager.translate("settings.data.backupnow", "Backup Now")
                        accessibleName: TranslationManager.translate("settings.data.backupnowAccessible",
                            "Create a manual backup of shots, settings, profiles, and media")
                        onClicked: {
                            if (MainController.backupManager) {
                                historyDataTab.backupInProgress = true;
                                if (!MainController.backupManager.createBackup(true)) {
                                    historyDataTab.backupInProgress = false;
                                }
                            }
                        }
                    }

                    BusyIndicator {
                        visible: historyDataTab.backupInProgress
                        running: historyDataTab.backupInProgress
                        implicitWidth: Theme.scaled(20)
                        implicitHeight: Theme.scaled(20)
                    }
                }

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
                    accessibleLabel: TranslationManager.translate("settings.data.restorefrombackup", "Restore backup")
                    enabled: MainController.backupManager && displayNames.length > 0
                    model: displayNames.length > 0 ? displayNames : [TranslationManager.translate("settings.data.nobackups", "No backups available")]
                    currentIndex: 0

                    // Derived from the cached C++ property (no blocking I/O)
                    readonly property var rawBackups: MainController.backupManager ? MainController.backupManager.availableBackups : []
                    readonly property var displayNames: {
                        var list = [];
                        for (var i = 0; i < rawBackups.length; i++) {
                            var parts = rawBackups[i].split("|");
                            if (parts.length === 2) list.push(parts[0]);
                        }
                        return list;
                    }
                    readonly property var backupFilenames: {
                        var list = [];
                        for (var i = 0; i < rawBackups.length; i++) {
                            var parts = rawBackups[i].split("|");
                            if (parts.length === 2) list.push(parts[1]);
                        }
                        return list;
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.restorebutton", "Restore Backup")
                    enabled: MainController.backupManager &&
                             restoreBackupCombo.displayNames.length > 0 &&
                             restoreBackupCombo.currentIndex >= 0 &&
                             !historyDataTab.restoreInProgress && !historyDataTab.backupInProgress
                    accessibleName: TranslationManager.translate("settings.data.restorebuttonAccessible",
                        "Restore shots, settings, profiles, and media from selected backup")
                    onClicked: {
                        if (MainController.backupManager && restoreBackupCombo.currentIndex >= 0) {
                            restoreConfirmDialog.selectedBackup = restoreBackupCombo.backupFilenames[restoreBackupCombo.currentIndex];
                            restoreConfirmDialog.displayName = restoreBackupCombo.displayNames[restoreBackupCombo.currentIndex];
                            restoreConfirmDialog.open();
                        }
                    }
                }
            }
        }

        // Right column: Server & Security
        Rectangle {
            objectName: "enableServer"
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

                // Server enable toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.history.enableserver"
                            fallback: "Enable Server"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Tr {
                            key: "settings.data.enableserverdesc"
                            fallback: "Access shot data, layout editor, and AI from your browser"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(9)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: Settings.shotServerEnabled
                        accessibleName: TranslationManager.translate("settings.history.enableserver", "Enable Server")
                        onToggled: Settings.shotServerEnabled = checked
                    }
                }

                // Server status indicator (URL link)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    visible: Settings.shotServerEnabled

                    property bool serverRunning: MainController.shotServer && MainController.shotServer.running
                    property bool secured: serverRunning && Settings.webSecurityEnabled &&
                                           MainController.shotServer && MainController.shotServer.hasTotpSecret

                    Rectangle {
                        width: Theme.scaled(8)
                        height: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: !parent.serverRunning ? Theme.errorColor :
                               parent.secured ? Theme.successColor : Theme.textSecondaryColor
                        Accessible.ignored: true
                    }

                    Text {
                        text: {
                            if (!parent.serverRunning)
                                return TranslationManager.translate("settings.data.serverstarting", "Starting...");
                            var url = MainController.shotServer.url || "";
                            if (parent.secured)
                                return url + " \u2022 " + TranslationManager.translate("settings.data.secured", "Secured");
                            if (Settings.webSecurityEnabled)
                                return url + " (HTTPS)";
                            return url;
                        }
                        color: parent.secured ? Theme.successColor :
                               parent.serverRunning ? Theme.textColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        font.underline: parent.serverRunning
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        Accessible.role: Accessible.Link
                        Accessible.name: text
                        Accessible.focusable: parent.serverRunning
                        Accessible.onPressAction: Qt.openUrlExternally(MainController.shotServer.url)

                        TapHandler {
                            enabled: parent.parent.serverRunning
                            onTapped: Qt.openUrlExternally(MainController.shotServer.url)
                        }
                    }
                }

                // Security enable toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: Settings.shotServerEnabled

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.data.enablesecurity"
                            fallback: "Enable Security"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Tr {
                            key: "settings.data.enablesecuritydesc"
                            fallback: "Encrypt connections and require a code from your authenticator app"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(9)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: Settings.webSecurityEnabled
                        accessibleName: TranslationManager.translate("settings.data.enablesecurity", "Enable Security")
                        onToggled: Settings.webSecurityEnabled = checked
                    }
                }

                // TOTP setup/reset buttons
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    visible: Settings.shotServerEnabled && Settings.webSecurityEnabled

                    AccessibleButton {
                        Layout.fillWidth: true
                        primary: true
                        text: TranslationManager.translate("settings.data.setuptotp", "Set Up Authenticator")
                        accessibleName: TranslationManager.translate("settings.data.setuptotpAccessible",
                            "Set up authenticator app for web access security")
                        visible: MainController.shotServer && !MainController.shotServer.hasTotpSecret
                        onClicked: {
                            var setup = MainController.shotServer.generateTotpSetup();
                            totpSetupDialog.totpSecret = setup.secret;
                            totpSetupDialog.totpUri = setup.uri;
                            totpSetupDialog.open();
                        }
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        destructive: true
                        text: TranslationManager.translate("settings.data.resettotp", "Reset Security")
                        accessibleName: TranslationManager.translate("settings.data.resettotpAccessible",
                            "Remove authenticator and all web sessions")
                        visible: MainController.shotServer && MainController.shotServer.hasTotpSecret
                        onClicked: totpResetDialog.open()
                    }
                }

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
                        text: ProfileManager.availableProfiles.length
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }

                Item { Layout.fillHeight: true }

                // Factory reset button
                AccessibleButton {
                    Layout.fillWidth: true
                    destructive: true
                    text: Qt.platform.os === "android" ?
                          TranslationManager.translate("settings.data.resetuninstall", "Remove All Data & Uninstall") :
                          TranslationManager.translate("settings.data.resetquit", "Remove All Data & Quit")
                    accessibleName: Qt.platform.os === "android" ?
                          TranslationManager.translate("settings.data.resetuninstallaccessible",
                              "Remove all app data and uninstall the application") :
                          TranslationManager.translate("settings.data.resetquitaccessible",
                              "Remove all app data and quit the application")
                    onClicked: factoryResetDialog1.open()
                }
            }
    }

    // Device Migration Dialog
    DeviceMigrationDialog {
        id: deviceMigrationDialog
    }

    // File dialogs for shot import
        FileDialog {
            id: shotZipDialog
            title: TranslationManager.translate("settings.history.selectZipTitle", "Select shot history ZIP archive")
            nameFilters: ["ZIP archives (*.zip)", "All files (*)"]
            property bool overwrite: false

            onAccepted: {
                if (MainController.shotImporter) {
                    MainController.shotImporter.importFromZip(selectedFile, overwrite)
                }
            }
        }

        FolderDialog {
            id: shotFolderDialog
            title: TranslationManager.translate("settings.history.selectFolderTitle", "Select folder containing .shot files")
            property bool overwrite: false

            onAccepted: {
                if (MainController.shotImporter) {
                    MainController.shotImporter.importFromDirectory(selectedFolder, overwrite)
                }
            }
        }

        // Extracting popup - shows during ZIP extraction
        Dialog {
            id: extractingPopup
            modal: true
            dim: true
            closePolicy: Dialog.NoAutoClose
            anchors.centerIn: Overlay.overlay
            padding: Theme.scaled(24)

            background: Rectangle {
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                border.width: 2
                border.color: Theme.primaryColor
            }

            contentItem: Column {
                spacing: Theme.spacingMedium
                width: Theme.scaled(250)

                Text {
                    text: TranslationManager.translate("settings.history.extracting", "Extracting...")
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                BusyIndicator {
                    running: true
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.scaled(48)
                    height: Theme.scaled(48)
                }

                Text {
                    text: TranslationManager.translate("settings.history.extractingDesc", "Please wait while the archive is extracted")
                    wrapMode: Text.Wrap
                    width: parent.width
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Connections {
            target: MainController.shotImporter
            function onIsExtractingChanged() {
                if (MainController.shotImporter.isExtracting) {
                    extractingPopup.open()
                } else {
                    extractingPopup.close()
                }
            }
        }

        // Import result feedback dialog
        Dialog {
            id: importResultDialog
            modal: true
            dim: true
            closePolicy: Dialog.CloseOnEscape
            anchors.centerIn: Overlay.overlay
            padding: Theme.scaled(24)

            property string resultMessage: ""
            property bool isError: false

            background: Rectangle {
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                border.width: 2
                border.color: importResultDialog.isError ? Theme.errorColor : Theme.primaryColor
            }

            contentItem: Column {
                spacing: Theme.spacingMedium
                width: Theme.scaled(300)

                Text {
                    text: importResultDialog.title
                    font: Theme.subtitleFont
                    color: importResultDialog.isError ? Theme.errorColor : Theme.textColor
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    text: importResultDialog.resultMessage
                    wrapMode: Text.Wrap
                    width: parent.width
                    font: Theme.bodyFont
                    color: Theme.textColor
                }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.ok", "OK")
                    accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: importResultDialog.close()
                }
            }
        }

        // Shot import result handling
        Connections {
            target: MainController.shotImporter
            function onImportComplete(imported, skipped, failed) {
                importResultDialog.title = TranslationManager.translate("shotimporter.title.importComplete", "Import Complete")
                importResultDialog.resultMessage =
                    TranslationManager.translate("shotimporter.result.imported", "Imported") + ": " + imported + " " + TranslationManager.translate("shotimporter.result.shots", "shots") + "\n" +
                    TranslationManager.translate("shotimporter.result.skipped", "Skipped (duplicates)") + ": " + skipped + "\n" +
                    TranslationManager.translate("shotimporter.result.failed", "Failed") + ": " + failed + "\n\n" +
                    TranslationManager.translate("shotimporter.result.totalShots", "Total shots") + ": " + (MainController.shotHistory ? MainController.shotHistory.totalShots : "?")
                importResultDialog.isError = failed > 0 && imported === 0
                importResultDialog.open()
            }
            function onImportError(translationKey, fallbackMessage) {
                importResultDialog.title = TranslationManager.translate("shotimporter.title.importFailed", "Import Failed")
                importResultDialog.resultMessage = TranslationManager.translate(translationKey, fallbackMessage)
                importResultDialog.isError = true
                importResultDialog.open()
            }
        }

    // Import complete notification
    Connections {
        target: MainController.dataMigration

        function onImportComplete(settingsImported, profilesImported, shotsImported, mediaImported, aiConversationsImported) {
            importCompletePopup.settingsCount = settingsImported
            importCompletePopup.profilesCount = profilesImported
            importCompletePopup.shotsCount = shotsImported
            importCompletePopup.mediaCount = mediaImported
            importCompletePopup.aiConversationsCount = aiConversationsImported
            importCompletePopup.open()

            // Refresh profiles list
            ProfileManager.refreshProfiles()
        }

        function onConnectionFailed(error) {
            // Error is already shown via errorMessage property
        }

        // Auth success/failure handled inside DeviceMigrationDialog
    }

    // Import complete popup
    Dialog {
        id: importCompletePopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Theme.scaled(300)
        padding: Theme.scaled(20)

        property int settingsCount: 0
        property int profilesCount: 0
        property int shotsCount: 0
        property int mediaCount: 0
        property int aiConversationsCount: 0

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

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/tick.svg"
                        sourceSize.width: Theme.scaled(14)
                        sourceSize.height: Theme.scaled(14)
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

                Tr {
                    key: "settings.data.aiconversations"
                    fallback: "AI Conversations:"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                    visible: importCompletePopup.aiConversationsCount > 0
                }
                Text {
                    text: importCompletePopup.aiConversationsCount
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    visible: importCompletePopup.aiConversationsCount > 0
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
            historyDataTab.backupInProgress = false;
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
            historyDataTab.backupInProgress = false;
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
            // Note: backupInProgress is reset by onBackupFailed which fires alongside this signal
            console.log("Storage permission needed - user should grant access");
        }
    }

    // Status message for backup operations (reparented to avoid layout warnings)
    Rectangle {
        id: backupStatusBackground
        parent: Overlay.overlay
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
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        property string selectedBackup: ""
        property string displayName: ""
        property bool mergeMode: true
        property bool restoreShots: true
        property bool restoreSettings: true
        property bool restoreProfiles: true
        property bool restoreMedia: true

        function resetDefaults() {
            mergeMode = true;
            restoreShots = true;
            restoreSettings = true;
            restoreProfiles = true;
            restoreMedia = true;
        }

        // Prevent closing while restore is running
        closePolicy: historyDataTab.restoreInProgress ? Dialog.NoAutoClose : (Dialog.CloseOnEscape | Dialog.CloseOnPressOutside)

        onClosed: {
            if (!historyDataTab.restoreInProgress) {
                resetDefaults();
            }
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                visible: !historyDataTab.restoreInProgress

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.restoredialog", "Restore Backup?")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Restoring state — replaces dialog content
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: restoreProgressCol.implicitHeight + Theme.scaled(60)
                visible: historyDataTab.restoreInProgress

                ColumnLayout {
                    id: restoreProgressCol
                    anchors.centerIn: parent
                    spacing: Theme.scaled(16)

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: historyDataTab.restoreInProgress
                        implicitWidth: Theme.scaled(48)
                        implicitHeight: Theme.scaled(48)
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: TranslationManager.translate("settings.data.restoring", "Restoring backup...")
                        color: Theme.textColor
                        font: Theme.titleFont
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: TranslationManager.translate("settings.data.restoringdesc", "Please wait, this may take a moment.")
                        color: Theme.textSecondaryColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(12)
                visible: !historyDataTab.restoreInProgress

                Text {
                    Layout.fillWidth: true
                    text: restoreConfirmDialog.displayName
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap

                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("settings.data.backupfile", "Backup file: ") + text
                }

                // Data type toggles
                Text {
                    text: TranslationManager.translate("settings.data.selectdata", "Select data to restore:")
                    color: Theme.textColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.scaled(8)
                    rowSpacing: Theme.scaled(8)

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.data.shots", "Shots")
                        primary: restoreConfirmDialog.restoreShots
                        accessibleName: TranslationManager.translate("settings.data.shots", "Shots") + ", " +
                            (restoreConfirmDialog.restoreShots
                                ? TranslationManager.translate("accessibility.selected", "selected")
                                : TranslationManager.translate("accessibility.notselected", "not selected"))
                        onClicked: restoreConfirmDialog.restoreShots = !restoreConfirmDialog.restoreShots
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.data.settingsai", "Settings")
                        primary: restoreConfirmDialog.restoreSettings
                        accessibleName: TranslationManager.translate("settings.data.settingsai", "Settings & AI Conversations") + ", " +
                            (restoreConfirmDialog.restoreSettings
                                ? TranslationManager.translate("accessibility.selected", "selected")
                                : TranslationManager.translate("accessibility.notselected", "not selected"))
                        onClicked: restoreConfirmDialog.restoreSettings = !restoreConfirmDialog.restoreSettings
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.data.profiles", "Profiles")
                        primary: restoreConfirmDialog.restoreProfiles
                        accessibleName: TranslationManager.translate("settings.data.profiles", "Profiles") + ", " +
                            (restoreConfirmDialog.restoreProfiles
                                ? TranslationManager.translate("accessibility.selected", "selected")
                                : TranslationManager.translate("accessibility.notselected", "not selected"))
                        onClicked: restoreConfirmDialog.restoreProfiles = !restoreConfirmDialog.restoreProfiles
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.data.media", "Media")
                        primary: restoreConfirmDialog.restoreMedia
                        accessibleName: TranslationManager.translate("settings.data.media", "Media") + ", " +
                            (restoreConfirmDialog.restoreMedia
                                ? TranslationManager.translate("accessibility.selected", "selected")
                                : TranslationManager.translate("accessibility.notselected", "not selected"))
                        onClicked: restoreConfirmDialog.restoreMedia = !restoreConfirmDialog.restoreMedia
                    }
                }

                // Merge/Replace switch — visible when any data type that respects merge is checked
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    visible: restoreConfirmDialog.restoreShots || restoreConfirmDialog.restoreSettings || restoreConfirmDialog.restoreProfiles || restoreConfirmDialog.restoreMedia

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)

                            Text {
                                text: restoreConfirmDialog.mergeMode
                                    ? TranslationManager.translate("settings.data.mergemode", "Merge with existing data")
                                    : TranslationManager.translate("settings.data.replacemode", "Replace all data")
                                color: restoreConfirmDialog.mergeMode ? Theme.textColor : Theme.warningColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(12)
                                font.bold: true
                                Accessible.ignored: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: restoreConfirmDialog.mergeMode
                                    ? TranslationManager.translate("settings.data.mergemodedesc",
                                        "Adds new entries. Existing shots and profiles are kept.")
                                    : TranslationManager.translate("settings.data.replacemodedesc",
                                        "Deletes ALL current shots and profiles, replaces with backup. Cannot be undone!")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }

                        StyledSwitch {
                            checked: !restoreConfirmDialog.mergeMode
                            accessibleName: restoreConfirmDialog.mergeMode
                                ? TranslationManager.translate("settings.data.switchreplace", "Switch to replace mode")
                                : TranslationManager.translate("settings.data.switchmerge", "Switch to merge mode")
                            onToggled: restoreConfirmDialog.mergeMode = !checked
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        id: cancelButton
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.data.cancelrestore", "Cancel restore operation")
                        onClicked: {
                            restoreConfirmDialog.resetDefaults();
                            restoreConfirmDialog.close();
                        }
                    }

                    AccessibleButton {
                        id: confirmButton
                        text: TranslationManager.translate("common.restore", "Restore")
                        primary: true
                        enabled: restoreConfirmDialog.restoreShots || restoreConfirmDialog.restoreSettings ||
                                 restoreConfirmDialog.restoreProfiles || restoreConfirmDialog.restoreMedia
                        accessibleName: TranslationManager.translate("settings.data.confirmrestore", "Confirm restore backup")
                        onClicked: {
                            if (MainController.backupManager) {
                                historyDataTab.restoreInProgress = true;
                                var started = MainController.backupManager.restoreBackup(
                                    restoreConfirmDialog.selectedBackup,
                                    restoreConfirmDialog.mergeMode,
                                    restoreConfirmDialog.restoreShots,
                                    restoreConfirmDialog.restoreSettings,
                                    restoreConfirmDialog.restoreProfiles,
                                    restoreConfirmDialog.restoreMedia
                                );
                                if (!started) {
                                    historyDataTab.restoreInProgress = false;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Restore result handlers
    Connections {
        target: MainController.backupManager
        enabled: historyDataTab.visible || historyDataTab.restoreInProgress

        function onRestoreCompleted(filename) {
            historyDataTab.restoreInProgress = false;
            restoreConfirmDialog.resetDefaults();
            restoreConfirmDialog.close();
            console.log("Restore completed:", filename);
            backupStatusText.text = TranslationManager.translate("settings.data.restoresuccess",
                "✓ Backup restored successfully");
            backupStatusText.color = Theme.successColor;
            backupStatusBackground.visible = true;
            backupStatusTimer.restart();

            // TTS announcement for accessibility
            if (MainController.accessibilityManager) {
                MainController.accessibilityManager.announce(
                    TranslationManager.translate("settings.data.restorecompletedAccessible",
                        "Backup restored successfully.")
                );
            }
        }

        function onRestoreFailed(error) {
            historyDataTab.restoreInProgress = false;
            restoreConfirmDialog.resetDefaults();
            restoreConfirmDialog.close();
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

    // TOTP Setup Dialog
    Dialog {
        id: totpSetupDialog
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: {
            if (totpCodeField.activeFocus) {
                // Center in the visible area above the keyboard
                var kbHeight = Qt.inputMethod.keyboardRectangle.height;
                if (kbHeight <= 0 && (Qt.platform.os === "android" || Qt.platform.os === "ios"))
                    kbHeight = parent.height * 0.45;
                var availableHeight = parent.height - kbHeight;
                return Math.round(Math.max(Theme.scaled(10), (availableHeight - height) / 2));
            }
            return Math.round((parent.height - height) / 2);
        }
        width: Theme.scaled(380)
        padding: 0
        modal: true

        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }

        property string totpSecret: ""
        property string totpUri: ""
        property string verifyError: ""
        property bool verifying: false

        onClosed: {
            totpCodeField.text = "";
            verifyError = "";
            verifying = false;
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.totpsetuptitle", "Set Up Authenticator")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Body
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(12)

                Text {
                    Layout.fillWidth: true
                    visible: !totpCodeField.activeFocus
                    text: TranslationManager.translate("settings.data.totpsetupinstructions",
                        "Scan this QR code with your authenticator app (Apple Passwords, Google Authenticator, Microsoft Authenticator, or similar).")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                }

                // QR code — hidden when keyboard is open (user already scanned it)
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: Theme.scaled(200)
                    height: Theme.scaled(200)
                    visible: !totpCodeField.activeFocus
                    color: "#ffffff"
                    radius: Theme.scaled(8)
                    Accessible.role: Accessible.Graphic
                    Accessible.name: TranslationManager.translate("settings.data.qrcodeAccessible",
                        "QR code for authenticator app setup. Use the manual code below if you cannot scan.")

                    QrCode {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        value: totpSetupDialog.totpUri
                        Accessible.ignored: true
                    }
                }

                // Manual entry secret — hidden when keyboard is open
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !totpCodeField.activeFocus
                    spacing: Theme.scaled(4)

                    Text {
                        text: TranslationManager.translate("settings.data.totpmanualentry", "Or enter this code manually:")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.scaled(36)
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: Theme.borderColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: totpSetupDialog.totpSecret
                                color: Theme.textColor
                                font.family: "monospace"
                                font.pixelSize: Theme.scaled(11)
                                font.bold: true

                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.data.totpsecretAccessible",
                                    "Secret code for manual entry: ") + totpSetupDialog.totpSecret
                            }
                        }

                        AccessibleButton {
                            text: secretCopyTimer.running ?
                                  TranslationManager.translate("settings.data.copied", "Copied") :
                                  TranslationManager.translate("settings.data.copy", "Copy")
                            accessibleName: TranslationManager.translate("settings.data.copySecretAccessible",
                                "Copy secret code to clipboard")
                            onClicked: {
                                clipboardHelper.text = totpSetupDialog.totpSecret;
                                clipboardHelper.selectAll();
                                clipboardHelper.copy();
                                clipboardHelper.text = "";
                                secretCopyTimer.restart();
                            }

                            Timer {
                                id: secretCopyTimer
                                interval: 2000
                            }
                        }
                    }
                }

                // Verification code input
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Text {
                        text: TranslationManager.translate("settings.data.totpverify",
                            "Enter a code from your authenticator to verify:")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        StyledTextField {
                            id: totpCodeField
                            Layout.fillWidth: true
                            maximumLength: 6
                            inputMethodHints: Qt.ImhDigitsOnly
                            horizontalAlignment: TextInput.AlignHCenter
                            font.pixelSize: Theme.scaled(18)
                            font.bold: true
                            font.letterSpacing: Theme.scaled(4)
                            enabled: !totpSetupDialog.verifying
                            accessibleName: TranslationManager.translate("settings.data.totpCodeFieldAccessible",
                                "Six digit verification code from authenticator app")

                            onTextChanged: {
                                totpSetupDialog.verifyError = "";
                            }

                            Keys.onReturnPressed: {
                                if (text.length === 6) totpVerifyButton.clicked();
                            }
                        }

                        AccessibleButton {
                            id: totpVerifyButton
                            primary: true
                            text: totpSetupDialog.verifying ?
                                  TranslationManager.translate("settings.data.verifying", "Verifying...") :
                                  TranslationManager.translate("settings.data.verify", "Verify")
                            accessibleName: TranslationManager.translate("settings.data.verifyAccessible",
                                "Verify authenticator code to complete setup")
                            enabled: totpCodeField.text.length === 6 && !totpSetupDialog.verifying
                            onClicked: {
                                totpSetupDialog.verifying = true;
                                var success = MainController.shotServer.completeTotpSetup(
                                    totpSetupDialog.totpSecret, totpCodeField.text);
                                totpSetupDialog.verifying = false;
                                if (success) {
                                    totpSetupDialog.close();
                                } else {
                                    totpSetupDialog.verifyError = TranslationManager.translate(
                                        "settings.data.totpverifyfailed", "Invalid code. Please try again.");
                                    totpCodeField.text = "";
                                    totpCodeField.forceActiveFocus();
                                }
                            }
                        }
                    }

                    Text {
                        visible: totpSetupDialog.verifyError !== ""
                        text: totpSetupDialog.verifyError
                        color: Theme.errorColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }

                // Cancel button
                AccessibleButton {
                    Layout.alignment: Qt.AlignRight
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.data.cancelTotpSetup",
                        "Cancel authenticator setup")
                    onClicked: totpSetupDialog.close()
                }
            }
        }
    }

    // TOTP Reset Confirmation Dialog
    Dialog {
        id: totpResetDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(380)
        padding: 0
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.resetsecuritytitle", "Reset Security?")
                    font: Theme.titleFont
                    color: Theme.warningColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Body
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.resetsecuritybody",
                        "This will remove your authenticator setup and sign out all web sessions. You will need to set up your authenticator app again.")
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.data.cancelResetSecurity",
                            "Cancel security reset")
                        onClicked: totpResetDialog.close()
                    }

                    AccessibleButton {
                        destructive: true
                        text: TranslationManager.translate("settings.data.confirmreset", "Reset")
                        accessibleName: TranslationManager.translate("settings.data.confirmResetAccessible",
                            "Confirm: remove authenticator and sign out all sessions")
                        onClicked: {
                            MainController.shotServer.resetTotpSecret();
                            totpResetDialog.close();
                        }
                    }
                }
            }
        }
    }

    // Factory Reset - Confirmation Dialog 1
    Dialog {
        id: factoryResetDialog1
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.factoryresettitle", "Remove All Data?")
                    font: Theme.titleFont
                    color: Theme.errorColor
                    Accessible.ignored: true
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Body
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.factoryresetbody",
                        "This will permanently delete ALL your data: settings, favourites, profiles, shot history, themes, and everything else. This cannot be undone.\n\nYour backups will NOT be deleted. You can find them in your Documents/Decenza Backups folder if you need to restore later, or delete them manually.")
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.data.cancelfactoryreset", "Cancel factory reset")
                        onClicked: factoryResetDialog1.close()
                    }

                    AccessibleButton {
                        destructive: true
                        text: TranslationManager.translate("settings.data.factoryresetcontinue", "Continue")
                        accessibleName: TranslationManager.translate("settings.data.factoryresetcontinueaccessible",
                            "Continue with factory reset, shows final confirmation")
                        onClicked: {
                            factoryResetDialog1.close()
                            factoryResetDialog2.open()
                        }
                    }
                }
            }
        }
    }

    // Factory Reset - Confirmation Dialog 2 (the fun one)
    Dialog {
        id: factoryResetDialog2
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.factoryresettitle2", "Are you REALLY sure?")
                    font: Theme.titleFont
                    color: Theme.errorColor
                    Accessible.ignored: true
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Body
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.factoryresetbody2",
                        "This is your last chance. All your espresso data, your carefully dialled-in profiles, your shot history \u2014 gone. Poof. Like that time you forgot to put the drip tray back.")
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.data.factoryresetchangedmind", "I changed my mind")
                        accessibleName: TranslationManager.translate("settings.data.factoryresetchangedmindaccessible",
                            "Cancel factory reset and keep all data")
                        onClicked: factoryResetDialog2.close()
                    }

                    AccessibleButton {
                        destructive: true
                        text: TranslationManager.translate("settings.data.factoryreset.nuke", "Yes, nuke everything")
                        accessibleName: TranslationManager.translate("settings.data.factoryreset.nukeaccessible",
                            "Confirm: permanently delete all data and exit the app")
                        onClicked: {
                            factoryResetDialog2.close()
                            MainController.factoryResetAndQuit()
                        }
                    }
                }
            }
        }
    }

}
}
