import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decenza

KeyboardAwareContainer {
    id: historyDataTab
    textFields: [totpCodeField]

    readonly property var backupSnapshot: BackupService.ready ? BackupService.snapshot : ({
        backups: [], hasStoragePermission: false, dailyBackupHour: -1,
        backupBusy: false, restoreBusy: false, aiConversationNote: "", lastResult: ""
    })
    readonly property bool backupInProgress: backupSnapshot.backupBusy === true
    readonly property bool restoreInProgress: backupSnapshot.restoreBusy === true ||
                                             restoreRequestId.length > 0 || backupUpload.busy
    property string backupRequestId: ""
    property string restoreRequestId: ""
    property string backupHourRequestId: ""
    property string backupDownloadRequestId: ""
    property bool savingBackup: false
    readonly property bool backupDownloadInProgress: backupDownloadRequestId.length > 0 || savingBackup
    readonly property var shotServerSnapshot: ShotServerSettingsService.ready
                                                ? ShotServerSettingsService.snapshot
                                                : ({ running: false, url: "", webSecurityEnabled: false,
                                                     hasTotpSecret: false })
    readonly property var importSnapshot: ImportService.ready ? ImportService.snapshot : ({
        shotDetected: false, profileDetected: false,
        shotImporting: false, shotExtracting: false, shotTotalFiles: 0,
        shotProcessedFiles: 0, shotStatus: "", profileScanning: false,
        profileImporting: false, profileTotal: 0, profileProcessed: 0,
        profileStatus: "", busy: false
    })
    readonly property var networkPreferences: SettingsPreferencesService.ready
                                                ? SettingsPreferencesService.preferences
                                                : ({ "network.shotServerEnabled": false,
                                                     "network.webSecurityEnabled": false,
                                                     "network.exportShotsToFile": false })
    property string shotServerPrivateRequestId: ""
    property string shotServerPrivateOperation: ""
    property string totpClipboardRequestId: ""

    property string _de1ImportRequestId: ""

    PreferenceWrite { id: shotServerPreferenceWrite; domain: "network"; onFailed: function(message) { shotServerError.show(message) } }
    PreferenceWrite { id: exportShotsPreferenceWrite; domain: "network"; onFailed: function(message) { exportShotsError.show(message) } }
    StatusToast { id: exportShotsError; error: true }

    LocalFileDownload {
        id: backupDownload
        executeOperation: function(operation, args) { return BackupService.execute(operation, args) }
        onFinished: function(success, reason) {
            historyDataTab.savingBackup = false
            historyDataTab.showBackupDownloadResult(success, reason)
        }
    }
    StatusToast { id: backupDownloadStatus }

    function showBackupDownloadResult(success, reason) {
        if (!success && (reason === "saveCancelled" || reason === "cancelled")) return
        backupDownloadStatus.error = !success
        const message = success
            ? TranslationManager.translate("settings.data.backupSaved", "Backup saved.")
            : reason === "busy"
                ? TranslationManager.translate("settings.data.backupDownloadBusy", "Another backup operation is in progress. Try again when it finishes.")
                : TranslationManager.translate("settings.data.backupDownloadFailed", "The backup could not be saved. Check the connection and try again.")
        backupDownloadStatus.show(message)
        AccessibilityManager.announce(message)
    }

    function cancelBackupDownload() {
        const requestId = backupDownloadRequestId
        backupDownloadRequestId = ""
        if (requestId.length > 0)
            BackupService.execute("cancelFileOperation", {requestId: requestId})
        backupDownload.cancelSilently()
        savingBackup = false
    }

    Component.onDestruction: {
        if (historyDataTab.backupDownloadRequestId.length > 0)
            BackupService.execute("cancelFileOperation", {requestId: historyDataTab.backupDownloadRequestId})
    }

    function copyTotpSecret(): void {
        if (totpClipboardRequestId.length > 0 || totpSetupDialog.totpSecret.length === 0)
            return
        totpSetupDialog.totpSecretCopied = false
        const requestId = LocalClipboard.newRequestId()
        totpClipboardRequestId = requestId
        if (!LocalClipboard.writeText(requestId, totpSetupDialog.totpSecret)) {
            totpClipboardRequestId = ""
            shotServerError.show(TranslationManager.translate(
                "common.copyFailed", "Could not copy to the clipboard."))
        }
    }

    Connections {
        target: LocalClipboard
        function onOperationCompleted(requestId, operation, success, text, reason) {
            if (requestId !== historyDataTab.totpClipboardRequestId
                    || operation !== "writeText") return
            historyDataTab.totpClipboardRequestId = ""
            if (!success) {
                shotServerError.show(TranslationManager.translate(
                    "common.copyFailed", "Could not copy to the clipboard."))
                return
            }
            totpSetupDialog.totpSecretCopied = true
            secretCopyTimer.restart()
        }
    }

    Connections {
        target: ShotServerSettingsService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId !== historyDataTab.shotServerPrivateRequestId
                    || operation !== historyDataTab.shotServerPrivateOperation) return
            historyDataTab.shotServerPrivateRequestId = ""
            historyDataTab.shotServerPrivateOperation = ""
            if (operation === "generateTotpSetup") {
                if (!success) { shotServerError.show(reason); return }
                totpSetupDialog.totpSecret = result.secret || ""
                totpSetupDialog.totpUri = result.uri || ""
                totpSetupDialog.open()
            } else if (operation === "completeTotpSetup") {
                totpSetupDialog.verifying = false
                if (success) totpSetupDialog.close()
                else {
                    totpSetupDialog.verifyError = TranslationManager.translate(
                        "settings.data.totpverifyfailed", "Invalid code. Please try again.")
                    totpCodeField.text = ""
                    totpCodeField.forceActiveFocus()
                }
            } else if (operation === "resetTotpSecret") {
                if (success) totpResetDialog.close()
                else shotServerError.show(reason)
            } else if (operation === "factoryResetAndQuit" && !success) {
                const message = reason === "disconnected"
                    ? TranslationManager.translate("settings.data.factoryResetOutcomeUnknown",
                        "The connection closed while the host was resetting. Check the host before trying again.")
                    : reason
                shotServerError.show(message)
            }
        }
    }
    StatusToast { id: shotServerError; error: true }

    function _showImportResult(success, result, reason) {
        if (!success) {
            importResultDialog.title = TranslationManager.translate("shotimporter.title.importFailed", "Import Failed")
            importResultDialog.resultMessage = _importReasonText(reason)
            importResultDialog.isError = true
        } else {
            const imported = result.shotsImported || 0
            const skipped = result.shotsSkipped || 0
            const failed = result.shotsFailed || 0
            const parts = []
            parts.push(TranslationManager.translate("shotimporter.result.imported", "Imported") + ": " + imported + " " + TranslationManager.translate("shotimporter.result.shots", "shots"))
            parts.push(TranslationManager.translate("shotimporter.result.skipped", "Skipped (duplicates)") + ": " + skipped)
            parts.push(TranslationManager.translate("shotimporter.result.failed", "Failed") + ": " + failed)
            if (result.profilesImported !== undefined) {
                parts.push(TranslationManager.translate("profileimporter.result.imported", "Profiles imported") + ": " + result.profilesImported)
                parts.push(TranslationManager.translate("profileimporter.result.skipped", "Profiles skipped") + ": " + (result.profilesSkipped || 0))
                parts.push(TranslationManager.translate("profileimporter.result.failed", "Profiles failed") + ": " + (result.profilesFailed || 0))
            }
            if (result.shotsError) parts.push(_importReasonText(result.shotsError))
            importResultDialog.title = TranslationManager.translate("shotimporter.title.importComplete", "Import Complete")
            importResultDialog.resultMessage = parts.join("\n")
            importResultDialog.isError = failed > 0 && imported === 0
        }
        importResultDialog.open()
    }

    function _importReasonText(reason) {
        const known = {
            "importBusy": TranslationManager.translate("shotimporter.error.alreadyInProgress", "An import is already in progress."),
            "databaseImportBusy": TranslationManager.translate("settings.history.databaseBusy", "A database import is already in progress."),
            "databaseImportFailed": TranslationManager.translate("settings.history.databaseImportFailed", "The database could not be imported."),
            "invalidSelection": TranslationManager.translate("settings.history.invalidSelection", "Choose a supported file."),
            "invalidFileMetadata": TranslationManager.translate("settings.history.invalidFile", "The selected file is not supported."),
            "directoryEntryLimit": TranslationManager.translate("settings.history.folderEntryLimit", "The selected folder contains too many files to scan."),
            "selectionFileLimit": TranslationManager.translate("settings.history.folderShotLimit", "The selected folder contains too many shot files to import at once."),
            "noSupportedFiles": TranslationManager.translate("settings.history.folderNoShots", "No .shot files were found in the selected folder."),
            "inspectionBusy": TranslationManager.translate("settings.history.folderScanBusy", "A folder scan is already running. Try again shortly."),
            "transferSizeLimit": TranslationManager.translate("settings.history.fileTooLarge", "The selected files are too large."),
            "incompleteTransfer": TranslationManager.translate("settings.history.incompleteUpload", "The file upload did not complete."),
            "writeFailed": TranslationManager.translate("settings.history.uploadFailed", "The selected file could not be read."),
            "serviceUnavailable": TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet."),
            "cancelled": TranslationManager.translate("common.cancelled", "Cancelled")
        }
        if (known[reason]) return known[reason]
        if (reason && reason.indexOf("shotimporter.") === 0)
            return TranslationManager.translate(reason, TranslationManager.translate("common.error.unknown", "The import could not be completed."))
        return TranslationManager.translate("common.error.unknown", "The import could not be completed.")
    }
    readonly property bool hasStoragePerm: BackupService.ready && backupSnapshot.hasStoragePermission === true
    function recheckStoragePermission() { BackupService.refresh() }
    onVisibleChanged: {
        if (visible) recheckStoragePermission()
    }

    function showBackupResult(operation, success, result, reason) {
        const isRestore = operation === "restoreBackup" || operation === "restoreUploadedBackup"
        const error = result && result.error ? result.error : reason
        const disconnected = !success && reason === "disconnected"
        backupStatusText.text = disconnected
            ? TranslationManager.translate("settings.data.backupOutcomeUnknown",
                "Connection lost while the host operation continued. Its final result may be unknown.")
            : success
            ? (isRestore
                ? TranslationManager.translate("settings.data.restoresuccess", "✓ Backup restored successfully")
                : TranslationManager.translate("settings.data.backupsuccess", "✓ Backup created successfully"))
            : "✗ " + (error || TranslationManager.translate("settings.data.operationfailed", "The backup operation failed"))
        backupStatusText.color = disconnected ? Theme.warningColor : (success ? Theme.successColor : Theme.errorColor)
        backupStatusBackground.visible = true
        backupStatusTimer.restart()
        if (success && isRestore)
            AccessibilityManager.announce(TranslationManager.translate("settings.data.restorecompletedAccessible", "Backup restored successfully."))
        else if (success)
            AccessibilityManager.announce(TranslationManager.translate("settings.data.backupcreatedAccessible", "Backup created successfully"))
        else if (!disconnected)
            AccessibilityManager.announce((isRestore
                ? TranslationManager.translate("settings.data.restorefailedAccessible", "Restore failed: ")
                : TranslationManager.translate("settings.data.backupfailedAccessible", "Backup failed: ")) + (error || ""))
    }

    Connections {
        target: BackupService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            backupDownload.completeOperation(requestId, operation, success, result, reason)
            if (operation === "prepareBackupDownload" && requestId === historyDataTab.backupDownloadRequestId) {
                historyDataTab.backupDownloadRequestId = ""
                if (!success) {
                    historyDataTab.showBackupDownloadResult(false, reason)
                    return
                }
                historyDataTab.savingBackup = true
                if (!backupDownload.start(result) && historyDataTab.savingBackup) {
                    historyDataTab.savingBackup = false
                    BackupService.execute("releaseBundle", {bundleId: result.bundleId})
                    historyDataTab.showBackupDownloadResult(false, "invalidBundle")
                }
            } else if (operation === "createBackup" && requestId === historyDataTab.backupRequestId) {
                historyDataTab.backupRequestId = ""
                historyDataTab.showBackupResult(operation, success, result, reason)
            } else if ((operation === "restoreBackup" || operation === "restoreUploadedBackup")
                       && requestId === historyDataTab.restoreRequestId) {
                historyDataTab.restoreRequestId = ""
                restoreConfirmDialog.resetDefaults()
                restoreConfirmDialog.close()
                historyDataTab.showBackupResult(operation, success, result, reason)
            } else if (operation === "setBackupHour" && requestId === historyDataTab.backupHourRequestId) {
                historyDataTab.backupHourRequestId = ""
                if (!success) BackupService.refresh()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Shot History stats and import
        SettingsCard {
            searchId: "shotHistory"
            title: TranslationManager.translate("settings.search.shotHistoryTitle", "Shot History")
            description: TranslationManager.translate("settings.search.shotHistoryDesc", "View and manage shot history")
            keywords: ["history", "shots", "past", "records", "import", "de1", "migrate", "transfer", "migration", "device", "wifi", "sync"]
            showHeader: false
            fillContent: true
            contentMargins: Theme.scaled(12)
            spacing: Theme.scaled(6)
            Layout.fillWidth: false
            Layout.preferredWidth: Theme.scaled(300)

            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.history.title", "Shot History") + " →"
                accessibleName: TranslationManager.translate("settings.history.openShotHistory", "Open Shot History")
                primary: true
                onClicked: AppShell.shotHistoryRequested({})
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
                    text: HistoryService.totalShots
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                }

                Item { Layout.fillWidth: true }
            }

            // Divider
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
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
                visible: ImportService.ready && (historyDataTab.importSnapshot.shotImporting
                         || historyDataTab.importSnapshot.profileImporting
                         || historyDataTab.importSnapshot.profileScanning)

                Text {
                    text: {
                        const s = historyDataTab.importSnapshot
                        return [s.shotStatus, s.profileStatus].filter(Boolean).join("\n")
                    }
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(11)
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: historyDataTab.importSnapshot.shotTotalFiles + historyDataTab.importSnapshot.profileTotal || 1
                    value: historyDataTab.importSnapshot.shotProcessedFiles + historyDataTab.importSnapshot.profileProcessed

                    background: Rectangle {
                        implicitHeight: Theme.scaled(6)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(3)
                    }

                    contentItem: Item {
                        implicitHeight: Theme.scaled(6)
                        Rectangle {
                        width: parent.width * ((historyDataTab.importSnapshot.shotTotalFiles + historyDataTab.importSnapshot.profileTotal) > 0 ?
                               (historyDataTab.importSnapshot.shotProcessedFiles + historyDataTab.importSnapshot.profileProcessed) /
                               (historyDataTab.importSnapshot.shotTotalFiles + historyDataTab.importSnapshot.profileTotal) : 0)
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
                    visible: ImportService.ready && (historyDataTab.importSnapshot.shotImporting
                             || historyDataTab.importSnapshot.shotExtracting)
                    enabled: ImportService.ready
                    onClicked: {
                        ImportService.execute("cancel")
                    }
                }
            }

            // Import buttons (visible when not importing)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)
                visible: (!ImportService.ready || !historyDataTab.importSnapshot.busy)
                         && !shotFileTransfer.busy

                Text {
                    Layout.fillWidth: true
                    visible: !ImportService.ready
                    text: TranslationManager.translate(
                        "settings.history.importServiceUnavailable",
                        "Import is unavailable while the host service is disconnected.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    wrapMode: Text.WordWrap
                }

                // DE1 App detection info
                Text {
                    id: de1AppStatus
                    Layout.fillWidth: true
                    property bool detected: historyDataTab.importSnapshot.shotDetected
                        || historyDataTab.importSnapshot.profileDetected
                    text: detected ? TranslationManager.translate("settings.history.found", "DE1 app found") : TranslationManager.translate("settings.history.de1AppNotFound", "DE1 app not found on device")
                    color: detected ? Theme.successColor : Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(9)
                    wrapMode: Text.Wrap
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.history.importFromDE1", "Import from DE1 App")
                    accessibleName: TranslationManager.translate("settings.history.importFromDE1Desc", "Auto-detect and import from DE1 tablet app")
                    visible: de1AppStatus.detected
                    enabled: ImportService.ready
                    onClicked: {
                        historyDataTab._de1ImportRequestId = ImportService.execute("importFromDE1App",
                            { "overwrite": overwriteSwitch.checked })
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.history.zip", "ZIP...")
                        accessibleName: TranslationManager.translate("settings.history.importFromZip", "Import shot history from ZIP archive")
                        enabled: ImportService.ready && !shotFileTransfer.busy
                        onClicked: {
                            shotZipDialog.overwrite = overwriteSwitch.checked
                            shotZipDialog.open()
                        }
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.history.folder", "Folder...")
                        accessibleName: TranslationManager.translate("settings.history.importFromFolder", "Import shot history from folder")
                        enabled: ImportService.ready && !shotFileTransfer.busy
                        onClicked: {
                            shotFolderDialog.overwrite = overwriteSwitch.checked
                            if (Qt.platform.os === "wasm") {
                                shotFileTransfer.startDirectory("shotFiles", {
                                    "overwrite": overwriteSwitch.checked
                                })
                            } else {
                                shotFolderDialog.open()
                            }
                        }
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        accessibleName: TranslationManager.translate(
                            "settings.history.cancelFolderSelection", "Cancel folder selection")
                        visible: shotFileTransfer.directorySelectionPending
                        onClicked: shotFileTransfer.cancelDirectorySelection()
                    }
                }
            }

            // Divider
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
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

        // Middle column: Daily Backup
        SettingsCard {
            searchId: "dailyBackup"
            title: TranslationManager.translate("settings.data.dailybackup", "Daily Backup")
            description: TranslationManager.translate("settings.search.dailyBackupDesc", "Auto-backup shots, settings, profiles daily")
            keywords: ["backup", "restore", "save", "data", "auto"]
            showHeader: false
            fillContent: true
            contentMargins: Theme.scaled(10)
            spacing: Theme.scaled(4)
            Layout.fillWidth: false
            Layout.preferredWidth: Theme.scaled(280)

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
                    fallback: "Backup time"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(12)
                }

                StyledComboBox {
                    id: backupTimeCombo
                    Layout.fillWidth: true
                    accessibleLabel: TranslationManager.translate("settings.data.backuptime", "Backup time")
                    model: {
                        var times = [TranslationManager.translate("settings.data.backupoff", "Off")];
                        for (let hour = 0; hour < 24; hour++) {
                            let hourStr = hour.toString().padStart(2, '0');
                            times.push(hourStr + ":00");
                        }
                        return times;
                    }
                    currentIndex: BackupService.ready
                                  ? historyDataTab.backupSnapshot.dailyBackupHour + 1 : 0
                    enabled: BackupService.ready && !historyDataTab.backupHourRequestId
                    onActivated: {
                        historyDataTab.backupHourRequestId = BackupService.execute("setBackupHour", {
                            hour: currentIndex - 1,
                            expectedHour: historyDataTab.backupSnapshot.dailyBackupHour
                        })
                    }
                }
            }

            // Status text
            Text {
                Layout.fillWidth: true
                visible: BackupService.ready && historyDataTab.backupSnapshot.dailyBackupHour >= 0
                text: {
                    if (!BackupService.ready) return ""
                    var hour = historyDataTab.backupSnapshot.dailyBackupHour.toString().padStart(2, '0');
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
                visible: SettingsSearchRegistry.isAvailable("android") &&
                         BackupService.ready &&
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
                visible: SettingsSearchRegistry.isAvailable("android") &&
                         BackupService.ready &&
                         !historyDataTab.hasStoragePerm
                text: TranslationManager.translate("settings.data.grantpermission", "Grant Storage Permission")
                accessibleName: TranslationManager.translate("settings.data.grantpermissionAccessible",
                    "Open settings to grant storage permission")
                onClicked: {
                    BackupService.execute("requestStoragePermission")
                    historyDataTab.recheckStoragePermission()
                }
            }

            // Manual backup button with loading indicator
            RowLayout {
                Layout.alignment: Qt.AlignLeft
                spacing: Theme.scaled(8)

                AccessibleButton {
                    id: backupNowButton
                    enabled: BackupService.ready && historyDataTab.hasStoragePerm && !historyDataTab.backupSnapshot.activeOperation
                    text: historyDataTab.backupInProgress ?
                          TranslationManager.translate("settings.data.backingup", "Creating Backup...") :
                          TranslationManager.translate("settings.data.backupnow", "Backup Now")
                    accessibleName: TranslationManager.translate("settings.data.backupnowAccessible",
                        "Create a manual backup of shots, settings, profiles, and media")
                    onClicked: {
                        historyDataTab.backupRequestId = BackupService.execute("createBackup")
                    }
                }

                BusyIndicator {
                    visible: historyDataTab.backupInProgress
                    running: historyDataTab.backupInProgress
                    implicitWidth: Theme.scaled(20)
                    implicitHeight: Theme.scaled(20)
                }
            }

            // What the last restore's AI-conversation step could not do.
            //
            // Here, and not appended to the status pill: the pill is painted
            // in success green and auto-dismissed after five seconds, and
            // this message names an action the user has to take ("import the
            // shots as well", "run the import again"). It also has to appear
            // when the restore FAILED, which the pill's success path never
            // reaches. Bound to the property so it survives both.
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: historyDataTab.backupSnapshot.aiConversationNote || ""
                wrapMode: Text.WordWrap
                color: Theme.warningColor
                font.pixelSize: Theme.scaled(12)
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            // Restore from backup section
            Tr {
                key: "settings.data.restorefrombackup"
                fallback: "Restore backup"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(12)
            }

            StyledComboBox {
                id: restoreBackupCombo
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("settings.data.restorefrombackup", "Restore backup")
                enabled: BackupService.ready && displayNames.length > 0
                model: displayNames.length > 0 ? displayNames : [TranslationManager.translate("settings.data.nobackups", "No backups available")]
                currentIndex: 0

                // Derived from the cached C++ property (no blocking I/O)
                readonly property var rawBackups: historyDataTab.backupSnapshot.backups || []
                readonly property var displayNames: rawBackups.map(function(row) { return row.displayName })
                readonly property var backupIds: rawBackups.map(function(row) { return row.id })
            }

            RowLayout {
                Layout.fillWidth: true

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.saveBackup", "Save Backup to This Device")
                    accessibleName: TranslationManager.translate("settings.data.saveBackupAccessible", "Save selected backup to this device")
                    enabled: BackupService.ready && !historyDataTab.backupDownloadInProgress &&
                             !historyDataTab.backupSnapshot.activeOperation &&
                             restoreBackupCombo.currentIndex >= 0 &&
                             restoreBackupCombo.currentIndex < restoreBackupCombo.rawBackups.length &&
                             restoreBackupCombo.rawBackups[restoreBackupCombo.currentIndex].downloadable === true
                    onClicked: {
                        historyDataTab.backupDownloadRequestId = BackupService.execute("prepareBackupDownload", {
                            backupId: restoreBackupCombo.backupIds[restoreBackupCombo.currentIndex]
                        })
                    }
                }

                BusyIndicator {
                    visible: historyDataTab.backupDownloadInProgress
                    running: visible
                    implicitWidth: Theme.scaled(20)
                    implicitHeight: Theme.scaled(20)
                }

                AccessibleButton {
                    visible: historyDataTab.backupDownloadInProgress
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.data.cancelBackupSave", "Cancel saving backup")
                    onClicked: historyDataTab.cancelBackupDownload()
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                visible: backupDownload.busy && !backupDownload.waitingForSave
                from: 0
                to: backupDownload.totalBytes
                value: backupDownload.offset
                Accessible.name: TranslationManager.translate("settings.data.backupSaveProgress", "Saving backup")
            }

            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.data.restorebutton", "Restore Backup")
                enabled: BackupService.ready &&
                         restoreBackupCombo.displayNames.length > 0 &&
                         restoreBackupCombo.currentIndex >= 0 &&
                         !historyDataTab.backupSnapshot.activeOperation
                accessibleName: TranslationManager.translate("settings.data.restorebuttonAccessible",
                    "Restore shots, settings, profiles, and media from selected backup")
                onClicked: {
                    if (BackupService.ready && restoreBackupCombo.currentIndex >= 0) {
                        restoreConfirmDialog.resetDefaults();
                        restoreConfirmDialog.selectedBackup = restoreBackupCombo.backupIds[restoreBackupCombo.currentIndex];
                        restoreConfirmDialog.displayName = restoreBackupCombo.displayNames[restoreBackupCombo.currentIndex];
                        restoreConfirmDialog.open();
                    }
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.data.restoreFromFile", "Restore Backup from This Device")
                accessibleName: TranslationManager.translate("settings.data.restoreFromFileAccessible", "Select a backup on this device to restore")
                enabled: BackupService.ready && ImportService.ready && !historyDataTab.restoreInProgress &&
                         !historyDataTab.backupSnapshot.activeOperation
                onClicked: backupRestoreFileDialog.open()
            }
        }

        // Right column: Share Data card, plus Export Shots card below it
        ColumnLayout {
            Layout.preferredWidth: Theme.scaled(280)
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

        SettingsCard {
            searchId: "enableServer"
            title: TranslationManager.translate("settings.history.enableserver", "Enable Server")
            description: TranslationManager.translate("settings.data.enableserverdesc", "Access shot data, layout editor, and AI from your browser")
            keywords: ["server", "http", "web", "remote", "network", "share", "security", "https", "totp", "authenticator", "password", "encryption", "reset", "factory", "delete", "clear", "uninstall", "wipe"]
            showHeader: false
            fillContent: true
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
                    checked: shotServerPreferenceWrite.displayedValue("shotServerEnabled", historyDataTab.networkPreferences["network.shotServerEnabled"])
                    accessibleName: TranslationManager.translate("settings.history.enableserver", "Enable Server")
                    enabled: shotServerPreferenceWrite.ready
                    onToggled: shotServerPreferenceWrite.submit("shotServerEnabled", checked)
                }
            }

            // Server status indicator (URL link)
            RowLayout {
                id: serverStatusRow

                Layout.fillWidth: true
                spacing: Theme.scaled(6)
                visible: historyDataTab.networkPreferences["network.shotServerEnabled"] === true

                property bool serverRunning: historyDataTab.shotServerSnapshot.running === true
                property bool secured: serverStatusRow.serverRunning && historyDataTab.shotServerSnapshot.webSecurityEnabled === true &&
                                       historyDataTab.shotServerSnapshot.hasTotpSecret === true

                Rectangle {
                    Layout.preferredWidth: Theme.scaled(8)
                    Layout.preferredHeight: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    color: !serverStatusRow.serverRunning ? Theme.errorColor :
                           serverStatusRow.secured ? Theme.successColor : Theme.textSecondaryColor
                    Accessible.ignored: true
                }

                Text {
                    text: {
                        if (!ShotServerSettingsService.ready)
                            return TranslationManager.translate("settings.data.serverUnavailable",
                                "Host server status is unavailable while disconnected.");
                        if (!serverStatusRow.serverRunning)
                            return TranslationManager.translate("settings.data.serverstarting", "Starting...");
                        var url = historyDataTab.shotServerSnapshot.url || "";
                        if (serverStatusRow.secured)
                            return url + " \u2022 " + TranslationManager.translate("settings.data.secured", "Secured");
                        if (historyDataTab.shotServerSnapshot.webSecurityEnabled === true)
                            return url + " (HTTPS)";
                        return url;
                    }
                    color: serverStatusRow.secured ? Theme.successColor :
                           serverStatusRow.serverRunning ? Theme.textColor : Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    font.underline: serverStatusRow.serverRunning
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    Accessible.role: Accessible.Link
                    Accessible.name: text
                    Accessible.focusable: serverStatusRow.serverRunning
                    Accessible.onPressAction: Qt.openUrlExternally(historyDataTab.shotServerSnapshot.url)

                    TapHandler {
                        SettingsSearch.title: TranslationManager.translate("settings.search.serverAddress", "Open web server address")
                        enabled: serverStatusRow.serverRunning
                        onTapped: Qt.openUrlExternally(historyDataTab.shotServerSnapshot.url)
                    }
                }
            }

            // Security enable toggle
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: historyDataTab.networkPreferences["network.shotServerEnabled"] === true

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
                    checked: shotServerPreferenceWrite.displayedValue("webSecurityEnabled", historyDataTab.networkPreferences["network.webSecurityEnabled"])
                    accessibleName: TranslationManager.translate("settings.data.enablesecurity", "Enable Security")
                    enabled: shotServerPreferenceWrite.ready
                    onToggled: shotServerPreferenceWrite.submit("webSecurityEnabled", checked)
                }
            }

            // TOTP setup/reset buttons
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)
                visible: historyDataTab.networkPreferences["network.shotServerEnabled"] === true && historyDataTab.shotServerSnapshot.webSecurityEnabled === true

                AccessibleButton {
                    Layout.fillWidth: true
                    primary: true
                    text: TranslationManager.translate("settings.data.setuptotp", "Set Up Authenticator")
                    accessibleName: TranslationManager.translate("settings.data.setuptotpAccessible",
                        "Set up authenticator app for web access security")
                    visible: historyDataTab.shotServerSnapshot.hasTotpSecret !== true
                    enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                    onClicked: {
                        historyDataTab.shotServerPrivateOperation = "generateTotpSetup";
                        historyDataTab.shotServerPrivateRequestId = ShotServerSettingsService.execute("generateTotpSetup");
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    destructive: true
                    text: TranslationManager.translate("settings.data.resettotp", "Reset Security")
                    accessibleName: TranslationManager.translate("settings.data.resettotpAccessible",
                        "Remove authenticator and all web sessions")
                    visible: historyDataTab.shotServerSnapshot.hasTotpSecret === true
                    enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
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
                    fallback: "Shots"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                }
                Text {
                    text: HistoryService.totalShots
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(11)
                }

                Tr {
                    key: "settings.data.profiles"
                    fallback: "Profiles"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                }
                Text {
                    text: ProfileService.availableProfiles.length
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(11)
                }
            }

            Item { Layout.fillHeight: true }

            // Factory reset button
            AccessibleButton {
                Layout.fillWidth: true
                destructive: true
                SettingsSearch.title: TranslationManager.translate("settings.search.factoryResetTitle", "Factory Reset")
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

        // Export Shots card — writes each shot as visualizer-format JSON to
        // the user history folder alongside the user profiles folder. Off by
        // default; toggling on bulk-exports the entire shot history.
        SettingsCard {
            searchId: "exportShotsCard"
            title: TranslationManager.translate("settings.data.exportshots", "Export Shots to File")
            description: TranslationManager.translate("settings.search.exportShotsDesc", "Mirror shots to JSON files for external tools")
            keywords: ["export", "json", "shots", "mirror", "backup", "files"]
            showHeader: false

            Tr {
                key: "settings.data.exportshots"
                fallback: "Export Shots to File"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Tr {
                        key: "settings.data.exportshotsrow"
                        fallback: "Mirror shots to JSON files"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Tr {
                        key: "settings.data.exportshotsdesc"
                        fallback: "Writes each shot as visualizer-format JSON to the history folder alongside your profiles. Files are for your archive only — the app never reads them back."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(9)
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }

                StyledSwitch {
                    checked: exportShotsPreferenceWrite.displayedValue(
                        "exportShotsToFile",
                        historyDataTab.networkPreferences["network.exportShotsToFile"])
                    enabled: exportShotsPreferenceWrite.ready
                    accessibleName: TranslationManager.translate(
                        "settings.data.exportshots", "Export Shots to File")
                    onToggled: exportShotsPreferenceWrite.submit("exportShotsToFile", checked)
                }
            }
        }
        }

    // Device Migration Dialog
    DeviceMigrationDialog {
        id: deviceMigrationDialog
    }

    LocalFileTransfer {
        id: backupUpload
        onFinished: function(success, result, reason) {
            if (!success) {
                restoreConfirmDialog.close()
                if (reason !== "cancelled")
                    historyDataTab.showBackupResult("restoreUploadedBackup", false, ({}),
                        TranslationManager.translate("settings.data.backupUploadFailed", "The backup could not be uploaded. Check the file and connection, then try again."))
                return
            }
            const args = Object.assign({}, backupUpload.options, {transferId: result.transferId})
            historyDataTab.restoreRequestId = BackupService.execute("restoreUploadedBackup", args)
        }
    }

    FileDialog {
        id: backupRestoreFileDialog
        title: TranslationManager.translate("settings.data.selectBackupFile", "Select a Decenza backup")
        nameFilters: ["Decenza backups (*.zip *.dcbackup *.db)"]
        onAccepted: {
            restoreConfirmDialog.resetDefaults()
            restoreConfirmDialog.selectedUpload = selectedFile
            restoreConfirmDialog.displayName = decodeURIComponent(selectedFile.toString().split("/").pop())
            restoreConfirmDialog.open()
        }
    }

    LocalFileTransfer {
        id: shotFileTransfer
        onFinished: function(success, result, reason) {
            if (reason === "cancelled") return
            historyDataTab._showImportResult(success, result || ({}), reason)
        }
    }

    // File dialogs for shot import
        FileDialog {
            id: shotZipDialog
            title: TranslationManager.translate("settings.history.selectZipTitle", "Select shot history ZIP archive")
            nameFilters: ["ZIP archives (*.zip)", "All files (*)"]
            property bool overwrite: false

            onAccepted: {
                shotFileTransfer.start([selectedFile], "shotZip", { "overwrite": overwrite })
            }
        }

        FolderDialog {
            id: shotFolderDialog
            title: TranslationManager.translate("settings.history.selectFolderTitle", "Select folder containing .shot files")
            property bool overwrite: false

            onAccepted: {
                shotFileTransfer.start([selectedFolder], "shotFiles", { "overwrite": overwrite })
            }
        }

        // Extracting popup - shows during ZIP extraction
        DecenzaDialog {
            id: extractingPopup
            modal: true
            dim: true
            closePolicy: Dialog.NoAutoClose
            // qmllint disable Quick.layout-positioning
            // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
            // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
            // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
            // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
            anchors.centerIn: Overlay.overlay
            // qmllint enable Quick.layout-positioning
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
            target: ImportService
            function onStateChanged() {
                if (ImportService.ready && historyDataTab.importSnapshot.shotExtracting) extractingPopup.open()
                else extractingPopup.close()
            }
            function onOperationCompleted(requestId, operation, success, result, reason) {
                if (requestId !== historyDataTab._de1ImportRequestId) return
                historyDataTab._de1ImportRequestId = ""
                historyDataTab._showImportResult(success, result || ({}), reason)
            }
        }

        // Import result feedback dialog
        DecenzaDialog {
            id: importResultDialog
            modal: true
            dim: true
            closePolicy: Dialog.CloseOnEscape
            // qmllint disable Quick.layout-positioning
            // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
            // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
            // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
            // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
            anchors.centerIn: Overlay.overlay
            // qmllint enable Quick.layout-positioning
            padding: Theme.scaled(24)

            property string resultMessage: ""
            property bool isError: false

            onClosed: {
                resultMessage = ""
                isError = false
            }

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

    // Status message for backup operations (reparented to avoid layout warnings)
    Rectangle {
        id: backupStatusBackground
        parent: Overlay.overlay
        visible: false
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        anchors.bottom: parent.bottom
        // qmllint enable Quick.layout-positioning
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.scaled(20)
        // NOT Layout.preferred*: this is reparented to Overlay.overlay and positioned with
        // anchors, so no Layout manages it and the attached properties would be inert — the
        // pill would collapse to 0x0 (Rectangle's implicit size). qmllint flags it as
        // layout-positioning because it is DECLARED inside one; that is a false positive.
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        width: backupStatusText.implicitWidth + Theme.scaled(20)
        height: backupStatusText.implicitHeight + Theme.scaled(20)
        // qmllint enable Quick.layout-positioning
        color: Theme.surfaceColor
        radius: Theme.scaled(4)
        border.color: Theme.borderColor
        border.width: 1
        z: 100

        Text {
            id: backupStatusText
            anchors.centerIn: parent
            font.pixelSize: Theme.scaled(12)
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    // Timer to auto-hide status message
    Timer {
        id: backupStatusTimer
        interval: 5000
        onTriggered: backupStatusBackground.visible = false
    }

    // Restore confirmation dialog
    DecenzaDialog {
        id: restoreConfirmDialog
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        anchors.centerIn: parent
        width: Theme.scaled(400)
        // qmllint enable Quick.layout-positioning
        padding: 0
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        property string selectedBackup: ""
        property url selectedUpload: ""
        property string displayName: ""
        property bool mergeMode: true
        property bool restoreShots: true
        property bool restoreSettings: true
        property bool restoreProfiles: true
        property bool restoreMedia: true

        function resetDefaults() {
            selectedUpload = "";
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

                    ProgressBar {
                        Layout.fillWidth: true
                        visible: backupUpload.busy && backupUpload.selectedFiles.length > 0
                        from: 0
                        to: backupUpload.selectedFiles.length > 0 ? backupUpload.selectedFiles[0].size : 1
                        value: backupUpload.offset
                        Accessible.name: TranslationManager.translate("settings.data.backupUploadProgress", "Uploading backup")
                    }

                    AccessibleButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: backupUpload.busy
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.data.cancelBackupUpload", "Cancel uploading backup")
                        onClicked: backupUpload.abort("cancelled")
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
                        accessibleName: TranslationManager.translate("settings.data.settingsai.accessible", "Settings & AI Conversations") + ", " +
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
                        Layout.preferredHeight: 1
                        color: Theme.borderColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)

                            // The label is STATIC and names what the switch does.
                            // It used to be a status line reading "Merge with
                            // existing data" while the switch beside it was
                            // checked for REPLACE — so an unchecked switch next
                            // to the word "Merge" meant a merge was about to
                            // happen. The maintainer misread his own dialog that
                            // way during testing, on a control that can delete
                            // every shot on the device. A switch adjacent to
                            // text is read as controlling that text.
                            Text {
                                text: TranslationManager.translate("settings.data.mergemode", "Merge with existing data")
                                color: Theme.textColor
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
                                        "Off: deletes ALL current shots and profiles, replaces with backup. Cannot be undone!")
                                color: restoreConfirmDialog.mergeMode ? Theme.textSecondaryColor : Theme.warningColor
                                font.pixelSize: Theme.scaled(10)
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }

                        StyledSwitch {
                            checked: restoreConfirmDialog.mergeMode
                            accessibleName: TranslationManager.translate("settings.data.mergemode", "Merge with existing data")
                            onToggled: restoreConfirmDialog.mergeMode = checked
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
                            if (backupUpload.busy) {
                                backupUpload.abort("cancelled")
                                return
                            }
                            if (historyDataTab.restoreInProgress) return
                            restoreConfirmDialog.resetDefaults();
                            restoreConfirmDialog.close();
                        }
                    }

                    AccessibleButton {
                        id: confirmButton
                        text: TranslationManager.translate("common.restore", "Restore")
                        primary: true
                        enabled: BackupService.ready && !historyDataTab.backupInProgress
                                 && !historyDataTab.restoreInProgress &&
                                 (restoreConfirmDialog.restoreShots || restoreConfirmDialog.restoreSettings ||
                                  restoreConfirmDialog.restoreProfiles || restoreConfirmDialog.restoreMedia)
                        accessibleName: TranslationManager.translate("settings.data.confirmrestore", "Confirm restore backup")
                        onClicked: {
                            const options = {
                                merge: restoreConfirmDialog.mergeMode,
                                shots: restoreConfirmDialog.restoreShots,
                                settings: restoreConfirmDialog.restoreSettings,
                                profiles: restoreConfirmDialog.restoreProfiles,
                                media: restoreConfirmDialog.restoreMedia
                            }
                            if (restoreConfirmDialog.selectedUpload.toString().length > 0) {
                                backupUpload.start([restoreConfirmDialog.selectedUpload], "backupArchive", options)
                            } else {
                                historyDataTab.restoreRequestId = BackupService.execute("restoreBackup",
                                    Object.assign({}, options, {backupId: restoreConfirmDialog.selectedBackup}))
                            }
                        }
                    }
                }
            }
        }
    }

    // TOTP Setup Dialog
    DecenzaDialog {
        id: totpSetupDialog
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        x: Math.round((parent.width - width) / 2)
        // qmllint enable Quick.layout-positioning
        y: {
            if (totpCodeField.activeFocus) {
                // Center in the visible area above the keyboard
                let kbHeight = Keyboard.rectangle.height;
                if (kbHeight <= 0 && (Qt.platform.os === "android" || Qt.platform.os === "ios"))
                    kbHeight = parent.height * 0.45;
                let availableHeight = parent.height - kbHeight;
                return Math.round(Math.max(Theme.scaled(10), (availableHeight - height) / 2));
            }
            return Math.round((parent.height - height) / 2);
        }
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        width: Theme.scaled(380)
        padding: 0
        modal: true

        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
        // qmllint enable Quick.layout-positioning

        property string totpSecret: ""
        property bool totpSecretCopied: false
        property string totpUri: ""
        property string verifyError: ""
        property bool verifying: false

        onOpened: {
            totpSecretCopied = false
            secretCopyTimer.stop()
        }

        onClosed: {
            totpCodeField.text = "";
            verifyError = "";
            verifying = false;
            totpSecretCopied = false
            secretCopyTimer.stop()
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
                    Layout.preferredWidth: Theme.scaled(200)
                    Layout.preferredHeight: Theme.scaled(200)
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
                            Layout.preferredHeight: Theme.scaled(36)
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: Theme.borderColor
                            border.width: 1

                            TextInput {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(6)
                                text: totpSetupDialog.totpSecret
                                readOnly: true
                                selectByMouse: true
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                clip: true
                                color: Theme.textColor
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.scaled(11)
                                font.bold: true

                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.data.totpsecretAccessible",
                                    "Secret code for manual entry: ") + totpSetupDialog.totpSecret
                            }
                        }

                        AccessibleButton {
                            text: totpSetupDialog.totpSecretCopied && secretCopyTimer.running ?
                                  TranslationManager.translate("settings.data.copied", "Copied") :
                                  TranslationManager.translate("settings.data.copy", "Copy")
                            accessibleName: TranslationManager.translate("settings.data.copySecretAccessible",
                                "Copy secret code to clipboard")
                            enabled: historyDataTab.totpClipboardRequestId.length === 0
                            onClicked: historyDataTab.copyTotpSecret()

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
                                     && ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                            onClicked: {
                                Keyboard.commit()
                                totpSetupDialog.verifying = true;
                                historyDataTab.shotServerPrivateOperation = "completeTotpSetup";
                                historyDataTab.shotServerPrivateRequestId = ShotServerSettingsService.execute(
                                    "completeTotpSetup", {secret: totpSetupDialog.totpSecret, code: totpCodeField.text});
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
    DecenzaDialog {
        id: totpResetDialog
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        anchors.centerIn: parent
        width: Theme.scaled(380)
        // qmllint enable Quick.layout-positioning
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
                        enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                        accessibleName: TranslationManager.translate("settings.data.confirmResetAccessible",
                            "Confirm: remove authenticator and sign out all sessions")
                        onClicked: {
                            historyDataTab.shotServerPrivateOperation = "resetTotpSecret";
                            historyDataTab.shotServerPrivateRequestId = ShotServerSettingsService.execute("resetTotpSecret");
                        }
                    }
                }
            }
        }
    }

    // Factory Reset - Confirmation Dialog 1
    DecenzaDialog {
        id: factoryResetDialog1
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        anchors.centerIn: parent
        width: Theme.scaled(400)
        // qmllint enable Quick.layout-positioning
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
    DecenzaDialog {
        id: factoryResetDialog2
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        anchors.centerIn: parent
        width: Theme.scaled(400)
        // qmllint enable Quick.layout-positioning
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
                        enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                        accessibleName: TranslationManager.translate("settings.data.factoryreset.nukeaccessible",
                            "Confirm: permanently delete all data and exit the app")
                        onClicked: {
                            factoryResetDialog2.close()
                            historyDataTab.shotServerPrivateOperation = "factoryResetAndQuit"
                            historyDataTab.shotServerPrivateRequestId = ShotServerSettingsService.execute("factoryResetAndQuit");
                        }
                    }
                }
            }
        }
    }

}
}
