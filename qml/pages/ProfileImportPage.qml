// The profile-list delegate reads this file's `profileList` id; Bound makes it
// statically resolvable. It declares both injected roles it uses, `modelData` and
// `index`, required in the same edit -- without that, Bound stops role injection and
// every row renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Dialogs
import QtQuick.Layouts
import Decenza

T.Page {
    id: profileImportPage
    objectName: "profileImportPage"
    background: ThemedPageBackground {}

    readonly property bool isIOS: Qt.platform.os === "ios"
    // The service publishes an empty snapshot while it is disconnected. Keep
    // every presentation binding on typed, display-only values so that a
    // reconnect never assigns undefined into a control or model.
    readonly property var importState: ProfileImportService.snapshot || ({})
    readonly property string statusMessage: typeof importState.statusMessage === "string"
        ? importState.statusMessage : ""
    readonly property string detectedFolder: typeof importState.detectedFolder === "string"
        ? importState.detectedFolder : ""
    readonly property var availableProfiles: Array.from(importState.availableProfiles || [])
    readonly property bool isImporting: importState.isImporting === true
    readonly property bool isScanning: importState.isScanning === true
    readonly property bool profilesTruncated: importState.profilesTruncated === true
    readonly property int processedProfiles: Math.max(0, Number(importState.processedProfiles) || 0)
    readonly property int totalProfiles: Math.max(0, Number(importState.totalProfiles) || 0)
    readonly property int totalAvailableProfiles: importState.totalAvailableProfiles === undefined
        || importState.totalAvailableProfiles === null
        ? availableProfiles.length
        : Math.max(0, Number(importState.totalAvailableProfiles) || 0)
    property bool initialScanPending: false
    property string pendingTransferId: ""
    property string pendingTransferOperation: ""
    readonly property string pageTitle: isIOS ?
        TranslationManager.translate("profileimport.title_ios", "Import Profile") :
        TranslationManager.translate("profileimport.title", "Import from Tablet")

    function rescan() {
        ProfileImportService.scanProfiles()
    }

    LocalFileTransfer {
        id: profileFileTransfer
        onFinished: function(success, result, reason) {
            if (!success) {
                if (reason === "cancelled") return
                WebDebugLogger.warn("Profiles", "ProfileImportPage", "Selection/import transfer failed: " + reason)
                return
            }
            const nextOperation = profileImportPage.isIOS ? "importTransfer" : "scanTransfer"
            if (ProfileImportService.ready)
                ProfileImportService.execute(nextOperation, { "transferId": result.transferId })
            else {
                profileImportPage.pendingTransferId = result.transferId
                profileImportPage.pendingTransferOperation = nextOperation
            }
        }
    }

    Component.onCompleted: {
        if (!isIOS) {
            if (ProfileImportService.ready) ProfileImportService.scanProfiles()
            else initialScanPending = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin
        spacing: Theme.scaled(15)

        // iOS: File picker UI
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius
            visible: profileImportPage.isIOS

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.scaled(20)
                width: parent.width * 0.7

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.ios.description",
                        "Import .json or .tcl profile files from\niCloud Drive, Files app, or other sources")
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                AccessibleButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Theme.scaled(250)
                    Layout.preferredHeight: Theme.scaled(60)
                    text: TranslationManager.translate("profileimport.ios.choose_file", "Choose Profile File")
                    accessibleName: TranslationManager.translate("profileImport.openFilePicker", "Open file picker to select a profile file")
                    primary: true
                    enabled: ProfileImportService.ready && !profileImportPage.isImporting
                    onClicked: profileFileDialog.open()
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: profileImportPage.statusMessage
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    visible: profileImportPage.statusMessage !== ""
                }
            }
        }

        // Header section (non-iOS only)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(80)
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius
            visible: !profileImportPage.isIOS

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(15)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Text {
                        text: profileImportPage.detectedFolder || TranslationManager.translate("profileimport.not_found", "DE1 app not found")
                        color: profileImportPage.detectedFolder ? Theme.textColor : Theme.textSecondaryColor
                        font: Theme.bodyFont
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Text {
                        text: profileImportPage.statusMessage
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.browse", "Browse...")
                    accessibleName: TranslationManager.translate("profileImport.browseFolder", "Browse for de1plus folder")
                    // Directory selection is requester-local and transfers only bounded file bytes;
                    // no selected host or browser path is sent to the importer.
                    visible: Qt.platform.os !== "android"
                    enabled: ProfileImportService.ready && !profileImportPage.isScanning
                    onClicked: {
                        if (Qt.platform.os === "wasm")
                            profileFileTransfer.startDirectory("profileFiles", {})
                        else
                            profileDirectoryDialog.open()
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate(
                        "profileimport.cancelFolderSelection", "Cancel folder selection")
                    visible: profileFileTransfer.directorySelectionPending
                    onClicked: profileFileTransfer.cancelDirectorySelection()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.import_file", "Import File...")
                    accessibleName: TranslationManager.translate("profileImport.importFileAndroid", "Open file picker to import a profile file")
                    visible: Qt.platform.os === "android"
                    enabled: ProfileImportService.ready && !profileImportPage.isImporting
                    onClicked: profileFileDialog.open()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.rescan", "Rescan")
                    accessibleName: TranslationManager.translate("profileImport.rescanProfiles", "Rescan for profiles from Decent tablet")
                    enabled: ProfileImportService.ready && !profileImportPage.isScanning
                    onClicked: profileImportPage.rescan()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.import_all", "Import All New")
                    accessibleName: TranslationManager.translate("profileImport.importAllNew", "Import all new profiles at once")
                    primary: true
                    visible: profileImportPage.availableProfiles.length > 0
                    enabled: ProfileImportService.ready && !profileImportPage.isImporting && !profileImportPage.isScanning
                    onClicked: ProfileImportService.importAllNew()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.update_all", "Update All")
                    accessibleName: TranslationManager.translate("profileImport.updateAllChanged", "Update all profiles that have changed")
                    warning: true
                    visible: {
                        // Only show if there are profiles with "different" status
                        var profiles = profileImportPage.availableProfiles
                        for (let i = 0; i < profiles.length; i++) {
                            if (profiles[i].status === "different") return true
                        }
                        return false
                    }
                    enabled: ProfileImportService.ready && !profileImportPage.isImporting && !profileImportPage.isScanning
                    onClicked: ProfileImportService.updateAllDifferent()
                }
            }
        }

        // Progress bar (during scanning/importing, non-iOS only)
        ProgressBar {
            id: progressBar
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(4)
            visible: !profileImportPage.isIOS && (profileImportPage.isScanning || profileImportPage.isImporting)
            from: 0
            to: profileImportPage.totalProfiles
            value: profileImportPage.processedProfiles

            background: Rectangle {
                color: Theme.surfaceColor
                radius: 2
            }
            contentItem: Rectangle {
                width: progressBar.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.primaryColor
            }
        }

        // Profile list (non-iOS only)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius
            visible: !profileImportPage.isIOS

            ListView {
                id: profileList
                anchors.fill: parent
                anchors.margins: Theme.scaled(10)
                clip: true
                model: profileImportPage.availableProfiles
                spacing: Theme.scaled(4)
                header: Text {
                    width: profileList.width
                    height: profileImportPage.profilesTruncated ? implicitHeight + Theme.spacingSmall : 0
                    visible: profileImportPage.profilesTruncated
                    text: TranslationManager.translate("profileImport.listTruncated",
                        "Showing %1 of %2 profiles. Batch actions still apply to the selected folder.")
                        .arg(profileImportPage.availableProfiles.length)
                        .arg(profileImportPage.totalAvailableProfiles)
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                }

                delegate: Rectangle {
                    id: profileDelegate
                    required property var modelData
                    required property int index

                    width: profileList.width
                    height: Math.max(Theme.scaled(56), importContentRow.implicitHeight + Theme.scaled(10) * 2)
                    radius: Theme.scaled(6)
                    color: profileDelegate.index % 2 === 0 ? Theme.rowAlternateColor : Theme.rowAlternateLightColor

                    property var profileData: profileDelegate.modelData
                    property string status: profileData.status || "new"
                    property bool isNew: status === "new"
                    property bool isIdentical: status === "identical"
                    property bool isDifferent: status === "different"

                    // Long-press to force re-import (overwrite without asking)
                    TapHandler {
                        longPressThreshold: 0.5
                        onLongPressed: {
                            if (!profileImportPage.isImporting) {
                                ProfileImportService.importById(profileDelegate.profileData.id, true)
                            }
                        }
                    }

                    RowLayout {
                        id: importContentRow
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(10)

                        // Status icon with semantic color
                        ColoredIcon {
                            Layout.preferredWidth: Theme.scaled(24)
                            Layout.preferredHeight: Theme.scaled(24)
                            Layout.alignment: Qt.AlignVCenter
                            source: profileDelegate.isNew ? "qrc:/icons/star-outline.svg" :
                                    profileDelegate.isIdentical ? "qrc:/icons/tick.svg" :
                                    "qrc:/icons/warning.svg"
                            iconWidth: Theme.scaled(18)
                            iconHeight: Theme.scaled(18)
                            iconColor: profileDelegate.isNew ? Theme.primaryColor :
                                       profileDelegate.isIdentical ? Theme.successColor :
                                       Theme.warningButtonColor
                        }

                        // Format badge (TCL/JSON)
                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(40)
                            Layout.preferredHeight: Theme.scaled(20)
                            radius: Theme.scaled(4)
                            color: profileDelegate.profileData.format === "TCL" ? Theme.sourceBadgeBlueColor : Theme.sourceBadgeGreenColor

                            Text {
                                anchors.centerIn: parent
                                text: profileDelegate.profileData.format || "?"
                                font.pixelSize: Theme.scaled(10)
                                font.bold: true
                                color: Theme.primaryContrastColor
                            }
                        }

                        // Profile info
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: profileDelegate.profileData.title || profileDelegate.profileData.filename
                                color: Theme.textColor
                                font: Theme.bodyFont
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: {
                                    var parts = []
                                    if (profileDelegate.profileData.author) parts.push(profileDelegate.profileData.author)
                                    parts.push((profileDelegate.profileData.frameCount || 0) + " frames")
                                    if (profileDelegate.isIdentical) {
                                        parts.push(TranslationManager.translate("profileimport.status.identical", "Already imported"))
                                    } else if (profileDelegate.isDifferent) {
                                        parts.push(TranslationManager.translate("profileimport.status.different", "Different version exists"))
                                    }
                                    return parts.join(" \u2022 ")
                                }
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                elide: Text.ElideRight
                            }
                        }

                        // Import button
                        AccessibleButton {
                            visible: !profileDelegate.isIdentical
                            text: profileDelegate.isDifferent ?
                                  TranslationManager.translate("profileimport.button.update", "Update") :
                                  TranslationManager.translate("profileimport.button.import", "Import")
                            accessibleName: profileDelegate.isDifferent ?
                                  TranslationManager.translate("profileImport.updateProfileVersion", "Update profile %1 with newer version").arg(profileDelegate.profileData.title) :
                                  TranslationManager.translate("profileImport.importProfile", "Import profile %1").arg(profileDelegate.profileData.title)
                            Accessible.description: profileDelegate.isDifferent ?
                                TranslationManager.translate("profileimport.accessible.forceHint", "Long-press the row to force overwrite without prompting.") : ""
                            warning: profileDelegate.isDifferent
                            primary: !profileDelegate.isDifferent
                            enabled: ProfileImportService.ready && !profileImportPage.isImporting
                            onClicked: {
                                ProfileImportService.importById(profileDelegate.profileData.id, false)
                            }
                        }

                        // Already imported indicator
                        ColoredIcon {
                            visible: profileDelegate.isIdentical
                            source: "qrc:/icons/tick.svg"
                            iconWidth: Theme.scaled(18)
                            iconHeight: Theme.scaled(18)
                            iconColor: Theme.successColor
                        }
                    }
                }

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: profileList.count === 0 && !profileImportPage.isScanning
                    text: profileImportPage.detectedFolder ?
                          TranslationManager.translate("profileimport.empty", "No profiles found in DE1 app folders") :
                          (Qt.platform.os === "android"
                           ? TranslationManager.translate("profileimport.not_installed_android", "DE1 app profiles not found.\n\nMake sure the DE1 app is installed\nand has profiles in de1plus/profiles,\nor use Import File to select profiles individually.")
                           : TranslationManager.translate("profileimport.not_installed", "DE1 app profiles not found.\n\nMake sure the DE1 app is installed\nand has profiles in de1plus/profiles,\nor use Browse to select the folder manually."))
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                // Loading indicator
                BusyIndicator {
                    anchors.centerIn: parent
                    visible: profileImportPage.isScanning && profileList.count === 0
                    running: visible
                }
            }
        }
    }

    // File picker dialog (used on iOS, available on all platforms)
    FileDialog {
        id: profileFileDialog
        title: TranslationManager.translate("profileimport.ios.dialog_title", "Select Profile File")
        nameFilters: ["Profile files (*.json *.tcl)", "JSON profiles (*.json)", "TCL profiles (*.tcl)", "All files (*)"]
        onAccepted: {
            profileFileTransfer.start([selectedFile], "profileFiles", {})
        }
    }

    FolderDialog {
        id: profileDirectoryDialog
        title: TranslationManager.translate("profileimport.dialog.select_folder", "Select folder containing profiles")
        onAccepted: profileFileTransfer.start([selectedFolder], "profileFiles", {})
    }

    // Duplicate dialog
    DecenzaDialog {
        id: duplicateDialog
        anchors.centerIn: parent
        width: Theme.scaled(400)
        modal: true
        title: TranslationManager.translate("profileimport.dialog.duplicate_title", "Profile Already Exists")

        // The import is paused for this choice, and Save as New can contain a
        // name that exists only in this dialog. Reloading here would cancel the
        // import and discard that draft.
        browserUnloadDirty: visible

        property string profileTitle: ""
        property bool showNameInput: false
        property bool actionTaken: false
        property string resolutionError: ""

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                Layout.fillWidth: true
                visible: duplicateDialog.resolutionError !== ""
                text: duplicateDialog.resolutionError
                color: Theme.errorColor
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: !duplicateDialog.showNameInput
                text: "\"" + duplicateDialog.profileTitle + "\" " +
                      TranslationManager.translate("profileimport.dialog.duplicate_msg",
                          "already exists with different settings.\n\nWhat would you like to do?")
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
            }

            // Name input (for Save as New)
            ColumnLayout {
                Layout.fillWidth: true
                visible: duplicateDialog.showNameInput
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("profileimport.dialog.new_name", "Enter a new name:")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }

                StyledTextField {
                    id: newNameInput
                    Layout.fillWidth: true
                    text: duplicateDialog.profileTitle + " (tablet)"
                    placeholderText: TranslationManager.translate("profileimport.dialog.name_placeholder", "Profile name")
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)
                visible: !duplicateDialog.showNameInput

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.overwrite", "Overwrite")
                    accessibleName: TranslationManager.translate("profileImport.overwriteExisting", "Overwrite existing profile with this version")
                    destructive: true
                    onClicked: {
                        duplicateDialog.actionTaken = true
                        duplicateDialog.resolutionError = ""
                        ProfileImportService.saveOverwrite()
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.save_as_new", "Save as New")
                    accessibleName: TranslationManager.translate("profileImport.saveAsNewProfile", "Save as a new profile with different name")
                    primary: true
                    onClicked: {
                        duplicateDialog.showNameInput = true
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("profileImport.cancelImport", "Cancel import and close dialog")
                    onClicked: {
                        duplicateDialog.actionTaken = true
                        ProfileImportService.cancelImport()
                        duplicateDialog.close()
                    }
                }
            }

            // Name input buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)
                visible: duplicateDialog.showNameInput

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.save", "Save")
                    accessibleName: TranslationManager.translate("profileImport.saveWithNewName", "Save profile with the new name")
                    primary: true
                    enabled: newNameInput.text.trim().length > 0
                    onClicked: {
                        Keyboard.commit()
                        duplicateDialog.actionTaken = true
                        duplicateDialog.resolutionError = ""
                        ProfileImportService.saveWithNewName(newNameInput.text.trim())
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.back", "Back")
                    accessibleName: TranslationManager.translate("profileImport.goBackToChoose", "Go back to choose a different option")
                    onClicked: {
                        duplicateDialog.showNameInput = false
                    }
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(8)
            border.color: Theme.borderColor
        }

        onClosed: {
            // If closed without an action (tap outside, Escape), cancel the pending import
            if (!actionTaken) {
                ProfileImportService.cancelImport()
            }
            actionTaken = false
            showNameInput = false
        }
    }

    // Connections for signals
    Connections {
        target: ProfileImportService

        function onReadyChanged() {
            if (!ProfileImportService.ready) return
            if (profileImportPage.initialScanPending) {
                profileImportPage.initialScanPending = false
                ProfileImportService.scanProfiles()
            }
            if (profileImportPage.pendingTransferId.length > 0) {
                const id = profileImportPage.pendingTransferId
                const op = profileImportPage.pendingTransferOperation
                profileImportPage.pendingTransferId = ""
                profileImportPage.pendingTransferOperation = ""
                ProfileImportService.execute(op, { "transferId": id })
            }
        }

        function onDuplicateFound(profileTitle, existingPath) {
            duplicateDialog.profileTitle = profileTitle
            duplicateDialog.showNameInput = false
            duplicateDialog.resolutionError = ""
            newNameInput.text = profileTitle + (profileImportPage.isIOS ? " (imported)" : " (tablet)")
            duplicateDialog.open()
        }

        function onOperationRejected(requestId, operation, reason) {
            duplicateDialog.resolutionError = TranslationManager.translate(
                "profileimport.dialog.invalid_name", "That name could not be used. Check it and try again.")
        }

        function onImportSuccess(profileTitle) {
            if (duplicateDialog.visible)
                duplicateDialog.close()
            if (!profileImportPage.isIOS) {
                // Refresh the profile status in the list
                profileImportPage.rescan()
            }
        }

        function onImportFailed(error) {
            if (duplicateDialog.visible) {
                duplicateDialog.resolutionError = TranslationManager.translate(
                    "profileimport.dialog.import_failed", "The profile could not be saved. Your choices are still available.")
                return
            }
            // error is also surfaced via statusMessage (set by C++ setStatus(error))
            WebDebugLogger.warn("Profiles", "ProfileImportPage", ["ProfileImporter: import failed:", error].map(String).join(" "))
        }

    }

    // Bottom bar
    BottomBar {
        title: profileImportPage.pageTitle
        onBackClicked: AppShell.backRequested()
    }
}
