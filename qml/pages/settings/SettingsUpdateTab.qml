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

                Text {
                    text: "Current Version"
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

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Installed"
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

                        Text {
                            text: "Auto-check for updates"
                            color: Theme.textColor
                            font.pixelSize: 14
                        }

                        Text {
                            text: "Check every hour"
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

                Text {
                    text: "Software Updates"
                    color: Theme.textColor
                    font.pixelSize: 16
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: "Check for new versions on GitHub"
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
                                        return "Update available: v" + MainController.updateChecker.latestVersion
                                    } else if (MainController.updateChecker.latestVersion) {
                                        return "You're up to date"
                                    } else {
                                        return "Check for updates to get started"
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

                            Text {
                                text: "Checking for updates..."
                                color: Theme.textColor
                                font.pixelSize: 14
                            }
                        }

                        // Download progress
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: MainController.updateChecker.downloading

                            Text {
                                text: "Downloading update..."
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
                                text: "Check Now"
                                enabled: !MainController.updateChecker.checking
                                onClicked: MainController.updateChecker.checkForUpdates()
                            }

                            ActionButton {
                                text: "Download & Install"
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

                        Text {
                            text: "Release Notes"
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
