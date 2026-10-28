import QtQuick
import QtQuick.Controls
import QtTest
import DecenzaOperationCompletionTests 1.0
import "OperationNavigationPolicy.js" as NavigationPolicy
import "BrowserRouteHistory.js" as BrowserRouteHistory
import "EspressoCompletionPolicy.js" as EspressoPolicy

TestCase {
    id: testCase
    name: "OperationCompletionState"
    when: windowShown
    width: 640
    height: 480

    OperationCompletionState {
        id: state
        duration: 40
        onFinished: testCase.finishedCount++
    }

    property int finishedCount: 0
    property bool navigationInProgress: false
    property int testTransitionDuration: 0
    property int browserModalDismissCount: 0
    property list<int> qtShotIds: [12, 11, 10]
    readonly property bool retainedDraftsDirty: BrowserRouteHistory.stackHasUnsavedChanges(navigationStack)
    property var observedEntry: ({route: "home", args: {}})
    readonly property var observedContextEntry: BrowserRouteHistory.entryWithPageContext(observedEntry, navigationStack.currentItem)

    StackView {
        id: navigationStack
        width: 1
        height: 1
        initialItem: idlePageComponent
        pushEnter: Transition {
            enabled: testCase.testTransitionDuration > 0
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: testCase.testTransitionDuration
            }
        }
        pushExit: Transition {
            enabled: testCase.testTransitionDuration > 0
            NumberAnimation { property: "opacity"; to: 0; duration: testCase.testTransitionDuration }
        }
        popEnter: Transition {
            enabled: testCase.testTransitionDuration > 0
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: testCase.testTransitionDuration }
        }
        popExit: Transition {
            enabled: testCase.testTransitionDuration > 0
            NumberAnimation { property: "opacity"; to: 0; duration: testCase.testTransitionDuration }
        }
        replaceEnter: Transition {
            enabled: testCase.testTransitionDuration > 0
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: testCase.testTransitionDuration }
        }
        replaceExit: Transition {
            enabled: testCase.testTransitionDuration > 0
            NumberAnimation { property: "opacity"; to: 0; duration: testCase.testTransitionDuration }
        }
    }

    Component { id: idlePageComponent; Item { objectName: "idlePage" } }
    Component { id: steamPageComponent; Item { objectName: "steamPage"; property bool openedFromSettingsSearch: false } }
    Component { id: espressoPageComponent; Item { objectName: "espressoPage" } }

    Component {
        id: ordinaryPageComponent
        Item { property string localSelection: "kept" }
    }
    Component {
        id: settingsPageComponent
        Item {
            objectName: "settingsPage"
            property string requestedTabId: ""
            property string selectedTab: "themes"
            readonly property var navigationContext: ({requestedTabId: selectedTab})
            function showTab(tabId) { selectedTab = tabId }
        }
    }
    Component {
        id: historyPageComponent
        Item {
            objectName: "shotHistoryPage"
            property var initialFilter: ({profileName: "Existing profile"})
            property string searchText: "typed search"
            property var initialSearchText: null
            property var selectedShots: []
            property var initialSelectedShots: []
            property var initialViewport: null
            property var viewport: null
            readonly property var navigationContext: ({initialFilter: initialFilter, searchText: searchText,
                                                       selectedShots: selectedShots, viewport: viewport})
            function applyInitialFilter(filter) {
                initialFilter = filter
                searchText = ""
            }
        }
    }

    Component {
        id: draftPageComponent
        Item {
            property bool profileModified: false
            property bool browserHasUnsavedChanges: false
        }
    }
    Component {
        id: reviewPageComponent
        Item {
            objectName: "postShotReviewPage"
            property int editShotId: 0
            property var shotIds: []
            property bool autoClose: false
            readonly property var navigationContext: ({editShotId: editShotId, shotIds: shotIds, autoClose: autoClose})
        }
    }
    Component {
        id: recipesInventoryPageComponent
        Item {
            id: recipesInventoryFixture
            objectName: "recipesPage"
            property string searchQuery: ""
            property bool showArchived: false
            property bool retainSearchOnActivation: false
            readonly property var navigationContext: ({searchQuery: recipesInventoryFixture.searchQuery,
                                                        showArchived: recipesInventoryFixture.showArchived})
        }
    }
    Component {
        id: beansInventoryPageComponent
        Item {
            id: beansInventoryFixture
            objectName: "bagInventoryPage"
            property string searchQuery: ""
            property bool showFinished: false
            property bool retainSearchOnActivation: false
            readonly property var navigationContext: ({searchQuery: beansInventoryFixture.searchQuery,
                                                        showFinished: beansInventoryFixture.showFinished})
        }
    }
    Component {
        id: stringBrowserRouteComponent
        Item {
            objectName: "stringBrowserPage"
            property string initialSearchText: ""
            property int initialFilterMode: 0
            property var navigationContext: ({})
        }
    }
    Component {
        id: profileSelectorRouteComponent
        Item {
            objectName: "profileSelectorPage"
            property var initialPickerContext: null
            property var navigationContext: ({})
        }
    }

    // Both application roots clear the guard from StackView.onBusyChanged.
    Connections {
        target: navigationStack
        function onBusyChanged() {
            if (!navigationStack.busy)
                testCase.navigationInProgress = false
        }
    }

    function resetNavigationStack() {
        testTransitionDuration = 0
        navigationInProgress = false
        while (navigationStack.depth > 1)
            navigationStack.pop(StackView.Immediate)
        if (navigationStack.currentItem.objectName !== "idlePage")
            navigationStack.replace(idlePageComponent, {}, StackView.Immediate)
    }

    function beginTestNavigation(component) {
        if (!NavigationPolicy.beginNavigation(testCase, navigationStack, function(callback) {
                Qt.callLater(callback)
            }))
            return false
        navigationStack.push(component)
        return true
    }

    function test_operationNavigationPolicy() {
        const phases = {EspressoPreheating: 1, Preinfusion: 2, Pouring: 3, Ending: 4,
            Steaming: 5, HotWater: 6, Flushing: 7, Descaling: 8, Transport: 9, Ready: 10}
        for (const phase of [1, 2, 3, 4])
            compare(NavigationPolicy.machineOperationPage(phase, phases), "espressoPage")
        const pages = ["steamPage", "hotWaterPage", "flushPage", "descalingPage", "transportPage", ""]
        for (let i = 0; i < pages.length; ++i)
            compare(NavigationPolicy.machineOperationPage(i + 5, phases), pages[i])
        compare(NavigationPolicy.operationAction("idlePage", "steamPage"), "push")
        compare(NavigationPolicy.operationAction("steamPage", "steamPage"), "stay")
        compare(NavigationPolicy.operationAction("espressoPage", "steamPage"), "replace")
        compare(NavigationPolicy.operationAction("screensaverPage", "steamPage"), "replace")
        compare(NavigationPolicy.backAction(1), "stay")
        compare(NavigationPolicy.backAction(2), "pop")
        compare(NavigationPolicy.dismissAction(1), "home")
        compare(NavigationPolicy.dismissAction(2), "pop")
    }

    function test_ordinaryDestinationsRetainPageAndTrackedEntry() {
        for (const name of ["recipesPage", "bagInventoryPage", "equipmentPage",
                            "autoFavoritesPage"]) {
            resetNavigationStack()
            let browserEntries = 1
            const pushPage = function(component, properties) {
                ++browserEntries
                return navigationStack.push(component, properties)
            }
            const page = NavigationPolicy.pushUnlessCurrent(
                navigationStack.currentItem, ordinaryPageComponent, name, {objectName: name}, pushPage)
            compare(navigationStack.depth, 2)
            compare(browserEntries, 2)
            page.localSelection = "user selected"
            compare(NavigationPolicy.pushUnlessCurrent(
                navigationStack.currentItem, ordinaryPageComponent, name, undefined, pushPage), null)
            compare(navigationStack.currentItem, page)
            compare(navigationStack.depth, 2)
            compare(browserEntries, 2)
            compare(page.localSelection, "user selected")
        }
    }

    function test_settingsRequestsRetainOrUpdateCurrentTabWithoutPushing() {
        resetNavigationStack()
        const settings = navigationStack.push(settingsPageComponent)
        let pushCount = 0
        let routeUpdates = 0
        let trackedTab = "themes"
        const pushPage = function() { ++pushCount }
        const showTab = function(tabId) {
            settings.showTab(tabId)
            trackedTab = tabId
            ++routeUpdates
        }
        // The tab index comes from each shell's visible SettingsTabs registry.
        for (const tabId of [undefined, null, "", "unknown", "hidden-debug"])
            compare(NavigationPolicy.showSettingsPage(settings, settingsPageComponent,
                         tabId, -1, pushPage, showTab), "stay")
        compare(settings.selectedTab, "themes")
        compare(trackedTab, "themes")
        compare(routeUpdates, 0)
        compare(pushCount, 0)
        compare(NavigationPolicy.showSettingsPage(settings, settingsPageComponent,
                     "machine", 1, pushPage, showTab), "update")
        compare(settings.selectedTab, "machine")
        compare(trackedTab, "machine")
        compare(routeUpdates, 1)
        compare(navigationStack.depth, 2)
        compare(pushCount, 0)
    }

    function test_settingsPushSuppliesOnlyAnExplicitValidTab() {
        for (const request of [{tab: "machine", index: 1}, {tab: undefined, index: -1},
                               {tab: "unknown", index: -1}]) {
            resetNavigationStack()
            let propertiesPassed = "not called"
            const pushPage = function(component, properties) {
                propertiesPassed = properties
                return navigationStack.push(component, properties || ({}))
            }
            let showCalls = 0
            compare(NavigationPolicy.showSettingsPage(null, settingsPageComponent,
                         request.tab, request.index, pushPage, function() { ++showCalls }), "push")
            compare(navigationStack.depth, 2)
            compare(showCalls, 0)
            if (request.index >= 0) {
                compare(propertiesPassed.requestedTabId, "machine")
                compare(navigationStack.currentItem.requestedTabId, "machine")
            } else {
                compare(propertiesPassed, undefined)
                compare(navigationStack.currentItem.requestedTabId, "")
            }
        }
    }

    function test_historyNullRequestPreservesSearchAndEmptyRequestClearsInPlace() {
        resetNavigationStack()
        const history = navigationStack.push(historyPageComponent)
        let pushCount = 0
        let routeUpdates = 0
        const pushPage = function() { ++pushCount }
        const applyFilter = function(initialFilter) {
            history.applyInitialFilter(initialFilter)
            ++routeUpdates
        }
        for (const filter of [undefined, null])
            compare(NavigationPolicy.showHistoryPage(history, historyPageComponent,
                         filter, pushPage, applyFilter), "stay")
        compare(history.initialFilter.profileName, "Existing profile")
        compare(history.searchText, "typed search")
        compare(routeUpdates, 0)
        compare(NavigationPolicy.showHistoryPage(history, historyPageComponent,
                     {}, pushPage, applyFilter), "update")
        compare(history.initialFilter, null)
        compare(history.searchText, "")
        compare(routeUpdates, 1)
        const filter = {recipeId: 7, recipeName: "Recipe"}
        compare(NavigationPolicy.showHistoryPage(history, historyPageComponent,
                     {initialFilter: filter}, pushPage, applyFilter), "update")
        compare(history.initialFilter.recipeId, 7)
        compare(history.initialFilter.recipeName, "Recipe")
        compare(routeUpdates, 2)
        compare(navigationStack.depth, 2)
        compare(pushCount, 0)
    }

    function test_historyPushForwardsFilterProperties() {
        for (const filter of [undefined, null, {}, {initialFilter: {recipeId: 7}}]) {
            resetNavigationStack()
            let pushedProperties = null
            const pushPage = function(component, properties) {
                pushedProperties = properties
                return navigationStack.push(component, properties)
            }
            let updates = 0
            compare(NavigationPolicy.showHistoryPage(null, historyPageComponent, filter,
                         pushPage, function() { ++updates }), "push")
            compare(navigationStack.depth, 2)
            compare(updates, 0)
            if (filter && filter.initialFilter) {
                compare(pushedProperties.initialFilter.recipeId, 7)
                compare(navigationStack.currentItem.initialFilter.recipeId, 7)
            } else {
                compare(Object.keys(pushedProperties).length, 0)
            }
        }
    }

    function test_hiddenPageDraftsContinueToProtectReload() {
        resetNavigationStack()
        const draft = navigationStack.push(draftPageComponent)
        compare(retainedDraftsDirty, false)
        draft.profileModified = true
        compare(retainedDraftsDirty, true)
        navigationStack.push(settingsPageComponent)
        compare(retainedDraftsDirty, true)
        draft.profileModified = false
        compare(retainedDraftsDirty, false)
        draft.browserHasUnsavedChanges = true
        compare(retainedDraftsDirty, true)
        draft.browserHasUnsavedChanges = false
        compare(retainedDraftsDirty, false)
        draft.profileModified = true
        compare(retainedDraftsDirty, true)
        navigationStack.replace(historyPageComponent, {}, StackView.Immediate)
        compare(retainedDraftsDirty, true)
        navigationStack.pop(StackView.Immediate)
        compare(navigationStack.currentItem, draft)
        navigationStack.pop(StackView.Immediate)
        compare(retainedDraftsDirty, false)
    }

    function test_manualSettingsTabContextRestoresIntoANewPage() {
        resetNavigationStack()
        observedEntry = {route: "settings", args: {requestedTabId: "themes"}}
        const settings = navigationStack.push(settingsPageComponent)
        settings.showTab("machine")
        compare(observedContextEntry.args.requestedTabId, "machine")
        const serialized = BrowserRouteHistory.makeState("context-session", 2,
            [{route: "home"}, observedContextEntry])
        resetNavigationStack()
        const entry = BrowserRouteHistory.parseState(serialized).stack[1]
        const restored = navigationStack.push(settingsPageComponent, BrowserRouteHistory.pageProperties(entry))
        verify(restored !== settings)
        compare(restored.requestedTabId, "machine")
    }

    function test_manualHistoryQueryAndFilterContextRoundTripDeclaredInputs() {
        resetNavigationStack()
        observedEntry = {route: "history", args: {initialFilter: {recipeId: 7}}}
        const history = navigationStack.push(historyPageComponent)
        history.initialFilter = null
        history.searchText = "roaster:Example " + "coffee ".repeat(40)
        history.selectedShots = [12, 11]
        history.viewport = {loaded: 100, y: 840}
        compare(observedContextEntry.args.searchText, history.searchText)
        verify(!observedContextEntry.args.initialFilter)
        const contextEntry = observedContextEntry
        contextEntry.args.selectionHostId = "test-host"
        const typedState = BrowserRouteHistory.makeState("context-session", 2,
            [{route: "home"}, contextEntry])
        const typedEntry = BrowserRouteHistory.parseState(typedState).stack[1]
        compare(typedEntry.args.selectionHostId, "test-host")
        const typedProperties = BrowserRouteHistory.pageProperties(typedEntry, "test-host")
        compare(BrowserRouteHistory.pageProperties(typedEntry, "other-host").initialSelectedShots, [])
        compare(BrowserRouteHistory.pageProperties(typedEntry, "other-host").initialViewport, null)
        verify(!Object.prototype.hasOwnProperty.call(typedProperties, "searchText"))
        compare(typedProperties.initialSearchText, history.searchText)
        // Initial property assignment fails if a serializer-only field leaks through.
        const restored = navigationStack.push(historyPageComponent, typedProperties)
        compare(restored.initialSearchText, history.searchText)
        compare(restored.initialFilter, null)
        compare(restored.initialSelectedShots, [12, 11])
        compare(restored.initialViewport.loaded, 100)
        compare(restored.initialViewport.y, 840)

        navigationStack.pop(StackView.Immediate)
        history.applyInitialFilter({recipeId: 9, recipeName: "New recipe"})
        compare(observedContextEntry.args.initialFilter.recipeId, 9)
        history.applyInitialFilter(null)
        compare(observedContextEntry.args.searchText, "")
        verify(!observedContextEntry.args.initialFilter)
    }

    function test_steppedShotContextReplacesRecordWithoutLosingNeighborIds() {
        resetNavigationStack()
        observedEntry = {route: "shotDetail", args: {editShotId: 12, shotIds: qtShotIds}}
        const review = navigationStack.push(reviewPageComponent,
            BrowserRouteHistory.pageProperties(BrowserRouteHistory.normalizeEntry(observedEntry)))
        compare(review.shotIds.length, 3)
        review.editShotId = 11
        compare(observedContextEntry.args.shotIds.length, 3)
        const serialized = BrowserRouteHistory.makeState("context-session", 2,
            [{route: "home"}, observedContextEntry])
        resetNavigationStack()
        const restoredEntry = BrowserRouteHistory.parseState(serialized).stack[1]
        const restored = navigationStack.push(reviewPageComponent, BrowserRouteHistory.pageProperties(restoredEntry))
        compare(restored.editShotId, 11)
        compare(restored.shotIds.length, 3)
        compare(restored.shotIds[2], 10)
    }

    function test_inventorySearchAndShelvesSurviveContextRoundTrip() {
        for (const route of ["recipes", "beans"]) {
            resetNavigationStack()
            observedEntry = {route: route, args: {}}
            const component = route === "recipes" ? recipesInventoryPageComponent : beansInventoryPageComponent
            const page = navigationStack.push(component)
            compare(navigationStack.currentItem, page)
            compare(page.objectName, route === "recipes" ? "recipesPage" : "bagInventoryPage")
            compare(typeof page.searchQuery, "string")
            page.searchQuery = "Ethiopia washed"
            if (route === "recipes") page.showArchived = true
            else page.showFinished = true
            compare(typeof page.navigationContext, "object")
            compare(page.navigationContext.searchQuery, "Ethiopia washed")
            verify(observedContextEntry !== null, JSON.stringify({route: observedEntry,
                page: page.objectName, context: page.navigationContext}))
            const entry = BrowserRouteHistory.normalizeEntry(observedContextEntry)
            compare(entry.args.searchQuery, "Ethiopia washed")
            const properties = BrowserRouteHistory.pageProperties(entry)
            compare(properties.retainSearchOnActivation, true)
            const restored = navigationStack.push(component, properties)
            compare(restored.searchQuery, "Ethiopia washed")
            if (route === "recipes") compare(restored.showArchived, true)
            else compare(restored.showFinished, true)
        }
    }

    function test_comparisonContextIsBoundedAndKeepsBaseAndWindow() {
        const qtListEntry = BrowserRouteHistory.normalizeEntry({route: "shotComparison",
            args: {selectedShotIds: qtShotIds, baseShotId: 11, windowStart: 0}})
        verify(qtListEntry !== null)
        compare(qtListEntry.args.selectedShotIds.length, 3)
        compare(qtListEntry.args.selectedShotIds[1], 11)
        const entry = BrowserRouteHistory.normalizeEntry({route: "shotComparison",
            args: {selectedShotIds: [12, 11, 10, 9, 8], baseShotId: 10, windowStart: 2}})
        const serialized = BrowserRouteHistory.makeState("context-session", 3, [{route: "home"}, entry])
        const restored = BrowserRouteHistory.parseState(serialized).stack[1]
        compare(restored.args.selectedShotIds.length, 5)
        compare(restored.args.baseShotId, 10)
        compare(restored.args.windowStart, 2)
        compare(Object.keys(BrowserRouteHistory.pageProperties(restored)).length, 0)
        verify(BrowserRouteHistory.normalizeEntry({route: "shotComparison",
            args: {selectedShotIds: [12, 11], baseShotId: 99}}) === null)
        verify(BrowserRouteHistory.normalizeEntry({route: "shotComparison",
            args: {selectedShotIds: [12, 11], windowStart: 1}}) === null)
        verify(BrowserRouteHistory.normalizeEntry({route: "shotComparison",
            args: {selectedShotIds: [12, 12]}}) === null)
        verify(BrowserRouteHistory.normalizeEntry({route: "recipes",
            args: {searchQuery: "x".repeat(8193)}}) === null)
    }

    function test_longShotListsKeepOpenedRecordAndBoundedNeighbors() {
        const ids = []
        for (let i = 1200; i > 0; --i) ids.push(i)
        for (const selected of [1200, 600, 1]) {
            const entry = BrowserRouteHistory.normalizeEntry({route: "shotDetail",
                args: {editShotId: selected, shotIds: ids}})
            verify(entry !== null)
            compare(entry.args.editShotId, selected)
            compare(entry.args.shotIds.length, 500)
            verify(entry.args.shotIds.indexOf(selected) >= 0)
            const index = entry.args.shotIds.indexOf(selected)
            if (selected === 600) {
                compare(index, 249)
                compare(entry.args.shotIds[index - 1], 601)
                compare(entry.args.shotIds[index + 1], 599)
            }
            const state = BrowserRouteHistory.makeState("neighbor-session", 2, [{route: "home"}, entry])
            resetNavigationStack()
            const restored = navigationStack.push(reviewPageComponent,
                BrowserRouteHistory.pageProperties(BrowserRouteHistory.parseState(state).stack[1]))
            compare(restored.editShotId, selected)
            compare(restored.shotIds.length, 500)
            verify(restored.shotIds.indexOf(selected) >= 0)
        }
        const absent = BrowserRouteHistory.normalizeEntry({route: "shotDetail",
            args: {editShotId: 2000, shotIds: ids}})
        compare(absent.args.shotIds.length, 1)
        compare(absent.args.shotIds[0], 2000)
        const excessive = BrowserRouteHistory.normalizeEntry({route: "shotDetail",
            args: {editShotId: 2000, shotIds: {length: 100001}}})
        compare(excessive.args.shotIds.length, 1)
        compare(excessive.args.shotIds[0], 2000)
        verify(BrowserRouteHistory.normalizeEntry({route: "shotComparison",
            args: {selectedShotIds: ids}}) === null)
    }

    // The community library was removed; a history state saved by an older build
    // must not restore a route that no longer has a page.
    function test_retiredCommunityRouteIsNotRestored() {
        verify(BrowserRouteHistory.normalizeEntry({route: "community", args: {}}) === null)
        const savedByOlderBuild = JSON.stringify({__decenzaHistory: 1, sessionId: "retired-session",
            index: 1, stack: [{route: "home"}, {route: "community", args: {}}]})
        verify(BrowserRouteHistory.parseState(savedByOlderBuild) === null)
    }

    function test_stringBrowserSearchAndModeKeepDraftOutOfRoute() {
        resetNavigationStack()
        observedEntry = {route: "stringBrowser", args: {}}
        const page = navigationStack.push(stringBrowserRouteComponent)
        page.navigationContext = {searchText: "temperature " + "word ".repeat(40), filterMode: 2,
                                  draft: {text: "must remain local"}}
        const json = BrowserRouteHistory.makeState("strings-session", 2, [{route: "home"}, observedContextEntry])
        const entry = BrowserRouteHistory.parseState(json).stack[1]
        verify(entry.args.draft === undefined)
        const restored = navigationStack.push(stringBrowserRouteComponent, BrowserRouteHistory.pageProperties(entry))
        compare(restored.initialSearchText, page.navigationContext.searchText)
        compare(restored.initialFilterMode, 2)
        verify(BrowserRouteHistory.normalizeEntry({route: "stringBrowser", args: {filterMode: 4}}) === null)
    }

    function test_profilePickerRouteRestoresVisitWithoutChangingFreshOpenDefaults() {
        resetNavigationStack()
        const fresh = navigationStack.push(profileSelectorRouteComponent,
            BrowserRouteHistory.pageProperties(BrowserRouteHistory.normalizeEntry({route: "profiles"})))
        compare(fresh.initialPickerContext, null)
        observedEntry = {route: "profiles", args: {}}
        fresh.navigationContext = {pickerContext: {searchText: "bloom", favorites: false,
            sources: ["downloaded", "mine"], beverages: ["espresso"], sortMode: "alpha"}}
        const json = BrowserRouteHistory.makeState("profiles-session", 2, [{route: "home"}, observedContextEntry])
        const restored = navigationStack.push(profileSelectorRouteComponent,
            BrowserRouteHistory.pageProperties(BrowserRouteHistory.parseState(json).stack[1]))
        compare(restored.initialPickerContext.searchText, "bloom")
        compare(restored.initialPickerContext.favorites, false)
        compare(restored.initialPickerContext.sources.length, 2)
        compare(restored.initialPickerContext.sources[1], "mine")
        compare(restored.initialPickerContext.beverages[0], "espresso")
        compare(restored.initialPickerContext.sortMode, "alpha")
        verify(BrowserRouteHistory.normalizeEntry({route: "profiles", args: {pickerContext: {
            searchText: "", favorites: false, sources: ["host-path"], beverages: [], sortMode: "usage"}}}) === null)
    }

    function test_browserBackConsumesOnlyTopSharedDialog() {
        const underlying = ({name: "underlying"})
        const topConfirmation = ({name: "confirmation"})
        browserModalDismissCount = 0

        AppShell.registerBrowserDialog(underlying, true, true,
                                       function() { ++testCase.browserModalDismissCount })
        compare(AppShell.browserDialogDraftsDirty, true)
        AppShell.registerBrowserDialog(topConfirmation, false, false,
                                       function() { ++testCase.browserModalDismissCount })

        // A host confirmation consumes Back without being dismissed, and the
        // draft beneath it remains registered and protects beforeunload.
        compare(AppShell.consumeBrowserBack(), true)
        compare(browserModalDismissCount, 0)
        compare(AppShell.browserDialogDraftsDirty, true)

        AppShell.unregisterBrowserDialog(topConfirmation)
        compare(AppShell.consumeBrowserBack(), true)
        compare(browserModalDismissCount, 1)
        compare(AppShell.browserDialogDraftsDirty, true)

        AppShell.unregisterBrowserDialog(underlying)
        compare(AppShell.consumeBrowserBack(), false)
        compare(AppShell.browserDialogDraftsDirty, false)
    }

    function test_browserRouteStateIsBoundedAndRoundTripsSafeArguments() {
        const stack = [
            {route: "home", args: {}},
            {route: "settings", args: {requestedTabId: "themes", ignored: "/private/path"}},
            {route: "recipeWizard", args: {
                mode: "create",
                prefill: {name: "Clone", profileJson: '{"steps":[]}', ignoredSecret: "token"}
            }}
        ]
        const serialized = BrowserRouteHistory.makeState("session-12345678", 9, stack)
        compare(typeof serialized, "string")
        verify(serialized.length > 0)
        const parsed = BrowserRouteHistory.parseState(serialized)
        verify(parsed !== null)
        compare(parsed.index, 9)
        compare(parsed.stack.length, 3)
        compare(parsed.stack[1].args.requestedTabId, "themes")
        verify(!parsed.stack[1].args.ignored)
        compare(parsed.stack[2].args.prefill.profileJson, '{"steps":[]}')
        verify(!parsed.stack[2].args.prefill.ignoredSecret)
        compare(BrowserRouteHistory.commonStackPrefixLength(
                    parsed.stack, [parsed.stack[0], {route: "settings", args: {requestedTabId: "machine"}}]), 1)

        verify(BrowserRouteHistory.parseState(serialized + " ".repeat(BrowserRouteHistory.maximumStateChars)) === null)
        verify(BrowserRouteHistory.makeState("session-12345678", 10, [
            {route: "home"},
            {route: "recipeWizard", args: {prefill: {profileJson: "x".repeat(65537)}}}
        ]) === null)
        verify(BrowserRouteHistory.makeState("session-12345678", 9,
            [{route: "home"}, {route: "../../src/main.cpp"}]) === null)
        verify(BrowserRouteHistory.makeState("short", 9, [{route: "home"}]) === null)
        verify(BrowserRouteHistory.makeState("session-12345678", 9, []) === null)
    }

    function test_browserLocationHashUsesOnlyValidatedRouteAliases() {
        compare(BrowserRouteHistory.normalizeLocationHash("recipes").route, "recipes")
        compare(BrowserRouteHistory.normalizeLocationHash("#flowCalibration").route, "flowCalibration")
        compare(BrowserRouteHistory.normalizeLocationHash("%72ecipes").route, "recipes")
        compare(BrowserRouteHistory.normalizeLocationHash("").route, "home")
        verify(BrowserRouteHistory.normalizeLocationHash("../recipes") === null)
        verify(BrowserRouteHistory.normalizeLocationHash("recipes?filter=private") === null)
        verify(BrowserRouteHistory.normalizeLocationHash("%E0%A4%A") === null)

        const recipes = BrowserRouteHistory.makeState("session-12345678", 4, [
            {route: "home"}, {route: "recipes"}
        ])
        const stale = BrowserRouteHistory.makeState("session-12345678", 4, [
            {route: "home"}, {route: "history", args: {searchText: "old"}}
        ])
        verify(BrowserRouteHistory.stateMatchesLocationHash(recipes, "recipes"))
        verify(!BrowserRouteHistory.stateMatchesLocationHash(stale, "recipes"))
        verify(!BrowserRouteHistory.stateMatchesLocationHash(stale, "unsupported"))
    }

    function test_recordFragmentsReplayMinimalIdentityIntoFreshPages() {
        const shot = {route: "shotDetail", args: {editShotId: 12, shotIds: [12, 11], autoClose: false}}
        const stack = [{route: "home"}, {route: "history", args: {searchText: "kept query"}}, shot]
        compare(BrowserRouteHistory.routeHash(stack), "shotDetail/12")
        const state = BrowserRouteHistory.makeState("records-session", 3, stack)
        verify(BrowserRouteHistory.stateMatchesLocationHash(state, "shotDetail/12"))
        verify(BrowserRouteHistory.stateMatchesLocationHash(state, "shotDetail"))
        verify(!BrowserRouteHistory.stateMatchesLocationHash(state, "shotDetail/11"))
        const preserved = BrowserRouteHistory.parseState(state)
        compare(preserved.stack[1].args.searchText, "kept query")
        compare(preserved.stack[2].args.shotIds.length, 2)

        resetNavigationStack()
        const copied = BrowserRouteHistory.normalizeLocationHash("#shotDetail/11")
        const restored = navigationStack.push(reviewPageComponent, BrowserRouteHistory.pageProperties(copied))
        compare(restored.editShotId, 11)
        compare(restored.shotIds.length, 0)
        compare(restored.autoClose, false)
        const reviewLink = BrowserRouteHistory.entryLocationHash({route: "postShotReview",
            args: {editShotId: 12, autoClose: true, shotIds: [12, 11]}})
        compare(reviewLink, "postShotReview/12")
        compare(BrowserRouteHistory.normalizeLocationHash(reviewLink).args.autoClose, false)

        const filename = "D-Flow / private" // A path is never put in a fragment.
        compare(BrowserRouteHistory.entryLocationHash({route: "profileInfo", args: {profileFilename: filename}}), "profileInfo")
        const publicName = "Café 100% #1.json"
        const profileLink = BrowserRouteHistory.entryLocationHash({route: "profileInfo",
            args: {profileFilename: publicName, profileName: "display-only title"}})
        compare(profileLink, "profileInfo/" + encodeURIComponent(publicName))
        const profile = BrowserRouteHistory.normalizeLocationHash(profileLink)
        compare(profile.args.profileFilename, publicName)
        compare(profile.args.profileName, "")
        compare(BrowserRouteHistory.pageProperties(profile).profileFilename, publicName)
        const recipeLink = BrowserRouteHistory.entryLocationHash({route: "recipeWizard", args: {
            mode: "edit", editRecipeId: 7, prefill: {profileJson: "private draft"}}})
        compare(recipeLink, "recipeWizard/7")
        const recipe = BrowserRouteHistory.normalizeLocationHash(recipeLink)
        compare(recipe.args.mode, "edit")
        compare(recipe.args.editRecipeId, 7)
        verify(recipe.args.prefill === undefined)
        compare(BrowserRouteHistory.entryLocationHash({route: "recipeWizard", args: {
            mode: "create", promoteShotId: 12, prefill: {profileJson: "private draft"}}}), "recipeWizard")
        compare(BrowserRouteHistory.normalizeLocationHash("sensorCalibration/4").args.sensor, 4)
        compare(BrowserRouteHistory.entryLocationHash({route: "sensorCalibration", args: {sensor: 4}}), "sensorCalibration/4")
        const favoriteLink = BrowserRouteHistory.entryLocationHash({route: "favoriteInfo", args: {
            shotId: 12, groupBy: "bean_profile_grinder_weight", beanBrand: "private bean"}})
        compare(favoriteLink, "favoriteInfo/bean_profile_grinder_weight/12")
        const favorite = BrowserRouteHistory.normalizeLocationHash(favoriteLink)
        compare(favorite.args.shotId, 12)
        compare(favorite.args.groupBy, "bean_profile_grinder_weight")
        compare(BrowserRouteHistory.pageProperties(favorite).resolveFromShot, true)
        verify(favorite.args.beanBrand === undefined)
    }

    function test_savedProfileLinkUsesOnlyCanonicalCatalogEditor() {
        const name = "D-Flow filename.json"
        const link = BrowserRouteHistory.entryLocationHash({route: "profileEditor", args: {
            editorEntryFilename: name, editorEntryType: "advanced", editorEntryTitle: "private title"}})
        compare(link, "profileEditor/" + encodeURIComponent(name))
        const parsed = BrowserRouteHistory.normalizeLocationHash(link)
        const rows = [{name: name, title: "A-Flow content", editorType: "aflow"},
                      {name: "Other.json", editorType: "advanced"}]
        const canonical = BrowserRouteHistory.resolveLocationEntry(parsed, rows)
        compare(canonical.args.editorEntryFilename, name)
        compare(canonical.args.editorEntryType, "aflow")
        compare(canonical.args.editorEntryTitle, "")
        compare(BrowserRouteHistory.pageProperties(canonical).editorEntryType, "aflow")
        verify(BrowserRouteHistory.resolveLocationEntry(parsed, []) === null)
        verify(BrowserRouteHistory.resolveLocationEntry(parsed, [{name: name, editorType: "unknown"}]) === null)
        verify(BrowserRouteHistory.normalizeLocationHash("profileEditor/advanced/" + encodeURIComponent(name)) === null)
        compare(BrowserRouteHistory.entryLocationHash({route: "profileEditor", args: {
            editorEntryFilename: name, editorEntryIsNew: true, editorEntryTitle: "unsaved"}}), "profileEditor")
        const current = BrowserRouteHistory.resolveLocationEntry(
            BrowserRouteHistory.normalizeLocationHash("profileEditor"), rows, {editorType: "dflow"})
        compare(current.args.editorEntryType, "dflow")
    }

    function test_recordFragmentBoundsRejectPathsAndUnrecognizedMetadata() {
        for (const hash of ["shotDetail/0", "shotDetail/-1", "shotDetail/01", "shotDetail/1.5",
                           "shotDetail/2147483648", "shotDetail/9007199254740992", "shotDetail/1/extra", "shotDetail/1?token=secret",
                           "profileInfo/..", "profileInfo/%2Fprivate%2Ffile.json", "profileInfo/a%5Cb.json",
                           "profileInfo/C%3Afile.json", "profileInfo/name%00.json", "profileInfo/%E0%A4%A",
                           "profileInfo/" + "x".repeat(201), "recipeWizard/0", "sensorCalibration/5",
                           "favoriteInfo/12/bean", "favoriteInfo/unknown/12", "favoriteInfo/bean/0",
                           "favoriteInfo/bean/01", "recipes/12", "x".repeat(2049)])
            verify(BrowserRouteHistory.normalizeLocationHash(hash) === null, hash)
        const unicodeName = "☕".repeat(100) + ".json"
        const fragment = BrowserRouteHistory.entryLocationHash({route: "profileInfo", args: {profileFilename: unicodeName}})
        verify(fragment.length < BrowserRouteHistory.maximumFragmentChars)
        compare(BrowserRouteHistory.normalizeLocationHash(fragment).args.profileFilename, unicodeName)
    }

    function test_sharedStackMutationsKeepOperationAndRootPolicies() {
        resetNavigationStack()
        let closedPage = null

        compare(NavigationPolicy.showOperationPage(
                    navigationStack, steamPageComponent, "steamPage",
                    function(page) { closedPage = page.objectName }), "push")
        compare(navigationStack.depth, 2)
        compare(closedPage, "idlePage")

        compare(NavigationPolicy.showOperationPage(
                    navigationStack, espressoPageComponent, "steamPage",
                    function(page) { closedPage = page.objectName }), "stay")
        compare(navigationStack.depth, 2)

        closedPage = "unchanged"
        compare(NavigationPolicy.showOperationPage(
                    navigationStack, espressoPageComponent, "espressoPage",
                    function(page) { closedPage = page.objectName }), "replace")
        compare(navigationStack.currentItem.objectName, "espressoPage")
        compare(closedPage, "unchanged")

        let homeCount = 0
        compare(NavigationPolicy.leaveOperationPage(
                    navigationStack, null, function() { ++homeCount }), true)
        tryCompare(navigationStack, "depth", 1)
        compare(homeCount, 0)

        navigationStack.replace(steamPageComponent, {}, StackView.Immediate)
        compare(NavigationPolicy.leaveOperationPage(
                    navigationStack, null, function() { ++homeCount }), true)
        compare(homeCount, 1)

        // The browser stack is rooted at Home and has no native show-home
        // callback; preserve that no-op if an operation ever reaches depth one.
        compare(NavigationPolicy.leaveOperationPage(
                    navigationStack, function() { return true }, null), false)
        compare(homeCount, 1)
    }

    function test_operationSearchPassesInitialProperties() {
        resetNavigationStack()
        compare(NavigationPolicy.showOperationPage(navigationStack, steamPageComponent,
            "steamPage", function(page) {}, {openedFromSettingsSearch: true}), "push")
        compare(navigationStack.currentItem.openedFromSettingsSearch, true)
    }

    function test_operationSearchHistoryRestoresWithoutPreparation() {
        for (const route of ["steam", "hotwater", "flush"]) {
            const entry = {route: route, args: {openedFromSettingsSearch: true}}
            const json = BrowserRouteHistory.makeState("test-session", 1,
                [{route: "home", args: {}}, entry])
            const restored = BrowserRouteHistory.parseState(json).stack[1]
            compare(restored.args.openedFromSettingsSearch, true)
            compare(BrowserRouteHistory.pageProperties(restored).openedFromSettingsSearch, true)
            compare(BrowserRouteHistory.entryLocationHash(restored), route)
            verify(BrowserRouteHistory.normalizeEntry({route: route,
                args: {openedFromSettingsSearch: "true"}}) === null)
        }
    }

    function test_sharedNavigationGuardImmediateTransitionRejectsSameTurnReentry() {
        resetNavigationStack()
        verify(beginTestNavigation(steamPageComponent))
        verify(!beginTestNavigation(espressoPageComponent))
        tryCompare(navigationStack, "depth", 2)
        tryCompare(testCase, "navigationInProgress", false)
    }

    function test_sharedNavigationGuardAnimatedTransitionClearsOnBusyChanged() {
        resetNavigationStack()
        testTransitionDuration = 80
        verify(beginTestNavigation(steamPageComponent))
        verify(navigationStack.busy)
        verify(navigationInProgress)
        verify(!beginTestNavigation(espressoPageComponent))
        tryVerify(function() { return !navigationStack.busy }, 2000)
        tryCompare(testCase, "navigationInProgress", false)
    }

    function test_activeShotCanHydrateOnlyAfterFlowBegins() {
        const phases = {
            EspressoPreheating: "preheat",
            Preinfusion: "preinfusion",
            Pouring: "pouring",
            Ending: "ending"
        }

        verify(EspressoPolicy.canHydrate(true, phases.Preinfusion, 41, phases))
        verify(EspressoPolicy.canHydrate(true, phases.Pouring, 41, phases))
        verify(!EspressoPolicy.canHydrate(false, phases.Pouring, 41, phases))
        verify(!EspressoPolicy.canHydrate(true, phases.EspressoPreheating, 41, phases))
        verify(!EspressoPolicy.canHydrate(true, phases.Ending, 41, phases))
        verify(!EspressoPolicy.canHydrate(true, phases.Pouring, 0, phases))
        verify(!EspressoPolicy.canHydrate(true, phases.Pouring, 1.5, phases))

        // The explicit start event is emitted only after flow starts, but its
        // phase predicate accepts any actual espresso operation phase.
        verify(EspressoPolicy.isOperationPhase(phases.EspressoPreheating, phases))
        verify(EspressoPolicy.isOperationPhase(phases.Preinfusion, phases))
        verify(EspressoPolicy.isOperationPhase(phases.Pouring, phases))
        verify(!EspressoPolicy.isOperationPhase(phases.Ending, phases))
    }

    function test_lateReviewResultCannotAttachToNextShot() {
        const oldShotToken = 41
        const nextShotToken = 42

        verify(EspressoPolicy.isCurrentReviewResult(123, oldShotToken, oldShotToken))
        verify(!EspressoPolicy.isCurrentReviewResult(123, oldShotToken, nextShotToken))
        verify(!EspressoPolicy.isCurrentReviewResult(0, oldShotToken, nextShotToken))
        verify(EspressoPolicy.isCurrentReviewResult(0, nextShotToken, nextShotToken))
        verify(!EspressoPolicy.isCurrentReviewResult(-1, nextShotToken, nextShotToken))
    }

    function init() {
        state.cancel()
        finishedCount = 0
    }

    function test_suspendBeforeShowKeepsCompletionHeld() {
        state.suspend()
        state.showCompletion("Steam Complete", "steam")

        wait(80)
        compare(state.pending, true)
        compare(state.visible, false)
        compare(finishedCount, 0)

        state.finish()
        compare(finishedCount, 1)
        compare(state.pending, false)
    }

    function test_showBeforeSuspendStopsCompletionTimer() {
        state.showCompletion("Steam Complete", "steam")
        compare(state.visible, true)

        state.suspend()
        wait(80)
        compare(state.visible, false)
        compare(finishedCount, 0)

        state.finish()
        compare(finishedCount, 1)
    }

    function test_cancelResetsSuspensionForNextOperation() {
        state.suspend()
        state.showCompletion("Old completion", "steam")
        state.cancel()
        state.showCompletion("New completion", "steam")

        compare(state.visible, true)
        tryCompare(testCase, "finishedCount", 1, 500)
        compare(state.pending, false)
    }
}
