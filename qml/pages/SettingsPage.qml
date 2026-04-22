import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../components"

Page {
    id: settingsPage
    objectName: "settingsPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("settings.title", "Settings")

    // Requested tab to switch to (set before pushing page)
    property int requestedTabIndex: -1

    // Card to highlight after search navigation (cleared after use)
    property string highlightCardId: ""

    // Track which tabs have been visited (lazy-load: only load tab content on first visit)
    property var loadedTabs: ({})

    function markTabLoaded(index) {
        if (!(index in loadedTabs)) {
            // Must create a new object - reassigning the same reference
            // won't trigger QML property change notifications
            var tabs = Object.assign({}, loadedTabs)
            tabs[index] = true
            loadedTabs = tabs
        }
    }

    StackView.onActivating: {
        if (requestedTabIndex >= 0) {
            markTabLoaded(requestedTabIndex)
        }
    }

    // Switch to requested tab after page transition completes (page is fully laid out)
    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("settings.title", "Settings")
        if (requestedTabIndex >= 0) {
            markTabLoaded(requestedTabIndex)
            tabBar.currentIndex = requestedTabIndex
            requestedTabIndex = -1
        }
    }

    // Search button (left end of tab bar)
    Rectangle {
        id: searchButton
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.left: parent.left
        anchors.leftMargin: Theme.standardMargin
        width: Theme.scaled(44)
        height: tabBar.height
        color: searchMouseArea.containsMouse ? Qt.lighter(Theme.surfaceColor, 1.2) : Theme.surfaceColor
        radius: Theme.scaled(12)
        border.width: 1
        border.color: Theme.borderColor
        z: 3

        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("settings.search.button", "Search settings")
        Accessible.focusable: true
        Accessible.onPressAction: searchMouseArea.clicked(null)

        Image {
            anchors.centerIn: parent
            source: "qrc:/icons/search.svg"
            sourceSize.width: Theme.scaled(20)
            sourceSize.height: Theme.scaled(20)
            Accessible.ignored: true
        }

        MouseArea {
            id: searchMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: settingsSearchDialog.open()
        }
    }

    // Tab bar at top
    TabBar {
        id: tabBar
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.left: searchButton.right
        anchors.leftMargin: Theme.scaled(8)
        anchors.right: parent.right
        anchors.rightMargin: Theme.standardMargin
        z: 2

        property bool accessibilityCustomHandler: true

        onCurrentIndexChanged: {
            settingsPage.markTabLoaded(currentIndex)

            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                // Build tab names based on which tabs are visible
                var tabNames = [
                    TranslationManager.translate("settings.tab.connections", "Connections"),
                    TranslationManager.translate("settings.tab.machine", "Machine"),
                    TranslationManager.translate("settings.tab.calibration", "Calibration"),
                    TranslationManager.translate("settings.tab.historyData", "History & Data"),
                    TranslationManager.translate("settings.tab.themes", "Themes"),
                    TranslationManager.translate("settings.tab.layout", "Layout"),
                    TranslationManager.translate("settings.tab.screensaver", "Screensaver"),
                    TranslationManager.translate("settings.tab.visualizer", "Visualizer"),
                    TranslationManager.translate("settings.tab.ai", "AI"),
                    TranslationManager.translate("settings.tab.mqtt", "MQTT"),
                    TranslationManager.translate("settings.tab.languageAccess", "Language & Access")
                ]
                tabNames.push(TranslationManager.translate("settings.tab.about", "About"))
                if (Settings.isDebugBuild) tabNames.push(TranslationManager.translate("settings.tab.debug", "Debug"))
                if (currentIndex >= 0 && currentIndex < tabNames.length) {
                    AccessibilityManager.announce(TranslationManager.translate("settings.accessible.tabAnnounce", "%1 tab").arg(tabNames[currentIndex]))
                }
            }
        }

        // Override Material contentItem to remove the accent-colored highlight indicator
        contentItem: ListView {
            model: tabBar.contentModel
            currentIndex: tabBar.currentIndex
            spacing: tabBar.spacing
            orientation: ListView.Horizontal
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.AutoFlickIfNeeded
            snapMode: ListView.SnapToItem
            highlightMoveDuration: 0
            highlight: Item {}  // No Material indicator
        }

        background: Rectangle {
            color: "transparent"
            // Bottom border line (active tab extends below to cover its portion)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.borderColor
            }
        }

        StyledTabButton {
            id: connectionsTab
            text: TranslationManager.translate("settings.tab.connections", "Connections")
            tabLabel: TranslationManager.translate("settings.tab.connections", "Connections")
        }

        StyledTabButton {
            id: machineTabButton
            text: TranslationManager.translate("settings.tab.machine", "Machine")
            tabLabel: TranslationManager.translate("settings.tab.machine", "Machine")
        }

        StyledTabButton {
            id: calibrationTabButton
            text: TranslationManager.translate("settings.tab.calibration", "Calibration")
            tabLabel: TranslationManager.translate("settings.tab.calibration", "Calibration")
        }

        StyledTabButton {
            id: historyDataTabButton
            text: TranslationManager.translate("settings.tab.historyData", "History & Data")
            tabLabel: TranslationManager.translate("settings.tab.historyData", "History & Data")
        }

        StyledTabButton {
            id: themesTabButton
            text: TranslationManager.translate("settings.tab.themes", "Themes")
            tabLabel: TranslationManager.translate("settings.tab.themes", "Themes")
        }

        StyledTabButton {
            id: layoutTabButton
            text: TranslationManager.translate("settings.tab.layout", "Layout")
            tabLabel: TranslationManager.translate("settings.tab.layout", "Layout")
        }

        StyledTabButton {
            id: screensaverTab
            text: TranslationManager.translate("settings.tab.screensaver", "Screensaver")
            tabLabel: TranslationManager.translate("settings.tab.screensaver", "Screensaver")
        }

        StyledTabButton {
            id: visualizerTabButton
            text: TranslationManager.translate("settings.tab.visualizer", "Visualizer")
            tabLabel: TranslationManager.translate("settings.tab.visualizer", "Visualizer")
        }

        StyledTabButton {
            id: aiTabButton
            text: TranslationManager.translate("settings.tab.ai", "AI")
            tabLabel: TranslationManager.translate("settings.tab.ai", "AI")
        }

        StyledTabButton {
            id: mqttTabButton
            text: TranslationManager.translate("settings.tab.mqtt", "MQTT")
            tabLabel: TranslationManager.translate("settings.tab.mqtt", "MQTT")
        }

        // Language & Access tab with badge for untranslated strings
        StyledTabButton {
            id: languageTabButton
            text: TranslationManager.translate("settings.tab.languageAccess", "Lang & Access")
            tabLabel: TranslationManager.translate("settings.tab.languageAccess.full", "Language & Access")

            // Override contentItem to add badge
            contentItem: Row {
                spacing: Theme.scaled(4)
                Text {
                    text: languageTabButton.text
                    font: languageTabButton.font
                    color: languageTabButton.checked ? Theme.textColor : Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    visible: TranslationManager.currentLanguage !== "en" && TranslationManager.untranslatedCount > 0
                    width: badgeText.width + 8
                    height: Theme.scaled(16)
                    radius: Theme.scaled(8)
                    color: Theme.warningColor
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: TranslationManager.untranslatedCount > 99 ? "99+" : TranslationManager.untranslatedCount
                        font.pixelSize: Theme.scaled(10)
                        font.bold: true
                        color: Theme.primaryContrastColor
                    }
                }
            }
        }

        StyledTabButton {
            id: aboutTabButton
            text: TranslationManager.translate("settings.tab.about", "About")
            tabLabel: TranslationManager.translate("settings.tab.about", "About")
        }

        StyledTabButton {
            id: debugTabButton
            visible: Settings.isDebugBuild
            text: TranslationManager.translate("settings.tab.debug", "Debug")
            tabLabel: TranslationManager.translate("settings.tab.debug", "Debug")
            width: visible ? implicitWidth : 0
        }
    }

    // Tab content area - all tabs preload in background
    StackLayout {
        id: tabContent
        anchors.top: tabBar.bottom
        anchors.topMargin: Theme.spacingMedium
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.bottomMargin: Theme.spacingMedium
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

        currentIndex: tabBar.currentIndex

        // Tab 0: Connections - loads synchronously (first tab appears instantly)
        Loader {
            id: connectionsLoader
            active: true
            asynchronous: false
            source: "settings/SettingsConnectionsTab.qml"
        }

        // Tab 1: Machine - lazy loaded on first visit
        Loader {
            id: machineLoader
            active: 1 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsMachineTab.qml"
        }

        // Tab 2: Calibration - lazy loaded on first visit
        Loader {
            id: calibrationLoader
            active: 2 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsCalibrationTab.qml"
        }

        // Tab 3: History & Data - lazy loaded on first visit
        Loader {
            id: historyDataLoader
            active: 3 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsHistoryDataTab.qml"
        }

        // Tab 4: Themes - lazy loaded on first visit
        Loader {
            id: themesLoader
            active: 4 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsThemesTab.qml"
            onLoaded: {
                item.openSaveThemeDialog.connect(function() {
                    saveThemeDialog.open()
                })
            }
        }

        // Tab 5: Layout - lazy loaded on first visit
        Loader {
            id: layoutLoader
            active: 5 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsLayoutTab.qml"
        }

        // Tab 6: Screensaver - lazy loaded on first visit
        Loader {
            id: screensaverLoader
            active: 6 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsScreensaverTab.qml"
        }

        // Tab 7: Visualizer - lazy loaded on first visit
        Loader {
            id: visualizerLoader
            active: 7 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsVisualizerTab.qml"
        }

        // Tab 8: AI - lazy loaded on first visit
        Loader {
            id: aiLoader
            active: 8 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsAITab.qml"
        }

        // Tab 9: MQTT / Home Automation - lazy loaded on first visit
        Loader {
            id: homeAutomationLoader
            active: 9 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsHomeAutomationTab.qml"
        }

        // Tab 10: Language & Access - lazy loaded on first visit
        Loader {
            id: languageLoader
            active: 10 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsLanguageTab.qml"
        }

        // Tab 11: About (Updates, Firmware, About) - lazy loaded on first visit
        Loader {
            id: aboutLoader
            active: 11 in settingsPage.loadedTabs
            asynchronous: true
            source: "settings/SettingsUpdateTab.qml"
        }

        // Tab 12: Debug - only in debug builds, lazy loaded on first visit
        Loader {
            id: debugLoader
            active: Settings.isDebugBuild && (12 in settingsPage.loadedTabs)
            asynchronous: true
            source: "settings/SettingsDebugTab.qml"
        }
    }

    // Save Theme Dialog
    Dialog {
        id: saveThemeDialog
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - keyboardOffset
        width: Theme.scaled(300)
        padding: 20

        property string themeName: ""
        property real keyboardOffset: 0

        Behavior on y {
            NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
        }

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && saveThemeDialog.visible) {
                    saveThemeDialog.keyboardOffset = parent.height * 0.25
                } else {
                    saveThemeDialog.keyboardOffset = 0
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        onOpened: {
            var current = Settings.activeThemeName
            // Don't pre-fill built-in names or "Custom"
            var name = (current === "Default Dark" || current === "Default Light" || current === "Custom") ? "" : current
            themeName = name
            themeNameInput.text = name
            themeNameInput.forceActiveFocus()
            themeNameInput.selectAll()
        }

        onClosed: {
            keyboardOffset = 0
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            Tr {
                key: "settings.themes.saveTheme"
                fallback: "Save Theme"
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.alignment: Qt.AlignHCenter
            }

            StyledTextField {
                id: themeNameInput
                Layout.fillWidth: true
                placeholder: TranslationManager.translate("settings.themes.themeNamePlaceholder", "Theme name")
                accessibleName: TranslationManager.translate("settings.themes.themeNamePlaceholder", "Theme name")
                onTextChanged: saveThemeDialog.themeName = text
                onAccepted: {
                    Qt.inputMethod.commit()
                    if (saveThemeDialog.themeName.trim().length > 0) {
                        Settings.saveCurrentTheme(saveThemeDialog.themeName.trim())
                        if (themesLoader.item) themesLoader.item.refreshPresets()
                        saveThemeDialog.close()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settingsPage.cancelSavingTheme", "Cancel saving theme")
                    onClicked: saveThemeDialog.close()
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    primary: true
                    text: TranslationManager.translate("common.save", "Save")
                    accessibleName: TranslationManager.translate("settingsPage.saveThemeWithName", "Save current theme with entered name")
                    enabled: saveThemeDialog.themeName.trim().length > 0
                    onClicked: {
                        Qt.inputMethod.commit()
                        var name = saveThemeDialog.themeName.trim()
                        if (name.length > 0 && name !== "Default") {
                            Settings.saveCurrentTheme(name)
                            if (themesLoader.item) themesLoader.item.refreshPresets()
                            saveThemeDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Settings search dialog
    SettingsSearchDialog {
        id: settingsSearchDialog
        onResultSelected: function(tabIndex, cardId) {
            settingsPage.highlightCardId = cardId || ""
            settingsPage.markTabLoaded(tabIndex)
            tabBar.currentIndex = tabIndex
            if (cardId) scrollToCard(tabIndex, cardId)
        }
    }

    // Scroll-to-card after search navigation (event-based, no timer)
    function scrollToCard(tabIndex, cardId) {
        var loader = tabContent.children[tabIndex]
        if (!loader) return

        if (loader.item) {
            // Tab already loaded — scroll immediately
            doScrollAndHighlight(loader.item, cardId)
        } else {
            // Wait for async Loader to finish via statusChanged signal
            var conn = function() {
                if (loader.status === Loader.Ready && loader.item) {
                    loader.statusChanged.disconnect(conn)
                    doScrollAndHighlight(loader.item, cardId)
                } else if (loader.status === Loader.Error) {
                    loader.statusChanged.disconnect(conn)
                    console.warn("SettingsPage: Tab failed to load for cardId:", cardId)
                }
            }
            loader.statusChanged.connect(conn)
        }
    }

    function doScrollAndHighlight(tabItem, cardId) {
        // Find card by objectName recursively
        var card = findChildByObjectName(tabItem, cardId)
        if (!card) {
            console.warn("SettingsPage: Could not find card '" + cardId + "' in tab")
            return
        }

        // Find the Flickable ancestor to scroll
        var flickable = findFlickableParent(card)
        if (flickable) {
            // Map card position to Flickable content coordinates
            var mappedPos = card.mapToItem(flickable.contentItem, 0, 0)
            var targetY = Math.max(0, Math.min(mappedPos.y - Theme.scaled(10),
                flickable.contentHeight - flickable.height))
            flickable.contentY = targetY
        }

        // Flash highlight
        highlightOverlay.target = card
        highlightOverlay.parent = card.parent
        highlightAnimation.restart()
        settingsPage.highlightCardId = ""
    }

    function findChildByObjectName(item, name) {
        if (!item) return null
        // Check the item itself (handles root-level objectName like SettingsLayoutTab)
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; i++) {
            var child = item.children[i]
            if (child.objectName === name) return child
            var found = findChildByObjectName(child, name)
            if (found) return found
        }
        // Flickable children live in contentItem, not in .children
        if (item.contentItem && item instanceof Flickable) {
            for (var j = 0; j < item.contentItem.children.length; j++) {
                var contentChild = item.contentItem.children[j]
                if (contentChild.objectName === name) return contentChild
                var found2 = findChildByObjectName(contentChild, name)
                if (found2) return found2
            }
        }
        return null
    }

    function findFlickableParent(item) {
        var p = item.parent
        while (p) {
            if (p instanceof Flickable) return p
            p = p.parent
        }
        return null
    }

    // Highlight overlay for search results
    Rectangle {
        id: highlightOverlay
        property Item target: null
        visible: false
        color: "transparent"
        border.width: 2
        border.color: Theme.primaryColor
        radius: Theme.cardRadius
        z: 100

        states: State {
            name: "positioned"
            when: highlightOverlay.target !== null
            PropertyChanges {
                target: highlightOverlay
                x: highlightOverlay.target ? highlightOverlay.target.x : 0
                y: highlightOverlay.target ? highlightOverlay.target.y : 0
                width: highlightOverlay.target ? highlightOverlay.target.width : 0
                height: highlightOverlay.target ? highlightOverlay.target.height : 0
            }
        }

        SequentialAnimation {
            id: highlightAnimation
            PropertyAction { target: highlightOverlay; property: "visible"; value: true }
            PropertyAction { target: highlightOverlay; property: "opacity"; value: 1 }
            NumberAnimation { target: highlightOverlay; property: "opacity"; from: 1; to: 0; duration: 2000; easing.type: Easing.InQuad }
            PropertyAction { target: highlightOverlay; property: "visible"; value: false }
            PropertyAction { target: highlightOverlay; property: "target"; value: null }
        }
    }

    // Bottom bar with back button
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("settings.title", "Settings")
        onBackClicked: root.goBack()
    }
}
