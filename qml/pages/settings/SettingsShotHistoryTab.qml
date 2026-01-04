import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: historyTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: Shot History stats
        Rectangle {
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(12)

                Tr {
                    key: "settings.history.title"
                    fallback: "Shot History"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.history.storedlocally"
                    fallback: "All shots are stored locally on your device"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                Item { height: 10 }

                // Stats
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(80)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(8)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(15)

                        ColumnLayout {
                            spacing: Theme.scaled(4)

                            Tr {
                                key: "settings.history.totalshots"
                                fallback: "Total Shots"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Text {
                                text: MainController.shotHistory ? MainController.shotHistory.totalShots : "0"
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(32)
                                font.bold: true
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                Item { Layout.fillHeight: true }
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
                            border.width: Theme.scaled(1)
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
                            border.width: Theme.scaled(1)

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
                                    border.width: Theme.scaled(1)

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
