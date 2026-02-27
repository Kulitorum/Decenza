import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

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
    property string shotDebugLog: ""
    // Saved context for re-fetching shot history after conversation clear
    property string savedBeanBrand: ""
    property string savedBeanType: ""
    property string savedProfileName: ""

    // Emitted when the overlay clears pendingShotSummary (parent must handle)
    signal pendingShotSummaryCleared()
    signal closed()

    visible: false
    color: Theme.backgroundColor
    z: 200

    function open() {
        visible = true
        Qt.callLater(function() {
            conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
        })
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

        // Always fetch recent shot history as context (even for existing conversations,
        // since trimHistory() may have reduced prior shots to one-line summaries)
        overlay.savedBeanBrand = beanBrand || ""
        overlay.savedBeanType = beanType || ""
        overlay.savedProfileName = profileName || ""
        overlay.historicalContext = MainController.aiManager.getRecentShotContext(
            overlay.savedBeanBrand, overlay.savedBeanType, overlay.savedProfileName, shotId)

        // Format shot timestamp as human-readable label for AI display
        var shotTs = shotData.timestamp || 0
        var shotLabel = shotTs > 0
            ? new Date(shotTs * 1000).toLocaleString(Qt.locale(), "MMM d, HH:mm")
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

    // Consume ALL mouse/touch events - prevent pass-through to background
    MouseArea {
        anchors.fill: parent
        preventStealing: true
    }

    KeyboardAwareContainer {
        id: conversationKeyboardContainer
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        textFields: [conversationInput]

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
                            color: "white"
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
                            color: "white"
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: clearArea
                            anchors.fill: parent
                            onClicked: {
                                if (MainController.aiManager) {
                                    MainController.aiManager.clearCurrentConversation()
                                    // Re-fetch historical context so next message includes prior shots
                                    if (overlay.shotId > 0) {
                                        overlay.historicalContext = MainController.aiManager.getRecentShotContext(
                                            overlay.savedBeanBrand, overlay.savedBeanType, overlay.savedProfileName, overlay.shotId)
                                    }
                                }
                            }
                        }
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

                        Text {
                            text: "\u2715"
                            font.pixelSize: Theme.scaled(12)
                            color: Theme.textSecondaryColor

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
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    StyledTextField {
                        id: conversationInput
                        Layout.fillWidth: true
                        placeholder: overlay.pendingShotSummary.length > 0
                                     ? TranslationManager.translate("conversation.placeholder.withshot", "Ask about this shot...")
                                     : TranslationManager.translate("conversation.placeholder", "Ask a follow-up question...")
                        enabled: MainController.aiManager && MainController.aiManager.conversation &&
                                 !MainController.aiManager.conversation.busy

                        Keys.onReturnPressed: sendFollowUp()
                        Keys.onEnterPressed: sendFollowUp()

                        function sendFollowUp() {
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
                                var systemPrompt = conversation.multiShotSystemPrompt(bevType)
                                conversation.ask(systemPrompt, message)
                                sent = true
                            } else {
                                sent = conversation.followUp(message)
                            }

                            if (sent) {
                                text = ""
                                if (hasShotData)
                                    overlay.pendingShotSummaryCleared()
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(60)
                        Layout.preferredHeight: Theme.scaled(44)
                        radius: Theme.scaled(6)
                        color: conversationInput.text.length > 0 ? Theme.primaryColor : Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: conversationInput.text.length > 0 ? 0 : 1

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("conversation.send.accessible", "Send message")
                        Accessible.focusable: true
                        Accessible.onPressAction: sendArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("conversation.send", "Send")
                            font: Theme.bodyFont
                            color: conversationInput.text.length > 0 ? "white" : Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: sendArea
                            anchors.fill: parent
                            enabled: conversationInput.text.length > 0 &&
                                     MainController.aiManager && MainController.aiManager.conversation &&
                                     !MainController.aiManager.conversation.busy
                            onClicked: conversationInput.sendFollowUp()
                        }
                    }
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
            overlay.visible = false
            overlay.closed()
        }
    }

    // Scroll management for conversation updates
    property real _preResponseHeight: 0
    Connections {
        target: MainController.aiManager ? MainController.aiManager.conversation : null
        function onResponseReceived(response) {
            // Scroll to top of the new response
            Qt.callLater(function() {
                conversationFlickable.contentY = Math.max(0, overlay._preResponseHeight)
            })
        }
        function onHistoryChanged() {
            // Save height before updating — this is where the new response will start
            overlay._preResponseHeight = conversationText.contentHeight
            // Refresh conversation text
            conversationText.text = MainController.aiManager.conversation.getConversationText()
            // Scroll to bottom to show the user's message / thinking indicator
            Qt.callLater(function() {
                conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
            })
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
                        "Remote Access must be enabled before you can report bad advice. Go to Settings → Shot History and turn on Remote Access, then try again.")
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
                        color: "white"
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

        contentItem: Text {
            Accessible.ignored: true
            text: TranslationManager.translate("shotdetail.unsupportedbeverage.message",
                "AI analysis isn't available for %1 profiles yet \u2014 only espresso and filter are supported for now. Sorry about that!").arg(unsupportedBeverageDialog.beverageType)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            width: unsupportedBeverageDialog.availableWidth
        }

        standardButtons: Dialog.Ok
    }
}
