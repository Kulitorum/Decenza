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
                selectByMouse: true
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

    ConversationOverlay {
        id: conversationOverlay
        anchors.fill: parent
        pendingShotSummary: shotDetailPage.pendingShotSummary
        shotId: shotDetailPage.shotId
        beverageType: shotData.beverageType || "espresso"
        isMistakeShot: MainController.aiManager ? MainController.aiManager.isMistakeShot(shotData) : false
        overlayTitle: TranslationManager.translate("shotdetail.conversation.title", "AI Conversation")
        onPendingShotSummaryCleared: shotDetailPage.pendingShotSummary = ""
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
                    // Check beverage type is supported
                    var bevType = (shotData.beverageType || "espresso")
                    if (!MainController.aiManager.isSupportedBeverageType(bevType)) {
                        unsupportedBeverageDialog.beverageType = bevType
                        unsupportedBeverageDialog.open()
                        return
                    }

                    // Switch to the right conversation for this bean+profile
                    MainController.aiManager.switchConversation(
                        shotData.beanBrand || "",
                        shotData.beanType || "",
                        shotData.profileName || ""
                    )

                    // Check for mistake shot
                    if (MainController.aiManager.isMistakeShot(shotData)) {
                        shotDetailPage.pendingShotSummary = ""
                    } else {
                        // Generate shot summary from historical data, with recipe dedup and change detection
                        var raw = MainController.aiManager.generateHistoryShotSummary(shotData)
                        shotDetailPage.pendingShotSummary = MainController.aiManager.conversation.processShotForConversation(raw, shotDetailPage.shotId)
                    }

                    // Open conversation overlay
                    conversationOverlay.open()
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

    // Unsupported beverage type dialog
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
