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

                Tr {
                    key: "settings.data.sharedesc"
                    fallback: "Enable the server to share your data with another device on your WiFi network."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item { height: Theme.scaled(5) }

                // Server toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.data.serverenabled"
                        fallback: "Data Server"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: MainController.shotServer.running
                        accessibleName: TranslationManager.translate("settings.data.serverenabled", "Data Server")
                        onToggled: {
                            if (checked) {
                                MainController.shotServer.start()
                            } else {
                                MainController.shotServer.stop()
                            }
                        }
                    }
                }

                // Server URL display
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(60)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)
                    visible: MainController.shotServer.running

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.data.serverurl"
                            fallback: "Server URL"
                            Layout.alignment: Qt.AlignHCenter
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: MainController.shotServer.url
                            color: Theme.accentColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }
                    }
                }

                // Not running message
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(60)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)
                    visible: !MainController.shotServer.running

                    Tr {
                        anchors.centerIn: parent
                        key: "settings.data.serveroff"
                        fallback: "Server is off"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
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

                // Server URL input
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: !MainController.dataMigration.isImporting

                    StyledTextField {
                        id: serverUrlInput
                        Layout.fillWidth: true
                        placeholderText: "192.168.1.100:8888"
                        text: MainController.dataMigration.serverUrl
                        enabled: !MainController.dataMigration.isConnecting && !MainController.dataMigration.isImporting
                    }

                    StyledButton {
                        text: MainController.dataMigration.manifest && Object.keys(MainController.dataMigration.manifest).length > 0 ?
                              TranslationManager.translate("settings.data.disconnect", "Disconnect") :
                              TranslationManager.translate("settings.data.connect", "Connect")
                        enabled: !MainController.dataMigration.isConnecting && !MainController.dataMigration.isImporting &&
                                 (serverUrlInput.text.trim().length > 0 || MainController.dataMigration.manifest)
                        onClicked: {
                            if (MainController.dataMigration.manifest && Object.keys(MainController.dataMigration.manifest).length > 0) {
                                MainController.dataMigration.disconnect()
                            } else {
                                MainController.dataMigration.connectToServer(serverUrlInput.text.trim())
                            }
                        }
                    }

                    StyledButton {
                        text: MainController.dataMigration.isSearching ?
                              TranslationManager.translate("settings.data.searching", "Searching...") :
                              TranslationManager.translate("settings.data.search", "Search")
                        enabled: !MainController.dataMigration.isConnecting && !MainController.dataMigration.isImporting &&
                                 !MainController.dataMigration.isSearching
                        onClicked: MainController.dataMigration.startDiscovery()
                    }
                }

                // Discovered devices list
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)
                    visible: MainController.dataMigration.discoveredDevices.length > 0 ||
                             MainController.dataMigration.isSearching

                    // Searching indicator
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: MainController.dataMigration.isSearching

                        BusyIndicator {
                            running: true
                            Layout.preferredWidth: Theme.scaled(16)
                            Layout.preferredHeight: Theme.scaled(16)
                        }

                        Text {
                            text: TranslationManager.translate("settings.data.searchingdevices", "Searching for Decenza devices...")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    // Device list
                    Repeater {
                        model: MainController.dataMigration.discoveredDevices

                        Rectangle {
                            Layout.fillWidth: true
                            height: deviceRow.height + Theme.scaled(16)
                            color: deviceMouseArea.containsMouse ? Theme.surfaceColor : Theme.backgroundColor
                            radius: Theme.scaled(6)
                            border.width: 1
                            border.color: Theme.borderColor

                            RowLayout {
                                id: deviceRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.scaled(8)
                                spacing: Theme.scaled(10)

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(2)

                                    Text {
                                        text: modelData.deviceName || "Unknown Device"
                                        color: Theme.textColor
                                        font.pixelSize: Theme.scaled(13)
                                        font.bold: true
                                    }

                                    Text {
                                        text: modelData.platform + " • v" + modelData.appVersion
                                        color: Theme.textSecondaryColor
                                        font.pixelSize: Theme.scaled(11)
                                    }
                                }

                                Text {
                                    text: modelData.ipAddress
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(11)
                                }
                            }

                            MouseArea {
                                id: deviceMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    MainController.dataMigration.connectToServer(modelData.serverUrl)
                                }
                            }
                        }
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
                            }
                        }

                        // Data available
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: Theme.scaled(4)
                            columnSpacing: Theme.scaled(15)

                            Tr {
                                key: "settings.data.profilecount"
                                fallback: "Profiles:"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }
                            Text {
                                text: MainController.dataMigration.manifest.profileCount || 0
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Tr {
                                key: "settings.data.shotcount"
                                fallback: "Shots:"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }
                            Text {
                                text: MainController.dataMigration.manifest.shotCount || 0
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Tr {
                                key: "settings.data.mediacount"
                                fallback: "Media files:"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }
                            Text {
                                text: MainController.dataMigration.manifest.mediaCount || 0
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Tr {
                                key: "settings.data.settings"
                                fallback: "Settings:"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }
                            Text {
                                text: MainController.dataMigration.manifest.hasSettings ?
                                      TranslationManager.translate("common.yes", "Yes") :
                                      TranslationManager.translate("common.no", "No")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(12)
                            }
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

                        StyledButton {
                            primary: true
                            text: TranslationManager.translate("settings.data.importall", "Import All")
                            visible: !MainController.dataMigration.isImporting
                            enabled: !MainController.dataMigration.isImporting
                            onClicked: MainController.dataMigration.importAll()
                        }

                        StyledButton {
                            text: TranslationManager.translate("common.cancel", "Cancel")
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

                        StyledButton {
                            text: TranslationManager.translate("settings.data.importsettings", "Import Settings")
                            enabled: MainController.dataMigration.manifest.hasSettings === true
                            onClicked: MainController.dataMigration.importOnlySettings()
                        }

                        StyledButton {
                            text: TranslationManager.translate("settings.data.importprofiles", "Import Profiles") +
                                  " (" + (MainController.dataMigration.manifest.profileCount || 0) + ")"
                            enabled: (MainController.dataMigration.manifest.profileCount || 0) > 0
                            onClicked: MainController.dataMigration.importOnlyProfiles()
                        }

                        StyledButton {
                            text: TranslationManager.translate("settings.data.importshots", "Import Shots") +
                                  " (" + (MainController.dataMigration.manifest.shotCount || 0) + ")"
                            enabled: (MainController.dataMigration.manifest.shotCount || 0) > 0
                            onClicked: MainController.dataMigration.importOnlyShots()
                        }

                        StyledButton {
                            text: TranslationManager.translate("settings.data.importmedia", "Import Media") +
                                  " (" + (MainController.dataMigration.manifest.mediaCount || 0) + ")"
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

            StyledButton {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("common.ok", "OK")
                onClicked: importCompletePopup.close()
            }
        }
    }
}
