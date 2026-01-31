import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: profileImportPage
    objectName: "profileImportPage"
    background: Rectangle { color: Theme.backgroundColor }

    readonly property bool isIOS: Qt.platform.os === "ios"
    readonly property string pageTitle: isIOS ?
        TranslationManager.translate("profileimport.title_ios", "Import Profile") :
        TranslationManager.translate("profileimport.title", "Import from Tablet")

    Component.onCompleted: {
        root.currentPageTitle = pageTitle
        if (!isIOS) {
            MainController.profileImporter.scanProfiles()
        }
    }
    StackView.onActivated: root.currentPageTitle = pageTitle

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
            color: Theme.surfaceColor
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
                    accessibleName: qsTr("Open file picker to select a profile file")
                    primary: true
                    enabled: !MainController.profileImporter.isImporting
                    onClicked: profileFileDialog.open()
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: MainController.profileImporter.statusMessage
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    visible: MainController.profileImporter.statusMessage !== ""
                }
            }
        }

        // Header section (non-iOS only)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(80)
            color: Theme.surfaceColor
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
                        text: MainController.profileImporter.detectedPath || TranslationManager.translate("profileimport.not_found", "DE1 app not found")
                        color: MainController.profileImporter.detectedPath ? Theme.textColor : Theme.textSecondaryColor
                        font: Theme.bodyFont
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Text {
                        text: MainController.profileImporter.statusMessage
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.rescan", "Rescan")
                    accessibleName: qsTr("Rescan for profiles from Decent tablet")
                    enabled: !MainController.profileImporter.isScanning
                    onClicked: MainController.profileImporter.scanProfiles()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.import_all", "Import All New")
                    accessibleName: qsTr("Import all new profiles at once")
                    primary: true
                    visible: MainController.profileImporter.availableProfiles.length > 0
                    enabled: !MainController.profileImporter.isImporting && !MainController.profileImporter.isScanning
                    onClicked: MainController.profileImporter.importAllNew()
                }

                AccessibleButton {
                    text: TranslationManager.translate("profileimport.button.update_all", "Update All")
                    accessibleName: qsTr("Update all profiles that have changed")
                    warning: true
                    visible: {
                        // Only show if there are profiles with "different" status
                        var profiles = MainController.profileImporter.availableProfiles
                        for (var i = 0; i < profiles.length; i++) {
                            if (profiles[i].status === "different") return true
                        }
                        return false
                    }
                    enabled: !MainController.profileImporter.isImporting && !MainController.profileImporter.isScanning
                    onClicked: MainController.profileImporter.updateAllDifferent()
                }
            }
        }

        // Progress bar (during scanning/importing, non-iOS only)
        ProgressBar {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(4)
            visible: !profileImportPage.isIOS && (MainController.profileImporter.isScanning || MainController.profileImporter.isImporting)
            from: 0
            to: MainController.profileImporter.totalProfiles
            value: MainController.profileImporter.processedProfiles

            background: Rectangle {
                color: Theme.surfaceColor
                radius: 2
            }
            contentItem: Rectangle {
                width: parent.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.primaryColor
            }
        }

        // Profile list (non-iOS only)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            visible: !profileImportPage.isIOS

            ListView {
                id: profileList
                anchors.fill: parent
                anchors.margins: Theme.scaled(10)
                clip: true
                model: MainController.profileImporter.availableProfiles
                spacing: Theme.scaled(4)

                delegate: Rectangle {
                    id: profileDelegate
                    width: profileList.width
                    height: Theme.scaled(56)
                    radius: Theme.scaled(6)
                    color: index % 2 === 0 ? "#1a1a1a" : "#222222"

                    property var profileData: modelData
                    property string status: profileData.status || "new"
                    property bool isNew: status === "new"
                    property bool isIdentical: status === "identical"
                    property bool isDifferent: status === "different"

                    // Long-press to force re-import (overwrite without asking)
                    TapHandler {
                        longPressThreshold: 0.5
                        onLongPressed: {
                            if (!MainController.profileImporter.isImporting) {
                                MainController.profileImporter.forceImportProfile(profileData.sourcePath)
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(10)

                        // Status icon
                        Text {
                            Layout.preferredWidth: Theme.scaled(24)
                            Layout.alignment: Qt.AlignVCenter
                            text: profileDelegate.isNew ? "\u2606" :      // Hollow star - new
                                  profileDelegate.isIdentical ? "\u2713" : // Checkmark - identical
                                  "\u26A0"                                  // Warning - different
                            font.pixelSize: Theme.scaled(18)
                            color: profileDelegate.isNew ? Theme.primaryColor :
                                   profileDelegate.isIdentical ? Theme.successColor :
                                   "#FFA500"  // Orange for different
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // Format badge (TCL/JSON)
                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(40)
                            Layout.preferredHeight: Theme.scaled(20)
                            radius: Theme.scaled(4)
                            color: profileData.format === "TCL" ? "#4a90d9" : "#4ad94a"

                            Text {
                                anchors.centerIn: parent
                                text: profileData.format || "?"
                                font.pixelSize: Theme.scaled(10)
                                font.bold: true
                                color: "white"
                            }
                        }

                        // Profile info
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: profileData.title || profileData.filename
                                color: Theme.textColor
                                font: Theme.bodyFont
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: {
                                    var parts = []
                                    if (profileData.author) parts.push(profileData.author)
                                    parts.push((profileData.frameCount || 0) + " frames")
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
                                  qsTr("Update profile %1 with newer version").arg(profileData.title) :
                                  qsTr("Import profile %1").arg(profileData.title)
                            warning: profileDelegate.isDifferent
                            primary: !profileDelegate.isDifferent
                            enabled: !MainController.profileImporter.isImporting
                            onClicked: {
                                MainController.profileImporter.importProfile(profileData.sourcePath)
                            }
                        }

                        // Already imported indicator
                        Text {
                            visible: profileDelegate.isIdentical
                            text: "\u2713"
                            font.pixelSize: Theme.scaled(20)
                            color: Theme.successColor
                        }
                    }
                }

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: profileList.count === 0 && !MainController.profileImporter.isScanning
                    text: MainController.profileImporter.detectedPath ?
                          TranslationManager.translate("profileimport.empty", "No profiles found in DE1 app folders") :
                          TranslationManager.translate("profileimport.not_installed", "DE1 app profiles not found.\n\nMake sure the DE1 app is installed\nand has profiles in de1plus/profiles.")
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                }

                // Loading indicator
                BusyIndicator {
                    anchors.centerIn: parent
                    visible: MainController.profileImporter.isScanning && profileList.count === 0
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
            MainController.profileImporter.importProfileFromUrl(selectedFile)
        }
    }

    // Duplicate dialog
    Dialog {
        id: duplicateDialog
        anchors.centerIn: parent
        width: Theme.scaled(400)
        modal: true
        title: TranslationManager.translate("profileimport.dialog.duplicate_title", "Profile Already Exists")

        property string profileTitle: ""
        property bool showNameInput: false

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

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
                    accessibleName: qsTr("Overwrite existing profile with this version")
                    destructive: true
                    onClicked: {
                        MainController.profileImporter.saveOverwrite()
                        duplicateDialog.close()
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.save_as_new", "Save as New")
                    accessibleName: qsTr("Save as a new profile with different name")
                    primary: true
                    onClicked: {
                        duplicateDialog.showNameInput = true
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.cancel", "Cancel")
                    accessibleName: qsTr("Cancel import and close dialog")
                    onClicked: {
                        MainController.profileImporter.cancelImport()
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
                    accessibleName: qsTr("Save profile with the new name")
                    primary: true
                    enabled: newNameInput.text.trim().length > 0
                    onClicked: {
                        MainController.profileImporter.saveWithNewName(newNameInput.text.trim())
                        duplicateDialog.close()
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.back", "Back")
                    accessibleName: qsTr("Go back to choose a different option")
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
            showNameInput = false
        }
    }

    // Connections for signals
    Connections {
        target: MainController.profileImporter

        function onDuplicateFound(profileTitle, existingPath) {
            duplicateDialog.profileTitle = profileTitle
            duplicateDialog.showNameInput = false
            newNameInput.text = profileTitle + (profileImportPage.isIOS ? " (imported)" : " (tablet)")
            duplicateDialog.open()
        }

        function onImportSuccess(profileTitle) {
            if (!profileImportPage.isIOS) {
                // Refresh the profile status in the list
                MainController.profileImporter.scanProfiles()
            }
        }

        function onBatchImportComplete(imported, skipped, failed) {
            // Could show a summary dialog here if desired
        }
    }

    // Bottom bar
    BottomBar {
        title: profileImportPage.pageTitle
        onBackClicked: root.goBack()
    }
}
