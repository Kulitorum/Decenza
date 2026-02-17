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

    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("shothistory.title", "Shot History")
        refreshFilterOptions()
        loadShots()
    }

    function loadShots() {
        loadMoreTimer.stop()
        currentOffset = 0
        hasMoreShots = true
        var filter = buildFilter()
        var shots = MainController.shotHistory.getShotsFiltered(filter, 0, pageSize)

        shotListView.contentY = 0

        // In-place model update to avoid flicker
        var i
        for (i = 0; i < shots.length; i++) {
            if (i < shotListModel.count) {
                shotListModel.set(i, shots[i])
            } else {
                shotListModel.append(shots[i])
            }
        }
        while (shotListModel.count > shots.length) {
            shotListModel.remove(shotListModel.count - 1)
        }

        currentOffset = shots.length
        hasMoreShots = shots.length >= pageSize
        filteredTotalCount = MainController.shotHistory.getFilteredShotCount(filter)
    }

    function loadMoreShots() {
        if (isLoadingMore || !hasMoreShots) return
        isLoadingMore = true
        var filter = buildFilter()
        var shots = MainController.shotHistory.getShotsFiltered(filter, currentOffset, pageSize)
        for (var i = 0; i < shots.length; i++) {
            shotListModel.append(shots[i])
        }
        currentOffset += shots.length
        hasMoreShots = shots.length >= pageSize
        isLoadingMore = false
    }

    // Get filter values from the model arrays directly (more reliable than currentText)
    property var profileOptions: []
    property var roasterOptions: []
    property var beanOptions: []

    // Track current selections by value (not index) to preserve across option updates
    property string selectedProfile: ""
    property string selectedRoaster: ""
    property string selectedBean: ""

    function refreshFilterOptions() {
        // Initial load - get all options
        var profiles = MainController.shotHistory.getDistinctProfiles()
        var roasters = MainController.shotHistory.getDistinctBeanBrands()
        var beans = MainController.shotHistory.getDistinctBeanTypes()
        profileOptions = [TranslationManager.translate("shothistory.allprofiles", "All Profiles")].concat(profiles)
        roasterOptions = [TranslationManager.translate("shothistory.allroasters", "All Roasters")].concat(roasters)
        beanOptions = [TranslationManager.translate("shothistory.allbeans", "All Beans")].concat(beans)

        // Restore ComboBox indices from preserved selection values (e.g. after returning from shot detail)
        var pIdx = selectedProfile ? profileOptions.indexOf(selectedProfile) : 0
        profileFilter.currentIndex = pIdx >= 0 ? pIdx : 0
        var rIdx = selectedRoaster ? roasterOptions.indexOf(selectedRoaster) : 0
        roasterFilter.currentIndex = rIdx >= 0 ? rIdx : 0
        var bIdx = selectedBean ? beanOptions.indexOf(selectedBean) : 0
        beanFilter.currentIndex = bIdx >= 0 ? bIdx : 0
    }

    function updateCascadingFilters(changedFilter) {
        var filter = {}

        // Build filter from current selections
        if (selectedProfile) filter.profileName = selectedProfile
        if (selectedRoaster) filter.beanBrand = selectedRoaster
        if (selectedBean) filter.beanType = selectedBean

        // Update options for filters OTHER than the one that changed
        if (changedFilter !== "profile") {
            var profiles = MainController.shotHistory.getDistinctProfilesFiltered(filter)
            var allProfiles = [TranslationManager.translate("shothistory.allprofiles", "All Profiles")]
            profileOptions = allProfiles.concat(profiles)
            // Restore selection if still valid
            var pIdx = selectedProfile ? profileOptions.indexOf(selectedProfile) : 0
            profileFilter.currentIndex = pIdx >= 0 ? pIdx : 0
        }

        if (changedFilter !== "roaster") {
            var roasters = MainController.shotHistory.getDistinctBeanBrandsFiltered(filter)
            var allRoasters = [TranslationManager.translate("shothistory.allroasters", "All Roasters")]
            roasterOptions = allRoasters.concat(roasters)
            // Restore selection if still valid
            var rIdx = selectedRoaster ? roasterOptions.indexOf(selectedRoaster) : 0
            roasterFilter.currentIndex = rIdx >= 0 ? rIdx : 0
        }

        if (changedFilter !== "bean") {
            var beans = MainController.shotHistory.getDistinctBeanTypesFiltered(filter)
            var allBeans = [TranslationManager.translate("shothistory.allbeans", "All Beans")]
            beanOptions = allBeans.concat(beans)
            // Restore selection if still valid
            var bIdx = selectedBean ? beanOptions.indexOf(selectedBean) : 0
            beanFilter.currentIndex = bIdx >= 0 ? bIdx : 0
        }
    }

    function onProfileChanged() {
        selectedProfile = profileFilter.currentIndex > 0 ? profileOptions[profileFilter.currentIndex] : ""
        updateCascadingFilters("profile")
        loadShots()
    }

    function onRoasterChanged() {
        selectedRoaster = roasterFilter.currentIndex > 0 ? roasterOptions[roasterFilter.currentIndex] : ""
        updateCascadingFilters("roaster")
        loadShots()
    }

    function onBeanChanged() {
        selectedBean = beanFilter.currentIndex > 0 ? beanOptions[beanFilter.currentIndex] : ""
        updateCascadingFilters("bean")
        loadShots()
    }

    function buildFilter() {
        var filter = {}
        if (selectedProfile) {
            filter.profileName = selectedProfile
        }
        if (selectedRoaster) {
            filter.beanBrand = selectedRoaster
        }
        if (selectedBean) {
            filter.beanType = selectedBean
        }
        if (searchField.text.length > 0) {
            filter.searchText = searchField.text
        }
        return filter
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
        // Sort selected shots chronologically before adding
        var sortedShots = selectedShots.slice().sort(function(a, b) { return a - b })
        for (var i = 0; i < sortedShots.length; i++) {
            MainController.shotComparison.addShot(sortedShots[i])
        }
        pageStack.push(Qt.resolvedUrl("ShotComparisonPage.qml"))
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

            StyledComboBox {
                id: profileFilter
                Layout.preferredWidth: Theme.scaled(140)
                model: profileOptions
                accessibleLabel: TranslationManager.translate("shothistory.filter.profile", "Profile filter")
                onActivated: if (shotHistoryPage.visible) onProfileChanged()

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: profileFilter.displayText
                    font: Theme.labelFont
                    color: Theme.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                    elide: Text.ElideRight
                }
            }

            StyledComboBox {
                id: roasterFilter
                Layout.preferredWidth: Theme.scaled(140)
                model: roasterOptions
                accessibleLabel: TranslationManager.translate("shothistory.filter.roaster", "Roaster filter")
                onActivated: if (shotHistoryPage.visible) onRoasterChanged()

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: roasterFilter.displayText
                    font: Theme.labelFont
                    color: Theme.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                    elide: Text.ElideRight
                }
            }

            StyledComboBox {
                id: beanFilter
                Layout.preferredWidth: Theme.scaled(140)
                model: beanOptions
                accessibleLabel: TranslationManager.translate("shothistory.filter.bean", "Bean filter")
                onActivated: if (shotHistoryPage.visible) onBeanChanged()

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: beanFilter.displayText
                    font: Theme.labelFont
                    color: Theme.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                    elide: Text.ElideRight
                }
            }

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

            Timer {
                id: searchTimer
                interval: 300
                onTriggered: loadShots()
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
                height: Theme.scaled(90)
                radius: Theme.cardRadius
                color: isSelected(model.id) ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                border.color: isSelected(model.id) ? Theme.primaryColor : "transparent"
                border.width: isSelected(model.id) ? 2 : 0

                property int shotEnjoyment: model.enjoyment || 0

                // Accessibility: row is a button whose primary action opens shot detail
                Accessible.role: Accessible.Button
                Accessible.name: {
                    var parts = []
                    if (model.profileName) parts.push(model.profileName)
                    if (model.dateTime) parts.push(model.dateTime)
                    var bean = (model.beanBrand || "") + (model.beanType ? " " + model.beanType : "")
                    if (bean) parts.push(bean)
                    var doseVal = model.doseWeight || 0
                    var yieldVal = model.finalWeight || 0
                    if (doseVal > 0 || yieldVal > 0)
                        parts.push(doseVal.toFixed(1) + "g to " + yieldVal.toFixed(1) + "g")
                    if (shotDelegate.shotEnjoyment > 0) parts.push(shotDelegate.shotEnjoyment + "%")
                    return parts.join(", ")
                }
                Accessible.focusable: true
                Accessible.onPressAction: openShotDetail(model.id)

                RowLayout {
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

                            Text {
                                text: model.hasVisualizerUpload ? "\u2601" : ""
                                font.pixelSize: Theme.scaled(16)
                                color: Theme.successColor
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

}
