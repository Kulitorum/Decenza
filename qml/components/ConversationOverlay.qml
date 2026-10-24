import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Decenza

/**
 * ConversationOverlay - Full-screen AI conversation panel with keyboard awareness.
 *
 * Usage:
 *   ConversationOverlay {
 *       id: conversationOverlay
 *       width: parent.width
 *       height: parent.height
 *       pendingShotSummary: myPage.pendingShotSummary
 *       shotId: myPage.shotId  // DB ID for internal lookups
 *       beverageType: "espresso"
 *       onPendingShotSummaryCleared: myPage.pendingShotSummary = ""
 *   }
 */
DecenzaDialog {
    id: overlay

    // Properties set by the parent page
    property string pendingShotSummary: ""
    property int shotId: 0
    property string shotLabel: ""  // Human-readable date/time for display (e.g. "Feb 15, 14:30")
    property string beverageType: "espresso"
    property string overlayTitle: TranslationManager.translate("conversation.title", "AI Conversation")
    property bool isMistakeShot: false
    readonly property var sessionState: ConversationSession.state
    readonly property bool contextLoading: Boolean(sessionState.contextLoading)
    property string shotDebugLog: ""
    // Saved context for re-fetching shot history after conversation clear
    // The shot this conversation is about. switchConversation derives the key
    // from it, so a basket change opens a separate thread rather than continuing
    // this one.
    property var savedShot: ({})
    property string _openRequestId: ""
    property string _pendingSendRequestId: ""
    property string _pendingSendText: ""
    property var _pendingIntakeMetadataRequests: ({})
    property var _unconfirmedIntakeMetadataByShot: ({})
    property string _operationError: ""
    property bool _conversationReadyForScroll: false
    property string _lastSessionResponse: ""
    // Surface local message/intake drafts to the hosting page's beforeunload
    // guard. A sent message remains dirty until its correlated host reply.
    readonly property bool browserHasUnsavedChanges: (
        conversationInput.text.length > 0
        || (inputDialog.visible && (inputDialogTextArea.text.length > 0
                                    || inputDialog._pendingMessage.length > 0))
        || _pendingSendRequestId.length > 0
        || Object.keys(_pendingIntakeMetadataRequests).length > 0
        || Object.keys(_unconfirmedIntakeMetadataByShot).length > 0
        || intakeVisible && (intakeTasteBalance.length > 0
                             || intakeTasteBody.length > 0 || intakeOverall > 0)
    )

    // Tap-only taste intake (add-ai-taste-intake): shown as a first-open gate over
    // the conversation when the host's taste-intake preference is on, this shot hasn't
    // been offered the intake yet, and at least one taste axis is unfilled.
    // Tapping Ask composes a question from the taps + attaches the shot; Skip
    // drops straight into the normal text conversation. Fully text-free.
    property bool intakeVisible: false
    property string intakeTasteBalance: ""
    property string intakeTasteBody: ""
    property int intakeOverall: 0
    property bool intakeShowExtraction: true
    property bool intakeShowBody: true
    property bool intakeShowOverall: true

    // Build the AI-facing first-turn question from the tapped axes. English by
    // design: this is a message to the advisor, whose system prompt and dial-in
    // reference are English; the visible chip labels are localized via Tr.
    function composeIntakeMessage() {
        var parts = []
        if (intakeTasteBalance === "sour") parts.push("a bit sour")
        else if (intakeTasteBalance === "balanced") parts.push("nicely balanced")
        else if (intakeTasteBalance === "bitter") parts.push("a bit bitter")
        if (intakeTasteBody === "thin") parts.push("thin-bodied")
        else if (intakeTasteBody === "medium") parts.push("medium-bodied")
        else if (intakeTasteBody === "heavy") parts.push("heavy-bodied")

        var tail = " What do you think, and how should I adjust the next shot?"
        if (parts.length > 0) {
            let msg = "Here's how this shot tasted: " + parts.join(", ") + "."
            if (intakeOverall > 0) msg += " I'd rate it " + intakeOverall + "/100."
            return msg + tail
        }
        if (intakeOverall > 0)
            return "I'd rate this shot " + intakeOverall + "/100." + tail
        return "What do you think of this shot, and how should I adjust the next one?"
    }

    // Persist the tapped axes to the shot (durable storage + Visualizer sync via
    // the metadata-update path), then compose + send the first turn and dismiss
    // the intake.
    function submitIntake() {
        var meta = ({})
        if (intakeTasteBalance.length > 0) meta.tasteBalance = intakeTasteBalance
        if (intakeTasteBody.length > 0) meta.tasteBody = intakeTasteBody
        if (intakeOverall > 0) meta.enjoyment = intakeOverall
        if (overlay.shotId > 0 && Object.keys(meta).length > 0) {
            const requestId = HistoryService.requestUpdateShotMetadata(overlay.shotId, meta)
            if (requestId.length > 0) {
                const pending = Object.assign({}, overlay._pendingIntakeMetadataRequests)
                pending[requestId] = overlay.shotId
                overlay._pendingIntakeMetadataRequests = pending
            } else {
                const unconfirmed = Object.assign({}, overlay._unconfirmedIntakeMetadataByShot)
                unconfirmed[String(overlay.shotId)] = true
                overlay._unconfirmedIntakeMetadataByShot = unconfirmed
            }
        }

        // Reflect the taps back to the host page right away so its rating slider
        // and taste chips update, and its per-shot intake gate sees the feedback
        // on the next Ask (the overlay only ever reads the host's shot snapshot).
        overlay.tasteIntakeSubmitted(intakeTasteBalance, intakeTasteBody, intakeOverall)

        // Send first, then dismiss only if it actually went through. Dismissing
        // up front would, on mobile (where conversationInput is hidden), silently
        // discard the composed question if the send were refused.
        var msg = composeIntakeMessage()
        conversationInput.text = msg
        if (conversationInput.sendFollowUp())
            overlay.intakeVisible = false
    }

    // Emitted when the overlay clears pendingShotSummary (parent must handle)
    signal pendingShotSummaryCleared()

    // Emitted when the tap-only intake is submitted, carrying the tapped axes so
    // the host page (e.g. PostShotReviewPage) can mirror them into its live edit
    // fields immediately — the overlay has already persisted them to the DB, so
    // the host advances its baseline rather than re-saving. Empty string / 0
    // means "not tapped" and should be left untouched by the host.
    signal tasteIntakeSubmitted(string tasteBalance, string tasteBody, int overall)

    property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    modal: true
    padding: 0
    closePolicy: Dialog.CloseOnEscape
    browserUnloadDirty: browserHasUnsavedChanges
    background: Rectangle { color: Theme.backgroundColor }
    z: 200

    onOpened: {
        if (conversationInput.enabled && !overlay.isMobile)
            conversationInput.forceActiveFocus(Qt.PopupFocusReason)
        else
            conversationText.forceActiveFocus(Qt.PopupFocusReason)
    }
    onClosed: ConversationSession.request("save")
    onAboutToShow: {
        conversationFlickable.contentY = Math.max(0,
            conversationFlickable.contentHeight - conversationFlickable.height)
    }

    onVisibleChanged: {
        if (!visible && inputDialog.visible) {
            // Drop any staged Send so inputDialog.onClosed doesn't fire a
            // followUp() against this now-hidden overlay. The user explicitly
            // dismissed the conversation; honour that and don't ship the
            // message in the background.
            inputDialog._pendingMessage = ""
            inputDialog.close()
        }
    }

    /**
     * Open the conversation overlay with shot context.
     * Encapsulates the common AI button click logic from the shot page and its other callers:
     * checks beverage type, switches conversation, generates summary, and opens overlay.
     */
    function openWithShot(shotData, beanBrand, beanType, profileName, shotId) {
        var bevType = shotData.beverageType || "espresso"
        overlay.savedShot = shotData
        overlay.pendingShotSummary = ""
        overlay.intakeVisible = false
        overlay._conversationReadyForScroll = false

        // Format shot timestamp as human-readable label for AI display
        var shotTs = shotData.timestamp || 0
        var shotLabel = shotTs > 0
            ? new Date(shotTs * 1000).toLocaleString(Qt.locale(), Settings.app.use12HourTime ? "MMM d, h:mm AP" : "MMM d, HH:mm")
            : ""

        overlay.shotId = shotId
        overlay.shotLabel = shotLabel
        overlay.beverageType = bevType
        overlay.shotDebugLog = shotData.debugLog || ""
        overlay._operationError = ""
        // Stored-shot curves stay on the host. Only the review page's unsaved
        // taste edits cross the conversation RPC boundary.
        const shotContext = shotId > 0 ? {
            tasteBalance: shotData.tasteBalance || "",
            tasteBody: shotData.tasteBody || "",
            enjoyment0to100: shotData.enjoyment0to100 || 0
        } : shotData
        overlay._openRequestId = ConversationSession.request("openShot", {
            shot: shotContext,
            shotId: shotId,
            shotLabel: shotLabel
        })
        overlay.open()
    }

    Connections {
        target: ConversationSession
        function onCompleted(requestId, operation, success, result, reason) {
            if (operation === "openShot" && requestId === overlay._openRequestId) {
                overlay._openRequestId = ""
                if (!success && reason === "unsupportedBeverage") {
                    unsupportedBeverageDialog.beverageType = overlay.beverageType
                    unsupportedBeverageDialog.open()
                } else if (!success) {
                    overlay._operationError = reason
                } else {
                    overlay._conversationReadyForScroll = true
                    overlay._pendingScrollKind = ""
                    conversationText._observedTranscript = ConversationSession.state.transcript || ""
                    overlay._lastSessionResponse = ConversationSession.state.lastResponse || ""
                    conversationFlickable.contentY = Math.max(0,
                        conversationFlickable.contentHeight - conversationFlickable.height)
                    if (ConversationSession.state.tasteIntakeEnabled) {
                        overlay.intakeTasteBalance = ""
                        overlay.intakeTasteBody = ""
                        overlay.intakeOverall = 0
                        overlay.intakeShowExtraction = true
                        overlay.intakeShowBody = true
                        overlay.intakeShowOverall = true
                        overlay.intakeVisible = true
                    } else {
                        overlay.intakeVisible = false
                    }
                }
            } else if (operation === "sendMessage" && requestId === overlay._pendingSendRequestId) {
                overlay._pendingSendRequestId = ""
                if (success) {
                    if (overlay.pendingShotSummary.length > 0)
                        overlay.pendingShotSummaryCleared()
                } else {
                    overlay._operationError = reason
                    if (conversationInput.text.length === 0)
                        conversationInput.text = overlay._pendingSendText
                }
                overlay._pendingSendText = ""
            } else if (!success && operation !== "state") {
                overlay._operationError = reason
            }
        }
        function onStateChanged() {
            const state = ConversationSession.state
            if (state.shotId === overlay.shotId) {
                overlay.pendingShotSummary = state.pendingShotSummary || ""
                overlay.isMistakeShot = Boolean(state.isMistakeShot)
            }
            if (!overlay._conversationReadyForScroll)
                return
            overlay.handleTranscriptChanged()
            const response = state.lastResponse || ""
            if (response !== overlay._lastSessionResponse) {
                overlay._lastSessionResponse = response
                overlay._waitingForResponse = false
            }
        }
    }

    Connections {
        target: HistoryService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (operation !== "metadata"
                    || overlay._pendingIntakeMetadataRequests[requestId] === undefined)
                return
            const shotId = overlay._pendingIntakeMetadataRequests[requestId]
            const pending = Object.assign({}, overlay._pendingIntakeMetadataRequests)
            delete pending[requestId]
            overlay._pendingIntakeMetadataRequests = pending
            const unconfirmed = Object.assign({}, overlay._unconfirmedIntakeMetadataByShot)
            if (success)
                delete unconfirmed[String(shotId)]
            else {
                unconfirmed[String(shotId)] = true
                overlay._operationError = reason
            }
            overlay._unconfirmedIntakeMetadataByShot = unconfirmed
        }
    }

    function handleTranscriptChanged() {
        const nextTranscript = sessionState.transcript || ""
        if (nextTranscript === conversationText._observedTranscript)
            return
        conversationText._observedTranscript = nextTranscript
        const isResponse = overlay._waitingForResponse
        if (!isResponse) {
            overlay._preResponseHeight = conversationText.contentHeight
            overlay._waitingForResponse = true
        }
        overlay._pendingScrollKind = isResponse ? "preResponse" : "bottom"
    }

    // Consume ALL mouse/touch events - prevent pass-through to background
    MouseArea {
        anchors.fill: parent
        preventStealing: true
    }

    KeyboardAwareContainer {
        id: conversationKeyboardContainer
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight + _androidKeyboardHeight
        textFields: [conversationInput]

        // On Android, adjustPan can't help because this overlay fills the window.
        // Shrink the container from the bottom so the input row stays above the keyboard.
        property real _androidKeyboardHeight: {
            if (Qt.platform.os !== "android") return 0
            if (!conversationKeyboardContainer.textFieldFocused) return 0
            var kbh = Keyboard.rectangle.height / Screen.devicePixelRatio
            return kbh > 0 ? kbh : overlay.height * 0.45
        }

        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
        }

        // Main conversation content
        Rectangle {
            anchors.fill: parent
            color: Theme.backgroundColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.standardMargin
                spacing: Theme.spacingSmall

                // Context label + Clear button row at top
                RowLayout {
                    Layout.fillWidth: true

                    // Context label showing bean+profile
                    Text {
                        visible: (overlay.sessionState.contextLabel || "").length > 0
                        text: overlay.sessionState.contextLabel || ""
                        font.pixelSize: Theme.scaled(12)
                        color: Theme.textSecondaryColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Item { Layout.fillWidth: true }

                    // Report bad advice button
                    Rectangle {
                        visible: (overlay.sessionState.messageCount || 0) >= 2
                        Layout.preferredWidth: reportText.width + Theme.scaled(16)
                        Layout.preferredHeight: Theme.scaled(32)
                        radius: Theme.scaled(4)
                        color: Theme.warningColor
                        opacity: 0.8

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("conversation.report.accessible", "Report bad AI advice")
                        Accessible.focusable: true
                        Accessible.onPressAction: reportArea.clicked(null)

                        Text {
                            id: reportText
                            anchors.centerIn: parent
                            text: TranslationManager.translate("conversation.report", "Report")
                            font.pixelSize: Theme.scaled(12)
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: reportArea
                            anchors.fill: parent
                            onClicked: reportWebDialog.open()
                        }
                    }

                    // Clear conversation button
                    Rectangle {
                        Layout.preferredWidth: clearText.width + Theme.scaled(16)
                        Layout.preferredHeight: Theme.scaled(32)
                        radius: Theme.scaled(4)
                        color: Theme.errorColor
                        opacity: 0.8

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("conversation.clear.accessible", "Clear conversation")
                        Accessible.focusable: true
                        Accessible.onPressAction: clearArea.clicked(null)

                        Text {
                            id: clearText
                            anchors.centerIn: parent
                            text: TranslationManager.translate("conversation.clear", "Clear")
                            font.pixelSize: Theme.scaled(12)
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: clearArea
                            anchors.fill: parent
                            onClicked: {
                                ConversationSession.request("clear")
                                overlay._waitingForResponse = false
                                overlay._pendingScrollKind = ""
                                overlay._preResponseHeight = 0
                            }
                        }
                    }

                    // Spacer to avoid overlap with global hide-keyboard button on mobile
                    // (button is scaled(36) wide + standardMargin from edge)
                    Item {
                        visible: overlay.isMobile
                        Layout.preferredWidth: Theme.scaled(52)
                        Layout.preferredHeight: 1
                    }
                }

                // Conversation history
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
                        // Markdown -> HTML, THEN emoji. The comment here used to claim
                        // "Qt's Markdown importer passes the inline <img> through" — that is
                        // FALSE, and every AI reply was silently losing everything after its
                        // first emoji. Converting first means the parser never sees an <img>.
                        property string _observedTranscript: ""
                        text: Theme.replaceEmojiWithImg(
                                  MarkdownRenderer.toHtml(overlay.sessionState.transcript || ""),
                                  Theme.bodyFont.pixelSize, true)
                        // RichText: the binding above now produces HTML. Left as MarkdownText
                        // this feeds <img> tags to the Markdown parser — the exact truncation
                        // this change fixes. Three reviewers caught that I switched only two of
                        // the four sites. See markdownrenderer.h.
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
                        // text now carries <img> emoji substitutions; strip the
                        // HTML tags (toAccessibleText) before stripping Markdown
                        // so VoiceOver/TalkBack don't read literal "<img src=…>".
                        // `text` is HTML now; toAccessibleText strips the tags and emoji.
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

                        // Apply any pending scroll target the moment the new
                        // Markdown layout is reflected in contentHeight, then
                        // wake the render thread. Driving the scroll off the
                        // layout-completion signal (rather than Qt.callLater,
                        // which fires before the layout pass settles) is what
                        // keeps the response visible on mobile without needing
                        // a tap to wake the render loop.
                        onContentHeightChanged: {
                            var kind = overlay._pendingScrollKind
                            if (kind === "bottom") {
                                conversationFlickable.contentY = Math.max(0,
                                    conversationFlickable.contentHeight - conversationFlickable.height)
                                overlay._pendingScrollKind = ""
                            } else if (kind === "preResponse") {
                                conversationFlickable.contentY = Math.max(0, overlay._preResponseHeight)
                                overlay._pendingScrollKind = ""
                            }
                            // Force a scene graph repaint on every layout pass.
                            // The render thread on mobile can stay asleep through
                            // property changes that originate from network reply
                            // signals; this catches any async layout finalization
                            // (e.g. Markdown reflow) that happens after the scroll.
                            conversationText.update()
                            conversationFlickable.update()
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

                // Loading indicator
                RowLayout {
                    visible: ConversationSession.busy
                    Layout.fillWidth: true

                    BusyIndicator {
                        running: true
                        Layout.preferredWidth: Theme.scaled(24)
                        Layout.preferredHeight: Theme.scaled(24)
                        palette.dark: Theme.primaryColor
                    }

                    Text {
                        text: TranslationManager.translate("conversation.thinking", "Thinking...")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                    }
                }

                // Error message display (with Retry when the failed turn can be re-sent)
                Rectangle {
                    id: errorBox
                    readonly property string _error: overlay.sessionState.errorMessage || overlay._operationError
                    visible: (_error.length > 0) && !ConversationSession.busy
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(36)
                    radius: Theme.scaled(4)
                    color: Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.15)
                    border.color: Theme.errorColor
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(10)
                        anchors.rightMargin: Theme.scaled(6)
                        spacing: Theme.scaled(8)

                        Text {
                            Layout.fillWidth: true
                            text: overlay.sessionState.errorMessage ||
                                (overlay._operationError.length > 0
                                    ? TranslationManager.translate("conversation.operationFailed",
                                        "Could not complete the conversation request. Please try again.")
                                    : "")
                            font.pixelSize: Theme.scaled(11)
                            color: Theme.errorColor
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        // Retry: re-sends the preserved failed turn without retyping.
                        Rectangle {
                            id: retryButton
                            visible: Boolean(overlay.sessionState.canRetry)
                            Layout.preferredWidth: Theme.scaled(64)
                            Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(6)
                            color: Theme.errorColor
                            border.width: 0

                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("conversation.retry.accessible", "Retry sending message")
                            Accessible.focusable: true
                            Accessible.onPressAction: retryArea.clicked(null)

                            Text {
                                anchors.centerIn: parent
                                text: TranslationManager.translate("conversation.retry", "Retry")
                                font: Theme.bodyFont
                                color: Theme.primaryContrastColor
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: retryArea
                                anchors.fill: parent
                                enabled: retryButton.visible
                                onClicked: {
                                    ConversationSession.request("retry")
                                }
                            }
                        }
                    }
                }

                // Mistake shot notice
                Rectangle {
                    visible: overlay.visible &&
                             overlay.pendingShotSummary.length === 0 &&
                             overlay.isMistakeShot
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(28)
                    radius: Theme.scaled(4)
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                    border.color: Theme.warningColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: TranslationManager.translate("conversation.mistakeshot",
                            "Shot excluded \u2014 too short or too little yield to be useful for dial-in")
                        font.pixelSize: Theme.scaled(11)
                        color: Theme.warningColor
                    }
                }

                // Shot data attached indicator
                Rectangle {
                    visible: overlay.pendingShotSummary.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(28)
                    radius: Theme.scaled(4)
                    color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                    border.color: Theme.primaryColor
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(8)
                        anchors.rightMargin: Theme.scaled(8)
                        spacing: Theme.scaled(6)

                        Image {
                            source: Theme.emojiToImage("\uD83D\uDCCA")
                            sourceSize.width: Theme.scaled(12)
                            sourceSize.height: Theme.scaled(12)
                            Layout.preferredWidth: Theme.scaled(12)
                            Layout.preferredHeight: Theme.scaled(12)
                        }

                        Text {
                            text: TranslationManager.translate("conversation.shotattached", "Shot data will be included with your message")
                            font.pixelSize: Theme.scaled(11)
                            color: Theme.primaryColor
                            Layout.fillWidth: true
                        }

                        Item {
                            Layout.preferredWidth: Theme.scaled(16)
                            Layout.preferredHeight: Theme.scaled(16)

                            ColoredIcon {
                                anchors.centerIn: parent
                                source: "qrc:/icons/cross.svg"
                                iconWidth: Theme.scaled(10)
                                iconHeight: Theme.scaled(10)
                                iconColor: Theme.textSecondaryColor
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("conversation.dismissshot.accessible", "Remove shot data")
                            Accessible.focusable: true
                            Accessible.onPressAction: dismissShotArea.clicked(null)

                            MouseArea {
                                id: dismissShotArea
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(4)
                                onClicked: {
                                    // User explicitly declined to attach this shot —
                                    // the next message is a genuine free-form question,
                                    // not about overlay.shotId. Without this, sendFollowUp()
                                    // would still stamp the dismissed shot's id onto an
                                    // unrelated turn (setShotIdForCurrentTurn guard only
                                    // checks shotId > 0, not "still relevant").
                                    overlay.shotId = 0
                                    overlay.pendingShotSummaryCleared()
                                }
                            }
                        }
                    }
                }

                // Input row
                RowLayout {
                    id: inputRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    // Shared by conversationInput, mobileInputButton, and send controls
                    property bool canSend: ConversationSession.ready &&
                                           !ConversationSession.busy &&
                                           !overlay.contextLoading
                    property string inputPlaceholder: overlay.pendingShotSummary.length > 0
                                     ? TranslationManager.translate("conversation.placeholder.withshot", "Ask about this shot...")
                                     : TranslationManager.translate("conversation.placeholder", "Ask a follow-up question...")

                    // Input field that holds the text and send logic.
                    // Visible inline on desktop; hidden on mobile where the
                    // fullscreen inputDialog is used instead.
                    StyledTextField {
                        id: conversationInput
                        Layout.fillWidth: true
                        visible: !overlay.isMobile
                        placeholder: parent.inputPlaceholder
                        enabled: parent.canSend

                        Keys.onReturnPressed: sendFollowUp()
                        Keys.onEnterPressed: sendFollowUp()

                        // Returns true if the message was actually sent, so callers
                        // (e.g. submitIntake) can avoid dismissing UI on a refused send.
                        function sendFollowUp() {
                            Keyboard.commit()
                            if (text.trim().length === 0 || !inputRow.canSend)
                                return false
                            overlay._pendingSendText = text
                            overlay._pendingSendRequestId = ConversationSession.request(
                                "sendMessage", { text: text })
                            text = ""
                            conversationInput.focus = false
                            Keyboard.hide()
                            return overlay._pendingSendRequestId.length > 0
                        }
                    }

                    // Mobile: tap target that opens fullscreen input dialog.
                    // Avoids keyboard opening/closing thrashing the conversation layout.
                    Rectangle {
                        id: mobileInputButton
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(44)
                        radius: Theme.scaled(6)
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: 1
                        visible: overlay.isMobile
                        opacity: inputTapArea.enabled ? 1.0 : 0.5
                        Accessible.ignored: true

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.verticalCenter: parent.verticalCenter
                            text: inputRow.inputPlaceholder
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            id: inputTapArea
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("conversation.input.accessible", "Type a message")
                            accessibleItem: mobileInputButton
                            enabled: inputRow.canSend
                            onAccessibleClicked: {
                                inputDialogTextArea.text = ""
                                inputDialog.open()
                                // forceActiveFocus is in inputDialog.onOpened to avoid
                                // keyboard flash before dialog animation completes
                            }
                        }
                    }

                    // Desktop: inline send button
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(60)
                        Layout.preferredHeight: Theme.scaled(44)
                        radius: Theme.scaled(6)
                        color: conversationInput.text.length > 0 ? Theme.primaryColor : Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: conversationInput.text.length > 0 ? 0 : 1
                        visible: !overlay.isMobile

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("conversation.send.accessible", "Send message")
                        Accessible.focusable: true
                        Accessible.onPressAction: sendArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("conversation.send", "Send")
                            font: Theme.bodyFont
                            color: conversationInput.text.length > 0 ? Theme.primaryContrastColor : Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: sendArea
                            anchors.fill: parent
                            enabled: conversationInput.text.length > 0 && inputRow.canSend
                            onClicked: conversationInput.sendFollowUp()
                        }
                    }
                }
            }
        }
    }

    // Tap-only taste intake layer (add-ai-taste-intake). Covers the conversation
    // content on first open. `anchors.bottomMargin: Theme.bottomBarHeight` keeps
    // this layer clear of the BottomBar's strip so Back stays tappable — the
    // z:50 here would otherwise sit ABOVE the (z:0) BottomBar, so it's the
    // geometry, not declaration order, that protects the bar.
    Rectangle {
        id: intakeLayer
        visible: overlay.intakeVisible
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        color: Theme.backgroundColor
        z: 50

        // Swallow taps so nothing behind the layer reacts.
        MouseArea { anchors.fill: parent }

        Tr { id: trIntakeTitle; key: "tasteIntake.title"; fallback: "How did this shot taste?"; visible: false }
        Tr { id: trIntakeSubtitle; key: "tasteIntake.subtitle"; fallback: "Tap what fits — or just ask."; visible: false }
        Tr { id: trIntakeSkip; key: "tasteIntake.skip"; fallback: "Skip"; visible: false }
        Tr { id: trIntakeAsk; key: "tasteIntake.ask"; fallback: "Ask"; visible: false }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.standardMargin
            spacing: Theme.spacingMedium

            Text {
                text: trIntakeTitle.text
                font: Theme.titleFont
                color: Theme.textColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: trIntakeTitle.text
            }
            Text {
                text: trIntakeSubtitle.text
                font.pixelSize: Theme.scaled(13)
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            TastePicker {
                id: intakePicker
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                showExtraction: overlay.intakeShowExtraction
                showBody: overlay.intakeShowBody
                showOverall: overlay.intakeShowOverall
                tasteBalance: overlay.intakeTasteBalance
                tasteBody: overlay.intakeTasteBody
                overall: overlay.intakeOverall
                onTasteBalanceModified: function(value) { overlay.intakeTasteBalance = value }
                onTasteBodyModified: function(value) { overlay.intakeTasteBody = value }
                onOverallModified: function(value) { overlay.intakeOverall = value }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                AccessibleButton {
                    text: trIntakeSkip.text
                    accessibleName: trIntakeSkip.text
                    accessibleDescription: TranslationManager.translate("tasteIntake.skip.desc", "Skip and type your own question")
                    onClicked: overlay.intakeVisible = false
                }
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: trIntakeAsk.text
                    accessibleName: trIntakeAsk.text
                    primary: true
                    icon.source: "qrc:/icons/sparkle.svg"
                    tintIcon: true
                    // Wait until any background context load finishes and the
                    // conversation is idle, so the composed turn sends cleanly.
                    enabled: inputRow.canSend
                    onClicked: overlay.submitIntake()
                }
            }
        }
    }

    // Fullscreen input dialog for mobile — avoids keyboard thrashing the conversation layout.
    // Opens above the keyboard with a large text area; Send dismisses and returns to conversation.
    DecenzaDialog {
        id: inputDialog
        parent: Overlay.overlay
        modal: true
        padding: 0
        closePolicy: Dialog.CloseOnEscape
        browserUnloadDirty: visible
            && (inputDialogTextArea.text.length > 0 || _pendingMessage.length > 0)
        onOpened: inputDialogTextArea.forceActiveFocus()

        // Send button stages the message here, then closes. The actual send
        // runs in onClosed so conversation.followUp() / ask() — which emit
        // historyChanged synchronously — only mutate the overlay once the
        // dialog has fully animated away and the keyboard has finished hiding.
        // Sending during the close transition leaves the render thread on
        // mobile in a stale-paint state and the response flashes-then-vanishes
        // until the user taps the screen.
        property string _pendingMessage: ""
        onClosed: {
            if (_pendingMessage.length === 0) return
            conversationInput.text = _pendingMessage
            _pendingMessage = ""
            conversationInput.sendFollowUp()
        }

        // Keyboard height for iOS shrink-above-keyboard layout.
        // Android uses adjustPan (window shifts), so always full height.
        property real keyboardHeight: {
            if (!inputDialogTextArea.activeFocus) return 0
            var kbh = Keyboard.rectangle.height
            return kbh > 0 ? kbh : parent.height * 0.45
        }

        width: parent.width
        height: {
            if (Qt.platform.os === "android") return parent.height
            if (keyboardHeight > 0) return parent.height - keyboardHeight
            return parent.height
        }
        x: 0
        y: 0

        background: Rectangle {
            color: Theme.surfaceColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header with Cancel and Send
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)

                AccessibleButton {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                    onClicked: inputDialog.close()
                }

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("conversation.input.title", "Message")
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    Accessible.ignored: true
                }

                AccessibleButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("conversation.send", "Send")
                    accessibleName: TranslationManager.translate("conversation.send.accessible", "Send message")
                    primary: true
                    enabled: inputDialogTextArea.text.length > 0 && inputRow.canSend
                    onClicked: {
                        Keyboard.commit()
                        // Stage the message and close; inputDialog.onClosed runs
                        // sendFollowUp() once the dialog + keyboard transitions
                        // are complete, so the overlay isn't being mutated on
                        // an obscured/animating surface.
                        inputDialog._pendingMessage = inputDialogTextArea.text
                        inputDialog.close()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Large text editing area
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.scaled(12)
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: inputDialogTextArea
                    font: Theme.bodyFont
                    color: Theme.textColor
                    wrapMode: TextArea.Wrap
                    leftPadding: Theme.scaled(8)
                    rightPadding: Theme.scaled(8)
                    topPadding: Theme.scaled(8)
                    bottomPadding: Theme.scaled(8)
                    background: Rectangle {
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                    }

                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("conversation.input.accessible", "Type a message")
                    Accessible.description: text
                    Accessible.focusable: true
                }
            }
        }
    }

    // Bottom bar for conversation overlay
    BottomBar {
        id: conversationBottomBar
        title: overlay.overlayTitle
        showBackButton: true
        onBackClicked: {
            overlay.close()
        }
    }

    // Scroll management for conversation updates.
    // Scroll targets are staged here and applied by conversationText.onContentHeightChanged
    // once the new Markdown has fully laid out — driving the scroll + repaint
    // off the layout-completion event (instead of Qt.callLater, which fires
    // before the layout pass settles) is what keeps the response visible on
    // mobile without needing a tap to wake the render loop.
    property real _preResponseHeight: 0
    property bool _waitingForResponse: false
    property string _pendingScrollKind: ""  // "" | "bottom" | "preResponse"

    // Report bad advice — directs to web interface
    DecenzaDialog {
        id: reportWebDialog
        anchors.centerIn: parent
        width: Theme.scaled(380)
        modal: true
        padding: 0

        background: Rectangle {
            implicitHeight: 0  // Break Material style binding loop on Dialog.implicitHeight
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                RowLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(12)

                    Text {
                        text: TranslationManager.translate("aiReport.title", "Report Bad Advice")
                        font: Theme.titleFont
                        color: Theme.textColor
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            Text {
                text: ConversationSession.reportUrl.length > 0
                    ? TranslationManager.translate("aiReport.webMessage",
                        "To report issues with the AI advisor, use the AI Conversations page in the web interface to view and download the conversation, then share it with the developer.")
                    : TranslationManager.translate("aiReport.webMessageDisabled",
                        "Remote Access must be enabled before you can report bad advice. Go to Settings → History & Data and turn on Remote Access, then try again.")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            Grid {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                columns: 2
                spacing: Theme.scaled(10)

                property real buttonWidth: (width - spacing) / 2
                property real buttonHeight: Theme.scaled(50)

                AccessibleButton {
                    id: cancelButton
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    text: TranslationManager.translate("aiReport.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("aiReport.cancelAccessible", "Cancel report")
                    onClicked: reportWebDialog.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondaryColor
                    }
                    contentItem: Text {
                        text: cancelButton.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AccessibleButton {
                    id: openWebButton
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    enabled: ConversationSession.reportUrl.length > 0
                    opacity: enabled ? 1.0 : 0.5
                    text: TranslationManager.translate("aiReport.openWeb", "Open")
                    accessibleName: TranslationManager.translate("aiReport.openWebAccessible", "Open AI Conversations in web browser")
                    onClicked: {
                        if (ConversationSession.reportUrl.length > 0)
                            Qt.openUrlExternally(ConversationSession.reportUrl)
                        reportWebDialog.close()
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
                        radius: Theme.buttonRadius
                        color: openWebButton.down || openWebButton.isPressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: openWebButton.text
                        font: Theme.bodyFont
                        color: Theme.primaryContrastColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    // Unsupported beverage type dialog (used by openWithShot)
    DecenzaDialog {
        id: unsupportedBeverageDialog
        property string beverageType: ""
        title: TranslationManager.translate("shotdetail.unsupportedbeverage.title", "AI Not Available")
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        padding: Theme.scaled(16)
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                Accessible.ignored: true
                text: TranslationManager.translate("shotdetail.unsupportedbeverage.message",
                    "AI analysis isn't available for %1 profiles yet \u2014 only espresso and filter are supported for now. Sorry about that!").arg(unsupportedBeverageDialog.beverageType)
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            AccessibleButton {
                text: TranslationManager.translate("common.ok", "OK")
                accessibleName: TranslationManager.translate("common.ok", "OK")
                Layout.alignment: Qt.AlignRight
                onClicked: unsupportedBeverageDialog.close()
            }
        }
    }
}
