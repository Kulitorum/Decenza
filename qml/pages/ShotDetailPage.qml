import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotDetailPage
    objectName: "shotDetailPage"
    background: Rectangle { color: Theme.backgroundColor }

    property int shotId: 0
    property var shotData: ({})
    property string pendingShotSummary: ""

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("shotdetail.title", "Shot Detail")
        loadShot()
    }

    function loadShot() {
        if (shotId > 0) {
            shotData = MainController.shotHistory.getShot(shotId)
        }
    }

    function formatRatio() {
        if (shotData.doseWeight > 0) {
            return "1:" + (shotData.finalWeight / shotData.doseWeight).toFixed(1)
        }
        return "-"
    }


    ScrollView {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            // Header
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Text {
                    text: shotData.profileName || "Shot Detail"
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Text {
                    text: shotData.dateTime || ""
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Graph
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(250)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                HistoryShotGraph {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    pressureData: shotData.pressure || []
                    flowData: shotData.flow || []
                    temperatureData: shotData.temperature || []
                    weightData: shotData.weight || []
                    phaseMarkers: shotData.phases || []
                    maxTime: shotData.duration || 60
                }
            }

            // Metrics row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                // Duration
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.duration"
                        fallback: "Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.duration || 0).toFixed(1) + "s"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }
                }

                // Dose
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.dose"
                        fallback: "Dose"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.doseWeight || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeDoseColor
                    }
                }

                // Output
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.output"
                        fallback: "Output"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.finalWeight || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeOutputColor
                    }
                }

                // Ratio
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.ratio"
                        fallback: "Ratio"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: formatRatio()
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }
                }

                // Rating
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.rating"
                        fallback: "Rating"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.enjoyment || 0) > 0 ? shotData.enjoyment + "%" : "-"
                        font: Theme.subtitleFont
                        color: Theme.warningColor
                    }
                }
            }

            // Bean info
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: beanColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: beanColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.beaninfo"
                        fallback: "Bean Info"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Theme.spacingLarge
                        rowSpacing: Theme.spacingSmall
                        Layout.fillWidth: true

                        Tr { key: "shotdetail.brand"; fallback: "Brand:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.beanBrand || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.type"; fallback: "Type:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.beanType || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.roastdate"; fallback: "Roast Date:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.roastDate || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.roastlevel"; fallback: "Roast Level:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.roastLevel || "-"; font: Theme.labelFont; color: Theme.textColor }
                    }
                }
            }

            // Grinder info
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: grinderColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: grinderColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.grinder"
                        fallback: "Grinder"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Theme.spacingLarge
                        rowSpacing: Theme.spacingSmall
                        Layout.fillWidth: true

                        Tr { key: "shotdetail.model"; fallback: "Model:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.grinderModel || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.setting"; fallback: "Setting:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.grinderSetting || "-"; font: Theme.labelFont; color: Theme.textColor }
                    }
                }
            }

            // Analysis
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: analysisColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: shotData.drinkTds > 0 || shotData.drinkEy > 0

                ColumnLayout {
                    id: analysisColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.analysis"
                        fallback: "Analysis"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    RowLayout {
                        spacing: Theme.spacingLarge

                        ColumnLayout {
                            spacing: Theme.scaled(2)
                            Tr { key: "shotdetail.tds"; fallback: "TDS"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                            Text { text: (shotData.drinkTds || 0).toFixed(2) + "%"; font: Theme.bodyFont; color: Theme.dyeTdsColor }
                        }

                        ColumnLayout {
                            spacing: Theme.scaled(2)
                            Tr { key: "shotdetail.ey"; fallback: "EY"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                            Text { text: (shotData.drinkEy || 0).toFixed(1) + "%"; font: Theme.bodyFont; color: Theme.dyeEyColor }
                        }
                    }
                }
            }

            // Barista
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: baristaRow.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: shotData.barista && shotData.barista !== ""

                RowLayout {
                    id: baristaRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.barista"
                        fallback: "Barista:"
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    Text {
                        text: shotData.barista || ""
                        font: Theme.labelFont
                        color: Theme.textColor
                    }
                }
            }

            // Notes
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: notesColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: notesColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.notes"
                        fallback: "Notes"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        text: shotData.espressoNotes || "-"
                        font: Theme.bodyFont
                        color: Theme.textColor
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            // AI Advice button
            Rectangle {
                id: aiAdviceButton
                visible: MainController.aiManager && shotData.duration > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                radius: Theme.cardRadius
                color: MainController.aiManager && MainController.aiManager.isConfigured
                       ? Theme.primaryColor : Theme.surfaceColor
                opacity: MainController.aiManager && MainController.aiManager.isAnalyzing ? 0.6 : 1.0

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(8)

                    Image {
                        source: "qrc:/icons/sparkle.svg"
                        width: Theme.scaled(20)
                        height: Theme.scaled(20)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: status === Image.Ready
                    }

                    Text {
                        text: MainController.aiManager && MainController.aiManager.isAnalyzing
                              ? TranslationManager.translate("shotdetail.analyzing", "Analyzing...")
                              : TranslationManager.translate("shotdetail.aiadvice", "AI Advice")
                        color: MainController.aiManager && MainController.aiManager.isConfigured
                               ? "white" : Theme.textColor
                        font: Theme.bodyFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: MainController.aiManager && MainController.aiManager.isConfigured && !MainController.aiManager.isAnalyzing
                    onClicked: {
                        // Generate shot summary from historical data
                        shotDetailPage.pendingShotSummary = MainController.aiManager.generateHistoryShotSummary(shotData)
                        // Open conversation dialog
                        aiConversationDialog.open()
                    }
                }
            }

            // Email Prompt button - fallback for users without API keys
            Rectangle {
                visible: MainController.aiManager && !MainController.aiManager.isConfigured && shotData.duration > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                radius: Theme.cardRadius
                color: Theme.surfaceColor

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(8)

                    Image {
                        source: "qrc:/icons/sparkle.svg"
                        width: Theme.scaled(20)
                        height: Theme.scaled(20)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: status === Image.Ready
                        opacity: 0.6
                    }

                    Text {
                        text: TranslationManager.translate("shotdetail.emailprompt", "Email Prompt")
                        color: Theme.textColor
                        font: Theme.bodyFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        var prompt = MainController.aiManager.generateHistoryShotSummary(shotData)
                        var subject = "Espresso AI Analysis - " + (shotData.profileName || "Shot")
                        Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent(subject) + "&body=" + encodeURIComponent(prompt))
                    }
                }
            }

            // Actions
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                StyledButton {
                    text: TranslationManager.translate("shotdetail.viewdebuglog", "View Debug Log")
                    Layout.fillWidth: true
                    onClicked: debugLogDialog.open()

                    background: Rectangle {
                        color: "transparent"
                        radius: Theme.buttonRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.labelFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                StyledButton {
                    text: TranslationManager.translate("shotdetail.deleteshot", "Delete Shot")
                    Layout.fillWidth: true
                    onClicked: deleteConfirmDialog.open()

                    background: Rectangle {
                        color: "transparent"
                        radius: Theme.buttonRadius
                        border.color: Theme.errorColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.labelFont
                        color: Theme.errorColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Visualizer status
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: shotData.visualizerId

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium

                    Tr {
                        key: "shotdetail.uploadedtovisualizer"
                        fallback: "\u2601 Uploaded to Visualizer"
                        font: Theme.labelFont
                        color: Theme.successColor
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: shotData.visualizerId || ""
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                }
            }

            // Bottom spacer
            Item { Layout.preferredHeight: Theme.spacingLarge }
        }
    }

    // Debug log dialog
    Dialog {
        id: debugLogDialog
        title: TranslationManager.translate("shotdetail.debuglog", "Debug Log")
        anchors.centerIn: parent
        width: parent.width * 0.9
        height: parent.height * 0.8
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth

            TextArea {
                text: shotData.debugLog || TranslationManager.translate("shotdetail.nodebuglog", "No debug log available")
                font.family: "monospace"
                font.pixelSize: Theme.scaled(12)
                color: Theme.textColor
                readOnly: true
                wrapMode: Text.Wrap
                background: Rectangle { color: "transparent" }
            }
        }

        standardButtons: Dialog.Close
    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirmDialog
        title: TranslationManager.translate("shotdetail.deleteconfirmtitle", "Delete Shot?")
        anchors.centerIn: parent
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        Tr {
            key: "shotdetail.deleteconfirmmessage"
            fallback: "This will permanently delete this shot from history."
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
        }

        standardButtons: Dialog.Cancel | Dialog.Ok

        onAccepted: {
            MainController.shotHistory.deleteShot(shotId)
            pageStack.pop()
        }
    }

    // AI Conversation Dialog
    Dialog {
        id: aiConversationDialog
        title: TranslationManager.translate("shotdetail.aiconversation", "AI Advice")
        anchors.centerIn: parent
        width: parent.width * 0.9
        height: parent.height * 0.85
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        function sendFollowUp() {
            if (!MainController.aiManager || !MainController.aiManager.conversation) return
            if (followUpInput.text.length === 0) return

            var conversation = MainController.aiManager.conversation

            // If no history, send initial analysis with shot data
            if (!conversation.hasHistory) {
                var systemPrompt = "You are an espresso analyst helping dial in shots on a Decent DE1 profiling machine. " +
                    "Follow James Hoffmann's methodology for dialing in espresso."
                var userPrompt = shotDetailPage.pendingShotSummary + "\n\nUser question: " + followUpInput.text
                conversation.send(systemPrompt, userPrompt)
            } else {
                conversation.followUp(followUpInput.text)
            }
            followUpInput.text = ""
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            // Conversation history
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth

                Text {
                    id: conversationText
                    width: parent.width
                    text: MainController.aiManager && MainController.aiManager.conversation
                          ? MainController.aiManager.conversation.getConversationText()
                          : ""
                    font: Theme.bodyFont
                    color: Theme.textColor
                    wrapMode: Text.Wrap
                    textFormat: Text.MarkdownText
                }
            }

            // Loading indicator
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: MainController.aiManager && MainController.aiManager.conversation &&
                         MainController.aiManager.conversation.busy
                visible: running
            }

            // Follow-up input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                StyledTextField {
                    id: followUpInput
                    Layout.fillWidth: true
                    placeholderText: TranslationManager.translate("shotdetail.followup.placeholder", "Ask a follow-up question...")
                    enabled: MainController.aiManager && MainController.aiManager.conversation &&
                             !MainController.aiManager.conversation.busy
                    onAccepted: sendFollowUp()
                }

                StyledButton {
                    text: TranslationManager.translate("shotdetail.send", "Send")
                    enabled: followUpInput.text.length > 0 &&
                             MainController.aiManager && MainController.aiManager.conversation &&
                             !MainController.aiManager.conversation.busy
                    onClicked: sendFollowUp()
                }
            }
        }

        // Clear conversation when dialog closes
        onClosed: {
            if (MainController.aiManager && MainController.aiManager.conversation) {
                MainController.aiManager.conversation.saveToStorage()
            }
        }

        // Update conversation text when response received
        Connections {
            target: MainController.aiManager ? MainController.aiManager.conversation : null
            function onHistoryChanged() {
                conversationText.text = MainController.aiManager.conversation.getConversationText()
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("shotdetail.title", "Shot Detail")
        rightText: shotData.profileName || ""
        onBackClicked: root.goBack()
    }
}
