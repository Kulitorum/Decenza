// The results-grid delegate reads this file's ids (`communityBrowser`, `resultsGrid`);
// Bound makes them statically resolvable. It declares its one injected role,
// `modelData`, required in the same edit -- without that, Bound stops role injection
// and every community card renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza
import "../components/CommunityBrowseNavigation.js" as CommunityBrowseNavigation

Item {
    id: communityBrowser
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("community.title", "Community")

    objectName: "communityBrowserPage"

    // Filter state
    property string filterType: ""
    property string filterScheme: ""
    property string filterVariable: ""
    property string filterAction: ""
    property string sortBy: "newest"
    readonly property var navigationContext: ({filterType: filterType, filterScheme: filterScheme, filterVariable: filterVariable,
                                               filterAction: filterAction, sortBy: sortBy,
                                               selectedEntryId: _restoringNavigationContext
                                                   ? _restoreSelectedEntryId : selectedEntryId,
                                               currentPage: _restoringNavigationContext
                                                   ? _restoreTargetPage : currentPage,
                                               contentY: _restoringNavigationContext
                                                   ? _restoreContentY : resultsGrid.contentY})
    readonly property var filterTypes: ["", "item", "zone", "layout", "theme"]
    readonly property var filterSchemes: ["", "dark", "light"]
    readonly property var filterVariables: ["", "%TEMP%", "%STEAM_TEMP%", "%PRESSURE%", "%FLOW%",
                                            "%WEIGHT%", "%WATER%", "%SHOT_TIME%", "%PROFILE%", "%STATE%",
                                            "%TIME%", "%DATE%", "%RATIO%", "%DOSE%", "%TARGET_WEIGHT%"]
    readonly property var filterSorts: ["newest", "popular"]
    property bool filtersReady: false
    property int currentPage: 1
    property int browseRequestedPage: 1
    property var communityEntries: []
    property int totalCommunityResults: 0
    property bool previewsUnavailable: false
    property string browseRequestId: ""
    property bool browseAgain: false
    property string downloadRequestId: ""
    property string initialSelectedEntryId: ""
    property int initialPage: 1
    property real initialContentY: 0
    property bool _restoringNavigationContext: false
    property int _restoreTargetPage: 1
    property string _restoreSelectedEntryId: ""
    property real _restoreContentY: 0
    property bool _restoreViewportPending: false

    // Selection
    property string selectedEntryId: ""

    Component.onCompleted: {
        typeFilter.currentIndex = Math.max(0, filterTypes.indexOf(filterType))
        variableFilter.currentIndex = Math.max(0, filterVariables.indexOf(filterVariable))
        actionFilter.currentIndex = actionFilter.catalogActions.map(a => a.id).indexOf(filterAction) + 1
        sortFilter.currentIndex = Math.max(0, filterSorts.indexOf(sortBy))
        filterType = filterTypes[typeFilter.currentIndex]
        filterVariable = filterVariables[variableFilter.currentIndex]
        filterAction = actionFilter.currentIndex > 0 ? actionFilter.catalogActions[actionFilter.currentIndex - 1].id : ""
        sortBy = filterSorts[sortFilter.currentIndex]
        filtersReady = true
        if (initialPage > 1 || initialSelectedEntryId !== "" || initialContentY > 0)
            restoreNavigationContext()
        else
            refreshResults()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.scaled(12)
        anchors.rightMargin: Theme.scaled(12)
        anchors.bottomMargin: Theme.bottomBarHeight
        spacing: Theme.spacingMedium

        // Filter bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            // Type filter
            StyledComboBox {
                id: typeFilter
                Layout.preferredWidth: Theme.scaled(120)
                accessibleLabel: TranslationManager.translate("community.filter.type", "Type filter")
                model: [TranslationManager.translate("community.type.all", "All Types"), TranslationManager.translate("community.type.items", "Items"), TranslationManager.translate("community.type.zones", "Zones"), TranslationManager.translate("community.type.layouts", "Layouts"), TranslationManager.translate("community.type.themes", "Themes")]
                onCurrentIndexChanged: {
                    if (!communityBrowser.filtersReady) return
                    communityBrowser.filterType = communityBrowser.filterTypes[currentIndex]
                    if (communityBrowser.filterType !== "theme") communityBrowser.filterScheme = ""
                    communityBrowser.refreshResults()
                }
            }

            // Variable filter
            StyledComboBox {
                id: variableFilter
                visible: communityBrowser.filterType !== "theme"
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.variable", "Variable filter")
                model: [
                    TranslationManager.translate("community.variable.any", "Any Variable"),
                    TranslationManager.translate("community.variable.groupTemp", "Group Head Temp"),
                    TranslationManager.translate("community.variable.steamTemp", "Steam Temp"),
                    TranslationManager.translate("community.variable.pressure", "Pressure"),
                    TranslationManager.translate("community.variable.flow", "Flow Rate"),
                    TranslationManager.translate("community.variable.weight", "Weight"),
                    TranslationManager.translate("community.variable.water", "Water Level"),
                    TranslationManager.translate("community.variable.shotTime", "Shot Time"),
                    TranslationManager.translate("community.variable.profile", "Profile"),
                    TranslationManager.translate("community.variable.state", "Machine State"),
                    TranslationManager.translate("community.variable.time", "Time"),
                    TranslationManager.translate("community.variable.date", "Date"),
                    TranslationManager.translate("community.variable.ratio", "Ratio"),
                    TranslationManager.translate("community.variable.dose", "Dose"),
                    TranslationManager.translate("community.variable.targetWeight", "Target Weight")]
                onCurrentIndexChanged: {
                    if (!communityBrowser.filtersReady) return
                    communityBrowser.filterVariable = communityBrowser.filterVariables[currentIndex]
                    communityBrowser.refreshResults()
                }
            }

            StyledComboBox {
                visible: communityBrowser.filterType === "theme"
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.scheme", "Theme scheme")
                model: [TranslationManager.translate("community.scheme.all", "All schemes"),
                        TranslationManager.translate("community.scheme.dark", "Dark"),
                        TranslationManager.translate("community.scheme.light", "Light")]
                currentIndex: Math.max(0, communityBrowser.filterSchemes.indexOf(communityBrowser.filterScheme))
                onActivated: function(index) {
                    communityBrowser.filterScheme = communityBrowser.filterSchemes[index]
                    communityBrowser.refreshResults()
                }
            }

            // Action filter: the layout action catalog, the same list both editors offer.
            StyledComboBox {
                id: actionFilter
                visible: communityBrowser.filterType !== "theme"
                readonly property var catalogActions: LayoutActions.pickerActions("idle", true)
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.action", "Action filter")
                model: [TranslationManager.translate("community.action.any", "Any Action")]
                       .concat(catalogActions.map(a => a.label))
                onCurrentIndexChanged: {
                    if (!communityBrowser.filtersReady) return
                    communityBrowser.filterAction = currentIndex > 0 ? catalogActions[currentIndex - 1].id : ""
                    communityBrowser.refreshResults()
                }
            }

            // Sort
            StyledComboBox {
                id: sortFilter
                Layout.preferredWidth: Theme.scaled(120)
                accessibleLabel: TranslationManager.translate("community.filter.sort", "Sort order")
                model: [TranslationManager.translate("community.sort.newest", "Newest"), TranslationManager.translate("community.sort.popular", "Most Popular")]
                onCurrentIndexChanged: {
                    if (!communityBrowser.filtersReady) return
                    communityBrowser.sortBy = communityBrowser.filterSorts[currentIndex]
                    communityBrowser.refreshResults()
                }
            }
        }

        // Loading indicator
        Text {
            visible: communityBrowser.browseRequestId !== ""
            text: TranslationManager.translate("community.status.loading", "Loading...")
            color: Theme.primaryColor
            font: Theme.bodyFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Results grid
        GridView {
            id: resultsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: Theme.scaled(300)
            cellHeight: Theme.scaled(200)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            onContentHeightChanged: {
                communityBrowser.restoreViewportWhenLayoutReady()
                communityBrowser.loadNextPageIfNeeded()
            }
            onHeightChanged: communityBrowser.restoreViewportWhenLayoutReady()

            model: communityBrowser.communityEntries

            delegate: LibraryItemCard {
                id: communityCard
                required property var modelData

                width: resultsGrid.cellWidth - Theme.scaled(8)
                height: resultsGrid.cellHeight - Theme.scaled(8)
                entryData: communityCard.modelData
                communityEntry: true
                displayMode: 0
                isSelected: communityBrowser.selectedEntryId === (communityCard.modelData.id || "")

                onPreviewUnavailable: function(entryId) {
                    communityBrowser.omitEntryWithoutPreview(entryId)
                }

                onClicked: {
                    var id = communityCard.modelData.id || ""
                    if (communityBrowser._restoringNavigationContext)
                        communityBrowser.cancelNavigationRestore()
                    communityBrowser.selectedEntryId =
                        communityBrowser.selectedEntryId === id ? "" : id
                }
                onDoubleClicked: {
                    communityBrowser.downloadEntry(communityCard.modelData.id)
                }
            }

            // Load more when reaching bottom
            onAtYEndChanged: communityBrowser.loadNextPageIfNeeded()

            // Empty state
            Text {
                visible: resultsGrid.count === 0 && communityBrowser.browseRequestId === ""
                anchors.centerIn: parent
                text: communityBrowser.previewsUnavailable
                    ? TranslationManager.translate("community.empty.previews", "No previews are available right now.")
                    : communityBrowser.filterType || communityBrowser.filterVariable || communityBrowser.filterAction
                    ? TranslationManager.translate("community.empty.filtered", "No entries match your filters.")
                    : TranslationManager.translate("community.empty.none", "Community library is empty.\nBe the first to share a widget!")
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Download feedback
    Connections {
        target: LibraryService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (operation === "browseCommunity") {
                const requestMatched = requestId === communityBrowser.browseRequestId
                const restoreStep = CommunityBrowseNavigation.restoreStep(
                    communityBrowser._restoringNavigationContext, requestMatched, success,
                    communityBrowser.browseRequestedPage, communityBrowser._restoreTargetPage,
                    result && result.total ? result.total : 0, 20)
                if (restoreStep === "ignore") return
                communityBrowser.browseRequestId = ""
                if (communityBrowser.browseAgain) {
                    communityBrowser.browseAgain = false
                    communityBrowser.requestBrowsePage(communityBrowser.browseRequestedPage)
                    return
                }
                if (success) {
                    const pageEntries = result.entries || []
                    communityBrowser.currentPage = communityBrowser.browseRequestedPage
                    communityBrowser.communityEntries = communityBrowser.browseRequestedPage === 1
                        ? pageEntries : communityBrowser.communityEntries.concat(pageEntries)
                    communityBrowser.totalCommunityResults = result.total || 0
                    if (restoreStep === "next") {
                        communityBrowser.requestBrowsePage(communityBrowser.currentPage + 1)
                        return
                    }
                    if (restoreStep === "apply") {
                        communityBrowser.applyRestoredNavigationContext()
                        return
                    }
                } else communityBrowser.showDownloadToast(reason || TranslationManager.translate("community.toast.loadFailed", "Unable to load community library"))
                if (restoreStep === "cancel") {
                    communityBrowser.cancelNavigationRestore()
                    if (communityBrowser.currentPage < 1)
                        communityBrowser.currentPage = 1
                }
                return
            }
            if (requestId !== communityBrowser.downloadRequestId || operation !== "download") return
            communityBrowser.downloadRequestId = ""
            if (!success) {
                communityBrowser.showDownloadToast(TranslationManager.translate("community.toast.failed", "Download failed: %1").arg(reason))
            } else if (result.alreadyExists) {
                communityBrowser.showDownloadToast(TranslationManager.translate("community.toast.duplicate", "Already in your library"))
            } else {
                communityBrowser.showDownloadToast(TranslationManager.translate("community.toast.added", "Added to your library!"))
            }
        }
    }

    // Simple toast notification
    Rectangle {
        id: downloadToast
        visible: false
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastText.implicitWidth + Theme.scaled(32)
        height: Theme.scaled(40)
        radius: Theme.scaled(20)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1

        property alias text: toastText.text

        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }

    Timer {
        id: downloadToastTimer
        interval: 3000
        onTriggered: downloadToast.visible = false
    }

    // Bottom navigation bar
    BottomBar {
        id: communityBottomBar
        title: TranslationManager.translate("community.title", "Community")
        onBackClicked: AppShell.backRequested()

        // Download button
        Rectangle {
            property bool downloadEnabled: communityBrowser.selectedEntryId !== "" && communityBrowser.downloadRequestId === ""
            width: downloadLabel.implicitWidth + Theme.scaled(32)
            height: Theme.scaled(40)
            radius: Theme.scaled(20)
            color: downloadEnabled ? communityBottomBar.contentColor : Qt.rgba(communityBottomBar.contentColor.r, communityBottomBar.contentColor.g, communityBottomBar.contentColor.b, 0.15)

            Accessible.role: Accessible.Button
            Accessible.name: communityBrowser.downloadRequestId !== ""
                             ? TranslationManager.translate("community.downloading", "Downloading")
                             : TranslationManager.translate("community.addToLibrary", "Add to Library")
            Accessible.focusable: true
            Accessible.onPressAction: downloadArea.clicked(null)

            Text {
                id: downloadLabel
                anchors.centerIn: parent
                text: communityBrowser.downloadRequestId !== ""
                      ? TranslationManager.translate("community.downloadingEllipsis", "Downloading...")
                      : TranslationManager.translate("community.addToLibrary", "Add to Library")
                color: parent.downloadEnabled ? communityBottomBar.color : Qt.rgba(communityBottomBar.contentColor.r, communityBottomBar.contentColor.g, communityBottomBar.contentColor.b, 0.5)
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: true
                Accessible.ignored: true
            }

            MouseArea {
                id: downloadArea
                anchors.fill: parent
                enabled: parent.downloadEnabled
                onClicked: communityBrowser.downloadEntry(communityBrowser.selectedEntryId)
            }
        }

        Text {
            visible: communityBrowser.communityEntries.length > 0
            text: TranslationManager.translate("community.entryCount", "%1 entries").arg(communityBrowser.communityEntries.length)
            color: communityBottomBar.contentColor
            font: Theme.captionFont
        }
    }

    function refreshResults() {
        cancelNavigationRestore()
        currentPage = 1
        selectedEntryId = ""
        resultsGrid.contentY = 0
        communityEntries = []
        totalCommunityResults = 0
        previewsUnavailable = false
        requestBrowsePage(1)
    }

    function restoreNavigationContext() {
        cancelNavigationRestore()
        _restoringNavigationContext = true
        _restoreTargetPage = Math.max(1, Math.min(initialPage, 100))
        _restoreSelectedEntryId = initialSelectedEntryId
        _restoreContentY = initialContentY
        currentPage = 0
        selectedEntryId = ""
        communityEntries = []
        totalCommunityResults = 0
        previewsUnavailable = false
        requestBrowsePage(1)
    }

    function cancelNavigationRestore() {
        _restoringNavigationContext = false
        _restoreViewportPending = false
        _restoreTargetPage = 1
        _restoreSelectedEntryId = ""
        _restoreContentY = 0
    }

    function applyRestoredNavigationContext() {
        const requestedSelection = _restoreSelectedEntryId
        const foundSelection = communityEntries.some(function(entry) {
            return entry && entry.id === requestedSelection
        })
        selectedEntryId = foundSelection ? requestedSelection : ""
        _restoreViewportPending = true
        // Flush frame-batched model changes before clamping the saved offset
        // (Qt 6.12 qtdeclarative/src/quick/items/qquickgridview.cpp:2782-2792).
        // Height/content-height changes retry until view geometry is usable.
        resultsGrid.forceLayout()
        restoreViewportWhenLayoutReady()
    }

    function restoreViewportWhenLayoutReady() {
        if (!_restoreViewportPending || !_restoringNavigationContext) return
        if (_restoreContentY > 0 && communityEntries.length > 0
                && (resultsGrid.height <= 0 || resultsGrid.contentHeight <= 0))
            return
        const maxContentY = Math.max(0, resultsGrid.contentHeight - resultsGrid.height)
        resultsGrid.contentY = Math.max(0, Math.min(_restoreContentY, maxContentY))
        cancelNavigationRestore()
    }

    function requestBrowsePage(page) {
        if (browseRequestId !== "") {
            browseAgain = true
            browseRequestedPage = page
            return
        }
        browseRequestedPage = page
        browseRequestId = LibraryService.newRequestId()
        LibraryService.execute("browseCommunity", {
            type: filterType, variable: filterType === "theme" ? "" : filterVariable,
            action: filterType === "theme" ? "" : filterAction,
            search: filterType === "theme" && filterScheme !== "" ? "scheme:" + filterScheme : "", sort: sortBy, page: page
        }, browseRequestId)
    }

    function omitEntryWithoutPreview(id) {
        if (id === "") return
        previewsUnavailable = true
        if (selectedEntryId === id) selectedEntryId = ""
        communityEntries = communityEntries.filter(function(entry) { return entry.id !== id })
        Qt.callLater(communityBrowser.loadNextPageIfNeeded)
    }

    function loadNextPageIfNeeded() {
        if (!_restoringNavigationContext && (resultsGrid.atYEnd || communityEntries.length === 0) && currentPage > 0
                && browseRequestId === "" && currentPage < Math.ceil(totalCommunityResults / 20))
            requestBrowsePage(currentPage + 1)
    }

    function downloadEntry(serverId) {
        if (downloadRequestId !== "" || serverId === "") return
        downloadRequestId = LibraryService.newRequestId()
        LibraryService.execute("download", { serverId: serverId }, downloadRequestId)
    }

    function showDownloadToast(message) {
        downloadToast.text = message
        downloadToast.visible = true
        downloadToastTimer.restart()
    }
}
