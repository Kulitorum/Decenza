import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: updateTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Current version info
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
                    key: "settings.update.currentversion"
                    fallback: "Current Version"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(80)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Decenza DE1"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "v" + AppVersion
                            color: Theme.accentColor
                            font.pixelSize: Theme.scaled(22)
                            font.bold: true
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: DE1Device.simulationMode ? "SIMULATION MODE" : "Built with Qt 6"
                            color: DE1Device.simulationMode ? Theme.primaryColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            font.bold: DE1Device.simulationMode
                        }
                    }
                }

                Item { height: 5 }

                // Auto-check toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        spacing: Theme.scaled(1)

                        Tr {
                            key: "settings.update.autocheck"
                            fallback: "Auto-check for updates"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                        }

                        Tr {
                            key: "settings.update.checkeveryhour"
                            fallback: "Check every hour"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
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
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.update.softwareupdates"
                    fallback: "Software Updates"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                // Status area
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: statusColumn.height + 20
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    ColumnLayout {
                        id: statusColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(8)

                        // Status row
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: !MainController.updateChecker.checking && !MainController.updateChecker.downloading

                            Rectangle {
                                width: Theme.scaled(10)
                                height: Theme.scaled(10)
                                radius: Theme.scaled(5)
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
                                font.pixelSize: Theme.scaled(13)
                            }
                        }

                        // Checking indicator
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: MainController.updateChecker.checking

                            BusyIndicator {
                                running: true
                                Layout.preferredWidth: Theme.scaled(20)
                                Layout.preferredHeight: Theme.scaled(20)
                            }

                            Tr {
                                key: "settings.update.checking"
                                fallback: "Checking for updates..."
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                            }
                        }

                        // Download progress
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(6)
                            visible: MainController.updateChecker.downloading

                            RowLayout {
                                spacing: Theme.scaled(8)

                                Tr {
                                    key: "settings.update.downloading"
                                    fallback: "Downloading update..."
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                }

                                Text {
                                    text: MainController.updateChecker.downloadProgress + "%"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(12)
                                }
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                value: MainController.updateChecker.downloadProgress / 100
                            }
                        }

                        // Error message
                        Text {
                            Layout.fillWidth: true
                            visible: MainController.updateChecker.errorMessage !== ""
                            text: MainController.updateChecker.errorMessage
                            color: Theme.errorColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Action buttons row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: !MainController.updateChecker.checking && !MainController.updateChecker.downloading

                    Button {
                        text: TranslationManager.translate("settings.update.checknow", "Check Now")
                        enabled: !MainController.updateChecker.checking
                        onClicked: MainController.updateChecker.checkForUpdates()

                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: Theme.scaled(12)
                            color: parent.enabled ? Theme.textColor : Theme.textSecondaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: Theme.scaled(100)
                            implicitHeight: Theme.scaled(32)
                            color: parent.down ? Qt.darker(Theme.surfaceColor, 1.1) : Theme.surfaceColor
                            border.color: Theme.borderColor
                            border.width: Theme.scaled(1)
                            radius: Theme.scaled(6)
                        }
                    }

                    Button {
                        text: TranslationManager.translate("settings.update.downloadinstall", "Download & Install")
                        visible: MainController.updateChecker.updateAvailable
                        onClicked: MainController.updateChecker.downloadAndInstall()

                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: Theme.scaled(12)
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: Theme.scaled(120)
                            implicitHeight: Theme.scaled(32)
                            color: parent.down ? Qt.darker(Theme.primaryColor, 1.1) : Theme.primaryColor
                            radius: Theme.scaled(6)
                        }
                    }

                    Button {
                        text: TranslationManager.translate("settings.update.whatsnew", "What's New?")
                        visible: MainController.updateChecker.releaseNotes !== ""
                        onClicked: releaseNotesPopup.open()

                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: Theme.scaled(12)
                            color: Theme.primaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: Theme.scaled(100)
                            implicitHeight: Theme.scaled(32)
                            color: parent.down ? Qt.darker(Theme.surfaceColor, 1.1) : Theme.surfaceColor
                            border.color: Theme.primaryColor
                            border.width: Theme.scaled(1)
                            radius: Theme.scaled(6)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Release notes popup
    Popup {
        id: releaseNotesPopup
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Math.min(600, updateTab.width - 40)
        height: Math.min(400, updateTab.height - 40)
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: Theme.scaled(1)
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(0)

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: Theme.scaled(44)
                color: Theme.backgroundColor
                radius: Theme.cardRadius

                // Square off bottom corners
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.cardRadius
                    color: Theme.backgroundColor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.scaled(15)
                    anchors.rightMargin: Theme.scaled(10)

                    Text {
                        text: TranslationManager.translate("settings.update.whatsnew", "What's New?") +
                              (MainController.updateChecker.latestVersion ? " - v" + MainController.updateChecker.latestVersion : "")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "X"
                        onClicked: releaseNotesPopup.close()

                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.textSecondaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: Theme.scaled(28)
                            implicitHeight: Theme.scaled(28)
                            color: parent.hovered ? Theme.surfaceColor : "transparent"
                            radius: Theme.scaled(4)
                        }
                    }
                }
            }

            // Scrollable content
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.scaled(15)

                ScrollView {
                    id: notesScrollView
                    anchors.fill: parent
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    TextArea {
                        id: notesText
                        width: notesScrollView.width
                        readOnly: true
                        text: MainController.updateChecker.releaseNotes
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                        wrapMode: Text.WordWrap
                        background: null
                        selectByMouse: true
                    }
                }

                // Scroll indicator - shows when more content below
                Rectangle {
                    id: scrollIndicator
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.scaled(5)
                    width: Theme.scaled(28)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(14)
                    color: Theme.primaryColor
                    opacity: 0.9
                    visible: {
                        var scrollBar = notesScrollView.ScrollBar.vertical
                        return scrollBar && scrollBar.size < 1.0 && scrollBar.position + scrollBar.size < 0.95
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "↓"
                        color: "white"
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            // Scroll down a bit
                            var scrollBar = notesScrollView.ScrollBar.vertical
                            if (scrollBar) {
                                scrollBar.position = Math.min(1.0 - scrollBar.size, scrollBar.position + 0.2)
                            }
                        }
                    }
                }
            }
        }
    }
}
