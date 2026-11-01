pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components/OperationNavigationPolicy.js" as OperationNavigationPolicy
import "../components/BrowserRouteHistory.js" as BrowserRouteHistory

ApplicationWindow {
    id: window
    visibility: Window.Maximized
    flags: Qt.Window | Qt.FramelessWindowHint
    width: 800
    height: 700
    title: "Decenza"
    font: Theme.bodyFont
    property int previousMachinePhase: -1
    property string initialRoute: ""
    property string decentMachineChoiceRequestId: ""
    property alias navigationInProgress: navigation.navigationInProgress
    property string browserHistorySessionId: ""
    property int browserHistoryIndex: 0
    property var browserRouteStack: []
    property var pendingBrowserPop: null
    property var pendingAppBackStack: null
    property bool applyingBrowserHistory: false
    property bool browserRouteHistoryReady: false
    readonly property bool stackedPagesHaveUnsavedChanges: BrowserRouteHistory.stackHasUnsavedChanges(pageStack)
    readonly property var currentNavigationContext: {
        const current = pageStack.currentItem
        return current && typeof current.navigationContext === "object" ? current.navigationContext : null
    }
    onCurrentNavigationContextChanged: Qt.callLater(syncCurrentNavigationContext)
    readonly property bool recipeServicesReady: RecipeService.ready && BagService.ready
        && ProfileService.ready && RecipeContext.ready && RecipeContext.historyReady
        && EquipmentService.ready && BeanUiService.ready && BeanSearchService.ready && SteamService.ready
        && MachineControlService.ready && HostStatusService.ready && LayoutService.ready
        && LibraryService.ready && ScreensaverService.ready
        && PreparationService.ready && MaintenanceService.ready && ShotUploadService.ready && LanguageService.ready
        && MqttService.ready && UpdateService.ready && AISettingsService.ready

    function historyStateJson(stack, index) {
        return BrowserRouteHistory.makeState(browserHistorySessionId, index, stack) || ""
    }

    function routeHash(stack) {
        return BrowserRouteHistory.routeHash(stack)
    }

    function entryForLocationHash(hash) {
        const parsed = BrowserRouteHistory.normalizeLocationHash(hash)
        const entry = BrowserRouteHistory.resolveLocationEntry(parsed, ProfileService.allProfilesList,
                                                                ProfileService.currentProfileSummary)
        if (parsed && !entry)
            actionError.show(TranslationManager.translate("navigation.profileUnavailable",
                "This profile is unavailable or its editor cannot be opened. Return to Profiles and select it again."))
        return entry
    }

    function propertiesForEntry(entry) {
        return BrowserRouteHistory.pageProperties(entry, session.hostId)
    }

    function restoreCurrentRouteContext() {
        if (browserRouteStack.length === 0) return
        const entry = browserRouteStack[browserRouteStack.length - 1]
        if (entry.route === "shotComparison" && Array.isArray(entry.args.selectedShotIds))
            ComparisonService.restoreSelection(entry.args.selectedShotIds, entry.args.baseShotId,
                                               entry.args.windowStart)
    }

    function syncCurrentNavigationContext() {
        if (applyingBrowserHistory || pendingBrowserPop || !browserRouteHistoryReady
                || browserRouteStack.length === 0) return
        const previous = browserRouteStack[browserRouteStack.length - 1]
        const entry = BrowserRouteHistory.entryWithPageContext(previous, pageStack.currentItem)
        if (entry && entry.route === "history")
            entry.args.selectionHostId = session.hostId || previous.args.selectionHostId || ""
        if (entry && JSON.stringify(entry) !== JSON.stringify(previous))
            updateCurrentTrackedRoute(entry)
    }

    function replaceBrowserState() {
        const json = historyStateJson(browserRouteStack, browserHistoryIndex)
        if (json.length > 0)
            BrowserHistoryController.replaceState(json, routeHash(browserRouteStack))
    }

    function pushTrackedPage(component, properties, replaceHistoryEntry) {
        syncCurrentNavigationContext()
        const entry = routeEntry(component, properties)
        if (!entry) {
            actionError.show(TranslationManager.translate(
                "navigation.history.unavailable", "This destination cannot be restored from browser history."))
            return false
        }
        if (browserRouteHistoryReady && !applyingBrowserHistory) {
            const nextStack = BrowserRouteHistory.normalizeStack(browserRouteStack.concat([entry]))
            const nextIndex = browserHistoryIndex + (replaceHistoryEntry ? 0 : 1)
            if (!nextStack || historyStateJson(nextStack, nextIndex).length === 0) {
                actionError.show(TranslationManager.translate(
                    "navigation.history.unavailable", "This destination cannot be restored from browser history."))
                return false
            }
            pageStack.push(component, properties || ({}))
            browserRouteStack = nextStack
            browserHistoryIndex = nextIndex
            const state = historyStateJson(browserRouteStack, browserHistoryIndex)
            const hash = routeHash(browserRouteStack)
            if (replaceHistoryEntry)
                BrowserHistoryController.replaceState(state, hash)
            else
                BrowserHistoryController.pushState(state, hash)
            return true
        }
        pageStack.push(component, properties || ({}))
        return true
    }

    function replaceTrackedPage(component, properties) {
        syncCurrentNavigationContext()
        const entry = routeEntry(component, properties)
        if (!entry) {
            actionError.show(TranslationManager.translate(
                "navigation.history.unavailable", "This destination cannot be restored from browser history."))
            return false
        }
        let nextStack = null
        if (browserRouteHistoryReady && !applyingBrowserHistory && browserRouteStack.length > 0) {
            const stack = browserRouteStack.slice(0, browserRouteStack.length - 1)
            stack.push(entry)
            nextStack = BrowserRouteHistory.normalizeStack(stack)
            if (!nextStack || historyStateJson(nextStack, browserHistoryIndex).length === 0) {
                actionError.show(TranslationManager.translate(
                    "navigation.history.unavailable", "This destination cannot be restored from browser history."))
                return false
            }
        }
        pageStack.replaceCurrentItem(component, properties || ({}))
        if (nextStack) {
            browserRouteStack = nextStack
            replaceBrowserState()
        }
        return true
    }

    function popTrackedPageLocally() {
        if (pageStack.depth <= 1) return false
        applyingBrowserHistory = true
        pageStack.pop()
        applyingBrowserHistory = false
        browserRouteStack = browserRouteStack.slice(0, browserRouteStack.length - 1)
        replaceBrowserState()
        return true
    }

    function syncTrackedPage(component, properties, action) {
        const entry = routeEntry(component, properties)
        if (!entry || applyingBrowserHistory || !browserRouteHistoryReady) return
        if (action === "push") {
            const stack = BrowserRouteHistory.normalizeStack(browserRouteStack.concat([entry]))
            if (!stack) return
            browserRouteStack = stack
            browserHistoryIndex += 1
            const json = historyStateJson(browserRouteStack, browserHistoryIndex)
            if (json.length > 0)
                BrowserHistoryController.pushState(json, routeHash(browserRouteStack))
        } else if (action === "replace") {
            const stack = browserRouteStack.slice(0, browserRouteStack.length - 1)
            stack.push(entry)
            const safeStack = BrowserRouteHistory.normalizeStack(stack)
            if (!safeStack) return
            browserRouteStack = safeStack
            replaceBrowserState()
        }
    }

    function updateCurrentTrackedRoute(entry) {
        if (!entry || !browserRouteHistoryReady || browserRouteStack.length === 0) return
        const stack = browserRouteStack.slice(0, browserRouteStack.length - 1)
        stack.push(entry)
        const safeStack = BrowserRouteHistory.normalizeStack(stack)
        if (!safeStack || historyStateJson(safeStack, browserHistoryIndex).length === 0) return
        browserRouteStack = safeStack
        replaceBrowserState()
    }

    // Creating Home starts service queries. Defer construction until readiness
    // notification delivery finishes so those queries cannot re-enter its binding.
    onRecipeServicesReadyChanged: Qt.callLater(function() {
        window.initializeNavigation()
        window.synchronizeSleepNavigation()
        window.synchronizeOperationNavigation()
    })
    function initializeNavigation() {
        if (recipeServicesReady && pageStack.depth === 0) {
            let browserState = BrowserRouteHistory.parseState(BrowserHistoryController.currentStateJson())
            const locationHash = BrowserHistoryController.currentLocationHash()
            const hashEntry = entryForLocationHash(locationHash)
            let initialEntry = null
            applyingBrowserHistory = true
            if (browserState) {
                if (!BrowserRouteHistory.stateMatchesLocationHash(browserState, locationHash)) {
                    // A stale marker must not override the actual URL on reload.
                    const stack = [{route: "home", args: ({})}]
                    if (hashEntry && hashEntry.route !== "home")
                        stack.push(hashEntry)
                    browserState = {
                        __decenzaHistory: 1,
                        sessionId: browserState.sessionId,
                        index: browserState.index,
                        stack: stack
                    }
                }
                browserHistorySessionId = browserState.sessionId
                browserHistoryIndex = browserState.index
                browserRouteStack = browserState.stack
            } else {
                browserHistorySessionId = BrowserHistoryController.newSessionId()
                browserHistoryIndex = 0
                initialEntry = hashEntry && hashEntry.route !== "home"
                    ? hashEntry : BrowserRouteHistory.normalizeEntry({route: initialRoute, args: ({})})
                browserRouteStack = [{route: "home", args: ({})}]
                if (initialEntry && initialEntry.route !== "home")
                    browserRouteStack.push(initialEntry)
            }
            for (let i = 0; i < browserRouteStack.length; ++i) {
                const entry = browserRouteStack[i]
                const component = componentForEntry(entry)
                if (component)
                    pageStack.push(component, propertiesForEntry(entry))
            }
            restoreCurrentRouteContext()
            applyingBrowserHistory = false
            browserRouteHistoryReady = pageStack.depth > 0
            if (browserRouteHistoryReady && browserState) {
                // Upgrade an old alias to the current record fragment without
                // discarding its richer marker-backed stack or query context.
                replaceBrowserState()
            } else if (browserRouteHistoryReady && !browserState) {
                if (browserRouteStack.length > 1) {
                    // Preserve a normal Home entry behind a legacy deep link.
                    browserRouteStack = [browserRouteStack[0]]
                    browserHistoryIndex = 0
                    replaceBrowserState()
                    browserRouteStack = [
                        {route: "home", args: ({})},
                        initialEntry
                    ]
                    browserHistoryIndex = 1
                    BrowserHistoryController.pushState(historyStateJson(browserRouteStack, browserHistoryIndex),
                                                       routeHash(browserRouteStack))
                } else {
                    replaceBrowserState()
                }
            }
            if (initialRoute === "brew") globalBrewDialog.open()
            initialRoute = ""
        }
    }

    function applyRouteStack(stack) {
        const safeStack = BrowserRouteHistory.normalizeStack(stack)
        if (!safeStack) return false
        const keep = BrowserRouteHistory.commonStackPrefixLength(browserRouteStack, safeStack)
        applyingBrowserHistory = true
        while (pageStack.depth > keep)
            pageStack.pop(StackView.Immediate)
        for (let i = keep; i < safeStack.length; ++i) {
            const entry = safeStack[i]
            const component = componentForEntry(entry)
            if (!component) {
                applyingBrowserHistory = false
                return false
            }
            pageStack.push(component, propertiesForEntry(entry), StackView.Immediate)
        }
        browserRouteStack = safeStack
        restoreCurrentRouteContext()
        applyingBrowserHistory = false
        return true
    }

    function handleBrowserPop(stateJson) {
        const target = BrowserRouteHistory.parseState(stateJson)
        // A restoration or guarded route change is already in flight. Ignore
        // additional popstate notifications instead of replacing its target.
        if (pendingBrowserPop) return
        if (!target || target.sessionId !== browserHistorySessionId
                || target.index === browserHistoryIndex || !browserRouteHistoryReady)
            return // Unknown/external entries remain browser-owned.
        const delta = browserHistoryIndex - target.index
        if (Math.abs(delta) > 64) {
            pendingBrowserPop = {
                stateJson: historyStateJson(browserRouteStack, browserHistoryIndex),
                targetJson: "",
                target: null,
                delta: delta,
                commonPrefixLength: browserRouteStack.length,
                leaveRequestIssued: false,
                removed: 0,
                rejectTraversal: true
            }
            BrowserHistoryController.restoreAfterPop(
                delta, pendingBrowserPop.stateJson, routeHash(browserRouteStack))
            return
        }
        const commonPrefixLength = BrowserRouteHistory.commonStackPrefixLength(browserRouteStack, target.stack)
        pendingBrowserPop = {
            stateJson: historyStateJson(browserRouteStack, browserHistoryIndex),
            targetJson: JSON.stringify(target),
            target: target,
            delta: delta,
            restoreDelta: delta,
            commonPrefixLength: commonPrefixLength,
            leaveRequestIssued: false,
            removed: 0
        }
        BrowserHistoryController.restoreAfterPop(delta, pendingBrowserPop.stateJson,
                                                  routeHash(browserRouteStack))
    }

    function handleLocationHash(hash, stateJson) {
        // popstate already handles marker-backed traversals. The companion
        // hashchange event for those traversals is ignored by this check.
        const marker = BrowserRouteHistory.parseState(stateJson)
        if (BrowserRouteHistory.stateMatchesLocationHash(marker, hash)) return
        if (!browserRouteHistoryReady || !browserHistorySessionId) return

        // A pop restoration or app navigation already owns the browser entry.
        if (pendingBrowserPop || navigationInProgress) return

        const entry = entryForLocationHash(hash)
        if (!entry) {
            canonicalizeUnknownLocationHash()
            return
        }
        const targetStack = [{route: "home", args: ({})}]
        if (entry.route !== "home") targetStack.push(entry)
        const targetIndex = browserHistoryIndex + 1
        const targetJson = historyStateJson(targetStack, targetIndex)
        if (!targetJson) {
            canonicalizeUnknownLocationHash()
            return
        }

        // The hash assignment already created this browser entry. Mark it
        // without pushing another one. Restore the source entry and let the
        // ordinary pop pipeline guard each page in the removed route suffix.
        BrowserHistoryController.replaceState(targetJson, routeHash(targetStack))
        handleBrowserPop(targetJson)
    }

    function canonicalizeUnknownLocationHash() {
        // A hash assignment adds a real browser entry. Give it the next
        // synthetic index even when the fragment is not a supported route.
        ++browserHistoryIndex
        replaceBrowserState()
    }

    function cancelPendingBrowserPop() {
        if (!pendingBrowserPop) return
        const pending = pendingBrowserPop
        pendingBrowserPop = null
        BrowserHistoryController.cancelRestoredPop()
        // Any pages above the page that declined were already allowed to
        // leave by their canonical guards. Keep the rejecting page instance
        // alive, and make the restored browser entry describe the remaining
        // stack rather than destroying that page to reconstruct the old one.
        if (pending.removed > 0)
            replaceBrowserState()
    }

    function acceptPendingBrowserBack() {
        const pending = pendingBrowserPop
        if (!pending) return false
        if (pageStack.depth <= pending.commonPrefixLength) {
            BrowserHistoryController.commitRestoredPop(
                -pending.delta, pending.targetJson, routeHash(pending.target.stack))
            return true
        }
        pending.leaveRequestIssued = false
        OperationNavigationPolicy.popIfPossible(pageStack)
        browserRouteStack = browserRouteStack.slice(0, browserRouteStack.length - 1)
        pending.removed += 1
        if (browserRouteStack.length <= pending.commonPrefixLength) {
            BrowserHistoryController.commitRestoredPop(
                -pending.delta, pending.targetJson, routeHash(pending.target.stack))
        } else {
            Qt.callLater(window.requestCurrentPageLeave)
        }
        return true
    }

    function requestCurrentPageLeave() {
        const current = pageStack.currentItem
        if (!current) return
        if (pendingBrowserPop) {
            if (pendingBrowserPop.leaveRequestIssued) return
            pendingBrowserPop.leaveRequestIssued = true
        }
        const profile = current as ProfileEditorPage
        const simple = current as SimpleProfileEditorPage
        const dflow = current as DFlowEditorPage
        const review = current as PostShotReviewPage
        const strings = current as StringBrowserPage
        const settings = current as SettingsPage
        const aiSettings = current as AISettingsPage
        const recipe = current as RecipeWizardPage
        const espresso = current as EspressoPage
        const steam = current as SteamPage
        const water = current as HotWaterPage
        const flush = current as FlushPage
        const descaling = current as DescalingPage
        if (profile) profile.handleBack()
        else if (simple) simple.handleBack()
        else if (dflow) dflow.handleBack()
        else if (review) review.handleBack()
        else if (strings) strings.handleBack()
        else if (settings) settings.handleBack()
        else if (aiSettings) aiSettings.handleBack()
        else if (recipe) recipe.requestExit()
        else if (espresso) espresso.stopAndGoBack()
        else if (steam) steam.leaveSteamFromPage()
        else if (water) water.requestBack()
        else if (flush) flush.requestBack()
        else if (descaling) descaling.navigateBack()
        else AppShell.backRequested()
    }

    function updateBeforeUnloadRequirement() {
        BrowserHistoryController.setBeforeUnloadRequired(
            stackedPagesHaveUnsavedChanges || AppShell.browserDialogDraftsDirty)
    }

    ThemeViewport { viewportWidth: window.width; viewportHeight: window.height }
    RemoteSessionBackend { id: session; service: HostSessionService }
    RemoteObjectConnection {
        id: serviceConnection
        backend: session
        services: [PreparationService, HostStatusService, MachineControlService, LiveShotGraph, SteamService, EquipmentService, RecipeService, BagService,
                   ProfileService, CalibrationService, FlowCalibrationService, SensorCalibrationService,
                   MaintenanceService, ShotUploadService, LanguageService, MqttService, UpdateService, AISettingsService, ShotServerSettingsService, ProfileImportService, VisualizerProfileImportService, BackupService, DataMigrationService, ImportService, DebugConversionService,
                   RecipeContext, BeanUiService, BeanSearchService,
                   HistoryService, SettingsPreferencesService, SettingsConnectionsService, SystemLogService, LayoutService, LibraryService, ScreensaverService, ThemeManagementService, GlobalSupportService, GHCSimulatorService, ShotReviewActions, ComparisonService, ConversationSession]
    }
    GHCSimulatorWindow {
        id: ghcSimulatorWindow
        controller: GHCSimulatorService
        translator: TranslationManager
        controllerAvailable: GHCSimulatorService.ready && GHCSimulatorService.available
            && SettingsPreferencesService.ready
        sharedHidden: SettingsPreferencesService.preferences["app.hideGhcSimulator"] === true
        useSharedHidePreference: false
    }
    Connections {
        target: GHCSimulatorService
        function onRaiseMainWindow() { window.raise() }
        function onRaiseGhcWindow() {
            if (ghcSimulatorWindow.visible) ghcSimulatorWindow.raise()
        }
    }
    onActiveChanged: {
        if (active && GHCSimulatorService.ready && GHCSimulatorService.available)
            GHCSimulatorService.mainWindowActivated()
    }
    Binding { target: ScaleDevice; property: "session"; value: session }
    Binding { target: BLEManager; property: "session"; value: session }
    Binding { target: UsbScaleManager; property: "session"; value: session }
    Binding { target: ProfileManager; property: "session"; value: session }
    Binding { target: DE1Device; property: "session"; value: session }
    Binding { target: MachineState; property: "session"; value: session }
    Binding { target: BelkaPortal; property: "session"; value: session }
    Binding { target: Settings; property: "session"; value: session }
    Binding { target: AccessibilityManager; property: "session"; value: session }
    Binding { target: TranslationManager; property: "languageService"; value: LanguageService }
    color: Theme.backgroundColor
    background: Rectangle { color: Theme.backgroundColor }

    Binding { target: MainController; property: "session"; value: session }
    palette.window: Theme.backgroundColor
    palette.windowText: Theme.textColor
    palette.base: Theme.insetBackgroundColor
    palette.text: Theme.textColor
    palette.button: Theme.surfaceColor
    palette.buttonText: Theme.textColor
    palette.highlight: Theme.accentColor
    palette.placeholderText: Theme.textSecondaryColor
    Component.onCompleted: session.connectToHost()

    // Older shared screens still address equipment through these native-shaped
    // facades. They point at the same QtRO service as the inventory page.
    Binding { target: MainController; property: "equipmentStorage"; value: EquipmentService }
    Binding { target: Settings.dye; property: "equipment"; value: EquipmentService }

    BrewDialog { id: globalBrewDialog }
    GlobalSupportDialogs { id: globalSupportDialogs }

    DecentMachineChooser {
        id: decentMachineDialog
        onSerialChosen: function(serial) {
            if (window.decentMachineChoiceRequestId.length)
                ShotUploadService.execute("chooseDecentMachine", { serial: serial })
            window.decentMachineChoiceRequestId = ""
        }
        onRejected: window.decentMachineChoiceRequestId = ""
    }
    Connections {
        target: ShotUploadService
        function onMachineChoiceNeeded(requestId, serials, labels) {
            window.decentMachineChoiceRequestId = requestId
            decentMachineDialog.openWith(serials, labels)
        }
        function onStateChanged() {
            if (!ShotUploadService.ready) {
                window.decentMachineChoiceRequestId = ""
                decentMachineDialog.close()
            }
        }
    }
    Connections {
        target: AppShell
        function onBrowserBackCancelled() { window.cancelPendingBrowserPop() }
    }
    Connections {
        target: BrowserHistoryController
        function onPopStateRequested(stateJson) { window.handleBrowserPop(stateJson) }
        function onHashChangeRequested(hash, stateJson) { window.handleLocationHash(hash, stateJson) }
        function onPositionRestored() {
            if (!window.pendingBrowserPop) {
                BrowserHistoryController.cancelRestoredPop()
                return
            }
            // A modal owns the first Back. Close it through its normal reject
            // path (or keep a non-dismissible confirmation open) and restore
            // the current browser entry before asking the page to leave.
            if (AppShell.consumeBrowserBack()) {
                window.cancelPendingBrowserPop()
                return
            }
            if (window.pendingBrowserPop.rejectTraversal) {
                window.cancelPendingBrowserPop()
                return
            }
            if (window.pageStack.depth > window.pendingBrowserPop.commonPrefixLength)
                window.requestCurrentPageLeave()
            else
                BrowserHistoryController.commitRestoredPop(
                    -window.pendingBrowserPop.delta, window.pendingBrowserPop.targetJson,
                    window.routeHash(window.pendingBrowserPop.target.stack))
        }
        function onPopStateCommitted(stateJson) {
            const target = BrowserRouteHistory.parseState(stateJson)
            const pending = window.pendingBrowserPop
            if (!target || !pending || target.sessionId !== window.browserHistorySessionId
                    || target.index !== pending.target.index) {
                window.cancelPendingBrowserPop()
                return
            }
            window.browserHistoryIndex = target.index
            window.pendingBrowserPop = null
            window.applyRouteStack(target.stack)
            window.replaceBrowserState()
        }
        function onAppBackCompleted(stateJson) {
            const target = BrowserRouteHistory.parseState(stateJson)
            const parentStack = window.pendingAppBackStack
            window.pendingAppBackStack = null
            if (!target || target.sessionId !== window.browserHistorySessionId
                    || target.index !== window.browserHistoryIndex - 1) {
                BrowserHistoryController.restoreAfterPop(
                    -1, window.historyStateJson(window.browserRouteStack, window.browserHistoryIndex),
                    window.routeHash(window.browserRouteStack))
                window.navigationInProgress = false
                return
            }
            window.browserHistoryIndex = target.index
            window.applyRouteStack(parentStack || target.stack)
            window.replaceBrowserState()
            window.navigationInProgress = false
        }
    }
    onStackedPagesHaveUnsavedChangesChanged:
        updateBeforeUnloadRequirement()
    Connections {
        target: AppShell
        function onBrowserDialogDraftsDirtyChanged() { window.updateBeforeUnloadRequirement() }
        function onScreensaverInhibitedChanged() { Qt.callLater(window.synchronizeSleepNavigation) }
    }
    Connections {
        target: HomePresetActions
        function onFailed(message) { actionError.show(message) }
    }
    StatusToast { id: actionError; error: true; durationMs: 5000 }
    property alias pageStack: navigation.stackView
    SharedAppNavigation {
        id: navigation
        anchors.fill: parent
        visible: session.connected && !session.stale && !serviceConnection.errorMessage
            && window.recipeServicesReady
        initialItem: null
        retainSearchOnActivation: true
        pageZoomEnabled: false
        pushPageHandler: window.pushTrackedPage
        backHandler: window.goBack
        postShotReviewHandler: window.openPostShotReview
        onWakeRequested: window.synchronizeSleepNavigation()
        onBrewSettingsRequested: globalBrewDialog.open()
        onBrewSettingRequested: title => globalBrewDialog.openSearchResult(title)
        onCompletionSuspendRequested: completionOverlay.suspend()
        onCompletionFinishRequested: completionOverlay.finish()
        onOperationNavigationStarting: window.syncCurrentNavigationContext()
        onOperationNavigated: (component, action, properties) => window.syncTrackedPage(component, properties, action)
        onCurrentItemChanged: {
            Qt.callLater(window.synchronizeSleepNavigation)
            if (AppShell.pendingBrewDialog) {
                AppShell.pendingBrewDialog = false
                Qt.callLater(function() { globalBrewDialog.open() })
            }
        }
        onBusyChanged: {
            if (!navigation.busy) {
                window.synchronizeSleepNavigation()
                window.synchronizeOperationNavigation()
            }
        }
    }

    function startNavigation() { return navigation.startNavigation() }
    function routeEntry(component, properties) {
        return BrowserRouteHistory.normalizeEntry(navigation.routeEntry(component, properties))
    }
    function componentForEntry(entry) { return navigation.componentForEntry(entry) }
    function showOperationPage(component, pageObjectName) {
        return navigation.showOperationPage(component, pageObjectName)
    }
    EspressoCompletionCoordinator {
        id: espressoCompletion
        isCalibrationShot: window.pageStack.find(item => item.objectName === "sensorCalibrationPage") !== null
        postShotReviewTimeout: Number(RecipeContext.preferences.postShotReviewTimeout)
        onLeaveOperationPageRequested: window.leaveOperationPage()
        onReviewRequested: shotId => window.openPostShotReview(shotId, true, true)
    }

    function openPostShotReview(shotId, autoClose, completedShot) {
        if (shotId <= 0 || !startNavigation()) return
        const props = {editShotId: shotId, autoClose: autoClose}
        let name = pageStack.currentItem ? pageStack.currentItem.objectName : ""
        if (completedShot && OperationNavigationPolicy.isOperationPage(name)
                && pageStack.depth > 1
                && pageStack.get(pageStack.depth - 2).objectName === "postShotReviewPage") {
            popTrackedPageLocally()
            name = pageStack.currentItem.objectName
        }
        if (completedShot && name === "postShotReviewPage") {
            replaceTrackedPage(navigation.postShotReviewPage, props)
            return
        }
        if (OperationNavigationPolicy.isOperationPage(name))
            replaceTrackedPage(navigation.postShotReviewPage, props)
        else pushTrackedPage(navigation.postShotReviewPage, props)
    }

    OperationCompletionOverlay {
        id: completionOverlay
        onFinished: window.leaveOperationPage()
    }

    function goBack() {
        if (AppShell.consumeBrowserBack())
            return
        if (pendingBrowserPop) {
            acceptPendingBrowserBack()
            return
        }
        if (!startNavigation()) return
        if (pageStack.depth <= 1)
            return
        if (browserHistoryIndex > 0) {
            // App Back follows the retained stack; browser toolbar traversal
            // keeps its chronological snapshots, including superseded reviews.
            pendingAppBackStack = browserRouteStack.slice(0, browserRouteStack.length - 1)
            BrowserHistoryController.goFromApp(-1, browserHistorySessionId, browserHistoryIndex - 1)
        } else
            popTrackedPageLocally()
    }

    function leaveOperationPage() {
        const name = pageStack.currentItem ? pageStack.currentItem.objectName : ""
        if (!OperationNavigationPolicy.isOperationPage(name)) return false
        goBack()
        return true
    }

    function synchronizeSleepNavigation() {
        if (!window.recipeServicesReady || !window.browserRouteHistoryReady
                || window.applyingBrowserHistory || pageStack.busy) return
        if (ScreensaverService.state.screensaverActive && !navigation.screensaverActive
                && !AppShell.screensaverInhibited) {
            if (navigation.currentItem) PopupCloser.closeAllUnder(navigation.currentItem)
            // Sleep is transient presentation, not another browser Back step.
            window.pushTrackedPage(navigation.screensaverPage, {}, true)
        } else if (!ScreensaverService.state.screensaverActive && navigation.screensaverActive) {
            window.popTrackedPageLocally()
        }
    }

    Connections {
        target: ScreensaverService
        function onStateChanged() { Qt.callLater(window.synchronizeSleepNavigation) }
    }
    Connections {
        target: ScreensaverService.state
        function onScreensaverActiveChanged() {
            window.synchronizeSleepNavigation()
            window.synchronizeOperationNavigation()
        }
    }

    function synchronizeOperationNavigation() {
        if (!SteamService.ready) {
            previousMachinePhase = -1
            return
        }
        if (ScreensaverService.ready && ScreensaverService.state.screensaverActive) {
            window.synchronizeSleepNavigation()
            return
        }
        if (pageStack.depth === 0) {
            // A phase snapshot may arrive before the deferred ready callback.
            // Install Home (and any requested initial route) first so operation
            // navigation never becomes the StackView's depth-one root.
            window.initializeNavigation()
        }
        if (pageStack.depth === 0 || pageStack.busy)
            return
        const phase = SteamService.state.phase
        const previous = window.previousMachinePhase
        window.previousMachinePhase = phase

        const operationPage = OperationNavigationPolicy.machineOperationPage(phase, MachineState.Phase,
                                                                              AppShell.drinkInCupView)
        if (previous !== phase && operationPage !== "") {
            completionOverlay.cancel()
            navigation.showMachineOperation(phase)
        } else if ((phase === MachineState.Phase.Idle || phase === MachineState.Phase.Ready)
                   && previous !== phase) {
            const name = pageStack.currentItem ? pageStack.currentItem.objectName : ""
            if (previous === MachineState.Phase.Steaming && name === "steamPage") {
                completionOverlay.showCompletion(
                    TranslationManager.translate("main.completion.steam", "Steam Complete"), "steam")
            } else if (previous === MachineState.Phase.HotWater
                       && (name === "hotWaterPage" || name === "espressoPage")
                       && !espressoCompletion.heldForDrink) {
                completionOverlay.showCompletion(
                    TranslationManager.translate("main.completion.hotwater", "Hot Water Complete"), "hotwater")
            } else if (previous === MachineState.Phase.Flushing && name === "flushPage"
                       && !AppShell.userExitedFlush) {
                completionOverlay.showCompletion(
                    TranslationManager.translate("main.completion.flush", "Flush Complete"), "flush")
            }
            AppShell.userExitedFlush = false
        }
    }
    Connections {
        target: SteamService
        function onReadyChanged() { window.synchronizeOperationNavigation() }
    }
    Connections {
        target: SteamService.state
        function onPhaseChanged() { window.synchronizeOperationNavigation() }
    }

    // Transport failures belong to the platform entry point. Page layouts are
    // instantiated from the same source files as the native application.
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 600)
        visible: !session.connected || session.stale || serviceConnection.errorMessage.length > 0
            || !window.recipeServicesReady || window.pageStack.depth === 0
        Label {
            Layout.fillWidth: true
            text: serviceConnection.errorMessage || session.errorMessage
                || (session.connected && (!window.recipeServicesReady || window.pageStack.depth === 0)
                    ? ProfileService.errorMessage || RecipeService.errorMessage || BagService.errorMessage
                        || TranslationManager.translate("common.loading", "Loading…")
                    : session.status)
            font: Theme.bodyFont
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Button {
            id: reconnectButton
            text: session.reloadRequired
                ? TranslationManager.translate("webclient.reload", "Reload interface")
                : TranslationManager.translate("webclient.reconnect", "Reconnect")
            enabled: !session.busy
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.focusable: true
            Accessible.onPressAction: reconnectButton.clicked()
            onClicked: {
                if (session.reloadRequired) session.reloadInterface()
                else session.connectToHost()
            }
        }
    }
}
