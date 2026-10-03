import QtQuick
import QtQuick.Layouts
import Decenza

KeyboardAwareContainer {
    id: homeAutomationTab
    textFields: [hostField, portField, usernameField, passwordField, baseTopicField]
    targetFlickable: mqttFlickable

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: MQTT Configuration
        Rectangle {
            objectName: "mqtt"
            Layout.preferredWidth: Theme.scaled(300)
            Layout.fillHeight: true
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius

            Flickable {
                id: mqttFlickable
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                contentHeight: leftColumn.height
                clip: true

                ColumnLayout {
                    id: leftColumn
                    width: parent.width
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "mqtt.title"
                        fallback: "MQTT"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    // Enable MQTT
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.scaled(5)

                        Tr {
                            key: "mqtt.enableMqtt"
                            fallback: "Enable MQTT"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            accessibleName: TranslationManager.translate("mqtt.enableMqtt", "Enable MQTT")
                            checked: Settings.mqtt.mqttEnabled
                            onCheckedChanged: Settings.mqtt.mqttEnabled = checked
                        }
                    }

                    Tr {
                        key: "mqtt.description"
                        fallback: "Connect to an MQTT broker to publish telemetry and receive commands"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderColor
                    }

                    // Broker Host
                    Tr {
                        key: "mqtt.brokerHost"
                        fallback: "Broker Host"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: hostField
                        Layout.fillWidth: true
                        text: Settings.mqtt.mqttBrokerHost
                        onEditingFinished: Settings.mqtt.mqttBrokerHost = text
                    }

                    // Port
                    Tr {
                        key: "mqtt.port"
                        fallback: "Port"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: portField
                        Layout.fillWidth: true
                        text: Settings.mqtt.mqttBrokerPort
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: {
                            var port = parseInt(text)
                            if (!isNaN(port) && port > 0 && port <= 65535) {
                                Settings.mqtt.mqttBrokerPort = port
                            }
                        }
                    }

                    // Encrypted connection (TLS)
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.scaled(5)

                        Tr {
                            key: "mqtt.useTls"
                            fallback: "Encrypted connection (TLS)"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            accessibleName: TranslationManager.translate("mqtt.useTls", "Encrypted connection (TLS)")
                            checked: Settings.mqtt.mqttUseTls
                            onToggled: {
                                // Follow the switch to the standard port, but leave a custom one alone.
                                if (checked && Settings.mqtt.mqttBrokerPort === 1883)
                                    Settings.mqtt.mqttBrokerPort = 8883
                                else if (!checked && Settings.mqtt.mqttBrokerPort === 8883)
                                    Settings.mqtt.mqttBrokerPort = 1883
                                Settings.mqtt.mqttUseTls = checked
                            }
                        }
                    }

                    // CA certificate, for brokers with a self-signed certificate
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: Settings.mqtt.mqttUseTls
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "mqtt.caCertificate"
                            fallback: "CA certificate (optional)"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }

                        Tr {
                            key: "mqtt.caCertificateHint"
                            fallback: "Only needed if your broker uses a self-signed certificate. Paste the CA certificate (PEM)."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        ExpandableTextArea {
                            id: caField
                            Layout.fillWidth: true
                            accessibleName: TranslationManager.translate("mqtt.caCertificate", "CA certificate (optional)")
                            placeholderText: "-----BEGIN CERTIFICATE-----"
                            text: Settings.mqtt.mqttCaCertificate
                            property bool rejected: false
                            onEditingFinished: {
                                const pem = text.trim()
                                rejected = pem.length > 0 && MainController.mqttClient.describeCaCertificate(pem).length === 0
                                if (!rejected)
                                    Settings.mqtt.mqttCaCertificate = pem
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.scaled(10)
                            color: caField.rejected ? Theme.errorColor : Theme.textSecondaryColor
                            text: {
                                if (caField.rejected)
                                    return TranslationManager.translate("mqtt.caCertificateInvalid", "Not a PEM certificate, so it was not saved.")
                                const summary = MainController.mqttClient.describeCaCertificate(Settings.mqtt.mqttCaCertificate)
                                return summary.length > 0
                                    ? TranslationManager.translate("mqtt.caCertificateLoaded", "Trusting: %1").arg(summary)
                                    : TranslationManager.translate("mqtt.caCertificateNone", "Using the system's trusted certificates.")
                            }
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        AccessibleButton {
                            visible: Settings.mqtt.mqttCaCertificate.length > 0
                            text: TranslationManager.translate("mqtt.caCertificateClear", "Clear certificate")
                            accessibleName: TranslationManager.translate("mqtt.caCertificateClearAccessible", "Clear the MQTT CA certificate")
                            onClicked: {
                                caField.rejected = false
                                caField.text = ""   // its text binding is gone once the user has typed
                                Settings.mqtt.mqttCaCertificate = ""
                            }
                        }
                    }

                    // Username
                    Tr {
                        key: "mqtt.username"
                        fallback: "Username (optional)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: usernameField
                        Layout.fillWidth: true
                        text: Settings.mqtt.mqttUsername
                        onEditingFinished: Settings.mqtt.mqttUsername = text
                    }

                    // Password
                    Tr {
                        key: "mqtt.password"
                        fallback: "Password (optional)"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: passwordField
                        Layout.fillWidth: true
                        text: Settings.mqtt.mqttPassword
                        echoMode: TextInput.Password
                        onEditingFinished: Settings.mqtt.mqttPassword = text
                    }

                    // Base Topic
                    Tr {
                        key: "mqtt.baseTopic"
                        fallback: "Base Topic"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: baseTopicField
                        Layout.fillWidth: true
                        text: Settings.mqtt.mqttBaseTopic
                        onEditingFinished: Settings.mqtt.mqttBaseTopic = text
                    }

                    // Connection status
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        color: Theme.insetBackgroundColor
                        radius: Theme.scaled(8)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)

                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(10)
                                Layout.preferredHeight: Theme.scaled(10)
                                radius: width / 2
                                color: MainController.mqttClient.connected ? Theme.successColor : Theme.textSecondaryColor
                            }

                            Text {
                                text: MainController.mqttClient.status
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(11)
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }
                        }
                    }

                    // Connect/Disconnect buttons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        AccessibleButton {
                            text: TranslationManager.translate("mqtt.connect", "Connect")
                            accessibleName: TranslationManager.translate("settings.homeAutomation.connectMqtt", "Connect to MQTT broker for home automation")
                            primary: true
                            enabled: !MainController.mqttClient.connected && Settings.mqtt.mqttBrokerHost.length > 0
                            onClicked: MainController.mqttClient.connectToBroker()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("mqtt.disconnect", "Disconnect")
                            accessibleName: TranslationManager.translate("settings.homeAutomation.disconnectMqtt", "Disconnect from MQTT broker")
                            // Also while connecting, retrying, or connected with a refused
                            // subscription — none of which reads as `connected`.
                            enabled: MainController.mqttClient.status !== "Disconnected"
                                     && MainController.mqttClient.status !== "Disabled"
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
            color: Theme.cardBackgroundColor
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

                    Tr {
                        key: "mqtt.publishingOptions"
                        fallback: "Publishing Options"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    // Publish Interval
                    Tr {
                        key: "mqtt.publishInterval"
                        fallback: "Publish Interval"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledComboBox {
                        id: intervalCombo
                        Layout.fillWidth: true
                        model: ["100 ms", "500 ms", "1 second", "5 seconds"]
                        accessibleLabel: TranslationManager.translate("settings.homeautomation.publishinterval", "Publish Interval")
                        currentIndex: {
                            var interval = Settings.mqtt.mqttPublishInterval
                            if (interval <= 100) return 0
                            if (interval <= 500) return 1
                            if (interval <= 1000) return 2
                            return 3
                        }
                        onActivated: {
                            var intervals = [100, 500, 1000, 5000]
                            Settings.mqtt.mqttPublishInterval = intervals[currentIndex]
                        }

                        background: Rectangle {
                            color: Theme.insetBackgroundColor
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

                        Tr {
                            key: "mqtt.retainMessages"
                            fallback: "Retain Messages"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            accessibleName: TranslationManager.translate("mqtt.retainMessages", "Retain Messages")
                            checked: Settings.mqtt.mqttRetainMessages
                            onCheckedChanged: Settings.mqtt.mqttRetainMessages = checked
                        }
                    }

                    Tr {
                        key: "mqtt.retainDescription"
                        fallback: "Broker retains last value for new subscribers"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderColor
                        Layout.topMargin: Theme.scaled(5)
                        Layout.bottomMargin: Theme.scaled(5)
                    }

                    Tr {
                        key: "mqtt.homeAssistant"
                        fallback: "Home Assistant"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    // Home Assistant Discovery
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.scaled(5)

                        Tr {
                            key: "mqtt.autoDiscovery"
                            fallback: "Auto-Discovery"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            Layout.fillWidth: true
                        }

                        StyledSwitch {
                            accessibleName: TranslationManager.translate("mqtt.autoDiscovery", "Auto-Discovery")
                            checked: Settings.mqtt.mqttHomeAssistantDiscovery
                            onCheckedChanged: Settings.mqtt.mqttHomeAssistantDiscovery = checked
                        }
                    }

                    Tr {
                        key: "mqtt.autoDiscoveryDescription"
                        fallback: "Automatically creates sensors and switches in Home Assistant"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("mqtt.publishDiscoveryNow", "Publish Discovery Now")
                        accessibleName: TranslationManager.translate("settings.homeAutomation.publishDiscovery", "Publish Home Assistant discovery message")
                        primary: true
                        enabled: MainController.mqttClient.connected
                        onClicked: MainController.mqttClient.publishDiscovery()
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.borderColor
                        Layout.topMargin: Theme.scaled(5)
                        Layout.bottomMargin: Theme.scaled(5)
                    }

                    Tr {
                        key: "mqtt.restApi"
                        fallback: "REST API"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Tr {
                        key: "mqtt.restApiDescription"
                        fallback: "Enable 'Remote Access' in the History & Data tab to use the REST API."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: apiColumn.height + Theme.scaled(16)
                        color: Theme.insetBackgroundColor
                        radius: Theme.scaled(8)
                        visible: MainController.shotServer.running

                        ColumnLayout {
                            id: apiColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.scaled(8)
                            spacing: Theme.scaled(4)

                            Tr {
                                key: "mqtt.availableEndpoints"
                                fallback: "Available Endpoints:"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(11)
                                font.bold: true
                            }

                            Text {
                                text: TranslationManager.translate("settings.homeautomation.apiState", "GET /api/state - Machine state")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.family: Theme.monoFontFamily
                            }

                            Text {
                                text: TranslationManager.translate("settings.homeautomation.apiTelemetry", "GET /api/telemetry - All sensor data")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.family: Theme.monoFontFamily
                            }

                            Text {
                                text: TranslationManager.translate("settings.homeautomation.apiCommand", "POST /api/command - Send wake/sleep")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.family: Theme.monoFontFamily
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
