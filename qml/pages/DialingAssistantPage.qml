import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: dialingPage
    objectName: "dialingAssistantPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("dialingassistant.title", "AI Recommendation")
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("dialingassistant.title", "AI Recommendation")

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        textFields: [followUpInput]

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        spacing: Theme.spacingMedium

        // Loading state
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: MainController.aiManager && MainController.aiManager.isAnalyzing

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingMedium

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: true
                    palette.dark: Theme.primaryColor
                }

                Tr {
                    key: "dialingassistant.loading.analyzing"
                    fallback: "Analyzing your shot..."
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    Layout.alignment: Qt.AlignHCenter
                }

                Tr {
                    key: "dialingassistant.loading.wait"
                    fallback: "This may take a few seconds"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // Error state
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: MainController.aiManager && !MainController.aiManager.isAnalyzing &&
                     MainController.aiManager.lastError.length > 0 &&
                     MainController.aiManager.lastRecommendation.length === 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingMedium
                width: parent.width * 0.8

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: Theme.scaled(64)
                    height: Theme.scaled(64)
                    radius: Theme.scaled(32)
                    color: Theme.errorColor
                    opacity: 0.2

                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        color: Theme.errorColor
                        font.pixelSize: Theme.scaled(32)
                        font.bold: true
                    }
                }

                Tr {
                    key: "dialingassistant.error.title"
                    fallback: "Analysis Failed"
                    color: Theme.textColor
                    font: Theme.subtitleFont
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: MainController.aiManager ? MainController.aiManager.lastError : ""
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }

                AccessibleButton {
                    text: TranslationManager.translate("dialingassistant.button.goback", "Go Back")
                    accessibleName: TranslationManager.translate("dialingAssistant.returnToPreviousPage", "Return to previous page")
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: pageStack.pop()
                }
            }
        }

        // Success state - recommendation content (scrollable, selectable text)
        Flickable {
            id: recommendationFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: recommendationText.contentHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            visible: MainController.aiManager && !MainController.aiManager.isAnalyzing &&
                     MainController.aiManager.lastRecommendation.length > 0

            // Reset scroll position when visible
            onVisibleChanged: if (visible) contentY = 0

            TextArea {
                id: recommendationText
                width: parent.width
                text: {
                    if (!MainController.aiManager) return ""

                    var conversation = MainController.aiManager.conversation
                    var hasConversation = conversation && conversation.messageCount > 2

                    // If we have a multi-turn conversation, show full history
                    if (hasConversation) {
                        return conversation.getConversationText()
                    }

                    // Otherwise show just the last recommendation with attribution
                    var recommendation = MainController.aiManager.lastRecommendation
                    if (recommendation.length === 0) return ""

                    // Format provider name nicely
                    var provider = MainController.aiManager.selectedProvider
                    var providerName = {
                        "openai": "OpenAI GPT-4o",
                        "anthropic": "Anthropic Claude",
                        "gemini": "Google Gemini",
                        "ollama": "Ollama"
                    }[provider] || provider

                    return recommendation + "\n\n---\n*" + TranslationManager.translate("dialingassistant.attribution", "Advice by") + " " + providerName + "*"
                }
                textFormat: Text.MarkdownText
                wrapMode: TextEdit.WordWrap
                readOnly: true
                selectByMouse: true
                font: Theme.bodyFont
                color: Theme.textColor
                background: null
                padding: 0
            }
        }

        // Follow-up conversation section (visible when we have a recommendation)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: MainController.aiManager && !MainController.aiManager.isAnalyzing &&
                     MainController.aiManager.lastRecommendation.length > 0

            // Follow-up input row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                StyledTextField {
                    id: followUpInput
                    Layout.fillWidth: true
                    placeholder: TranslationManager.translate("dialingassistant.followup.placeholder", "Ask a follow-up question...")
                    enabled: MainController.aiManager && MainController.aiManager.conversation &&
                             !MainController.aiManager.conversation.busy

                    Keys.onReturnPressed: followUpButton.clicked()
                    Keys.onEnterPressed: followUpButton.clicked()
                }

                AccessibleButton {
                    id: followUpButton
                    primary: enabled
                    text: MainController.aiManager && MainController.aiManager.conversation &&
                          MainController.aiManager.conversation.busy ? "..." : TranslationManager.translate("dialingassistant.followup.button", "Ask")
                    accessibleName: TranslationManager.translate("dialingAssistant.sendFollowUpQuestion", "Send follow-up question about dialing in")
                    enabled: followUpInput.text.length > 0 &&
                             MainController.aiManager && MainController.aiManager.conversation &&
                             !MainController.aiManager.conversation.busy
                    onClicked: {
                        if (!MainController.aiManager || !MainController.aiManager.conversation) return
                        if (followUpInput.text.length === 0) return

                        MainController.aiManager.conversation.followUp(followUpInput.text)
                        followUpInput.text = ""
                    }
                }
            }
        }

        // Action buttons (visible when we have a recommendation)
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            visible: MainController.aiManager && !MainController.aiManager.isAnalyzing &&
                     MainController.aiManager.lastRecommendation.length > 0

            AccessibleButton {
                text: "Copy"
                accessibleName: "Copy recommendation to clipboard"
                Layout.fillWidth: true
                onClicked: {
                    // Copy edited text to clipboard
                    recommendationText.selectAll()
                    recommendationText.copy()
                    recommendationText.deselect()
                    copyFeedback.visible = true
                    copyTimer.start()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(48)
                    radius: Theme.scaled(6)
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Tr {
                    key: "dialingassistant.button.copy"
                    fallback: "Copy"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                text: "Done"
                accessibleName: "Close recommendation"
                Layout.fillWidth: true
                onClicked: pageStack.pop()
                background: Rectangle {
                    implicitHeight: Theme.scaled(48)
                    radius: Theme.scaled(6)
                    color: Theme.primaryColor
                }
                contentItem: Tr {
                    key: "dialingassistant.button.done"
                    fallback: "Done"
                    color: "white"
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
    } // KeyboardAwareContainer

    // Copy feedback toast
    Rectangle {
        id: copyFeedback
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(100)
        width: copyText.width + 32
        height: Theme.scaled(40)
        radius: Theme.scaled(20)
        color: Theme.successColor
        visible: false
        opacity: visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }

        Tr {
            id: copyText
            anchors.centerIn: parent
            key: "dialingassistant.toast.copied"
            fallback: "Copied to clipboard"
            color: "white"
            font: Theme.bodyFont
        }

        Timer {
            id: copyTimer
            interval: 2000
            onTriggered: copyFeedback.visible = false
        }
    }

    // Navigate to this page when analysis completes
    Connections {
        target: MainController.aiManager
        function onRecommendationReceived(recommendation) {
            // Reset scroll to top when new recommendation arrives
            recommendationFlickable.contentY = 0
        }
        function onErrorOccurred(error) {
            // Stay on this page to show the error
        }
    }

    // Handle conversation updates (follow-ups)
    Connections {
        target: MainController.aiManager ? MainController.aiManager.conversation : null
        function onResponseReceived(response) {
            // Scroll to bottom when follow-up response arrives
            Qt.callLater(function() {
                recommendationFlickable.contentY = Math.max(0, recommendationFlickable.contentHeight - recommendationFlickable.height)
            })
        }
    }

}
