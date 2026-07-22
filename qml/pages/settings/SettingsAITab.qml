import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: aiTab
    textFields: [apiKeyField, ollamaEndpointField, openrouterModelField, customUrlField, claudeRcUrlField, providerEndpointField]
    targetFlickable: aiFlickable

    property string testResultMessage: ""
    property bool testResultSuccess: false

    // modelDisplayName()/availableModels() are non-reactive invokables. Bump
    // this on configurationChanged (which fires after the model is applied) and
    // reference it in those bindings so provider-card subtitles refresh when the
    // selected model changes.
    property int configTick: 0
    Connections {
        target: MainController.aiManager
        function onConfigurationChanged() { aiTab.configTick++ }
    }

    // Helper function to check if provider has a key configured
    function isProviderConfigured(providerId) {
        switch(providerId) {
            case "openai": return Settings.ai.openaiApiKey.length > 0
            case "anthropic": return Settings.ai.anthropicApiKey.length > 0
            case "gemini": return Settings.ai.geminiApiKey.length > 0
            case "openrouter": return Settings.ai.openrouterApiKey.length > 0 && Settings.ai.openrouterModel.length > 0
            case "ollama": return Settings.ai.ollamaEndpoint.length > 0 && Settings.ai.ollamaModel.length > 0
            default: return false
        }
    }

    // Discuss Shot app display names (index matches Settings.network.discussShotApp)
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
        color: Theme.cardBackgroundColor
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

                                property bool isSelected: Settings.ai.aiProvider === modelData.id
                                property bool hasKey: aiTab.isProviderConfigured(modelData.id)

                                color: {
                                    if (isSelected) return Theme.primaryColor
                                    if (hasKey) return Qt.rgba(0.2, 0.7, 0.3, 0.25)
                                    // Unconfigured state is meant to blend into the page
                                    // backdrop. With a flat Theme.backgroundColor page that
                                    // meant matching it exactly; with a background image
                                    // active there's no flat color to match, so scrim
                                    // instead — same tinted-glass look as the "hasKey"
                                    // branch above, at Theme's shared scrim alpha.
                                    return Theme.insetBackgroundColor
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
                                        text: {
                                            aiTab.configTick  // dependency: refresh on model change
                                            return MainController.aiManager ? MainController.aiManager.modelDisplayName(modelData.id) : ""
                                        }
                                        font.pixelSize: Theme.scaled(11)
                                        color: isSelected ? Qt.rgba(1,1,1,0.8) : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                }

                                MouseArea {
                                    id: providerArea
                                    anchors.fill: parent
                                    onClicked: Settings.ai.aiProvider = modelData.id
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
                    visible: Settings.ai.aiProvider !== "ollama"
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
                            switch(Settings.ai.aiProvider) {
                                case "openai": return Settings.ai.openaiApiKey
                                case "anthropic": return Settings.ai.anthropicApiKey
                                case "gemini": return Settings.ai.geminiApiKey
                                case "openrouter": return Settings.ai.openrouterApiKey
                                default: return ""
                            }
                        }
                        onTextChanged: {
                            switch(Settings.ai.aiProvider) {
                                case "openai": Settings.ai.openaiApiKey = text; break
                                case "anthropic": Settings.ai.anthropicApiKey = text; break
                                case "gemini": Settings.ai.geminiApiKey = text; break
                                case "openrouter": Settings.ai.openrouterApiKey = text; break
                            }
                        }
                    }

                    Text {
                        text: {
                            var getKey = TranslationManager.translate("settings.ai.getkey", "Get key:")
                            switch(Settings.ai.aiProvider) {
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

                // Model selection (providers exposing a fixed catalog of >1 model).
                // Generic: any provider whose availableModels() returns multiple
                // entries lights this up automatically — no per-provider wiring.
                ColumnLayout {
                    id: modelSelect
                    // Re-evaluated when the active provider changes (the binding
                    // references Settings.ai.aiProvider).
                    property var options: MainController.aiManager
                        ? MainController.aiManager.availableModels(Settings.ai.aiProvider)
                        : []
                    property string currentProvider: Settings.ai.aiProvider
                    visible: options.length > 1
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    // Index of the stored selection; default to 0 (the recommended
                    // first entry) when unset or stale, without writing it back.
                    function selectedIndex() {
                        var sel = Settings.ai.providerModel(currentProvider)
                        for (var i = 0; i < options.length; i++) {
                            if (options[i].id === sel) return i
                        }
                        return 0
                    }

                    // StyledComboBox assigns currentIndex imperatively on user
                    // selection, which severs the declarative binding below. Re-arm
                    // it on every provider switch so the combo tracks the stored
                    // model for whichever provider is now showing (matters once a
                    // second multi-model provider exists).
                    onCurrentProviderChanged: modelCombo.currentIndex = Qt.binding(modelSelect.selectedIndex)

                    Tr {
                        key: "settings.ai.model"
                        fallback: "Model"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }

                    StyledComboBox {
                        id: modelCombo
                        Layout.fillWidth: true
                        model: modelSelect.options
                        textRole: "name"
                        accessibleLabel: TranslationManager.translate("settings.ai.modelAccessible", "AI model")
                        currentIndex: modelSelect.selectedIndex()
                        // onActivated fires only on user selection, so programmatic
                        // currentIndex changes (provider switch) never write back.
                        onActivated: function(index) {
                            if (index >= 0 && index < modelSelect.options.length) {
                                Settings.ai.setProviderModel(modelSelect.currentProvider,
                                                             modelSelect.options[index].id)
                            }
                        }
                    }

                    // Provider-specific guidance. The English copy lives in
                    // AIProvider::modelHint() (next to the model catalog) so the
                    // app and the ShotServer web settings page share one source;
                    // the per-provider translation key keeps it translatable.
                    // The key is built dynamically, which the QML string scanner
                    // cannot see -- main.cpp registers these keys at startup so
                    // the batch-translation registry stays complete.
                    Text {
                        visible: text.length > 0
                        text: {
                            var hint = MainController.aiManager
                                ? MainController.aiManager.modelHint(modelSelect.currentProvider) : ""
                            if (hint === "") return ""
                            return TranslationManager.translate(
                                "settings.ai.modelHint." + modelSelect.currentProvider, hint)
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // OpenRouter model settings
                ColumnLayout {
                    visible: Settings.ai.aiProvider === "openrouter"
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
                        text: Settings.ai.openrouterModel
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        onTextChanged: Settings.ai.openrouterModel = text
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
                    visible: Settings.ai.aiProvider === "ollama"
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
                        text: Settings.ai.ollamaEndpoint
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onTextChanged: Settings.ai.ollamaEndpoint = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        StyledComboBox {
                            Layout.fillWidth: true
                            model: MainController.aiManager ? MainController.aiManager.ollamaModels : []
                            currentIndex: model ? model.indexOf(Settings.ai.ollamaModel) : -1
                            onCurrentTextChanged: if (currentText) Settings.ai.ollamaModel = currentText
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
                    visible: Settings.ai.aiProvider !== "ollama"
                    text: {
                        var perShot = TranslationManager.translate("settings.ai.pershot", "shot")
                        switch(Settings.ai.aiProvider) {
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
                    visible: Settings.ai.aiProvider === "ollama"
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

                    AccessibleButton {
                        visible: Settings.ai.aiProvider === "openai" || Settings.ai.aiProvider === "anthropic"
                        text: TranslationManager.translate("settings.ai.advanced", "Advanced")
                        accessibleName: TranslationManager.translate("settings.ai.advancedAccessible", "Configure custom API endpoint")
                        onClicked: {
                            endpointField.text = Settings.ai.aiProvider === "openai"
                                ? Settings.ai.openaiEndpoint
                                : Settings.ai.anthropicEndpoint
                            advancedEndpointDialog.open()
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

                // Taste intake toggle (add-ai-taste-intake)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.ai.tasteIntake"
                            fallback: "Ask how it tasted"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }

                        Tr {
                            key: "settings.ai.tasteIntakeDesc"
                            fallback: "When opening the assistant for a shot, first show a quick tap-only taste panel (Sour/Balanced/Bitter, body, rating) instead of the keyboard. Turn off to open the chat directly."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    StyledSwitch {
                        checked: Settings.ai.tasteIntakeOnAsk
                        accessibleName: TranslationManager.translate("settings.ai.tasteIntakeAccessible", "Ask how the shot tasted before chatting")
                        onToggled: Settings.ai.tasteIntakeOnAsk = checked
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
                        checked: Settings.mcp.mcpEnabled
                        accessibleName: TranslationManager.translate("settings.ai.mcp.enableAccessible", "Enable MCP server for AI remote control")
                        onCheckedChanged: Settings.mcp.mcpEnabled = checked
                    }
                }

                // Setup page link (visible only when the shot server is actually
                // listening — `shotServer.url` returns empty until then, and a
                // bare "/mcp/setup" gets resolved against the qrc base by
                // Qt.openUrlExternally on macOS, popping a "no application"
                // dialog instead of opening the browser).
                ColumnLayout {
                    readonly property string mcpSetupUrl: (Settings.mcp.mcpEnabled && MainController.shotServer
                        && MainController.shotServer.url.length > 0)
                        ? MainController.shotServer.url + "/mcp/setup" : ""

                    visible: mcpSetupUrl.length > 0
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.setupPageLabel", "Setup page:") + " "
                            + parent.mcpSetupUrl
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
                            onClicked: {
                                if (parent.parent.mcpSetupUrl.length > 0)
                                    Qt.openUrlExternally(parent.parent.mcpSetupUrl)
                            }
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
                    enabled: Settings.mcp.mcpEnabled
                    opacity: Settings.mcp.mcpEnabled ? 1.0 : 0.5

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
                            color: Settings.mcp.mcpAccessLevel === modelData.level ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: Settings.mcp.mcpAccessLevel === modelData.level ? Theme.primaryColor : Theme.borderColor
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
                                onAccessibleClicked: Settings.mcp.mcpAccessLevel = modelData.level
                            }
                        }
                    }
                }

                // Confirmation Level
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    enabled: Settings.mcp.mcpEnabled && Settings.mcp.mcpAccessLevel > 0
                    opacity: (Settings.mcp.mcpEnabled && Settings.mcp.mcpAccessLevel > 0) ? 1.0 : 0.5

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
                            color: Settings.mcp.mcpConfirmationLevel === modelData.level ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: Settings.mcp.mcpConfirmationLevel === modelData.level ? Theme.primaryColor : Theme.borderColor
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
                                onAccessibleClicked: Settings.mcp.mcpConfirmationLevel = modelData.level
                            }
                        }
                    }
                }

                // MCP Status line
                Text {
                    visible: Settings.mcp.mcpEnabled
                    text: {
                        var status = TranslationManager.translate("settings.ai.mcp.status.listening", "Listening on port %1").arg(Settings.network.shotServerPort)
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
                    visible: !Settings.mcp.mcpEnabled
                    text: TranslationManager.translate("settings.ai.mcp.status.disabled", "MCP server is disabled")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                }

                // ─── Remote Access (mobile connectors) subsection ───
                Item { height: Theme.scaled(8) }

                Tr {
                    key: "settings.ai.remoteMcp.title"
                    fallback: "Remote Access (Mobile Connectors)"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                }

                Tr {
                    key: "settings.ai.remoteMcp.description"
                    fallback: "Reach this MCP server from Claude or ChatGPT mobile apps over the public internet, through a reverse proxy or tunnel you run. The connector URL below carries a secret token — treat it like a password."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Enable remote access toggle (requires the MCP server itself to be on)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    enabled: Settings.mcp.mcpEnabled
                    opacity: Settings.mcp.mcpEnabled ? 1.0 : 0.5

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.ai.remoteMcp.enable"
                            fallback: "Enable Remote Access"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                        }
                        Tr {
                            key: "settings.ai.remoteMcp.enableHint"
                            fallback: "Off by default. Serves only the tokenized MCP route — no other web pages are exposed."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    StyledSwitch {
                        checked: Settings.mcp.remoteMcpEnabled
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.enableAccessible", "Enable remote MCP access for mobile connectors")
                        onCheckedChanged: Settings.mcp.remoteMcpEnabled = checked
                    }
                }

                // Remote-access configuration (visible only when enabled)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: Settings.mcp.mcpEnabled && Settings.mcp.remoteMcpEnabled

                    // Reachability mode selector
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        Tr {
                            key: "settings.ai.remoteMcp.modeLabel"
                            fallback: "How is it reachable?"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                        }

                        Repeater {
                            model: [
                                {
                                    mode: "custom",
                                    label: TranslationManager.translate("settings.ai.remoteMcp.mode.custom", "Custom URL (bring your own)"),
                                    detail: TranslationManager.translate("settings.ai.remoteMcp.mode.customDesc", "You run a reverse proxy / tunnel that forwards to this device."),
                                    enabled: true
                                },
                                {
                                    mode: "tailscale",
                                    label: TranslationManager.translate("settings.ai.remoteMcp.mode.tailscale", "Tailscale (built-in)"),
                                    detail: RemoteMcpAccess.tunnelAvailable
                                        ? TranslationManager.translate("settings.ai.remoteMcp.mode.tailscaleDesc", "Join your tailnet and expose a public Funnel URL — no other setup.")
                                        : TranslationManager.translate("settings.ai.remoteMcp.mode.tailscaleUnavailable", "Not included in this build."),
                                    enabled: RemoteMcpAccess.tunnelAvailable
                                }
                            ]
                            delegate: Rectangle {
                                id: modeDelegate
                                Layout.fillWidth: true
                                Layout.preferredHeight: modeCol.implicitHeight + Theme.scaled(16)
                                radius: Theme.scaled(6)
                                opacity: modelData.enabled ? 1.0 : 0.5
                                color: Settings.mcp.remoteMcpMode === modelData.mode ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                                border.color: Settings.mcp.remoteMcpMode === modelData.mode ? Theme.primaryColor : Theme.borderColor
                                border.width: 1
                                Accessible.ignored: true

                                ColumnLayout {
                                    id: modeCol
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
                                    enabled: modelData.enabled
                                    accessibleName: modelData.label + ". " + modelData.detail
                                    accessibleItem: modeDelegate
                                    onAccessibleClicked: if (modelData.enabled) Settings.mcp.remoteMcpMode = modelData.mode
                                }
                            }
                        }
                    }

                    // Public base URL (Mode C — bring your own)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)
                        visible: Settings.mcp.remoteMcpMode === "custom"

                        Tr {
                            key: "settings.ai.remoteMcp.baseUrlLabel"
                            fallback: "Public base URL"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                        }
                        Text {
                            // Not a Tr: the %1 port needs .arg() substitution,
                            // which Tr does not perform (mirrors the status line).
                            text: TranslationManager.translate("settings.ai.remoteMcp.baseUrlHint",
                                "The https:// address your proxy/tunnel exposes, forwarding to this tablet on port %1. Example: https://decenza.your-tailnet.ts.net")
                                .arg(Settings.mcp.remoteMcpPort)
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        StyledTextField {
                            id: remoteMcpBaseUrlField
                            Layout.fillWidth: true
                            placeholder: "https://decenza.your-tailnet.ts.net"
                            text: Settings.mcp.remoteMcpCustomBaseUrl
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                            onTextChanged: Settings.mcp.remoteMcpCustomBaseUrl = text
                        }
                        Text {
                            // Shown whenever no usable connector URL composes —
                            // blank field or a non-https/invalid base URL — so an
                            // "active" listener is never left without an
                            // explanation of why there's no URL to copy.
                            visible: RemoteMcpAccess.connectorUrl.length === 0
                            text: TranslationManager.translate("settings.ai.remoteMcp.baseUrlInvalid", "Enter a full https:// URL to get your connector URL.")
                            color: Theme.errorColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // Tailscale login (Mode A) — shown while the embedded node is
                    // waiting for the user to authorize it.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: Settings.mcp.remoteMcpMode === "tailscale"
                            && RemoteMcpAccess.loginUrl.length > 0

                        Tr {
                            key: "settings.ai.remoteMcp.tailscaleLoginLabel"
                            fallback: "Sign in to Tailscale"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                        }
                        Tr {
                            key: "settings.ai.remoteMcp.tailscaleLoginHint"
                            fallback: "Open this link (or scan the code) to authorize this device on your tailnet. You may also need to approve Funnel for it in the Tailscale admin console."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)
                            Text {
                                Layout.fillWidth: true
                                text: RemoteMcpAccess.loginUrl
                                color: Theme.accentColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WrapAnywhere
                                Accessible.role: Accessible.Link
                                Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.tailscaleLoginAccessible", "Open Tailscale login page")
                                Accessible.focusable: true
                                MouseArea {
                                    id: tsLoginArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(RemoteMcpAccess.loginUrl)
                                }
                                Accessible.onPressAction: tsLoginArea.clicked(null)
                            }
                            AccessibleButton {
                                text: TranslationManager.translate("common.button.copy", "Copy")
                                accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleLoginCopy", "Copy Tailscale login URL")
                                onClicked: MainController.copyToClipboard(RemoteMcpAccess.loginUrl)
                            }
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: Theme.scaled(180)
                            height: Theme.scaled(180)
                            color: "#ffffff"
                            radius: Theme.scaled(8)
                            Accessible.role: Accessible.Graphic
                            Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.tailscaleLoginQr", "QR code of the Tailscale login URL")
                            QrCode {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(8)
                                value: RemoteMcpAccess.loginUrl
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Connector URL + QR + copy (visible once a valid URL composes)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: RemoteMcpAccess.connectorUrl.length > 0

                        Tr {
                            key: "settings.ai.remoteMcp.connectorUrlLabel"
                            fallback: "Connector URL"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Text {
                                Layout.fillWidth: true
                                text: RemoteMcpAccess.connectorUrl
                                color: Theme.accentColor
                                font.pixelSize: Theme.scaled(12)
                                wrapMode: Text.WrapAnywhere
                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.connectorUrlAccessible", "Connector URL. Copy this into your AI app's custom connector settings.")
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("common.button.copy", "Copy")
                                accessibleName: TranslationManager.translate("settings.ai.remoteMcp.copyAccessible", "Copy connector URL to clipboard")
                                onClicked: {
                                    MainController.copyToClipboard(RemoteMcpAccess.connectorUrl)
                                    AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.copied", "Connector URL copied"))
                                }
                            }
                        }

                        // QR code for configuring the connector on claude.ai
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: Theme.scaled(180)
                            height: Theme.scaled(180)
                            color: "#ffffff"
                            radius: Theme.scaled(8)
                            Accessible.role: Accessible.Graphic
                            Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.qrAccessible", "QR code of the connector URL. Use the Copy button if you cannot scan it.")

                            QrCode {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(8)
                                value: RemoteMcpAccess.connectorUrl
                                Accessible.ignored: true
                            }
                        }

                        Tr {
                            key: "settings.ai.remoteMcp.setupGuidance"
                            fallback: "Add this as a custom connector in Claude, then paste the URL above (leave the OAuth fields blank). Mobile Claude apps sync connectors automatically."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: TranslationManager.translate("settings.ai.remoteMcp.openClaudeConnectors", "Open Connectors on claude.ai ↗")
                            color: Theme.accentColor
                            font.pixelSize: Theme.scaled(12)
                            font.underline: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Link
                            Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.openClaudeConnectorsAccessible", "Open the Connectors settings page on claude.ai in your browser")
                            Accessible.focusable: true
                            MouseArea {
                                id: claudeConnectorsArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("https://claude.ai/settings/connectors")
                            }
                            Accessible.onPressAction: claudeConnectorsArea.clicked(null)
                        }
                    }

                    // Status line
                    Text {
                        Layout.fillWidth: true
                        text: {
                            switch (RemoteMcpAccess.statusString) {
                            case "active":
                                return Settings.mcp.remoteMcpMode === "tailscale"
                                    ? TranslationManager.translate("settings.ai.remoteMcp.status.activeTailscale", "Active — public Funnel URL ready")
                                    : TranslationManager.translate("settings.ai.remoteMcp.status.active", "Active — listening on port %1").arg(RemoteMcpAccess.listenPort)
                            case "starting":
                                return TranslationManager.translate("settings.ai.remoteMcp.status.starting", "Starting…")
                            case "reconnecting":
                                return TranslationManager.translate("settings.ai.remoteMcp.status.reconnecting", "Reconnecting…")
                            case "error":
                                return TranslationManager.translate("settings.ai.remoteMcp.status.error", "Error: %1").arg(RemoteMcpAccess.statusDetail)
                            default:
                                return TranslationManager.translate("settings.ai.remoteMcp.status.off", "Off")
                            }
                        }
                        color: RemoteMcpAccess.statusString === "error" ? Theme.errorColor
                             : RemoteMcpAccess.statusString === "active" ? Theme.successColor
                             : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        font.bold: RemoteMcpAccess.statusString === "active"
                        wrapMode: Text.WordWrap
                    }

                    // One-time Tailscale setup — opens a step-by-step popup. Hidden
                    // once the connector is active (setup is done), so it doesn't
                    // linger after everything is working.
                    AccessibleButton {
                        visible: Settings.mcp.remoteMcpMode === "tailscale"
                            && RemoteMcpAccess.statusString !== "active"
                        text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSetupButton", "Set up Tailscale Funnel (one-time)…")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSetupAccessible", "Open Tailscale Funnel setup instructions")
                        warning: RemoteMcpAccess.statusString === "error"
                        onClicked: tailscaleSetupDialog.open()
                    }

                    // Rotate token (revokes the current URL)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(12)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.ai.remoteMcp.rotateLabel"
                                fallback: "Rotate token"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                                font.bold: true
                            }
                            Tr {
                                key: "settings.ai.remoteMcp.rotateHint"
                                fallback: "Generates a new URL and immediately disconnects anything using the old one. Update the connector on claude.ai afterwards."
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.remoteMcp.rotateButton", "Rotate")
                            accessibleName: TranslationManager.translate("settings.ai.remoteMcp.rotateAccessible", "Rotate remote access token")
                            destructive: true
                            onClicked: rotateTokenDialog.open()
                        }
                    }

                    // Sign out of Tailscale — wipes the embedded node's identity so
                    // a stored nodekey bound to the wrong/deleted tailnet can't keep
                    // looping the login ("device already exists" / 403). Kept at the
                    // panel level (not nested under the login/connector blocks) so it
                    // stays reachable while the node is still waiting for login.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(12)
                        visible: Settings.mcp.remoteMcpMode === "tailscale"
                            && RemoteMcpAccess.tunnelAvailable

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.ai.remoteMcp.tailscaleSignout.label"
                                fallback: "Sign out of Tailscale"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                                font.bold: true
                            }
                            Tr {
                                key: "settings.ai.remoteMcp.tailscaleSignout.hint"
                                fallback: "Clears this device's tailnet identity. Use this if login keeps failing (\"device already exists\" or a 403 error) — then sign in again with the account you want."
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(11)
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.button", "Sign out")
                            accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.accessible", "Sign out of Tailscale and clear this device's tailnet identity")
                            destructive: true
                            onClicked: tailscaleSignoutDialog.open()
                        }
                    }
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
                            text: aiTab.discussAppNames[Settings.network.discussShotApp] ?? aiTab.discussAppNames[0]
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("settings.ai.discuss.selectApp", "Select AI app for discussing shots") + ". " + (aiTab.discussAppNames[Settings.network.discussShotApp] ?? aiTab.discussAppNames[0])
                            accessibleItem: discussAppButton
                            onAccessibleClicked: discussAppDialog.open()
                        }
                    }
                }

                // Custom URL field (only when Custom URL is selected)
                ColumnLayout {
                    visible: Settings.network.discussShotApp === 5
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    StyledTextField {
                        id: customUrlField
                        Layout.fillWidth: true
                        placeholder: "https://localhost:8080"
                        text: Settings.network.discussShotCustomUrl
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onTextChanged: Settings.network.discussShotCustomUrl = text
                    }
                }

                // Claude Desktop (Remote Control) setup — visible when Claude Desktop is selected
                ColumnLayout {
                    visible: Settings.network.discussShotApp === Settings.network.discussAppClaudeDesktop
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
                        text: Settings.network.claudeRcSessionUrl
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onTextChanged: Settings.network.claudeRcSessionUrl = text
                    }
                }

                // Spacer to push content up
                Item { Layout.fillHeight: true }
            }
        }
    }

    // Discuss Shot app selector
    // Advanced endpoint dialog (OpenAI / Anthropic custom endpoint)
    Dialog {
        id: advancedEndpointDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(360), Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(endpointDialogTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                id: endpointDialogTitle
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.customEndpoint", "Custom Endpoint")
                color: Theme.textColor
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.customEndpointHint", "Leave empty for default. Set to use an OpenAI/Anthropic-compatible API endpoint.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.Wrap
            }

            StyledTextField {
                id: endpointField
                Layout.fillWidth: true
                placeholderText: Settings.ai.aiProvider === "openai"
                    ? "https://api.openai.com"
                    : "https://api.anthropic.com"
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: advancedEndpointDialog.close()
                }
                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.button.save", "Save")
                    onClicked: {
                        Qt.inputMethod.commit()
                        switch(Settings.ai.aiProvider) {
                            case "openai": Settings.ai.openaiEndpoint = endpointField.text; break
                            case "anthropic": Settings.ai.anthropicEndpoint = endpointField.text; break
                        }
                        advancedEndpointDialog.close()
                    }
                }
            }
        }
    }

    // MCP Help Dialog
    // Confirm rotating the remote-access token (revokes the current URL).
    Dialog {
        id: rotateTokenDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(360), Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(rotateTokenTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                id: rotateTokenTitle
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.remoteMcp.rotateConfirmTitle", "Rotate remote access token?")
                color: Theme.textColor
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.remoteMcp.rotateConfirmBody", "The current connector URL will stop working immediately and any connected AI app will be disconnected. You will need to update the connector on claude.ai with the new URL.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: rotateTokenDialog.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("settings.ai.remoteMcp.rotateButton", "Rotate")
                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.rotateAccessible", "Rotate remote access token")
                    destructive: true
                    onClicked: {
                        RemoteMcpAccess.rotateToken()
                        rotateTokenDialog.close()
                        AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.rotated", "Token rotated. New connector URL generated."))
                    }
                }
            }
        }
    }

    // Confirm signing out of Tailscale (wipes the embedded node's identity).
    Dialog {
        id: tailscaleSignoutDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(360), Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(tailscaleSignoutTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                id: tailscaleSignoutTitle
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.confirmTitle", "Sign out of Tailscale?")
                color: Theme.textColor
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.confirmBody", "This clears this device's Tailscale identity. Remote access stops working until you sign in again, and you'll get a fresh login link. Use this to recover from a login that keeps failing.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: tailscaleSignoutDialog.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.button", "Sign out")
                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.accessible", "Sign out of Tailscale and clear this device's tailnet identity")
                    destructive: true
                    onClicked: {
                        var wiped = RemoteMcpAccess.forgetTailscale()
                        tailscaleSignoutDialog.close()
                        AccessibilityManager.announce(wiped
                            ? TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.done", "Signed out of Tailscale. Sign in again to use remote access.")
                            : TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.failed", "Could not clear the Tailscale identity. Please try again."))
                    }
                }
            }
        }
    }

    // Step-by-step Tailscale Funnel setup instructions.
    Dialog {
        id: tailscaleSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(480), Theme.scaled(520))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        readonly property string aclSnippet:
            '"nodeAttrs": [\n  { "target": ["autogroup:member"], "attr": ["funnel"] },\n],'

        onOpened: AccessibilityManager.announce(tsSetupTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Flickable {
            implicitHeight: Math.min(tsSetupCol.implicitHeight,
                tailscaleSetupDialog.parent ? tailscaleSetupDialog.parent.height - Theme.scaled(120) : Theme.scaled(560))
            contentHeight: tsSetupCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: tsSetupCol
                width: parent.width
                spacing: Theme.scaled(12)

                Text {
                    id: tsSetupTitle
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.title", "Set up Tailscale Funnel")
                    color: Theme.textColor
                    font.family: Theme.subtitleFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.intro", "A one-time setup on your Tailscale account lets Decenza expose a public URL for AI connectors. Do both steps in the Tailscale admin console — then come back, Decenza connects automatically (no restart).")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                // Step 1 — HTTPS certificates
                Tr {
                    key: "settings.ai.remoteMcp.setup.step1"
                    fallback: "1.  Enable HTTPS certificates"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.step1body", "On the DNS page, scroll to the bottom and turn on “HTTPS Certificates”.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }
                AccessibleButton {
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.openDns", "Open DNS settings ↗")
                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.setup.openDnsAccessible", "Open Tailscale DNS settings in browser")
                    onClicked: Qt.openUrlExternally("https://login.tailscale.com/admin/dns")
                }

                // Step 2 — Funnel node attribute
                Tr {
                    key: "settings.ai.remoteMcp.setup.step2"
                    fallback: "2.  Allow Funnel for this device"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.step2body", "Open Access controls and click “JSON editor” at the top. On the right, open the “Funnel” panel and click “Add Funnel to policy”, then Save (the red button). No typing needed.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.step2fallback", "Don’t see it? Paste this rule after the first “{” in the JSON editor instead, then Save:")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.backgroundColor
                    border.color: Theme.borderColor
                    border.width: 1
                    radius: Theme.scaled(6)
                    Layout.preferredHeight: tsSnippetText.implicitHeight + Theme.scaled(16)
                    Text {
                        id: tsSnippetText
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        text: tailscaleSetupDialog.aclSnippet
                        color: Theme.textColor
                        // Real family names, not the generic "monospace" alias:
                        // no platform provides a family by that name, so Qt ran
                        // a full font-alias sweep (~66 ms, with a warning) and
                        // fell back to a host font anyway. Ordered macOS /
                        // Windows / Linux; Qt takes the first that exists.
                        //
                        // Built with Qt.font() rather than `font.families:`,
                        // which is not assignable as a grouped property — that
                        // spelling fails at load with "Cannot assign to
                        // non-existent property". Same pattern as Theme.qml.
                        font: Qt.font({
                            families: ["Menlo", "Consolas", "DejaVu Sans Mono", "Courier New"],
                            pixelSize: Theme.scaled(11)
                        })
                        wrapMode: Text.WrapAnywhere
                        Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.setup.snippetAccessible", "Tailscale ACL rule to grant Funnel")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.remoteMcp.setup.copyRule", "Copy rule")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.setup.copyRuleAccessible", "Copy the Tailscale Funnel ACL rule to clipboard")
                        onClicked: {
                            MainController.copyToClipboard(tailscaleSetupDialog.aclSnippet)
                            AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.setup.copied", "Rule copied"))
                        }
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.remoteMcp.setup.openAcls", "Open Access controls ↗")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.setup.openAclsAccessible", "Open Tailscale access controls in browser")
                        onClicked: Qt.openUrlExternally("https://login.tailscale.com/admin/acls")
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: Theme.scaled(4) }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    AccessibleButton {
                        text: TranslationManager.translate("common.button.done", "Done")
                        accessibleName: TranslationManager.translate("common.button.done", "Done")
                        primary: true
                        onClicked: tailscaleSetupDialog.close()
                    }
                }
            }
        }
    }

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
                        // Mirror the guard on the inline setup-page link: only show
                        // when the shot server is actually listening, otherwise
                        // `shotServer.url` is empty and Qt.openUrlExternally("/mcp/setup")
                        // gets resolved against the qrc base on macOS.
                        visible: Settings.mcp.mcpEnabled && MainController.shotServer
                            && MainController.shotServer.url.length > 0
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
                        accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
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
        currentIndex: Settings.network.discussShotApp
        onSelected: function(index, value) { Settings.network.discussShotApp = index }
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
                        // Markdown -> HTML, then emoji: raw emoji would reach the platform
                        // colour renderer (the macOS render-thread crash), and injecting <img>
                        // before the parse would truncate the reply. See markdownrenderer.h.
                        text: Theme.replaceEmojiWithImg(
                                  MarkdownRenderer.toHtml(MainController.aiManager?.conversation?.getConversationText() ?? ""),
                                  Theme.bodyFont.pixelSize, true)
                        // RichText: the binding above produces HTML, not markdown. See
                        // markdownrenderer.h — MarkdownText here would truncate at the first emoji.
                        textFormat: Text.RichText
                        wrapMode: TextEdit.WordWrap
                        readOnly: true
                        selectByMouse: true
                        font: Theme.bodyFont
                        color: Theme.textColor
                        background: null
                        padding: 0

                        Accessible.role: Accessible.EditableText
                        Accessible.name: TranslationManager.translate("conversation.accessible.transcript", "AI conversation transcript")
                        // toAccessibleText: `text` is HTML now, not markdown.
                        Accessible.description: Theme.toAccessibleText(text)
                        Accessible.focusable: true
                        activeFocusOnTab: true

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
                            Qt.inputMethod.commit()
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
        property bool _waitingForResponse: false
        Connections {
            target: MainController.aiManager?.conversation ?? null
            function onResponseReceived() {
                conversationOverlay._waitingForResponse = false
                // Scroll to top of the new response
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationOverlay._preResponseHeight)
                    // Mobile render thread can stay asleep when text updates from a
                    // network reply — force a scene graph repaint so the reply
                    // appears without requiring a touch to wake the render loop.
                    conversationText.update()
                    conversationFlickable.update()
                })
            }
            function onErrorOccurred() {
                conversationOverlay._waitingForResponse = false
            }
            function onHistoryChanged() {
                // Only save the scroll target when the user sends (before response arrives).
                // The response also triggers historyChanged, but we handle that in onResponseReceived.
                if (!conversationOverlay._waitingForResponse) {
                    conversationOverlay._preResponseHeight = conversationText.contentHeight
                    conversationOverlay._waitingForResponse = true
                }
                conversationText.text = MainController.aiManager?.conversation?.getConversationText() ?? ""
                // Scroll to bottom to show user's message / thinking indicator
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                    conversationText.update()
                    conversationFlickable.update()
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
