import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
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
    property bool advancedMode: Settings.boolValue("shotReview/advancedMode", false)
    property int swipeDirection: 0  // 1 = going older, -1 = going newer; swipeDirection: 1 exits left, enters from right; -1 exits right, enters from left
    property bool navigating: false  // true only during a navigateToShot transition; guards enterAnimation from firing on non-navigation loads
    property bool reloadingFromVisualizer: false  // true when loadShot() was called from onVisualizerInfoUpdated; suppresses duplicate badge reanalysis

    // Pick up toggle changes made on any other page sharing this setting
    // (Post-Shot Review, Shot Comparison, Espresso view selector).
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "shotReview/advancedMode")
                shotDetailPage.advancedMode = Settings.boolValue("shotReview/advancedMode", false)
        }
    }

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
            var wasNavigating = shotDetailPage.navigating
            shotDetailPage.navigating = false
            // Defer both calls until after layout has updated: returnToBounds() needs
            // final content bounds, and enterAnimation must start after new content is laid out.
            Qt.callLater(function() {
                scrollView.contentItem.returnToBounds()
                if (wasNavigating)
                    enterAnimation.start()
            })
            // Recompute quality badges in background (handles stale values after KB updates).
            // Skip when reloading after a visualizer update — badges didn't change and the
            // visualizer path already triggers a second onShotReady via loadShot().
            if (!shotDetailPage.reloadingFromVisualizer)
                MainController.shotHistory.requestReanalyzeBadges(id)
            shotDetailPage.reloadingFromVisualizer = false
        }
        function onShotDeleted(deletedId) {
            if (deletedId === shotDetailPage.shotId)
                pageStack.pop()
        }
        function onVisualizerInfoUpdated(id, success) {
            if (id !== shotDetailPage.shotId) return
            if (success) {
                shotDetailPage.reloadingFromVisualizer = true
                loadShot()
            } else {
                console.warn("ShotDetailPage: Failed to save visualizer info for shot", id)
            }
        }
        function onShotBadgesUpdated(id, channeling, tempUnstable, grindIssue, skipFirstFrame) {
            if (id !== shotDetailPage.shotId) return
            var updated = Object.assign({}, shotData)
            updated.channelingDetected = channeling
            updated.temperatureUnstable = tempUnstable
            updated.grindIssueDetected = grindIssue
            updated.skipFirstFrameDetected = skipFirstFrame
            shotData = updated
        }
    }

    function navigateToShot(index) {
        if (index >= 0 && index < shotIds.length) {
            enterAnimation.stop()
            exitAnimation.stop()
            swipeDirection = index > currentIndex ? 1 : -1
            exitAnimation.targetIndex = index
            exitAnimation.start()
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
                MainController.shotHistory.requestUpdateVisualizerInfo(shotDetailPage.shotId, shotId, url)
            }
        }
        function onUpdateSuccess(visualizerId) {
            if (shotDetailPage.shotId > 0) {
                shotDetailPage.reloadingFromVisualizer = true
                loadShot()
            }
        }
    }

    // Exit: slide + fade out, then load new shot; Enter: slide + fade in on data ready
    SequentialAnimation {
        id: exitAnimation
        property int targetIndex: 0

        ParallelAnimation {
            NumberAnimation {
                target: scrollView; property: "opacity"
                to: 0; duration: 140; easing.type: Easing.InQuad
            }
            NumberAnimation {
                target: contentSlide; property: "x"
                to: shotDetailPage.swipeDirection * -Theme.scaled(50)
                duration: 140; easing.type: Easing.InQuad
            }
        }
        ScriptAction {
            script: {
                currentIndex = exitAnimation.targetIndex
                shotId = shotIds[currentIndex]
                contentSlide.x = shotDetailPage.swipeDirection * Theme.scaled(50)
                shotDetailPage.navigating = true
                shotDetailPage.reloadingFromVisualizer = false
                loadShot()
            }
        }
    }

    ParallelAnimation {
        id: enterAnimation
        NumberAnimation {
            target: scrollView; property: "opacity"
            from: 0; to: 1; duration: 180; easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: contentSlide; property: "x"
            to: 0; duration: 180; easing.type: Easing.OutQuad
        }
    }

    ScrollView {
        id: scrollView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentWidth: availableWidth
        transform: Translate { id: contentSlide; x: 0 }
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            // Header: Profile (Temp) + Basic/Advanced toggle
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            textFormat: Text.RichText
                            text: {
                                var name = shotData.profileName || TranslationManager.translate("shotdetail.title", "Shot Detail")
                                var t = shotData.temperatureOverride
                                var result
                                if (t !== undefined && t !== null && t > 0) {
                                    result = name + " (" + Math.round(t) + "\u00B0C)"
                                } else {
                                    result = name
                                }
                                return Theme.replaceEmojiWithImg(result, Theme.titleFont.pixelSize)
                            }
                            font: Theme.titleFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: shotData.dateTime || ""
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                            Layout.maximumWidth: shotDetailPage.width * 0.35
                        }

                        QualityBadges {
                            visible: !!(shotData.profileKbId
                                        || shotData.channelingDetected
                                        || shotData.temperatureUnstable
                                        || shotData.grindIssueDetected
                                        || shotData.skipFirstFrameDetected)
                            Layout.fillWidth: false
                            Layout.maximumWidth: shotDetailPage.width * 0.5
                            channelingDetected: shotData.channelingDetected ?? false
                            temperatureUnstable: shotData.temperatureUnstable ?? false
                            grindIssueDetected: shotData.grindIssueDetected ?? false
                            skipFirstFrameDetected: shotData.skipFirstFrameDetected ?? false
                            onSummaryRequested: detailAnalysisDialog.open()
                        }

                        ShotAnalysisDialog {
                            id: detailAnalysisDialog
                            shotData: shotDetailPage.shotData
                        }
                    }
                }

                // KB sparkle button — opens the profile knowledge base
                Image {
                    id: headerSparkle
                    visible: !!(shotData.profileKbId)
                    source: "qrc:/icons/sparkle.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                    Layout.alignment: Qt.AlignVCenter
                    opacity: headerSparkleArea.containsMouse ? 1.0 : 0.6
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }

                    AccessibleMouseArea {
                        id: headerSparkleArea
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(-8)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        accessibleName: TranslationManager.translate("profileselector.accessible.view_knowledge", "View AI knowledge base")
                        accessibleItem: headerSparkle
                        onAccessibleClicked: {
                            shotKnowledgeDialog.profileTitle = shotData.profileName || ""
                            shotKnowledgeDialog.content = ProfileManager.profileKnowledgeContent(shotData.profileName)
                            shotKnowledgeDialog.open()
                        }
                    }
                }

                // Edit shot button
                Rectangle {
                    Layout.preferredWidth: Theme.scaled(36)
                    Layout.preferredHeight: Theme.scaled(36)
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.scaled(18)
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: Theme.scaled(1)

                    Accessible.ignored: true

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/edit.svg"
                        sourceSize.width: Theme.scaled(18)
                        sourceSize.height: Theme.scaled(18)
                        Accessible.ignored: true

                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.textColor
                        }
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: TranslationManager.translate("shotdetail.button.edit", "Edit shot")
                        accessibleItem: parent
                        onAccessibleClicked: {
                            pageStack.push(Qt.resolvedUrl("PostShotReviewPage.qml"),
                                { editShotId: shotDetailPage.shotId, autoClose: false })
                        }
                    }
                }

                // Basic/Advanced mode toggle (matches espresso page view selector)
                Rectangle {
                    Layout.preferredWidth: Theme.scaled(36)
                    Layout.preferredHeight: Theme.scaled(36)
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.scaled(18)
                    color: shotDetailPage.advancedMode ? Theme.accentColor : Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: Theme.scaled(1)

                    Accessible.ignored: true

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/settings.svg"
                        sourceSize.width: Theme.scaled(18)
                        sourceSize.height: Theme.scaled(18)

                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: shotDetailPage.advancedMode ? Theme.primaryContrastColor : Theme.textColor
                        }
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: shotDetailPage.advancedMode
                            ? TranslationManager.translate("shotReview.mode.switchBasic", "Switch to basic view")
                            : TranslationManager.translate("shotReview.mode.switchAdvanced", "Switch to advanced view")
                        accessibleItem: parent
                        accessibleRole: Accessible.CheckBox
                        accessibleChecked: shotDetailPage.advancedMode
                        onAccessibleClicked: {
                            shotDetailPage.advancedMode = !shotDetailPage.advancedMode
                            Settings.setValue("shotReview/advancedMode", shotDetailPage.advancedMode)
                        }
                    }
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
                Accessible.description: TranslationManager.translate("shotdetail.accessible.graph.swipe", "Swipe left for older shot, swipe right for newer shot.")
                Accessible.focusable: true
                focus: true

                // Visual offset during swipe
                transform: Translate { x: graphSwipeArea.swipeOffset * 0.3 }

                HistoryShotGraph {
                    id: shotGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    advancedMode: shotDetailPage.advancedMode
                    showPhaseLabels: shotDetailPage.advancedMode
                    pressureData: shotData.pressure || []
                    flowData: shotData.flow || []
                    temperatureData: shotData.temperature || []
                    weightData: shotData.weight || []
                    weightFlowRateData: shotData.weightFlowRate || []
                    resistanceData: shotData.resistance || []
                    conductanceData: shotData.conductance || []
                    darcyResistanceData: shotData.darcyResistance || []
                    conductanceDerivativeData: shotData.conductanceDerivative || []
                    temperatureMixData: shotData.temperatureMix || []
                    pressureGoalData: shotData.pressureGoal || []
                    flowGoalData: shotData.flowGoal || []
                    temperatureGoalData: shotData.temperatureGoal || []
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
                            Qt.callLater(function() { scrollView.contentItem.returnToBounds() })
                        }
                    }
                }

            }

            GraphLegend {
                graph: shotGraph
                advancedMode: shotDetailPage.advancedMode
            }

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
                    Accessible.ignored: true
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

            // Phase summary panel (advanced mode only)
            PhaseSummaryPanel {
                Layout.fillWidth: true
                phaseSummaries: shotData.phaseSummaries || []
                visible: shotDetailPage.advancedMode && (shotData.phaseSummaries || []).length > 0
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

            // Bean + Grinder info side by side
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: !!(shotData.beanBrand || shotData.beanType || shotData.roastDate || shotData.roastLevel
                            || shotData.grinderBrand || shotData.grinderModel || shotData.grinderBurrs || shotData.grinderSetting)

                // Bean info card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1  // Equal weight
                    Layout.preferredHeight: beanColumn.height + Theme.spacingLarge
                    Layout.alignment: Qt.AlignTop
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
                            textFormat: Text.RichText
                            text: {
                                var title = TranslationManager.translate("shotdetail.beaninfo", "Beans")
                                var grind = shotData.grinderSetting || ""
                                var result = grind ? title + " (" + grind + ")" : title
                                return Theme.replaceEmojiWithImg(result, Theme.subtitleFont.pixelSize)
                            }
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: Theme.spacingLarge
                            rowSpacing: Theme.spacingSmall
                            Layout.fillWidth: true

                            Tr { key: "shotdetail.roaster"; fallback: "Roaster:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.beanBrand); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.beanBrand || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.beanBrand); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                            Tr { key: "shotdetail.coffee"; fallback: "Coffee:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.beanType); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.beanType || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.beanType); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                            Tr { key: "shotdetail.roastdate"; fallback: "Roast Date:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.roastDate); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.roastDate || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.roastDate); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                            Tr { key: "shotdetail.roastlevel"; fallback: "Roast Level:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.roastLevel); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.roastLevel || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.roastLevel); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }
                        }
                    }
                }

                // Grinder info card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1  // Equal weight
                    Layout.preferredHeight: grinderColumn.height + Theme.spacingLarge
                    Layout.alignment: Qt.AlignTop
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    visible: !!(shotData.grinderBrand || shotData.grinderModel || shotData.grinderBurrs || shotData.grinderSetting)
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
                            Accessible.ignored: true
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: Theme.spacingLarge
                            rowSpacing: Theme.spacingSmall
                            Layout.fillWidth: true

                            Tr { key: "shotdetail.brand"; fallback: "Brand:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.grinderBrand); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.grinderBrand || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.grinderBrand); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                            Tr { key: "shotdetail.model"; fallback: "Model:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.grinderModel); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.grinderModel || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.grinderModel); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                            Tr { key: "shotdetail.burrs"; fallback: "Burrs:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.grinderBurrs); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.grinderBurrs || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.grinderBurrs); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                            Tr { key: "shotdetail.setting"; fallback: "Setting:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: !!(shotData.grinderSetting); Accessible.ignored: true }
                            Text { textFormat: Text.RichText; text: Theme.replaceEmojiWithImg(shotData.grinderSetting || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: !!(shotData.grinderSetting); Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }
                        }
                    }
                }
            }

            // Analysis
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: analysisColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: shotDetailPage.advancedMode && (shotData.drinkTds > 0 || shotData.drinkEy > 0)
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
                        Accessible.ignored: true
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
                visible: !!shotData.barista && shotData.barista !== ""
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
                        Accessible.ignored: true
                    }

                    Text {
                        textFormat: Text.RichText
                        text: Theme.replaceEmojiWithImg(shotData.barista || "", Theme.labelFont.pixelSize)
                        font: Theme.labelFont
                        color: Theme.textColor
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                }
            }

            // Actions
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                AccessibleButton {
                    visible: shotDetailPage.advancedMode
                    text: TranslationManager.translate("shotdetail.viewdebuglog", "View Debug Log")
                    accessibleName: TranslationManager.translate("shotDetail.viewDebugLog", "View debug log for this shot")
                    Layout.fillWidth: true
                    onClicked: debugLogDialog.open()
                }

                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.deleteshot", "Delete Shot")
                    accessibleName: TranslationManager.translate("shotDetail.deleteShotPermanently", "Permanently delete this shot from history")
                    destructive: true
                    Layout.fillWidth: true
                    onClicked: deleteConfirmDialog.open()
                }
            }

            // Visualizer status
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: !!shotData.visualizerId && shotData.visualizerId !== ""
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
                            Accessible.ignored: true
                        }
                        Tr {
                            key: "shotdetail.uploadedtovisualizer"
                            fallback: "Uploaded to Visualizer"
                            font: Theme.labelFont
                            color: Theme.successColor
                            Accessible.ignored: true
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: shotData.visualizerId || ""
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Layout.maximumWidth: parent.width * 0.5
                        elide: Text.ElideRight
                        Accessible.ignored: true
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
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent.width * 0.9
        height: parent.height * 0.8
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

            Text {
                text: TranslationManager.translate("shotdetail.debuglog", "Debug Log")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(10)
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

            AccessibleButton {
                text: TranslationManager.translate("shotdetail.close", "Close")
                accessibleName: TranslationManager.translate("shotdetail.closeDebugLog", "Close debug log")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: debugLogDialog.close()
            }
        }
    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(360)
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

            Text {
                text: TranslationManager.translate("shotdetail.deleteconfirmtitle", "Delete Shot?")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("shotdetail.deleteconfirmmessage", "This will permanently delete this shot from history.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("shotdetail.cancelDelete", "Cancel delete")
                    Layout.fillWidth: true
                    onClicked: deleteConfirmDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.delete", "Delete")
                    accessibleName: TranslationManager.translate("shotdetail.confirmDelete", "Confirm delete shot")
                    destructive: true
                    Layout.fillWidth: true
                    onClicked: {
                        deleteConfirmDialog.close()
                        MainController.shotHistory.requestDeleteShot(shotId)
                    }
                }
            }
        }
    }

    // Profile AI knowledge base dialog
    Dialog {
        id: shotKnowledgeDialog
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(shotKbContent.implicitHeight + Theme.scaled(120), parent.height - Theme.scaled(80))
        padding: 0
        modal: true

        property string profileTitle: ""
        property string content: ""

        function formatContent(raw) {
            var lines = raw.split('\n')
            var parts = []
            for (var i = 0; i < lines.length; i++) {
                var line = lines[i]
                if (!line.trim()) continue
                if (line.startsWith('Also matches:') || line.startsWith('AnalysisFlags:')) continue
                var colonIdx = line.indexOf(': ')
                if (colonIdx > 0 && colonIdx <= 35 && !line.startsWith('DO NOT') && !line.startsWith('-')) {
                    var label = line.substring(0, colonIdx).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                    var value = line.substring(colonIdx + 2).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                    parts.push('<b>' + label + ':</b> ' + value)
                } else if (line.startsWith('DO NOT')) {
                    parts.push('<i>' + line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') + '</i>')
                } else {
                    parts.push(line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'))
                }
            }
            return parts.join('<br>')
        }

        header: Item {
            implicitHeight: Theme.scaled(50)

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scaled(8)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.primaryColor
                    }
                }

                Text {
                    text: shotKnowledgeDialog.profileTitle
                    font: Theme.titleFont
                    color: Theme.textColor
                    anchors.verticalCenter: parent.verticalCenter
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

        contentItem: Flickable {
            clip: true
            contentHeight: shotKbContent.implicitHeight + Theme.scaled(30)
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            Text {
                id: shotKbContent
                width: parent.width - Theme.scaled(40)
                x: Theme.scaled(20)
                y: Theme.scaled(15)
                text: shotKnowledgeDialog.formatContent(shotKnowledgeDialog.content)
                textFormat: Text.RichText
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
        }

        footer: Item {
            implicitHeight: Theme.scaled(55)

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }

            AccessibleButton {
                anchors.centerIn: parent
                width: Theme.scaled(100)
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                onClicked: shotKnowledgeDialog.close()
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
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

        // Profile name + date in the bottom bar remain visible while the user scrolls,
        // providing context when the header is off-screen.
        ColumnLayout {
            visible: !!(shotData.profileName)
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Accessible.role: Accessible.StaticText
            Accessible.name: (shotData.profileName || "") + (shotData.dateTime ? ", " + shotData.dateTime : "")
            Accessible.focusable: true

            Text {
                text: shotData.profileName || ""
                font: Theme.labelFont
                color: Theme.textColor
                elide: Text.ElideRight
                Layout.maximumWidth: shotDetailPage.width * 0.3
                Accessible.ignored: true
            }

            Text {
                text: shotData.dateTime || ""
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                elide: Text.ElideRight
                Layout.maximumWidth: shotDetailPage.width * 0.3
                Accessible.ignored: true
            }
        }

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
                    color: Theme.primaryContrastColor
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
                    color: Theme.primaryContrastColor
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

        // Discuss button - opens external AI app
        Rectangle {
            id: discussButton
            readonly property bool isClaudeDesktopReady:
                Settings.discussShotApp !== Settings.discussAppClaudeDesktop
                || Settings.claudeRcSessionUrl.length > 0
            visible: shotData.duration > 0 && Settings.discussShotApp !== Settings.discussAppNone
            enabled: isClaudeDesktopReady
            opacity: enabled ? 1.0 : 0.5
            Layout.preferredWidth: discussContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: discussArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("shotdetail.accessible.discuss", "Discuss shot with external AI app")
            Accessible.focusable: true
            Accessible.onPressAction: discussArea.clicked(null)

            Row {
                id: discussContent
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
                    key: "shotdetail.discuss"
                    fallback: "Discuss"
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: discussArea
                anchors.fill: parent
                enabled: discussButton.isClaudeDesktopReady
                onClicked: {
                    if (!Settings.mcpEnabled && MainController.aiManager) {
                        var summary = MainController.aiManager.generateHistoryShotSummary(shotData)
                        if (summary.length > 0) MainController.copyToClipboard(summary)
                    }
                    var url = Settings.discussShotUrl()
                    if (url.length > 0) Settings.openDiscussUrl(url)
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

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
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
