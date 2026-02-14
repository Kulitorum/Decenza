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
 *       shotId: myPage.shotId
 *       beverageType: "espresso"
 *       onPendingShotSummaryCleared: myPage.pendingShotSummary = ""
 *   }
 */
Rectangle {
    id: overlay

    // Properties set by the parent page
    property string pendingShotSummary: ""
    property int shotId: 0
    property string beverageType: "espresso"
    property string overlayTitle: TranslationManager.translate("conversation.title", "AI Conversation")
    property bool isMistakeShot: false
    property string historicalContext: ""

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

        // For new conversations, include recent shot history as context
        if (!MainController.aiManager.conversation.hasHistory) {
            overlay.historicalContext = MainController.aiManager.getRecentShotContext(
                beanBrand || "", beanType || "", profileName || "", shotId)
        } else {
            overlay.historicalContext = ""
        }

        // Check for mistake shot
        if (MainController.aiManager.isMistakeShot(shotData)) {
            overlay.pendingShotSummary = ""
        } else {
            // Generate shot summary from history shot data, with recipe dedup and change detection
            var raw = MainController.aiManager.generateHistoryShotSummary(shotData)
            overlay.pendingShotSummary = MainController.aiManager.conversation.processShotForConversation(raw, shotId)
        }

        overlay.shotId = shotId
        overlay.beverageType = bevType
        overlay.isMistakeShot = MainController.aiManager.isMistakeShot(shotData)
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

                    // Clear conversation button
                    Rectangle {
                        width: clearText.width + Theme.scaled(16)
                        height: Theme.scaled(32)
                        radius: Theme.scaled(4)
                        color: Theme.errorColor
                        opacity: 0.8

                        Text {
                            id: clearText
                            anchors.centerIn: parent
                            text: TranslationManager.translate("conversation.clear", "Clear")
                            font.pixelSize: Theme.scaled(12)
                            color: "white"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (MainController.aiManager) {
                                    MainController.aiManager.clearCurrentConversation()
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

                        Text {
                            text: "\uD83D\uDCCA"
                            font.pixelSize: Theme.scaled(12)
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

                            MouseArea {
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
                            if (overlay.pendingShotSummary.length > 0) {
                                // For new conversations with historical context, prepend previous shots
                                var shotSection = "## Shot #" + overlay.shotId + "\n\nHere's my latest shot:\n\n" +
                                                  overlay.pendingShotSummary + "\n\n" + text
                                if (overlay.historicalContext.length > 0 && !conversation.hasHistory) {
                                    message = overlay.historicalContext + "\n\n" + shotSection
                                    overlay.historicalContext = ""
                                } else {
                                    message = shotSection
                                }
                                overlay.pendingShotSummaryCleared()
                            }

                            // Use ask() for new conversation, followUp() for existing
                            if (!conversation.hasHistory) {
                                var bevType = (overlay.beverageType || "espresso").toLowerCase()
                                var systemPrompt = conversation.multiShotSystemPrompt(bevType)
                                conversation.ask(systemPrompt, message)
                                text = ""
                            } else {
                                if (conversation.followUp(message)) {
                                    text = ""
                                }
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

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("conversation.send", "Send")
                            font: Theme.bodyFont
                            color: conversationInput.text.length > 0 ? "white" : Theme.textSecondaryColor
                        }

                        MouseArea {
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

    // Unsupported beverage type dialog (used by openWithShot)
    Dialog {
        id: unsupportedBeverageDialog
        property string beverageType: ""
        title: TranslationManager.translate("shotdetail.unsupportedbeverage.title", "AI Not Available")
        anchors.centerIn: parent
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        Text {
            text: TranslationManager.translate("shotdetail.unsupportedbeverage.message",
                "AI analysis isn't available for %1 profiles yet \u2014 only espresso and filter are supported for now. Sorry about that!").arg(unsupportedBeverageDialog.beverageType)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            width: parent.width
        }

        standardButtons: Dialog.Ok
    }
}
