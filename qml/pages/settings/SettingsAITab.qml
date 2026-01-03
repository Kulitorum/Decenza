import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: aiTab

    property string testResultMessage: ""
    property bool testResultSuccess: false

    // Helper function to check if provider has a key configured
    function isProviderConfigured(providerId) {
        switch(providerId) {
            case "openai": return Settings.openaiApiKey.length > 0
            case "anthropic": return Settings.anthropicApiKey.length > 0
            case "gemini": return Settings.geminiApiKey.length > 0
            case "ollama": return Settings.ollamaEndpoint.length > 0 && Settings.ollamaModel.length > 0
            default: return false
        }
    }

    ColumnLayout {
        id: aiTabContent
        anchors.fill: parent
        property real keyboardOffset: Qt.inputMethod.visible ? -80 : 0
        transform: Translate { y: aiTabContent.keyboardOffset }
        spacing: 10

        Behavior on keyboardOffset {
            NumberAnimation { duration: 150 }
        }

        // Provider selection - horizontal row
        Rectangle {
            Layout.fillWidth: true
            height: 70
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Repeater {
                    model: [
                        { id: "openai", name: "OpenAI", desc: "GPT-4o" },
                        { id: "anthropic", name: "Anthropic", desc: "Claude" },
                        { id: "gemini", name: "Gemini", desc: "Flash" },
                        { id: "ollama", name: "Ollama", desc: "Local" }
                    ]

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6

                        property bool isSelected: Settings.aiProvider === modelData.id
                        property bool hasKey: aiTab.isProviderConfigured(modelData.id)

                        color: {
                            if (isSelected) return Theme.primaryColor
                            if (hasKey) return Qt.rgba(0.2, 0.7, 0.3, 0.25)
                            return Qt.darker(Theme.surfaceColor, 1.15)
                        }
                        border.color: {
                            if (isSelected) return Theme.primaryColor
                            if (hasKey) return Qt.rgba(0.2, 0.7, 0.3, 0.5)
                            return "transparent"
                        }
                        border.width: isSelected ? 2 : 1

                        Column {
                            anchors.centerIn: parent
                            spacing: 2

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.name
                                font.pixelSize: 12
                                font.bold: isSelected
                                color: isSelected ? "white" : Theme.textColor
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.desc
                                font.pixelSize: 10
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

        // Claude recommendation note
        Rectangle {
            Layout.fillWidth: true
            color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
            radius: Theme.cardRadius
            height: recommendationText.implicitHeight + 16

            Tr {
                id: recommendationText
                anchors.fill: parent
                anchors.margins: 8
                key: "settings.ai.recommendation"
                fallback: "For shot analysis, we recommend Claude (Anthropic). In our testing, Claude better understands espresso extraction dynamics and gives more accurate dial-in advice. Other providers work for translation and general tasks."
                wrapMode: Text.WordWrap
                color: Theme.textSecondaryColor
                font.pixelSize: 11
            }
        }

        // API Key / Ollama settings
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // Cloud provider API key
                ColumnLayout {
                    visible: Settings.aiProvider !== "ollama"
                    Layout.fillWidth: true
                    spacing: 6

                    Tr {
                        key: "settings.ai.apiKey"
                        fallback: "API Key"
                        color: Theme.textColor
                        font.pixelSize: 13
                        font.bold: true
                    }

                    StyledTextField {
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
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
                        onAccepted: focus = false
                        Keys.onReturnPressed: focus = false
                    }

                    Text {
                        text: {
                            var getKey = TranslationManager.translate("settings.ai.getkey", "Get key:")
                            switch(Settings.aiProvider) {
                                case "openai": return getKey + " platform.openai.com -> API Keys"
                                case "anthropic": return getKey + " console.anthropic.com -> API Keys"
                                case "gemini": return getKey + " aistudio.google.com -> Get API Key"
                                default: return ""
                            }
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: 11
                    }
                }

                // Ollama settings
                ColumnLayout {
                    visible: Settings.aiProvider === "ollama"
                    Layout.fillWidth: true
                    spacing: 6

                    Tr {
                        key: "settings.ai.ollamaSettings"
                        fallback: "Ollama Settings"
                        color: Theme.textColor
                        font.pixelSize: 13
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        StyledTextField {
                            Layout.fillWidth: true
                            placeholderText: "http://localhost:11434"
                            text: Settings.ollamaEndpoint
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                            onTextChanged: Settings.ollamaEndpoint = text
                            onAccepted: focus = false
                            Keys.onReturnPressed: focus = false
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ComboBox {
                            Layout.fillWidth: true
                            model: MainController.aiManager ? MainController.aiManager.ollamaModels : []
                            currentIndex: model ? model.indexOf(Settings.ollamaModel) : -1
                            onCurrentTextChanged: if (currentText) Settings.ollamaModel = currentText
                            background: Rectangle {
                                implicitHeight: 36
                                color: Theme.surfaceColor
                                border.color: Theme.borderColor
                                radius: 6
                            }
                        }

                        Rectangle {
                            width: 70
                            height: 36
                            radius: 6
                            color: Theme.surfaceColor
                            border.color: Theme.borderColor

                            Tr {
                                anchors.centerIn: parent
                                key: "settings.ai.refresh"
                                fallback: "Refresh"
                                color: Theme.textColor
                                font.pixelSize: 12
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: MainController.aiManager?.refreshOllamaModels()
                            }
                        }
                    }

                    Tr {
                        key: "settings.ai.ollamainstall"
                        fallback: "Install: ollama.ai -> run: ollama pull llama3.2"
                        color: Theme.textSecondaryColor
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillHeight: true }

                // Test connection + cost in a row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        width: 110
                        height: 36
                        radius: 6
                        color: MainController.aiManager?.isConfigured ? Theme.primaryColor : Theme.surfaceColor
                        border.color: MainController.aiManager?.isConfigured ? Theme.primaryColor : Theme.borderColor

                        Tr {
                            anchors.centerIn: parent
                            key: "settings.ai.testconnection"
                            fallback: "Test Connection"
                            color: MainController.aiManager?.isConfigured ? "white" : Theme.textSecondaryColor
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: MainController.aiManager?.isConfigured
                            onClicked: {
                                aiTab.testResultMessage = TranslationManager.translate("settings.ai.testing", "Testing...")
                                MainController.aiManager.testConnection()
                            }
                        }
                    }

                    Text {
                        visible: aiTab.testResultMessage.length > 0
                        text: aiTab.testResultMessage
                        color: aiTab.testResultSuccess ? Theme.successColor : Theme.errorColor
                        font.pixelSize: 11
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
                                case "ollama": return TranslationManager.translate("settings.ai.free", "Free")
                                default: return ""
                            }
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: 11
                    }
                }
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
