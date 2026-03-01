import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: aiSettingsPage
    objectName: "aiSettingsPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("aisettings.title", "AI Dialing Assistant")
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("aisettings.title", "AI Dialing Assistant")

    property string testResultMessage: ""
    property bool testResultSuccess: false

    KeyboardAwareContainer {
        anchors.fill: parent
        textFields: [apiKeyField, ollamaEndpointField]

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.standardMargin
        contentHeight: contentColumn.height
        clip: true

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: Theme.spacingMedium

            // Header description
            Tr {
                key: "aisettings.description"
                fallback: "Get AI-powered recommendations to dial in your espresso. Follows James Hoffmann's methodology: one variable at a time."
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Item { height: Theme.spacingSmall }

            // Provider Selection
            Tr {
                key: "aisettings.label.provider"
                fallback: "AI Provider"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            // Provider cards
            Repeater {
                model: [
                    { id: "openai", nameKey: "aisettings.provider.openai", name: "OpenAI", descKey: "aisettings.provider.openai.desc", desc: "Excellent reasoning" },
                    { id: "anthropic", nameKey: "aisettings.provider.anthropic", name: "Anthropic", descKey: "aisettings.provider.anthropic.desc", desc: "Nuanced analysis" },
                    { id: "gemini", nameKey: "aisettings.provider.gemini", name: "Google Gemini", descKey: "aisettings.provider.gemini.desc", desc: "Fast & cheap" },
                    { id: "ollama", nameKey: "aisettings.provider.ollama", name: "Ollama (Local)", descKey: "aisettings.provider.ollama.desc", desc: "Free, private, runs on your computer" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.scaled(500)
                    height: Theme.scaled(70)
                    radius: Theme.buttonRadius
                    color: Settings.aiProvider === modelData.id ? Theme.primaryColor : Theme.surfaceColor
                    border.color: Settings.aiProvider === modelData.id ? Theme.primaryColor : Theme.borderColor
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)

                            Tr {
                                key: modelData.nameKey
                                fallback: modelData.name
                                font: Theme.bodyFont
                                color: Theme.textColor
                            }
                            Tr {
                                key: modelData.descKey
                                fallback: modelData.desc
                                font.pixelSize: Theme.scaled(12)
                                color: Theme.textSecondaryColor
                            }
                        }

                        // Checkmark if selected
                        Rectangle {
                            width: Theme.scaled(24)
                            height: Theme.scaled(24)
                            radius: Theme.scaled(12)
                            visible: Settings.aiProvider === modelData.id
                            color: "transparent"
                            border.color: Theme.textColor
                            border.width: 2

                            Rectangle {
                                anchors.centerIn: parent
                                width: Theme.scaled(12)
                                height: Theme.scaled(12)
                                radius: Theme.scaled(6)
                                color: Theme.textColor
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: Settings.aiProvider = modelData.id
                    }
                }
            }

            // Current model
            Text {
                visible: MainController.aiManager && MainController.aiManager.currentModelName.length > 0
                text: TranslationManager.translate("aisettings.label.currentmodel", "Model") + ": " + (MainController.aiManager ? MainController.aiManager.currentModelName : "")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
            }

            Item { height: Theme.spacingMedium }

            // API Key section (for cloud providers)
            ColumnLayout {
                visible: Settings.aiProvider !== "ollama"
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Tr {
                    key: "aisettings.label.apikey"
                    fallback: "API Key"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }

                StyledTextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: TranslationManager.translate("aisettings.placeholder.apikey", "Paste your API key here")
                    text: {
                        switch(Settings.aiProvider) {
                            case "openai": return Settings.openaiApiKey
                            case "anthropic": return Settings.anthropicApiKey
                            case "gemini": return Settings.geminiApiKey
                            default: return ""
                        }
                    }
                    onTextChanged: {
                        switch(Settings.aiProvider) {
                            case "openai": Settings.openaiApiKey = text; break
                            case "anthropic": Settings.anthropicApiKey = text; break
                            case "gemini": Settings.geminiApiKey = text; break
                        }
                    }
                }
            }

            // Ollama settings
            ColumnLayout {
                visible: Settings.aiProvider === "ollama"
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Tr {
                    key: "aisettings.label.endpoint"
                    fallback: "Ollama Endpoint"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }

                StyledTextField {
                    id: ollamaEndpointField
                    Layout.fillWidth: true
                    placeholderText: "http://localhost:11434"
                    text: Settings.ollamaEndpoint
                    onTextChanged: Settings.ollamaEndpoint = text
                }

                Item { height: Theme.spacingSmall }

                Tr {
                    key: "aisettings.label.model"
                    fallback: "Model"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    StyledComboBox {
                        id: ollamaModelCombo
                        Layout.fillWidth: true
                        accessibleLabel: TranslationManager.translate("aisettings.label.model", "Model")
                        model: MainController.aiManager ? MainController.aiManager.ollamaModels : []
                        currentIndex: model.indexOf(Settings.ollamaModel)
                        onCurrentTextChanged: if (currentText) Settings.ollamaModel = currentText

                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: ollamaModelCombo.activeFocus ? Theme.primaryColor : Theme.borderColor
                            border.width: 1
                        }

                        contentItem: Text {
                            text: ollamaModelCombo.displayText || TranslationManager.translate("aisettings.placeholder.model", "Select a model")
                            color: Theme.textColor
                            font: Theme.bodyFont
                            leftPadding: Theme.scaled(12)
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("aisettings.button.refresh", "Refresh")
                        accessibleName: TranslationManager.translate("aiSettings.refreshOllamaModels", "Refresh list of available Ollama AI models")
                        onClicked: {
                            if (MainController.aiManager) {
                                MainController.aiManager.refreshOllamaModels()
                            }
                        }
                    }
                }
            }

            Item { height: Theme.spacingMedium }

            // Test Connection
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                AccessibleButton {
                    primary: MainController.aiManager && MainController.aiManager.isConfigured
                    enabled: MainController.aiManager && MainController.aiManager.isConfigured
                    text: TranslationManager.translate("aisettings.button.test", "Test Connection")
                    accessibleName: TranslationManager.translate("aiSettings.testConnection", "Test connection to AI service")
                    onClicked: {
                        aiSettingsPage.testResultMessage = TranslationManager.translate("aisettings.status.testing", "Testing...")
                        MainController.aiManager.testConnection()
                    }
                }

                Text {
                    text: aiSettingsPage.testResultMessage
                    color: aiSettingsPage.testResultSuccess ? Theme.successColor : Theme.errorColor
                    font.pixelSize: Theme.scaled(14)
                    visible: aiSettingsPage.testResultMessage.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            Item { height: Theme.spacingLarge }

            // Help text
            Tr {
                key: "aisettings.help.title"
                fallback: "How to get an API key:"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Text {
                text: {
                    switch(Settings.aiProvider) {
                        case "openai": return TranslationManager.translate("aisettings.help.openai", "1. Visit platform.openai.com\n2. Create an account or sign in\n3. Go to API Keys section\n4. Create a new key and paste it above")
                        case "anthropic": return TranslationManager.translate("aisettings.help.anthropic", "1. Visit console.anthropic.com\n2. Create an account or sign in\n3. Go to API Keys section\n4. Create a new key and paste it above")
                        case "gemini": return TranslationManager.translate("aisettings.help.gemini", "1. Visit aistudio.google.com\n2. Sign in with your Google account\n3. Click 'Get API Key'\n4. Create a key and paste it above")
                        case "ollama": return TranslationManager.translate("aisettings.help.ollama", "1. Install Ollama from ollama.ai\n2. Open a terminal and run:\n   ollama pull llama3.2\n3. Ensure Ollama is running\n4. Click Refresh above to see models")
                        default: return ""
                    }
                }
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Item { height: Theme.spacingLarge }

            // Cost info
            Rectangle {
                Layout.fillWidth: true
                height: costColumn.height + Theme.spacingMedium * 2
                radius: Theme.buttonRadius
                color: Theme.surfaceColor

                ColumnLayout {
                    id: costColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.scaled(4)

                    Tr {
                        key: "aisettings.cost.title"
                        fallback: "Estimated Cost"
                        color: Theme.textColor
                        font: Theme.subtitleFont
                    }

                    Text {
                        text: {
                            var modelName = MainController.aiManager ? MainController.aiManager.currentModelName : ""
                            switch(Settings.aiProvider) {
                                case "openai": return TranslationManager.translate("aisettings.cost.openai", "~$0.006 per analysis") + " (" + modelName + ")"
                                case "anthropic": return TranslationManager.translate("aisettings.cost.anthropic", "~$0.01 per analysis") + " (" + modelName + ")"
                                case "gemini": return TranslationManager.translate("aisettings.cost.gemini", "<$0.001 per analysis") + " (" + modelName + ")"
                                case "ollama": return TranslationManager.translate("aisettings.cost.ollama", "Free (runs locally on your computer)")
                                default: return ""
                            }
                        }
                        color: Theme.textSecondaryColor
                        font: Theme.bodyFont
                    }

                    Text {
                        visible: Settings.aiProvider !== "ollama"
                        text: {
                            switch(Settings.aiProvider) {
                                case "openai": return TranslationManager.translate("aisettings.cost.monthly.openai", "Under $1/month at 3 shots per day")
                                case "anthropic": return TranslationManager.translate("aisettings.cost.monthly.anthropic", "Under $1/month at 3 shots per day")
                                case "gemini": return TranslationManager.translate("aisettings.cost.monthly.gemini", "About $0.05/month at 3 shots per day — practically free")
                                default: return ""
                            }
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Item { height: Theme.spacingLarge }
        }
        }
    }

    // Connection to AIManager for test results
    Connections {
        target: MainController.aiManager
        function onTestResultChanged() {
            aiSettingsPage.testResultSuccess = MainController.aiManager.lastTestSuccess
            aiSettingsPage.testResultMessage = MainController.aiManager.lastTestResult
        }
    }

}
