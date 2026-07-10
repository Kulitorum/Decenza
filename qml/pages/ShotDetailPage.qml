import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../components"
import "../components/layout/ShotPlanConfig.js" as ShotPlanConfig

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

    // Field selection + order for the snapshot line, taken from the user's first
    // idle-page Shot Plan widget so this line shows the fields they configured.
    // Reactive on layout edits. Only the item list is used — the snapshot is
    // always a plain fragment line (sentence/stacked toggles are ignored).
    readonly property var _shotPlanItemOrder: {
        var layout
        try { layout = JSON.parse(Settings.network.layoutConfiguration) } catch (e) { layout = null }
        return ShotPlanConfig.itemOrderFromLayout(layout)
    }

    // Whether THIS shot's grinder reports RPM (a Niche Zero does not). Gates
    // every RPM display on the page so a stale/spurious recorded RPM never shows
    // for a non-RPM grinder. Mirrors the Post-Shot Review page's editRpmCapable.
    readonly property bool _shotRpmCapable:
        Settings.dye.grinderRpmCapable(shotData.grinderBrand || "", shotData.grinderModel || "")

    // Recipe identity for the recipe card, live-resolved by shotData.recipeId
    // (a shot-linked recipe can only be archived, never deleted, so the row
    // always resolves). Grind/rpm on that card still comes from the shot
    // snapshot, never this map's pin. _resolvedRecipeId guards against
    // re-requesting on the badge-driven shotData reassigns.
    property var resolvedRecipe: ({})
    property int _resolvedRecipeId: -1

    // Resolve the recipe whenever the shot changes (initial load + swipe).
    onShotDataChanged: {
        var rid = shotData.recipeId || -1
        if (rid > 0 && rid !== _resolvedRecipeId) {
            _resolvedRecipeId = rid
            resolvedRecipe = ({})
            MainController.recipeStorage.requestRecipe(rid)
        } else if (rid <= 0) {
            _resolvedRecipeId = -1
            resolvedRecipe = ({})
        }
    }

    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, recipe) {
            if (recipeId === shotDetailPage._resolvedRecipeId)
                shotDetailPage.resolvedRecipe = recipe
        }
    }

    // Pick up toggle changes made on any other page sharing this setting
    // (Post-Shot Review, Shot Comparison, Espresso view selector).
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "shotReview/advancedMode")
                shotDetailPage.advancedMode = Settings.boolValue("shotReview/advancedMode", false)
        }
    }

    // Re-assert on every activation, not just creation — returning here after a
    // page was pushed on top would otherwise keep that page's header title.
    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("shotdetail.title", "Shot Detail")
    }

    // One labeled component row inside the recipe card: a caption label over
    // its value; hides itself when the value is empty (so absent components
    // leave no gap).
    component RecipeField: ColumnLayout {
        id: rf
        property string fieldLabel: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 0
        visible: rf.value !== ""
        Text {
            text: rf.fieldLabel
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            text: rf.value
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    // Hidden Tr instances for the recipe-card component labels (reuse existing
    // keys where they exist; "Dial-in" is new).
    Tr { id: trRowProfile; key: "recipes.wizard.rowProfile"; fallback: "Profile"; visible: false }
    Tr { id: trRowBeans; key: "shotdetail.beaninfo"; fallback: "Beans"; visible: false }
    Tr { id: trRowDialIn; key: "shotdetail.recipe.dialIn"; fallback: "Dial-in"; visible: false }
    Tr { id: trRowSteam; key: "recipes.wizard.rowSteam"; fallback: "Steam / milk"; visible: false }
    Tr { id: trRowWater; key: "recipes.wizard.rowHotWater"; fallback: "Hot water"; visible: false }
    Tr { id: trRowEquipment; key: "shotdetail.equipment"; fallback: "Equipment"; visible: false }

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
            // Quality badges already arrived recomputed in `shot` via
            // loadShotRecordStatic, which also persists drift to the DB and
            // emits shotBadgesUpdated when it does. No extra reanalyze call
            // needed here — onShotBadgesUpdated below catches the persist event.
        }
        function onShotDeleted(deletedId) {
            if (deletedId === shotDetailPage.shotId)
                pageStack.pop()
        }
        function onVisualizerInfoUpdated(id, success) {
            if (id !== shotDetailPage.shotId) return
            if (success) {
                loadShot()
            } else {
                console.warn("ShotDetailPage: Failed to save visualizer info for shot", id)
            }
        }
        function onShotBadgesUpdated(id, channeling, grindIssue, skipFirstFrame, pourTruncated) {
            if (id !== shotDetailPage.shotId) return
            var updated = Object.assign({}, shotData)
            updated.channelingDetected = channeling
            updated.grindIssueDetected = grindIssue
            updated.skipFirstFrameDetected = skipFirstFrame
            updated.pourTruncatedDetected = pourTruncated
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
        if (shotData.doseWeightG > 0) {
            return "1:" + (shotData.finalWeightG / shotData.doseWeightG).toFixed(1)
        }
        return "-"
    }

    // --- Recipe-component row text (all from THIS shot's frozen snapshot) ---
    // Profile · effective brew temperature.
    function recipeProfileText() {
        var parts = []
        if (shotData.profileName) parts.push(shotData.profileName)
        var t = shotData.temperatureOverrideC || 0
        if (t > 0) parts.push(Math.round(Theme.cToDisplay(t)) + Theme.tempUnitSuffix())
        return parts.join(" · ")
    }
    // Dose → yield · grind · rpm (rpm only for rpm-capable grinders).
    function recipeDialInText() {
        var _ = TranslationManager.translationVersion
        var parts = []
        var dose = shotData.doseWeightG || 0
        var yieldG = (shotData.targetWeightG || 0) > 0 ? shotData.targetWeightG : (shotData.finalWeightG || 0)
        if (dose > 0 && yieldG > 0) parts.push(dose.toFixed(1) + "g → " + yieldG.toFixed(1) + "g")
        else if (dose > 0) parts.push(dose.toFixed(1) + "g")
        var g = shotData.grinderSetting || ""
        if (g.length > 0) parts.push(TranslationManager.translate("equipment.card.lastGrind", "Grind %1").arg(g))
        if ((shotData.rpm || 0) > 0 && shotDetailPage._shotRpmCapable)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(shotData.rpm))
        return parts.join(" · ")
    }
    // Steam / milk (pitcher · Ng milk) — empty unless the recipe steams milk.
    function recipeSteamText() {
        if (!shotData.steamJson) return ""
        try {
            var s = JSON.parse(shotData.steamJson)
            if (!s.hasMilk) return ""
            var parts = []
            if (s.pitcherName) parts.push(s.pitcherName)
            if ((s.milkWeightG || 0) > 0)
                parts.push(TranslationManager.translate("recipes.list.milkWeight", "%1g milk").arg(s.milkWeightG))
            return parts.join(" · ")
        } catch (e) { return "" }
    }
    // Hot water (vessel · volume · temp) — empty unless the recipe adds water.
    function recipeWaterText() {
        if (!shotData.hotWaterJson) return ""
        try {
            var w = JSON.parse(shotData.hotWaterJson)
            if (!w.hasWater) return ""
            var parts = []
            if (w.vesselName) parts.push(w.vesselName)
            if ((w.volume || 0) > 0) parts.push(w.volume + (w.mode === "volume" ? "ml" : "g"))
            if ((w.temperatureC || 0) > 0) parts.push(Math.round(Theme.cToDisplay(w.temperatureC)) + Theme.tempUnitSuffix())
            return parts.join(" · ")
        } catch (e) { return "" }
    }

    function graphAccessibleDescription() {
        var parts = []
        if (shotData.profileName)
            parts.push(shotData.profileName)
        parts.push((shotData.durationSec || 0).toFixed(0) + "s")
        parts.push((shotData.doseWeightG || 0).toFixed(1) + "g in")
        parts.push((shotData.finalWeightG || 0).toFixed(1) + "g out")
        if (shotData.doseWeightG > 0)
            parts.push("ratio " + formatRatio())
        return TranslationManager.translate("shotdetail.accessible.graph", "Shot graph") + ": " + parts.join(", ")
    }


    // Handle visualizer upload/update status changes
    Connections {
        target: MainController.visualizer
        function onUploadSuccess(shotId, url) {
            // Visualizer-id persistence is owned by MainController
            // (uploadSucceededForShot → requestUpdateVisualizerInfo),
            // not this page. UI refresh still arrives via the
            // visualizerInfoUpdated handler.
        }
        function onUpdateSuccess(visualizerId) {
            if (shotDetailPage.shotId > 0) {
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
                                var t = shotData.temperatureOverrideC
                                var result
                                if (t !== undefined && t !== null && t > 0) {
                                    result = name + " (" + Math.round(Theme.cToDisplay(t)) + Theme.tempUnitSuffix() + ")"
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
                                        || shotData.grindIssueDetected
                                        || shotData.skipFirstFrameDetected
                                        || shotData.pourTruncatedDetected)
                            Layout.fillWidth: false
                            Layout.maximumWidth: shotDetailPage.width * 0.5
                            channelingDetected: shotData.channelingDetected ?? false
                            grindIssueDetected: shotData.grindIssueDetected ?? false
                            skipFirstFrameDetected: shotData.skipFirstFrameDetected ?? false
                            pourTruncatedDetected: shotData.pourTruncatedDetected ?? false
                            verdictCategory: (shotData && shotData.detectorResults)
                                ? (shotData.detectorResults.verdictCategory ?? "") : ""
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

                // Create-recipe button (promote this shot to a recipe —
                // opens the composer prefilled from it, add-recipes)
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
                        source: "qrc:/icons/pin.svg"
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
                        accessibleName: TranslationManager.translate("shotdetail.button.recipe", "Create recipe from this shot")
                        accessibleItem: parent
                        onAccessibleClicked: {
                            pageStack.push(Qt.resolvedUrl("RecipeWizardPage.qml"),
                                { mode: "create", promoteShotId: shotDetailPage.shotId })
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

            // Shot Plan snapshot line — this shot's frozen dial-in rendered as a
            // glanceable sentence, so shots can be compared by swiping without
            // scrolling (#1447). Fed ENTIRELY from the shot snapshot, never the
            // live dial; reuses the home-screen ShotPlanText renderer so the
            // format can't drift. Non-interactive (clicked() left unhandled).
            ShotPlanText {
                id: shotPlanSnapshot
                Layout.fillWidth: true
                // Sit tight under the title like the Shot Review page: the main
                // column's spacing is spacingMedium (card rhythm), so pull the
                // snapshot up to leave only the review page's scaled(6) gap.
                Layout.topMargin: Theme.scaled(6) - Theme.spacingMedium
                visible: text !== ""
                sentence: false
                maxLines: 2
                // Fields + order come from the user's Shot Plan widget config.
                itemOrder: shotDetailPage._shotPlanItemOrder
                singleTemp: true
                profileName: shotData.profileName || ""
                dose: shotData.doseWeightG || 0
                // targetWeightG is the planned target (0 for volume/timer
                // profiles) — fall back to the actual output so a yield still shows.
                profileYield: shotData.targetWeightG || 0
                targetWeight: (shotData.targetWeightG || 0) > 0
                    ? shotData.targetWeightG : (shotData.finalWeightG || 0)
                yieldTargetOnly: true
                // temperatureOverrideC always carries the effective brew temp
                // (user override OR profile default); 0 only for legacy volume shots.
                profileTemp: shotData.temperatureOverrideC || 0
                overrideTemp: shotData.temperatureOverrideC || 0
                tempOverridden: false
                roasterBrand: shotData.beanBrand || ""
                coffeeName: shotData.beanType || ""
                roastDate: shotData.roastDate || ""
                grindSize: shotData.grinderSetting || ""
                grindRpm: shotData.rpm || 0
                // Only show RPM for grinders that actually report it (a Niche
                // Zero does not); a stale/spurious recorded RPM must not surface.
                rpmCapable: shotDetailPage._shotRpmCapable
                beverageType: shotData.beverageType || "espresso"
                // Maintenance shots are never saved to history, so a saved shot is
                // always a normal plan — no "no coffee in portafilter" warning here.
                isCleaning: false
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.focusable: true
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
                    maxTime: shotData.durationSec || 60
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
                        (shotData.durationSec || 0).toFixed(1) + "s"
                    Tr {
                        key: "shotdetail.duration"
                        fallback: "Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (shotData.durationSec || 0).toFixed(1) + "s"
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
                        (shotData.doseWeightG || 0).toFixed(1) + "g"
                    Tr {
                        key: "shotdetail.dose"
                        fallback: "Dose"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (shotData.doseWeightG || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeDoseColor
                        Accessible.ignored: true
                    }
                }

                // Grind removed from the metrics row — it's a per-shot dial-in,
                // now shown in the top Shot Plan snapshot line (glanceable) and
                // on its owning card (recipe when a recipe was used, else bean).

                // Output (with optional target)
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: {
                        var label = TranslationManager.translate("shotdetail.output", "Output") + ": " +
                            (shotData.finalWeightG || 0).toFixed(1) + "g"
                        var t = shotData.targetWeightG
                        if (t !== undefined && t !== null && t > 0 && Math.abs(t - shotData.finalWeightG) > 0.5)
                            label += " (" + Math.round(t) + "g target)"
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
                            text: (shotData.finalWeightG || 0).toFixed(1) + "g"
                            font: Theme.subtitleFont
                            color: Theme.dyeOutputColor
                        }
                        Text {
                            visible: {
                                var t = shotData.targetWeightG
                                return t !== undefined && t !== null && t > 0
                                    && Math.abs(t - shotData.finalWeightG) > 0.5
                            }
                            text: {
                                var t = shotData.targetWeightG
                                return (t !== undefined && t !== null && t > 0) ? "(" + Math.round(t) + "g)" : ""
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
                        ((shotData.enjoyment0to100 || 0) > 0 ? shotData.enjoyment0to100 + "%" : "-")
                    Tr {
                        key: "shotdetail.rating"
                        fallback: "Rating"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (shotData.enjoyment0to100 || 0) > 0 ? shotData.enjoyment0to100 + "%" : "-"
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

            // Recipe card — shown only when this shot was pulled with a recipe
            // (recipeId > 0). One cohesive card that presents the recipe AND its
            // components (profile, beans, dial-in, steam/water, equipment),
            // modelled on the recipe editor's summary — so it reads as a recipe,
            // not scattered cards. Identity (name, drink type, profile) is live-
            // resolved by id and follows renames; every value shown is this
            // shot's own frozen snapshot, never the recipe's since-edited pins.
            // When a recipe is used this replaces the standalone bean/equipment
            // cards below (which gate to the no-recipe case).
            Rectangle {
                id: recipeCard
                Layout.fillWidth: true
                Layout.preferredHeight: recipeColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                border.color: Theme.borderColor
                border.width: Theme.scaled(1)
                visible: (shotData.recipeId || -1) > 0

                readonly property string recipeName: shotDetailPage.resolvedRecipe.name || ""
                readonly property string recipeDrinkLabel:
                    DrinkType.shortLabel(DrinkType.fromRecipeMap(shotDetailPage.resolvedRecipe))

                Accessible.role: Accessible.Grouping
                Accessible.name: {
                    var parts = [TranslationManager.translate("shotdetail.recipe", "Recipe")]
                    if (recipeName !== "") parts.push(recipeName)
                    if (recipeDrinkLabel !== "") parts.push(recipeDrinkLabel)
                    var p = shotDetailPage.recipeProfileText(); if (p !== "") parts.push(p)
                    var b = detailRecipeBeanSummary.summaryText; if (b !== "") parts.push(b)
                    var d = shotDetailPage.recipeDialInText(); if (d !== "") parts.push(d)
                    var e = detailRecipeEquipment.accessibleSummary; if (e !== "") parts.push(e)
                    return parts.join(", ")
                }

                ColumnLayout {
                    id: recipeColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    // --- Hero: eyebrow + recipe name + drink type ---
                    Tr {
                        key: "shotdetail.recipe"
                        fallback: "Recipe"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: recipeCard.recipeName !== ""
                        textFormat: Text.RichText
                        text: Theme.replaceEmojiWithImg(recipeCard.recipeName, Theme.titleFont.pixelSize)
                        font: Theme.titleFont
                        color: Theme.textColor
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)
                        visible: recipeCard.recipeDrinkLabel !== ""
                        ColoredIcon {
                            Layout.alignment: Qt.AlignVCenter
                            source: DrinkType.icon(DrinkType.fromRecipeMap(shotDetailPage.resolvedRecipe))
                            iconWidth: Theme.scaled(16)
                            iconHeight: Theme.scaled(16)
                            iconColor: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: recipeCard.recipeDrinkLabel
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(2)
                        Layout.bottomMargin: Theme.scaled(2)
                        height: Theme.scaled(1)
                        color: Theme.borderColor
                        Accessible.ignored: true
                    }

                    // --- Components (each from this shot's frozen values) ---
                    RecipeField {
                        fieldLabel: trRowProfile.text
                        value: shotDetailPage.recipeProfileText()
                    }

                    // Beans — the shared BeanSummary + Bean Base details.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        Text {
                            text: trRowBeans.text
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }
                        BeanSummary {
                            id: detailRecipeBeanSummary
                            Layout.fillWidth: true
                            useShotData: true
                            roasterName: shotData.beanBrand || ""
                            coffeeName: shotData.beanType || ""
                            roastDate: shotData.roastDate || ""
                            roastLevel: shotData.roastLevel || ""
                            beanBaseData: shotData.beanBaseJson || ""
                        }
                        BeanBaseDetailsRow {
                            Layout.fillWidth: true
                            beanBaseJson: shotData.beanBaseJson || ""
                        }
                    }

                    RecipeField {
                        fieldLabel: trRowDialIn.text
                        value: shotDetailPage.recipeDialInText()
                    }
                    RecipeField {
                        fieldLabel: trRowSteam.text
                        value: shotDetailPage.recipeSteamText()
                    }
                    RecipeField {
                        fieldLabel: trRowWater.text
                        value: shotDetailPage.recipeWaterText()
                    }

                    // Equipment — the shared EquipmentSummary (grinder/basket/
                    // puck only; grind/rpm live on the Dial-in row above).
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        visible: detailRecipeEquipment.accessibleSummary !== ""
                        Text {
                            text: trRowEquipment.text
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }
                        EquipmentSummary {
                            id: detailRecipeEquipment
                            Layout.fillWidth: true
                            grinderName: shotData.equipmentName || ""
                            grinderBrand: shotData.grinderBrand || ""
                            grinderModel: shotData.grinderModel || ""
                            grinderBurrs: shotData.grinderBurrs || ""
                            basketBrand: shotData.basketBrand || ""
                            basketModel: shotData.basketModel || ""
                            puckPrepCanonical: shotData.puckPrep || ""
                            equipmentState: shotData.equipmentState || ""
                        }
                    }
                    // Read-only: re-linking beans is an edit, available on the
                    // Post-Shot Review page (via the header Edit button).
                }
            }

            // Bean + Grinder info side by side — shown ONLY when the shot used
            // no recipe. With a recipe, the beans and equipment are folded into
            // the recipe card above so it reads as one cohesive recipe.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: (shotData.recipeId || -1) <= 0

                // Bean info card: read-only summary of this shot's bean
                // snapshot + a re-link action ("historicalShot" semantics:
                // updates only this shot, never the active bag).
                Rectangle {
                    id: beanCard
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1  // Equal weight
                    Layout.preferredHeight: beanColumn.height + Theme.spacingLarge
                    Layout.alignment: Qt.AlignTop
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    // Grind is bag-scoped without a recipe, so it lives here ONLY
                    // when the shot used no recipe (recipeId <= 0); with a recipe
                    // it lives on the recipe card instead.
                    readonly property bool showGrind: (shotData.recipeId || -1) <= 0
                    readonly property string beanGrindLine: {
                        var _ = TranslationManager.translationVersion
                        var parts = []
                        var g = shotData.grinderSetting || ""
                        if (g.length > 0)
                            parts.push(TranslationManager.translate("equipment.card.lastGrind", "Grind %1").arg(g))
                        if ((shotData.rpm || 0) > 0 && shotDetailPage._shotRpmCapable)
                            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(shotData.rpm))
                        return parts.join(" · ")
                    }

                    Accessible.role: Accessible.Grouping
                    Accessible.name: {
                        var name = TranslationManager.translate("shotdetail.beaninfo", "Beans")
                        if (showGrind && beanGrindLine !== "")
                            name += ", " + beanGrindLine
                        return name
                    }

                    ColumnLayout {
                        id: beanColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        Tr {
                            key: "shotdetail.beaninfo"
                            fallback: "Beans"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }

                        // Grind/rpm — this shot's frozen dial-in (grind's home
                        // when no recipe was used).
                        Text {
                            Layout.fillWidth: true
                            visible: beanCard.showGrind && beanCard.beanGrindLine !== ""
                            text: beanCard.beanGrindLine
                            font: Theme.captionFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }

                        BeanSummary {
                            id: detailBeanSummary
                            Layout.fillWidth: true
                            useShotData: true
                            roasterName: shotData.beanBrand || ""
                            coffeeName: shotData.beanType || ""
                            roastDate: shotData.roastDate || ""
                            roastLevel: shotData.roastLevel || ""
                            beanBaseData: shotData.beanBaseJson || ""
                        }

                        // Bean Base details (per-shot snapshot — shows the bean
                        // this shot was actually pulled with). Zero footprint
                        // for unlinked/legacy shots.
                        BeanBaseDetailsRow {
                            Layout.fillWidth: true
                            beanBaseJson: shotData.beanBaseJson || ""
                        }
                        // Read-only: re-linking beans is an edit, done on the
                        // Post-Shot Review page (via the header Edit button).
                    }
                }

                // Equipment info card (grinder + basket + puck prep). Shares the
                // EquipmentSummary renderer with the inventory card and the
                // post-shot review page, fed from the shot's resolved fields.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1  // Equal weight
                    Layout.preferredHeight: equipmentColumn.height + Theme.spacingLarge
                    Layout.alignment: Qt.AlignTop
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    // Grind/rpm is a per-shot dial-in, not equipment — it lives on
                    // the recipe card (recipe used) or the bean card, never here. The
                    // gate is grinder/basket/puck identity only.
                    visible: !!(shotData.grinderBrand || shotData.grinderModel || shotData.grinderBurrs
                                || shotData.basketBrand || shotData.basketModel
                                || shotData.puckPrep || shotData.equipmentName)
                    Accessible.role: Accessible.Grouping
                    Accessible.name: TranslationManager.translate("shotdetail.equipment", "Equipment")
                                     + ": " + equipmentSummary.accessibleSummary

                    ColumnLayout {
                        id: equipmentColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        Tr {
                            key: "shotdetail.equipment"
                            fallback: "Equipment"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            Accessible.ignored: true
                        }

                        EquipmentSummary {
                            id: equipmentSummary
                            Layout.fillWidth: true
                            grinderName: shotData.equipmentName || ""
                            grinderBrand: shotData.grinderBrand || ""
                            grinderModel: shotData.grinderModel || ""
                            grinderBurrs: shotData.grinderBurrs || ""
                            // grindSetting/rpm deliberately NOT fed — grind is a
                            // dial-in, shown on the recipe or bean card, not here.
                            basketBrand: shotData.basketBrand || ""
                            basketModel: shotData.basketModel || ""
                            puckPrepCanonical: shotData.puckPrep || ""
                            equipmentState: shotData.equipmentState || ""
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
                visible: shotDetailPage.advancedMode && (shotData.drinkTdsPct > 0 || shotData.drinkEyPct > 0)
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
                            Text { text: (shotData.drinkTdsPct || 0).toFixed(2) + "%"; font: Theme.bodyFont; color: Theme.dyeTdsColor }
                        }

                        ColumnLayout {
                            spacing: Theme.scaled(2)
                            Tr { key: "shotdetail.ey"; fallback: "EY"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                            Text { text: (shotData.drinkEyPct || 0).toFixed(1) + "%"; font: Theme.bodyFont; color: Theme.dyeEyColor }
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
                    font.family: Theme.monoFontFamily
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

    // Shot Detail is read-only — beans are re-linked on the Post-Shot Review
    // page. Still refresh if this shot's metadata changes elsewhere (e.g. after
    // editing it on the review page pushed on top), so returning shows fresh data.
    Connections {
        target: MainController.shotHistory
        function onShotMetadataUpdated(id, success) {
            if (id === shotDetailPage.shotId && success)
                shotDetailPage.loadShot()
        }
    }

    // Profile AI knowledge base dialog
    // Shared KB popup (qml/components/ProfileKnowledgeDialog.qml).
    ProfileKnowledgeDialog {
        id: shotKnowledgeDialog
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
            visible: shotData.durationSec > 0 && !MainController.visualizer.uploading
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
            visible: MainController.aiManager && MainController.aiManager.isConfigured && shotData.durationSec > 0
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
                Settings.network.discussShotApp !== Settings.network.discussAppClaudeDesktop
                || Settings.network.claudeRcSessionUrl.length > 0
            visible: shotData.durationSec > 0 && Settings.network.discussShotApp !== Settings.network.discussAppNone
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
                    if (!Settings.mcp.mcpEnabled && MainController.aiManager) {
                        // Prose, not the JSON envelope — the user is pasting this into
                        // an external AI tool, where prose is more readable and avoids
                        // double-shipping the structured fields.
                        var summary = MainController.aiManager.buildShotAnalysisProseForShot(shotData)
                        if (summary.length > 0) MainController.copyToClipboard(summary)
                    }
                    var url = Settings.network.discussShotUrl()
                    if (url.length > 0) Settings.network.openDiscussUrl(url)
                }
            }
        }

        // Email Prompt button - fallback for users without API keys
        Rectangle {
            id: emailButton
            visible: MainController.aiManager && !MainController.aiManager.isConfigured && shotData.durationSec > 0
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
                    // Prose, not the JSON envelope — the email body lands in the
                    // user's mail client; prose is readable and the prior JSON shape
                    // double-shipped structured fields (#1042).
                    var prompt = MainController.aiManager.buildShotAnalysisProseForShot(shotData)
                    var subject = "Espresso AI Analysis - " + (shotData.profileName || "Shot")
                    Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent(subject) + "&body=" + encodeURIComponent(prompt))
                }
            }
        }
    }

}
