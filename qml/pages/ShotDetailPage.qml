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
    // Shot navigation - list of shot IDs to swipe through
    property var shotIds: []  // Array of shot IDs (chronological order)
    property int currentIndex: -1  // Current position in shotIds
    // Persisted graph height (like PostShotReviewPage)
    property real graphHeight: Settings.value("shotDetail/graphHeight", Theme.scaled(250))

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("shotdetail.title", "Shot Detail")
        // Initialize currentIndex if shotIds provided
        if (shotIds.length > 0 && currentIndex < 0) {
            currentIndex = shotIds.indexOf(shotId)
            if (currentIndex < 0) currentIndex = 0
        }
        loadShot()
        graphCard.forceActiveFocus()
    }

    function loadShot() {
        if (shotId > 0) {
            MainController.shotHistory.requestShot(shotId)
        }
    }

    // Handle async shot data
    Connections {
        target: MainController.shotHistory
        function onShotReady(id, shot) {
            if (id !== shotDetailPage.shotId) return
            shotData = shot
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

    function graphAccessibleDescription() {
        var parts = []
        if (shotData.profileName)
            parts.push(shotData.profileName)
        parts.push((shotData.duration || 0).toFixed(0) + "s")
        parts.push((shotData.doseWeight || 0).toFixed(1) + "g in")
        parts.push((shotData.finalWeight || 0).toFixed(1) + "g out")
        if (shotData.doseWeight > 0)
            parts.push("ratio " + formatRatio())
        return TranslationManager.translate("shotdetail.accessible.graph", "Shot graph") + ": " + parts.join(", ")
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

            GraphInspectBar { graph: shotGraph }

            // Resizable graph with swipe navigation
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), shotDetailPage.graphHeight))
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                clip: true

                Accessible.role: Accessible.Graphic
                Accessible.name: graphAccessibleDescription()
                Accessible.focusable: true
                focus: true

                // Visual offset during swipe
                transform: Translate { x: graphSwipeArea.swipeOffset * 0.3 }

                HistoryShotGraph {
                    id: shotGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    pressureData: shotData.pressure || []
                    flowData: shotData.flow || []
                    temperatureData: shotData.temperature || []
                    weightData: shotData.weight || []
                    weightFlowRateData: shotData.weightFlowRate || []
                    resistanceData: shotData.resistance || []
                    phaseMarkers: shotData.phases || []
                    maxTime: shotData.duration || 60
                    Accessible.ignored: true
                }

                // Swipe handler overlay
                SwipeableArea {
                    id: graphSwipeArea
                    anchors.fill: parent
                    anchors.bottomMargin: resizeHandle.height
                    canSwipeLeft: canGoNext()
                    canSwipeRight: canGoPrevious()

                    onSwipedLeft: { shotGraph.dismissInspect(); navigateToShot(currentIndex + 1) }
                    onSwipedRight: { shotGraph.dismissInspect(); navigateToShot(currentIndex - 1) }
                    onTapped: function(x, y) {
                        var graphPos = mapToItem(shotGraph, x, y)
                        if (graphPos.x > shotGraph.plotArea.x + shotGraph.plotArea.width) {
                            shotGraph.toggleRightAxis()
                        } else {
                            shotGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                    }
                    onMoved: function(x, y) {
                        var graphPos = mapToItem(shotGraph, x, y)
                        shotGraph.inspectAtPosition(graphPos.x, graphPos.y)
                    }
                }

                // Resize handle at bottom
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.ignored: true

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Repeater {
                            model: 3
                            Rectangle {
                                width: Theme.scaled(30)
                                height: 1
                                color: Theme.textSecondaryColor
                                opacity: resizeMouseArea.containsMouse || resizeMouseArea.pressed ? 0.8 : 0.4
                            }
                        }
                    }

                    MouseArea {
                        id: resizeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeVerCursor
                        preventStealing: true

                        property real startY: 0
                        property real startHeight: 0

                        onPressed: function(mouse) {
                            startY = mouse.y + resizeHandle.mapToItem(shotDetailPage, 0, 0).y
                            startHeight = graphCard.Layout.preferredHeight
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(shotDetailPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                newHeight = Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), newHeight))
                                shotDetailPage.graphHeight = newHeight
                            }
                        }

                        onReleased: {
                            Settings.setValue("shotDetail/graphHeight", shotDetailPage.graphHeight)
                        }
                    }
                }
            }

            GraphLegend { graph: shotGraph }

            // Shot navigation buttons (list is newest-first, so lower index = newer)
            RowLayout {
                visible: shotIds.length > 1
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                AccessibleButton {
                    id: newerShotButton
                    text: TranslationManager.translate("shotdetail.newershot", "Newer Shot")
                    accessibleName: TranslationManager.translate("shotdetail.accessible.newershot",
                        "Newer shot") + ", " + TranslationManager.translate("shotdetail.accessible.position",
                        "Shot %1 of %2").arg(currentIndex + 1).arg(shotIds.length)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 10  // Equal base for both buttons
                    enabled: canGoPrevious()
                    onClicked: navigateToShot(currentIndex - 1)
                }

                Text {
                    text: (currentIndex + 1) + " / " + shotIds.length
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.name: TranslationManager.translate("shotdetail.accessible.position",
                        "Shot %1 of %2").arg(currentIndex + 1).arg(shotIds.length)
                }

                AccessibleButton {
                    id: olderShotButton
                    text: TranslationManager.translate("shotdetail.oldershot", "Older Shot")
                    accessibleName: TranslationManager.translate("shotdetail.accessible.oldershot",
                        "Older shot") + ", " + TranslationManager.translate("shotdetail.accessible.position",
                        "Shot %1 of %2").arg(currentIndex + 1).arg(shotIds.length)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 10  // Equal base for both buttons
                    enabled: canGoNext()
                    onClicked: navigateToShot(currentIndex + 1)
                }
            }

            // Metrics row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                // Duration
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("shotdetail.duration", "Duration") + ": " +
                        (shotData.duration || 0).toFixed(1) + "s"
                    Tr {
                        key: "shotdetail.duration"
                        fallback: "Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (shotData.duration || 0).toFixed(1) + "s"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                }

                // Dose
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("shotdetail.dose", "Dose") + ": " +
                        (shotData.doseWeight || 0).toFixed(1) + "g"
                    Tr {
                        key: "shotdetail.dose"
                        fallback: "Dose"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (shotData.doseWeight || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeDoseColor
                        Accessible.ignored: true
                    }
                }

                // Output (with optional target)
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: {
                        var label = TranslationManager.translate("shotdetail.output", "Output") + ": " +
                            (shotData.finalWeight || 0).toFixed(1) + "g"
                        var y = shotData.yieldOverride
                        if (y !== undefined && y !== null && y > 0 && Math.abs(y - shotData.finalWeight) > 0.5)
                            label += " (" + Math.round(y) + "g target)"
                        return label
                    }
                    Tr {
                        key: "shotdetail.output"
                        fallback: "Output"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Row {
                        spacing: Theme.scaled(4)
                        Accessible.ignored: true
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
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("shotdetail.ratio", "Ratio") + ": " + formatRatio()
                    Tr {
                        key: "shotdetail.ratio"
                        fallback: "Ratio"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: formatRatio()
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                }

                // Rating
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("shotdetail.rating", "Rating") + ": " +
                        ((shotData.enjoyment || 0) > 0 ? shotData.enjoyment + "%" : "-")
                    Tr {
                        key: "shotdetail.rating"
                        fallback: "Rating"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (shotData.enjoyment || 0) > 0 ? shotData.enjoyment + "%" : "-"
                        font: Theme.subtitleFont
                        color: Theme.warningColor
                        Accessible.ignored: true
                    }
                }
            }

            // Notes (shown first, above bean/grinder cards)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: !!(shotData.espressoNotes && shotData.espressoNotes !== "")

                Tr {
                    key: "shotdetail.notes"
                    fallback: "Notes"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                ExpandableTextArea {
                    Layout.fillWidth: true
                    inlineHeight: Theme.scaled(80)
                    text: shotData.espressoNotes || ""
                    accessibleName: TranslationManager.translate("shotdetail.notes", "Notes")
                    textFont: Theme.bodyFont
                    readOnly: true
                }
            }

            // Bean info (hidden when all rows are empty)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: beanColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: !!(shotData.beanBrand || shotData.beanType || shotData.roastDate || shotData.roastLevel)
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("shotdetail.beaninfo", "Beans")

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

                        Tr { key: "shotdetail.roaster"; fallback: "Roaster:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.beanBrand) }
                        Text { text: shotData.beanBrand || ""; font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.beanBrand) }

                        Tr { key: "shotdetail.coffee"; fallback: "Coffee:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.beanType) }
                        Text { text: shotData.beanType || ""; font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.beanType) }

                        Tr { key: "shotdetail.roastdate"; fallback: "Roast Date:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.roastDate) }
                        Text { text: shotData.roastDate || ""; font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.roastDate) }

                        Tr { key: "shotdetail.roastlevel"; fallback: "Roast Level:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.roastLevel) }
                        Text { text: shotData.roastLevel || ""; font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.roastLevel) }
                    }
                }
            }

            // Grinder info (hidden when all rows are empty)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: grinderColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: !!(shotData.grinderModel || shotData.grinderSetting)
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("shotdetail.grinder", "Grinder")

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

                        Tr { key: "shotdetail.model"; fallback: "Model:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.grinderModel) }
                        Text { text: shotData.grinderModel || ""; font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.grinderModel) }

                        Tr { key: "shotdetail.setting"; fallback: "Setting:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.grinderSetting) }
                        Text { text: shotData.grinderSetting || ""; font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.grinderSetting) }
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
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("shotdetail.analysis", "Analysis")

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
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("shotdetail.barista", "Barista:") + " " + (shotData.barista || "")

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
                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("shotdetail.uploadedtovisualizer",
                    "Uploaded to Visualizer") + ": " + (shotData.visualizerId || "")

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium

                    Row {
                        spacing: Theme.scaled(4)
                        Image {
                            source: "qrc:/emoji/2601.svg"
                            sourceSize.width: Theme.labelFont.pixelSize
                            sourceSize.height: Theme.labelFont.pixelSize
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Tr {
                            key: "shotdetail.uploadedtovisualizer"
                            fallback: "Uploaded to Visualizer"
                            font: Theme.labelFont
                            color: Theme.successColor
                        }
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

                Accessible.role: Accessible.EditableText
                Accessible.name: TranslationManager.translate("shotdetail.debuglog", "Debug Log")
                Accessible.description: text.substring(0, 200)
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
            MainController.shotHistory.requestDeleteShot(shotId)
            pageStack.pop()
        }
    }

    ConversationOverlay {
        id: conversationOverlay
        anchors.fill: parent
        overlayTitle: TranslationManager.translate("shotdetail.conversation.title", "AI Conversation")
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
            Accessible.focusable: true
            Accessible.onPressAction: uploadButtonArea.clicked(null)

            Row {
                id: uploadButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/emoji/2601.svg"  // Cloud icon
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
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
                    Accessible.ignored: true
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
            Accessible.focusable: true
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
                    Accessible.ignored: true
                }

                Tr {
                    key: "shotdetail.aiadvice"
                    fallback: "AI Advice"
                    color: "white"
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: aiButtonArea
                anchors.fill: parent
                onClicked: {
                    conversationOverlay.openWithShot(shotData, shotData.beanBrand, shotData.beanType, shotData.profileName, shotDetailPage.shotId)
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
            Accessible.focusable: true
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
                    Accessible.ignored: true
                }

                Tr {
                    key: "shotdetail.email"
                    fallback: "Email"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
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
