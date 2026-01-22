import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

KeyboardAwareContainer {
    id: aiTab
    textFields: [apiKeyField, ollamaEndpointField, openrouterModelField]

    property string testResultMessage: ""
    property bool testResultSuccess: false

    // Helper function to check if provider has a key configured
    function isProviderConfigured(providerId) {
        switch(providerId) {
            case "openai": return Settings.openaiApiKey.length > 0
            case "anthropic": return Settings.anthropicApiKey.length > 0
            case "gemini": return Settings.geminiApiKey.length > 0
            case "openrouter": return Settings.openrouterApiKey.length > 0 && Settings.openrouterModel.length > 0
            case "ollama": return Settings.ollamaEndpoint.length > 0 && Settings.ollamaModel.length > 0
            default: return false
        }
    }

    // Full-width card
    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.scaled(8)
        color: Theme.surfaceColor
        radius: Theme.cardRadius

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.scaled(16)
            contentHeight: aiTabContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: aiTabContent
                width: parent.width
                spacing: Theme.scaled(16)

                // Provider selection - centered row of fixed-size buttons
                Item {
                    Layout.fillWidth: true
                    implicitHeight: providerRow.height

                    Row {
                        id: providerRow
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.scaled(8)

                        Repeater {
                            model: [
                                { id: "openai", name: "OpenAI", desc: "GPT-4o" },
                                { id: "anthropic", name: "Anthropic", desc: "Claude" },
                                { id: "gemini", name: "Gemini", desc: "Flash" },
                                { id: "openrouter", name: "OpenRouter", desc: "Multi" },
                                { id: "ollama", name: "Ollama", desc: "Local" }
                            ]

                            delegate: Rectangle {
                                width: Theme.scaled(90)
                                height: Theme.scaled(56)
                                radius: Theme.scaled(8)

                                property bool isSelected: Settings.aiProvider === modelData.id
                                property bool hasKey: aiTab.isProviderConfigured(modelData.id)

                                color: {
                                    if (isSelected) return Theme.primaryColor
                                    if (hasKey) return Qt.rgba(0.2, 0.7, 0.3, 0.25)
                                    return Theme.backgroundColor
                                }
                                border.color: {
                                    if (isSelected) return Theme.primaryColor
                                    if (hasKey) return Qt.rgba(0.2, 0.7, 0.3, 0.5)
                                    return Theme.borderColor
                                }
                                border.width: 1

                                Column {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(2)

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.name
                                        font.pixelSize: Theme.scaled(13)
                                        font.bold: isSelected
                                        color: isSelected ? "white" : Theme.textColor
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.desc
                                        font.pixelSize: Theme.scaled(11)
                                        color: isSelected ? Qt.rgba(1,1,1,0.8) : Theme.textSecondaryColor
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: Settings.aiProvider = modelData.id
                                }
                            }
                        }
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Claude recommendation note
                Rectangle {
                    Layout.fillWidth: true
                    color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                    radius: Theme.scaled(6)
                    implicitHeight: recommendationText.implicitHeight + Theme.scaled(16)

                    Tr {
                        id: recommendationText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: Theme.scaled(12)
                        key: "settings.ai.recommendation"
                        fallback: "For shot analysis, we recommend Claude (Anthropic). In our testing, Claude better understands espresso extraction dynamics and gives more accurate dial-in advice. Other providers work for translation and general tasks."
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }
                }

                // API Key section (cloud providers)
                ColumnLayout {
                    visible: Settings.aiProvider !== "ollama"
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.ai.apiKey"
                        fallback: "API Key"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    StyledTextField {
                        id: apiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        text: {
                            switch(Settings.aiProvider) {
                                case "openai": return Settings.openaiApiKey
                                case "anthropic": return Settings.anthropicApiKey
                                case "gemini": return Settings.geminiApiKey
                                case "openrouter": return Settings.openrouterApiKey
                                default: return ""
                            }
                        }
                        onTextChanged: {
                            switch(Settings.aiProvider) {
                                case "openai": Settings.openaiApiKey = text; break
                                case "anthropic": Settings.anthropicApiKey = text; break
                                case "gemini": Settings.geminiApiKey = text; break
                                case "openrouter": Settings.openrouterApiKey = text; break
                            }
                        }
                    }

                    Text {
                        text: {
                            var getKey = TranslationManager.translate("settings.ai.getkey", "Get key:")
                            switch(Settings.aiProvider) {
                                case "openai": return getKey + " platform.openai.com -> API Keys"
                                case "anthropic": return getKey + " console.anthropic.com -> API Keys"
                                case "gemini": return getKey + " aistudio.google.com -> Get API Key"
                                case "openrouter": return getKey + " openrouter.ai -> Keys"
                                default: return ""
                            }
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }

                // OpenRouter model settings
                ColumnLayout {
                    visible: Settings.aiProvider === "openrouter"
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.ai.openrouterModel"
                        fallback: "Model"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    StyledTextField {
                        id: openrouterModelField
                        Layout.fillWidth: true
                        placeholderText: "anthropic/claude-sonnet-4"
                        text: Settings.openrouterModel
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        onTextChanged: Settings.openrouterModel = text
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.openroutermodelhint", "Enter model ID from openrouter.ai/models (e.g., anthropic/claude-sonnet-4, openai/gpt-4o)")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // Ollama settings
                ColumnLayout {
                    visible: Settings.aiProvider === "ollama"
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.ai.ollamaSettings"
                        fallback: "Ollama Settings"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    StyledTextField {
                        id: ollamaEndpointField
                        Layout.fillWidth: true
                        placeholderText: "http://localhost:11434"
                        text: Settings.ollamaEndpoint
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onTextChanged: Settings.ollamaEndpoint = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        StyledComboBox {
                            Layout.fillWidth: true
                            model: MainController.aiManager ? MainController.aiManager.ollamaModels : []
                            currentIndex: model ? model.indexOf(Settings.ollamaModel) : -1
                            onCurrentTextChanged: if (currentText) Settings.ollamaModel = currentText
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.refresh", "Refresh")
                            accessibleName: qsTr("Refresh list of available Ollama AI models")
                            onClicked: MainController.aiManager?.refreshOllamaModels()
                        }
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.ollamainstall", "Install: ollama.ai -> run: ollama pull llama3.2")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Test connection row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    AccessibleButton {
                        primary: MainController.aiManager?.isConfigured ?? false
                        enabled: MainController.aiManager?.isConfigured ?? false
                        text: TranslationManager.translate("settings.ai.testconnection", "Test Connection")
                        accessibleName: qsTr("Test connection to the AI service")
                        onClicked: {
                            aiTab.testResultMessage = TranslationManager.translate("settings.ai.testing", "Testing...")
                            MainController.aiManager.testConnection()
                        }
                    }

                    Text {
                        visible: aiTab.testResultMessage.length > 0
                        text: aiTab.testResultMessage
                        color: aiTab.testResultSuccess ? Theme.successColor : Theme.errorColor
                        font.pixelSize: Theme.scaled(12)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: aiTab.testResultMessage.length === 0
                        text: {
                            switch(Settings.aiProvider) {
                                case "openai": return "~$0.01/" + TranslationManager.translate("settings.ai.pershot", "shot")
                                case "anthropic": return "~$0.003/" + TranslationManager.translate("settings.ai.pershot", "shot")
                                case "gemini": return "~$0.002/" + TranslationManager.translate("settings.ai.pershot", "shot")
                                case "openrouter": return TranslationManager.translate("settings.ai.variesbymodel", "Varies by model")
                                case "ollama": return TranslationManager.translate("settings.ai.free", "Free")
                                default: return ""
                            }
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        id: continueConversationBtn
                        property bool hasConversation: MainController.aiManager && MainController.aiManager.conversation &&
                                                       MainController.aiManager.conversation.hasSavedConversation
                        visible: MainController.aiManager && MainController.aiManager.isConfigured
                        enabled: hasConversation
                        text: hasConversation
                              ? TranslationManager.translate("settings.ai.continueconversation", "Continue Chat")
                              : TranslationManager.translate("settings.ai.noconversation", "No Chat")
                        accessibleName: hasConversation
                              ? qsTr("Continue previous AI conversation")
                              : qsTr("No saved AI conversation")
                        onClicked: {
                            MainController.aiManager.conversation.loadFromStorage()
                            conversationOverlay.visible = true
                        }
                    }
                }

                // Spacer to push content up
                Item { Layout.fillHeight: true }
            }
        }
    }

    // Conversation overlay panel
    Rectangle {
        id: conversationOverlay
        visible: false
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.7)
        z: 200

        MouseArea {
            anchors.fill: parent
            onClicked: {
                MainController.aiManager?.conversation?.saveToStorage()
                conversationOverlay.visible = false
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.scaled(16)
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            MouseArea { anchors.fill: parent; onClicked: {} }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: TranslationManager.translate("settings.ai.conversation.title", "AI Conversation")
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.conversation.clear", "Clear")
                        accessibleName: qsTr("Clear entire AI conversation history")
                        destructive: true
                        onClicked: {
                            MainController.aiManager?.conversation?.clearHistory()
                            MainController.aiManager?.conversation?.saveToStorage()
                            conversationOverlay.visible = false
                        }
                    }

                    Item { width: Theme.scaled(8) }

                    StyledIconButton {
                        text: "\u00D7"
                        accessibleName: TranslationManager.translate("common.close", "Close")
                        onClicked: {
                            MainController.aiManager?.conversation?.saveToStorage()
                            conversationOverlay.visible = false
                        }
                    }
                }

                Flickable {
                    id: conversationFlickable
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: conversationText.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    TextArea {
                        id: conversationText
                        width: parent.width
                        text: MainController.aiManager?.conversation?.getConversationText() ?? ""
                        textFormat: Text.MarkdownText
                        wrapMode: TextEdit.WordWrap
                        readOnly: true
                        font: Theme.bodyFont
                        color: Theme.textColor
                        background: null
                        padding: 0
                    }
                }

                RowLayout {
                    visible: MainController.aiManager?.conversation?.busy ?? false
                    Layout.fillWidth: true

                    BusyIndicator {
                        running: true
                        Layout.preferredWidth: Theme.scaled(20)
                        Layout.preferredHeight: Theme.scaled(20)
                        palette.dark: Theme.primaryColor
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.conversation.thinking", "Thinking...")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    StyledTextField {
                        id: conversationInput
                        Layout.fillWidth: true
                        placeholder: TranslationManager.translate("settings.ai.conversation.placeholder", "Ask a question...")
                        enabled: !(MainController.aiManager?.conversation?.busy ?? true)

                        Keys.onReturnPressed: sendMsg()
                        Keys.onEnterPressed: sendMsg()

                        function sendMsg() {
                            if (text.length === 0) return
                            MainController.aiManager?.conversation?.followUp(text)
                            text = ""
                        }
                    }

                    AccessibleButton {
                        primary: conversationInput.text.length > 0
                        enabled: conversationInput.text.length > 0 && !(MainController.aiManager?.conversation?.busy ?? true)
                        text: TranslationManager.translate("settings.ai.conversation.send", "Send")
                        accessibleName: qsTr("Send message to AI")
                        onClicked: conversationInput.sendMsg()
                    }
                }
            }
        }

        Connections {
            target: MainController.aiManager?.conversation ?? null
            function onResponseReceived() {
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                })
            }
            function onHistoryChanged() {
                conversationText.text = MainController.aiManager?.conversation?.getConversationText() ?? ""
            }
        }
    }

    Connections {
        target: MainController.aiManager
        function onTestResultChanged() {
            aiTab.testResultSuccess = MainController.aiManager.lastTestSuccess
            aiTab.testResultMessage = MainController.aiManager.lastTestResult
        }
    }
}
