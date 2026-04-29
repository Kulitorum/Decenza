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
 *       anchors.fill: parent
 *       pendingShotSummary: myPage.pendingShotSummary
 *       shotId: myPage.shotId  // DB ID for internal lookups
 *       beverageType: "espresso"
 *       onPendingShotSummaryCleared: myPage.pendingShotSummary = ""
 *   }
 */
Rectangle {
    id: overlay

    // Properties set by the parent page
    property string pendingShotSummary: ""
    property int shotId: 0
    property string shotLabel: ""  // Human-readable date/time for display (e.g. "Feb 15, 14:30")
    property string beverageType: "espresso"
    property string overlayTitle: TranslationManager.translate("conversation.title", "AI Conversation")
    property bool isMistakeShot: false
    property string historicalContext: ""
    property bool contextLoading: false
    property string shotDebugLog: ""
    // Saved context for re-fetching shot history after conversation clear
    property string savedBeanBrand: ""
    property string savedBeanType: ""
    property string savedProfileName: ""

    // Emitted when the overlay clears pendingShotSummary (parent must handle)
    signal pendingShotSummaryCleared()
    signal closed()

    property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    visible: false
    color: Theme.backgroundColor
    z: 200

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

    function open() {
        visible = true
        // Scroll synchronously now that the overlay is visible. The TextArea
        // bound `text` to getConversationText() at creation, so contentHeight
        // is already current — no need to defer through Qt.callLater (which
        // would race against any pending Markdown reflow).
        conversationFlickable.contentY = Math.max(0,
            conversationFlickable.contentHeight - conversationFlickable.height)
    }

    /**
     * Open the conversation overlay with shot context.
     * Encapsulates the common AI button click logic from PostShotReviewPage and ShotDetailPage:
     * checks beverage type, switches conversation, generates summary, and opens overlay.
     */
    function openWithShot(shotData, beanBrand, beanType, profileName, shotId) {
        if (!MainController.aiManager || !MainController.aiManager.conversation) return

        // Don't switch conversations while a request is in-flight
        if (MainController.aiManager.conversation.busy) return

        // Check beverage type is supported
        var bevType = (shotData.beverageType || "espresso")
        if (!MainController.aiManager.isSupportedBeverageType(bevType)) {
            unsupportedBeverageDialog.beverageType = bevType
            unsupportedBeverageDialog.open()
            return
        }

        // Switch to the right conversation for this bean+profile
        MainController.aiManager.switchConversation(
            beanBrand || "",
            beanType || "",
            profileName || ""
        )

        // Fetch recent shot history as context on a background thread.
        // Result arrives via recentShotContextReady signal → Connections handler below.
        overlay.savedBeanBrand = beanBrand || ""
        overlay.savedBeanType = beanType || ""
        overlay.savedProfileName = profileName || ""
        overlay.historicalContext = ""
        overlay.contextLoading = true
        MainController.aiManager.requestRecentShotContext(
            overlay.savedBeanBrand, overlay.savedBeanType, overlay.savedProfileName, shotId)

        // Format shot timestamp as human-readable label for AI display
        var shotTs = shotData.timestamp || 0
        var shotLabel = shotTs > 0
            ? new Date(shotTs * 1000).toLocaleString(Qt.locale(), Settings.app.use12HourTime ? "MMM d, h:mm AP" : "MMM d, HH:mm")
            : ""

        // Check for mistake shot
        var isMistake = MainController.aiManager.isMistakeShot(shotData)
        if (isMistake) {
            overlay.pendingShotSummary = ""
        } else {
            // Generate shot summary from history shot data, with recipe dedup and change detection
            var raw = MainController.aiManager.generateHistoryShotSummary(shotData)
            overlay.pendingShotSummary = MainController.aiManager.conversation.processShotForConversation(raw, shotLabel)
        }

        overlay.shotId = shotId
        overlay.shotLabel = shotLabel
        overlay.beverageType = bevType
        overlay.isMistakeShot = isMistake
        overlay.shotDebugLog = shotData.debugLog || ""
        overlay.open()
    }

    // Receive async historical context from background thread
    Connections {
        target: MainController.aiManager
        function onRecentShotContextReady(context) {
            overlay.historicalContext = context
            overlay.contextLoading = false
        }
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
            var kbh = Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio
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
                        visible: MainController.aiManager && MainController.aiManager.conversation &&
                                 MainController.aiManager.conversation.contextLabel.length > 0
                        text: MainController.aiManager && MainController.aiManager.conversation ? (MainController.aiManager.conversation.contextLabel || "") : ""
                        font.pixelSize: Theme.scaled(12)
                        color: Theme.textSecondaryColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Item { Layout.fillWidth: true }

                    // Report bad advice button
                    Rectangle {
                        visible: MainController.aiManager && MainController.aiManager.conversation &&
                                 MainController.aiManager.conversation.messageCount >= 2
                        width: reportText.width + Theme.scaled(16)
                        height: Theme.scaled(32)
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
                        width: clearText.width + Theme.scaled(16)
                        height: Theme.scaled(32)
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
                                if (MainController.aiManager) {
                                    MainController.aiManager.clearCurrentConversation()
                                    // Reset the scroll state machine. clearHistory() emits
                                    // historyChanged but never responseReceived/errorOccurred,
                                    // so without this the next user-send onHistoryChanged would
                                    // see _waitingForResponse left at true and treat the send
                                    // as a response — scrolling to a stale _preResponseHeight
                                    // instead of the bottom.
                                    overlay._waitingForResponse = false
                                    overlay._pendingScrollKind = ""
                                    overlay._preResponseHeight = 0
                                    // Re-fetch historical context on background thread
                                    if (overlay.shotId > 0) {
                                        overlay.historicalContext = ""
                                        overlay.contextLoading = true
                                        MainController.aiManager.requestRecentShotContext(
                                            overlay.savedBeanBrand, overlay.savedBeanType, overlay.savedProfileName, overlay.shotId)
                                    }
                                }
                            }
                        }
                    }

                    // Spacer to avoid overlap with global hide-keyboard button on mobile
                    // (button is scaled(36) wide + standardMargin from edge)
                    Item {
                        visible: overlay.isMobile
                        width: Theme.scaled(52)
                        height: 1
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
                        text: MainController.aiManager && MainController.aiManager.conversation
                              ? MainController.aiManager.conversation.getConversationText()
                              : ""
                        textFormat: Text.MarkdownText
                        wrapMode: TextEdit.WordWrap
                        readOnly: true
                        selectByMouse: true
                        font: Theme.bodyFont
                        color: Theme.textColor
                        background: null
                        padding: 0

                        Accessible.role: Accessible.EditableText
                        Accessible.name: TranslationManager.translate("conversation.accessible.transcript", "AI conversation transcript")
                        Accessible.description: Theme.stripMarkdown(text)
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
                    visible: MainController.aiManager && MainController.aiManager.conversation &&
                             MainController.aiManager.conversation.busy
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

                // Error message display
                Rectangle {
                    visible: MainController.aiManager && MainController.aiManager.conversation &&
                             MainController.aiManager.conversation.errorMessage.length > 0 &&
                             !MainController.aiManager.conversation.busy
                    Layout.fillWidth: true
                    height: Theme.scaled(28)
                    radius: Theme.scaled(4)
                    color: Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.15)
                    border.color: Theme.errorColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: MainController.aiManager && MainController.aiManager.conversation ? (MainController.aiManager.conversation.errorMessage || "") : ""
                        font.pixelSize: Theme.scaled(11)
                        color: Theme.errorColor
                    }
                }

                // Mistake shot notice
                Rectangle {
                    visible: overlay.visible &&
                             overlay.pendingShotSummary.length === 0 &&
                             overlay.isMistakeShot
                    Layout.fillWidth: true
                    height: Theme.scaled(28)
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
                    height: Theme.scaled(28)
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
                            width: Theme.scaled(12)
                            height: Theme.scaled(12)
                        }

                        Text {
                            text: TranslationManager.translate("conversation.shotattached", "Shot data will be included with your message")
                            font.pixelSize: Theme.scaled(11)
                            color: Theme.primaryColor
                            Layout.fillWidth: true
                        }

                        Item {
                            width: Theme.scaled(16)
                            height: Theme.scaled(16)

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
                                onClicked: overlay.pendingShotSummaryCleared()
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
                    property bool canSend: MainController.aiManager && MainController.aiManager.conversation &&
                                           !MainController.aiManager.conversation.busy &&
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

                        function sendFollowUp() {
                            Qt.inputMethod.commit()
                            if (text.length === 0) return
                            if (!MainController.aiManager || !MainController.aiManager.conversation) return

                            var conversation = MainController.aiManager.conversation
                            var message = text

                            // If there's a pending shot, include it with the user's question
                            var hasShotData = overlay.pendingShotSummary.length > 0
                            if (hasShotData) {
                                // For new conversations with historical context, prepend previous shots
                                var shotSection = "## Shot (" + overlay.shotLabel + ")\n\nHere's my latest shot:\n\n" +
                                                  overlay.pendingShotSummary + "\n\n" + text
                                if (overlay.historicalContext.length > 0) {
                                    message = overlay.historicalContext + "\n\n" + shotSection
                                    overlay.historicalContext = ""
                                } else {
                                    message = shotSection
                                }
                            }

                            // Use ask() for new conversation, followUp() for existing
                            var sent = false
                            if (!conversation.hasHistory) {
                                var bevType = (overlay.beverageType || "espresso").toLowerCase()
                                var systemPrompt = conversation.multiShotSystemPrompt(bevType, overlay.savedProfileName)
                                conversation.ask(systemPrompt, message)
                                sent = true
                            } else {
                                sent = conversation.followUp(message)
                            }

                            if (sent) {
                                text = ""
                                // Dismiss keyboard after sending
                                conversationInput.focus = false
                                Qt.inputMethod.hide()
                                if (hasShotData)
                                    overlay.pendingShotSummaryCleared()
                            }
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

    // Fullscreen input dialog for mobile — avoids keyboard thrashing the conversation layout.
    // Opens above the keyboard with a large text area; Send dismisses and returns to conversation.
    Dialog {
        id: inputDialog
        parent: Overlay.overlay
        modal: true
        padding: 0
        closePolicy: Dialog.CloseOnEscape
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
            var kbh = Qt.inputMethod.keyboardRectangle.height
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
                        Qt.inputMethod.commit()
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
            // Save conversation before closing
            if (MainController.aiManager && MainController.aiManager.conversation) {
                MainController.aiManager.conversation.saveToStorage()
            }
            overlay.contextLoading = false  // Reset in case async result never arrives
            overlay.visible = false
            overlay.closed()
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
    Connections {
        target: MainController.aiManager ? MainController.aiManager.conversation : null
        function onResponseReceived(response) {
            overlay._waitingForResponse = false
            // Scroll target was staged by onHistoryChanged (which aiconversation.cpp
            // emits immediately before responseReceived). In the common case the
            // text= assignment changed contentHeight, so onContentHeightChanged
            // already ran the scroll and consumed _pendingScrollKind. Clear it
            // defensively here in case contentHeight didn't change (e.g. response
            // produced an identical height) — otherwise the stale "preResponse"
            // sentinel would be consumed by the next unrelated layout pass.
            overlay._pendingScrollKind = ""
        }
        function onErrorOccurred(error) {
            // Reset flag so the next send captures scroll position correctly
            overlay._waitingForResponse = false
            overlay._pendingScrollKind = ""
        }
        function onHistoryChanged() {
            var isResponse = overlay._waitingForResponse
            // Only save the scroll target when the user sends (before response arrives).
            // The response triggers historyChanged too — its scroll target is
            // _preResponseHeight, captured below on the user-send path.
            if (!isResponse) {
                overlay._preResponseHeight = conversationText.contentHeight
                overlay._waitingForResponse = true
            }
            // Stage the scroll target before mutating text. The text= assignment
            // updates contentHeight synchronously, which fires
            // conversationText.onContentHeightChanged in the same JS turn —
            // that handler reads _pendingScrollKind, performs the scroll, and
            // wakes the render thread.
            overlay._pendingScrollKind = isResponse ? "preResponse" : "bottom"
            conversationText.text = MainController.aiManager.conversation.getConversationText()
        }
    }

    // Report bad advice — directs to web interface
    Dialog {
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
                text: MainController.shotServer && MainController.shotServer.running
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
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AccessibleButton {
                    width: parent.buttonWidth
                    height: parent.buttonHeight
                    enabled: MainController.shotServer && MainController.shotServer.running
                    opacity: enabled ? 1.0 : 0.5
                    text: TranslationManager.translate("aiReport.openWeb", "Open")
                    accessibleName: TranslationManager.translate("aiReport.openWebAccessible", "Open AI Conversations in web browser")
                    onClicked: {
                        if (MainController.shotServer)
                            Qt.openUrlExternally(MainController.shotServer.url + "/ai-conversations")
                        reportWebDialog.close()
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(60)
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
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
    Dialog {
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
