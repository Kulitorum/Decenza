import QtQuick
import QtQuick.Layouts
import Decenza

KeyboardAwareContainer {
    id: homeAutomationTab
    enabled: pendingRequestId.length === 0
    textFields: [hostField, portField, usernameField, passwordField, passwordConfirmationField, baseTopicField, clientIdField]
    targetFlickable: mqttFlickable
    property string pendingRequestId: ""
    property string pendingOperation: ""
    property string operationError: ""

    function submitPreference(name: string, value: var, extraArguments: var): void {
        if (!MqttService.ready || pendingRequestId.length > 0) return
        operationError = ""
        pendingOperation = "setPreference"
        const args = { name: name, value: value }
        if (extraArguments) {
            for (const key in extraArguments)
                args[key] = extraArguments[key]
        }
        if (name === "mqttBrokerHost" || name === "mqttBrokerPort"
                || name === "mqttUseTls" || name === "mqttCaCertificate")
            args.passwordConfirmation = passwordConfirmationField.text
        pendingRequestId = MqttService.execute("setPreference", args, MqttService.revision)
    }

    function submitAction(operation: string): void {
        if (!MqttService.ready || pendingRequestId.length > 0) return
        operationError = ""
        pendingOperation = operation
        pendingRequestId = MqttService.execute(operation, {}, MqttService.revision)
    }

    Connections {
        target: MqttService
        function onOperationCompleted(requestId, operation, success, result, reason, revision) {
            if (requestId !== homeAutomationTab.pendingRequestId
                    || operation !== homeAutomationTab.pendingOperation) return
            homeAutomationTab.pendingRequestId = ""
            homeAutomationTab.pendingOperation = ""
            if (success) {
                if (passwordConfirmationField.text.length > 0)
                    passwordConfirmationField.clear()
                if (passwordField.text.length > 0)
                    passwordField.clear()
                homeAutomationTab.operationError = ""
            } else {
                homeAutomationTab.operationError = reason === "passwordReentryRequired"
                    ? TranslationManager.translate("settings.homeAutomation.passwordReentryRequired", "Re-enter the saved password to change broker, TLS, or CA settings.")
                    : reason === "invalidCertificate"
                        ? TranslationManager.translate("mqtt.caCertificateInvalid", "Not a PEM certificate, so it was not saved.")
                        : TranslationManager.translate("settings.homeAutomation.saveFailed", "The setting could not be saved. Review the value and try again.")
            }
        }
    }

    function value(name: string, fallback: var): var {
        return MqttService.snapshot[name] === undefined ? fallback : MqttService.snapshot[name]
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Left column: MQTT Configuration
        SettingsCard {
            searchId: "mqtt"
            title: TranslationManager.translate("mqtt.title", "MQTT")
            description: TranslationManager.translate("settings.search.mqttDesc", "Home automation broker connection")
            keywords: ["mqtt", "home", "assistant", "automation", "broker", "ha"]
            // The header scrolls with the content.
            showHeader: false
            fillContent: true
            Layout.fillWidth: false
            Layout.preferredWidth: Theme.scaled(300)

            Flickable {
                id: mqttFlickable
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                            checked: homeAutomationTab.value("mqttEnabled", false)
                            onToggled: homeAutomationTab.submitPreference("mqttEnabled", checked)
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

                    Text {
                        visible: homeAutomationTab.operationError.length > 0 || !MqttService.ready
                        text: homeAutomationTab.operationError.length > 0
                            ? homeAutomationTab.operationError
                            : TranslationManager.translate("settings.homeAutomation.unavailable", "Home automation settings are not available from the connected host.")
                        color: Theme.errorColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.AlertMessage
                        Accessible.name: text
                    }

                    Tr {
                        visible: homeAutomationTab.pendingRequestId.length > 0
                        key: "common.saving"
                        fallback: "Saving…"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
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
                        accessibleName: TranslationManager.translate("mqtt.brokerHost", "Broker Host")
                        Layout.fillWidth: true
                        text: homeAutomationTab.value("mqttBrokerHost", "")
                        onEditingFinished: homeAutomationTab.submitPreference("mqttBrokerHost", text.trim())
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
                        accessibleName: TranslationManager.translate("mqtt.port", "Port")
                        Layout.fillWidth: true
                        text: homeAutomationTab.value("mqttBrokerPort", 1883)
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: {
                            var port = parseInt(text)
                            if (!isNaN(port) && port > 0 && port <= 65535) {
                                homeAutomationTab.submitPreference("mqttBrokerPort", port)
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
                            checked: homeAutomationTab.value("mqttUseTls", false)
                            onToggled: {
                                // Follow the switch to the standard port, but leave a custom one alone.
                                const port = homeAutomationTab.value("mqttBrokerPort", 1883)
                                const recommendedPort = checked && port === 1883 ? 8883
                                    : !checked && port === 8883 ? 1883 : 0
                                homeAutomationTab.submitPreference("mqttUseTls", checked,
                                    recommendedPort > 0 ? { recommendedPort: recommendedPort } : {})
                            }
                        }
                    }

                    // CA certificate, for brokers with a self-signed certificate
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: homeAutomationTab.value("mqttUseTls", false)
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
                            text: ""
                            property bool dirty: false
                            onTextChanged: if (activeFocus) dirty = true
                            property bool rejected: false
                            onEditingFinished: {
                                const pem = text.trim()
                                rejected = false
                                if (dirty) {
                                    homeAutomationTab.submitPreference("mqttCaCertificate", pem)
                                    dirty = false
                                }
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
                                const summary = homeAutomationTab.value("mqttCaCertificateSummary", "")
                                return summary.length > 0
                                    ? TranslationManager.translate("mqtt.caCertificateLoaded", "Trusting: %1").arg(summary)
                                    : TranslationManager.translate("mqtt.caCertificateNone", "Using the system's trusted certificates.")
                            }
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        AccessibleButton {
                            visible: homeAutomationTab.value("mqttCaCertificateConfigured", false)
                            text: TranslationManager.translate("mqtt.caCertificateClear", "Clear certificate")
                            accessibleName: TranslationManager.translate("mqtt.caCertificateClearAccessible", "Clear the MQTT CA certificate")
                            onClicked: {
                                caField.rejected = false
                                caField.text = ""
                                homeAutomationTab.submitPreference("mqttCaCertificate", "")
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
                        accessibleName: TranslationManager.translate("mqtt.username", "Username (optional)")
                        Layout.fillWidth: true
                        text: homeAutomationTab.value("mqttUsername", "")
                        onEditingFinished: homeAutomationTab.submitPreference("mqttUsername", text)
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
                        accessibleName: TranslationManager.translate("mqtt.password", "Password (optional)")
                        Layout.fillWidth: true
                        text: ""
                        echoMode: TextInput.Password
                        onEditingFinished: if (text.length > 0) homeAutomationTab.submitPreference("mqttPassword", text)
                    }

                    Tr {
                        key: "settings.homeAutomation.passwordStatus"
                        fallback: homeAutomationTab.value("mqttPasswordConfigured", false) ? "A password is saved. Enter a new one to replace it." : "No password is saved."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        Layout.fillWidth: true
                    }

                    AccessibleButton {
                        visible: homeAutomationTab.value("mqttPasswordConfigured", false)
                        text: TranslationManager.translate("settings.homeAutomation.clearPassword", "Clear saved password")
                        accessibleName: TranslationManager.translate("settings.homeAutomation.clearPasswordAccessible", "Clear the saved MQTT password")
                        onClicked: homeAutomationTab.submitPreference("mqttPassword", "")
                    }

                    StyledTextField {
                        id: passwordConfirmationField
                        accessibleName: TranslationManager.translate("settings.homeAutomation.passwordConfirmation", "Re-enter saved password to change broker settings")
                        Layout.fillWidth: true
                        placeholderText: TranslationManager.translate("settings.homeAutomation.passwordConfirmationHint", "Re-enter saved password only when changing broker host, port, TLS or certificate")
                        echoMode: TextInput.Password
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
                        accessibleName: TranslationManager.translate("mqtt.baseTopic", "Base Topic")
                        Layout.fillWidth: true
                        text: homeAutomationTab.value("mqttBaseTopic", "")
                        onEditingFinished: homeAutomationTab.submitPreference("mqttBaseTopic", text)
                    }

                    // Client ID
                    Tr {
                        key: "mqtt.clientId"
                        fallback: "Client ID"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    StyledTextField {
                        id: clientIdField
                        Layout.fillWidth: true
                        text: homeAutomationTab.value("mqttClientId", "")
                        // Empty would only regenerate a random one on the next connect.
                        onEditingFinished: if (text.trim().length > 0) homeAutomationTab.submitPreference("mqttClientId", text.trim())
                        accessibleName: TranslationManager.translate("mqtt.clientId", "Client ID")
                    }

                    // Home Assistant device ID
                    Tr {
                        key: "mqtt.deviceId"
                        fallback: "Home Assistant device ID"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            Layout.fillWidth: true
                            text: homeAutomationTab.value("mqttDeviceId", "")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(11)
                            elide: Text.ElideMiddle
                            Accessible.role: Accessible.StaticText
                            Accessible.name: TranslationManager.translate("mqtt.deviceId", "Home Assistant device ID") + ": " + text
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("mqtt.newDeviceId", "New device ID")
                            accessibleName: TranslationManager.translate("mqtt.newDeviceIdAccessible", "Give this device a new Home Assistant device ID")
                            onClicked: homeAutomationTab.submitAction("newDeviceId")
                        }
                    }

                    Tr {
                        key: "mqtt.clientIdHint"
                        fallback: "The client ID must be unique on the broker. Use New device ID only on a second device restored from the same backup, so it shows up in Home Assistant as its own device."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(10)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
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
                                color: homeAutomationTab.value("connected", false) ? Theme.successColor : Theme.textSecondaryColor
                            }

                            Text {
                                text: homeAutomationTab.value("status", "Disconnected")
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
                            enabled: MqttService.ready && !homeAutomationTab.value("connected", false)
                                     && homeAutomationTab.value("mqttBrokerHost", "").length > 0
                            onClicked: homeAutomationTab.submitAction("connectToBroker")
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("mqtt.disconnect", "Disconnect")
                            accessibleName: TranslationManager.translate("settings.homeAutomation.disconnectMqtt", "Disconnect from MQTT broker")
                            // Also while connecting, retrying, or connected with a refused
                            // subscription — none of which reads as `connected`.
                            enabled: MqttService.ready && homeAutomationTab.value("status", "Disconnected") !== "Disconnected"
                                     && homeAutomationTab.value("status", "Disconnected") !== "Disabled"
                            onClicked: homeAutomationTab.submitAction("disconnectFromBroker")
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // Right column: Options and Info
        SettingsCard {
            searchId: "mqttPublishing"
            title: TranslationManager.translate("mqtt.publishingOptions", "Publishing Options")
            description: TranslationManager.translate("settings.search.mqttPublishingDesc", "What Decenza publishes to the MQTT broker, and how often")
            keywords: ["mqtt", "publish", "interval", "retain", "home assistant", "discovery", "topic"]
            // The header scrolls with the content.
            showHeader: false
            fillContent: true
            Layout.fillWidth: true

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                            var interval = homeAutomationTab.value("mqttPublishInterval", 1000)
                            if (interval <= 100) return 0
                            if (interval <= 500) return 1
                            if (interval <= 1000) return 2
                            return 3
                        }
                        onActivated: {
                            var intervals = [100, 500, 1000, 5000]
                            homeAutomationTab.submitPreference("mqttPublishInterval", intervals[currentIndex])
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
                            checked: homeAutomationTab.value("mqttRetainMessages", false)
                            onToggled: homeAutomationTab.submitPreference("mqttRetainMessages", checked)
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
                            checked: homeAutomationTab.value("mqttHomeAssistantDiscovery", false)
                            onToggled: homeAutomationTab.submitPreference("mqttHomeAssistantDiscovery", checked)
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
                        enabled: homeAutomationTab.value("connected", false)
                        onClicked: homeAutomationTab.submitAction("publishDiscovery")
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
                        visible: homeAutomationTab.value("shotServerRunning", false)

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
                                text: homeAutomationTab.value("shotServerUrl", "")
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
