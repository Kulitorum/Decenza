// The tab-loading Repeater delegate reads this file's ids (`settingsPage`,
// `saveThemeDialog`); Bound makes them statically resolvable. It and the tab-button
// delegate both already declare every injected model role they use as a required
// property, so Bound cannot break role injection here. (The tab-button delegate reads
// no file id; it only needs its roles.)
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Window
import Decenza

T.Page {
    id: settingsPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("settings.title", "Settings")

    objectName: "settingsPage"
    background: ThemedPageBackground {}


    // Requested tab to switch to (set before pushing page). Symbolic id from SettingsTabs.
    property string requestedTabId: ""

    // Track which tabs have been visited (lazy-load: only load tab content on first visit)
    property var loadedTabs: ({})

    function markTabLoaded(index) {
        if (!(index in loadedTabs)) {
            // Must create a new object - reassigning the same reference
            // won't trigger QML property change notifications
            let tabs = Object.assign({}, loadedTabs)
            tabs[index] = true
            loadedTabs = tabs
        }
    }

    StackView.onActivating: {
        var idx = requestedTabId.length > 0 ? SettingsTabs.indexOf(requestedTabId) : -1
        if (idx >= 0) markTabLoaded(idx)
    }

    // Switch tabs on a page that is ALREADY on top of the stack. `requestedTabId` is consumed only
    // by StackView.onActivated below, so assigning it to a live page does nothing — main.qml calls
    // this instead when the user taps a settings widget from inside Settings, which must switch
    // tabs rather than push a second copy of this page.
    function showTab(tabId) {
        var idx = SettingsTabs.indexOf(tabId)
        if (idx < 0)
            return
        markTabLoaded(idx)
        tabBar.currentIndex = idx
    }

    // Switch to requested tab after page transition completes (page is fully laid out)
    StackView.onActivated: {
        var idx = requestedTabId.length > 0 ? SettingsTabs.indexOf(requestedTabId) : -1
        if (idx >= 0) {
            markTabLoaded(idx)
            tabBar.currentIndex = idx
        }
        requestedTabId = ""
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

        // ThemedIcon, not a bare Image: search.svg strokes white, and this button's
        // background is Theme.surfaceColor — which is #ffffff in light mode. As a plain
        // Image the icon was white-on-white and completely INVISIBLE in light mode.
        // Found by looking at the running app in light mode, not by reading the code.
        ThemedIcon {
            anchors.centerIn: parent
            source: "qrc:/icons/search.svg"
            iconSize: Theme.scaled(20)
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

            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
                let tabNames = SettingsTabs.visibleTabNames()
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
            // Scrim the whole bar (not just the active tab) when a custom background
            // image is set — unselected tab labels otherwise sit directly on the photo
            // with no surface behind them, hurting legibility.
            color: Theme.glassChrome ? Theme.cardBackgroundColor : "transparent"

            // Bottom border line (active tab extends below to cover its portion)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.borderColor
            }
        }

        Repeater {
            model: SettingsTabs.visibleTabs()

            StyledTabButton {
                id: tabBtn
                required property var modelData
                readonly property string tabId: modelData.id
                readonly property bool isLanguageTab: modelData.id === "languageAccess"

                // Referencing translationVersion forces the language-tab branch below to re-
                // evaluate on language change (the other branch goes through tabLabels, which
                // already depends on translationVersion).
                text: {
                    var v = TranslationManager.translationVersion
                    return isLanguageTab
                        ? TranslationManager.translate("settings.tab.languageAccess", "Lang & Access")
                        : SettingsTabs.tabLabels[tabId]
                }
                tabLabel: {
                    var v = TranslationManager.translationVersion
                    return isLanguageTab
                        ? TranslationManager.translate("settings.tab.languageAccess.full", "Language & Access")
                        : SettingsTabs.tabLabels[tabId]
                }

                // Shared Row contentItem: the badge is only visible on the Language & Access tab
                // (Row ignores invisible children in its layout, so width matches plain-text tabs).
                contentItem: Row {
                    spacing: Theme.scaled(4)
                    Text {
                        text: tabBtn.text
                        font: tabBtn.font
                        color: tabBtn.checked ? Theme.textColor : Theme.textSecondaryOnBackgroundColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        visible: tabBtn.isLanguageTab
                                 && TranslationManager.currentLanguage !== "en"
                                 && TranslationManager.untranslatedCount > 0
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
        }
    }

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

        // One Loader per visible tab, in SettingsTabs order.
        // Tabs with loadSync=true are loaded eagerly; others lazy-load on first visit.
        Repeater {
            id: tabLoaders
            model: SettingsTabs.visibleTabs()

            Loader {
                required property var modelData
                required property int index
                readonly property string tabId: modelData.id

                active: modelData.loadSync || (index in settingsPage.loadedTabs)
                // Synchronous instantiation: Android crash reports showed QQmlConnections
                // crashing inside QQmlIncubationController while browsing settings. Forcing
                // asynchronous: false eliminates the incubation path entirely. Tabs remain
                // lazy-loaded (active is gated by loadedTabs); loadSync only controls whether
                // a tab is pre-activated on page open vs. activated on first visit.
                asynchronous: false
                source: modelData.source

                onStatusChanged: {
                    if (status === Loader.Loading)
                        WebDebugLogger.debug("App", "SettingsPage", ["loading tab", tabId].map(String).join(" "))  // TODO: remove after #844 confirmed resolved
                    else if (status === Loader.Ready)
                        WebDebugLogger.debug("App", "SettingsPage", ["tab ready", tabId].map(String).join(" "))  // TODO: remove after #844 confirmed resolved
                    else if (status === Loader.Error)
                        WebDebugLogger.warn("App", "SettingsPage", ["tab load error", tabId].map(String).join(" "))
                }

                onLoaded: {
                    // Themes tab emits a signal requesting the Save Theme dialog
                    var themesTab = item as SettingsThemesTab
                    if (themesTab) {
                        themesTab.openSaveThemeDialog.connect(function() {
                            saveThemeDialog.open()
                        })
                    }
                    // Calibration tab's Sensor Calibration card forwards to global
                    // navigation, carrying which sensor was chosen.
                    var calibrationTab = item as SettingsCalibrationTab
                    if (calibrationTab) {
                        calibrationTab.openSensorCalibration.connect(function(sensor) {
                            AppShell.sensorCalibrationRequested(sensor)
                        })
                    }
                    // Machine tab's Maintenance card forwards to global navigation
                    var machineTab = item as SettingsMachineTab
                    if (machineTab) {
                        machineTab.openDescaling.connect(function() {
                            AppShell.descalingRequested()
                        })
                        machineTab.openTransport.connect(function() {
                            AppShell.transportRequested()
                        })
                    }
                }
            }
        }
    }

    // Save Theme Dialog
    DecenzaDialog {
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
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && saveThemeDialog.visible) {
                    saveThemeDialog.keyboardOffset = saveThemeDialog.parent.height * 0.25
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
            var current = Settings.theme.activeThemeName
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

        function doSave(name) {
            Settings.theme.saveCurrentTheme(name)
            var themesLoader = tabLoaders.itemAt(SettingsTabs.indexOf("themes")) as Loader
            var themesTab = themesLoader ? themesLoader.item as SettingsThemesTab : null
            if (themesTab) {
                themesTab.refreshPresets()
            }
            saveThemeDialog.close()
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
                    Keyboard.commit()
                    var name = saveThemeDialog.themeName.trim()
                    if (name.length > 0 && name !== "Default") {
                        saveThemeDialog.doSave(name)
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
                        Keyboard.commit()
                        var name = saveThemeDialog.themeName.trim()
                        if (name.length > 0 && name !== "Default") {
                            saveThemeDialog.doSave(name)
                        }
                    }
                }
            }
        }
    }

    // Settings search dialog
    SettingsSearchDialog {
        id: settingsSearchDialog
        onResultSelected: function(result) {
            if (result.externalRoute) {
                // External destination outside the Settings tab stack (e.g.
                // ProfileSelectorPage). `goToProfileSelector()` pushes the
                // target on top of the Settings page, so the user can press
                // Back to return to the search context.
                if (result.externalRoute === "profileSelector") {
                    AppShell.profileSelectorRequested()
                }
                return
            }
            var tabIndex = SettingsTabs.indexOf(result.tabId)
            if (tabIndex < 0) {
                WebDebugLogger.warn("App", "SettingsPage", ["Search result for unknown tab '" + result.tabId + "'"].map(String).join(" "))
                return
            }
            settingsPage.markTabLoaded(tabIndex)
            tabBar.currentIndex = tabIndex
            settingsPage.scrollToCard(tabIndex, result.cardId, result.kind === "adjustment" ? result.title : "")
        }
    }

    // Scroll-to-card after search navigation (event-based, no timer)
    // A scroll still waiting for its tab to load; a newer search replaces it.
    property var _pendingScroll: null

    function scrollToCard(tabIndex, cardId, targetTitle) {
        if (_pendingScroll) {
            _pendingScroll.loader.statusChanged.disconnect(_pendingScroll.handler)
            _pendingScroll = null
        }
        var loader = tabLoaders.itemAt(tabIndex)
        if (!loader) {
            WebDebugLogger.warn("App", "SettingsPage", ["No loader for tab index", tabIndex].map(String).join(" "))
            return
        }

        if (loader.item) {
            // Tab already loaded — scroll immediately
            doScrollAndHighlight(loader.item, cardId, targetTitle)
        } else {
            // Tab not yet instantiated — connect statusChanged; with asynchronous: false
            // loading is synchronous but item is only valid after active flips, so
            // statusChanged is still the correct hook when scrollToCard is called
            // before the loader's active binding has re-evaluated
            let conn = function() {
                if (loader.status === Loader.Loading)
                    return
                loader.statusChanged.disconnect(conn)
                settingsPage._pendingScroll = null
                if (loader.status === Loader.Ready && loader.item)
                    doScrollAndHighlight(loader.item, cardId, targetTitle)
                else if (loader.status === Loader.Error)
                    WebDebugLogger.warn("App", "SettingsPage", ["Tab failed to load for cardId:", cardId].map(String).join(" "))
            }
            _pendingScroll = { loader: loader, handler: conn }
            loader.statusChanged.connect(conn)
        }
    }

    function doScrollAndHighlight(tabItem, cardId, targetTitle) {
        var card = findChildByObjectName(tabItem, cardId)
        if (!card) {
            WebDebugLogger.warn("App", "SettingsPage", ["Could not find card '" + cardId + "' in tab"].map(String).join(" "))
            return
        }
        // Hidden right now (shown: false, e.g. while the refill kit is fitted): the tab is as
        // close as search can get.
        if (!card.visible) {
            WebDebugLogger.info("App", "SettingsPage", ["Search result card '" + cardId + "' is hidden in this state"].map(String).join(" "))
            return
        }
        // An adjustment's result highlights its row. A row hidden in this state falls back to
        // the card; one that does not exist means the index and the tab disagree.
        var target = targetTitle ? SettingsSearchLocator.findRow(card, targetTitle) : card
        if (!target) {
            if (SettingsSearchLocator.hasItem(card, targetTitle))
                WebDebugLogger.info("App", "SettingsPage", ["Search result '" + targetTitle + "' is hidden in this state"].map(String).join(" "))
            else
                WebDebugLogger.warn("App", "SettingsPage", ["Could not find '" + targetTitle + "' on card '" + cardId + "'"].map(String).join(" "))
            target = card
        }

        // Find the Flickable ancestor to scroll
        var flickable = findFlickableParent(target)
        if (flickable) {
            // Map target position to Flickable content coordinates
            let mappedPos = target.mapToItem(flickable.contentItem, 0, 0)
            let targetY = Math.max(0, Math.min(mappedPos.y - Theme.scaled(10),
                flickable.contentHeight - flickable.height))
            flickable.contentY = targetY
        }

        // Flash highlight. Hosted in the scrolling content, never in target.parent: that is
        // usually a Layout, which would lay the overlay out as one more row.
        var host = flickable ? flickable.contentItem : tabItem
        highlightOverlay.parent = host
        // A row's edges run flush with its text, so its outline stands off a little.
        var pad = target === card ? 0 : Theme.scaled(4)
        highlightOverlay.x = Qt.binding(function() { return offsetIn(target, host).x - pad })
        highlightOverlay.y = Qt.binding(function() { return offsetIn(target, host).y - pad })
        highlightOverlay.width = Qt.binding(function() { return target.width + 2 * pad })
        highlightOverlay.height = Qt.binding(function() { return target.height + 2 * pad })
        highlightAnimation.restart()
    }

    // `item`'s position in `host`, summed by hand: unlike mapToItem(), every x/y read here is a
    // binding dependency, so the highlight follows a freshly loaded tab while its layouts settle.
    function offsetIn(item, host) {
        var x = 0, y = 0
        for (var p = item; p && p !== host; p = p.parent) {
            x += p.x
            y += p.y
        }
        return Qt.point(x, y)
    }

    function findChildByObjectName(item, name) {
        if (!item) return null
        for (let i = 0; i < item.children.length; i++) {
            let child = item.children[i]
            if (child.objectName === name) return child
            let found = findChildByObjectName(child, name)
            if (found) return found
        }
        // Flickable children live in contentItem, not in .children
        if (item.contentItem && item instanceof Flickable) {
            for (let j = 0; j < item.contentItem.children.length; j++) {
                let contentChild = item.contentItem.children[j]
                if (contentChild.objectName === name) return contentChild
                let found2 = findChildByObjectName(contentChild, name)
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
        visible: false
        color: "transparent"
        border.width: 2
        border.color: Theme.primaryColor
        radius: Theme.cardRadius
        z: 100

        SequentialAnimation {
            id: highlightAnimation
            PropertyAction { target: highlightOverlay; property: "visible"; value: true }
            PropertyAction { target: highlightOverlay; property: "opacity"; value: 1 }
            NumberAnimation { target: highlightOverlay; property: "opacity"; from: 1; to: 0; duration: 2000; easing.type: Easing.InQuad }
            PropertyAction { target: highlightOverlay; property: "visible"; value: false }
        }
    }

    // Bottom bar with back button
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("settings.title", "Settings")
        onBackClicked: AppShell.backRequested()
    }
}
