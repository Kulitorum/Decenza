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

        // Left column: Simulation and Window Resolution
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Simulation section
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
                        key: "settings.debug.simulation"
                        fallback: "Simulation"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.debug.simulationDesc"
                        fallback: "Enable these options to test the app without hardware connected."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(20)

                        Tr {
                            key: "settings.debug.simulateMachine"
                            fallback: "Simulate machine connection"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: DE1Device.simulationMode
                            onToggled: {
                                DE1Device.simulationMode = checked
                                if (ScaleDevice) {
                                    ScaleDevice.simulationMode = checked
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(20)

                        Tr {
                            key: "settings.debug.headlessMode"
                            fallback: "Headless mode (no GHC)"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: DE1Device.isHeadless
                            onToggled: {
                                DE1Device.isHeadless = checked
                            }
                        }
                    }
                }
            }

            // Battery Drainer section (Android only)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(150)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: Qt.platform.os === "android"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "settings.debug.batteryDrainTest"
                        fallback: "Battery Drain Test"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.debug.batteryDrainTestDesc"
                        fallback: "Drains battery by maxing CPU, GPU, screen brightness and flashlight. Useful for testing smart charging."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(20)

                        Text {
                            text: BatteryDrainer.running ? "Draining... Tap overlay to stop" : "Battery: " + BatteryManager.batteryPercent + "%"
                            color: BatteryDrainer.running ? Theme.warningColor : Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: BatteryDrainer.running ? "Stop" : "Start Drain"
                            accessibleName: BatteryDrainer.running ? "Stop battery drain" : "Start battery drain test"
                            onClicked: {
                                if (BatteryDrainer.running) {
                                    BatteryDrainer.stop()
                                } else {
                                    BatteryDrainer.start()
                                }
                            }
                        }
                    }
                }
            }

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
                                border.width: Theme.scaled(1)
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
                        }
                    }
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
