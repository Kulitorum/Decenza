import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotHistoryPage
    objectName: "shotHistoryPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Tap outside to dismiss keyboard
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            if (searchField.activeFocus) {
                searchField.focus = false
                Qt.inputMethod.hide()
            }
        }
    }

    property var selectedShots: []
    property int currentOffset: 0
    property int pageSize: 50
    property bool hasMoreShots: true
    property bool isLoadingMore: false
    property int filteredTotalCount: 0

    // External filter passed from other pages (e.g., AutoFavoritesPage "Show" button)
    property var initialFilter: null
    property bool _populatingSearch: false

    // Sort settings
    property string sortField: Settings.shotHistorySortField
    property string sortDirection: Settings.shotHistorySortDirection

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
        root.currentPageTitle = TranslationManager.translate("shothistory.title", "Shot History")
        if (initialFilter) {
            // Populate search field with filter terms so user can edit/save
            var parts = []
            if (initialFilter.beanBrand) parts.push(initialFilter.beanBrand)
            if (initialFilter.beanType) parts.push(initialFilter.beanType)
            if (initialFilter.profileName) parts.push(initialFilter.profileName)
            if (initialFilter.grinderModel) parts.push(initialFilter.grinderModel)
            if (initialFilter.grinderSetting) parts.push(initialFilter.grinderSetting)
            _populatingSearch = true
            searchField.text = parts.join(" ")
            searchField.lastTriggeredText = searchField.text.trim()
            _populatingSearch = false
        }
        loadShots()
    }

    function loadShots() {
        loadMoreTimer.stop()
        isLoadingMore = false
        currentOffset = 0
        hasMoreShots = true
        shotListView.contentY = 0
        var filter = buildFilter()
        MainController.shotHistory.requestShotsFiltered(filter, 0, pageSize)
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
            loadShots()
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
                currentOffset += results.length
                hasMoreShots = results.length >= pageSize
                isLoadingMore = false
            } else {
                // loadShots result (full refresh)
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
                currentOffset = results.length
                hasMoreShots = results.length >= pageSize
            }
            filteredTotalCount = totalCount
        }
    }

    function buildFilter() {
        var filter = {}
        if (searchField.text.length > 0) {
            var searchText = searchField.text

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

            // Strip any remaining keyword tokens (e.g. duplicate dose:18 dose:20)
            searchText = searchText.replace(/\b(rating|dose|yield|time|tds|ey):\d+(?:\.\d+)?(?:-\d+(?:\.\d+)?|\+)?/g, "")

            // Pass remaining text (after stripping keywords) as FTS search
            searchText = searchText.trim().replace(/\s+/g, " ")
            if (searchText.length > 0 && !initialFilter) {
                filter.searchText = searchText
            }
        }
        // Merge initialFilter fields (from AutoFavoritesPage "Show" button etc.)
        if (initialFilter) {
            var filterFields = ["profileName", "beanBrand", "beanType", "grinderModel", "grinderSetting"]
            for (var k = 0; k < filterFields.length; k++) {
                var field = filterFields[k]
                if (initialFilter[field] !== undefined && initialFilter[field] !== "")
                    filter[field] = initialFilter[field]
            }
        }

        filter.sortField = sortField
        filter.sortDirection = sortDirection
        return filter
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
        pageStack.push(Qt.resolvedUrl("ShotComparisonPage.qml"))
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
        pageStack.push(Qt.resolvedUrl("ShotDetailPage.qml"), {
            shotId: shotId,
            shotIds: shotIds
        })
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
            visible: selectedShots.length > 0

            Text {
                text: selectedShots.length + " " + TranslationManager.translate("shothistory.selected", "selected")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.clear", "Clear")
                accessibleName: TranslationManager.translate("shotHistory.clearSelection", "Clear shot selection")
                onClicked: clearSelection()
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
                enabled: selectedShots.length >= 2
                onClicked: openComparison()
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
                // Disable predictive text / autocorrect — forces IME to commit each
                // character individually. Without this, the IME holds composing text
                // and commits the entire word at once on space, which triggers a blank screen.
                inputMethodHints: Qt.ImhNoPredictiveText
                property string lastTriggeredText: ""
                onTextChanged: {
                    var trimmed = text.trim()
                    if (trimmed !== lastTriggeredText) {
                        lastTriggeredText = trimmed
                        // User edited the search field — drop exact-match filter, use FTS
                        if (!_populatingSearch && initialFilter)
                            initialFilter = null
                        if (!_populatingSearch)
                            searchTimer.restart()
                    }
                }

                // Clear button
                Text {
                    id: searchClearButton
                    visible: searchField.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(10)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u2715"
                    font.pixelSize: Theme.scaled(16)
                    color: Theme.textSecondaryColor

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

            AccessibleButton {
                text: TranslationManager.translate("shothistory.keywords", "Keywords")
                accessibleName: TranslationManager.translate("shothistory.searchhelp", "Search syntax help")
                onClicked: searchHelpDialog.open()
            }

            AccessibleButton {
                text: TranslationManager.translate("shothistory.save", "Save")
                accessibleName: TranslationManager.translate("shothistory.saveSearch", "Save current search")
                enabled: searchField.text.trim().length > 0
                         && Settings.savedSearches.indexOf(searchField.text.trim()) === -1
                onClicked: Settings.addSavedSearch(searchField.text.trim())
            }

            AccessibleButton {
                text: "\u2630 " + TranslationManager.translate("shothistory.saved", "Saved")
                accessibleName: TranslationManager.translate("shothistory.openSavedSearches", "Open saved searches")
                enabled: Settings.savedSearches.length > 0
                onClicked: savedSearchesDialog.open()
            }

            // Sort field button
            AccessibleButton {
                text: sortFieldLabels[sortField] || "Date"
                accessibleName: TranslationManager.translate("shothistory.sortBy", "Sort by %1").arg(sortFieldLabels[sortField] || "Date")
                onClicked: sortPickerDialog.open()
            }

            // Sort direction button
            AccessibleButton {
                text: sortDirection === "DESC" ? "\u25BC" : "\u25B2"
                accessibleName: sortDirection === "DESC"
                    ? TranslationManager.translate("shothistory.sortDescending", "Sort descending, tap to sort ascending")
                    : TranslationManager.translate("shothistory.sortAscending", "Sort ascending, tap to sort descending")
                onClicked: {
                    sortDirection = (sortDirection === "DESC") ? "ASC" : "DESC"
                    Settings.shotHistorySortDirection = sortDirection
                    loadShots()
                }
            }

            Timer {
                id: searchTimer
                interval: 300
                onTriggered: loadShots()
            }
        }

        // Filter banner (shown when initialFilter is active)
        Rectangle {
            Layout.fillWidth: true
            height: filterBannerRow.implicitHeight + Theme.spacingSmall * 2
            radius: Theme.scaled(8)
            color: Qt.alpha(Theme.primaryColor, 0.15)
            visible: initialFilter !== null
            Accessible.ignored: true

            RowLayout {
                id: filterBannerRow
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                spacing: Theme.spacingSmall

                Text {
                    text: {
                        if (!initialFilter) return ""
                        var parts = []
                        if (initialFilter.beanBrand) parts.push(initialFilter.beanBrand)
                        if (initialFilter.beanType) parts.push(initialFilter.beanType)
                        if (initialFilter.profileName) parts.push(initialFilter.profileName)
                        if (initialFilter.grinderModel) {
                            var g = initialFilter.grinderModel
                            if (initialFilter.grinderSetting) g += " @ " + initialFilter.grinderSetting
                            parts.push(g)
                        }
                        return TranslationManager.translate("shothistory.filteredBy", "Filtered:") + " " + parts.join(" \u00b7 ")
                    }
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.primaryColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                AccessibleButton {
                    text: "\u2715 " + TranslationManager.translate("shothistory.clearFilter", "Clear")
                    accessibleName: TranslationManager.translate("shothistory.clearFilterAccessible", "Clear favorites filter")
                    onClicked: clearInitialFilter()
                }
            }
        }

        // Shot count
        Text {
            text: {
                var loaded = shotListModel.count
                var filtered = filteredTotalCount
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
                    Qt.inputMethod.hide()
                }
            }

            // Infinite scroll - load more when near bottom
            onContentYChanged: {
                if (!isLoadingMore && hasMoreShots && contentHeight > 0) {
                    var threshold = contentHeight - height - Theme.scaled(200)
                    if (contentY > threshold) {
                        loadMoreTimer.restart()
                    }
                }
            }

            Timer {
                id: loadMoreTimer
                interval: 100
                onTriggered: loadMoreShots()
            }

            delegate: Rectangle {
                id: shotDelegate
                width: shotListView.width
                height: Math.max(Theme.scaled(90), shotContentRow.implicitHeight + Theme.spacingMedium * 2)
                radius: Theme.cardRadius
                color: isSelected(model.id) ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                border.color: isSelected(model.id) ? Theme.primaryColor : "transparent"
                border.width: isSelected(model.id) ? 2 : 0

                property int shotEnjoyment: model.enjoyment || 0

                // Accessibility: row is a button whose primary action opens shot detail.
                // Note: visual tap toggles selection (line 696); TalkBack double-tap opens detail
                // because detail view is the more useful primary action for screen reader users.
                Accessible.role: Accessible.Button
                Accessible.name: {
                    var parts = []
                    if (model.profileName) parts.push(model.profileName)
                    if (model.dateTime) parts.push(model.dateTime)
                    var bean = (model.beanBrand || "") + (model.beanType ? " " + model.beanType : "")
                    if (bean) parts.push(bean)
                    var doseVal = model.doseWeight || 0
                    var yieldVal = model.finalWeight || 0
                    if (doseVal > 0 && yieldVal > 0)
                        parts.push(doseVal.toFixed(1) + "g to " + yieldVal.toFixed(1) + "g")
                    if (shotDelegate.shotEnjoyment > 0) parts.push(shotDelegate.shotEnjoyment + "%")
                    return parts.join(", ")
                }
                Accessible.focusable: true
                Accessible.onPressAction: openShotDetail(model.id)

                RowLayout {
                    id: shotContentRow
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Selection checkbox
                    CheckBox {
                        checked: isSelected(model.id)
                        onClicked: toggleSelection(model.id)
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: TranslationManager.translate("shothistory.accessible.compare", "Compare")
                        Accessible.checked: checked
                        Accessible.focusable: true

                        indicator: Rectangle {
                            implicitWidth: Theme.scaled(24)
                            implicitHeight: Theme.scaled(24)
                            radius: Theme.scaled(4)
                            color: parent.checked ? Theme.primaryColor : "transparent"
                            border.color: parent.checked ? Theme.primaryColor : Theme.borderColor
                            border.width: 2

                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"
                                font.pixelSize: Theme.scaled(16)
                                color: Theme.textColor
                                visible: parent.parent.checked
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Shot info — all text is decorative (already summarized in row Accessible.name)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Text {
                                text: model.dateTime || ""
                                font: Theme.subtitleFont
                                color: Theme.textColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: {
                                    var name = model.profileName || ""
                                    var tempOvr = model.temperatureOverride || 0
                                    if (tempOvr > 0) {
                                        return name + " (" + Math.round(tempOvr) + "\u00B0C)"
                                    }
                                    return name
                                }
                                font: Theme.labelFont
                                color: Theme.primaryColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                        }

                        Text {
                            text: {
                                var bean = (model.beanBrand || "") + (model.beanType ? " " + model.beanType : "")
                                var grind = model.grinderSetting || ""
                                if (bean && grind) return bean + " (" + grind + ")"
                                if (bean) return bean
                                if (grind) return "Grind: " + grind
                                return ""
                            }
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
                                    var dose = (model.doseWeight || 0).toFixed(1)
                                    var actual = (model.finalWeight || 0).toFixed(1)
                                    var yieldText = actual + "g"
                                    var yieldOvr = model.yieldOverride || 0
                                    if (yieldOvr > 0 && Math.abs(yieldOvr - model.finalWeight) > 0.5) {
                                        yieldText = actual + "g (" + Math.round(yieldOvr) + "g)"
                                    }
                                    return dose + "g \u2192 " + yieldText
                                }
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: (model.duration || 0).toFixed(1) + "s"
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Image {
                                source: "qrc:/emoji/2601.svg"  // Cloud icon
                                sourceSize.width: Theme.scaled(16)
                                sourceSize.height: Theme.scaled(16)
                                visible: model.hasVisualizerUpload
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
                        width: loadButtonText.implicitWidth + Theme.scaled(20)
                        height: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.warningColor
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("shothistory.accessible.load", "Load profile")
                        Accessible.focusable: true
                        Accessible.onPressAction: loadArea.clicked(null)

                        Text {
                            id: loadButtonText
                            anchors.centerIn: parent
                            text: "Load"
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: "white"
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: loadArea
                            anchors.fill: parent
                            onClicked: {
                                MainController.loadShotWithMetadata(model.id)
                                pageStack.pop()
                            }
                        }
                    }

                    // Edit button (green circle with E)
                    Rectangle {
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: "#2E7D32"
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("shothistory.accessible.edit", "Edit shot")
                        Accessible.focusable: true
                        Accessible.onPressAction: editArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "E"
                            font.pixelSize: Theme.scaled(18)
                            font.bold: true
                            color: "white"
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: editArea
                            anchors.fill: parent
                            onClicked: {
                                pageStack.push(Qt.resolvedUrl("PostShotReviewPage.qml"), { editShotId: model.id })
                            }
                        }
                    }

                    // Detail arrow
                    Rectangle {
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
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
                            color: "white"
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: detailArea
                            anchors.fill: parent
                            onClicked: openShotDetail(model.id)
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: toggleSelection(model.id)
                    onPressAndHold: openShotDetail(model.id)
                }
            }

            footer: Item {
                width: shotListView.width
                height: isLoadingMore ? Theme.scaled(50) : 0
                visible: isLoadingMore

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
        onBackClicked: root.goBack()
    }

    Dialog {
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
                text: TranslationManager.translate("shothistory.deleteconfirmmessage", "Permanently delete %1 shot(s) from history?").arg(selectedShots.length)
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
                        deleteSelectedShots()
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
        Qt.inputMethod.show()
    }

    // Saved searches dialog
    Dialog {
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
                model: Settings.savedSearches
                spacing: Theme.scaled(2)

                delegate: Rectangle {
                    readonly property bool _accessibilityMode: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

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
                                text: modelData
                                font: Theme.bodyFont
                                color: Theme.textColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }

                            // Inline delete button (hidden in accessibility mode)
                            Rectangle {
                                visible: !_accessibilityMode
                                width: Theme.scaled(28)
                                height: Theme.scaled(28)
                                radius: Theme.scaled(14)
                                color: deleteArea.pressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor

                                Text {
                                    anchors.centerIn: parent
                                    text: "\u2715"
                                    font.pixelSize: Theme.scaled(14)
                                    font.bold: true
                                    color: "white"
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: deleteArea
                                    anchors.fill: parent
                                    onClicked: {
                                        Settings.removeSavedSearch(modelData)
                                        if (Settings.savedSearches.length === 0) {
                                            savedSearchesDialog.close()
                                        }
                                    }
                                }
                            }
                        }

                        // Separate delete button row for accessibility mode (outside row bounds)
                        AccessibleButton {
                            visible: _accessibilityMode
                            text: TranslationManager.translate("shothistory.delete", "Delete")
                            accessibleName: TranslationManager.translate("shothistory.deleteSavedSearch", "Delete search: %1").arg(modelData)
                            destructive: true
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            Layout.leftMargin: Theme.scaled(10)
                            Layout.rightMargin: Theme.scaled(4)
                            onClicked: {
                                Settings.removeSavedSearch(modelData)
                                if (Settings.savedSearches.length === 0) {
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
                            searchField.text = modelData
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
    Dialog {
        id: sortPickerDialog
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
                text: TranslationManager.translate("shothistory.sortByTitle", "Sort By")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            ListView {
                id: sortFieldList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Theme.scaled(400))
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(10)
                Layout.rightMargin: Theme.scaled(10)
                clip: true
                model: sortFieldKeys
                spacing: Theme.scaled(2)

                delegate: Rectangle {
                    width: sortFieldList.width
                    height: Theme.scaled(44)
                    radius: Theme.scaled(6)
                    color: sortItemArea.pressed ? Qt.darker(Theme.surfaceColor, 1.1) : "transparent"

                    Accessible.role: Accessible.Button
                    Accessible.name: sortFieldLabels[modelData] || modelData
                    Accessible.focusable: true
                    Accessible.onPressAction: sortItemArea.clicked(null)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(10)
                        anchors.rightMargin: Theme.scaled(10)
                        spacing: Theme.spacingSmall

                        Text {
                            text: sortFieldLabels[modelData] || modelData
                            font: Theme.bodyFont
                            color: Theme.textColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }

                        Text {
                            text: "\u2713"
                            font.pixelSize: Theme.scaled(18)
                            color: Theme.primaryColor
                            visible: sortField === modelData
                            Accessible.ignored: true
                        }
                    }

                    MouseArea {
                        id: sortItemArea
                        anchors.fill: parent
                        onClicked: {
                            sortField = modelData
                            sortDirection = defaultSortDirections[modelData] || "DESC"
                            Settings.shotHistorySortField = sortField
                            Settings.shotHistorySortDirection = sortDirection
                            sortPickerDialog.close()
                            loadShots()
                        }
                    }
                }
            }

            // Close button
            AccessibleButton {
                text: TranslationManager.translate("shothistory.close", "Close")
                accessibleName: TranslationManager.translate("shothistory.closeSortPicker", "Close sort picker")
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.scaled(12)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: sortPickerDialog.close()
            }
        }
    }

    // Search syntax help dialog
    Dialog {
        id: searchHelpDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(420), shotHistoryPage.width - Theme.scaled(40))
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
                Text { text: TranslationManager.translate("shothistory.helpheaderfilters", "Filters"); font.bold: true; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textColor; Accessible.ignored: true }
                Text { text: TranslationManager.translate("shothistory.helpheaderexample", "Example"); font.bold: true; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textColor; Accessible.ignored: true }

                // Data rows — keyword column is tappable to insert into search
                Rectangle {
                    color: ratingArea.pressed ? Theme.surfaceColor : "transparent"
                    radius: Theme.scaled(4)
                    implicitWidth: ratingLabel.implicitWidth + Theme.scaled(8)
                    implicitHeight: ratingLabel.implicitHeight + Theme.scaled(4)
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg("rating:")
                    Accessible.focusable: true
                    Accessible.onPressAction: ratingArea.clicked(null)
                    Text { id: ratingLabel; text: "rating:"; anchors.centerIn: parent; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.primaryColor; font.bold: true; Accessible.ignored: true }
                    MouseArea { id: ratingArea; anchors.fill: parent; onClicked: insertSearchKeyword("rating:") }
                }
                Text { text: TranslationManager.translate("shothistory.helprating", "Enjoyment (0-100)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
                Text { text: "rating:70+"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }

                Rectangle {
                    color: doseArea.pressed ? Theme.surfaceColor : "transparent"
                    radius: Theme.scaled(4)
                    implicitWidth: doseLabel.implicitWidth + Theme.scaled(8)
                    implicitHeight: doseLabel.implicitHeight + Theme.scaled(4)
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg("dose:")
                    Accessible.focusable: true
                    Accessible.onPressAction: doseArea.clicked(null)
                    Text { id: doseLabel; text: "dose:"; anchors.centerIn: parent; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.primaryColor; font.bold: true; Accessible.ignored: true }
                    MouseArea { id: doseArea; anchors.fill: parent; onClicked: insertSearchKeyword("dose:") }
                }
                Text { text: TranslationManager.translate("shothistory.helpdose", "Dose weight (g)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
                Text { text: "dose:16-18"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }

                Rectangle {
                    color: yieldArea.pressed ? Theme.surfaceColor : "transparent"
                    radius: Theme.scaled(4)
                    implicitWidth: yieldLabel.implicitWidth + Theme.scaled(8)
                    implicitHeight: yieldLabel.implicitHeight + Theme.scaled(4)
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg("yield:")
                    Accessible.focusable: true
                    Accessible.onPressAction: yieldArea.clicked(null)
                    Text { id: yieldLabel; text: "yield:"; anchors.centerIn: parent; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.primaryColor; font.bold: true; Accessible.ignored: true }
                    MouseArea { id: yieldArea; anchors.fill: parent; onClicked: insertSearchKeyword("yield:") }
                }
                Text { text: TranslationManager.translate("shothistory.helpyield", "Yield weight (g)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
                Text { text: "yield:30-40"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }

                Rectangle {
                    color: timeArea.pressed ? Theme.surfaceColor : "transparent"
                    radius: Theme.scaled(4)
                    implicitWidth: timeLabel.implicitWidth + Theme.scaled(8)
                    implicitHeight: timeLabel.implicitHeight + Theme.scaled(4)
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg("time:")
                    Accessible.focusable: true
                    Accessible.onPressAction: timeArea.clicked(null)
                    Text { id: timeLabel; text: "time:"; anchors.centerIn: parent; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.primaryColor; font.bold: true; Accessible.ignored: true }
                    MouseArea { id: timeArea; anchors.fill: parent; onClicked: insertSearchKeyword("time:") }
                }
                Text { text: TranslationManager.translate("shothistory.helptime", "Duration (seconds)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
                Text { text: "time:25-35"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }

                Rectangle {
                    color: tdsArea.pressed ? Theme.surfaceColor : "transparent"
                    radius: Theme.scaled(4)
                    implicitWidth: tdsLabel.implicitWidth + Theme.scaled(8)
                    implicitHeight: tdsLabel.implicitHeight + Theme.scaled(4)
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg("tds:")
                    Accessible.focusable: true
                    Accessible.onPressAction: tdsArea.clicked(null)
                    Text { id: tdsLabel; text: "tds:"; anchors.centerIn: parent; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.primaryColor; font.bold: true; Accessible.ignored: true }
                    MouseArea { id: tdsArea; anchors.fill: parent; onClicked: insertSearchKeyword("tds:") }
                }
                Text { text: "TDS"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
                Text { text: "tds:1.3-1.5"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }

                Rectangle {
                    color: eyArea.pressed ? Theme.surfaceColor : "transparent"
                    radius: Theme.scaled(4)
                    implicitWidth: eyLabel.implicitWidth + Theme.scaled(8)
                    implicitHeight: eyLabel.implicitHeight + Theme.scaled(4)
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg("ey:")
                    Accessible.focusable: true
                    Accessible.onPressAction: eyArea.clicked(null)
                    Text { id: eyLabel; text: "ey:"; anchors.centerIn: parent; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.primaryColor; font.bold: true; Accessible.ignored: true }
                    MouseArea { id: eyArea; anchors.fill: parent; onClicked: insertSearchKeyword("ey:") }
                }
                Text { text: TranslationManager.translate("shothistory.helpey", "Extraction yield (%)"); font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
                Text { text: "ey:18-22"; font.pixelSize: Theme.labelFont.pixelSize; color: Theme.textSecondaryColor; Accessible.ignored: true }
            }

            // Syntax explanation
            Text {
                text: TranslationManager.translate("shothistory.searchhelpsyntax",
                    "Syntax: N (exact), N-M (range), N+ (minimum)\nCombine keywords with text: ethiopia dose:18 rating:70+")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(12)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            // Close button
            AccessibleButton {
                text: TranslationManager.translate("shothistory.close", "Close")
                accessibleName: TranslationManager.translate("shothistory.closeHelp", "Close search help")
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.scaled(12)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: searchHelpDialog.close()
            }
        }
    }

}
