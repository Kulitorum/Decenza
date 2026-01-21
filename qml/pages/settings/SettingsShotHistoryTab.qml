import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import DecenzaDE1
import "../../components"

Item {
    id: historyTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Shot History stats and import
        Rectangle {
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(12)
                spacing: Theme.scaled(6)

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.history.title", "Shot History") + " →"
                    accessibleName: "Open Shot History"
                    primary: true
                    onClicked: pageStack.push(Qt.resolvedUrl("../ShotHistoryPage.qml"))
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.history.storedlocally"
                    fallback: "All shots are stored locally on your device"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                }

                // Stats - single line
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Tr {
                        key: "settings.history.totalshots"
                        fallback: "Total Shots:"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Text {
                        text: MainController.shotHistory ? MainController.shotHistory.totalShots : "0"
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(12)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }
                }

                // Show on idle screen toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: "Show on Idle screen"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Layout.fillWidth: true
                    }

                    StyledSwitch {
                        checked: Settings.showHistoryButton
                        accessibleName: "Show on Idle screen"
                        onToggled: Settings.showHistoryButton = checked
                    }
                }

                // Auto-Favorites toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: TranslationManager.translate("settings.history.autofavorites", "Auto-Favorites")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(11)
                        }

                        Text {
                            text: TranslationManager.translate("settings.history.autofavoritesdesc",
                                  "Show button for recent bean+profile combinations")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(9)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: Settings.autoFavoritesEnabled
                        accessibleName: "Auto-Favorites"
                        onToggled: Settings.autoFavoritesEnabled = checked
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Import section
                Text {
                    text: "Import from DE1 App"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                }

                // Overwrite toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: "Overwrite existing"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Layout.fillWidth: true
                    }

                    StyledSwitch {
                        id: overwriteSwitch
                        checked: false
                        accessibleName: "Overwrite existing"
                    }
                }

                // Progress bar (visible during import)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)
                    visible: MainController.shotImporter && MainController.shotImporter.isImporting

                    Text {
                        text: MainController.shotImporter ? MainController.shotImporter.statusMessage : ""
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: MainController.shotImporter ? MainController.shotImporter.totalFiles : 1
                        value: MainController.shotImporter ? MainController.shotImporter.processedFiles : 0

                        background: Rectangle {
                            implicitHeight: Theme.scaled(6)
                            color: Theme.backgroundColor
                            radius: Theme.scaled(3)
                        }

                        contentItem: Item {
                            implicitHeight: Theme.scaled(6)
                            Rectangle {
                                width: parent.width * (MainController.shotImporter && MainController.shotImporter.totalFiles > 0 ?
                                       MainController.shotImporter.processedFiles / MainController.shotImporter.totalFiles : 0)
                                height: parent.height
                                radius: Theme.scaled(3)
                                color: Theme.primaryColor
                            }
                        }
                    }

                    AccessibleButton {
                        text: "Cancel"
                        accessibleName: "Cancel import"
                        Layout.alignment: Qt.AlignRight
                        onClicked: {
                            if (MainController.shotImporter) {
                                MainController.shotImporter.cancel()
                            }
                        }
                    }
                }

                // Import buttons (visible when not importing)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)
                    visible: !MainController.shotImporter || !MainController.shotImporter.isImporting

                    // DE1 App detection info
                    Text {
                        id: de1AppStatus
                        Layout.fillWidth: true
                        property string detectedPath: MainController.shotImporter ? MainController.shotImporter.detectDE1AppHistoryPath() : ""
                        text: detectedPath ? ("Found: " + detectedPath) : "DE1 app not found on device"
                        color: detectedPath ? Theme.successColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(9)
                        wrapMode: Text.Wrap
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: "Import from DE1 App"
                        accessibleName: "Auto-detect and import from DE1 tablet app"
                        visible: de1AppStatus.detectedPath !== ""
                        onClicked: {
                            if (MainController.shotImporter) {
                                MainController.shotImporter.importFromDE1App(overwriteSwitch.checked)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: "ZIP..."
                            accessibleName: "Import shot history from ZIP archive"
                            onClicked: {
                                shotZipDialog.overwrite = overwriteSwitch.checked
                                shotZipDialog.open()
                            }
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            text: "Folder..."
                            accessibleName: "Import shot history from folder"
                            onClicked: {
                                shotFolderDialog.overwrite = overwriteSwitch.checked
                                shotFolderDialog.open()
                            }
                        }
                    }
                }
            }
        }

        // File dialogs for shot import
        FileDialog {
            id: shotZipDialog
            title: "Select shot history ZIP archive"
            nameFilters: ["ZIP archives (*.zip)", "All files (*)"]
            property bool overwrite: false

            onAccepted: {
                if (MainController.shotImporter) {
                    MainController.shotImporter.importFromZip(selectedFile, overwrite)
                }
            }
        }

        FolderDialog {
            id: shotFolderDialog
            title: "Select folder containing .shot files"
            property bool overwrite: false

            onAccepted: {
                if (MainController.shotImporter) {
                    MainController.shotImporter.importFromDirectory(selectedFolder, overwrite)
                }
            }
        }

        // Extracting popup - shows during ZIP extraction
        Popup {
            id: extractingPopup
            modal: true
            dim: true
            closePolicy: Popup.NoAutoClose
            anchors.centerIn: Overlay.overlay
            padding: Theme.scaled(24)

            background: Rectangle {
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                border.width: 2
                border.color: Theme.primaryColor
            }

            contentItem: Column {
                spacing: Theme.spacingMedium
                width: Theme.scaled(250)

                Text {
                    text: "Extracting..."
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                BusyIndicator {
                    running: true
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.scaled(48)
                    height: Theme.scaled(48)
                }

                Text {
                    text: "Please wait while the archive is extracted"
                    wrapMode: Text.Wrap
                    width: parent.width
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Connections {
            target: MainController.shotImporter
            function onIsExtractingChanged() {
                if (MainController.shotImporter.isExtracting) {
                    extractingPopup.open()
                } else {
                    extractingPopup.close()
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

        // Shot import result handling
        Connections {
            target: MainController.shotImporter
            function onImportComplete(imported, skipped, failed) {
                importResultDialog.title = "Import Complete"
                importResultDialog.message = "Imported: " + imported + " shots\n" +
                                             "Skipped (duplicates): " + skipped + "\n" +
                                             "Failed: " + failed + "\n\n" +
                                             "Total shots: " + (MainController.shotHistory ? MainController.shotHistory.totalShots : "?")
                importResultDialog.isError = failed > 0 && imported === 0
                importResultDialog.open()
            }
            function onImportError(message) {
                importResultDialog.title = "Import Failed"
                importResultDialog.message = message
                importResultDialog.isError = true
                importResultDialog.open()
            }
        }

        // Right column: HTTP Server
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(12)

                Tr {
                    key: "settings.history.remoteaccess"
                    fallback: "Remote Access"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.history.enablehttpserver"
                    fallback: "Enable HTTP server to browse shots from any web browser on your network"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                Item { height: 5 }

                // Enable toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.history.enableserver"
                            fallback: "Enable Server"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            key: "settings.history.starthttpserver"
                            fallback: "Start HTTP server on this device"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        id: serverSwitch
                        checked: Settings.shotServerEnabled
                        accessibleName: TranslationManager.translate("settings.history.remoteAccess", "Remote Access")
                        onToggled: Settings.shotServerEnabled = checked
                    }
                }

                // Port setting
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    opacity: serverSwitch.checked ? 1.0 : 0.5

                    Tr {
                        key: "settings.history.port"
                        fallback: "Port"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    TextField {
                        id: portField
                        text: Settings.shotServerPort.toString()
                        inputMethodHints: Qt.ImhDigitsOnly
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        enabled: !serverSwitch.checked
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: Theme.scaled(80)

                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: portField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1
                        }

                        onEditingFinished: {
                            var port = parseInt(text)
                            if (port >= 1024 && port <= 65535) {
                                Settings.shotServerPort = port
                            } else {
                                text = Settings.shotServerPort.toString()
                            }
                        }
                    }
                }

                Item { height: 5 }

                // Server status box
                Rectangle {
                    Layout.fillWidth: true
                    height: statusContent.height + 30
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)
                    visible: serverSwitch.checked

                    ColumnLayout {
                        id: statusContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        RowLayout {
                            spacing: Theme.scaled(10)

                            Rectangle {
                                width: Theme.scaled(12)
                                height: Theme.scaled(12)
                                radius: Theme.scaled(6)
                                color: MainController.shotServer && MainController.shotServer.running ?
                                       Theme.successColor : Theme.errorColor
                            }

                            Text {
                                text: MainController.shotServer && MainController.shotServer.running ?
                                      TranslationManager.translate("settings.history.serverrunning", "Server Running") :
                                      TranslationManager.translate("settings.history.serverstopped", "Server Stopped")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }
                        }

                        // URL display
                        Rectangle {
                            visible: MainController.shotServer && MainController.shotServer.running
                            Layout.fillWidth: true
                            height: Theme.scaled(50)
                            color: Theme.surfaceColor
                            radius: Theme.scaled(4)
                            border.color: Theme.primaryColor
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(10)
                                spacing: Theme.scaled(10)

                                Text {
                                    Layout.fillWidth: true
                                    text: MainController.shotServer ? MainController.shotServer.url : ""
                                    color: Theme.primaryColor
                                    font.pixelSize: Theme.scaled(16)
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                }

                                Rectangle {
                                    width: Theme.scaled(60)
                                    height: Theme.scaled(30)
                                    radius: Theme.scaled(4)
                                    color: copyArea.pressed ? Theme.primaryColor : "transparent"
                                    border.color: Theme.primaryColor
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: copyFeedback.visible ? TranslationManager.translate("settings.history.copied", "Copied") : TranslationManager.translate("settings.history.copy", "Copy")
                                        color: copyArea.pressed ? "white" : Theme.primaryColor
                                        font.pixelSize: Theme.scaled(12)
                                    }

                                    MouseArea {
                                        id: copyArea
                                        anchors.fill: parent
                                        onClicked: {
                                            if (MainController.shotServer) {
                                                textHelper.text = MainController.shotServer.url
                                                textHelper.selectAll()
                                                textHelper.copy()
                                                copyFeedback.visible = true
                                                copyTimer.restart()
                                            }
                                        }
                                    }

                                    TextEdit {
                                        id: textHelper
                                        visible: false
                                    }

                                    Timer {
                                        id: copyTimer
                                        interval: 2000
                                        onTriggered: copyFeedback.visible = false
                                    }

                                    Rectangle {
                                        id: copyFeedback
                                        visible: false
                                    }
                                }
                            }
                        }

                        Tr {
                            visible: MainController.shotServer && MainController.shotServer.running
                            Layout.fillWidth: true
                            key: "settings.history.openurl"
                            fallback: "Open this URL in any browser on your network"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
