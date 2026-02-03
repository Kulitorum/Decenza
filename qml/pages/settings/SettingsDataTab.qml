import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: dataTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Server status (source device)
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
                    key: "settings.data.sharedata"
                    fallback: "Share Data"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                // Server status
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(50)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    Tr {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        key: MainController.shotServer.running ? "settings.data.serverready" : "settings.data.enableinhistory"
                        fallback: MainController.shotServer.running ? "Server is ready for connections." : "Enable 'Remote Access' in the Shot History tab to share data."
                        color: MainController.shotServer.running ? Theme.successColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }
                }

                Item { Layout.fillHeight: true }

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
                        text: MainController.availableProfiles.length
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }
            }
        }

        // Right column: Import from another device
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
                    key: "settings.data.importfrom"
                    fallback: "Import from Another Device"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                Tr {
                    key: "settings.data.importdesc"
                    fallback: "Connect to another Decenza device on your WiFi to import settings, profiles, and shot history."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item { height: Theme.scaled(5) }

                // Device discovery and selection
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: !MainController.dataMigration.isImporting &&
                             !(MainController.dataMigration.manifest && MainController.dataMigration.manifest.deviceName !== undefined)

                    // Search button and status
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        AccessibleButton {
                            text: MainController.dataMigration.isSearching ?
                                  TranslationManager.translate("settings.data.searching", "Searching...") :
                                  TranslationManager.translate("settings.data.searchdevices", "Search for Devices")
                            accessibleName: MainController.dataMigration.isSearching ?
                                  TranslationManager.translate("settings.data.searchingAccessible", "Searching for devices on your network") :
                                  TranslationManager.translate("settings.data.searchAccessible", "Search for other Decenza devices on your network")
                            primary: true
                            enabled: !MainController.dataMigration.isConnecting && !MainController.dataMigration.isImporting &&
                                     !MainController.dataMigration.isSearching
                            onClicked: MainController.dataMigration.startDiscovery()
                        }

                        BusyIndicator {
                            running: MainController.dataMigration.isSearching
                            visible: MainController.dataMigration.isSearching
                            Layout.preferredWidth: Theme.scaled(20)
                            Layout.preferredHeight: Theme.scaled(20)
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Single device - show as clickable card
                    Rectangle {
                        Layout.fillWidth: true
                        height: singleDeviceRow.height + Theme.scaled(16)
                        color: singleDeviceMouseArea.containsMouse ? Theme.surfaceColor : Theme.backgroundColor
                        radius: Theme.scaled(6)
                        border.width: 1
                        border.color: Theme.borderColor
                        visible: MainController.dataMigration.discoveredDevices.length === 1

                        RowLayout {
                            id: singleDeviceRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.scaled(8)
                            spacing: Theme.scaled(10)

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(2)

                                Text {
                                    text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                          (MainController.dataMigration.discoveredDevices[0].deviceName || "Unknown Device") : ""
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                    font.bold: true
                                }

                                Text {
                                    text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                          (MainController.dataMigration.discoveredDevices[0].platform + " • v" +
                                           MainController.dataMigration.discoveredDevices[0].appVersion) : ""
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(11)
                                }
                            }

                            Text {
                                text: MainController.dataMigration.discoveredDevices.length > 0 ?
                                      MainController.dataMigration.discoveredDevices[0].ipAddress : ""
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
                            }
                        }

                        MouseArea {
                            id: singleDeviceMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (MainController.dataMigration.discoveredDevices.length > 0) {
                                    MainController.dataMigration.connectToServer(
                                        MainController.dataMigration.discoveredDevices[0].serverUrl)
                                }
                            }
                        }
                    }

                    // Multiple devices - show combobox with connect button
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: MainController.dataMigration.discoveredDevices.length > 1

                        ComboBox {
                            id: deviceComboBox
                            Layout.fillWidth: true
                            model: MainController.dataMigration.discoveredDevices
                            textRole: "deviceName"
                            displayText: currentIndex >= 0 && model.length > 0 ?
                                         model[currentIndex].deviceName + " (" + model[currentIndex].ipAddress + ")" :
                                         TranslationManager.translate("settings.data.selectdevice", "Select a device")

                            delegate: ItemDelegate {
                                width: deviceComboBox.width
                                contentItem: ColumnLayout {
                                    spacing: 2
                                    Text {
                                        text: modelData.deviceName || "Unknown Device"
                                        color: Theme.textColor
                                        font.pixelSize: Theme.scaled(13)
                                    }
                                    Text {
                                        text: modelData.platform + " • " + modelData.ipAddress
                                        color: Theme.textSecondaryColor
                                        font.pixelSize: Theme.scaled(11)
                                    }
                                }
                                highlighted: deviceComboBox.highlightedIndex === index
                            }
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.data.connect", "Connect")
                            accessibleName: TranslationManager.translate("settings.data.connectAccessible", "Connect to selected device")
                            enabled: deviceComboBox.currentIndex >= 0
                            onClicked: {
                                var device = MainController.dataMigration.discoveredDevices[deviceComboBox.currentIndex]
                                MainController.dataMigration.connectToServer(device.serverUrl)
                            }
                        }
                    }

                    // No devices found message
                    Text {
                        visible: !MainController.dataMigration.isSearching &&
                                 MainController.dataMigration.discoveredDevices.length === 0 &&
                                 MainController.dataMigration.currentOperation === TranslationManager.translate("settings.data.nodevices", "No devices found")
                        text: TranslationManager.translate("settings.data.nodeviceshint", "Make sure the other device has the web server enabled in Settings.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Connection status
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: MainController.dataMigration.isConnecting

                    BusyIndicator {
                        running: true
                        Layout.preferredWidth: Theme.scaled(20)
                        Layout.preferredHeight: Theme.scaled(20)
                    }

                    Tr {
                        key: "settings.data.connecting"
                        fallback: "Connecting..."
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                    }
                }

                // Error message
                Text {
                    Layout.fillWidth: true
                    visible: MainController.dataMigration.errorMessage !== ""
                    text: MainController.dataMigration.errorMessage
                    color: Theme.errorColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                }

                // Manifest display (when connected)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: manifestColumn.height + Theme.scaled(20)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)
                    visible: MainController.dataMigration.manifest && MainController.dataMigration.manifest.deviceName !== undefined

                    ColumnLayout {
                        id: manifestColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(8)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Rectangle {
                                width: Theme.scaled(10)
                                height: Theme.scaled(10)
                                radius: Theme.scaled(5)
                                color: Theme.successColor
                            }

                            Text {
                                text: TranslationManager.translate("settings.data.connectedto", "Connected to:") + " " +
                                      (MainController.dataMigration.manifest.deviceName || "Unknown Device")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                                font.bold: true
                                Layout.fillWidth: true
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.data.disconnect", "Disconnect")
                                accessibleName: TranslationManager.translate("settings.data.disconnectAccessible", "Disconnect from remote device")
                                onClicked: MainController.dataMigration.disconnect()
                            }
                        }

                        // Data available - compact single line
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.data.profiles", "Profiles") + ": " +
                                  (MainController.dataMigration.manifest.profileCount || 0) + " • " +
                                  TranslationManager.translate("settings.data.shots", "Shots") + ": " +
                                  (MainController.dataMigration.manifest.shotCount || 0) + " • " +
                                  TranslationManager.translate("settings.data.media", "Media") + ": " +
                                  (MainController.dataMigration.manifest.mediaCount || 0) + " • " +
                                  TranslationManager.translate("settings.data.settings", "Settings") + ": " +
                                  (MainController.dataMigration.manifest.hasSettings ?
                                   TranslationManager.translate("common.yes", "Yes") :
                                   TranslationManager.translate("common.no", "No"))
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Import progress
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    visible: MainController.dataMigration.isImporting

                    RowLayout {
                        spacing: Theme.scaled(8)

                        Text {
                            text: MainController.dataMigration.currentOperation
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                        }

                        Text {
                            text: Math.round(MainController.dataMigration.progress * 100) + "%"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        value: MainController.dataMigration.progress
                    }
                }

                // Action buttons
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: MainController.dataMigration.manifest && MainController.dataMigration.manifest.deviceName !== undefined

                    // Import All button
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        AccessibleButton {
                            primary: true
                            text: TranslationManager.translate("settings.data.importall", "Import All")
                            accessibleName: TranslationManager.translate("settings.data.importAllAccessible", "Import all data from remote device including settings, profiles, shots, and media")
                            visible: !MainController.dataMigration.isImporting
                            enabled: !MainController.dataMigration.isImporting
                            onClicked: MainController.dataMigration.importAll()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("common.cancel", "Cancel")
                            accessibleName: TranslationManager.translate("settings.data.cancelImport", "Cancel import operation")
                            visible: MainController.dataMigration.isImporting
                            onClicked: MainController.dataMigration.cancel()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Individual import buttons
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: !MainController.dataMigration.isImporting

                        AccessibleButton {
                            text: TranslationManager.translate("settings.data.importsettings", "Import Settings")
                            accessibleName: TranslationManager.translate("settings.data.importSettingsAccessible", "Import only settings from remote device")
                            enabled: MainController.dataMigration.manifest.hasSettings === true
                            onClicked: MainController.dataMigration.importOnlySettings()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.data.importprofiles", "Import Profiles") +
                                  " (" + (MainController.dataMigration.manifest.profileCount || 0) + ")"
                            accessibleName: TranslationManager.translate("settings.data.importProfilesAccessible", "Import only profiles from remote device")
                            enabled: (MainController.dataMigration.manifest.profileCount || 0) > 0
                            onClicked: MainController.dataMigration.importOnlyProfiles()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.data.importshots", "Import Shots") +
                                  " (" + (MainController.dataMigration.manifest.shotCount || 0) + ")"
                            accessibleName: TranslationManager.translate("settings.data.importShotsAccessible", "Import only shot history from remote device")
                            enabled: (MainController.dataMigration.manifest.shotCount || 0) > 0
                            onClicked: MainController.dataMigration.importOnlyShots()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.data.importmedia", "Import Media") +
                                  " (" + (MainController.dataMigration.manifest.mediaCount || 0) + ")"
                            accessibleName: TranslationManager.translate("settings.data.importMediaAccessible", "Import only media files from remote device")
                            enabled: (MainController.dataMigration.manifest.mediaCount || 0) > 0
                            onClicked: MainController.dataMigration.importOnlyMedia()
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    // Import complete notification
    Connections {
        target: MainController.dataMigration

        function onImportComplete(settingsImported, profilesImported, shotsImported, mediaImported) {
            importCompletePopup.settingsCount = settingsImported
            importCompletePopup.profilesCount = profilesImported
            importCompletePopup.shotsCount = shotsImported
            importCompletePopup.mediaCount = mediaImported
            importCompletePopup.open()

            // Refresh profiles list
            MainController.refreshProfiles()
        }

        function onConnectionFailed(error) {
            // Error is already shown via errorMessage property
        }
    }

    // Import complete popup
    Popup {
        id: importCompletePopup
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.scaled(300)
        padding: Theme.scaled(20)

        property int settingsCount: 0
        property int profilesCount: 0
        property int shotsCount: 0
        property int mediaCount: 0

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

                    Text {
                        anchors.centerIn: parent
                        text: "\u2713"
                        color: "white"
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
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
            }

            AccessibleButton {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("common.ok", "OK")
                accessibleName: TranslationManager.translate("settings.data.closeImportDialog", "Close import complete dialog")
                onClicked: importCompletePopup.close()
            }
        }
    }
}
