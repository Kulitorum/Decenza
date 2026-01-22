import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "components"

ApplicationWindow {
    id: root
    visible: true
    visibility: Qt.platform.os === "android" ? Window.FullScreen : Window.AutomaticVisibility
    // On desktop use reference size; on Android let system control fullscreen
    width: 960
    height: 600
    title: "Decenza DE1"
    color: Theme.backgroundColor

    // Flag to prevent navigation during flow calibration
    property bool calibrationInProgress: false

    // Debug flag to force live view on operation pages (for development)
    property bool debugLiveView: false

    // Global accessibility: find closest Text within radius of tap
    // Use physical units: 10mm (~1cm) converted to pixels
    property real accessibilitySearchRadius: Screen.pixelDensity * 10  // 10mm in pixels

    // Collect all Text items with their global positions
    function collectTexts(item, offsetX, offsetY, results) {
        if (!item || !item.visible) return

        // Skip items with custom handlers
        if (item.accessibilityCustomHandler) return

        // Handle ScrollView/Flickable scroll offset
        var scrollOffsetX = 0
        var scrollOffsetY = 0
        if (item.contentItem && item.contentX !== undefined) {
            scrollOffsetX = -item.contentX
            scrollOffsetY = -item.contentY
        }

        // If this is a Text with content, add it
        if (item instanceof Text && item.text && item.text.length > 0) {
            var centerX = offsetX + item.width / 2
            var centerY = offsetY + item.height / 2
            results.push({ text: item, x: centerX, y: centerY })
        }

        // Check contentItem for ScrollView/Flickable
        if (item.contentItem && item.contentX !== undefined) {
            collectTexts(item.contentItem, offsetX + scrollOffsetX, offsetY + scrollOffsetY, results)
        }

        // Recurse into children
        var childList = item.children || item.contentChildren || []
        for (var i = 0; i < childList.length; i++) {
            var child = childList[i]
            if (!child || !child.visible || child.width === undefined) continue
            collectTexts(child, offsetX + child.x + scrollOffsetX, offsetY + child.y + scrollOffsetY, results)
        }
    }

    function findTextAt(item, tapX, tapY) {
        var results = []
        collectTexts(item, 0, 0, results)

        var closest = null
        var closestDist = accessibilitySearchRadius

        for (var i = 0; i < results.length; i++) {
            var dx = results[i].x - tapX
            var dy = results[i].y - tapY
            var dist = Math.sqrt(dx * dx + dy * dy)
            if (dist < closestDist) {
                closestDist = dist
                closest = results[i].text
            }
        }

        return closest
    }

    // Handle app close: save position, put devices to sleep
    onClosing: function(close) {
        // Save window position on desktop (not size - keep default to match real device)
        if (Qt.platform.os !== "android" && Qt.platform.os !== "ios") {
            Settings.setValue("mainWindow/x", root.x)
            Settings.setValue("mainWindow/y", root.y)
        }

        // Send scale sleep first (it's faster/simpler)
        if (ScaleDevice && ScaleDevice.connected) {
            console.log("Sending scale to sleep on app close")
            ScaleDevice.sleep()
        }

        // Small delay before sending DE1 sleep to let scale command go through
        close.accepted = false
        scaleSleepTimer.start()
    }

    Timer {
        id: scaleSleepTimer
        interval: 150  // Give scale sleep command time to send
        onTriggered: {
            // Now send DE1 to sleep
            if (DE1Device && DE1Device.connected) {
                console.log("Sending DE1 to sleep on app close")
                DE1Device.goToSleep()
            }
            // Wait for DE1 command to complete
            closeTimer.start()
        }
    }

    Timer {
        id: closeTimer
        interval: 300  // 300ms to allow BLE commands to complete
        onTriggered: Qt.quit()
    }

    // Auto-sleep inactivity timer (base setting from Preferences)
    property int autoSleepMinutes: {
        var val = Settings.value("autoSleepMinutes", 60)
        return (val === undefined || val === null) ? 60 : parseInt(val)
    }

    // Two-counter sleep system:
    // - sleepCountdownNormal: resets on user activity, counts down from autoSleepMinutes
    // - sleepCountdownStayAwake: only set on auto-wake, never reset by activity
    // Sleep when BOTH <= 0
    property int sleepCountdownNormal: -1      // Minutes remaining (-1 = not started)
    property int sleepCountdownStayAwake: -1   // Minutes remaining (-1 = already satisfied)

    // Active operation phases that should pause the sleep countdown
    property bool operationActive: {
        var phase = MachineState.phase
        return phase === MachineStateType.Phase.EspressoPreheating ||
               phase === MachineStateType.Phase.Preinfusion ||
               phase === MachineStateType.Phase.Pouring ||
               phase === MachineStateType.Phase.Ending ||
               phase === MachineStateType.Phase.Steaming ||
               phase === MachineStateType.Phase.HotWater ||
               phase === MachineStateType.Phase.Flushing ||
               phase === MachineStateType.Phase.Descaling ||
               phase === MachineStateType.Phase.Cleaning
    }

    // Sleep countdown timer - ticks every minute
    Timer {
        id: sleepCountdownTimer
        interval: 60 * 1000  // 1 minute
        running: !screensaverActive && !root.operationActive && root.autoSleepMinutes > 0
        repeat: true
        onTriggered: {
            // Decrement both counters (only if > 0)
            if (root.sleepCountdownNormal > 0) root.sleepCountdownNormal--
            if (root.sleepCountdownStayAwake > 0) root.sleepCountdownStayAwake--

            // Debug: log countdown status on every tick
            console.log("Sleep countdown: normal=" + root.sleepCountdownNormal +
                       ", stayAwake=" + root.sleepCountdownStayAwake)

            // Sleep when BOTH <= 0
            if (root.sleepCountdownNormal <= 0 && root.sleepCountdownStayAwake <= 0) {
                console.log("Auto-sleep triggered (both counters expired)")
                triggerAutoSleep()
            }
        }
    }

    // Reset normal countdown on user activity (phase change)
    Connections {
        target: MachineState
        function onPhaseChanged() {
            if (!screensaverActive && root.autoSleepMinutes > 0) {
                root.sleepCountdownNormal = root.autoSleepMinutes
            }
        }
    }

    // Handle settings changes
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "autoSleepMinutes") {
                var val = Settings.value("autoSleepMinutes", 60)
                root.autoSleepMinutes = (val === undefined || val === null) ? 60 : parseInt(val)
                // Update normal countdown to new value
                if (!screensaverActive && root.autoSleepMinutes > 0) {
                    root.sleepCountdownNormal = root.autoSleepMinutes
                }
            } else if (key === "ui/configurePageScale") {
                var val = Settings.value("ui/configurePageScale", false)
                console.log("configurePageScale changed:", val, "type:", typeof val)
                Theme.configurePageScaleEnabled = (val === true || val === "true")
                console.log("configurePageScaleEnabled set to:", Theme.configurePageScaleEnabled)
            }
        }
    }

    function triggerAutoSleep() {
        // Put scale to LCD-off mode (keep connected for wake)
        if (ScaleDevice && ScaleDevice.connected) {
            ScaleDevice.disableLcd()  // LCD off only, stay connected
        }
        // Put DE1 to sleep
        if (DE1Device && DE1Device.connected) {
            DE1Device.goToSleep()
        }
        // Show screensaver
        goToScreensaver()
    }

    // Current page title - set by each page
    property string currentPageTitle: ""

    // Flag to prevent premature UI display
    property bool appInitialized: false


    // Suppress scale dialogs briefly after waking from sleep
    property bool justWokeFromSleep: false
    Timer {
        id: wakeSuppressionTimer
        interval: 2000  // Suppress dialogs for 2 seconds after wake
        onTriggered: root.justWokeFromSleep = false
    }

    // Periodic timer to keep steam heater on when idle
    // The DE1 may have an internal timeout that reduces steam heater power after some idle time.
    // This timer resends the steam settings every 60 seconds to maintain target temperature.
    Timer {
        id: steamHeaterTimer
        interval: 60000  // Every 60 seconds
        running: Settings.keepSteamHeaterOn && !Settings.steamDisabled &&
                 DE1Device.connected &&
                 (MachineState.phase === MachineStateType.Phase.Idle ||
                  MachineState.phase === MachineStateType.Phase.Ready)
        repeat: true
        onTriggered: {
            console.log("Resending steam settings to keep heater on")
            MainController.applySteamSettings()
        }
    }

    // Track if we were just steaming (for auto-flush timer)
    property bool wasSteaming: false

    // Auto-flush steam wand timer - triggers Idle request after steaming ends
    Timer {
        id: steamAutoFlushTimer
        interval: Settings.steamAutoFlushSeconds * 1000
        running: false
        repeat: false
        onTriggered: {
            console.log("Steam auto-flush timer triggered, requesting Idle state")
            if (DE1Device && DE1Device.connected) {
                DE1Device.requestState(0)  // 0 = Idle
            }
        }
    }

    // Update scale factor when window resizes or override changes
    onWidthChanged: updateScale()
    onHeightChanged: updateScale()
    Connections {
        target: Theme
        function onScaleMultiplierChanged() { updateScale() }
        function onPageScaleMultiplierChanged() { updateScale() }
    }
    // Raise all application windows together when this window is activated
    onActiveChanged: {
        if (active && typeof GHCSimulator !== "undefined" && GHCSimulator) {
            GHCSimulator.mainWindowActivated()
        }
    }

    // Listen for GHC window activation to raise ourselves (simulator mode only)
    Connections {
        target: typeof GHCSimulator !== "undefined" ? GHCSimulator : null
        function onRaiseMainWindow() {
            root.raise()
        }
    }

    Component.onCompleted: {
        // Restore window position on desktop (not size - keep default to match real device)
        if (Qt.platform.os !== "android" && Qt.platform.os !== "ios") {
            var savedX = Settings.value("mainWindow/x", -1)
            var savedY = Settings.value("mainWindow/y", -1)
            if (savedX >= 0 && savedY >= 0) {
                root.x = savedX
                root.y = savedY
            }
        }

        // Initialize per-page scale settings
        var configVal = Settings.value("ui/configurePageScale", false)
        console.log("Init configurePageScale:", configVal, "type:", typeof configVal)
        // Handle both boolean and string values from QSettings
        Theme.configurePageScaleEnabled = (configVal === true || configVal === "true")
        console.log("Init configurePageScaleEnabled:", Theme.configurePageScaleEnabled)

        updateScale()

        // Keep screen on while app is running (Android only)
        // This prevents Android's system timeout from turning off the screen
        // while the user is prepping beans, etc. The screensaver handles sleep.
        if (Qt.platform.os === "android") {
            ScreensaverManager.setKeepScreenOn(true)
        }

        // Check for crash log from previous session
        if (PreviousCrashLog && PreviousCrashLog.length > 0) {
            // Delay showing crash dialog slightly to ensure UI is ready
            Qt.callLater(function() {
                crashReportDialog.open()
            })
        }

        // Check for first run and show welcome dialog or start scanning
        var firstRunComplete = Settings.value("firstRunComplete", false)
        if (!firstRunComplete) {
            firstRunDialog.open()
        } else {
            // On subsequent launches, still check if storage setup is needed
            // (e.g., after reinstall when QSettings was restored but SAF permission wasn't)
            checkStorageSetup()
        }

        // Initialize sleep countdowns (fresh app start, not auto-woken)
        if (root.autoSleepMinutes > 0) {
            root.sleepCountdownNormal = root.autoSleepMinutes
            root.sleepCountdownStayAwake = 0  // Already satisfied on fresh start
        }

        // Mark app as initialized
        root.appInitialized = true
    }

    function updateScale() {
        // Scale based on window size vs reference (960x600 tablet dp)
        var scaleX = width / Theme.refWidth
        var scaleY = height / Theme.refHeight
        var autoScale = Math.min(scaleX, scaleY)

        // Apply global multiplier and per-page multiplier
        Theme.scale = autoScale * Theme.scaleMultiplier * Theme.pageScaleMultiplier
    }

    // Global tap handler for accessibility - announces any Text tapped
    // Only announces text that is NOT inside an interactive element (buttons have their own announcements)
    MouseArea {
        id: accessibilityTapOverlay
        anchors.fill: parent
        z: 10000  // Above everything
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        propagateComposedEvents: true

        // Check if an item or any ancestor is interactive (Button, focusable, etc.)
        function isInsideInteractive(item) {
            var current = item
            while (current) {
                // Check for common interactive types
                if (current.Accessible && current.Accessible.focusable) return true
                if (current.activeFocusOnTab) return true
                if (current.toString().indexOf("Button") !== -1) return true
                if (current.toString().indexOf("AccessibleMouseArea") !== -1) return true
                current = current.parent
            }
            return false
        }

        onPressed: function(mouse) {
            var textItem = findTextAt(parent, mouse.x, mouse.y)
            if (textItem && textItem.text && !isInsideInteractive(textItem)) {
                AccessibilityManager.announceLabel(cleanForSpeech(textItem.text))
            }
            mouse.accepted = false
        }

        onClicked: function(mouse) { mouse.accepted = false }
        onReleased: function(mouse) { mouse.accepted = false }
    }

    // Floating "Done Editing" button - appears when translation edit mode is active
    Rectangle {
        id: doneEditingButton
        visible: typeof TranslationManager !== "undefined" && TranslationManager.editModeEnabled
        z: 10002  // Above the translation overlay

        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.scaled(10)

        width: doneEditingRow.width + Theme.scaled(24)
        height: Theme.scaled(40)
        radius: height / 2
        color: Theme.primaryColor

        RowLayout {
            id: doneEditingRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Text {
                text: "✓"
                font.pixelSize: Theme.scaled(16)
                color: "white"
            }

            Tr {
                key: "main.doneediting"
                fallback: "Done Editing"
                font: Theme.bodyFont
                color: "white"
            }

            // Show count of untranslated strings
            Rectangle {
                visible: TranslationManager.untranslatedCount > 0
                width: untranslatedText.width + Theme.scaled(12)
                height: Theme.scaled(22)
                radius: height / 2
                color: Theme.warningColor

                Text {
                    id: untranslatedText
                    anchors.centerIn: parent
                    text: TranslationManager.untranslatedCount
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                    color: "white"
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: TranslationManager.editModeEnabled = false
        }
    }

    // Pre-compile pages at startup by loading them once (warms QML cache)
    // These are NOT used for navigation - StackView uses Components below
    // The Loaders just ensure QML is parsed/compiled before first navigation
    Loader {
        id: preloadIdle
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { IdlePage {} }
        onLoaded: active = false  // Unload after compilation (Loader owns the item)
    }
    Loader {
        id: preloadEspresso
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { EspressoPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadSteam
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { SteamPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadHotWater
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { HotWaterPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadFlush
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { FlushPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadSettings
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { SettingsPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadProfileSelector
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { ProfileSelectorPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadProfileEditor
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { ProfileEditorPage {} }
        onLoaded: active = false
    }
    Loader {
        id: preloadRecipeEditor
        active: true
        asynchronous: true
        visible: false
        sourceComponent: Component { RecipeEditorPage {} }
        onLoaded: active = false
    }

    // Navigation guard to prevent double-taps during page transitions
    property bool navigationInProgress: false
    Timer {
        id: navigationGuardTimer
        interval: 300  // Block navigation for 300ms after a transition starts
        onTriggered: root.navigationInProgress = false
    }

    function startNavigation() {
        if (navigationInProgress || pageStack.busy) return false
        navigationInProgress = true
        navigationGuardTimer.restart()
        return true
    }

    // Page stack for navigation
    StackView {
        id: pageStack
        anchors.fill: parent
        initialItem: idlePage

        // Default: instant transitions (no animation)
        pushEnter: Transition {}
        pushExit: Transition {}
        popEnter: Transition {}
        popExit: Transition {}
        replaceEnter: Transition {}
        replaceExit: Transition {}

        // Windows-only: Ctrl+mousewheel to zoom current page
        WheelHandler {
            enabled: Qt.platform.os === "windows"
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                var pageName = pageStack.currentItem ? (pageStack.currentItem.objectName || "") : ""
                if (!pageName) return

                var currentScale = Theme.pageScaleMultiplier
                var delta = event.angleDelta.y > 0 ? 0.05 : -0.05
                var newScale = Math.max(0.5, Math.min(2.0, currentScale + delta))

                if (newScale !== currentScale) {
                    Theme.pageScaleMultiplier = newScale
                    Settings.setValue("pageScale/" + pageName, newScale)
                    console.log("Ctrl+wheel zoom:", pageName, "scale =", newScale.toFixed(2))
                }
            }
        }

        Component {
            id: idlePage
            IdlePage {}
        }

        Component {
            id: espressoPage
            EspressoPage {}
        }

        Component {
            id: steamPage
            SteamPage {}
        }

        Component {
            id: hotWaterPage
            HotWaterPage {}
        }

        Component {
            id: settingsPage
            SettingsPage {}
        }

        Component {
            id: flushPage
            FlushPage {}
        }

        Component {
            id: profileEditorPage
            ProfileEditorPage {}
        }

        Component {
            id: recipeEditorPage
            RecipeEditorPage {}
        }

        Component {
            id: profileSelectorPage
            ProfileSelectorPage {}
        }

        Component {
            id: descalingPage
            DescalingPage {}
        }

        Component {
            id: visualizerBrowserPage
            VisualizerBrowserPage {}
        }

        Component {
            id: profileImportPage
            ProfileImportPage {}
        }

        Component {
            id: beanInfoPage
            BeanInfoPage {}
        }

        Component {
            id: postShotReviewPage
            PostShotReviewPage {}
        }

        Component {
            id: profileInfoPage
            ProfileInfoPage {}
        }
    }

    // Update per-page scale when navigating between pages
    Connections {
        target: pageStack
        function onCurrentItemChanged() {
            updateCurrentPageScale()
            announceCurrentPage()
        }
    }

    // Announce page name for accessibility when page changes
    function announceCurrentPage() {
        if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
        var pageName = pageStack.currentItem ? (pageStack.currentItem.objectName || "") : ""
        if (!pageName) return

        // Map objectNames to human-readable page names
        // Use "screen" or "settings" suffix to distinguish from button names
        var pageNames = {
            "idlePage": qsTr("Home screen"),
            "espressoPage": qsTr("Espresso screen"),
            "steamPage": qsTr("Steam settings"),
            "hotWaterPage": qsTr("Hot water settings"),
            "flushPage": qsTr("Flush settings"),
            "settingsPage": qsTr("Settings"),
            "profileSelectorPage": qsTr("Profile selector"),
            "profileEditorPage": qsTr("Profile editor"),
            "recipeEditorPage": qsTr("Recipe editor"),
            "shotHistoryPage": qsTr("Shot history"),
            "descalingPage": qsTr("Descaling screen"),
            "visualizerBrowserPage": qsTr("Visualizer browser"),
            "profileImportPage": qsTr("Import profiles"),
            "postShotReviewPage": qsTr("Shot review"),
            "beanInfoPage": qsTr("Bean info"),
            "dialingAssistantPage": qsTr("AI assistant"),
            "shotDetailPage": qsTr("Shot detail"),
            "shotComparisonPage": qsTr("Shot comparison")
        }
        var displayName = pageNames[pageName] || pageName
        AccessibilityManager.announce(displayName)
    }

    function updateCurrentPageScale() {
        var pageName = pageStack.currentItem ? (pageStack.currentItem.objectName || "") : ""
        console.log("updateCurrentPageScale: pageName =", pageName)
        Theme.currentPageObjectName = pageName
        if (pageName) {
            Theme.pageScaleMultiplier = parseFloat(Settings.value("pageScale/" + pageName, 1.0)) || 1.0
        } else {
            Theme.pageScaleMultiplier = 1.0
        }
        console.log("Scale config: enabled =", Theme.configurePageScaleEnabled,
                    "page =", Theme.currentPageObjectName,
                    "scale =", Theme.pageScaleMultiplier)
    }

    // Initialize page scale after pageStack is ready
    Timer {
        id: initPageScaleTimer
        interval: 100
        onTriggered: {
            console.log("initPageScaleTimer triggered")
            updateCurrentPageScale()
        }
        Component.onCompleted: start()
    }

    // Global error dialog for BLE issues
    Popup {
        id: bleErrorDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        padding: 24

        property string errorMessage: ""
        property bool isLocationError: false

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        Tr { id: trEnableLocation; key: "main.dialog.enableLocation.title"; fallback: "Enable Location"; visible: false }
        Tr { id: trErrorPrefix; key: "common.accessibility.errorPrefix"; fallback: "Error:"; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trErrorPrefix.text + " " + errorMessage, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Text {
                text: trEnableLocation.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: bleErrorDialog.errorMessage
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            AccessibleButton {
                Tr { id: trOpenLocationSettings; key: "main.button.openLocationSettings"; fallback: "Open Location Settings"; visible: false }
                text: trOpenLocationSettings.text
                accessibleName: trOpenLocationSettings.text
                visible: bleErrorDialog.isLocationError
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    BLEManager.openLocationSettings()
                    bleErrorDialog.close()
                }
            }

            AccessibleButton {
                Tr { id: trOkBle; key: "common.button.ok"; fallback: "OK"; visible: false }
                text: trOkBle.text
                accessibleName: trDismissDialogBle.text
                Tr { id: trDismissDialogBle; key: "common.accessibility.dismissDialog"; fallback: "Dismiss dialog"; visible: false }
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: bleErrorDialog.close()
            }
        }
    }

    Connections {
        target: BLEManager
        function onErrorOccurred(error) {
            if (error.indexOf("Location") !== -1) {
                bleErrorDialog.isLocationError = true
                bleErrorDialog.errorMessage = "Please enable Location services.\nAndroid requires Location for Bluetooth scanning."
            } else {
                bleErrorDialog.isLocationError = false
                bleErrorDialog.errorMessage = error
            }
            bleErrorDialog.open()
        }
        function onFlowScaleFallback() {
            // Don't show during screensaver or when waking from it
            if (screensaverActive) return
            if (root.justWokeFromSleep) return
            flowScaleDialog.open()
        }
        function onScaleDisconnected() {
            // Don't show during screensaver or when waking from it
            if (screensaverActive) return
            if (root.justWokeFromSleep) return
            scaleDisconnectedDialog.open()
        }
    }

    // FlowScale fallback dialog (no scale found at startup)
    Popup {
        id: flowScaleDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        padding: 24

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        Tr { id: trNoScaleFoundTitle; key: "main.dialog.noScaleFound.title"; fallback: "No Scale Found"; visible: false }
        Tr { id: trNoScaleFoundAnnounce; key: "main.dialog.noScaleFound.announce"; fallback: "No Bluetooth scale detected. Using estimated weight from flow measurement."; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trNoScaleFoundAnnounce.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Text {
                text: trNoScaleFoundTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.noScaleFound.message"
                fallback: "No Bluetooth scale was detected.\n\nUsing estimated weight from DE1 flow measurement instead.\n\nYou can search for your scale in Settings → Bluetooth."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                Tr { id: trOkFlow; key: "common.button.ok"; fallback: "OK"; visible: false }
                Tr { id: trDismissFlow; key: "common.accessibility.dismissDialog"; fallback: "Dismiss dialog"; visible: false }
                text: trOkFlow.text
                accessibleName: trDismissFlow.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: flowScaleDialog.close()
            }
        }
    }

    // Scale disconnected dialog
    Popup {
        id: scaleDisconnectedDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        padding: 24

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        Tr { id: trScaleDisconnectedTitle; key: "main.dialog.scaleDisconnected.title"; fallback: "Scale Disconnected"; visible: false }
        Tr { id: trScaleDisconnectedAnnounce; key: "main.dialog.scaleDisconnected.announce"; fallback: "Warning: Scale disconnected"; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trScaleDisconnectedAnnounce.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Text {
                text: trScaleDisconnectedTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.scaleDisconnected.message"
                fallback: "Your Bluetooth scale has disconnected.\n\nUsing estimated weight from DE1 flow measurement until the scale reconnects.\n\nCheck that your scale is powered on and in range."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                Tr { id: trOkScaleDisc; key: "common.button.ok"; fallback: "OK"; visible: false }
                Tr { id: trDismissScaleDisc; key: "common.accessibility.dismissDialog"; fallback: "Dismiss dialog"; visible: false }
                text: trOkScaleDisc.text
                accessibleName: trDismissScaleDisc.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: scaleDisconnectedDialog.close()
            }
        }
    }

    // Water tank refill dialog
    Popup {
        id: refillDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose
        padding: 24

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        Tr { id: trRefillTitle; key: "main.dialog.refillWater.title"; fallback: "Refill Water Tank"; visible: false }
        Tr { id: trRefillAnnounce; key: "main.dialog.refillWater.announce"; fallback: "Warning: Water tank needs refill"; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trRefillAnnounce.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Text {
                text: trRefillTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.refillWater.message"
                fallback: "The water tank is empty.\n\nPlease refill the water tank and press OK to continue."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                Tr { id: trOkRefill; key: "common.button.ok"; fallback: "OK"; visible: false }
                Tr { id: trDismissRefill; key: "main.accessibility.dismissRefillWarning"; fallback: "Dismiss refill warning"; visible: false }
                text: trOkRefill.text
                accessibleName: trDismissRefill.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: refillDialog.close()
            }
        }
    }

    // Show/hide refill dialog based on machine state
    Connections {
        target: MachineState
        function onPhaseChanged() {
            if (MachineState.phase === MachineStateType.Phase.Refill) {
                refillDialog.open()
            } else if (refillDialog.opened) {
                refillDialog.close()
            }
        }
    }

    // Update notification dialog
    Popup {
        id: updateDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        padding: 24

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Row {
                spacing: 10
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.primaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Tr {
                    key: "update.available"
                    fallback: "Update Available"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }
            }

            Text {
                text: TranslationManager.translate("update.version", "Version") + " " + (MainController.updateChecker ? MainController.updateChecker.latestVersion : "") + " " + TranslationManager.translate("update.isavailable", "is available.") + "\n\n" + TranslationManager.translate("update.downloadprompt", "Would you like to download and install it now?")
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            Row {
                spacing: Theme.spacingMedium
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    text: TranslationManager.translate("update.later", "Later")
                    accessibleName: qsTr("Dismiss update and remind me later")
                    onClicked: {
                        if (MainController.updateChecker) {
                            MainController.updateChecker.dismissUpdate()
                        }
                        updateDialog.close()
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("update.updatenow", "Update Now")
                    primary: true
                    accessibleName: qsTr("Download and install the update now")
                    onClicked: {
                        if (MainController.updateChecker) {
                            MainController.updateChecker.downloadAndInstall()
                        }
                        updateDialog.close()
                        // Navigate to settings update tab (index 9) to show progress
                        goToSettings(9)
                    }
                }
            }
        }
    }

    // Listen for update notification from UpdateChecker
    Connections {
        target: MainController.updateChecker
        enabled: MainController.updateChecker !== null

        function onUpdatePromptRequested() {
            // Don't show during screensaver
            if (!screensaverActive) {
                updateDialog.open()
            }
        }
    }

    // Completion overlay
    property string completionMessage: ""
    property string completionType: ""  // "steam", "hotwater", "flush"

    // Translatable completion messages
    Tr { id: trSteamComplete; key: "main.completion.steam"; fallback: "Steam Complete"; visible: false }
    Tr { id: trHotWaterComplete; key: "main.completion.hotwater"; fallback: "Hot Water Complete"; visible: false }
    Tr { id: trFlushComplete; key: "main.completion.flush"; fallback: "Flush Complete"; visible: false }

    Rectangle {
        id: completionOverlay
        anchors.fill: parent
        color: Theme.backgroundColor
        opacity: 0
        visible: opacity > 0
        z: 500

        Column {
            anchors.centerIn: parent
            spacing: 20

            // Checkmark circle
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 120
                height: 120
                radius: 60
                color: "transparent"
                border.color: Theme.primaryColor
                border.width: 4

                Text {
                    anchors.centerIn: parent
                    text: "\u2713"  // Checkmark
                    font.pixelSize: 60
                    font.bold: true
                    color: Theme.primaryColor
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: completionMessage
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    if (completionType === "hotwater") {
                        return Math.max(0, ScaleDevice ? ScaleDevice.weight : 0).toFixed(0) + "g"
                    } else {
                        return MachineState.shotTime.toFixed(1) + "s"
                    }
                }
                color: Theme.textColor
                font: Theme.timerFont
            }
        }

        Behavior on opacity {
            // Only animate fade-out, not fade-in (instant show prevents settings flash)
            enabled: completionOverlay.opacity > 0
            NumberAnimation { duration: 200 }
        }
    }

    Timer {
        id: completionTimer
        interval: 3000
        onTriggered: {
            console.log("completionTimer: triggered, navigating to idlePage")
            completionOverlay.opacity = 0
            if (pageStack.currentItem && pageStack.currentItem.objectName !== "idlePage") {
                pageStack.replace(idlePage)
            }
        }
    }

    function showCompletion(message, type) {
        console.log("showCompletion: message='" + message + "' type='" + type + "' currentPage=" + (pageStack.currentItem ? pageStack.currentItem.objectName : "null"))
        completionMessage = message
        completionType = type
        completionOverlay.opacity = 1  // Instant (Behavior disabled when opacity is 0)
        completionTimer.start()
    }

    // Espresso stop reason overlay (shown on top of any page)
    property string stopReason: ""  // "manual", "weight", "machine", ""
    property bool stopOverlayVisible: false
    property bool wasEspressoOperation: false  // Track if the operation that just ended was espresso

    function getStopReasonText() {
        switch (stopReason) {
            case "manual": return "Stopped manually"
            case "weight": return "Target weight reached"
            case "machine": return "Profile complete - DE1 stopped the shot"
            default: return "Shot ended"
        }
    }

    Rectangle {
        id: stopReasonOverlay
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(120)  // Above the info bar
        anchors.horizontalCenter: parent.horizontalCenter
        z: 500

        width: stopReasonText.width + Theme.spacingLarge * 2
        height: Theme.scaled(44)
        radius: Theme.scaled(22)
        color: Theme.warningColor

        visible: stopOverlayVisible || fadeOutAnim.running
        opacity: stopOverlayVisible ? 1 : 0
        scale: 1.0

        // Pop-in animation (punch effect: 100% → 110% → 100%)
        SequentialAnimation {
            id: popInAnim
            NumberAnimation {
                target: stopReasonOverlay
                property: "scale"
                from: 1.0
                to: 1.10
                duration: 150
                easing.type: Easing.OutBack
                easing.overshoot: 1.5
            }
            NumberAnimation {
                target: stopReasonOverlay
                property: "scale"
                from: 1.10
                to: 1.0
                duration: 350
                easing.type: Easing.OutBack
                easing.overshoot: 1.5
            }
        }

        // Fade-out animation only (pop-in is instant)
        Behavior on opacity {
            enabled: stopOverlayVisible  // Only animate when hiding
            NumberAnimation { id: fadeOutAnim; duration: 2000 }
        }

        Text {
            id: stopReasonText
            anchors.centerIn: parent
            text: getStopReasonText()
            color: "black"
            font: Theme.bodyFont
        }
    }

    Timer {
        id: stopOverlayTimer
        interval: 3000
        onTriggered: stopOverlayVisible = false
    }

    Connections {
        target: MachineState
        function onTargetWeightReached() {
            root.stopReason = "weight"
        }
        function onShotStarted() {
            // Track if this is an espresso operation (check current phase)
            var phase = MachineState.phase
            root.wasEspressoOperation = (phase === MachineStateType.Phase.EspressoPreheating ||
                                         phase === MachineStateType.Phase.Preinfusion ||
                                         phase === MachineStateType.Phase.Pouring)
            root.stopReason = ""
            root.stopOverlayVisible = false
            stopOverlayTimer.stop()
        }
        function onShotEnded() {
            // Only show stop overlay for espresso operations, not steam/hot water/flush
            if (!root.wasEspressoOperation) {
                return
            }

            // If no reason set, DE1 ended the shot (profile complete or machine-initiated)
            if (root.stopReason === "") {
                root.stopReason = "machine"
            }
            // Show the overlay with pop-in animation
            root.stopOverlayVisible = true
            popInAnim.start()
            stopOverlayTimer.start()
            console.log("Stop overlay:", getStopReasonText())

            // Reset for next operation
            root.wasEspressoOperation = false
        }
    }

    // Crash report dialog - shown on startup if app crashed previously
    CrashReportDialog {
        id: crashReportDialog
        crashLog: PreviousCrashLog || ""
        debugLogTail: PreviousDebugLogTail || ""

        onDismissed: {
            // Clear the crash log file
            MainController.clearCrashLog()
        }
        onReported: {
            // Clear the crash log file after successful report
            MainController.clearCrashLog()
        }
    }

    // First-run welcome dialog
    Popup {
        id: firstRunDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose
        padding: 24

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        Tr { id: trWelcomeTitle; key: "main.dialog.welcome.title"; fallback: "Welcome to Decenza DE1"; visible: false }

        contentItem: Column {
            spacing: Theme.spacingLarge
            width: Theme.dialogWidth

            Text {
                text: trWelcomeTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.welcome.message"
                fallback: "Before we begin:\n\n• Turn on your DE1 by holding the middle stop button for a few seconds\n• Power on your Bluetooth scale\n\nThe app will search for your DE1 espresso machine and compatible scales."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                Tr { id: trContinue; key: "common.button.continue"; fallback: "Continue"; visible: false }
                Tr { id: trContinueToApp; key: "main.accessibility.continueToApp"; fallback: "Continue to app"; visible: false }
                text: trContinue.text
                accessibleName: trContinueToApp.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    Settings.setValue("firstRunComplete", true)
                    firstRunDialog.close()
                    checkStorageSetup()
                }
            }
        }
    }

    // Storage setup dialog (Android 11+ - request MANAGE_EXTERNAL_STORAGE permission)
    Popup {
        id: storageSetupDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose
        padding: 24

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"
        }

        Tr { id: trStorageTitle; key: "main.dialog.storage.title"; fallback: "Save profiles to Documents?"; visible: false }

        contentItem: Column {
            spacing: Theme.spacingLarge
            width: Theme.dialogWidth

            Text {
                text: trStorageTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.storage.message"
                fallback: "Allow Decenza to save profiles to Documents/Decenza so they survive if you reinstall the app."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            Tr {
                key: "main.dialog.storage.warning"
                fallback: "If you skip this, profiles may be lost on reinstall (updates should be fine)."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.labelFont
                color: Theme.warningColor
            }

            Row {
                spacing: Theme.spacingLarge
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    Tr { id: trSkip; key: "common.button.skip"; fallback: "Skip"; visible: false }
                    Tr { id: trSkipStorage; key: "main.accessibility.skipStorageSetup"; fallback: "Skip storage setup"; visible: false }
                    text: trSkip.text
                    accessibleName: trSkipStorage.text
                    onClicked: {
                        ProfileStorage.skipSetup()
                        storageSetupDialog.close()
                        startBluetoothScan()
                    }
                }

                AccessibleButton {
                    Tr { id: trAllow; key: "common.button.allow"; fallback: "Allow"; visible: false }
                    Tr { id: trAllowStorage; key: "main.accessibility.allowStorageAccess"; fallback: "Allow storage access"; visible: false }
                    text: trAllow.text
                    accessibleName: trAllowStorage.text
                    onClicked: {
                        ProfileStorage.selectFolder()
                        // Dialog stays open - will close when app resumes with permission granted
                    }
                }
            }
        }
    }

    // Check permission when app becomes active (user returns from settings)
    Connections {
        target: Qt.application
        function onStateChanged(state) {
            if (state === Qt.ApplicationActive && storageSetupDialog.opened) {
                // User returned from settings - check if permission was granted
                ProfileStorage.checkPermissionAndNotify()
            }
        }
    }

    // Handle permission result
    Connections {
        target: ProfileStorage
        function onFolderSelected(success) {
            if (storageSetupDialog.opened) {
                storageSetupDialog.close()
                startBluetoothScan()
            }
        }
    }

    // Check if storage setup is needed (Android only)
    function checkStorageSetup() {
        if (ProfileStorage.needsSetup) {
            storageSetupDialog.open()
        } else {
            startBluetoothScan()
        }
    }

    // Start BLE scanning (called after first-run dialog or on subsequent launches)
    function startBluetoothScan() {
        if (BLEManager.hasSavedScale) {
            // Try direct connect if we have a saved scale (this also starts scanning)
            BLEManager.tryDirectConnectToScale()
        } else {
            // First run or no saved scale - scan for scales so user can pair one
            BLEManager.scanForScales()
        }
        // Always start scanning after a delay (startScan is safe to call multiple times)
        scanDelayTimer.start()
    }

    Timer {
        id: scanDelayTimer
        interval: 1000  // Give direct connect time, then ensure scan is running
        onTriggered: BLEManager.startScan()
    }

    // Status bar overlay (hidden during screensaver)
    StatusBar {
        id: statusBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.statusBarHeight
        z: 600  // Above completionOverlay (500)
        visible: !screensaverActive
    }

    // Track previous phase to detect operation starts
    property int previousPhase: MachineStateType.Phase.Disconnected

    // Connection state handler - auto navigate based on machine state
    Connections {
        target: MachineState

        function onPhaseChanged() {
            let phase = MachineState.phase
            let currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""
            let wasIdle = (root.previousPhase === MachineStateType.Phase.Idle ||
                          root.previousPhase === MachineStateType.Phase.Ready ||
                          root.previousPhase === MachineStateType.Phase.Heating)

            // Apply settings when entering operations (to handle GHC-initiated starts)
            if (phase === MachineStateType.Phase.Steaming && wasIdle) {
                // Start steam heating when entering steam (from GHC button)
                // Uses startSteamHeating() to force heater on regardless of keepSteamHeaterOn
                MainController.startSteamHeating()
                console.log("Started steam heating on phase change to Steaming")
                // Stop any pending auto-flush timer when starting new steam
                steamAutoFlushTimer.stop()
            } else if (phase === MachineStateType.Phase.HotWater && wasIdle) {
                MainController.applyHotWaterSettings()
                console.log("Applied hot water settings on phase change")
            } else if (phase === MachineStateType.Phase.Flushing && wasIdle) {
                MainController.applyFlushSettings()
                console.log("Applied flush settings on phase change")
            }

            // Check if steaming just ended
            let wasSteamingBefore = (root.previousPhase === MachineStateType.Phase.Steaming)
            if (wasSteamingBefore && (phase === MachineStateType.Phase.Idle || phase === MachineStateType.Phase.Ready)) {
                // Turn off steam heater if keepSteamHeaterOn is false
                if (!Settings.keepSteamHeaterOn) {
                    console.log("Steaming ended, turning off steam heater (keepSteamHeaterOn=false)")
                    MainController.turnOffSteamHeater()
                }
                // Start auto-flush timer if enabled
                if (Settings.steamAutoFlushSeconds > 0) {
                    console.log("Steaming ended, starting auto-flush timer for", Settings.steamAutoFlushSeconds, "seconds")
                    steamAutoFlushTimer.restart()
                }
            }

            // Update previous phase tracking
            root.previousPhase = phase

            // Navigate to active operation pages (skip during calibration mode or page transition)
            if (phase === MachineStateType.Phase.EspressoPreheating ||
                phase === MachineStateType.Phase.Preinfusion ||
                phase === MachineStateType.Phase.Pouring ||
                phase === MachineStateType.Phase.Ending) {
                if (currentPage !== "espressoPage" && !root.calibrationInProgress && !pageStack.busy) {
                    pageStack.replace(espressoPage)
                }
            } else if (phase === MachineStateType.Phase.Steaming) {
                if (currentPage !== "steamPage" && !pageStack.busy) {
                    pageStack.replace(steamPage)
                }
            } else if (phase === MachineStateType.Phase.HotWater) {
                if (currentPage !== "hotWaterPage" && !pageStack.busy) {
                    pageStack.replace(hotWaterPage)
                }
            } else if (phase === MachineStateType.Phase.Flushing) {
                if (currentPage !== "flushPage" && !pageStack.busy) {
                    pageStack.replace(flushPage)
                }
            } else if (phase === MachineStateType.Phase.Descaling) {
                if (currentPage !== "descalingPage" && !pageStack.busy) {
                    pageStack.replace(descalingPage)
                }
            } else if (phase === MachineStateType.Phase.Cleaning) {
                // For now, cleaning uses the built-in machine routine
                // Could navigate to a cleaning page in the future
            } else if (phase === MachineStateType.Phase.Idle || phase === MachineStateType.Phase.Ready) {
                // DE1 went to idle - if we're on an operation page, show completion
                // Note: Don't check pageStack.busy here - completion must always be handled
                console.log("Phase Idle/Ready: currentPage=" + currentPage + " completionOverlay.opacity=" + completionOverlay.opacity)

                if (currentPage === "steamPage") {
                    showCompletion(trSteamComplete.text, "steam")
                } else if (currentPage === "hotWaterPage") {
                    showCompletion(trHotWaterComplete.text, "hotwater")
                } else if (currentPage === "flushPage") {
                    showCompletion(trFlushComplete.text, "flush")
                } else {
                    console.log("Phase Idle/Ready: NOT on operation page, no completion shown")
                }

                // Pre-load all operation settings when machine is Ready
                // This ensures GHC-initiated operations use correct settings immediately
                if (phase === MachineStateType.Phase.Ready) {
                    MainController.applySteamSettings()
                    MainController.applyHotWaterSettings()
                    MainController.applyFlushSettings()
                    console.log("Pre-loaded steam/hot water/flush settings for Ready state")
                } else if (Settings.keepSteamHeaterOn && !Settings.steamDisabled) {
                    // Keep steam heater on when idle if setting is enabled
                    MainController.applySteamSettings()
                }
            }
        }
    }

    // Helper functions for navigation
    // Note: startNavigation() guard prevents double-taps on user-initiated navigation
    // Note: Page announcements are handled centrally by announceCurrentPage() on page change
    function goToIdle() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "idlePage") {
            pageStack.replace(idlePage)
        }
    }

    function goToEspresso() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "espressoPage") {
            pageStack.replace(espressoPage)
        }
    }

    function goToSteam() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "steamPage") {
            pageStack.replace(steamPage)
        }
    }

    function goToHotWater() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "hotWaterPage") {
            pageStack.replace(hotWaterPage)
        }
    }

    function goToSettings(tabIndex) {
        if (!startNavigation()) return
        if (tabIndex !== undefined && tabIndex >= 0) {
            pageStack.push(settingsPage, {requestedTabIndex: tabIndex})
        } else {
            pageStack.push(settingsPage)
        }
    }

    function goBack() {
        if (!startNavigation()) return
        if (pageStack.depth > 1) {
            pageStack.pop()
        }
    }

    function goToProfileEditor() {
        if (!startNavigation()) return
        // Route to D-Flow editor for recipe-mode profiles, Advanced editor for frame-based
        if (MainController.isCurrentProfileRecipe) {
            pageStack.push(recipeEditorPage)
        } else {
            pageStack.push(profileEditorPage)
        }
    }

    function goToRecipeEditor() {
        if (!startNavigation()) return
        // Explicitly go to D-Flow editor
        pageStack.push(recipeEditorPage)
    }

    function switchToRecipeEditor() {
        if (!startNavigation()) return
        // Replace current editor with D-Flow editor (for switching between editors)
        pageStack.replace(recipeEditorPage)
    }

    function switchToAdvancedEditor() {
        if (!startNavigation()) return
        // Replace current editor with Advanced editor (for switching between editors)
        pageStack.replace(profileEditorPage)
    }

    function goToAdvancedEditor() {
        if (!startNavigation()) return
        // Explicitly go to Advanced editor
        pageStack.push(profileEditorPage)
    }

    function goToProfileSelector() {
        if (!startNavigation()) return
        pageStack.push(profileSelectorPage)
    }

    function goToDescaling() {
        if (!startNavigation()) return
        pageStack.push(descalingPage)
    }

    function goToFlush() {
        if (!startNavigation()) return
        pageStack.push(flushPage)
    }

    function goToVisualizerBrowser() {
        if (!startNavigation()) return
        pageStack.push(visualizerBrowserPage)
    }

    function goToProfileImport() {
        if (!startNavigation()) return
        pageStack.push(profileImportPage)
    }

    function goToShotMetadata(shotId) {
        if (!startNavigation()) return
        // Pass editShotId to edit the just-saved shot (always use edit mode now)
        pageStack.push(postShotReviewPage, { editShotId: shotId || 0 })
    }

    // Helper to announce arbitrary text for accessibility (used for non-page announcements)
    function announceNavigation(text) {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(text)
        }
    }

    // Clean up text for TTS (replace underscores, expand units, etc.)
    function cleanForSpeech(text) {
        if (!text) return ""
        var cleaned = text
        // Remove common file extensions
        cleaned = cleaned.replace(/\.(json|tcl|txt)$/i, "")
        // Replace underscores and hyphens with spaces
        cleaned = cleaned.replace(/[_-]/g, " ")
        // Expand units for natural speech
        cleaned = cleaned.replace(/°C/g, " degrees Celsius")
        cleaned = cleaned.replace(/(\d)\s*C\b/g, "$1 degrees Celsius")  // "72C" -> "72 degrees Celsius"
        cleaned = cleaned.replace(/(\d)\s*ml\b/gi, "$1 milliliters")
        cleaned = cleaned.replace(/(\d)\s*g\b/g, "$1 grams")
        cleaned = cleaned.replace(/(\d)\s*bar\b/gi, "$1 bar")
        cleaned = cleaned.replace(/(\d)\s*s\b/g, "$1 seconds")
        cleaned = cleaned.replace(/(\d)\s*%/g, "$1 percent")
        // Remove multiple spaces
        cleaned = cleaned.replace(/\s+/g, " ")
        return cleaned.trim()
    }

    property bool screensaverActive: false

    function goToScreensaver() {
        console.log("[Main] goToScreensaver called, type:", ScreensaverManager.screensaverType)
        screensaverActive = true
        // Reset sleep counters (stopped state)
        root.sleepCountdownNormal = 0
        root.sleepCountdownStayAwake = 0
        // Navigate to screensaver page for all modes (including "disabled")
        // For "disabled" mode, ScreensaverPage shows a black screen and lets
        // Android's system timeout turn off the screen naturally
        pageStack.replace(screensaverPage)
    }

    function goToIdleFromScreensaver() {
        screensaverActive = false
        // Restore screen brightness in case disabled mode dimmed it
        ScreensaverManager.restoreScreenBrightness()
        // Initialize sleep countdown (stayAwake is set separately by onAutoWakeTriggered if needed)
        root.sleepCountdownNormal = root.autoSleepMinutes
        root.sleepCountdownStayAwake = 0  // Already satisfied unless auto-wake sets it
        console.log("Waking from screensaver: normal countdown=" + root.sleepCountdownNormal)
        pageStack.replace(idlePage)
    }

    Component {
        id: screensaverPage
        ScreensaverPage {}
    }

    // Touch capture to reset sleep countdown (transparent, doesn't block input)
    MouseArea {
        anchors.fill: parent
        z: 1000  // Above everything
        propagateComposedEvents: true
        onPressed: function(mouse) {
            // Reset normal countdown on user touch (but not stayAwake)
            if (root.autoSleepMinutes > 0 && !screensaverActive) {
                root.sleepCountdownNormal = root.autoSleepMinutes
            }
            mouse.accepted = false  // Let the touch through
        }
        onReleased: function(mouse) { mouse.accepted = false }
        onClicked: function(mouse) { mouse.accepted = false }
    }

    // Keyboard shortcut for simulation mode (Ctrl+D)
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: {
            var newState = !DE1Device.simulationMode
            console.log("Toggling simulation mode:", newState ? "ON" : "OFF")
            DE1Device.simulationMode = newState
            if (ScaleDevice) {
                ScaleDevice.simulationMode = newState
            }
        }
    }

    // Keyboard shortcuts for machine control (like original DE1 app)
    // E = Espresso
    Shortcut {
        sequence: "E"
        onActivated: {
            if (MachineState.isReady) {
                console.log("[Keyboard] Starting espresso via 'E' key")
                DE1Device.startEspresso()
            } else {
                console.log("[Keyboard] Cannot start espresso - machine not ready, phase:", MachineState.phase)
            }
        }
    }

    // S = Steam
    Shortcut {
        sequence: "S"
        onActivated: {
            if (MachineState.isReady) {
                console.log("[Keyboard] Starting steam via 'S' key")
                DE1Device.startSteam()
            } else {
                console.log("[Keyboard] Cannot start steam - machine not ready, phase:", MachineState.phase)
            }
        }
    }

    // W = Hot Water
    Shortcut {
        sequence: "W"
        onActivated: {
            if (MachineState.isReady) {
                console.log("[Keyboard] Starting hot water via 'W' key")
                DE1Device.startHotWater()
            } else {
                console.log("[Keyboard] Cannot start hot water - machine not ready, phase:", MachineState.phase)
            }
        }
    }

    // F = Flush
    Shortcut {
        sequence: "F"
        onActivated: {
            if (MachineState.isReady) {
                console.log("[Keyboard] Starting flush via 'F' key")
                DE1Device.startFlush()
            } else {
                console.log("[Keyboard] Cannot start flush - machine not ready, phase:", MachineState.phase)
            }
        }
    }

    // Space = Stop / Go to Idle
    Shortcut {
        sequence: "Space"
        onActivated: {
            console.log("[Keyboard] Stop/Idle via Space key, phase:", MachineState.phase)
            DE1Device.stopOperation()
            root.goToIdle()
        }
    }

    // P = Sleep
    Shortcut {
        sequence: "P"
        onActivated: {
            console.log("[Keyboard] Going to sleep via 'P' key")
            // Put scale to LCD-off mode (keep connected for wake)
            if (ScaleDevice && ScaleDevice.connected) {
                ScaleDevice.disableLcd()
            }
            DE1Device.goToSleep()
            goToScreensaver()
        }
    }

    // 2-finger swipe detection for back gesture (accessibility)
    MultiPointTouchArea {
        anchors.fill: parent
        z: -1  // Behind all controls
        minimumTouchPoints: 2
        maximumTouchPoints: 2
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

        property var startPoints: []

        onPressed: function(touchPoints) {
            if (touchPoints.length === 2) {
                startPoints = [{x: touchPoints[0].x, y: touchPoints[0].y},
                               {x: touchPoints[1].x, y: touchPoints[1].y}]
            }
        }

        onReleased: function(touchPoints) {
            // Check for 2-finger swipe left (back gesture) when accessibility is on
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled &&
                startPoints.length === 2 && touchPoints.length === 2) {
                var deltaX1 = touchPoints[0].x - startPoints[0].x
                var deltaX2 = touchPoints[1].x - startPoints[1].x
                var avgDeltaX = (deltaX1 + deltaX2) / 2

                // Swipe left threshold: -100 pixels
                if (avgDeltaX < -100) {
                    // Two-finger swipe left = go back
                    AccessibilityManager.announce(trAnnounceGoingBack.text)
                    if (stackView.depth > 1) {
                        stackView.pop()
                    } else {
                        goToIdle()
                    }
                }
            }
            startPoints = []
        }
    }

    // ============ ACCESSIBILITY: Frame Change Ticks ============
    Connections {
        target: MainController
        enabled: AccessibilityManager.enabled

        function onFrameChanged(frameIndex, frameName) {
            AccessibilityManager.playTick()
        }
    }

    // DYE: Navigate to shot metadata page after shot ends
    Connections {
        target: MainController

        function onShotEndedShowMetadata() {
            console.log("Shot ended, showing metadata page with shotId:", MainController.lastSavedShotId)
            // Restart timer to ensure overlay survives page change
            stopOverlayTimer.restart()
            goToShotMetadata(MainController.lastSavedShotId)
        }
    }

    // Auto-wake: Exit screensaver when scheduled wake time is reached
    Connections {
        target: MainController

        function onAutoWakeTriggered() {
            console.log("[Main] Auto-wake triggered, exiting screensaver")
            if (screensaverActive) {
                goToIdleFromScreensaver()
                // Set stay-awake countdown if enabled (after goToIdleFromScreensaver sets normal)
                if (Settings.autoWakeStayAwakeEnabled && Settings.autoWakeStayAwakeMinutes > 0) {
                    root.sleepCountdownStayAwake = Settings.autoWakeStayAwakeMinutes
                    console.log("Auto-wake: stayAwake countdown=" + root.sleepCountdownStayAwake)
                }
            }
        }

        function onRemoteSleepRequested() {
            console.log("[Main] Remote sleep requested via MQTT/REST API")
            if (!screensaverActive) {
                goToScreensaver()
            }
        }
    }

    // ============ ACCESSIBILITY: Machine State Announcements ============
    // Translatable accessibility announcements for machine state
    Tr { id: trAnnounceDisconnected; key: "main.accessibility.machineDisconnected"; fallback: "Machine disconnected"; visible: false }
    Tr { id: trAnnounceSleeping; key: "main.accessibility.machineSleeping"; fallback: "Machine sleeping"; visible: false }
    Tr { id: trAnnounceIdle; key: "main.accessibility.machineIdle"; fallback: "Machine idle"; visible: false }
    Tr { id: trAnnounceHeating; key: "main.accessibility.heating"; fallback: "Heating"; visible: false }
    Tr { id: trAnnounceReady; key: "main.accessibility.ready"; fallback: "Ready"; visible: false }
    Tr { id: trAnnouncePreheating; key: "main.accessibility.preheatingEspresso"; fallback: "Preheating for espresso"; visible: false }
    Tr { id: trAnnouncePreinfusion; key: "main.accessibility.preinfusionStarted"; fallback: "Preinfusion started"; visible: false }
    Tr { id: trAnnouncePouring; key: "main.accessibility.pouring"; fallback: "Pouring"; visible: false }
    Tr { id: trAnnounceShotComplete; key: "main.accessibility.shotComplete"; fallback: "Shot complete"; visible: false }
    Tr { id: trAnnounceSteaming; key: "main.accessibility.steaming"; fallback: "Steaming"; visible: false }
    Tr { id: trAnnounceHotWater; key: "main.accessibility.dispensingHotWater"; fallback: "Dispensing hot water"; visible: false }
    Tr { id: trAnnounceFlushing; key: "main.accessibility.flushing"; fallback: "Flushing"; visible: false }
    Tr { id: trAnnounceDescaling; key: "main.accessibility.descaling"; fallback: "Descaling in progress"; visible: false }
    Tr { id: trAnnounceCleaning; key: "main.accessibility.cleaning"; fallback: "Cleaning in progress"; visible: false }

    Connections {
        target: MachineState
        enabled: AccessibilityManager.enabled

        function onPhaseChanged() {
            var phase = MachineState.phase
            var announcement = ""

            switch (phase) {
                case MachineStateType.Phase.Disconnected:
                    announcement = trAnnounceDisconnected.text
                    break
                case MachineStateType.Phase.Sleep:
                    announcement = trAnnounceSleeping.text
                    break
                case MachineStateType.Phase.Idle:
                    announcement = trAnnounceIdle.text
                    break
                case MachineStateType.Phase.Heating:
                    announcement = trAnnounceHeating.text
                    break
                case MachineStateType.Phase.Ready:
                    announcement = trAnnounceReady.text
                    break
                case MachineStateType.Phase.EspressoPreheating:
                    announcement = trAnnouncePreheating.text
                    break
                case MachineStateType.Phase.Preinfusion:
                    announcement = trAnnouncePreinfusion.text
                    break
                case MachineStateType.Phase.Pouring:
                    announcement = trAnnouncePouring.text
                    break
                case MachineStateType.Phase.Ending:
                    announcement = trAnnounceShotComplete.text
                    break
                case MachineStateType.Phase.Steaming:
                    announcement = trAnnounceSteaming.text
                    break
                case MachineStateType.Phase.HotWater:
                    announcement = trAnnounceHotWater.text
                    break
                case MachineStateType.Phase.Flushing:
                    announcement = trAnnounceFlushing.text
                    break
                case MachineStateType.Phase.Descaling:
                    announcement = trAnnounceDescaling.text
                    break
                case MachineStateType.Phase.Cleaning:
                    announcement = trAnnounceCleaning.text
                    break
            }

            if (announcement.length > 0) {
                AccessibilityManager.announce(announcement, true)
            }
        }
    }

    // ============ ACCESSIBILITY: Connection Status Announcements ============
    Tr { id: trAnnounceMachineConnected; key: "main.accessibility.machineConnected"; fallback: "Machine connected"; visible: false }
    Tr { id: trAnnounceScaleConnected; key: "main.accessibility.scaleConnected"; fallback: "Scale connected:"; visible: false }
    Tr { id: trAnnounceGoingBack; key: "main.accessibility.goingBack"; fallback: "Going back"; visible: false }

    Connections {
        target: DE1Device
        enabled: AccessibilityManager.enabled

        function onConnectedChanged() {
            if (DE1Device.connected) {
                AccessibilityManager.announce(trAnnounceMachineConnected.text)
            } else {
                AccessibilityManager.announce(trAnnounceDisconnected.text, true)
            }
        }
    }

    Connections {
        target: ScaleDevice
        enabled: AccessibilityManager.enabled && ScaleDevice !== null

        function onConnectedChanged() {
            if (ScaleDevice && ScaleDevice.connected) {
                AccessibilityManager.announce(trAnnounceScaleConnected.text + " " + ScaleDevice.name)
            }
            // Disconnection is handled by scaleDisconnectedDialog
        }
    }

    // ============ PER-PAGE SCALE CONFIGURATION OVERLAY ============
    // Floating centered control for adjusting page scale
    // Uses scaledBase() to maintain consistent size regardless of current page scale
    Rectangle {
        id: pageScaleOverlay
        visible: root.appInitialized && Theme.configurePageScaleEnabled && !screensaverActive && Theme.currentPageObjectName !== ""
        z: 800  // Above most content, below dialogs

        onVisibleChanged: console.log("pageScaleOverlay visible:", visible,
            "appInit:", root.appInitialized,
            "enabled:", Theme.configurePageScaleEnabled,
            "screensaver:", screensaverActive,
            "page:", Theme.currentPageObjectName)

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaledBase(8)

        width: scaleRow.width + Theme.scaledBase(24) * 2
        height: Theme.scaledBase(40)
        radius: height / 2
        color: Theme.surfaceColor
        border.width: 1
        border.color: Theme.primaryColor

        RowLayout {
            id: scaleRow
            anchors.centerIn: parent
            spacing: Theme.scaledBase(8)

            Text {
                text: "Scale:"
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaledBase(12)
            }

            ValueInput {
                id: pageScaleInput
                value: Theme.pageScaleMultiplier
                from: 0.3
                to: 2.0
                stepSize: 0.05
                decimals: 2
                suffix: "x"
                valueColor: Theme.primaryColor
                useBaseScale: true  // Use scaledBase() for consistent size
                accessibleName: qsTr("Page scale")

                onValueModified: function(newValue) {
                    Theme.pageScaleMultiplier = newValue
                    Settings.setValue("pageScale/" + Theme.currentPageObjectName, newValue)
                }
            }
        }
    }

    // ============ GLOBAL HIDE KEYBOARD BUTTON ============
    // Appears when soft keyboard is visible - positioned at top right below status bar
    Rectangle {
        id: globalHideKeyboardButton
        visible: Qt.inputMethod.visible
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin + 4
        width: hideKeyboardText.width + 24
        height: 28
        radius: 14
        color: Theme.primaryColor
        z: 9999  // Above everything

        Tr {
            id: hideKeyboardText
            anchors.centerIn: parent
            key: "main.button.hideKeyboard"
            fallback: "Hide keyboard"
            color: "white"
            font.pixelSize: 13
            font.bold: true
        }

        MouseArea {
            anchors.fill: parent
            onClicked: Qt.inputMethod.hide()
        }
    }
}
