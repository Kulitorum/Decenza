pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Decenza
import "OperationNavigationPolicy.js" as OperationNavigationPolicy

// One local navigation stack and destination catalog in each application instance.
// The browser supplies tracked mutations; native window/device lifecycle stays in main.qml.
T.StackView {
    id: root
    property alias stackView: root
    property bool navigationInProgress: false
    property Item _previousPage: null
    property bool screensaverActive: currentItem !== null && currentItem.objectName === "screensaverPage"
    property bool retainSearchOnActivation: false
    property bool pageZoomEnabled: true
    property var pushPageHandler: null
    property var backHandler: null
    property var postShotReviewHandler: null
    signal screensaverRequested()
    signal wakeRequested()
    signal brewSettingsRequested()
    signal completionSuspendRequested()
    signal completionFinishRequested()
    signal operationNavigationStarting()
    signal operationNavigated(Component component, string action)
    signal pageScalePersistenceRequested(string pageName, real scale)

    initialItem: idlePage
    focus: true
    layer.enabled: Settings.theme.activeShader === "crt"
    layer.effect: CrtShaderEffect {}
    pushEnter: Transition {}
    pushExit: Transition {}
    popEnter: Transition {}
    popExit: Transition {}
    replaceEnter: Transition {}
    replaceExit: Transition {}
    onCurrentItemChanged: {
        const coveredPage = _previousPage
        _previousPage = currentItem
        if (coveredPage && coveredPage !== currentItem)
            PopupCloser.closeAllUnder(coveredPage)
    }
    onBusyChanged: {
        if (!busy) navigationInProgress = false
    }
    Keys.onReleased: event => {
        if ((event.key === Qt.Key_Back || event.key === Qt.Key_Escape)
                && OperationNavigationPolicy.backAction(root.depth) === "pop") {
            event.accepted = true
            root.goBack()
        }
    }

    Loader {
        active: Settings.theme.backgroundSource === "shot"
        sourceComponent: Component { LastShotChartRenderer {} }
    }
    CurrentPageBackground { page: root.currentItem as T.Page }
    Binding { target: AppShell; property: "currentPage"; value: root.currentItem }
    Binding { target: Theme; property: "currentPageObjectName";
        value: root.currentItem ? root.currentItem.objectName : "" }
    StatusBar {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.statusBarHeight
        z: 600
        visible: !root.screensaverActive
    }
    // Keep the overlay in the window's sibling layer, above host overlays too.
    CrtOverlay {
        parent: root.parent
        anchors.fill: parent
        z: 950
    }
    WheelHandler {
        enabled: root.pageZoomEnabled && Qt.platform.os === "windows"
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => {
            const pageName = root.currentItem ? root.currentItem.objectName : ""
            if (!pageName) return
            const currentScale = Theme.pageScaleMultiplier
            const delta = event.angleDelta.y > 0 ? 0.05 : -0.05
            const newScale = Math.max(0.5, Math.min(2.0, currentScale + delta))
            if (newScale !== currentScale) {
                Theme.pageScaleMultiplier = newScale
                root.pageScalePersistenceRequested(pageName, newScale)
                WebDebugLogger.debug("Scale", "main", ["Ctrl+wheel zoom:", pageName, "scale =", newScale.toFixed(2)].map(String).join(" "))
            }
        }
    }

    readonly property Component idlePage: Component { IdlePage {} }
    readonly property Component espressoPage: Component { EspressoPage {} }
    readonly property Component steamPage: Component { SteamPage {} }
    readonly property Component hotWaterPage: Component { HotWaterPage {} }
    readonly property Component settingsPage: Component { SettingsPage {} }
    readonly property Component flushPage: Component { FlushPage {} }
    readonly property Component profileEditorPage: Component { ProfileEditorPage {} }
    readonly property Component dflowEditorPage: Component { DFlowEditorPage {} }
    readonly property Component pressureEditorPage: Component { PressureEditorPage {} }
    readonly property Component flowEditorPage: Component { FlowEditorPage {} }
    readonly property Component profileSelectorPage: Component { ProfileSelectorPage {} }
    readonly property Component descalingPage: Component { DescalingPage {} }
    readonly property Component transportPage: Component { TransportPage {} }
    readonly property Component sensorCalibrationPage: Component { SensorCalibrationPage {} }
    readonly property Component visualizerBrowserPage: Component { VisualizerBrowserPage {} }
    readonly property Component profileImportPage: Component { ProfileImportPage {} }
    readonly property Component beanInfoPage: Component { BeanInfoPage { retainSearchOnActivation: root.retainSearchOnActivation } }
    readonly property Component postShotReviewPage: Component { PostShotReviewPage {} }
    readonly property Component profileInfoPage: Component { ProfileInfoPage {} }
    readonly property Component screensaverPage: Component { ScreensaverPage {} }
    readonly property Component recipesPage: Component { RecipesPage { retainSearchOnActivation: root.retainSearchOnActivation } }
    readonly property Component recipeWizardPage: Component { RecipeWizardPage {} }
    readonly property Component shotHistoryPage: Component { ShotHistoryPage {} }
    readonly property Component shotComparisonPage: Component { ShotComparisonPage {} }
    readonly property Component equipmentPage: Component {
        EquipmentPage {
            backend: SharedEquipmentUiBackend {}
            canGoBack: root.depth > 1
        }
    }
    readonly property Component autoFavoritesPage: Component { AutoFavoritesPage {} }
    readonly property Component autoFavoriteInfoPage: Component { AutoFavoriteInfoPage {} }
    readonly property Component communityBrowserPage: Component { CommunityBrowserPage {} }
    readonly property Component visualizerMultiImportPage: Component { VisualizerMultiImportPage {} }
    readonly property Component flowCalibrationPage: Component { FlowCalibrationPage {} }
    readonly property Component aiSettingsPage: Component { AISettingsPage {} }
    readonly property Component stringBrowserPage: Component { StringBrowserPage {} }
    readonly property Component addLanguagePage: Component { AddLanguagePage {} }

    function startNavigation() {
        return OperationNavigationPolicy.beginNavigation(root, root, function(callback) {
            Qt.callLater(callback)
        })
    }

    function showHome() {
        const home = root.find(item => item.objectName === "idlePage")
        if (home)
            root.popToItem(home)
        else
            root.replace(null, idlePage)
    }

    function goToIdle() {
        if (!startNavigation()) return
        if (!root.currentItem || root.currentItem.objectName !== "idlePage")
            root.showHome()
    }

    function showOperationPage(component, pageObjectName) {
        operationNavigationStarting()
        const action = OperationNavigationPolicy.showOperationPage(
            root, component, pageObjectName,
            function(current) { PopupCloser.closeAllUnder(current) })
        operationNavigated(component, action)
        return action
    }

    function leaveOperationPage() {
        return OperationNavigationPolicy.leaveOperationPage(
            root, null, function() { root.showHome() })
    }

    function isCalibrationShot() {
        return root.find(item => item.objectName === "sensorCalibrationPage") !== null
    }

    function pushPage(component, properties) {
        if (pushPageHandler) return pushPageHandler(component, properties)
        return properties ? root.push(component, properties) : root.push(component)
    }

    function pushUnlessCurrent(component, pageObjectName, properties) {
        return OperationNavigationPolicy.pushUnlessCurrent(
            root.currentItem, component, pageObjectName, properties, root.pushPage)
    }

    function goToEspresso() {
        if (!startNavigation()) return
        showOperationPage(espressoPage, "espressoPage")
    }

    function goToSteam() {
        if (!startNavigation()) return
        showOperationPage(steamPage, "steamPage")
    }

    function goToHotWater() {
        if (!startNavigation()) return
        showOperationPage(hotWaterPage, "hotWaterPage")
    }

    function goToSettings(tabId) {
        if (!startNavigation()) return
        const settings = root.currentItem as SettingsPage
        OperationNavigationPolicy.showSettingsPage(
            settings, settingsPage, tabId, SettingsTabs.indexOf(tabId), root.pushPage,
            function(requestedTabId) { settings.showTab(requestedTabId) })
    }

    function goBack() {
        if (backHandler) return backHandler()
        if (!startNavigation()) return
        OperationNavigationPolicy.popIfPossible(root)
    }

    function goToProfileEditor(filename, requestedType) {
        if (!startNavigation()) return
        // Route to appropriate editor based on editor type
        var editorType = requestedType || ProfileService.currentProfileSummary.editorType
        const props = { editorEntryFilename: filename || "", editorEntryType: editorType }
        if (editorType === "pressure") {
            root.pushPage(pressureEditorPage, props)
        } else if (editorType === "flow") {
            root.pushPage(flowEditorPage, props)
        } else if (editorType === "dflow" || editorType === "aflow") {
            root.pushPage(dflowEditorPage, props)
        } else {
            root.pushPage(profileEditorPage, props)
        }
    }

    function goToNewProfileDraft(editorType, title) {
        if (!startNavigation()) return
        const requestedType = editorType || "advanced"
        const props = {
            editorEntryFilename: "",
            editorEntryType: requestedType,
            editorEntryTitle: title || TranslationManager.translate("profileEditor.title", "Profile Editor"),
            editorEntryIsNew: true
        }
        if (requestedType === "pressure") {
            root.pushPage(pressureEditorPage, props)
        } else if (requestedType === "flow") {
            root.pushPage(flowEditorPage, props)
        } else if (requestedType === "dflow" || requestedType === "aflow") {
            root.pushPage(dflowEditorPage, props)
        } else {
            root.pushPage(profileEditorPage, props)
        }
    }

    function goToDFlowEditor() {
        if (!startNavigation()) return
        // Explicitly go to D-Flow editor
        root.pushPage(dflowEditorPage)
    }

    function goToPressureEditor() {
        if (!startNavigation()) return
        root.pushPage(pressureEditorPage)
    }

    function goToFlowEditor() {
        if (!startNavigation()) return
        root.pushPage(flowEditorPage)
    }

    function goToAdvancedEditor() {
        if (!startNavigation()) return
        // Explicitly go to Advanced editor
        root.pushPage(profileEditorPage)
    }

    function goToProfileSelector() {
        if (!startNavigation()) return
        root.pushPage(profileSelectorPage)
    }

    function goToDescaling() {
        if (!startNavigation()) return
        showOperationPage(descalingPage, "descalingPage")
    }

    function goToTransport() {
        if (!startNavigation()) return
        showOperationPage(transportPage, "transportPage")
    }

    function goToSensorCalibration(sensor) {
        if (!startNavigation()) return
        root.pushPage(sensorCalibrationPage, { sensor: sensor })
    }

    function goToFlush() {
        if (!startNavigation()) return
        showOperationPage(flushPage, "flushPage")
    }

    function goToRecipes() {
        if (!startNavigation()) return
        pushUnlessCurrent(recipesPage, "recipesPage")
    }

    function goToRecipeWizard(mode, options) {
        if (!startNavigation()) return
        var props = options ? Object.assign({}, options) : ({})
        props.mode = mode
        root.pushPage(recipeWizardPage, props)
    }

    function goToShotHistory(filter) {
        if (!startNavigation()) return
        const history = root.currentItem as ShotHistoryPage
        OperationNavigationPolicy.showHistoryPage(
            history, shotHistoryPage, filter, root.pushPage,
            function(initialFilter) { history.applyInitialFilter(initialFilter) })
    }

    function goToShotDetail(shotId, shotIds) {
        if (!startNavigation()) return
        root.pushPage(postShotReviewPage, { editShotId: shotId, shotIds: shotIds || [], autoClose: false })
    }

    function goToShotComparison() {
        if (!startNavigation()) return
        root.pushPage(shotComparisonPage)
    }

    function goToPostShotReview(shotId, autoClose) {
        if (postShotReviewHandler) return postShotReviewHandler(shotId, autoClose)
        if (!startNavigation()) return
        root.pushPage(postShotReviewPage, { editShotId: shotId, autoClose: autoClose })
    }

    function goToProfileInfo(profileFilename, profileName) {
        if (!startNavigation()) return
        root.pushPage(profileInfoPage, { profileFilename: profileFilename, profileName: profileName })
    }

    function goToBeanInfo() {
        if (!startNavigation()) return
        pushUnlessCurrent(beanInfoPage, "bagInventoryPage")
    }

    function goToEquipment() {
        if (!startNavigation()) return
        pushUnlessCurrent(equipmentPage, "equipmentPage")
    }

    function goToAutoFavorites() {
        if (!startNavigation()) return
        pushUnlessCurrent(autoFavoritesPage, "autoFavoritesPage")
    }

    function goToAutoFavoriteInfo(options) {
        if (!startNavigation()) return
        root.pushPage(autoFavoriteInfoPage, options || ({}))
    }

    function goToCommunityBrowser() {
        if (!startNavigation()) return
        pushUnlessCurrent(communityBrowserPage, "communityBrowserPage")
    }

    function goToVisualizerMultiImport() {
        if (!startNavigation()) return
        root.pushPage(visualizerMultiImportPage)
    }

    function goToFlowCalibration() {
        if (!startNavigation()) return
        root.pushPage(flowCalibrationPage)
    }

    function goToAISettings() {
        if (!startNavigation()) return
        root.pushPage(aiSettingsPage)
    }

    function goToStringBrowser() {
        if (!startNavigation()) return
        root.pushPage(stringBrowserPage)
    }

    function goToAddLanguage() {
        if (!startNavigation()) return
        root.pushPage(addLanguagePage)
    }

    function goToVisualizerBrowser() {
        if (!startNavigation()) return
        root.pushPage(visualizerBrowserPage)
    }

    function goToProfileImport() {
        if (!startNavigation()) return
        root.pushPage(profileImportPage)
    }

    function goToShotMetadata(shotId) {
        if (!startNavigation()) return
        // The review takes the shot page's place, so Back returns to where the shot was
        // started from. Started from another review, it replaces that one too, so
        // back-to-back shots do not pile reviews up.
        const props = { editShotId: shotId || 0 }
        root.leaveOperationPage()
        if (root.currentItem && root.currentItem.objectName === "postShotReviewPage")
            root.replaceCurrentItem(postShotReviewPage, props)
        else
            root.pushPage(postShotReviewPage, props)
    }

    function routeEntry(component, properties) {
        const props = properties || ({})
        let route = ""
        let args = ({})
        if (component === idlePage) route = "home"
        else if (component === recipesPage) { route = "recipes"; args = props }
        else if (component === shotHistoryPage) { route = "history"; args = props }
        else if (component === beanInfoPage) { route = "beans"; args = props }
        else if (component === equipmentPage) route = "equipment"
        else if (component === espressoPage) route = "espresso"
        else if (component === steamPage) route = "steam"
        else if (component === hotWaterPage) route = "hotwater"
        else if (component === flushPage) route = "flush"
        else if (component === profileSelectorPage) { route = "profiles"; args = props }
        else if (component === autoFavoritesPage) route = "favorites"
        else if (component === autoFavoriteInfoPage) { route = "favoriteInfo"; args = props }
        else if (component === communityBrowserPage) { route = "community"; args = props }
        else if (component === screensaverPage) route = "screensaver"
        else if (component === descalingPage) route = "descaling"
        else if (component === transportPage) route = "transport"
        else if (component === flowCalibrationPage) route = "flowCalibration"
        else if (component === sensorCalibrationPage) { route = "sensorCalibration"; args = props }
        else if (component === settingsPage) { route = "settings"; args = props }
        else if (component === profileImportPage) route = "profileImport"
        else if (component === visualizerBrowserPage) route = "visualizer"
        else if (component === visualizerMultiImportPage) route = "visualizerMultiImport"
        else if (component === aiSettingsPage) route = "ai"
        else if (component === addLanguagePage) route = "addLanguage"
        else if (component === stringBrowserPage) { route = "stringBrowser"; args = props }
        else if (component === postShotReviewPage) {
            route = props.autoClose === true ? "postShotReview" : "shotDetail"
            args = props
        }
        else if (component === shotComparisonPage) {
            route = "shotComparison"
            args = {selectedShotIds: ComparisonService.selectedShotIds,
                    baseShotId: ComparisonService.baseShotId, windowStart: ComparisonService.windowStart}
        }
        else if (component === profileInfoPage) { route = "profileInfo"; args = props }
        else if (component === recipeWizardPage) { route = "recipeWizard"; args = props }
        else if (component === profileEditorPage || component === pressureEditorPage
                 || component === flowEditorPage || component === dflowEditorPage) {
            route = "profileEditor"
            args = {
                editorEntryFilename: props.editorEntryFilename || "",
                editorEntryType: props.editorEntryType || (component === pressureEditorPage ? "pressure"
                    : component === flowEditorPage ? "flow"
                    : component === dflowEditorPage ? "dflow" : "advanced"),
                editorEntryTitle: props.editorEntryTitle || "",
                editorEntryIsNew: props.editorEntryIsNew === true
            }
        }
        return route ? {route: route, args: args} : null
    }

    function componentForEntry(entry) {
        switch (entry.route) {
        case "home": return idlePage
        case "recipes": return recipesPage
        case "history": return shotHistoryPage
        case "beans": return beanInfoPage
        case "equipment": return equipmentPage
        case "espresso": return espressoPage
        case "steam": return steamPage
        case "hotwater": return hotWaterPage
        case "flush": return flushPage
        case "profiles": return profileSelectorPage
        case "favorites": return autoFavoritesPage
        case "favoriteInfo": return autoFavoriteInfoPage
        case "community": return communityBrowserPage
        case "screensaver": return screensaverPage
        case "descaling": return descalingPage
        case "transport": return transportPage
        case "flowCalibration": return flowCalibrationPage
        case "sensorCalibration": return sensorCalibrationPage
        case "settings": return settingsPage
        case "profileImport": return profileImportPage
        case "visualizer": return visualizerBrowserPage
        case "visualizerMultiImport": return visualizerMultiImportPage
        case "ai": return aiSettingsPage
        case "addLanguage": return addLanguagePage
        case "stringBrowser": return stringBrowserPage
        case "shotDetail":
        case "postShotReview": return postShotReviewPage
        case "shotComparison": return shotComparisonPage
        case "profileInfo": return profileInfoPage
        case "recipeWizard": return recipeWizardPage
        case "profileEditor":
            switch (entry.args.editorEntryType) {
            case "pressure": return pressureEditorPage
            case "flow": return flowEditorPage
            case "dflow":
            case "aflow": return dflowEditorPage
            default: return profileEditorPage
            }
        }
        return null
    }

    Connections {
        target: AppShell

        function onBackRequested() { root.goBack() }

        function onProfileEditorRequested() { root.goToProfileEditor() }
        function onProfileDraftEditorRequested(filename, editorType) { root.goToProfileEditor(filename, editorType) }
        function onNewProfileDraftRequested(editorType, title) { root.goToNewProfileDraft(editorType, title) }
        function onProfileSelectorRequested() { root.goToProfileSelector() }
        function onProfileImportRequested() { root.goToProfileImport() }
        function onVisualizerBrowserRequested() { root.goToVisualizerBrowser() }
        function onDescalingRequested() { root.goToDescaling() }
        function onTransportRequested() { root.goToTransport() }
        function onSensorCalibrationRequested(sensor) { root.goToSensorCalibration(sensor) }

        function onEspressoRequested() { root.goToEspresso() }
        function onSteamRequested() { root.goToSteam() }
        function onHotWaterRequested() { root.goToHotWater() }
        function onFlushRequested() { root.goToFlush() }
        function onSettingsRequested(tabId) { root.goToSettings(tabId) }
        function onDflowEditorRequested() { root.goToDFlowEditor() }
        function onRecipesRequested() { root.goToRecipes() }
        function onRecipeWizardRequested(mode, options) { root.goToRecipeWizard(mode, options) }
        function onShotHistoryRequested(filter) { root.goToShotHistory(filter) }
        function onShotDetailRequested(shotId, shotIds) { root.goToShotDetail(shotId, shotIds) }
        function onShotComparisonRequested() { root.goToShotComparison() }
        function onPostShotReviewRequested(shotId, autoClose) { root.goToPostShotReview(shotId, autoClose) }
        function onProfileInfoRequested(profileFilename, profileName) { root.goToProfileInfo(profileFilename, profileName) }
        function onBeanInfoRequested() { root.goToBeanInfo() }
        function onEquipmentRequested() { root.goToEquipment() }
        function onAutoFavoritesRequested() { root.goToAutoFavorites() }
        function onAutoFavoriteInfoRequested(options) { root.goToAutoFavoriteInfo(options) }
        function onCommunityBrowserRequested() { root.goToCommunityBrowser() }
        function onVisualizerMultiImportRequested() { root.goToVisualizerMultiImport() }
        function onFlowCalibrationRequested() { root.goToFlowCalibration() }
        function onAiSettingsRequested() { root.goToAISettings() }
        function onStringBrowserRequested() { root.goToStringBrowser() }
        function onAddLanguageRequested() { root.goToAddLanguage() }
        // Back where there is somewhere to go back to, home otherwise.
        function onDismissRequested() {
            if (root.backHandler || OperationNavigationPolicy.dismissAction(root.depth) === "pop")
                root.goBack()
            else
                root.goToIdle()
        }

        function onBrewSettingsRequested() { root.brewSettingsRequested() }
        function onScreensaverRequested() { root.screensaverRequested() }
        function onIdleFromScreensaverRequested() { root.wakeRequested() }
        function onCompletionSuspendRequested() { root.completionSuspendRequested() }
        function onCompletionFinishRequested() { root.completionFinishRequested() }
    }
}
