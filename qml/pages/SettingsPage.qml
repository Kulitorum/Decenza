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

    // Requested tab to switch to (set before pushing page). Symbolic id from SettingsTabs.
    property string requestedTabId: ""

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
        var idx = requestedTabId.length > 0 ? SettingsTabs.indexOf(requestedTabId) : -1
        if (idx >= 0) markTabLoaded(idx)
    }

    // Switch to requested tab after page transition completes (page is fully laid out)
    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("settings.title", "Settings")
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
                var tabNames = SettingsTabs.visibleTabNames()
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
                        color: tabBtn.checked ? Theme.textColor : Theme.textSecondaryColor
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
                // Keep settings tabs lazy-loaded, but instantiate them synchronously.
                // Android crash reports have shown QQmlConnections crashing inside
                // QQmlIncubationController while browsing settings; avoiding async
                // incubation keeps Connections setup on the tab-switch event.
                asynchronous: false
                source: modelData.source

                onStatusChanged: {
                    if (status === Loader.Loading)
                        console.log("SettingsPage: loading tab", tabId)
                    else if (status === Loader.Ready)
                        console.log("SettingsPage: tab ready", tabId)
                    else if (status === Loader.Error)
                        console.warn("SettingsPage: tab load error", tabId)
                }

                onLoaded: {
                    // Themes tab emits a signal requesting the Save Theme dialog
                    if (tabId === "themes" && item) {
                        item.openSaveThemeDialog.connect(function() {
                            saveThemeDialog.open()
                        })
                    }
                }
            }
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

        function doSave(name) {
            Settings.saveCurrentTheme(name)
            var themesLoader = tabLoaders.itemAt(SettingsTabs.indexOf("themes"))
            if (themesLoader && themesLoader.item && themesLoader.item.refreshPresets) {
                themesLoader.item.refreshPresets()
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
                    Qt.inputMethod.commit()
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
                        Qt.inputMethod.commit()
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
        onResultSelected: function(tabId, cardId) {
            var tabIndex = SettingsTabs.indexOf(tabId)
            if (tabIndex < 0) return
            settingsPage.highlightCardId = cardId || ""
            settingsPage.markTabLoaded(tabIndex)
            tabBar.currentIndex = tabIndex
            if (cardId) scrollToCard(tabIndex, cardId)
        }
    }

    // Scroll-to-card after search navigation (event-based, no timer)
    function scrollToCard(tabIndex, cardId) {
        var loader = tabLoaders.itemAt(tabIndex)
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
