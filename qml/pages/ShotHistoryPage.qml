pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: shotHistoryPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("shothistory.title", "Shot History")

    objectName: "shotHistoryPage"
    background: ThemedPageBackground {}

    // Tap outside to dismiss keyboard
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            if (searchField.activeFocus) {
                searchField.focus = false
                Keyboard.hide()
            }
        }
    }

    property var selectedShots: []
    property int currentOffset: 0
    property int pageSize: 50
    property bool hasMoreShots: true
    property bool isLoadingMore: false
    property int filteredTotalCount: 0
    property bool _waitingForShotLoad: false

    // First activation does a full reset-to-top load. Subsequent activations
    // (returning from a pushed child page like Shot Detail) re-query the same
    // window and restore the scroll position instead of jumping to the top.
    property bool _initialized: false
    property real _pendingRestoreContentY: -1

    // Wait for async loadShotWithMetadata to complete before popping
    Connections {
        target: MainController
        enabled: shotHistoryPage._waitingForShotLoad
        function onShotMetadataLoaded(shotId, success) {
            shotHistoryPage._waitingForShotLoad = false
            if (success)
                AppShell.backRequested()
        }
    }

    // External filter passed from other pages (e.g., AutoFavoritesPage "Show" button)
    property var initialFilter: null
    property bool _populatingSearch: false

    // Sort settings
    property string sortField: Settings.network.shotHistorySortField
    property string sortDirection: Settings.network.shotHistorySortDirection

    readonly property var sortFieldLabels: ({
        "timestamp": TranslationManager.translate("shothistory.sort.date", "Date"),
        "profile_name": TranslationManager.translate("shothistory.sort.profile", "Profile"),
        "bean_brand": TranslationManager.translate("shothistory.sort.roaster", "Roaster"),
        "bean_type": TranslationManager.translate("shothistory.sort.coffee", "Coffee"),
        "enjoyment": TranslationManager.translate("shothistory.sort.rating", "Rating"),
        "ratio": TranslationManager.translate("shothistory.sort.ratio", "Ratio"),
        "duration_seconds": TranslationManager.translate("shothistory.sort.duration", "Duration"),
        "dose_weight": TranslationManager.translate("shothistory.sort.dose", "Dose"),
        "final_weight": TranslationManager.translate("shothistory.sort.yield", "Yield")
    })
    readonly property var sortFieldKeys: [
        "timestamp", "profile_name", "bean_brand", "bean_type",
        "enjoyment", "ratio", "duration_seconds", "dose_weight", "final_weight"
    ]
    readonly property var defaultSortDirections: ({
        "timestamp": "DESC", "profile_name": "ASC", "bean_brand": "ASC",
        "bean_type": "ASC", "enjoyment": "DESC", "ratio": "DESC",
        "duration_seconds": "ASC", "dose_weight": "DESC", "final_weight": "DESC"
    })

    StackView.onActivated: {
        if (!_initialized) {
            _initialized = true
            if (initialFilter) {
                // Populate search field with filter terms so user can edit/save
                var parts = []
                if (initialFilter.beanBrand) parts.push(initialFilter.beanBrand)
                if (initialFilter.beanType) parts.push(initialFilter.beanType)
                if (initialFilter.profileName) parts.push(initialFilter.profileName)
                if (initialFilter.grinderBrand) parts.push(initialFilter.grinderBrand)
                if (initialFilter.grinderModel) parts.push(initialFilter.grinderModel)
                if (initialFilter.grinderSetting) parts.push(initialFilter.grinderSetting)
                _populatingSearch = true
                searchField.text = parts.join(" ")
                searchField.lastTriggeredText = searchField.text.trim()
                _populatingSearch = false
            }
            loadShots()
        } else {
            // Returning from a pushed child page (Shot Detail, comparison,
            // post-shot review): re-query the same window so edited ratings or
            // badges stay fresh, then restore where the user was scrolled.
            reloadPreservingScroll()
        }
    }

    function loadShots() {
        loadMoreTimer.stop()
        isLoadingMore = false
        currentOffset = 0
        hasMoreShots = true
        // Explicit reset-to-top entry point (filter/sort/search change, clear
        // filter, batch delete): discard any pending scroll-restore from a
        // superseded reloadPreservingScroll() so it can't be applied to this
        // refreshed result set once its own in-flight request is dropped.
        _pendingRestoreContentY = -1
        shotListView.contentY = 0
        var filter = buildFilter()
        MainController.shotHistory.requestShotsFiltered(filter, 0, pageSize)
    }

    // Refresh the list without losing the user's place. Re-queries every row
    // that was already loaded (not just the first page) so the model isn't
    // truncated, then restores contentY once the rows are laid out.
    function reloadPreservingScroll() {
        loadMoreTimer.stop()
        isLoadingMore = false
        currentOffset = 0
        hasMoreShots = true
        _pendingRestoreContentY = shotListView.contentY
        var windowSize = Math.max(shotListModel.count, pageSize)
        var filter = buildFilter()
        MainController.shotHistory.requestShotsFiltered(filter, 0, windowSize)
    }

    function loadMoreShots() {
        if (isLoadingMore || !hasMoreShots) return
        isLoadingMore = true
        var filter = buildFilter()
        MainController.shotHistory.requestShotsFiltered(filter, currentOffset, pageSize)
    }

    // Reload after async batch delete completes
    Connections {
        target: MainController.shotHistory
        function onShotsDeleted() {
            shotHistoryPage.loadShots()
        }
    }

    // Handle async results from requestShotsFiltered()
    Connections {
        target: MainController.shotHistory
        function onShotsFilteredReady(results, isAppend, totalCount) {
            if (isAppend) {
                // loadMoreShots result
                for (var i = 0; i < results.length; i++) {
                    shotListModel.append(results[i])
                }
                shotHistoryPage.currentOffset += results.length
                shotHistoryPage.hasMoreShots = results.length >= shotHistoryPage.pageSize
                shotHistoryPage.isLoadingMore = false
            } else {
                // Full refresh (loadShots or reloadPreservingScroll)
                var j
                for (j = 0; j < results.length; j++) {
                    if (j < shotListModel.count) {
                        shotListModel.set(j, results[j])
                    } else {
                        shotListModel.append(results[j])
                    }
                }
                while (shotListModel.count > results.length) {
                    shotListModel.remove(shotListModel.count - 1)
                }
                shotHistoryPage.currentOffset = results.length
                shotHistoryPage.hasMoreShots = results.length >= shotHistoryPage.pageSize
                if (shotHistoryPage._pendingRestoreContentY >= 0) {
                    var targetY = shotHistoryPage._pendingRestoreContentY
                    shotHistoryPage._pendingRestoreContentY = -1
                    // Defer until the ListView has re-laid-out the refreshed
                    // model so contentHeight is final before we clamp/restore.
                    Qt.callLater(function() {
                        var maxY = Math.max(0, shotListView.contentHeight - shotListView.height)
                        shotListView.contentY = Math.min(targetY, maxY)
                    })
                }
            }
            shotHistoryPage.filteredTotalCount = totalCount
        }
    }

    function buildFilter() {
        var filter = {}
        // Read displayText (not text) so the in-progress IME preedit on Gboard/Samsung
        // is included — matches the onDisplayTextChanged trigger that scheduled this run.
        if (searchField.displayText.length > 0) {
            var searchText = searchField.displayText

            // Parse numeric keyword filters from search text
            // Syntax: keyword:N (exact), keyword:N-M (range), keyword:N+ (min only)
            var keywords = [
                { pattern: /\brating:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minEnjoyment", maxKey: "maxEnjoyment" },
                { pattern: /\brating:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minEnjoyment", maxKey: null },
                { pattern: /\brating:(\d+(?:\.\d+)?)\b/g, minKey: "minEnjoyment", maxKey: "maxEnjoyment", exact: true },
                { pattern: /\bdose:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minDose", maxKey: "maxDose" },
                { pattern: /\bdose:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minDose", maxKey: null },
                { pattern: /\bdose:(\d+(?:\.\d+)?)\b/g, minKey: "minDose", maxKey: "maxDose", exact: true },
                { pattern: /\byield:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minYield", maxKey: "maxYield" },
                { pattern: /\byield:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minYield", maxKey: null },
                { pattern: /\byield:(\d+(?:\.\d+)?)\b/g, minKey: "minYield", maxKey: "maxYield", exact: true },
                { pattern: /\btime:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minDuration", maxKey: "maxDuration" },
                { pattern: /\btime:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minDuration", maxKey: null },
                { pattern: /\btime:(\d+(?:\.\d+)?)\b/g, minKey: "minDuration", maxKey: "maxDuration", exact: true },
                { pattern: /\btds:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minTds", maxKey: "maxTds" },
                { pattern: /\btds:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minTds", maxKey: null },
                { pattern: /\btds:(\d+(?:\.\d+)?)\b/g, minKey: "minTds", maxKey: "maxTds", exact: true },
                { pattern: /\bey:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minEy", maxKey: "maxEy" },
                { pattern: /\bey:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minEy", maxKey: null },
                { pattern: /\bey:(\d+(?:\.\d+)?)\b/g, minKey: "minEy", maxKey: "maxEy", exact: true }
            ]

            for (var i = 0; i < keywords.length; i++) {
                var kw = keywords[i]
                var match = kw.pattern.exec(searchText)
                if (match) {
                    if (match.length === 3) {
                        // Range: N-M
                        filter[kw.minKey] = parseFloat(match[1])
                        filter[kw.maxKey] = parseFloat(match[2])
                    } else if (kw.exact) {
                        // Exact: N (set both min and max to same value)
                        filter[kw.minKey] = parseFloat(match[1])
                        filter[kw.maxKey] = parseFloat(match[1])
                    } else {
                        // Min only: N+
                        filter[kw.minKey] = parseFloat(match[1])
                    }
                    // Strip the matched keyword from the search text
                    searchText = searchText.replace(match[0], "")
                }
            }

            // Parse quality flag keywords (channeling:yes, grind:yes, skipframe:yes, puckfailed:yes)
            var flagKeywords = [
                { pattern: /\bchanneling:yes\b/gi, filterKey: "filterChanneling" },
                { pattern: /\bgrind:yes\b/gi, filterKey: "filterGrindIssue" },
                { pattern: /\bskipframe:yes\b/gi, filterKey: "filterSkipFirstFrame" },
                { pattern: /\bpuckfailed:yes\b/gi, filterKey: "filterPourTruncated" }
            ]
            for (var j = 0; j < flagKeywords.length; j++) {
                var fk = flagKeywords[j]
                if (fk.pattern.test(searchText)) {
                    filter[fk.filterKey] = true
                    searchText = searchText.replace(fk.pattern, "")
                }
            }

            // recipe: — the first STRING-valued keyword here. Every other one is
            // numeric or boolean, so none of them has had to decide where a term
            // ends. Two forms, because names routinely share a leading word
            // ("Dad Monday", "Dad Tuesday"):
            //   recipe:dad             single token
            //   recipe:"dad tuesday"   quoted, spaces allowed
            // Both are SUBSTRING matches, not exact: an auto-suggested name looks
            // like "Hometown Blend Latte · D-Flow / Q", and requiring that typed in
            // full — middot included — would make the keyword unusable. Exactness
            // is the tap-through's job, which compares an id rather than a string.
            // An unterminated quote runs to end-of-string instead of failing.
            // The unquoted branch requires at least one character (\S+, NOT \S*).
            // With \S* a bare "recipe:" matched an EMPTY term, so "recipe: dad" —
            // a space after the colon, which people type — set the no-match
            // sentinel while "dad" stayed in the free text and got ANDed against
            // it, returning zero shots where it used to find Dad Monday and Dad
            // Tuesday. An incomplete "recipe:" is better treated as not-a-keyword
            // and left to free text.
            //
            // An explicitly EMPTY quoted term (`recipe:""`) is different — that is
            // a deliberate narrowing request with nothing to narrow by, so it gets
            // the sentinel and honestly matches nothing.
            var recipeMatch = /\brecipe:(?:"([^"]*)"?|(\S+))/i.exec(searchText)
            if (recipeMatch) {
                var recipeTerm = recipeMatch[1] !== undefined ? recipeMatch[1] : recipeMatch[2]
                recipeTerm = (recipeTerm || "").trim()
                filter.recipeName = recipeTerm.length > 0 ? recipeTerm : " "
                searchText = searchText.replace(recipeMatch[0], "")
            }

            // bag: — the SECOND string-valued keyword, deliberately identical in
            // grammar to recipe: above (quoted or single-token, \S+ not \S*, an
            // explicitly empty quoted term getting the no-match sentinel), so a
            // user who learned one has learned the other. What differs is only
            // what it matches: a bag's coffee name, roaster and roast date
            // combined, because bag identity is spread over all three and the
            // user narrowing by "the July Ethiopian" cannot be expected to know
            // which field holds which word. Storage-side name is `bagTerm`, not
            // `bagName`, so it never reads as the banner's `bagLabel`.
            var bagMatch = /\bbag:(?:"([^"]*)"?|(\S+))/i.exec(searchText)
            if (bagMatch) {
                var bagTerm = bagMatch[1] !== undefined ? bagMatch[1] : bagMatch[2]
                bagTerm = (bagTerm || "").trim()
                filter.bagTerm = bagTerm.length > 0 ? bagTerm : " "
                searchText = searchText.replace(bagMatch[0], "")
            }

            // Strip any remaining keyword tokens (e.g. duplicate dose:18 dose:20)
            searchText = searchText.replace(/\b(rating|dose|yield|time|tds|ey):\d+(?:\.\d+)?(?:-\d+(?:\.\d+)?|\+)?/g, "")
            searchText = searchText.replace(/\b(channeling|temp|grind|skipframe|puckfailed):yes\b/gi, "")
            // \S* (not \S+) so a bare "recipe:" with no term is stripped too,
            // rather than falling through to FTS as the literal word "recipe".
            searchText = searchText.replace(/\brecipe:(?:"[^"]*"?|\S*)/gi, "")
            searchText = searchText.replace(/\bbag:(?:"[^"]*"?|\S*)/gi, "")

            // Pass remaining text as FTS search (skipped when exact initialFilter is active)
            searchText = searchText.trim().replace(/\s+/g, " ")
            if (searchText.length > 0 && !initialFilter) {
                filter.searchText = searchText
            }
        }
        // Merge initialFilter fields (from AutoFavoritesPage "Show" button etc.)
        if (initialFilter) {
            var filterFields = ["profileName", "beanBrand", "beanType", "grinderBrand", "grinderModel", "grinderSetting"]
            for (var k = 0; k < filterFields.length; k++) {
                var field = filterFields[k]
                if (initialFilter[field] !== undefined && initialFilter[field] !== "")
                    filter[field] = initialFilter[field]
            }
            // Numeric filters from the Auto-Favorites "Show" button in weight mode,
            // plus the recipe tap-through (history-recipe-identity). recipeId is an
            // ID, not a name: a rename cannot move it and two recipes sharing a
            // name stay distinct. initialFilter also carries `recipeName`, which is
            // deliberately absent from both loops — it is the banner's label only,
            // never a query term. `bagId` (history-bag-filter) is the same shape,
            // with `bagLabel` playing recipeName's banner-only part.
            var numericFields = ["minDose", "maxDose", "minYield", "maxYield", "targetWeight", "recipeId", "bagId"]
            for (var m = 0; m < numericFields.length; m++) {
                var nf = numericFields[m]
                if (initialFilter[nf] !== undefined && initialFilter[nf] !== null)
                    filter[nf] = initialFilter[nf]
            }
        }

        filter.sortField = sortField
        filter.sortDirection = sortDirection
        return filter
    }

    // Tap-through from a row's recipe name. Goes through initialFilter — the same
    // channel the Auto-Favorites "Show" button uses — so the banner, its Clear
    // control, and the free-text suppression all come for free. recipeName rides
    // along for the banner label only; the query matches on the id.
    function filterByRecipe(recipeId, recipeName) {
        if (!recipeId || recipeId <= 0)
            return
        Keyboard.commit()
        _populatingSearch = true
        searchField.text = ""
        searchField.lastTriggeredText = ""
        _populatingSearch = false
        initialFilter = { "recipeId": recipeId, "recipeName": recipeName || "" }
        loadShots()
    }

    // Same channel, same reasoning, for a bag (history-bag-filter). Reached from
    // the Custom widget's "this bag" action rather than a tap on a row; bagLabel
    // is the banner's text only, never a query term.
    function filterByBag(bagId, bagLabel) {
        if (!bagId || bagId <= 0)
            return
        Keyboard.commit()
        _populatingSearch = true
        searchField.text = ""
        searchField.lastTriggeredText = ""
        _populatingSearch = false
        initialFilter = { "bagId": bagId, "bagLabel": bagLabel || "" }
        loadShots()
    }

    // Apply an arbitrary initialFilter to the page that is ALREADY showing —
    // what main.qml's goToShotHistory() calls when Shot History is the current
    // page. An empty (or absent) filter clears, so a plain "Go to History" tap
    // from the status bar is a clear rather than the silent no-op it used to be.
    function applyInitialFilter(f) {
        if (!f || Object.keys(f).length === 0) {
            clearInitialFilter()
            return
        }
        Keyboard.commit()
        _populatingSearch = true
        searchField.text = ""
        searchField.lastTriggeredText = ""
        _populatingSearch = false
        initialFilter = f
        loadShots()
    }

    function clearInitialFilter() {
        initialFilter = null
        _populatingSearch = true
        searchField.text = ""
        searchField.lastTriggeredText = ""
        _populatingSearch = false
        loadShots()
    }

    function toggleSelection(shotId) {
        var idx = selectedShots.indexOf(shotId)
        if (idx >= 0) {
            selectedShots.splice(idx, 1)
        } else {
            selectedShots.push(shotId)
        }
        selectedShots = selectedShots.slice()  // Trigger binding update
    }

    function isSelected(shotId) {
        return selectedShots.indexOf(shotId) >= 0
    }

    function clearSelection() {
        selectedShots = []
    }

    function openComparison() {
        MainController.shotComparison.clearAll()
        // Sort selected shots chronologically, then batch-add in one DB load
        var sortedShots = selectedShots.slice().sort(function(a, b) { return a - b })
        MainController.shotComparison.addShots(sortedShots)
        AppShell.shotComparisonRequested()
    }

    function deleteSelectedShots() {
        var toDelete = selectedShots.slice()  // snapshot before signals can modify selectedShots
        MainController.shotHistory.deleteShots(toDelete)
        clearSelection()
    }

    // Get the list of shot IDs for navigation (selected shots or all loaded shots)
    function getNavigableShotIds() {
        if (selectedShots.length > 0) {
            // Return selected shots sorted chronologically
            return selectedShots.slice().sort(function(a, b) { return a - b })
        } else {
            // Return all loaded shots from the model
            var ids = []
            for (var i = 0; i < shotListModel.count; i++) {
                ids.push(shotListModel.get(i).id)
            }
            return ids
        }
    }

    function openShotDetail(shotId) {
        var shotIds = getNavigableShotIds()
        AppShell.shotDetailRequested(shotId, shotIds)
    }

    ListModel {
        id: shotListModel
    }

    // Filter bar
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.spacingMedium

        // Header row with selection count and compare button
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            visible: shotHistoryPage.selectedShots.length > 0

            Text {
                text: shotHistoryPage.selectedShots.length + " " + TranslationManager.translate("shothistory.selected", "selected")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.clear", "Clear")
                accessibleName: TranslationManager.translate("shotHistory.clearSelection", "Clear shot selection")
                onClicked: shotHistoryPage.clearSelection()
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.delete", "Delete")
                accessibleName: TranslationManager.translate("shotHistory.deleteSelected", "Delete selected shots")
                destructive: true
                onClicked: bulkDeleteConfirmDialog.open()
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.compare", "Compare")
                accessibleName: TranslationManager.translate("shotHistory.compareShots", "Compare selected shots side by side")
                primary: true
                enabled: shotHistoryPage.selectedShots.length >= 2
                onClicked: shotHistoryPage.openComparison()
            }
        }

        // Filter row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            StyledTextField {
                id: searchField
                Layout.fillWidth: true
                placeholder: TranslationManager.translate("shothistory.searchplaceholder", "Search shots...")
                rightPadding: searchClearButton.visible ? Theme.scaled(36) : Theme.scaled(12)
                // Hint the Android IME away from autocorrect. Some IMEs (notably Gboard)
                // ignore this, so we also drive the filter from `displayText` below —
                // displayText includes the IME preedit composing text, so the filter
                // updates per keystroke instead of waiting for a word commit.
                inputMethodHints: Qt.ImhNoPredictiveText
                property string lastTriggeredText: ""
                onDisplayTextChanged: {
                    var trimmed = displayText.trim()
                    if (trimmed !== lastTriggeredText) {
                        lastTriggeredText = trimmed
                        // User edited the search field — drop exact-match filter, use FTS
                        if (!shotHistoryPage._populatingSearch && shotHistoryPage.initialFilter)
                            shotHistoryPage.initialFilter = null
                        if (!shotHistoryPage._populatingSearch)
                            searchTimer.restart()
                    }
                }

                // Clear button (inline, hidden in accessibility mode to avoid overlapping elements)
                Item {
                    id: searchClearButton
                    width: Theme.scaled(20)
                    height: Theme.scaled(20)
                    visible: searchField.displayText.length > 0 && !(typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled)
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(10)
                    anchors.verticalCenter: parent.verticalCenter

                    ColoredIcon {
                        anchors.centerIn: parent
                        source: "qrc:/icons/cross.svg"
                        iconWidth: Theme.scaled(14)
                        iconHeight: Theme.scaled(14)
                        iconColor: Theme.textSecondaryColor
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.scaled(6)
                        onClicked: {
                            searchField.text = ""
                            searchField.focus = false
                        }
                    }
                }
            }

            // Accessible clear button (outside TextField bounds for TalkBack discoverability)
            AccessibleButton {
                visible: searchField.displayText.length > 0 && typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled
                accessibleName: TranslationManager.translate("shothistory.clearsearch", "Clear search")
                icon.source: "qrc:/icons/cross.svg"
                onClicked: {
                    searchField.text = ""
                    searchField.focus = false
                }
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.keywords", "Keywords")
                accessibleName: TranslationManager.translate("shothistory.searchhelp", "Search syntax help")
                onClicked: searchHelpDialog.open()
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.save", "Save")
                accessibleName: TranslationManager.translate("shothistory.saveSearch", "Save current search")
                enabled: searchField.text.trim().length > 0
                         && Settings.network.savedSearches.indexOf(searchField.text.trim()) === -1
                onClicked: Settings.network.addSavedSearch(searchField.text.trim())
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.saved", "Saved")
                accessibleName: TranslationManager.translate("shothistory.openSavedSearches", "Open saved searches")
                icon.source: "qrc:/icons/list.svg"
                enabled: Settings.network.savedSearches.length > 0
                onClicked: savedSearchesDialog.open()
            }

            // Sort field button
            AccessibleButton {
                text: shotHistoryPage.sortFieldLabels[shotHistoryPage.sortField] || TranslationManager.translate("shothistory.sort.date", "Date")
                accessibleName: TranslationManager.translate("shothistory.sortBy", "Sort by %1").arg(shotHistoryPage.sortFieldLabels[shotHistoryPage.sortField]
                                  || TranslationManager.translate("shothistory.sort.date", "Date"))
                onClicked: sortPickerDialog.open()
            }

            // Sort direction button
            AccessibleButton {
                icon.source: shotHistoryPage.sortDirection === "DESC" ? "qrc:/icons/SortDescending.svg" : "qrc:/icons/SortAscending.svg"
                tintIcon: true
                accessibleName: shotHistoryPage.sortDirection === "DESC"
                    ? TranslationManager.translate("shothistory.sortDescending", "Sort descending, tap to sort ascending")
                    : TranslationManager.translate("shothistory.sortAscending", "Sort ascending, tap to sort descending")
                onClicked: {
                    shotHistoryPage.sortDirection = (shotHistoryPage.sortDirection === "DESC") ? "ASC" : "DESC"
                    Settings.network.shotHistorySortDirection = shotHistoryPage.sortDirection
                    shotHistoryPage.loadShots()
                }
            }

            Timer {
                id: searchTimer
                interval: 300
                onTriggered: shotHistoryPage.loadShots()
            }
        }

        // Filter banner (shown when initialFilter is active)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: filterBannerRow.implicitHeight + Theme.spacingSmall * 2
            radius: Theme.scaled(8)
            color: Qt.alpha(Theme.primaryColor, 0.15)
            visible: shotHistoryPage.initialFilter !== null

            RowLayout {
                id: filterBannerRow
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                spacing: Theme.spacingSmall

                Text {
                    text: {
                        if (!shotHistoryPage.initialFilter) return ""
                        var parts = []
                        if (shotHistoryPage.initialFilter.recipeName) parts.push(shotHistoryPage.initialFilter.recipeName)
                        if (shotHistoryPage.initialFilter.bagLabel) parts.push(shotHistoryPage.initialFilter.bagLabel)
                        if (shotHistoryPage.initialFilter.beanBrand) parts.push(shotHistoryPage.initialFilter.beanBrand)
                        if (shotHistoryPage.initialFilter.beanType) parts.push(shotHistoryPage.initialFilter.beanType)
                        if (shotHistoryPage.initialFilter.profileName) parts.push(shotHistoryPage.initialFilter.profileName)
                        if (shotHistoryPage.initialFilter.grinderBrand || shotHistoryPage.initialFilter.grinderModel) {
                            var g = ((shotHistoryPage.initialFilter.grinderBrand || "") + " " + (shotHistoryPage.initialFilter.grinderModel || "")).trim()
                            if (shotHistoryPage.initialFilter.grinderSetting) g += " @ " + shotHistoryPage.initialFilter.grinderSetting
                            parts.push(g)
                        }
                        if (shotHistoryPage.initialFilter.minDose !== undefined && shotHistoryPage.initialFilter.maxDose !== undefined) {
                            var mid = (shotHistoryPage.initialFilter.minDose + shotHistoryPage.initialFilter.maxDose) / 2
                            parts.push(TranslationManager.translate("shothistory.filter.doseGrams", "%1g dose").arg(mid.toFixed(1)))
                        }
                        if (shotHistoryPage.initialFilter.targetWeight !== undefined && shotHistoryPage.initialFilter.targetWeight >= 0) {
                            parts.push(TranslationManager.translate("shothistory.filter.yieldGrams", "%1g yield").arg(shotHistoryPage.initialFilter.targetWeight.toFixed(1)))
                        }
                        return TranslationManager.translate("shothistory.filteredBy", "Filtered:") + " " + parts.join(" \u00B7 ")
                    }
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.primaryColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                AccessibleButton {
                    text: TranslationManager.translate("shothistory.clearFilter", "Clear")
                    accessibleName: TranslationManager.translate("shothistory.clearFilterAccessible", "Clear favorites filter")
                    icon.source: "qrc:/icons/cross.svg"
                    onClicked: shotHistoryPage.clearInitialFilter()
                }
            }
        }

        // Shot count
        Text {
            text: {
                var loaded = shotListModel.count
                var filtered = shotHistoryPage.filteredTotalCount
                var total = MainController.shotHistory.totalShots
                var countText = loaded + " " + TranslationManager.translate("shothistory.shots", "shots")
                if (filtered > loaded) {
                    countText += " (" + TranslationManager.translate("shothistory.of", "of") + " " + filtered + ")"
                }
                if (filtered < total) {
                    countText += " [" + TranslationManager.translate("shothistory.filtered", "filtered") + "]"
                }
                return countText
            }
            font: Theme.captionFont
            color: Theme.textSecondaryColor
        }

        // Shot list
        ListView {
            id: shotListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: shotListModel
            spacing: Theme.spacingSmall
            boundsBehavior: Flickable.StopAtBounds

            // Dismiss keyboard when user starts scrolling
            onMovementStarted: {
                if (searchField.activeFocus) {
                    searchField.focus = false
                    Keyboard.hide()
                }
            }

            // Infinite scroll - load more when near bottom
            onContentYChanged: {
                if (!shotHistoryPage.isLoadingMore && shotHistoryPage.hasMoreShots && contentHeight > 0) {
                    var threshold = contentHeight - height - Theme.scaled(200)
                    if (contentY > threshold) {
                        loadMoreTimer.restart()
                    }
                }
            }

            Timer {
                id: loadMoreTimer
                interval: 100
                onTriggered: shotHistoryPage.loadMoreShots()
            }

            delegate: Rectangle {
                id: shotDelegate

                // The ListView's model role object, declared rather than injected.
                required property var model

                width: shotListView.width
                height: Math.max(Theme.scaled(90), shotContentRow.implicitHeight + Theme.spacingMedium * 2)
                radius: Theme.cardRadius
                color: shotHistoryPage.isSelected(model.id) ? Qt.darker(Theme.cardBackgroundColor, 1.2) : Theme.cardBackgroundColor
                border.color: shotHistoryPage.isSelected(model.id) ? Theme.primaryColor : "transparent"
                border.width: shotHistoryPage.isSelected(model.id) ? 2 : 0

                property int shotEnjoyment: model.enjoyment0to100 || 0

                // Accessibility: row is a button whose primary action opens shot detail.
                // Note: visual tap toggles selection (line 696); TalkBack double-tap opens detail
                // because detail view is the more useful primary action for screen reader users.
                // A shot made with a recipe (history-recipe-identity). The name
                // and drink type ride in with the shot list itself (one LEFT
                // JOIN in requestShotsFiltered), NOT resolved per delegate.
                // Gated on the NAME resolving, not just on the id. The invariant
                // that a shot-linked recipe is never hard-deleted is enforced in
                // exactly one function (RecipeStorage::requestDeleteRecipe) and
                // there is no FK behind it, while a non-merge import does a
                // wholesale DELETE FROM recipes. If an id ever fails to resolve,
                // falling back to the profile row is a correct-looking row; an
                // id-only gate would instead render an EMPTY identity line with
                // the profile already demoted away from it.
                property bool hasRecipe: (model.recipeId || 0) > 0 && !!model.recipeName
                property bool recipeIsArchived: hasRecipe && (model.recipeArchived === true)

                // Profile plus the shot's temperature override. Rendered on the
                // identity line for a recipe-less shot and at the head of the
                // secondary line otherwise — one function so the two placements
                // cannot drift.
                function profileText() {
                    var name = model.profileName || ""
                    var tempOvr = model.temperatureOverrideC || 0
                    if (tempOvr > 0)
                        return name + " (" + Math.round(Theme.cToDisplay(tempOvr)) + Theme.tempUnitSuffix() + ")"
                    return name
                }

                function beanText() {
                    return (model.beanBrand || "") + (model.beanType ? " " + model.beanType : "")
                }

                // Everything on the secondary line except the pinned grind. The
                // profile leads it only when the recipe took the identity slot.
                function secondaryText() {
                    var bean = beanText()
                    if (!hasRecipe)
                        return bean
                    var profile = profileText()
                    if (profile && bean) return profile + " · " + bean
                    return profile || bean
                }

                // Grind, with the RPM half paired when recorded (variable-RPM
                // grinders). Always labelled — it sits among other numbers on the
                // metrics line, where a bare "8.75 · 1500" identifies nothing.
                function grindText() {
                    var grind = model.grinderSetting || ""
                    if (!grind) return ""
                    if (model.rpm > 0) grind += " · " + model.rpm
                    return TranslationManager.translate("shothistory.metric.grind", "Grind") + " " + grind
                }

                Accessible.role: Accessible.Button
                Accessible.name: {
                    var parts = []
                    // Recipe first: for a user who named it themselves it is the
                    // strongest identity on the row, and it is the only thing here
                    // the profile/bean text cannot imply.
                    if (shotDelegate.hasRecipe) {
                        // The archived state is DIMMED visually, so it has to be
                        // spoken too — colour is never the only carrier.
                        parts.push(model.recipeArchived
                                   ? TranslationManager.translate("shothistory.accessible.recipeArchived",
                                                                  "%1 (archived recipe)").arg(model.recipeName)
                                   : model.recipeName)
                    }
                    if (model.profileName) parts.push(model.profileName)
                    if (model.dateTime) parts.push(model.dateTime)
                    var bean = (model.beanBrand || "") + (model.beanType ? " " + model.beanType : "")
                    if (bean) parts.push(bean)
                    var doseVal = model.doseWeightG || 0
                    var yieldVal = model.finalWeightG || 0
                    if (doseVal > 0 && yieldVal > 0)
                        parts.push(doseVal.toFixed(1) + "g to " + yieldVal.toFixed(1) + "g")
                    // Pre-existing gap: the grind is on the row but was never spoken,
                    // so the one number a dialing-in user scans for was unreachable by
                    // screen reader. Same helper as the visible metric, so the two
                    // cannot word it differently.
                    var grindSpoken = shotDelegate.grindText()
                    if (grindSpoken) parts.push(grindSpoken)
                    if (shotDelegate.shotEnjoyment > 0) parts.push(shotDelegate.shotEnjoyment + "%")
                    // Same keys the visible QualityBadges use, so the spoken row and the
                    // badges cannot drift apart or disagree in a translated locale. These
                    // were four hardcoded English strings until this change.
                    var issues = []
                    if (model.pourTruncatedDetected)
                        issues.push(TranslationManager.translate("badges.puckFailed", "Puck failed"))
                    if (model.channelingDetected)
                        issues.push(TranslationManager.translate("badges.channeling", "Channeling detected"))
                    if (model.grindIssueDetected)
                        issues.push(TranslationManager.translate("badges.grindIssue", "Grind issue"))
                    if (model.skipFirstFrameDetected)
                        issues.push(TranslationManager.translate("badges.skipFirstFrame", "First step skipped"))
                    if (issues.length > 0) parts.push(issues.join(", "))
                    // The cloud icon is the only thing carrying "uploaded" in this row, and it
                    // is Accessible.ignored — without this the state was unreachable by screen
                    // reader, which is the failure CLAUDE.md means by "never the only carrier".
                    if (model.hasVisualizerUpload)
                        parts.push(TranslationManager.translate("shotdetail.uploadedtovisualizer",
                                                                "Uploaded to Visualizer"))
                    return parts.join(", ")
                }
                Accessible.focusable: true
                Accessible.onPressAction: shotHistoryPage.openShotDetail(model.id)

                RowLayout {
                    id: shotContentRow
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Selection checkbox
                    CheckBox {
                        id: checkBox
                        checked: shotHistoryPage.isSelected(shotDelegate.model.id)
                        onClicked: shotHistoryPage.toggleSelection(shotDelegate.model.id)
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: TranslationManager.translate("shothistory.accessible.compare", "Compare")
                        Accessible.checked: checked
                        Accessible.focusable: true

                        indicator: Rectangle {
                            implicitWidth: Theme.scaled(24)
                            implicitHeight: Theme.scaled(24)
                            radius: Theme.scaled(4)
                            color: checkBox.checked ? Theme.primaryColor : "transparent"
                            border.color: checkBox.checked ? Theme.primaryColor : Theme.borderColor
                            border.width: 2

                            ColoredIcon {
                                anchors.centerIn: parent
                                source: "qrc:/icons/tick.svg"
                                iconWidth: Theme.scaled(16)
                                iconHeight: Theme.scaled(16)
                                // primaryContrastColor, not primaryColor: the indicator's fill is
                                // Theme.primaryColor when checked, so a primaryColor tick is drawn
                                // blue-on-blue and cannot be seen. Same class as SettingsPage's
                                // white-on-white search icon — visible only by looking at the app.
                                iconColor: Theme.primaryContrastColor
                                visible: checkBox.checked
                            }
                        }
                    }

                    // Shot info — all text is decorative (already summarized in row Accessible.name)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        // Identity line. A recipe-driven shot puts the recipe here
                        // — for a user who named the recipe themselves it is the
                        // strongest handle on the row, and the profile is machinery
                        // by comparison. The profile is not dropped, it moves to the
                        // secondary line below, carrying its own temperature
                        // override with it (the override belongs to the profile, not
                        // to the recipe, whose stored temperature is a baseline).
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Text {
                                text: shotDelegate.model.dateTime || ""
                                font: Theme.subtitleFont
                                color: Theme.textColor
                                Accessible.ignored: true
                            }

                            // Themed SVG, not an emoji — a colour glyph in a plain
                            // Text crashes the render thread on macOS.
                            //
                            // ThemedIcon, NOT ColoredIcon: the latter is a Button
                            // that absorbs clicks by design (ColoredIcon.qml:35),
                            // which would punch a dead spot into the row where
                            // tap-to-select works everywhere else.
                            ThemedIcon {
                                visible: shotDelegate.hasRecipe
                                source: DrinkType.icon(shotDelegate.model.recipeDrinkType || "")
                                iconSize: Theme.scaled(16)
                                color: shotDelegate.recipeIsArchived ? Theme.textSecondaryColor
                                                                     : Theme.primaryColor
                                Layout.alignment: Qt.AlignVCenter
                                Accessible.ignored: true
                            }

                            Text {
                                id: identityText
                                textFormat: Text.StyledText
                                text: Theme.replaceEmojiWithImg(
                                          shotDelegate.hasRecipe ? (shotDelegate.model.recipeName || "")
                                                                 : shotDelegate.profileText(),
                                          Theme.labelFont.pixelSize)
                                font: Theme.labelFont
                                // Archived recipes dim. The row's Accessible.name says
                                // "archived" as well, so the colour is not the only
                                // carrier of the state.
                                color: shotDelegate.recipeIsArchived ? Theme.textSecondaryColor
                                                                     : Theme.primaryColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight

                                // Tap the recipe name to see every shot made with it.
                                // A real action, so it is its own focusable stop rather
                                // than being folded into the row's summary; on a
                                // recipe-less row this collapses and the profile name
                                // stays plain text as before.
                                Accessible.ignored: !shotDelegate.hasRecipe
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate(
                                                     "shothistory.accessible.showRecipeShots",
                                                     "Show all shots using %1").arg(shotDelegate.model.recipeName || "")
                                Accessible.focusable: shotDelegate.hasRecipe
                                Accessible.onPressAction: shotHistoryPage.filterByRecipe(
                                                              shotDelegate.model.recipeId,
                                                              shotDelegate.model.recipeName)

                                // Sized to the PAINTED TEXT, not to the item: this Text
                                // is Layout.fillWidth, so anchors.fill would claim the
                                // whole remaining strip of the row. That stole two
                                // things from every recipe row — a tap on the blank
                                // space right of a short name (previously: toggle
                                // selection) and press-and-hold anywhere on that strip
                                // (previously: open detail), because the row's own
                                // MouseArea sits at z: -1 and never sees a press this
                                // one accepts.
                                MouseArea {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: Math.min(parent.width, parent.implicitWidth)
                                    enabled: shotDelegate.hasRecipe
                                    onClicked: shotHistoryPage.filterByRecipe(
                                                   shotDelegate.model.recipeId,
                                                   shotDelegate.model.recipeName)
                                    // Forwarded so the row's gesture still works over
                                    // the name itself, not just around it.
                                    onPressAndHold: shotHistoryPage.openShotDetail(shotDelegate.model.id)
                                }
                            }
                        }

                        // Secondary line: identity only (profile · bean). The grind
                        // used to trail it here as a "(8)" parenthetical, where a long
                        // roaster name silently elided it away — it now sits on the
                        // metrics line below, beside the other dial-in numbers.
                        Text {
                            id: secondaryIdentity
                            textFormat: Text.StyledText
                            text: Theme.replaceEmojiWithImg(shotDelegate.secondaryText(),
                                                            Theme.labelFont.pixelSize)
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            visible: text !== ""
                            Accessible.ignored: true
                        }

                        RowLayout {
                            spacing: Theme.spacingLarge

                            Text {
                                text: {
                                    var dose = (shotDelegate.model.doseWeightG || 0).toFixed(1)
                                    var actual = (shotDelegate.model.finalWeightG || 0).toFixed(1)
                                    var yieldText = actual + "g"
                                    var target = shotDelegate.model.targetWeightG || 0
                                    if (target > 0 && Math.abs(target - shotDelegate.model.finalWeightG) > 0.5) {
                                        yieldText = actual + "g (" + Math.round(target) + "g)"
                                    }
                                    return dose + "g \u2192 " + yieldText
                                }
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: TranslationManager.translate("shothistory.metric.time", "Time")
                                      + " " + (shotDelegate.model.durationSec || 0).toFixed(1) + "s"
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            // Grind lives on the metrics line with the other dial-in
                            // numbers, labelled because "8.75 · 1500" says nothing on
                            // its own. It sets no elide: on the identity line it used
                            // to be a trailing "(8)" parenthetical that a long roaster
                            // name silently ate, and this line carries only short
                            // numbers so nothing crowds it out. (Not a guarantee —
                            // a RowLayout can still squeeze an un-elided Text below
                            // its implicit width and clip it. Add Layout.minimumWidth
                            // if this ever needs to be one.)
                            Text {
                                text: shotDelegate.grindText()
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                visible: text !== ""
                                Accessible.ignored: true
                            }

                            // "Uploaded to Visualizer". Chrome, so a themed icon rather than
                            // the Twemoji cloud this used to reach for: that asset's fills are
                            // baked in at #CCD6DD/#E1E8ED, near-white, which all but vanished
                            // against a light-mode row and could not follow the theme at all.
                            // Matches the same indicator on ShotDetailPage.
                            ThemedIcon {
                                source: "qrc:/icons/CloudUpload.svg"
                                iconSize: Theme.scaled(16)
                                color: Theme.successColor
                                visible: shotDelegate.model.hasVisualizerUpload
                                // Announced as part of the row's Accessible.name instead.
                                Accessible.ignored: true
                            }

                            // Quality issue indicator dots. Order: red puckFailed first
                            // (most severe — shot has no tuning signal), then channeling
                            // (red), grind (orange), skipFirstFrame (red).
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(8); Layout.preferredHeight: Theme.scaled(8); radius: Theme.scaled(4)
                                color: Theme.errorColor
                                visible: shotDelegate.model.pourTruncatedDetected ?? false
                                Accessible.ignored: true
                            }
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(8); Layout.preferredHeight: Theme.scaled(8); radius: Theme.scaled(4)
                                color: Theme.errorColor
                                visible: shotDelegate.model.channelingDetected ?? false
                                Accessible.ignored: true
                            }
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(8); Layout.preferredHeight: Theme.scaled(8); radius: Theme.scaled(4)
                                color: Theme.warningColor
                                visible: shotDelegate.model.grindIssueDetected ?? false
                                Accessible.ignored: true
                            }
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(8); Layout.preferredHeight: Theme.scaled(8); radius: Theme.scaled(4)
                                color: Theme.errorColor
                                visible: shotDelegate.model.skipFirstFrameDetected ?? false
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Rating percentage
                    Text {
                        text: shotDelegate.shotEnjoyment > 0 ? shotDelegate.shotEnjoyment + "%" : ""
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                        color: Theme.warningColor
                        Layout.preferredWidth: Theme.scaled(45)
                        horizontalAlignment: Text.AlignRight
                        visible: shotDelegate.shotEnjoyment > 0
                        Accessible.ignored: true
                    }

                    // Load Profile button
                    Rectangle {
                        Layout.preferredWidth: loadButtonText.implicitWidth + Theme.scaled(20)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.warningColor
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("shothistory.accessible.load", "Load profile")
                        Accessible.focusable: true
                        Accessible.onPressAction: loadArea.clicked(null)

                        Text {
                            id: loadButtonText
                            anchors.centerIn: parent
                            text: TranslationManager.translate("shotHistory.button.load", "Load")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: loadArea
                            anchors.fill: parent
                            onClicked: {
                                shotHistoryPage._waitingForShotLoad = true
                                MainController.loadShotWithMetadata(shotDelegate.model.id)
                            }
                        }
                    }

                    // Create-recipe button (promote this shot to a recipe —
                    // opens the composer prefilled from the shot, add-recipes).
                    // Hidden when the shot already came FROM a recipe: offering to
                    // create one from it then reads as broken. Shot Detail has
                    // gated this since shot-pages-card-cleanup; History, Auto
                    // Favorites and the web list never got the same rule.
                    Rectangle {
                        visible: !shotDelegate.hasRecipe
                        Layout.preferredWidth: recipeButtonText.implicitWidth + Theme.scaled(20)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("shothistory.accessible.recipe", "Create recipe from this shot")
                        Accessible.focusable: true
                        Accessible.onPressAction: recipeArea.clicked(null)

                        Text {
                            id: recipeButtonText
                            anchors.centerIn: parent
                            text: TranslationManager.translate("shotHistory.button.recipe", "Recipe")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: recipeArea
                            anchors.fill: parent
                            onClicked: {
                                AppShell.recipeWizardRequested("create", { promoteShotId: shotDelegate.model.id })
                            }
                        }
                    }

                    // Edit button (green circle with E)
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(40)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.successColor
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("shothistory.accessible.edit", "Edit shot")
                        Accessible.focusable: true
                        Accessible.onPressAction: editArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "E"
                            font.pixelSize: Theme.scaled(18)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: editArea
                            anchors.fill: parent
                            onClicked: {
                                AppShell.postShotReviewRequested(shotDelegate.model.id, false)
                            }
                        }
                    }

                    // Detail arrow
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(40)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("shothistory.accessible.details", "View details")
                        Accessible.focusable: true
                        Accessible.onPressAction: detailArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: ">"
                            font.pixelSize: Theme.scaled(20)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: detailArea
                            anchors.fill: parent
                            onClicked: shotHistoryPage.openShotDetail(shotDelegate.model.id)
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: shotHistoryPage.toggleSelection(shotDelegate.model.id)
                    onPressAndHold: shotHistoryPage.openShotDetail(shotDelegate.model.id)
                }
            }

            footer: Item {
                width: shotListView.width
                height: shotHistoryPage.isLoadingMore ? Theme.scaled(50) : 0
                visible: shotHistoryPage.isLoadingMore

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("shothistory.loading", "Loading more...")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Empty state
            Tr {
                anchors.centerIn: parent
                key: "shothistory.noshots"
                fallback: "No shots found"
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                visible: shotListModel.count === 0
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("shothistory.title", "Shot History")
        rightText: MainController.shotHistory.totalShots + " " + TranslationManager.translate("shothistory.shots", "shots")
        onBackClicked: AppShell.backRequested()
    }

    DecenzaDialog {
        id: bulkDeleteConfirmDialog
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

            // Title
            Text {
                text: TranslationManager.translate("shothistory.deleteconfirmtitle", "Delete Shots?")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            // Message
            Text {
                text: TranslationManager.translate("shothistory.deleteconfirmmessage", "Permanently delete %1 shot(s) from history?").arg(shotHistoryPage.selectedShots.length)
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

            // Buttons
            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("shothistory.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("shothistory.cancelDelete", "Cancel delete")
                    Layout.fillWidth: true
                    onClicked: bulkDeleteConfirmDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("shothistory.delete", "Delete")
                    accessibleName: TranslationManager.translate("shothistory.confirmDelete", "Confirm delete shots")
                    destructive: true
                    Layout.fillWidth: true
                    onClicked: {
                        bulkDeleteConfirmDialog.close()
                        shotHistoryPage.deleteSelectedShots()
                    }
                }
            }
        }
    }

    function insertSearchKeyword(keyword) {
        var currentText = searchField.text
        if (currentText.length > 0 && !currentText.endsWith(" ")) {
            currentText += " "
        }
        searchField.text = currentText + keyword
        searchHelpDialog.close()
        searchField.forceActiveFocus()
        Keyboard.show()
    }

    // Saved searches dialog
    DecenzaDialog {
        id: savedSearchesDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(400), shotHistoryPage.width - Theme.scaled(40))
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
                text: TranslationManager.translate("shothistory.savedSearchesTitle", "Saved Searches")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            ListView {
                id: savedSearchesList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Theme.scaled(300))
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(10)
                Layout.rightMargin: Theme.scaled(10)
                clip: true
                model: Settings.network.savedSearches
                spacing: Theme.scaled(2)

                delegate: Rectangle {
                    id: savedSearchDelegate

                    required property var modelData

                    readonly property bool _accessibilityMode: typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled

                    width: savedSearchesList.width
                    height: Theme.scaled(44) + (_accessibilityMode ? Theme.scaled(40) : 0)
                    radius: Theme.scaled(6)
                    color: delegateTapArea.pressed ? Qt.darker(Theme.surfaceColor, 1.1) : "transparent"

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.applySavedSearch", "Apply search: %1").arg(modelData)
                    Accessible.focusable: true
                    Accessible.onPressAction: delegateTapArea.clicked(null)

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(44)
                            Layout.leftMargin: Theme.scaled(10)
                            Layout.rightMargin: Theme.scaled(4)
                            spacing: Theme.spacingSmall

                            Text {
                                text: savedSearchDelegate.modelData
                                font: Theme.bodyFont
                                color: Theme.textColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }

                            // Inline delete button (hidden in accessibility mode)
                            Rectangle {
                                visible: !savedSearchDelegate._accessibilityMode
                                Layout.preferredWidth: Theme.scaled(28)
                                Layout.preferredHeight: Theme.scaled(28)
                                radius: Theme.scaled(14)
                                color: deleteArea.pressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor

                                Image {
                                    anchors.centerIn: parent
                                    source: "qrc:/icons/cross.svg"
                                    sourceSize.width: Theme.scaled(12)
                                    sourceSize.height: Theme.scaled(12)
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: deleteArea
                                    anchors.fill: parent
                                    onClicked: {
                                        Settings.network.removeSavedSearch(savedSearchDelegate.modelData)
                                        if (Settings.network.savedSearches.length === 0) {
                                            savedSearchesDialog.close()
                                        }
                                    }
                                }
                            }
                        }

                        // Separate delete button row for accessibility mode (outside row bounds)
                        AccessibleButton {
                            visible: savedSearchDelegate._accessibilityMode
                            text: TranslationManager.translate("shothistory.delete", "Delete")
                            accessibleName: TranslationManager.translate("shothistory.deleteSavedSearch", "Delete search: %1").arg(savedSearchDelegate.modelData)
                            destructive: true
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            Layout.leftMargin: Theme.scaled(10)
                            Layout.rightMargin: Theme.scaled(4)
                            onClicked: {
                                Settings.network.removeSavedSearch(savedSearchDelegate.modelData)
                                if (Settings.network.savedSearches.length === 0) {
                                    savedSearchesDialog.close()
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: delegateTapArea
                        anchors.fill: parent
                        z: -1
                        onClicked: {
                            searchField.text = savedSearchDelegate.modelData
                            savedSearchesDialog.close()
                        }
                    }
                }
            }

            // Close button
            AccessibleButton {
                text: TranslationManager.translate("shothistory.close", "Close")
                accessibleName: TranslationManager.translate("shothistory.closeSavedSearches", "Close saved searches")
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.scaled(12)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: savedSearchesDialog.close()
            }
        }
    }

    // Sort picker dialog
    SelectionDialog {
        id: sortPickerDialog
        title: TranslationManager.translate("shothistory.sortByTitle", "Sort By")
        options: shotHistoryPage.sortFieldKeys.map(function(key) { return shotHistoryPage.sortFieldLabels[key] || key })
        currentIndex: shotHistoryPage.sortFieldKeys.indexOf(shotHistoryPage.sortField)
        onSelected: function(index, value) {
            shotHistoryPage.sortField = shotHistoryPage.sortFieldKeys[index]
            shotHistoryPage.sortDirection = shotHistoryPage.defaultSortDirections[shotHistoryPage.sortFieldKeys[index]] || "DESC"
            Settings.network.shotHistorySortField = shotHistoryPage.sortField
            Settings.network.shotHistorySortDirection = shotHistoryPage.sortDirection
            shotHistoryPage.loadShots()
        }
    }

    // Search syntax help dialog
    DecenzaDialog {
        id: searchHelpDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        // Width follows content between a design minimum and a screen-bounded maximum, so a wider
        // font (fallback, or a host font that beat the bundled one) or a long translation widens
        // the dialog instead of pushing its third column under the clip. The grid cells also
        // elide, so at the screen bound the layout degrades to shortened text rather than
        // content that is simply not there. (#1469, #1537)
        //
        // No binding loop: a Text's implicitWidth is its natural unwrapped/unelided width, which
        // does not depend on the width it is allocated — so content -> width is one-directional.
        width: Math.min(Math.max(Theme.scaled(420), searchHelpColumn.implicitWidth),
                        shotHistoryPage.width - Theme.scaled(40))
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        // Height-capped + scrollable so a wider or fallback system font (or a long
        // translation) can never push the dialog off-screen — it scrolls instead.
        // Flickable, deliberately NOT ScrollView. ScrollView adopts its child as contentItem
        // and drives that child's geometry from contentWidth/contentHeight — which fights a
        // ColumnLayout, since the layout also sizes itself. The observable result was a
        // scroll range of ~2px with rows below it unreachable, verified on the running app
        // at labelSize 26. Flickable never touches its children's geometry, so the layout's
        // implicitHeight stays purely content-driven and contentHeight is honest.
        contentItem: Flickable {
            id: searchHelpFlick
            // Leaves room for the footer, so dialog height (content + footer) still fits
            // the page. Without subtracting it the footer pushed the dialog past the
            // window edge at large font sizes.
            implicitHeight: Math.min(searchHelpColumn.implicitHeight,
                                     shotHistoryPage.height - Theme.scaled(80)
                                         - searchHelpDialog.footer.implicitHeight)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // Width pinned to the viewport so there is never a horizontal scroll — the dialog
            // widens to fit instead, and past the screen bound the grid cells elide.
            contentWidth: width
            contentHeight: searchHelpColumn.implicitHeight

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: searchHelpColumn
                width: searchHelpFlick.width
                spacing: 0

                Text {
                    text: TranslationManager.translate("shothistory.searchhelptitle", "Search Syntax")
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.ignored: true
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(20)
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                }

                Text {
                    text: TranslationManager.translate("shothistory.searchhelpintro", "Use keywords to filter by numeric fields.\nTap a keyword below to add it to your search.")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(10)
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                }

                // Keyword reference grid
                GridLayout {
                    columns: 3
                    columnSpacing: Theme.scaled(12)
                    rowSpacing: Theme.scaled(6)
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(12)
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)

                    // Header row
                    Text { text: TranslationManager.translate("shothistory.helpheaderkeyword", "Keyword"); font.bold: true; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textColor; Accessible.ignored: true }
                    Text { text: TranslationManager.translate("shothistory.helpheaderfilters", "Filters"); font.bold: true; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: TranslationManager.translate("shothistory.helpheaderexample", "Example"); font.bold: true; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    // Data rows — keyword column is tappable to insert into search
                    SearchKeywordChip {
                        keyword: "rating:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helprating", "Enjoyment (0-100)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "rating:70+"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "dose:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helpdose", "Dose weight (g)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "dose:16-18"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "yield:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helpyield", "Yield weight (g)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "yield:30-40"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "time:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helptime", "Duration (seconds)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "time:25-35"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "tds:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shotHistory.label.tds", "TDS"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "tds:1.3-1.5"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "ey:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helpey", "Extraction yield (%)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "ey:18-22"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    // recipe: — a name, where every keyword above takes a number.
                    SearchKeywordChip {
                        keyword: "recipe:"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helprecipe", "Recipe name (quote it if it has spaces)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "recipe:\"dad tuesday\""; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    // Quality flag keywords
                    SearchKeywordChip {
                        keyword: "channeling:yes"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helpchanneling", "Channeling detected"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "channeling:yes"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "grind:yes"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helpgrind", "Grind issue"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "grind:yes"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "skipframe:yes"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helpskipframe", "First step skipped"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "skipframe:yes"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }

                    SearchKeywordChip {
                        keyword: "puckfailed:yes"
                        onPicked: shotHistoryPage.insertSearchKeyword(keyword)
                    }
                    Text { text: TranslationManager.translate("shothistory.helppuckfailed", "Puck failed"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: "puckfailed:yes"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true; Layout.fillWidth: true; elide: Text.ElideRight }
                }

                // Syntax explanation
                Text {
                    text: TranslationManager.translate("shothistory.searchhelpsyntax",
                        "Syntax: N (exact), N-M (range), N+ (minimum)\nQuality flags: channeling:yes, grind:yes, skipframe:yes, puckfailed:yes\nCombine keywords with text: ethiopia dose:18 channeling:yes")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(12)
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                }

            }
        }

        // Close lives in the footer, outside the scrolling content, so it is reachable at
        // any font size and window height. While it was the last child of the scrolling
        // column, large text pushed it to the very end of the scroll range where it sat
        // half under the dialog edge — verified on a small window at labelSize 26.
        footer: Item {
            implicitHeight: helpCloseButton.implicitHeight + Theme.scaled(24)

            AccessibleButton {
                id: helpCloseButton
                text: TranslationManager.translate("shothistory.close", "Close")
                accessibleName: TranslationManager.translate("shothistory.closeHelp", "Close search help")
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                onClicked: searchHelpDialog.close()
            }
        }
    }

}
