// The provider-tile Repeater delegate reads this file's `aiTab` id; Bound makes it
// statically resolvable. It and the three MCP option-list delegates (access level,
// confirmation level, remote mode) each declare their one injected role, `modelData`,
// required in the same edit -- without that, Bound stops role injection and all four
// option lists render blank at RUNTIME, silently. (Those three read no file id; they
// only need their role.)
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

KeyboardAwareContainer {
    id: aiTab
    textFields: [apiKeyField, ollamaEndpointField, customUrlField, claudeRcUrlField]
    targetFlickable: aiFlickable

    property string testResultMessage: ""
    property bool testResultSuccess: false
    readonly property var aiState: AISettingsService.snapshot
    readonly property var preferences: SettingsPreferencesService.preferences
    property string remoteConnectorUrl: ""
    property string pendingMcpUrlRequestId: ""
    property string pendingRotateRequestId: ""
    property string lastMcpUrlSignature: ""
    property string pendingTestRequestId: ""
    property string pendingOllamaRequestId: ""
    property string pendingClipboardRequestId: ""
    property string pendingClipboardText: ""
    property string pendingConversationSendRequestId: ""
    property string pendingConversationSendText: ""
    property string pendingConversationLoadRequestId: ""
    property string conversationRequestError: ""
    readonly property bool browserHasUnsavedChanges: conversationInput.text.length > 0
        || conversationInput.preeditText.length > 0 || pendingConversationSendRequestId.length > 0

    function discardConversationDraft() {
        if (pendingConversationSendRequestId.length > 0) return
        Keyboard.commit()
        conversationInput.text = ""
        conversationRequestError = ""
    }

    function sendConversationMessage() {
        Keyboard.commit()
        if (!ConversationSession.ready || ConversationSession.busy
                || pendingConversationSendRequestId.length > 0
                || conversationInput.text.trim().length === 0) return false
        conversationRequestError = ""
        pendingConversationSendText = conversationInput.text
        pendingConversationSendRequestId = ConversationSession.request("sendMessage",
            {text: pendingConversationSendText})
        if (pendingConversationSendRequestId.length === 0) {
            pendingConversationSendText = ""
            return false
        }
        conversationInput.text = ""
        return true
    }

    function completeConversationSend(requestId, operation, success) {
        if (operation !== "sendMessage" || requestId !== pendingConversationSendRequestId)
            return false
        pendingConversationSendRequestId = ""
        if (!success) {
            if (conversationInput.text.length === 0 && conversationInput.preeditText.length === 0)
                conversationInput.text = pendingConversationSendText
            conversationRequestError = TranslationManager.translate("conversation.operationFailed",
                "Could not complete the conversation request. Please try again.")
            AccessibilityManager.announce(conversationRequestError, true)
        }
        pendingConversationSendText = ""
        return true
    }

    ExternalUrlOpener { id: externalUrlOpener }
    StatusToast { id: preferenceError; error: true }

    PreferenceWrite {
        id: aiPreferenceWrite
        domain: "ai"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: endpointPreferenceWrite
        domain: "ai"
        onCompleted: function(success) {
            if (success) advancedEndpointDialog.close()
            else expectedRevision = SettingsPreferencesService.revisions.ai
        }
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: mcpPreferenceWrite
        domain: "mcp"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: networkPreferenceWrite
        domain: "aiNetwork"
        onFailed: function(message) { preferenceError.show(message) }
    }

    function openExternal(url) {
        const reservation = externalUrlOpener.reserve()
        if (!reservation || !externalUrlOpener.openReserved(reservation, url))
            preferenceError.show(TranslationManager.translate("common.openLinkFailed", "Could not open this link."))
    }

    function copyText(value) {
        const requestId = LocalClipboard.newRequestId()
        pendingClipboardRequestId = requestId
        pendingClipboardText = value
        if (!LocalClipboard.writeText(requestId, value)) {
            pendingClipboardRequestId = ""
            pendingClipboardText = ""
            preferenceError.show(TranslationManager.translate("common.copyFailed", "Could not copy to the clipboard."))
            return false
        }
        return true
    }

    function pref(name, fallback, domain) {
        const key = (domain || "ai") + "." + name
        const value = preferences[key]
        return value === undefined ? fallback : value
    }

    function write(domain, name, value) {
        const writer = domain === "ai" ? aiPreferenceWrite
                     : domain === "mcp" ? mcpPreferenceWrite
                     : networkPreferenceWrite
        if (!writer.ready) {
            preferenceError.show(TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet."))
            return false
        }
        return writer.submit(name, value)
    }

    function providerModel(providerId) {
        const models = preferences["ai.providerModels"] || {}
        return models[providerId] || ""
    }

    function refreshMcpUrls() {
        const enabled = pref("remoteMcpEnabled", false, "mcp")
        if (!enabled) {
            remoteConnectorUrl = ""
            lastMcpUrlSignature = ""
            return
        }
        if (!AISettingsService.ready) {
            lastMcpUrlSignature = ""
            return
        }
        const signature = [pref("remoteMcpMode", "custom", "mcp"),
                           pref("remoteMcpCustomBaseUrl", "", "mcp"),
                           aiState.remoteMcpStatus || "",
                           aiState.remoteUrlRevision || 0].join("|")
        if (signature === lastMcpUrlSignature || pendingMcpUrlRequestId.length > 0)
            return
        lastMcpUrlSignature = signature
        pendingMcpUrlRequestId = AISettingsService.request("requestMcpUrls")
    }

    function submitProviderKey(value) {
        const provider = pref("aiProvider", "openai")
        const key = provider === "openai" ? "openaiApiKey"
                  : provider === "anthropic" ? "anthropicApiKey"
                  : provider === "gemini" ? "geminiApiKey"
                  : provider === "openrouter" ? "openrouterApiKey" : ""
        if (key.length === 0 || !value)
            return false
        const accepted = write("ai", key, value)
        if (accepted) apiKeyField.text = ""
        return accepted
    }

    // Helper function to check if provider has a key configured
    function isProviderConfigured(providerId) {
        switch(providerId) {
            case "openai": return Boolean(aiTab.pref("hasOpenaiApiKey", false))
            case "anthropic": return Boolean(aiTab.pref("hasAnthropicApiKey", false))
            case "gemini": return Boolean(aiTab.pref("hasGeminiApiKey", false))
            case "openrouter": return Boolean(aiTab.pref("hasOpenrouterApiKey", false))
            case "ollama": return aiTab.pref("ollamaEndpoint", "").length > 0 && aiTab.pref("ollamaModel", "").length > 0
            default: return false
        }
    }

    // Discuss Shot app display names (index matches aiTab.pref("discussShotApp", 0, "aiNetwork"))
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

    // ─── Connect AI Apps state ───
    // The AI app the user named, in the order the picker lists them (index = the
    // stored discussShotApp): the three that connect remotely first.
    readonly property var aiAppOrder: [0, 1, 2, 7, 3, 4, 5, 6]
    readonly property int aiApp: pref("discussShotApp", 0, "aiNetwork")
    // Claude (app or web) and ChatGPT take a remote connector.
    readonly property bool aiAppConnectsRemotely: aiApp === 0 || aiApp === 1 || aiApp === 2
    readonly property bool networkOn: pref("shotServerEnabled", false, "network") === true
    readonly property bool mcpOn: pref("mcpEnabled", false, "mcp") === true
    readonly property bool remoteOn: pref("remoteMcpEnabled", false, "mcp") === true
    // Access and confirmation apply to whichever MCP endpoint is on.
    readonly property bool anyMcpOn: remoteOn || mcpOn
    readonly property string remoteMode: pref("remoteMcpMode", "custom", "mcp")

    function tailscaleStatusText() {
        if (!(aiState.remoteMcpTunnelAvailable ?? false))
            return TranslationManager.translate("settings.ai.apps.tailscaleUnavailable", "Not included in this build. Use your own URL in Advanced options.")
        if (remoteConnectorUrl.length > 0)
            return TranslationManager.translate("settings.ai.apps.tailscaleConnected", "Connected")
        if (!networkOn || !remoteOn)
            return TranslationManager.translate("settings.ai.apps.tailscaleWaiting", "Starts once the steps above are on. Sign in and set up in Settings → Connections → Network.")
        switch (aiState.remoteMcpStatus || "off") {
        case "publishing":
            return TranslationManager.translate("settings.ai.remoteMcp.status.publishing", "Funnel is on — waiting for Tailscale to bring up the public address. This can take a few minutes after starting.")
        case "error":
            return TranslationManager.translate("settings.ai.remoteMcp.status.error", "Error: %1").arg(aiState.remoteMcpStatusDetail || "")
        default:
            return TranslationManager.translate("settings.ai.apps.tailscaleSignIn", "Connecting. If it asks you to sign in, do that in Settings → Connections → Network.")
        }
    }

    // Ticks on the "Connect it" steps the page can do itself. A new link must be
    // copied again, so a link change clears the copy tick.
    property string linkCopiedFor: ""
    property var appOpened: ({})
    function markAppOpened(app) {
        const opened = Object.assign({}, appOpened)
        opened[app] = true
        appOpened = opened
    }
    onRemoteConnectorUrlChanged: linkCopiedFor = ""

    // "What AI apps can do": each choice sets access level and confirmation together.
    readonly property var mcpPresets: [
        { access: 0, confirm: -1,
          label: TranslationManager.translate("settings.ai.apps.preset.look", "Look only"),
          detail: TranslationManager.translate("settings.ai.apps.preset.lookDetail", "Reads your machine, shots and profiles. Can't change anything.") },
        { access: 1, confirm: 1,
          label: TranslationManager.translate("settings.ai.apps.preset.ask", "Ask before acting"),
          detail: TranslationManager.translate("settings.ai.apps.preset.askDetail", "Can also start, stop, wake and sleep the machine, and asks you on this screen before it starts anything.") },
        { access: 2, confirm: 0,
          label: TranslationManager.translate("settings.ai.apps.preset.full", "Full control"),
          detail: TranslationManager.translate("settings.ai.apps.preset.fullDetail", "Can also upload profiles and change settings, without asking first.") }
    ]
    readonly property int mcpPresetIndex: {
        const access = pref("mcpAccessLevel", 1, "mcp")
        const confirm = pref("mcpConfirmationLevel", 1, "mcp")
        for (let i = 0; i < mcpPresets.length; i++) {
            if (mcpPresets[i].access === access && (mcpPresets[i].confirm < 0 || mcpPresets[i].confirm === confirm))
                return i
        }
        return -1
    }

    // Several MCP settings written one after another: the host takes a write only
    // at the domain's current revision, which each completion has already moved on.
    property var mcpWriteQueue: []
    function writeMcpSequence(writes) {
        mcpWriteQueue = mcpWriteQueue.concat(writes)
        if (!mcpSequenceWrite.busy) writeNextMcp()
    }
    function writeNextMcp() {
        if (mcpWriteQueue.length === 0) return
        const next = mcpWriteQueue[0]
        mcpWriteQueue = mcpWriteQueue.slice(1)
        if (!mcpSequenceWrite.submit(next.name, next.value)) {
            mcpWriteQueue = []
            preferenceError.show(TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet."))
        }
    }
    function applyMcpPreset(preset) {
        const writes = [{ name: "mcpAccessLevel", value: preset.access }]
        if (preset.confirm >= 0)
            writes.push({ name: "mcpConfirmationLevel", value: preset.confirm })
        writeMcpSequence(writes)
    }
    // Turning Remote Access on chooses Tailscale, the recommended route, unless the
    // user already set up their own URL (the stored mode defaults to custom).
    function setRemoteAccess(on) {
        const writes = [{ name: "remoteMcpEnabled", value: on }]
        if (on && remoteMode !== "tailscale" && (aiState.remoteMcpTunnelAvailable ?? false)
                && pref("remoteMcpCustomBaseUrl", "", "mcp").length === 0)
            writes.unshift({ name: "remoteMcpMode", value: "tailscale" })
        writeMcpSequence(writes)
    }
    PreferenceWrite {
        id: mcpSequenceWrite
        domain: "mcp"
        onCompleted: function(success) {
            if (success) Qt.callLater(aiTab.writeNextMcp)
            else aiTab.mcpWriteQueue = []
        }
        onFailed: function(message) { preferenceError.show(message) }
    }

    Component.onCompleted: {
        AISettingsService.refresh()
        ConversationSession.refresh()
        Qt.callLater(aiTab.refreshMcpUrls)
    }

    Connections {
        target: SettingsPreferencesService
        function onPreferencesChanged() { aiTab.refreshMcpUrls() }
    }
    Connections {
        target: AISettingsService
        function onStateChanged() { aiTab.refreshMcpUrls() }
        function onCompleted(requestId, operation, success, result, reason) {
            if (requestId === aiTab.pendingTestRequestId && operation === "testConnection") {
                aiTab.pendingTestRequestId = ""
                aiTab.testResultSuccess = success && Boolean(result.success ?? true)
                aiTab.testResultMessage = success
                    ? (result.message || TranslationManager.translate("settings.ai.testSucceeded", "Connection successful."))
                    : (result.message || TranslationManager.translate("settings.ai.testFailed", "Connection failed."))
                return
            }
            if (requestId === aiTab.pendingOllamaRequestId && operation === "refreshOllamaModels") {
                aiTab.pendingOllamaRequestId = ""
                if (!success) preferenceError.show(result.message || TranslationManager.translate("settings.ai.refreshFailed", "Could not refresh models."))
                return
            }
            if (requestId === aiTab.pendingMcpUrlRequestId && operation === "requestMcpUrls") {
                aiTab.pendingMcpUrlRequestId = ""
                if (aiTab.pref("remoteMcpEnabled", false, "mcp")) {
                    aiTab.remoteConnectorUrl = success ? (result.connectorUrl || "") : ""
                    if (!success) preferenceError.show(TranslationManager.translate("settings.ai.remoteMcp.urlUnavailable", "Remote access details are unavailable."))
                }
                Qt.callLater(aiTab.refreshMcpUrls)
                return
            }
            if (requestId === aiTab.pendingRotateRequestId && operation === "rotateMcpToken") {
                aiTab.pendingRotateRequestId = ""
                if (success) {
                    aiTab.remoteConnectorUrl = result.connectorUrl || ""
                    aiTab.lastMcpUrlSignature = ""
                    Qt.callLater(aiTab.refreshMcpUrls)
                    AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.rotated", "Token rotated. New connector URL generated."))
                } else preferenceError.show(TranslationManager.translate("settings.ai.remoteMcp.rotateFailed", "Could not rotate the remote access token."))
            }
        }
    }
    Connections {
        target: LocalClipboard
        function onOperationCompleted(requestId, operation, success, text, reason) {
            if (requestId !== aiTab.pendingClipboardRequestId || operation !== "writeText") return
            aiTab.pendingClipboardRequestId = ""
            aiTab.pendingClipboardText = ""
            if (!success) preferenceError.show(TranslationManager.translate("common.copyFailed", "Could not copy to the clipboard."))
        }
    }

    // Two cards, one above the other: the AI Advisor inside Decenza, and the
    // outside AI apps that connect over MCP.
    Flickable {
        id: aiFlickable
        anchors.fill: parent
        contentHeight: aiTabContent.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: aiTabContent
            width: parent.width
            spacing: Theme.spacingMedium

            SettingsCard {
                searchId: "aiProvider"
                title: TranslationManager.translate("settings.ai.section.advisorConfig", "AI Advisor Config")
                description: TranslationManager.translate("settings.search.aiProviderDesc", "Configure AI for shot analysis")
                keywords: ["ai", "advisor", "assistant", "provider", "openai", "anthropic", "gemini", "ollama", "api", "key", "model"]
                showHeader: false
                contentMargins: Theme.scaled(12)
                spacing: Theme.scaled(16)

                Text {
                    text: TranslationManager.translate("settings.ai.section.advisorConfig", "AI Advisor Config")
                    font.family: Theme.subtitleFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    color: Theme.textColor
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.borderColor
                }
                Tr {
                    Layout.fillWidth: true
                    key: "settings.ai.advisor.intro"
                    fallback: "The AI Advisor is the chat inside Decenza that reviews your shots and suggests what to change. It uses the provider and API key below, billed to that account."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.WordWrap
                }

                // Provider selection: fixed-size buttons, centred, wrapping to a
                // second row on a screen too narrow for all five.
                Item {
                    Layout.fillWidth: true
                    implicitHeight: providerRow.implicitHeight

                    Flow {
                        id: providerRow
                        readonly property real tileWidth: Theme.scaled(90)
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, 5 * tileWidth + 4 * spacing)
                        spacing: Theme.scaled(8)

                        Repeater {
                            SettingsSearch.title: TranslationManager.translate("settings.ai.section.provider", "AI Provider")
                            model: [
                                { id: "openai", name: "OpenAI" },
                                { id: "anthropic", name: "Anthropic" },
                                { id: "gemini", name: "Gemini" },
                                { id: "openrouter", name: "OpenRouter" },
                                { id: "ollama", name: "Ollama" }
                            ]

                            delegate: Rectangle {
                                id: providerTile
                                required property var modelData

                                width: providerRow.tileWidth
                                height: Theme.scaled(56)
                                radius: Theme.scaled(8)

                                property bool isSelected: aiTab.pref("aiProvider", "openai") === providerTile.modelData.id
                                property bool hasKey: aiTab.isProviderConfigured(providerTile.modelData.id)

                                color: {
                                    if (isSelected) return Theme.primaryColor
                                    if (hasKey) return Qt.alpha(Theme.successColor, 0.25)
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
                                    if (hasKey) return Qt.alpha(Theme.successColor, 0.5)
                                    return Theme.borderColor
                                }
                                border.width: 1

                                Accessible.ignored: true

                                Column {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(2)

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: providerTile.modelData.name
                                        font.pixelSize: Theme.labelFont.pixelSize
                                        font.bold: providerTile.isSelected
                                        color: providerTile.isSelected ? Theme.primaryContrastColor : Theme.textColor
                                        Accessible.ignored: true
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: aiTab.aiState.modelDisplayNames ? (aiTab.aiState.modelDisplayNames[providerTile.modelData.id] ?? "") : ""
                                        font.pixelSize: Theme.labelFont.pixelSize
                                        color: providerTile.isSelected ? Qt.alpha(Theme.primaryContrastColor, 0.8) : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: providerTile.modelData.name + (providerTile.isSelected
                                        ? " (" + TranslationManager.translate("settings.ai.selected", "selected") + ")"
                                        : "")
                                    accessibleItem: providerTile
                                    onAccessibleClicked: {
                                        apiKeyField.text = ""
                                        aiTab.write("ai", "aiProvider", providerTile.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                // Provider guidance. This used to recommend Claude specifically
                // for shot analysis, on the strength of testing that is now
                // several model generations old and no longer holds — all three
                // cloud providers give good dial-in advice. Keep this text about
                // how to CHOOSE (where you already have credit), not about which
                // vendor is smarter; the latter goes stale every few months and
                // nothing fails when it does.
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
                        fallback: "Claude, OpenAI and Gemini all give good dial-in advice — pick whichever you already have an API key for. Each provider's first model in the list is the recommended default; the note under the model picker explains the trade-offs. Ollama runs locally with no API cost."
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                    }
                }

                // API Key section (cloud providers)
                ColumnLayout {
                    visible: aiTab.pref("aiProvider", "openai") !== "ollama"
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.ai.apiKeyForAdvisor"
                        fallback: "API Key (for AI Advisor)"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        StyledTextField {
                            id: apiKeyField
                            accessibleName: TranslationManager.translate("settings.ai.apiKey.accessible", "API key")
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                            placeholderText: aiTab.isProviderConfigured(aiTab.pref("aiProvider", "openai"))
                                ? TranslationManager.translate("settings.ai.apiKey.stored", "Key stored. Enter a new key to replace it.")
                                : TranslationManager.translate("settings.ai.apiKey.enter", "Enter API key")
                            onEditingFinished: aiTab.submitProviderKey(text)
                        }
                        AccessibleButton {
                            visible: aiTab.isProviderConfigured(aiTab.pref("aiProvider", "openai"))
                            text: TranslationManager.translate("common.clear", "Clear")
                            accessibleName: TranslationManager.translate("settings.ai.apiKey.clear", "Clear the stored API key")
                            onClicked: {
                                const provider = aiTab.pref("aiProvider", "openai")
                                const key = provider === "openai" ? "openaiApiKey"
                                          : provider === "anthropic" ? "anthropicApiKey"
                                          : provider === "gemini" ? "geminiApiKey" : "openrouterApiKey"
                                aiTab.write("ai", key, "")
                            }
                        }
                    }

                    // Where to get a key for the selected provider, as a link to that page.
                    Text {
                        id: getKeyLink
                        readonly property var keyPages: ({
                            "openai": "https://platform.openai.com/api-keys",
                            "anthropic": "https://console.anthropic.com/settings/keys",
                            "gemini": "https://aistudio.google.com/app/apikey",
                            "openrouter": "https://openrouter.ai/keys"
                        })
                        readonly property string keyPage: keyPages[aiTab.pref("aiProvider", "openai")] || ""
                        visible: keyPage.length > 0
                        text: TranslationManager.translate("settings.ai.getKeyAt", "Get a key at %1 ↗").arg(keyPage.replace("https://", "").split("/")[0])
                        color: Theme.accentColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        font.underline: true
                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("settings.ai.getKeyAccessible", "Open the page to get an API key in your browser")
                            accessibleRole: Accessible.Link
                            cursorShape: Qt.PointingHandCursor
                            onAccessibleClicked: aiTab.openExternal(getKeyLink.keyPage)
                        }
                    }
                }

                // Model selection (providers exposing a fixed catalog of >1 model).
                // Generic: any provider whose availableModels() returns multiple
                // entries lights this up automatically — no per-provider wiring.
                ColumnLayout {
                    id: modelSelect
                    // Re-evaluated when the active provider changes (the binding
                    // references aiTab.pref("aiProvider", "openai")).
                    property var options: aiTab.aiState["models." + aiTab.pref("aiProvider", "openai")] || []
                    property string currentProvider: aiTab.pref("aiProvider", "openai")
                    visible: options.length > 1
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    // Index of the stored selection; default to 0 (the recommended
                    // first entry) when unset or stale, without writing it back.
                    function selectedIndex() {
                        var sel = aiTab.providerModel(currentProvider)
                        for (let i = 0; i < options.length; i++) {
                            if (options[i].id === sel) return i
                        }
                        return 0
                    }

                    Tr {
                        key: "settings.ai.model"
                        fallback: "Model"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
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
                                aiTab.write("ai", "providerModel", ({providerId: modelSelect.currentProvider, modelId: modelSelect.options[index].id}))
                            }
                        }
                    }
                }

                // Provider-specific guidance, shown for single-model providers too.
                // The English copy lives in AIProvider::modelHint() so the app and
                // the ShotServer page share one source; main.cpp registers the
                // dynamic key so the batch-translation registry stays complete.
                Text {
                    visible: text.length > 0
                    text: {
                        var provider = aiTab.pref("aiProvider", "openai")
                        var hint = (aiTab.aiState["modelHint." + provider] || "")
                        if (hint === "") return ""
                        return TranslationManager.translate((aiTab.aiState["modelHintKey." + provider] || ""), hint)
                    }
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                // Ollama settings
                ColumnLayout {
                    visible: aiTab.pref("aiProvider", "openai") === "ollama"
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.ai.ollamaSettings"
                        fallback: "Ollama Settings"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    StyledTextField {
                        id: ollamaEndpointField
                        accessibleName: TranslationManager.translate("settings.ai.ollamaEndpoint.accessible", "Ollama endpoint URL")
                        Layout.fillWidth: true
                        text: aiTab.pref("ollamaEndpoint", "")
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onEditingFinished: aiTab.write("ai", "ollamaEndpoint", text)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        StyledComboBox {
                            Layout.fillWidth: true
                            model: aiTab.aiState.ollamaModels || []
                            currentIndex: model ? model.indexOf(aiTab.pref("ollamaModel", "")) : -1
                            onActivated: function(index) { if (index >= 0 && index < model.length) aiTab.write("ai", "ollamaModel", model[index]) }
                            accessibleLabel: TranslationManager.translate("settings.ai.ollamaModel", "Ollama model")
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.refresh", "Refresh")
                            accessibleName: TranslationManager.translate("settings.ai.refreshOllamaModels", "Refresh list of available Ollama AI models")
                            onClicked: aiTab.pendingOllamaRequestId = AISettingsService.request("refreshOllamaModels")
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.ai.ollamainstall", "Install: ollama.ai -> run: ollama pull llama3.2")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }
                }

                // Divider
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.borderColor
                }

                // Cost info. Comes from AIProvider::costHint() so the estimate
                // sits beside the model catalog it prices — the previous
                // per-provider strings hardcoded here understated the cost by
                // 5x to 8x depending on the model, and went stale unnoticed,
                // because nothing tied them to the models they described.
                // Empty when the selected model has no priced entry, so bind
                // visible to the text rather than to the provider alone.
                Text {
                    visible: aiTab.pref("aiProvider", "openai") !== "ollama" && text.length > 0
                    text: aiTab.aiState.selectedCostHint || ""
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    visible: aiTab.pref("aiProvider", "openai") === "ollama"
                    text: TranslationManager.translate("settings.ai.cost.ollama", "Free — runs locally on your computer")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                // Test connection row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    AccessibleButton {
                        primary: (aiTab.aiState.configured ?? false)
                        enabled: (aiTab.aiState.configured ?? false)
                        text: TranslationManager.translate("settings.ai.testconnection", "Test Connection")
                        accessibleName: TranslationManager.translate("settings.ai.testConnectionAccessible", "Test connection to the AI service")
                        onClicked: {
                            aiTab.testResultMessage = TranslationManager.translate("settings.ai.testing", "Testing...")
                            aiTab.pendingTestRequestId = AISettingsService.request("testConnection")
                        }
                    }

                    AccessibleButton {
                        visible: aiTab.pref("aiProvider", "openai") === "openai" || aiTab.pref("aiProvider", "openai") === "anthropic"
                        text: TranslationManager.translate("settings.ai.advanced", "Advanced")
                        accessibleName: TranslationManager.translate("settings.ai.advancedAccessible", "Configure custom API endpoint")
                        onClicked: advancedEndpointDialog.open()
                    }

                    Text {
                        visible: aiTab.testResultMessage.length > 0
                        text: aiTab.testResultMessage
                        color: aiTab.testResultSuccess ? Theme.successColor : Theme.errorColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        id: continueConversationBtn
                        SettingsSearch.title: TranslationManager.translate("settings.ai.continueconversation", "Continue Chat")
                        property bool hasConversation: aiTab.aiState.hasAnyConversation ?? false
                        visible: aiTab.aiState.configured ?? false
                        enabled: hasConversation
                        text: hasConversation
                              ? TranslationManager.translate("settings.ai.continueconversation", "Continue Chat")
                              : TranslationManager.translate("settings.ai.noconversation", "No Chat")
                        accessibleName: hasConversation
                              ? TranslationManager.translate("settings.ai.continueConversationAccessible", "Continue previous AI conversation")
                              : TranslationManager.translate("settings.ai.noConversationAccessible", "No saved AI conversation")
                        onClicked: {
                            aiTab.conversationRequestError = ""
                            aiTab.pendingConversationLoadRequestId = ConversationSession.request("loadMostRecent")
                            conversationOverlay.open()
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
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }

                        Tr {
                            key: "settings.ai.tasteIntakeDesc"
                            fallback: "When opening the assistant for a shot, first show a quick tap-only taste panel (Sour/Balanced/Bitter, body, rating) instead of the keyboard. Turn off to open the chat directly."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    StyledSwitch {
                        checked: aiTab.pref("tasteIntakeOnAsk", true)
                        accessibleName: TranslationManager.translate("settings.ai.tasteIntakeAccessible", "Ask how the shot tasted before chatting")
                        onToggled: aiTab.write("ai", "tasteIntakeOnAsk", checked)
                    }
                }

            }

            SettingsCard {
                searchId: "aiApps"
                title: TranslationManager.translate("settings.ai.section.aiApps", "Connect AI Apps (MCP)")
                description: TranslationManager.translate("settings.search.aiAppsDesc", "Let Claude or ChatGPT read your shots and control your machine")
                keywords: ["mcp", "claude", "chatgpt", "openai", "connector", "remote", "server", "protocol", "discuss", "tailscale", "plugin"]
                showHeader: false
                contentMargins: Theme.scaled(12)
                spacing: Theme.scaled(16)

                Text {
                    text: TranslationManager.translate("settings.ai.section.aiApps", "Connect AI Apps (MCP)")
                    font.family: Theme.subtitleFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    color: Theme.textColor
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.borderColor
                }
                Tr {
                    Layout.fillWidth: true
                    key: "settings.ai.apps.intro"
                    fallback: "Let Claude or ChatGPT, in their own apps, read your shots and control your machine. They use your Claude or ChatGPT subscription; the AI Advisor's API key is not used."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.WordWrap
                }

                // ─── 1. Your AI app: decides the steps below and what Discuss opens ───
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.ai.apps.yourApp"
                        fallback: "Your AI app"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }
                    StyledComboBox {
                        id: aiAppCombo
                        Layout.fillWidth: true
                        model: aiTab.aiAppOrder.map(function(app) { return aiTab.discussAppNames[app] })
                        accessibleLabel: TranslationManager.translate("settings.ai.apps.yourAppAccessible", "Your AI app")
                        currentIndex: Math.max(0, aiTab.aiAppOrder.indexOf(aiTab.aiApp))
                        onActivated: function(index) {
                            if (index >= 0 && index < aiTab.aiAppOrder.length)
                                aiTab.write("aiNetwork", "discussShotApp", aiTab.aiAppOrder[index])
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: aiTab.aiApp === aiTab.pref("discussAppNone", 6, "aiNetwork")
                            ? TranslationManager.translate("settings.ai.apps.noneHint", "The Discuss button is hidden.")
                            : aiTab.aiAppConnectsRemotely || aiTab.aiApp === aiTab.pref("discussAppClaudeDesktop", 7, "aiNetwork")
                            ? TranslationManager.translate("settings.ai.apps.connectsHint", "The Discuss button opens this app. Once it is connected below, it reads the shot itself; until then the shot is copied for you to paste.")
                            : TranslationManager.translate("settings.ai.apps.pasteHint", "This app can't connect to Decenza. The Discuss button opens it with your shot copied, ready to paste.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }
                    // Custom URL field (only when Custom URL is selected)
                    ColumnLayout {
                        visible: aiTab.pref("discussShotApp", 0, "aiNetwork") === 5
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        StyledTextField {
                            id: customUrlField
                            accessibleName: TranslationManager.translate("settings.ai.discussCustomUrl.accessible", "Custom discuss-shot URL")
                            Layout.fillWidth: true
                            placeholder: "https://localhost:8080"
                            text: aiTab.pref("discussShotCustomUrl", "", "aiNetwork")
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                            onEditingFinished: aiTab.write("aiNetwork", "discussShotCustomUrl", text)
                        }
                    }
                    // Claude Desktop (Remote Control) setup — visible when Claude Desktop is selected
                    ColumnLayout {
                        visible: aiTab.pref("discussShotApp", 0, "aiNetwork") === aiTab.pref("discussAppClaudeDesktop", 7, "aiNetwork")
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate(
                                "settings.ai.discuss.claudeDesktop.help",
                                "Paste the session URL printed by `claude remote-control`. See the MCP Setup page for step-by-step instructions.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)
                            StyledTextField {
                                id: claudeRcUrlField
                                accessibleName: TranslationManager.translate("settings.ai.claudeRcUrl.accessible", "Claude remote-control session URL")
                                Layout.fillWidth: true
                                placeholderText: aiTab.pref("hasClaudeRcSessionUrl", false, "aiNetwork")
                                    ? TranslationManager.translate("settings.ai.claudeRcUrl.stored", "Session URL stored. Enter a new URL to replace it.")
                                    : "https://claude.ai/..."
                                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                                onEditingFinished: if (text.length > 0) aiTab.write("aiNetwork", "claudeRcSessionUrl", text)
                            }
                            AccessibleButton {
                                visible: aiTab.pref("hasClaudeRcSessionUrl", false, "aiNetwork")
                                text: TranslationManager.translate("common.clear", "Clear")
                                accessibleName: TranslationManager.translate("settings.ai.claudeRcUrl.clear", "Clear the stored Claude session URL")
                                onClicked: aiTab.write("aiNetwork", "claudeRcSessionUrl", "")
                            }
                        }
                    }
                }

                // ─── 2. Ready to connect: everything the connection needs, in order ───
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: aiTab.aiAppConnectsRemotely || aiTab.aiApp === aiTab.pref("discussAppClaudeDesktop", 7, "aiNetwork")

                    Tr {
                        key: "settings.ai.apps.ready"
                        fallback: "Ready to connect"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    // Enable Network: shown, not switched, here (its switch lives in
                    // the Network card, once).
                    ChecklistRow {
                        done: aiTab.networkOn
                        label: TranslationManager.translate("settings.network.enable", "Enable Network")
                        detail: aiTab.networkOn ? "" : TranslationManager.translate("settings.ai.apps.networkOff", "Turn it on in Settings → Connections → Network.")
                        AccessibleButton {
                            visible: !aiTab.networkOn
                            text: TranslationManager.translate("settings.ai.apps.openNetwork", "Open Network")
                            accessibleName: TranslationManager.translate("settings.ai.mcp.needsNetworkAccessible", "The MCP server needs Enable Network. Open the Network card in Connections settings")
                            onClicked: AppShell.settingsRequested("connections")
                        }
                    }

                    // Claude Desktop uses the local server, switched on in Advanced options.
                    ChecklistRow {
                        visible: aiTab.aiApp === aiTab.pref("discussAppClaudeDesktop", 7, "aiNetwork")
                        done: aiTab.mcpOn
                        label: TranslationManager.translate("settings.ai.mcp.enable", "Enable MCP Server")
                        detail: aiTab.mcpOn ? "" : TranslationManager.translate("settings.ai.apps.localMcpOff", "Turn it on under Advanced… → Local network MCP.")
                        AccessibleButton {
                            visible: !aiTab.mcpOn
                            text: TranslationManager.translate("settings.ai.apps.openAdvanced", "Advanced…")
                            accessibleName: TranslationManager.translate("settings.ai.apps.turnOnLocalAccessible", "Turn on the local MCP server in Advanced options")
                            onClicked: mcpAdvancedDialog.open()
                        }
                    }

                    ChecklistRow {
                        visible: aiTab.aiAppConnectsRemotely
                        done: aiTab.remoteOn
                        label: TranslationManager.translate("settings.ai.remoteMcp.enable", "Enable Remote Access")
                        detail: TranslationManager.translate("settings.ai.remoteMcp.enableDetail", "Lets the app reach Decenza from anywhere, through a private link.")
                        StyledSwitch {
                            checked: aiTab.remoteOn
                            enabled: aiTab.networkOn
                            accessibleName: TranslationManager.translate("settings.ai.remoteMcp.enableAccessible", "Enable remote MCP access for mobile connectors")
                            onToggled: aiTab.setRemoteAccess(checked)
                        }
                    }

                    ChecklistRow {
                        visible: aiTab.aiAppConnectsRemotely && aiTab.remoteMode === "tailscale"
                        done: aiTab.remoteConnectorUrl.length > 0
                        label: TranslationManager.translate("settings.ai.apps.tailscale", "Tailscale")
                        detail: aiTab.tailscaleStatusText()
                        detailIsError: (aiTab.aiState.remoteMcpStatus || "off") === "error"
                        AccessibleButton {
                            visible: aiTab.remoteConnectorUrl.length === 0
                            text: TranslationManager.translate("settings.ai.apps.setUpTailscale", "Set up")
                            accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleInNetworkCardAccessible", "Open the Network card in Connections settings for Tailscale sign-in and setup")
                            onClicked: AppShell.settingsRequested("connections")
                        }
                    }

                    ChecklistRow {
                        visible: aiTab.aiAppConnectsRemotely && aiTab.remoteMode !== "tailscale"
                        done: aiTab.remoteConnectorUrl.length > 0
                        label: TranslationManager.translate("settings.ai.apps.ownUrl", "Your own URL")
                        detail: aiTab.remoteConnectorUrl.length > 0 ? ""
                            : TranslationManager.translate("settings.ai.apps.ownUrlMissing", "Enter your https:// address in Advanced options.")
                        AccessibleButton {
                            visible: aiTab.remoteConnectorUrl.length === 0
                            text: TranslationManager.translate("settings.ai.apps.openAdvanced", "Advanced…")
                            accessibleName: TranslationManager.translate("settings.ai.apps.enterOwnUrlAccessible", "Enter your own URL in Advanced options")
                            onClicked: mcpAdvancedDialog.open()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: aiTab.aiApp === aiTab.pref("discussAppClaudeDesktop", 7, "aiNetwork")
                        text: TranslationManager.translate("settings.ai.apps.desktopNext", "Then set up Claude Desktop under Advanced… → Local network MCP. It connects over your WiFi.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }
                }

                // ─── 3. What AI apps can do: one choice that sets access and confirmation ───
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: aiTab.aiAppConnectsRemotely || aiTab.aiApp === aiTab.pref("discussAppClaudeDesktop", 7, "aiNetwork")
                    enabled: aiTab.anyMcpOn
                    opacity: enabled ? 1.0 : 0.5

                    Tr {
                        key: "settings.ai.apps.canDo"
                        fallback: "What AI apps can do"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Repeater {
                            SettingsSearch.title: TranslationManager.translate("settings.ai.apps.canDo", "What AI apps can do")
                            SettingsSearch.keywords: ["access level", "confirmation", "control", "monitor", "automation", "permission"]
                            model: aiTab.mcpPresets
                            delegate: Rectangle {
                                id: presetTile
                                required property var modelData
                                required property int index
                                readonly property bool selected: aiTab.mcpPresetIndex === presetTile.index

                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: presetLabel.implicitHeight + Theme.scaled(24)
                                radius: Theme.scaled(6)
                                color: selected ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                                border.color: selected ? Theme.primaryColor : Theme.borderColor
                                border.width: 1
                                Accessible.ignored: true

                                Text {
                                    id: presetLabel
                                    anchors.centerIn: parent
                                    width: parent.width - Theme.scaled(16)
                                    horizontalAlignment: Text.AlignHCenter
                                    text: presetTile.modelData.label
                                    color: Theme.textColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    font.bold: presetTile.selected
                                    wrapMode: Text.WordWrap
                                    Accessible.ignored: true
                                }
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: presetTile.modelData.label + ". " + presetTile.modelData.detail
                                    accessibleItem: presetTile
                                    onAccessibleClicked: aiTab.applyMcpPreset(presetTile.modelData)
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: aiTab.mcpPresetIndex >= 0
                            ? aiTab.mcpPresets[aiTab.mcpPresetIndex].detail
                            : TranslationManager.translate("settings.ai.apps.customAccess", "Custom access and confirmation, set in Advanced options.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }
                }

                // ─── 4. Connect it: shown once Remote Access is on, before the link
                // answers, so the steps are there while Tailscale comes up ───
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: aiTab.networkOn && aiTab.remoteOn

                    RowLayout {
                        Layout.fillWidth: true
                        Tr {
                            key: "settings.ai.apps.connectIt"
                            fallback: "Connect it"
                            color: Theme.textColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: aiTab.remoteConnectorUrl.length > 0
                                ? TranslationManager.translate("settings.ai.apps.linkReady", "Link ready")
                                : TranslationManager.translate("settings.ai.apps.linkPending", "Link not ready yet")
                            color: aiTab.remoteConnectorUrl.length > 0 ? Theme.successColor : Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.bold: true
                        }
                    }

                    // The link and its QR code, or what it is waiting for.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Theme.scaled(8)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)
                                visible: aiTab.remoteConnectorUrl.length > 0
                                Text {
                                    Layout.fillWidth: true
                                    text: aiTab.remoteConnectorUrl
                                    color: Theme.accentColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    wrapMode: Text.WrapAnywhere
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.connectorUrlAccessible", "Connector URL. Copy this into your AI app's custom connector settings.")
                                }
                                AccessibleButton {
                                    text: TranslationManager.translate("common.button.copy", "Copy")
                                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.copyAccessible", "Copy connector URL to clipboard")
                                    onClicked: {
                                        aiTab.copyText(aiTab.remoteConnectorUrl)
                                        AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.copied", "Connector URL copied"))
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: aiTab.remoteConnectorUrl.length === 0
                                text: aiTab.remoteMode === "tailscale"
                                    ? TranslationManager.translate("settings.ai.apps.linkPendingTailscale", "The link appears here once Tailscale is connected. You can read the steps meanwhile.") + " " + aiTab.tailscaleStatusText()
                                    : TranslationManager.translate("settings.ai.apps.linkPendingOwnUrl", "The link appears here once your own URL is set in Advanced options.")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.labelFont.pixelSize
                                wrapMode: Text.WordWrap
                            }
                        }

                        // For adding the connector from a phone.
                        Rectangle {
                            visible: aiTab.remoteConnectorUrl.length > 0
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: Theme.scaled(150)
                            Layout.preferredHeight: Theme.scaled(150)
                            color: "#ffffff"
                            radius: Theme.scaled(8)
                            Accessible.role: Accessible.Graphic
                            Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.qrAccessible", "QR code of the connector URL. Use the Copy button if you cannot scan it.")

                            QrCode {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(8)
                                value: aiTab.remoteConnectorUrl
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Both apps, always shown.
                    // Claude, step by step.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            Layout.topMargin: Theme.scaled(6)
                            text: TranslationManager.translate("settings.ai.connect.claude.title", "Claude")
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.ai.connect.claude.note", "Works in Claude on the web, the desktop app and Claude's phone apps.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                        ChecklistRow {
                            number: 1
                            done: aiTab.linkCopiedFor === "claude"
                            label: TranslationManager.translate("settings.ai.connect.copyStep", "Copy the link")
                            AccessibleButton {
                                enabled: aiTab.remoteConnectorUrl.length > 0
                                text: TranslationManager.translate("settings.ai.connect.copyLink", "Copy link")
                                accessibleName: TranslationManager.translate("settings.ai.connect.claude.copyAccessible", "Copy the connector link for Claude")
                                onClicked: {
                                    aiTab.copyText(aiTab.remoteConnectorUrl)
                                    aiTab.linkCopiedFor = "claude"
                                }
                            }
                        }
                        ChecklistRow {
                            number: 2
                            done: aiTab.appOpened["claude"] === true
                            label: TranslationManager.translate("settings.ai.connect.claude.openStep", "Open Claude and sign in if it asks")
                            AccessibleButton {
                                primary: true
                                text: TranslationManager.translate("settings.ai.connect.claude.open", "Open Claude")
                                accessibleName: TranslationManager.translate("settings.ai.connect.claude.openAccessible", "Open Claude's connector settings in your browser")
                                onClicked: {
                                    aiTab.openExternal("https://claude.ai/customize/connectors")
                                    aiTab.markAppOpened("claude")
                                }
                            }
                        }
                        ChecklistRow {
                            number: 3
                            label: TranslationManager.translate("settings.ai.connect.claude.addStep", "Click + Add, then Add custom connector.")
                        }
                        ChecklistRow {
                            number: 4
                            label: TranslationManager.translate("settings.ai.connect.claude.nameStep", "Name it Decenza, paste the link into MCP server URL and click Continue.")
                        }
                        ChecklistRow {
                            number: 5
                            label: TranslationManager.translate("settings.ai.connect.claude.authStep", "Leave No sign-in selected and click Add, then click Connect.")
                        }
                        ChecklistRow {
                            number: 6
                            label: TranslationManager.translate("settings.ai.connect.claude.useStep", "In a chat, click +, then Connectors, and turn Decenza on.")
                            detail: TranslationManager.translate("settings.ai.connect.claude.useStepDetail", "Claude asks before it uses each tool. Choose Allow once or Always allow.")
                        }
                    }

                    // ChatGPT, step by step.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            Layout.topMargin: Theme.scaled(6)
                            text: TranslationManager.translate("settings.ai.connect.chatgpt.title", "ChatGPT")
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.ai.connect.chatgpt.note", "Needs a paid plan, and works in ChatGPT on the web.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                        ChecklistRow {
                            number: 1
                            done: aiTab.linkCopiedFor === "chatgpt"
                            label: TranslationManager.translate("settings.ai.connect.copyStep", "Copy the link")
                            AccessibleButton {
                                enabled: aiTab.remoteConnectorUrl.length > 0
                                text: TranslationManager.translate("settings.ai.connect.copyLink", "Copy link")
                                accessibleName: TranslationManager.translate("settings.ai.connect.chatgpt.copyAccessible", "Copy the connector link for ChatGPT")
                                onClicked: {
                                    aiTab.copyText(aiTab.remoteConnectorUrl)
                                    aiTab.linkCopiedFor = "chatgpt"
                                }
                            }
                        }
                        ChecklistRow {
                            number: 2
                            done: aiTab.appOpened["chatgpt"] === true
                            label: TranslationManager.translate("settings.ai.connect.chatgpt.openStep", "Open ChatGPT and sign in if it asks")
                            AccessibleButton {
                                primary: true
                                text: TranslationManager.translate("settings.ai.connect.chatgpt.open", "Open ChatGPT")
                                accessibleName: TranslationManager.translate("settings.ai.connect.chatgpt.openAccessible", "Open ChatGPT's plugins page in your browser")
                                onClicked: {
                                    aiTab.openExternal("https://chatgpt.com/plugins")
                                    aiTab.markAppOpened("chatgpt")
                                }
                            }
                        }
                        ChecklistRow {
                            number: 3
                            label: TranslationManager.translate("settings.ai.connect.chatgpt.addStep", "On the Plugins page, click +, then Add custom MCP server.")
                            detail: TranslationManager.translate("settings.ai.connect.chatgpt.addStepDetail", "Some accounts call it Create MCP App. If neither is there, turn on Developer mode in Settings → Security and login, then reload the page.")
                        }
                        ChecklistRow {
                            number: 4
                            label: TranslationManager.translate("settings.ai.connect.chatgpt.nameStep", "Name it Decenza and paste the link under Connection.")
                        }
                        ChecklistRow {
                            number: 5
                            label: TranslationManager.translate("settings.ai.connect.chatgpt.authStep", "Change Authentication from OAuth to No authentication, tick I understand and want to continue, then click Create as a plugin.")
                        }
                        ChecklistRow {
                            number: 6
                            label: TranslationManager.translate("settings.ai.connect.chatgpt.useStep", "Click Connect Decenza. In a new chat, type @Decenza and pick it.")
                            detail: TranslationManager.translate("settings.ai.connect.chatgpt.useStepDetail", "ChatGPT asks before each tool runs, sometimes as \"Allow file materialization\". Choose Allow once.")
                        }
                    }


                    // A new link revokes the old one.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(12)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "settings.ai.remoteMcp.newConnectorLink"
                                fallback: "New connector link"
                                color: Theme.textColor
                                font.pixelSize: Theme.labelFont.pixelSize
                                font.bold: true
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.ai.remoteMcp.rotateHintApps"
                                fallback: "Makes a new link and disconnects the old one at once. Then update it in Claude or ChatGPT."
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.labelFont.pixelSize
                                wrapMode: Text.WordWrap
                            }
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.remoteMcp.rotateButton", "Rotate")
                            accessibleName: TranslationManager.translate("settings.ai.remoteMcp.rotateAccessible", "Rotate remote access token")
                            destructive: true
                            onClicked: rotateTokenDialog.open()
                        }
                    }
                }

                // Everything advanced: finer access settings, a custom URL, a new
                // link, the QR code and local network MCP. Findable by search.
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: TranslationManager.translate("settings.ai.mcp.local.advancedLink", "Advanced…")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    font.underline: true
                    AccessibleMouseArea {
                        SettingsSearch.title: TranslationManager.translate("settings.ai.apps.advancedTitle", "Advanced AI app options")
                        SettingsSearch.keywords: ["enable mcp server", "mcp server", "access level", "confirmation", "custom url", "proxy", "tunnel", "local", "lan", "wifi", "claude desktop", "setup page", "mcp setup", "advanced"]
                        accessibleName: TranslationManager.translate("settings.ai.apps.advancedAccessible", "Advanced AI app options")
                        accessibleRole: Accessible.Link
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onAccessibleClicked: mcpAdvancedDialog.open()
                    }
                }
            }
        }
    }


    // Advanced AI app options: what the page sets for you, set by hand.
    DecenzaDialog {
        id: mcpAdvancedDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(480), Theme.scaled(640))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(mcpAdvancedTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Flickable {
            implicitHeight: Math.min(advancedCol.implicitHeight,
                mcpAdvancedDialog.parent ? mcpAdvancedDialog.parent.height - Theme.scaled(120) : Theme.scaled(560))
            contentHeight: advancedCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: advancedCol
                width: parent.width
                spacing: Theme.scaled(12)

                Text {
                    id: mcpAdvancedTitle
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.apps.advancedTitle", "Advanced AI app options")
                    color: Theme.textColor
                    font.family: Theme.subtitleFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Tr {
                    Layout.topMargin: Theme.scaled(8)
                    key: "settings.ai.apps.advanced.access"
                    fallback: "Access and confirmation"
                    color: Theme.textColor
                    font.pixelSize: Theme.bodyFont.pixelSize
                    font.bold: true
                }
                Tr {
                    Layout.fillWidth: true
                    key: "settings.ai.apps.advanced.accessHint"
                    fallback: "\"What AI apps can do\" sets both of these. Set them separately here."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.WordWrap
                }
                // Access Level
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    enabled: aiTab.anyMcpOn
                    opacity: enabled ? 1.0 : 0.5

                    Tr {
                        key: "settings.ai.mcp.accessLevel"
                        fallback: "Access Level"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
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
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: accessDelegateCol.implicitHeight + Theme.scaled(16)
                            radius: Theme.scaled(6)
                            color: aiTab.pref("mcpAccessLevel", 1, "mcp") === accessDelegate.modelData.level ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: aiTab.pref("mcpAccessLevel", 1, "mcp") === accessDelegate.modelData.level ? Theme.primaryColor : Theme.borderColor
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
                                    text: accessDelegate.modelData.label
                                    color: Theme.textColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    font.bold: true
                                    Accessible.ignored: true
                                }
                                Text {
                                    text: accessDelegate.modelData.detail
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: accessDelegate.modelData.label + ". " + accessDelegate.modelData.detail
                                accessibleItem: accessDelegate
                                onAccessibleClicked: aiTab.write("mcp", "mcpAccessLevel", accessDelegate.modelData.level)
                            }
                        }
                    }
                }
                // Confirmation Level
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    enabled: aiTab.anyMcpOn && aiTab.pref("mcpAccessLevel", 1, "mcp") > 0
                    opacity: enabled ? 1.0 : 0.5

                    Tr {
                        key: "settings.ai.mcp.confirmationLevel"
                        fallback: "Confirmation"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
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
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: confirmDelegateCol.implicitHeight + Theme.scaled(16)
                            radius: Theme.scaled(6)
                            color: aiTab.pref("mcpConfirmationLevel", 1, "mcp") === confirmDelegate.modelData.level ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: aiTab.pref("mcpConfirmationLevel", 1, "mcp") === confirmDelegate.modelData.level ? Theme.primaryColor : Theme.borderColor
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
                                    text: confirmDelegate.modelData.label
                                    color: Theme.textColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    font.bold: true
                                    Accessible.ignored: true
                                }
                                Text {
                                    text: confirmDelegate.modelData.detail
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: confirmDelegate.modelData.label + ". " + confirmDelegate.modelData.detail
                                accessibleItem: confirmDelegate
                                onAccessibleClicked: aiTab.write("mcp", "mcpConfirmationLevel", confirmDelegate.modelData.level)
                            }
                        }
                    }
                }

                Tr {
                    Layout.topMargin: Theme.scaled(8)
                    key: "settings.ai.apps.advanced.connection"
                    fallback: "Connection"
                    color: Theme.textColor
                    font.pixelSize: Theme.bodyFont.pixelSize
                    font.bold: true
                }
                // Reachability mode selector
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Tr {
                        key: "settings.ai.remoteMcp.modeLabel"
                        fallback: "How is it reachable?"
                        color: Theme.textColor
                        font.pixelSize: Theme.labelFont.pixelSize
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
                                detail: (aiTab.aiState.remoteMcpTunnelAvailable ?? false)
                                    ? TranslationManager.translate("settings.ai.remoteMcp.mode.tailscaleDesc", "Join your tailnet and expose a public Funnel URL — no other setup.")
                                    : TranslationManager.translate("settings.ai.remoteMcp.mode.tailscaleUnavailable", "Not included in this build."),
                                enabled: (aiTab.aiState.remoteMcpTunnelAvailable ?? false)
                            }
                        ]
                        delegate: Rectangle {
                            id: modeDelegate
                            required property var modelData

                            Layout.fillWidth: true
                            Layout.preferredHeight: modeCol.implicitHeight + Theme.scaled(16)
                            radius: Theme.scaled(6)
                            opacity: modeDelegate.modelData.enabled ? 1.0 : 0.5
                            color: aiTab.pref("remoteMcpMode", "custom", "mcp") === modeDelegate.modelData.mode ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15) : "transparent"
                            border.color: aiTab.pref("remoteMcpMode", "custom", "mcp") === modeDelegate.modelData.mode ? Theme.primaryColor : Theme.borderColor
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
                                    text: modeDelegate.modelData.label
                                    color: Theme.textColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    font.bold: true
                                    Accessible.ignored: true
                                }
                                Text {
                                    text: modeDelegate.modelData.detail
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                            }
                            AccessibleMouseArea {
                                anchors.fill: parent
                                enabled: modeDelegate.modelData.enabled
                                accessibleName: modeDelegate.modelData.label + ". " + modeDelegate.modelData.detail
                                accessibleItem: modeDelegate
                                onAccessibleClicked: if (modeDelegate.modelData.enabled) aiTab.write("mcp", "remoteMcpMode", modeDelegate.modelData.mode)
                            }
                        }
                    }
                }
                // Public base URL (Mode C — bring your own)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)
                    visible: aiTab.pref("remoteMcpMode", "custom", "mcp") === "custom"

                    Tr {
                        key: "settings.ai.remoteMcp.baseUrlLabel"
                        fallback: "Public base URL"
                        color: Theme.textColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        font.bold: true
                    }
                    Text {
                        // Not a Tr: the %1 port needs .arg() substitution,
                        // which Tr does not perform (mirrors the status line).
                        text: TranslationManager.translate("settings.ai.remoteMcp.baseUrlHint",
                            "The https:// address your proxy/tunnel exposes, forwarding to this tablet on port %1. Example: https://decenza.your-tailnet.ts.net")
                            .arg(aiTab.pref("remoteMcpPort", 8890, "mcp"))
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    StyledTextField {
                        id: remoteMcpBaseUrlField
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.baseUrl.accessible", "Remote MCP base URL")
                        Layout.fillWidth: true
                        placeholder: "https://decenza.your-tailnet.ts.net"
                        text: aiTab.pref("remoteMcpCustomBaseUrl", "", "mcp")
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                        onEditingFinished: aiTab.write("mcp", "remoteMcpCustomBaseUrl", text)
                    }
                    Text {
                        // Shown whenever no usable connector URL composes —
                        // blank field or a non-https/invalid base URL — so an
                        // "active" listener is never left without an
                        // explanation of why there's no URL to copy.
                        visible: aiTab.remoteConnectorUrl.length === 0
                        text: TranslationManager.translate("settings.ai.remoteMcp.baseUrlInvalid", "Enter a full https:// URL to get your connector URL.")
                        color: Theme.errorColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Tr {
                    Layout.topMargin: Theme.scaled(8)
                    key: "settings.ai.mcp.local.title"
                    fallback: "Local network MCP (advanced)"
                    color: Theme.textColor
                    font.pixelSize: Theme.bodyFont.pixelSize
                    font.bold: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(12)
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.ai.mcp.enable"
                            fallback: "Enable MCP Server"
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.bold: true
                        }
                        StyledSwitch {
                            checked: aiTab.mcpOn
                            accessibleName: TranslationManager.translate("settings.ai.mcp.enableLocalAccessible", "Enable the local MCP server on your WiFi network")
                            onToggled: aiTab.write("mcp", "mcpEnabled", checked)
                        }
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.ai.mcp.local.intro"
                        fallback: "For Claude Desktop on a computer on the same WiFi. Most people should use Remote Access on the AI tab instead: it is easier and works on more devices."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }

                    // Setup page link (visible only when the shot server is actually
                    // listening — `shotServer.url` returns empty until then, and a
                    // bare "/mcp/setup" gets resolved against the qrc base by
                    // Qt.openUrlExternally on macOS, popping a "no application"
                    // dialog instead of opening the browser).
                    ColumnLayout {
                        id: mcpSetupColumn

                        readonly property string mcpSetupUrl: (aiTab.pref("mcpEnabled", false, "mcp") && (aiTab.aiState.shotServerUrl || "").length > 0)
                            ? aiTab.aiState.shotServerUrl + "/mcp/setup" : ""

                        visible: mcpSetupColumn.mcpSetupUrl.length > 0
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: TranslationManager.translate("settings.ai.mcp.setupPageLabel", "Setup page:") + " "
                                + mcpSetupColumn.mcpSetupUrl
                            color: Theme.accentColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.underline: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            AccessibleMouseArea {
                                id: setupLinkArea
                                accessibleName: TranslationManager.translate("settings.ai.mcp.setupLinkAccessible", "Open MCP setup page in browser")
                                accessibleRole: Accessible.Link
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onAccessibleClicked: {
                                    if (mcpSetupColumn.mcpSetupUrl.length > 0)
                                        aiTab.openExternal(mcpSetupColumn.mcpSetupUrl)
                                }
                            }
                        }
                        Tr {
                            key: "settings.ai.mcp.setupPageHint"
                            fallback: "Open this link on your desktop computer with Claude Desktop installed."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // MCP Status line
                    Text {
                        visible: aiTab.pref("mcpEnabled", false, "mcp") && aiTab.pref("shotServerEnabled", false, "network")
                        text: {
                            var status = TranslationManager.translate("settings.ai.mcp.status.listening", "Listening on port %1").arg(aiTab.aiState.shotServerPort || 0)
                            if ((aiTab.aiState.mcpSessionCount !== undefined)) {
                                let sessions = (aiTab.aiState.mcpSessionCount || 0)
                                if (sessions > 0)
                                    status += " · " + TranslationManager.translate("settings.ai.mcp.status.sessions", "%1 active session(s)").arg(sessions)
                            }
                            return status
                        }
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.mcp.setupGuide", "Setup Guide")
                        accessibleName: TranslationManager.translate("settings.ai.mcp.helpAccessible", "What is MCP and how to set it up")
                        onClicked: mcpHelpDialog.open()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    AccessibleButton {
                        text: TranslationManager.translate("common.button.done", "Done")
                        accessibleName: TranslationManager.translate("common.button.done", "Done")
                        primary: true
                        onClicked: mcpAdvancedDialog.close()
                    }
                }
            }
        }
    }

    // Discuss Shot app selector
    // Advanced endpoint dialog (OpenAI / Anthropic custom endpoint)
    DecenzaDialog {
        id: advancedEndpointDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(360), Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        property string provider: ""

        onAboutToShow: {
            provider = aiTab.pref("aiProvider", "openai")
            endpointField.text = aiTab.pref(provider === "openai" ? "openaiEndpoint" : "anthropicEndpoint", "")
            endpointPreferenceWrite.expectedRevision = SettingsPreferencesService.revisions.ai
        }

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
                    + " (" + (advancedEndpointDialog.provider === "openai" ? "OpenAI" : "Anthropic") + ")"
                color: Theme.textColor
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.customEndpointHint", "Leave empty for default. Set to use an OpenAI/Anthropic-compatible API endpoint.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.labelFont.pixelSize
                wrapMode: Text.Wrap
            }

            StyledTextField {
                id: endpointField
                Layout.fillWidth: true
                placeholderText: advancedEndpointDialog.provider === "openai"
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
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: advancedEndpointDialog.close()
                }
                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.button.save", "Save")
                    accessibleName: TranslationManager.translate("settings.ai.saveEndpointAccessible", "Save custom endpoint")
                    enabled: endpointPreferenceWrite.ready
                    onClicked: {
                        Keyboard.commit()
                        endpointPreferenceWrite.submit(advancedEndpointDialog.provider === "openai"
                            ? "openaiEndpoint" : "anthropicEndpoint", endpointField.text)
                    }
                }
            }
        }
    }

    // MCP Help Dialog
    // Confirm rotating the remote-access token (revokes the current URL).
    DecenzaDialog {
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
                font.pixelSize: Theme.labelFont.pixelSize
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
                        aiTab.pendingRotateRequestId = AISettingsService.request("rotateMcpToken")
                        rotateTokenDialog.close()
                        AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.rotated", "Token rotated. New connector URL generated."))
                    }
                }
            }
        }
    }

    DecenzaDialog {
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
                        font.pixelSize: Theme.bodyFont.pixelSize
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
                        font.pixelSize: Theme.labelFont.pixelSize
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
                            text: TranslationManager.translate("settings.ai.mcp.help.platformNoteRemoteFirst",
                                "The easiest way to connect is Remote Access on this tab: it works with Claude and ChatGPT on phones, the web and computers. This guide covers the advanced local setup, which connects Claude Desktop over your WiFi network.\n\nThe Discuss button on shot review pages opens your chosen AI app with the shot, through Remote Access.")
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.whatCanDo", "What can it do?")
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.capabilities",
                            "- Monitor machine state, temperature, water level\n- Browse and analyze your shot history\n- Get AI-powered dial-in advice after each shot\n- Start/stop operations (DE1 v1.0 headless only — most GHC machines require physical button press)\n- Change profiles, grinder settings, brew parameters")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.howToSetup", "How to set up")
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.steps",
                            "1. Enable MCP Server (toggle on the settings page)\n2. On your computer: install Claude Desktop (claude.ai/download)\n3. Open the setup page link on your computer (shown on the settings page when MCP is enabled)\n4. Copy and run the install command in your terminal — it installs Node.js if needed and configures Claude Desktop automatically\n5. Restart Claude Desktop\n6. Ask Claude about your espresso!\n\nBoth devices must be on the same WiFi network.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.accessLevels", "Access Levels")
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.accessDesc",
                            "Monitor Only — The AI can read data but cannot control the machine.\nControl — The AI can also start/stop operations.\nFull Automation — The AI can also change profiles and settings.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.security", "Security")
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    Text {
                        text: TranslationManager.translate("settings.ai.mcp.help.securityDesc",
                            "MCP uses an API key (included in the config) to authenticate requests. The server only listens on your local network. Enable web security (TOTP) in Connections settings for additional protection.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
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
                        // `shotServer.url` is empty and aiTab.openExternal("/mcp/setup")
                        // gets resolved against the qrc base on macOS.
                        visible: aiTab.pref("mcpEnabled", false, "mcp") && (aiTab.aiState.shotServerUrl || "").length > 0
                        text: TranslationManager.translate("settings.ai.mcp.help.openGuide", "Open Web Guide")
                        accessibleName: TranslationManager.translate("settings.ai.mcp.help.openGuideAccessible", "Open MCP setup guide in browser")
                        onClicked: {
                            aiTab.openExternal(aiTab.aiState.shotServerUrl + "/mcp/setup")
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

                Item { Layout.preferredHeight: Theme.scaled(4) }
            }
        }
    }

    // Conversation overlay panel
    DecenzaDialog {
        id: conversationOverlay
        // A dialog drawn over the tab, opened by Continue Chat; not settings of its own.
        SettingsSearch.overlay: true
        width: aiTab.width
        height: aiTab.height
        padding: 0
        modal: true
        browserUnloadDirty: aiTab.browserHasUnsavedChanges
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        onOpened: conversationInput.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: {
            Keyboard.commit()
            ConversationSession.request("save")
            if (aiTab.visible && continueConversationBtn.visible)
                continueConversationBtn.forceActiveFocus(Qt.PopupFocusReason)
        }
        background: Rectangle { color: Qt.rgba(0, 0, 0, 0.7) }

        contentItem: Rectangle {
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
                            visible: ConversationSession.state && (ConversationSession.state.contextLabel || "").length > 0
                            text: (ConversationSession.state.contextLabel || "")
                            font.pixelSize: Theme.labelFont.pixelSize
                            color: Theme.textSecondaryColor
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.conversation.clear", "Clear")
                        accessibleName: TranslationManager.translate("settings.ai.clearConversation", "Clear entire AI conversation history")
                        destructive: true
                        onClicked: {
                            ConversationSession.request("clear")
                            conversationOverlay.close()
                        }
                    }

                    Item { Layout.preferredWidth: Theme.scaled(8) }

                    StyledIconButton {
                        text: "\u00D7"
                        accessibleName: TranslationManager.translate("common.close", "Close")
                        onClicked: {
                            conversationOverlay.close()
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
                                  MarkdownRenderer.toHtml(ConversationSession.state.transcript || ""),
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
                    visible: (ConversationSession.state.busy ?? false)
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
                    visible: text.length > 0 && !ConversationSession.busy
                    text: aiTab.conversationRequestError || ConversationSession.state.errorMessage || ""
                    color: Theme.errorColor
                    font.pixelSize: Theme.labelFont.pixelSize
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
                        enabled: ConversationSession.ready && !ConversationSession.busy
                            && aiTab.pendingConversationSendRequestId.length === 0

                        Keys.onReturnPressed: aiTab.sendConversationMessage()
                        Keys.onEnterPressed: aiTab.sendConversationMessage()
                    }

                    AccessibleButton {
                        primary: conversationInput.text.length > 0
                        enabled: conversationInput.text.length > 0 && ConversationSession.ready
                            && !ConversationSession.busy && aiTab.pendingConversationSendRequestId.length === 0
                        text: TranslationManager.translate("settings.ai.conversation.send", "Send")
                        accessibleName: TranslationManager.translate("settings.ai.sendMessage", "Send message to AI")
                        onClicked: aiTab.sendConversationMessage()
                    }
                }
            }
        }

        property real _preResponseHeight: 0
        property bool _waitingForResponse: false
        property string _lastTranscript: ""
        Connections {
            target: ConversationSession
            function onCompleted(requestId, operation, success, result, reason) {
                if (operation === "loadMostRecent" && requestId === aiTab.pendingConversationLoadRequestId) {
                    aiTab.pendingConversationLoadRequestId = ""
                    if (!success) {
                        aiTab.conversationRequestError = TranslationManager.translate("conversation.operationFailed",
                            "Could not complete the conversation request. Please try again.")
                        AccessibilityManager.announce(aiTab.conversationRequestError, true)
                    }
                    conversationOverlay._waitingForResponse = false
                    Qt.callLater(function() {
                        conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                    })
                    return
                }
                if (!aiTab.completeConversationSend(requestId, operation, success)) return
                if (!success) { conversationOverlay._waitingForResponse = false; return }
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
            function onStateChanged() {
                const transcript = ConversationSession.state.transcript || ""
                if (transcript === conversationOverlay._lastTranscript) return
                conversationOverlay._lastTranscript = transcript
                if (!conversationOverlay._waitingForResponse)
                    conversationOverlay._preResponseHeight = conversationText.contentHeight
                conversationOverlay._waitingForResponse = true
                conversationText.text = Theme.replaceEmojiWithImg(MarkdownRenderer.toHtml(transcript), Theme.bodyFont.pixelSize, true)
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                    conversationText.update()
                    conversationFlickable.update()
                })
            }
        }
    }

}
