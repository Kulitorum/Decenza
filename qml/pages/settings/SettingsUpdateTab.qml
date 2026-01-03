import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: updateTab

    RowLayout {
        anchors.fill: parent
        spacing: 15

        // Left column: Current version info
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 12

                Tr {
                    key: "settings.update.currentversion"
                    fallback: "Current Version"
                    color: Theme.textColor
                    font.pixelSize: 16
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: "Decenza DE1"
                    color: Theme.textSecondaryColor
                    font.pixelSize: 12
                }

                Item { height: 5 }

                Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    color: Theme.backgroundColor
                    radius: 8

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: MainController.updateChecker ? MainController.updateChecker.currentVersion : "Unknown"
                            color: Theme.primaryColor
                            font.pixelSize: 28
                            font.bold: true
                        }

                        Tr {
                            Layout.alignment: Qt.AlignHCenter
                            key: "settings.update.installed"
                            fallback: "Installed"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                        }
                    }
                }

                Item { height: 10 }

                // Auto-check toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        spacing: 2

                        Tr {
                            key: "settings.update.autocheck"
                            fallback: "Auto-check for updates"
                            color: Theme.textColor
                            font.pixelSize: 14
                        }

                        Tr {
                            key: "settings.update.checkeveryhour"
                            fallback: "Check every hour"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Switch {
                        checked: Settings.autoCheckUpdates
                        onToggled: Settings.autoCheckUpdates = checked
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Right column: Update status and actions
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 12

                Tr {
                    key: "settings.update.softwareupdates"
                    fallback: "Software Updates"
                    color: Theme.textColor
                    font.pixelSize: 16
                    font.bold: true
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.update.checkongithub"
                    fallback: "Check for new versions on GitHub"
                    color: Theme.textSecondaryColor
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Item { height: 5 }

                // Check button or status
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentColumn.height + 30
                    color: Theme.backgroundColor
                    radius: 8

                    ColumnLayout {
                        id: contentColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 15
                        spacing: 12

                        // Status row
                        RowLayout {
                            spacing: 10
                            visible: !MainController.updateChecker.checking && !MainController.updateChecker.downloading

                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: MainController.updateChecker.updateAvailable ? Theme.primaryColor : Theme.successColor
                            }

                            Text {
                                text: {
                                    if (MainController.updateChecker.updateAvailable) {
                                        return TranslationManager.translate("settings.update.updateavailable", "Update available:") + " v" + MainController.updateChecker.latestVersion
                                    } else if (MainController.updateChecker.latestVersion) {
                                        return TranslationManager.translate("settings.update.uptodate", "You're up to date")
                                    } else {
                                        return TranslationManager.translate("settings.update.checktostart", "Check for updates to get started")
                                    }
                                }
                                color: Theme.textColor
                                font.pixelSize: 14
                            }
                        }

                        // Checking indicator
                        RowLayout {
                            spacing: 10
                            visible: MainController.updateChecker.checking

                            BusyIndicator {
                                running: true
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                            }

                            Tr {
                                key: "settings.update.checking"
                                fallback: "Checking for updates..."
                                color: Theme.textColor
                                font.pixelSize: 14
                            }
                        }

                        // Download progress
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: MainController.updateChecker.downloading

                            Tr {
                                key: "settings.update.downloading"
                                fallback: "Downloading update..."
                                color: Theme.textColor
                                font.pixelSize: 14
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                value: MainController.updateChecker.downloadProgress / 100
                            }

                            Text {
                                text: MainController.updateChecker.downloadProgress + "%"
                                color: Theme.textSecondaryColor
                                font.pixelSize: 12
                            }
                        }

                        // Error message
                        Text {
                            Layout.fillWidth: true
                            visible: MainController.updateChecker.errorMessage !== ""
                            text: MainController.updateChecker.errorMessage
                            color: Theme.errorColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        // Action buttons
                        RowLayout {
                            spacing: 10
                            visible: !MainController.updateChecker.checking && !MainController.updateChecker.downloading

                            ActionButton {
                                translationKey: "settings.update.checknow"
                                translationFallback: "Check Now"
                                enabled: !MainController.updateChecker.checking
                                onClicked: MainController.updateChecker.checkForUpdates()
                            }

                            ActionButton {
                                translationKey: "settings.update.downloadinstall"
                                translationFallback: "Download & Install"
                                visible: MainController.updateChecker.updateAvailable
                                onClicked: MainController.updateChecker.downloadAndInstall()
                            }
                        }
                    }
                }

                // Release notes
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.backgroundColor
                    radius: 8
                    visible: MainController.updateChecker.releaseNotes !== ""

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 8

                        Tr {
                            key: "settings.update.releasenotes"
                            fallback: "Release Notes"
                            color: Theme.textSecondaryColor
                            font.pixelSize: 12
                            font.bold: true
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            TextArea {
                                readOnly: true
                                text: MainController.updateChecker.releaseNotes
                                color: Theme.textColor
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                background: null
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true; visible: MainController.updateChecker.releaseNotes === "" }
            }
        }
    }
}
