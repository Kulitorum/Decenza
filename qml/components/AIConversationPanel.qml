import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

/**
 * AIConversationPanel - Reusable AI conversation controls
 *
 * Provides two buttons for AI interaction:
 * - "Ask <provider>" / "Follow up" - sends a query
 * - "Clear" - resets conversation history
 *
 * Usage:
 *   AIConversationPanel {
 *       conversation: myAIConversation
 *       systemPrompt: "You are an espresso expert..."
 *       userPrompt: shotDataText
 *       onResponseReceived: (response) => { responseText.text = response }
 *   }
 *
 * Or with follow-up input:
 *   AIConversationPanel {
 *       conversation: myAIConversation
 *       systemPrompt: "You are an espresso expert..."
 *       userPrompt: followUpField.text.length > 0 ? followUpField.text : initialPrompt
 *       showFollowUpInput: true
 *   }
 */
Item {
    id: root

    // Required: The AIConversation instance to use
    property var conversation: null

    // The system prompt (context/role for the AI)
    property string systemPrompt: ""

    // The user prompt to send (can be bound to a TextField for follow-ups)
    property string userPrompt: ""

    // Optional: Show a text input for follow-up questions
    property bool showFollowUpInput: false

    // Optional: Placeholder text for follow-up input
    property string followUpPlaceholder: "Ask a follow-up question..."

    // Optional: Custom button text
    property string askButtonText: ""

    // Signals
    signal responseReceived(string response)
    signal errorOccurred(string error)

    // Internal: computed button text
    readonly property string _buttonText: {
        if (askButtonText) return askButtonText
        if (!conversation) return "Ask AI"
        if (conversation.hasHistory) return "Follow up"
        return "Ask " + conversation.providerName
    }

    readonly property bool _canAsk: {
        if (!conversation) return false
        if (conversation.busy) return false
        if (!userPrompt && !followUpInput.text) return false
        return true
    }

    implicitWidth: mainLayout.implicitWidth
    implicitHeight: mainLayout.implicitHeight

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        spacing: Theme.scaled(8)

        // Follow-up input (optional)
        StyledTextField {
            id: followUpInput
            Layout.fillWidth: true
            visible: root.showFollowUpInput && conversation && conversation.hasHistory
            placeholder: root.followUpPlaceholder
            enabled: conversation && !conversation.busy

            Keys.onReturnPressed: askButton.clicked()
            Keys.onEnterPressed: askButton.clicked()
        }

        // Buttons row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            // Ask / Follow up button
            AccessibleButton {
                id: askButton
                Layout.fillWidth: true
                primary: true
                text: conversation && conversation.busy ? "..." : root._buttonText
                accessibleName: conversation && conversation.hasHistory
                    ? TranslationManager.translate("aiConversation.sendFollowUp", "Send follow-up question to AI")
                    : TranslationManager.translate("aiConversation.askForRecommendation", "Ask AI for recommendation")
                enabled: root._canAsk

                onClicked: {
                    if (!conversation) return

                    var prompt = followUpInput.visible && followUpInput.text
                        ? followUpInput.text
                        : root.userPrompt

                    if (!prompt) return

                    if (conversation.hasHistory) {
                        conversation.followUp(prompt)
                    } else {
                        conversation.ask(root.systemPrompt, prompt)
                    }

                    // Clear follow-up input after sending
                    if (followUpInput.visible) {
                        followUpInput.text = ""
                    }
                }
            }

            // Clear button
            AccessibleButton {
                id: clearButton
                text: "Clear"
                accessibleName: TranslationManager.translate("aiConversation.clearHistory", "Clear AI conversation history")
                visible: conversation && conversation.hasHistory
                enabled: conversation && !conversation.busy

                onClicked: {
                    if (conversation) {
                        conversation.clearHistory()
                    }
                }
            }
        }

        // Error message (if any)
        Text {
            Layout.fillWidth: true
            visible: conversation && conversation.errorMessage
            text: conversation ? conversation.errorMessage : ""
            color: Theme.errorColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.WordWrap
        }
    }

    // Forward signals from conversation
    Connections {
        target: conversation
        function onResponseReceived(response) {
            root.responseReceived(response)
        }
        function onErrorOccurred(error) {
            root.errorOccurred(error)
        }
    }
}
