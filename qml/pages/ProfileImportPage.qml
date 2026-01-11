import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: profileImportPage
    objectName: "profileImportPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("profileimport.title", "Import from Tablet")
        // Auto-scan when page opens
        MainController.profileImporter.scanProfiles()
    }
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("profileimport.title", "Import from Tablet")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin
        spacing: Theme.scaled(15)

        // Header section
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(80)
            color: Theme.surfaceColor
            radius: Theme.cardRadius

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

                StyledButton {
                    text: TranslationManager.translate("profileimport.button.rescan", "Rescan")
                    enabled: !MainController.profileImporter.isScanning
                    onClicked: MainController.profileImporter.scanProfiles()

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor
                        border.color: Theme.primaryColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.primaryColor
                        font: Theme.captionFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                    }
                }

                StyledButton {
                    text: TranslationManager.translate("profileimport.button.import_all", "Import All New")
                    visible: MainController.profileImporter.availableProfiles.length > 0
                    enabled: !MainController.profileImporter.isImporting && !MainController.profileImporter.isScanning
                    onClicked: MainController.profileImporter.importAllNew()

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.captionFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                    }
                }
            }
        }

        // Progress bar (during scanning/importing)
        ProgressBar {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(4)
            visible: MainController.profileImporter.isScanning || MainController.profileImporter.isImporting
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

        // Profile list
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

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
                        StyledButton {
                            visible: !profileDelegate.isIdentical
                            text: profileDelegate.isDifferent ?
                                  TranslationManager.translate("profileimport.button.update", "Update") :
                                  TranslationManager.translate("profileimport.button.import", "Import")
                            enabled: !MainController.profileImporter.isImporting
                            onClicked: {
                                MainController.profileImporter.importProfile(profileData.sourcePath)
                            }

                            background: Rectangle {
                                radius: Theme.scaled(4)
                                color: profileDelegate.isDifferent ? "#FFA500" : Theme.primaryColor
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "white"
                                font: Theme.captionFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(10)
                                rightPadding: Theme.scaled(10)
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

                StyledButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.overwrite", "Overwrite")
                    onClicked: {
                        MainController.profileImporter.saveOverwrite()
                        duplicateDialog.close()
                    }

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.errorColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                StyledButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.save_as_new", "Save as New")
                    onClicked: {
                        duplicateDialog.showNameInput = true
                    }

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                StyledButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.cancel", "Cancel")
                    onClicked: {
                        MainController.profileImporter.cancelImport()
                        duplicateDialog.close()
                    }

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Name input buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)
                visible: duplicateDialog.showNameInput

                StyledButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.save", "Save")
                    enabled: newNameInput.text.trim().length > 0
                    onClicked: {
                        MainController.profileImporter.saveWithNewName(newNameInput.text.trim())
                        duplicateDialog.close()
                    }

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                StyledButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileimport.button.back", "Back")
                    onClicked: {
                        duplicateDialog.showNameInput = false
                    }

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
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
            newNameInput.text = profileTitle + " (tablet)"
            duplicateDialog.open()
        }

        function onImportSuccess(profileTitle) {
            // Refresh the profile status in the list
            MainController.profileImporter.scanProfiles()
        }

        function onBatchImportComplete(imported, skipped, failed) {
            // Could show a summary dialog here if desired
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("profileimport.title", "Import from Tablet")
        onBackClicked: root.goBack()
    }
}
