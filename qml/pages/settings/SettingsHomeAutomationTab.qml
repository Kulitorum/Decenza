import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

KeyboardAwareContainer {
    id: homeAutomationTab
    textFields: [hostField, portField, usernameField, passwordField, baseTopicField]

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: MQTT Configuration
        Rectangle {
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                contentHeight: leftColumn.height
                clip: true

                ColumnLayout {
                    id: leftColumn
                    width: parent.width
                    spacing: Theme.scaled(10)

                    Text {
                        text: "MQTT"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    // Enable MQTT
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.scaled(5)

                        Text {
                            text: "Enable MQTT"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            checked: Settings.mqttEnabled
                            onCheckedChanged: Settings.mqttEnabled = checked
                        }
                    }

                    Text {
                        text: "Connect to an MQTT broker to publish telemetry and receive commands"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    // Broker Host
                    Text {
                        text: "Broker Host"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: hostField
                        Layout.fillWidth: true
                        text: Settings.mqttBrokerHost
                        onEditingFinished: Settings.mqttBrokerHost = text
                    }

                    // Port
                    Text {
                        text: "Port"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: portField
                        Layout.fillWidth: true
                        text: Settings.mqttBrokerPort
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: {
                            var port = parseInt(text)
                            if (!isNaN(port) && port > 0 && port <= 65535) {
                                Settings.mqttBrokerPort = port
                            }
                        }
                    }

                    // Username
                    Text {
                        text: "Username (optional)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: usernameField
                        Layout.fillWidth: true
                        text: Settings.mqttUsername
                        onEditingFinished: Settings.mqttUsername = text
                    }

                    // Password
                    Text {
                        text: "Password (optional)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: passwordField
                        Layout.fillWidth: true
                        text: Settings.mqttPassword
                        echoMode: TextInput.Password
                        onEditingFinished: Settings.mqttPassword = text
                    }

                    // Base Topic
                    Text {
                        text: "Base Topic"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: baseTopicField
                        Layout.fillWidth: true
                        text: Settings.mqttBaseTopic
                        onEditingFinished: Settings.mqttBaseTopic = text
                    }

                    // Connection status
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(40)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(8)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)

                            Rectangle {
                                width: Theme.scaled(10)
                                height: Theme.scaled(10)
                                radius: width / 2
                                color: MainController.mqttClient.connected ? Theme.successColor : Theme.textSecondaryColor
                            }

                            Text {
                                text: MainController.mqttClient.status
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(11)
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Connect/Disconnect buttons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        AccessibleButton {
                            text: "Connect"
                            accessibleName: TranslationManager.translate("settings.homeAutomation.connectMqtt", "Connect to MQTT broker for home automation")
                            primary: true
                            enabled: !MainController.mqttClient.connected && Settings.mqttBrokerHost.length > 0
                            onClicked: MainController.mqttClient.connectToBroker()
                        }

                        AccessibleButton {
                            text: "Disconnect"
                            accessibleName: TranslationManager.translate("settings.homeAutomation.disconnectMqtt", "Disconnect from MQTT broker")
                            enabled: MainController.mqttClient.connected
                            onClicked: MainController.mqttClient.disconnectFromBroker()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // Right column: Options and Info
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                contentHeight: rightColumn.height
                clip: true

                ColumnLayout {
                    id: rightColumn
                    width: parent.width
                    spacing: Theme.scaled(10)

                    Text {
                        text: "Publishing Options"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    // Publish Interval
                    Text {
                        text: "Publish Interval"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledComboBox {
                        id: intervalCombo
                        Layout.fillWidth: true
                        model: ["100 ms", "500 ms", "1 second", "5 seconds"]
                        accessibleLabel: TranslationManager.translate("settings.homeautomation.publishinterval", "Publish Interval")
                        currentIndex: {
                            var interval = Settings.mqttPublishInterval
                            if (interval <= 100) return 0
                            if (interval <= 500) return 1
                            if (interval <= 1000) return 2
                            return 3
                        }
                        onActivated: {
                            var intervals = [100, 500, 1000, 5000]
                            Settings.mqttPublishInterval = intervals[currentIndex]
                        }

                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: Theme.borderColor
                            border.width: 1
                        }

                        contentItem: Text {
                            text: intervalCombo.displayText
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(8)
                        }
                    }

                    // Retain Messages
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.scaled(5)

                        Text {
                            text: "Retain Messages"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            checked: Settings.mqttRetainMessages
                            onCheckedChanged: Settings.mqttRetainMessages = checked
                        }
                    }

                    Text {
                        text: "Broker retains last value for new subscribers"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                        Layout.topMargin: Theme.scaled(5)
                        Layout.bottomMargin: Theme.scaled(5)
                    }

                    Text {
                        text: "Home Assistant"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    // Home Assistant Discovery
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.scaled(5)

                        Text {
                            text: "Auto-Discovery"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            checked: Settings.mqttHomeAssistantDiscovery
                            onCheckedChanged: Settings.mqttHomeAssistantDiscovery = checked
                        }
                    }

                    Text {
                        text: "Automatically creates sensors and switches in Home Assistant"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    AccessibleButton {
                        text: "Publish Discovery Now"
                        accessibleName: TranslationManager.translate("settings.homeAutomation.publishDiscovery", "Publish Home Assistant discovery message")
                        primary: true
                        enabled: MainController.mqttClient.connected
                        onClicked: MainController.mqttClient.publishDiscovery()
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                        Layout.topMargin: Theme.scaled(5)
                        Layout.bottomMargin: Theme.scaled(5)
                    }

                    Text {
                        text: "REST API"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Text {
                        text: "Enable 'Remote Access' in the Shot History tab to use the REST API."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: apiColumn.height + Theme.scaled(16)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(8)
                        visible: MainController.shotServer.running

                        ColumnLayout {
                            id: apiColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.scaled(8)
                            spacing: Theme.scaled(4)

                            Text {
                                text: "Available Endpoints:"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(11)
                                font.bold: true
                            }

                            Text {
                                text: "GET /api/state - Machine state"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.family: "monospace"
                            }

                            Text {
                                text: "GET /api/telemetry - All sensor data"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.family: "monospace"
                            }

                            Text {
                                text: "POST /api/command - Send wake/sleep"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.family: "monospace"
                            }

                            Text {
                                text: MainController.shotServer.url
                                color: Theme.accentColor
                                font.pixelSize: Theme.scaled(10)
                                Layout.topMargin: Theme.scaled(4)
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
