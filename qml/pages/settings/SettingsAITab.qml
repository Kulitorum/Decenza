import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: aiTab
    textFields: [apiKeyField, ollamaEndpointField, openrouterModelField, customUrlField]
    targetFlickable: aiFlickable

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

    // Discuss Shot app display names (index matches Settings.discussShotApp)
    readonly property var discussAppNames: [
        TranslationManager.translate("settings.ai.discuss.app.claudeApp", "Claude App"),
        TranslationManager.translate("settings.ai.discuss.app.claudeWeb", "Claude Web"),
        TranslationManager.translate("settings.ai.discuss.app.chatgpt", "ChatGPT"),
        TranslationManager.translate("settings.ai.discuss.app.gemini", "Gemini"),
        TranslationManager.translate("settings.ai.discuss.app.grok", "Grok"),
        TranslationManager.translate("settings.ai.discuss.customUrl", "Custom URL"),
        TranslationManager.translate("settings.ai.discuss.app.none", "None"),
        TranslationManager.translate("settings.ai.discuss.app.claudeDesktop", "Claude Desktop")
    ]

    // Full-width card
    Rectangle {
        objectName: "aiProvider"
        anchors.fill: parent
        color: Theme.surfaceColor
        radius: Theme.cardRadius

        Flickable {
            id: aiFlickable
            anchors.fill: parent
            anchors.margins: Theme.scaled(12)
            contentHeight: aiTabContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: aiTabContent
                width: parent.width
                spacing: Theme.scaled(16)

                // ═══════════════════════════════════════════
                // SECTION 1: AI Provider
                // ═══════════════════════════════════════════
                Text {
                    text: TranslationManager.translate("settings.ai.section.provider", "AI Provider")
                    font.family: Theme.subtitleFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    color: Theme.textColor
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

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
                                { id: "openai", name: "OpenAI" },
                                { id: "anthropic", name: "Anthropic" },
                                { id: "gemini", name: "Gemini" },
                                { id: "openrouter", name: "OpenRouter" },
                                { id: "ollama", name: "Ollama" }
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

                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.name + (isSelected
                                    ? " (" + TranslationManager.translate("settings.ai.selected", "selected") + ")"
                                    : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: providerArea.clicked(null)

                                Column {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(2)

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.name
                                        font.pixelSize: Theme.scaled(13)
                                        font.bold: isSelected
                                        color: isSelected ? Theme.primaryContrastColor : Theme.textColor
                                        Accessible.ignored: true
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: MainController.aiManager ? MainController.aiManager.modelDisplayName(modelData.id) : ""
                                        font.pixelSize: Theme.scaled(11)
                                        color: isSelected ? Qt.rgba(1,1,1,0.8) : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                }

                                MouseArea {
                                    id: providerArea
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
                            accessibleLabel: TranslationManager.translate("settings.ai.ollamaModel", "Ollama model")
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.refresh", "Refresh")
                            accessibleName: TranslationManager.translate("settings.ai.refreshOllamaModels", "Refresh list of available Ollama AI models")
                            onClicked: MainController.aiManager?.refreshOllamaModels()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.ai.ollamainstall", "Install: ollama.ai -> run: ollama pull llama3.2")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Cost info
                Text {
                    visible: Settings.aiProvider !== "ollama"
                    text: {
                        var perShot = TranslationManager.translate("settings.ai.pershot", "shot")
                        switch(Settings.aiProvider) {
                            case "openai": return TranslationManager.translate("settings.ai.cost.openai",
                                "Estimated cost: ~$0.006/" + perShot + " — under $1/month at 3 shots per day")
                            case "anthropic": return TranslationManager.translate("settings.ai.cost.anthropic",
                                "Estimated cost: ~$0.01/" + perShot + " — under $1/month at 3 shots per day")
                            case "gemini": return TranslationManager.translate("settings.ai.cost.gemini",
                                "Estimated cost: <$0.001/" + perShot + " — about $0.05/month at 3 shots per day")
                            case "openrouter": return TranslationManager.translate("settings.ai.cost.openrouter",
                                "Cost varies by model")
                            default: return ""
                        }
                    }
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    visible: Settings.aiProvider === "ollama"
                    text: TranslationManager.translate("settings.ai.cost.ollama", "Free — runs locally on your computer")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Test connection row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    AccessibleButton {
                        primary: MainController.aiManager?.isConfigured ?? false
                        enabled: MainController.aiManager?.isConfigured ?? false
                        text: TranslationManager.translate("settings.ai.testconnection", "Test Connection")
                        accessibleName: TranslationManager.translate("settings.ai.testConnectionAccessible", "Test connection to the AI service")
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

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        id: continueConversationBtn
                        property bool hasConversation: MainController.aiManager && MainController.aiManager.hasAnyConversation
                        visible: MainController.aiManager && MainController.aiManager.isConfigured
                        enabled: hasConversation
                        text: hasConversation
                              ? TranslationManager.translate("settings.ai.continueconversation", "Continue Chat")
                              : TranslationManager.translate("settings.ai.noconversation", "No Chat")
                        accessibleName: hasConversation
                              ? TranslationManager.translate("settings.ai.continueConversationAccessible", "Continue previous AI conversation")
                              : TranslationManager.translate("settings.ai.noConversationAccessible", "No saved AI conversation")
                        onClicked: {
                            MainController.aiManager.loadMostRecentConversation()
                            conversationOverlay.visible = true
                            Qt.callLater(function() {
                                conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                            })
                        }
                    }
                }

                // ═══════════════════════════════════════════
                // SECTION 2: MCP Server (AI Remote Control)
                // ═══════════════════════════════════════════
                Item { height: Theme.scaled(8) }

                RowLayout {
                    objectName: "mcpServer"
                    Layout.fillWidth: true
                    Text {
                        text: TranslationManager.translate("settings.ai.section.mcp", "MCP Server (AI Remote Control)")
                        font.family: Theme.subtitleFont.family
                        font.pixelSize: Theme.subtitleFont.pixelSize
                        font.bold: true
                        color: Theme.textColor
                        Layout.fillWidth: true
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.mcp.setupGuide", "Setup Guide")
                        accessibleName: TranslationManager.translate("settings.ai.mcp.helpAccessible", "What is MCP and how to set it up")
                        onClicked: mcpHelpDialog.open()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.borderColor
                }

                // Enable MCP toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.ai.mcp.enable"
                            fallback: "Enable MCP Server"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }

                        Tr {
                            key: "settings.ai.mcp.description"
                            fallback: "Allows AI assistants like Claude Desktop to monitor and control your DE1 remotely."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    StyledSwitch {
                        checked: Settings.mcpEnabled
                        accessibleName: TranslationManager.translate("settings.ai.mcp.enableAccessible", "Enable MCP server for AI remote control")
                        onCheckedChanged: Settings.mcpEnabled = checked
                    }
                }

                // Setup page link (visible when MCP enabled)
                ColumnLayout {
                    visible: Settings.mcpEnabled && MainController.shotServer
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.setupPageLabel", "Setup page:") + " "
                            + (MainController.shotServer ? MainController.shotServer.url : "") + "/mcp/setup"
                        color: Theme.accentColor
                        font.pixelSize: Theme.scaled(12)
                        font.underline: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.Link
                        Accessible.name: TranslationManager.translate("settings.ai.mcp.setupLinkAccessible", "Open MCP setup page in browser")
                        Accessible.focusable: true
                        MouseArea {
                            id: setupLinkArea
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally((MainController.shotServer ? MainController.shotServer.url : "") + "/mcp/setup")
                        }
                        Accessible.onPressAction: setupLinkArea.clicked(null)
                    }
                    Tr {
                        key: "settings.ai.mcp.setupPageHint"
                        fallback: "Open this link on your desktop computer with Claude Desktop installed."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Access Level
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    enabled: Settings.mcpEnabled
                    opacity: Settings.mcpEnabled ? 1.0 : 0.5

                    Tr {
                        key: "settings.ai.mcp.accessLevel"
                        fallback: "Access Level"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Repeater {
                        model: [
                            {
                                level: 0,
                                label: TranslationManager.translate("settings.ai.mcp.access.monitor", "Monitor Only"),
                                detail: TranslationManager.translate("settings.ai.mcp.access.monitorDesc", "Read state, telemetry, shot history, profiles")
                            },
                            {
                                level: 1,
                                label: TranslationManager.translate("settings.ai.mcp.access.control", "Control"),
                                detail: TranslationManager.translate("settings.ai.mcp.access.controlDesc", "Monitor + start/stop operations, wake/sleep")
                            },
                            {
                                level: 2,
                                label: TranslationManager.translate("settings.ai.mcp.access.full", "Full Automation"),
                                detail: TranslationManager.translate("settings.ai.mcp.access.fullDesc", "Control + upload profiles, change settings")
                            }
                        ]

                        delegate: Rectangle {
                            id: accessDelegate
                            Layout.fillWidth: true
                            Layout.preferredHeight: accessDelegateCol.implicitHeight + Theme.scaled(16)
                            radius: Theme.scaled(6)
                            color: Settings.mcpAccessLevel === modelData.level ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: Settings.mcpAccessLevel === modelData.level ? Theme.primaryColor : Theme.borderColor
                            border.width: 1

                            Accessible.ignored: true

                            ColumnLayout {
                                id: accessDelegateCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.scaled(12)
                                spacing: Theme.scaled(2)

                                Text {
                                    text: modelData.label
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                    font.bold: true
                                    Accessible.ignored: true
                                }
                                Text {
                                    text: modelData.detail
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(11)
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: modelData.label + ". " + modelData.detail
                                accessibleItem: accessDelegate
                                onAccessibleClicked: Settings.mcpAccessLevel = modelData.level
                            }
                        }
                    }
                }

                // Confirmation Level
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    enabled: Settings.mcpEnabled && Settings.mcpAccessLevel > 0
                    opacity: (Settings.mcpEnabled && Settings.mcpAccessLevel > 0) ? 1.0 : 0.5

                    Tr {
                        key: "settings.ai.mcp.confirmationLevel"
                        fallback: "Confirmation"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Repeater {
                        model: [
                            {
                                level: 0,
                                label: TranslationManager.translate("settings.ai.mcp.confirm.none", "None"),
                                detail: TranslationManager.translate("settings.ai.mcp.confirm.noneDesc", "Commands execute immediately")
                            },
                            {
                                level: 1,
                                label: TranslationManager.translate("settings.ai.mcp.confirm.dangerous", "Dangerous Only"),
                                detail: TranslationManager.translate("settings.ai.mcp.confirm.dangerousDesc", "Confirm start operations, profile uploads, settings changes")
                            },
                            {
                                level: 2,
                                label: TranslationManager.translate("settings.ai.mcp.confirm.all", "All Control"),
                                detail: TranslationManager.translate("settings.ai.mcp.confirm.allDesc", "Confirm every machine control and write operation")
                            }
                        ]

                        delegate: Rectangle {
                            id: confirmDelegate
                            Layout.fillWidth: true
                            Layout.preferredHeight: confirmDelegateCol.implicitHeight + Theme.scaled(16)
                            radius: Theme.scaled(6)
                            color: Settings.mcpConfirmationLevel === modelData.level ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: Settings.mcpConfirmationLevel === modelData.level ? Theme.primaryColor : Theme.borderColor
                            border.width: 1

                            Accessible.ignored: true

                            ColumnLayout {
                                id: confirmDelegateCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.scaled(12)
                                spacing: Theme.scaled(2)

                                Text {
                                    text: modelData.label
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(13)
                                    font.bold: true
                                    Accessible.ignored: true
                                }
                                Text {
                                    text: modelData.detail
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(11)
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: modelData.label + ". " + modelData.detail
                                accessibleItem: confirmDelegate
                                onAccessibleClicked: Settings.mcpConfirmationLevel = modelData.level
                            }
                        }
                    }
                }

                // MCP Status line
                Text {
                    visible: Settings.mcpEnabled
                    text: {
                        var status = TranslationManager.translate("settings.ai.mcp.status.listening", "Listening on port %1").arg(Settings.shotServerPort)
                        if (typeof McpServer !== "undefined" && McpServer) {
                            var sessions = McpServer.activeSessionCount
                            if (sessions > 0)
                                status += " · " + TranslationManager.translate("settings.ai.mcp.status.sessions", "%1 active session(s)").arg(sessions)
                        }
                        return status
                    }
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: !Settings.mcpEnabled
                    text: TranslationManager.translate("settings.ai.mcp.status.disabled", "MCP server is disabled")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }


                // ─── Discuss Shot subsection ───
                Item { height: Theme.scaled(4) }

                Tr {
                    key: "settings.ai.discuss.title"
                    fallback: "Discuss Shot"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "settings.ai.discuss.openIn"
                        fallback: "Open in:"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                    }

                    Rectangle {
                        id: discussAppButton
                        Layout.fillWidth: true
                        height: Theme.scaled(36)
                        radius: Theme.scaled(6)
                        color: Theme.backgroundColor
                        border.color: Theme.borderColor
                        border.width: 1

                        Accessible.ignored: true

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.verticalCenter: parent.verticalCenter
                            text: aiTab.discussAppNames[Settings.discussShotApp] ?? aiTab.discussAppNames[0]
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("settings.ai.discuss.selectApp", "Select AI app for discussing shots") + ". " + (aiTab.discussAppNames[Settings.discussShotApp] ?? aiTab.discussAppNames[0])
                            accessibleItem: discussAppButton
                            onAccessibleClicked: discussAppDialog.open()
                        }
                    }
                }

                // Custom URL field (only when Custom URL is selected)
                ColumnLayout {
                    visible: Settings.discussShotApp === 5
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    StyledTextField {
                        id: customUrlField
                        Layout.fillWidth: true
                        placeholder: "https://localhost:8080"
                        text: Settings.discussShotCustomUrl
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onTextChanged: Settings.discussShotCustomUrl = text
                    }
                }

                // Claude Desktop (Remote Control) setup — visible when Claude Desktop is selected
                ColumnLayout {
                    visible: Settings.discussShotApp === Settings.discussAppClaudeDesktop
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                            "settings.ai.discuss.claudeDesktop.help",
                            "Paste the session URL printed by `claude remote-control`. See the MCP Setup page for step-by-step instructions.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    StyledTextField {
                        id: claudeRcUrlField
                        Layout.fillWidth: true
                        placeholder: "https://claude.ai/..."
                        text: Settings.claudeRcSessionUrl
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onTextChanged: Settings.claudeRcSessionUrl = text
                    }
                }

                // Spacer to push content up
                Item { Layout.fillHeight: true }
            }
        }
    }

    // Discuss Shot app selector
    // MCP Help Dialog
    Dialog {
        id: mcpHelpDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(400), Theme.scaled(500))
        modal: true
        padding: 0
        topPadding: 0
        bottomPadding: 0
        header: null
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(mcpHelpTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Flickable {
            width: parent ? parent.width : mcpHelpDialog.width
            implicitHeight: Math.min(helpContent.implicitHeight, mcpHelpDialog.parent ? mcpHelpDialog.parent.height - Theme.scaled(100) : Theme.scaled(500))
            contentHeight: helpContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: helpContent
                width: parent.width
                spacing: Theme.scaled(12)

                // Header
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(48)

                    Text {
                        id: mcpHelpTitle
                        anchors.centerIn: parent
                        text: TranslationManager.translate("settings.ai.mcp.help.title", "What is MCP?")
                        font.pixelSize: Theme.scaled(18)
                        font.family: Theme.bodyFont.family
                        font.bold: true
                        color: Theme.textColor
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.borderColor
                    }
                }

                // Content
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(16)
                    Layout.rightMargin: Theme.scaled(16)
                    spacing: Theme.scaled(12)

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.intro",
                            "MCP (Model Context Protocol) lets AI assistants like Claude Desktop connect directly to your DE1 espresso machine. Instead of copy-pasting shot data, the AI can read your machine state, analyze shots, and suggest dial-in changes in real time.")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        color: Qt.rgba(Theme.warningButtonColor.r, Theme.warningButtonColor.g, Theme.warningButtonColor.b, 0.15)
                        radius: Theme.scaled(6)
                        implicitHeight: platformNoteText.implicitHeight + Theme.scaled(12)

                        Text {
                            id: platformNoteText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.scaled(8)
                            text: TranslationManager.translate("settings.ai.mcp.help.platformNote",
                                "The MCP server runs on any platform (tablet, phone, desktop). Claude Desktop on your macOS or Windows computer connects to it over your WiFi network.\n\nDoes NOT work with: claude.ai web, Claude iOS/Android apps.\n\nTip: Use the Discuss button on shot review pages to open any AI with shot data on clipboard — works everywhere without MCP.")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.whatCanDo", "What can it do?")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.capabilities",
                            "- Monitor machine state, temperature, water level\n- Browse and analyze your shot history\n- Get AI-powered dial-in advice after each shot\n- Start/stop operations (DE1 v1.0 headless only — most GHC machines require physical button press)\n- Change profiles, grinder settings, brew parameters")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.howToSetup", "How to set up")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.steps",
                            "1. Enable MCP Server (toggle on the settings page)\n2. On your computer: install Claude Desktop (claude.ai/download)\n3. Open the setup page link on your computer (shown on the settings page when MCP is enabled)\n4. Copy and run the install command in your terminal — it installs Node.js if needed and configures Claude Desktop automatically\n5. Restart Claude Desktop\n6. Ask Claude about your espresso!\n\nBoth devices must be on the same WiFi network.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.accessLevels", "Access Levels")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.accessDesc",
                            "Monitor Only — The AI can read data but cannot control the machine.\nControl — The AI can also start/stop operations.\nFull Automation — The AI can also change profiles and settings.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.security", "Security")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.securityDesc",
                            "MCP uses an API key (included in the config) to authenticate requests. The server only listens on your local network. Enable web security (TOTP) in Connections settings for additional protection.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // Buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.scaled(16)
                    spacing: Theme.scaled(8)

                    AccessibleButton {
                        visible: Settings.mcpEnabled && MainController.shotServer
                        text: TranslationManager.translate("settings.ai.mcp.help.openGuide", "Open Web Guide")
                        accessibleName: TranslationManager.translate("settings.ai.mcp.help.openGuideAccessible", "Open MCP setup guide in browser")
                        onClicked: {
                            Qt.openUrlExternally(MainController.shotServer.url + "/mcp/setup")
                            mcpHelpDialog.close()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.close", "Close")
                        accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Close dialog")
                        onClicked: mcpHelpDialog.close()
                    }
                }

                Item { height: Theme.scaled(4) }
            }
        }
    }

    SelectionDialog {
        id: discussAppDialog
        title: TranslationManager.translate("settings.ai.discuss.selectAppTitle", "Select AI App")
        options: aiTab.discussAppNames
        currentIndex: Settings.discussShotApp
        onSelected: function(index, value) { Settings.discussShotApp = index }
    }

    // Conversation overlay panel
    Rectangle {
        id: conversationOverlay
        visible: false
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.7)
        z: 200

        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("conversation.close.accessible", "Close conversation overlay")
        Accessible.focusable: true
        Accessible.onPressAction: conversationDismissArea.clicked(null)

        MouseArea {
            id: conversationDismissArea
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

                    ColumnLayout {
                        spacing: Theme.scaled(2)
                        Text {
                            text: TranslationManager.translate("settings.ai.conversation.title", "AI Conversation")
                            font: Theme.subtitleFont
                            color: Theme.textColor
                        }
                        Text {
                            visible: MainController.aiManager && MainController.aiManager.conversation &&
                                     MainController.aiManager.conversation.contextLabel.length > 0
                            text: MainController.aiManager ? (MainController.aiManager.conversation.contextLabel || "") : ""
                            font.pixelSize: Theme.scaled(11)
                            color: Theme.textSecondaryColor
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.conversation.clear", "Clear")
                        accessibleName: TranslationManager.translate("settings.ai.clearConversation", "Clear entire AI conversation history")
                        destructive: true
                        onClicked: {
                            MainController.aiManager?.clearCurrentConversation()
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
                        selectByMouse: true
                        font: Theme.bodyFont
                        color: Theme.textColor
                        background: null
                        padding: 0

                        onCursorRectangleChanged: {
                            if (selectedText.length === 0) {
                                selectionScrollTimer.stop()
                                return
                            }
                            var cursorViewY = cursorRectangle.y - conversationFlickable.contentY
                            var margin = Theme.scaled(30)
                            if (cursorViewY > conversationFlickable.height - margin) {
                                selectionScrollTimer.scrollStep = Math.min(Theme.scaled(10), Math.max(2, (cursorViewY - conversationFlickable.height + margin) / 2))
                                if (!selectionScrollTimer.running) selectionScrollTimer.start()
                            } else if (cursorViewY < margin) {
                                selectionScrollTimer.scrollStep = -Math.min(Theme.scaled(10), Math.max(2, (margin - cursorViewY) / 2))
                                if (!selectionScrollTimer.running) selectionScrollTimer.start()
                            } else {
                                selectionScrollTimer.stop()
                            }
                        }
                    }

                    Timer {
                        id: selectionScrollTimer
                        property real scrollStep: 0
                        interval: 30
                        repeat: true
                        onTriggered: {
                            if (conversationText.selectedText.length === 0) { stop(); return }
                            var newY = conversationFlickable.contentY + scrollStep
                            newY = Math.max(0, Math.min(newY, conversationFlickable.contentHeight - conversationFlickable.height))
                            if (newY === conversationFlickable.contentY) { stop(); return }
                            conversationFlickable.contentY = newY
                        }
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

                // Error message display
                Text {
                    visible: MainController.aiManager && MainController.aiManager.conversation &&
                             MainController.aiManager.conversation.errorMessage.length > 0 &&
                             !MainController.aiManager.conversation.busy
                    text: MainController.aiManager ? (MainController.aiManager.conversation.errorMessage || "") : ""
                    color: Theme.errorColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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
                        accessibleName: TranslationManager.translate("settings.ai.sendMessage", "Send message to AI")
                        onClicked: conversationInput.sendMsg()
                    }
                }
            }
        }

        property real _preResponseHeight: 0
        Connections {
            target: MainController.aiManager?.conversation ?? null
            function onResponseReceived() {
                // Scroll to top of the new response
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationOverlay._preResponseHeight)
                })
            }
            function onHistoryChanged() {
                // Save height before updating — this is where the new response will start
                conversationOverlay._preResponseHeight = conversationText.contentHeight
                conversationText.text = MainController.aiManager?.conversation?.getConversationText() ?? ""
                // Scroll to bottom to show user's message / thinking indicator
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                })
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
