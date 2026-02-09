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

    // Shot navigation - list of shot IDs to swipe through
    property var shotIds: []  // Array of shot IDs (chronological order)
    property int currentIndex: -1  // Current position in shotIds

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("shotdetail.title", "Shot Detail")
        // Initialize currentIndex if shotIds provided
        if (shotIds.length > 0 && currentIndex < 0) {
            currentIndex = shotIds.indexOf(shotId)
            if (currentIndex < 0) currentIndex = 0
        }
        loadShot()
    }

    function loadShot() {
        if (shotId > 0) {
            shotData = MainController.shotHistory.getShot(shotId)
        }
    }

    function navigateToShot(index) {
        if (index >= 0 && index < shotIds.length) {
            currentIndex = index
            shotId = shotIds[index]
            loadShot()
        }
    }

    function canGoNext() {
        return shotIds.length > 0 && currentIndex < shotIds.length - 1
    }

    function canGoPrevious() {
        return shotIds.length > 0 && currentIndex > 0
    }

    function formatRatio() {
        if (shotData.doseWeight > 0) {
            return "1:" + (shotData.finalWeight / shotData.doseWeight).toFixed(1)
        }
        return "-"
    }


    // Handle visualizer upload/update status changes
    Connections {
        target: MainController.visualizer
        function onUploadSuccess(shotId, url) {
            if (shotDetailPage.shotId > 0) {
                MainController.shotHistory.updateVisualizerInfo(shotDetailPage.shotId, shotId, url)
                loadShot()
            }
        }
        function onUpdateSuccess(visualizerId) {
            if (shotDetailPage.shotId > 0) {
                loadShot()
            }
        }
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

            // Header: Profile (Temp) · Bean (Grind)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Text {
                    text: {
                        var name = shotData.profileName || "Shot Detail"
                        var t = shotData.temperatureOverride
                        if (t !== undefined && t !== null && t > 0) {
                            return name + " (" + Math.round(t) + "\u00B0C)"
                        }
                        return name
                    }
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Text {
                    text: shotData.dateTime || ""
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Graph with swipe navigation
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(250)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                clip: true

                // Visual offset during swipe
                transform: Translate { x: graphSwipeArea.swipeOffset * 0.3 }

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

                // Swipe handler overlay
                SwipeableArea {
                    id: graphSwipeArea
                    anchors.fill: parent
                    canSwipeLeft: canGoNext()
                    canSwipeRight: canGoPrevious()

                    onSwipedLeft: navigateToShot(currentIndex + 1)
                    onSwipedRight: navigateToShot(currentIndex - 1)
                }

                // Position indicator (only show if navigating through multiple shots)
                Rectangle {
                    visible: shotIds.length > 1
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: Theme.spacingSmall
                    width: positionText.width + Theme.scaled(16)
                    height: Theme.scaled(24)
                    radius: Theme.scaled(12)
                    color: Qt.rgba(0, 0, 0, 0.5)

                    Text {
                        id: positionText
                        anchors.centerIn: parent
                        text: (currentIndex + 1) + " / " + shotIds.length
                        font: Theme.captionFont
                        color: "white"
                    }
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

                // Output (with optional target)
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.output"
                        fallback: "Output"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Row {
                        spacing: Theme.scaled(4)
                        Text {
                            text: (shotData.finalWeight || 0).toFixed(1) + "g"
                            font: Theme.subtitleFont
                            color: Theme.dyeOutputColor
                        }
                        Text {
                            visible: {
                                var y = shotData.yieldOverride
                                return y !== undefined && y !== null && y > 0
                                    && Math.abs(y - shotData.finalWeight) > 0.5
                            }
                            text: {
                                var y = shotData.yieldOverride
                                return (y !== undefined && y !== null && y > 0) ? "(" + Math.round(y) + "g)" : ""
                            }
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            anchors.baseline: parent.children[0].baseline
                        }
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

                    Text {
                        text: {
                            var title = TranslationManager.translate("shotdetail.beaninfo", "Beans")
                            var grind = shotData.grinderSetting || ""
                            return grind ? title + " (" + grind + ")" : title
                        }
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

            // Actions
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.viewdebuglog", "View Debug Log")
                    accessibleName: TranslationManager.translate("shotDetail.viewDebugLog", "View debug log for this shot")
                    Layout.fillWidth: true
                    onClicked: debugLogDialog.open()
                }

                AccessibleButton {
                    id: deleteButton
                    text: TranslationManager.translate("shotdetail.deleteshot", "Delete Shot")
                    accessibleName: TranslationManager.translate("shotDetail.deleteShotPermanently", "Permanently delete this shot from history")
                    Layout.fillWidth: true
                    onClicked: deleteConfirmDialog.open()

                    background: Rectangle {
                        color: "transparent"
                        radius: Theme.buttonRadius
                        border.color: Theme.errorColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: deleteButton.text
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

    // Conversation overlay panel - full screen with keyboard awareness
    Rectangle {
        id: conversationOverlay
        visible: false
        anchors.fill: parent
        color: Theme.backgroundColor
        z: 200

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

                    // Clear button row at top
                    RowLayout {
                        Layout.fillWidth: true

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
                                text: TranslationManager.translate("shotdetail.conversation.clear", "Clear")
                                font.pixelSize: Theme.scaled(12)
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (MainController.aiManager && MainController.aiManager.conversation) {
                                        MainController.aiManager.conversation.clearHistory()
                                        MainController.aiManager.conversation.saveToStorage()
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
                            font: Theme.bodyFont
                            color: Theme.textColor
                            background: null
                            padding: 0
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
                            text: TranslationManager.translate("shotdetail.conversation.thinking", "Thinking...")
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                        }
                    }

                    // Shot data attached indicator
                    Rectangle {
                        visible: shotDetailPage.pendingShotSummary.length > 0
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
                                text: "\uD83D\uDCCA"  // chart emoji
                                font.pixelSize: Theme.scaled(12)
                            }

                            Text {
                                text: TranslationManager.translate("shotdetail.conversation.shotattached", "Shot data will be included with your message")
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
                                    onClicked: shotDetailPage.pendingShotSummary = ""
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
                            placeholder: shotDetailPage.pendingShotSummary.length > 0
                                         ? TranslationManager.translate("shotdetail.conversation.placeholder.withshot", "Ask about this shot...")
                                         : TranslationManager.translate("shotdetail.conversation.placeholder", "Ask a follow-up question...")
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
                                if (shotDetailPage.pendingShotSummary.length > 0) {
                                    message = "Here's my latest shot:\n\n" + shotDetailPage.pendingShotSummary +
                                              "\n\n" + text
                                    shotDetailPage.pendingShotSummary = ""  // Clear after sending
                                }

                                // Use ask() for new conversation, followUp() for existing
                                if (!conversation.hasHistory) {
                                    var bevType = (shotData.beverageType || "espresso").toLowerCase()
                                    var isFilter = bevType === "filter" || bevType === "pourover"
                                    var systemPrompt = isFilter
                                        ? "You are an expert filter coffee consultant helping a user optimise brews made on a Decent DE1 profiling machine over multiple attempts. " +
                                          "Key principles: Taste is king — numbers serve taste, not the other way around. " +
                                          "Profile intent is the reference frame — evaluate actual vs. what the profile intended, not pour-over or drip norms. " +
                                          "DE1 filter uses low pressure (1-3 bar), high flow, and long ratios (1:10-1:17) — these are all intentional, not problems. " +
                                          "One variable at a time — never recommend changing multiple things at once. " +
                                          "Track progress across brews and reference previous brews to identify trends. " +
                                          "If grinder info is shared, consider burr geometry (flat vs conical) in your analysis. " +
                                          "Focus on clarity, sweetness, and balance rather than espresso-style body and intensity."
                                        : "You are an expert espresso consultant helping a user dial in their shots on a Decent DE1 profiling machine over multiple attempts. " +
                                          "Key principles: Taste is king — numbers serve taste, not the other way around. " +
                                          "Profile intent is the reference frame — evaluate actual vs. what the profile intended, not generic espresso norms. " +
                                          "A Blooming Espresso at 2 bar or a turbo shot at 15 seconds are not problems — they're by design. " +
                                          "The DE1 controls either pressure or flow (never both); when one is the target, the other is a result of puck resistance. " +
                                          "One variable at a time — never recommend changing multiple things at once. " +
                                          "Track progress across shots and reference previous shots to identify trends. " +
                                          "If grinder info is shared, consider burr geometry (flat vs conical) in your analysis. " +
                                          "Never default to generic rules like 'grind finer', 'aim for 9 bar', or '25-30 seconds' without evidence from the data."
                                    conversation.ask(systemPrompt, message)
                                } else {
                                    conversation.followUp(message)
                                }
                                text = ""
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
                                text: TranslationManager.translate("shotdetail.conversation.send", "Send")
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
            title: TranslationManager.translate("shotdetail.conversation.title", "AI Conversation")
            showBackButton: true
            onBackClicked: {
                // Save conversation before closing
                if (MainController.aiManager && MainController.aiManager.conversation) {
                    MainController.aiManager.conversation.saveToStorage()
                }
                conversationOverlay.visible = false
            }
        }

        // Update conversation text when response received
        Connections {
            target: MainController.aiManager ? MainController.aiManager.conversation : null
            function onResponseReceived(response) {
                // Scroll to bottom when new response arrives
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                })
            }
            function onHistoryChanged() {
                // Refresh conversation text
                conversationText.text = MainController.aiManager.conversation.getConversationText()
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("shotdetail.title", "Shot Detail")
        onBackClicked: root.goBack()

        // Upload / Re-Upload to Visualizer button
        Rectangle {
            id: uploadButton
            visible: shotData.duration > 0 && !MainController.visualizer.uploading
            Layout.preferredWidth: uploadButtonContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: uploadButtonArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: shotData.visualizerId
                ? TranslationManager.translate("shotdetail.button.reupload", "Re-Upload to Visualizer")
                : TranslationManager.translate("shotdetail.button.upload", "Upload to Visualizer")
            Accessible.onPressAction: uploadButtonArea.clicked(null)

            Row {
                id: uploadButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Text {
                    text: "\u2601"  // Cloud icon
                    font.pixelSize: Theme.scaled(16)
                    color: "white"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Tr {
                    key: shotData.visualizerId
                         ? "shotdetail.button.reupload"
                         : "shotdetail.button.upload"
                    fallback: shotData.visualizerId
                              ? "Re-Upload"
                              : "Upload"
                    color: "white"
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: uploadButtonArea
                anchors.fill: parent
                onClicked: {
                    if (shotData.visualizerId) {
                        MainController.visualizer.updateShotOnVisualizer(
                            shotData.visualizerId, shotData)
                    } else {
                        MainController.visualizer.uploadShotFromHistory(shotData)
                    }
                }
            }
        }

        // Uploading/Updating indicator
        Tr {
            visible: MainController.visualizer.uploading
            key: shotData.visualizerId
                 ? "shotdetail.status.updating"
                 : "shotdetail.status.uploading"
            fallback: shotData.visualizerId ? "Updating..." : "Uploading..."
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }

        // AI Advice button
        Rectangle {
            id: aiButton
            visible: MainController.aiManager && MainController.aiManager.isConfigured && shotData.duration > 0
            Layout.preferredWidth: aiButtonContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: aiButtonArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("shotdetail.aiadvice", "AI Advice")
            Accessible.onPressAction: aiButtonArea.clicked(null)

            Row {
                id: aiButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                }

                Tr {
                    key: "shotdetail.aiadvice"
                    fallback: "AI Advice"
                    color: "white"
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: aiButtonArea
                anchors.fill: parent
                onClicked: {
                    // Generate shot summary from historical data
                    shotDetailPage.pendingShotSummary = MainController.aiManager.generateHistoryShotSummary(shotData)
                    // Open conversation overlay
                    conversationOverlay.visible = true
                }
            }
        }

        // Email Prompt button - fallback for users without API keys
        Rectangle {
            id: emailButton
            visible: MainController.aiManager && !MainController.aiManager.isConfigured && shotData.duration > 0
            Layout.preferredWidth: emailButtonContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: emailButtonArea.pressed ? Qt.darker(Theme.surfaceColor, 1.1) : Theme.surfaceColor
            border.color: Theme.borderColor
            border.width: 1

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("shotdetail.emailprompt", "Email Prompt")
            Accessible.onPressAction: emailButtonArea.clicked(null)

            Row {
                id: emailButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                    opacity: 0.6
                }

                Tr {
                    key: "shotdetail.email"
                    fallback: "Email"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: emailButtonArea
                anchors.fill: parent
                onClicked: {
                    var prompt = MainController.aiManager.generateHistoryShotSummary(shotData)
                    var subject = "Espresso AI Analysis - " + (shotData.profileName || "Shot")
                    Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent(subject) + "&body=" + encodeURIComponent(prompt))
                }
            }
        }
    }
}
