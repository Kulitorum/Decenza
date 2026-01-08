import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import DecenzaDE1
import "../../components"

Item {
    id: debugTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Window Resolution
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Window Resolution section (Windows/desktop only)
            Rectangle {
                id: resolutionSection
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(120)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: Qt.platform.os === "windows"

                // Resolution presets (Decent tablet first as default, landscape only)
                property var resolutions: [
                    { name: "Decent Tablet", width: 1200, height: 800 },
                    { name: "Tablet 7\"", width: 1024, height: 600 },
                    { name: "Tablet 10\"", width: 1280, height: 800 },
                    { name: "iPad 10.2\"", width: 1080, height: 810 },
                    { name: "iPad Pro 11\"", width: 1194, height: 834 },
                    { name: "iPad Pro 12.9\"", width: 1366, height: 1024 },
                    { name: "Desktop HD", width: 1280, height: 720 },
                    { name: "Desktop Full HD", width: 1920, height: 1080 }
                ]

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "settings.debug.windowResolution"
                        fallback: "Window Resolution"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(20)

                        Tr {
                            key: "settings.debug.resizeWindow"
                            fallback: "Resize window to test UI scaling"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Item { Layout.fillWidth: true }

                        StyledComboBox {
                            id: resolutionCombo
                            Layout.preferredWidth: Theme.scaled(200)
                            model: resolutionSection.resolutions
                            textRole: "name"
                            displayText: Window.window ? (Window.window.width + " x " + Window.window.height) : "Select..."

                            delegate: ItemDelegate {
                                width: resolutionCombo.width
                                contentItem: Text {
                                    text: modelData.name + " (" + modelData.width + "x" + modelData.height + ")"
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                    verticalAlignment: Text.AlignVCenter
                                }
                                highlighted: resolutionCombo.highlightedIndex === index
                                background: Rectangle {
                                    color: highlighted ? Theme.accentColor : Theme.surfaceColor
                                }
                            }

                            background: Rectangle {
                                color: Theme.backgroundColor
                                border.color: Theme.textSecondaryColor
                                border.width: 1
                                radius: Theme.scaled(4)
                            }

                            contentItem: Text {
                                text: resolutionCombo.displayText
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(8)
                            }

                            onActivated: function(index) {
                                var res = resolutionSection.resolutions[index]
                                if (Window.window && res) {
                                    Window.window.width = res.width
                                    Window.window.height = res.height
                                }
                            }
                        }
                    }
                }
            }

            // Simulation toggles
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: simTogglesContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: simTogglesContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.debug.simulationToggles"
                        fallback: "Simulation Toggles"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(20)

                        Text {
                            text: "Headless machine"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: DE1Device.isHeadless
                            onToggled: DE1Device.setIsHeadless(checked)
                        }
                    }
                }
            }

            // Profile Converter section
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: profileConverterContent.implicitHeight + Theme.scaled(30)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: profileConverterContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(10)

                    Text {
                        text: "Profile Converter"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Convert DE1 app TCL profiles to native JSON format. Preserves all fields including popup messages, per-frame weight, etc."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }

                    // Progress indicator when converting
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(5)
                        visible: MainController.profileConverter && MainController.profileConverter.isConverting

                        Text {
                            text: MainController.profileConverter ? MainController.profileConverter.statusMessage : ""
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: MainController.profileConverter ? MainController.profileConverter.totalFiles : 1
                            value: MainController.profileConverter ? MainController.profileConverter.processedFiles : 0
                        }

                        Text {
                            text: MainController.profileConverter ?
                                  "Converting: " + MainController.profileConverter.currentFile : ""
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        property string de1AppPath: MainController.profileConverter ?
                                                    MainController.profileConverter.detectDE1AppProfilesPath() : ""

                        Text {
                            text: parent.de1AppPath ? "DE1 app found" : "DE1 app not found"
                            color: parent.de1AppPath ? Theme.primaryColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "Overwrite"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        StyledSwitch {
                            id: overwriteSwitch
                            checked: false
                        }

                        AccessibleButton {
                            text: "Convert Profiles"
                            accessibleName: "Convert DE1 app profiles to native format"
                            enabled: MainController.profileConverter &&
                                     !MainController.profileConverter.isConverting &&
                                     parent.de1AppPath !== ""
                            onClicked: {
                                var sourcePath = parent.de1AppPath
                                // For development, use the source directory
                                var destPath = "C:/CODE/de1-qt/resources/profiles"
                                MainController.profileConverter.convertProfiles(sourcePath, destPath, overwriteSwitch.checked)
                            }
                        }
                    }
                }
            }

            // Connections for profile converter
            Connections {
                target: MainController.profileConverter
                function onConversionComplete(success, errors) {
                    var skipped = MainController.profileConverter.skippedCount
                    profileConvertResultDialog.title = errors > 0 ? "Conversion Complete (with errors)" : "Conversion Complete"
                    var msg = "Successfully converted: " + success + " profiles"
                    if (skipped > 0) {
                        msg += "\nSkipped (already exist): " + skipped
                    }
                    if (errors > 0) {
                        msg += "\nErrors: " + errors
                    }
                    profileConvertResultDialog.message = msg
                    profileConvertResultDialog.isError = errors > 0
                    profileConvertResultDialog.open()
                }
                function onConversionError(message) {
                    profileConvertResultDialog.title = "Conversion Failed"
                    profileConvertResultDialog.message = message
                    profileConvertResultDialog.isError = true
                    profileConvertResultDialog.open()
                }
            }

            // Profile conversion result dialog
            Popup {
                id: profileConvertResultDialog
                modal: true
                dim: true
                anchors.centerIn: Overlay.overlay
                padding: Theme.scaled(24)

                property string title: ""
                property string message: ""
                property bool isError: false

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 2
                    border.color: profileConvertResultDialog.isError ? Theme.dangerColor : Theme.primaryColor
                }

                contentItem: Column {
                    spacing: Theme.spacingMedium
                    width: Theme.scaled(300)

                    Text {
                        text: profileConvertResultDialog.title
                        font: Theme.subtitleFont
                        color: profileConvertResultDialog.isError ? Theme.dangerColor : Theme.textColor
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: profileConvertResultDialog.message
                        wrapMode: Text.Wrap
                        width: parent.width
                        font: Theme.bodyFont
                        color: Theme.textColor
                    }

                    AccessibleButton {
                        text: "OK"
                        accessibleName: "Dismiss dialog"
                        anchors.horizontalCenter: parent.horizontalCenter
                        onClicked: profileConvertResultDialog.close()
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }
        }

        // Right column: Shot Database and Translation
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Database Import section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(160)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "settings.debug.shotDatabase"
                        fallback: "Shot Database"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.debug.shotDatabaseDesc"
                        fallback: "Import shots from another device. Merge adds new shots, Replace overwrites all data."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            text: TranslationManager.translate("settings.debug.currentShots", "Current shots:") + " " + (MainController.shotHistory ? MainController.shotHistory.totalShots : 0)
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.debug.merge", "Merge...")
                            accessibleName: "Import and merge database"
                            onClicked: {
                                importDialog.mergeMode = true
                                importDialog.open()
                            }
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.debug.replace", "Replace...")
                            accessibleName: "Import and replace database"
                            onClicked: {
                                importDialog.mergeMode = false
                                importDialog.open()
                            }
                        }
                    }
                }
            }

            FileDialog {
                id: importDialog
                title: mergeMode ? TranslationManager.translate("settings.debug.selectMerge", "Select database to merge") : TranslationManager.translate("settings.debug.selectReplace", "Select database to replace with")
                nameFilters: ["SQLite databases (*.db)", "All files (*)"]
                property bool mergeMode: true

                onAccepted: {
                    if (MainController.shotHistory) {
                        var success = MainController.shotHistory.importDatabase(selectedFile, mergeMode)
                        if (success) {
                            console.log("Database import successful")
                            importResultDialog.title = "Import Successful"
                            importResultDialog.message = "Database imported successfully.\nTotal shots: " + MainController.shotHistory.totalShots
                            importResultDialog.isError = false
                            importResultDialog.open()
                        }
                        // Errors are handled via errorOccurred signal
                    }
                }
            }

            // Import result feedback dialog
            Popup {
                id: importResultDialog
                modal: true
                dim: true
                anchors.centerIn: Overlay.overlay
                padding: Theme.scaled(24)

                property string title: ""
                property string message: ""
                property bool isError: false

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 2
                    border.color: importResultDialog.isError ? Theme.dangerColor : Theme.primaryColor
                }

                contentItem: Column {
                    spacing: Theme.spacingMedium
                    width: Theme.scaled(300)

                    Text {
                        text: importResultDialog.title
                        font: Theme.subtitleFont
                        color: importResultDialog.isError ? Theme.dangerColor : Theme.textColor
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: importResultDialog.message
                        wrapMode: Text.Wrap
                        width: parent.width
                        font: Theme.bodyFont
                        color: Theme.textColor
                    }

                    AccessibleButton {
                        text: "OK"
                        accessibleName: "Dismiss dialog"
                        anchors.horizontalCenter: parent.horizontalCenter
                        onClicked: importResultDialog.close()
                    }
                }
            }

            // Handle import errors
            Connections {
                target: MainController.shotHistory
                function onErrorOccurred(message) {
                    importResultDialog.title = "Import Failed"
                    importResultDialog.message = message
                    importResultDialog.isError = true
                    importResultDialog.open()
                }
            }

            // Translation Developer Tools section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(180)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "settings.debug.translationTools"
                        fallback: "Translation Developer Tools"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.debug.translationToolsDesc"
                        fallback: "Tools for managing community translations. Enable upload to allow submitting translations from the Language settings."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(20)

                        Tr {
                            key: "settings.debug.enableUpload"
                            fallback: "Enable translation upload"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: Settings.developerTranslationUpload
                            onToggled: Settings.developerTranslationUpload = checked
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(15)

                        Text {
                            text: TranslationManager.autoTranslating ?
                                  TranslationManager.translate("settings.debug.translating", "Translating...") :
                                  (TranslationManager.uploading ?
                                   TranslationManager.translate("settings.debug.uploading", "Uploading...") :
                                   TranslationManager.translate("settings.debug.batchProcess", "Batch process all languages"))
                            color: TranslationManager.autoTranslating || TranslationManager.uploading ? Theme.primaryColor : Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: TranslationManager.autoTranslating ?
                                  TranslationManager.translate("settings.debug.cancel", "Cancel") :
                                  TranslationManager.translate("settings.debug.translateUploadAll", "Translate & Upload All")
                            accessibleName: "Translate and upload all languages"
                            enabled: !TranslationManager.uploading
                            onClicked: {
                                if (TranslationManager.autoTranslating) {
                                    TranslationManager.cancelAutoTranslate()
                                } else {
                                    TranslationManager.translateAndUploadAllLanguages()
                                }
                            }
                        }
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }
        }
    }
}
