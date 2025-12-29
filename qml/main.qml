import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1

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


    // Put machine and scale to sleep when closing the app
    onClosing: function(close) {
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

    // Auto-sleep inactivity timer
    property int autoSleepMinutes: {
        var val = Settings.value("autoSleepMinutes", 60)
        return (val === undefined || val === null) ? 60 : parseInt(val)
    }

    Timer {
        id: inactivityTimer
        interval: root.autoSleepMinutes * 60 * 1000  // Convert minutes to ms
        running: root.autoSleepMinutes > 0 && !screensaverActive
        repeat: false
        onTriggered: {
            console.log("Auto-sleep triggered after", root.autoSleepMinutes, "minutes of inactivity")
            triggerAutoSleep()
        }
    }

    // Restart timer when settings change
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "autoSleepMinutes") {
                var val = Settings.value("autoSleepMinutes", 60)
                root.autoSleepMinutes = (val === undefined || val === null) ? 60 : parseInt(val)
                resetInactivityTimer()
            }
        }
    }

    function resetInactivityTimer() {
        if (root.autoSleepMinutes > 0) {
            inactivityTimer.restart()
        }
    }

    function triggerAutoSleep() {
        // Put scale to sleep
        if (ScaleDevice && ScaleDevice.connected) {
            ScaleDevice.sleep()
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

    // Suppress scale dialogs briefly after waking from sleep
    property bool justWokeFromSleep: false
    Timer {
        id: wakeSuppressionTimer
        interval: 2000  // Suppress dialogs for 2 seconds after wake
        onTriggered: root.justWokeFromSleep = false
    }

    // Update scale factor when window resizes
    onWidthChanged: updateScale()
    onHeightChanged: updateScale()
    Component.onCompleted: {
        updateScale()

        // Check for first run and show welcome dialog or start scanning
        var firstRunComplete = Settings.value("firstRunComplete", false)
        if (!firstRunComplete) {
            firstRunDialog.open()
        } else {
            startBluetoothScan()
        }
    }

    function updateScale() {
        // Scale based on window size vs reference (960x600 tablet dp)
        var scaleX = width / Theme.refWidth
        var scaleY = height / Theme.refHeight
        Theme.scale = Math.min(scaleX, scaleY)
    }

    // Global tap handler for accessibility - announces any Text tapped
    MouseArea {
        id: accessibilityTapOverlay
        anchors.fill: parent
        z: 10000  // Above everything
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        propagateComposedEvents: true

        onPressed: function(mouse) {
            var textItem = findTextAt(parent, mouse.x, mouse.y)
            if (textItem && textItem.text) {
                AccessibilityManager.announceLabel(cleanForSpeech(textItem.text))
            }
            mouse.accepted = false
        }

        onClicked: function(mouse) { mouse.accepted = false }
        onReleased: function(mouse) { mouse.accepted = false }
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
            id: profileEditorPage
            ProfileEditorPage {}
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
            id: flushPage
            FlushPage {}
        }

        Component {
            id: visualizerBrowserPage
            VisualizerBrowserPage {}
        }
    }

    // Global error dialog for BLE issues
    Dialog {
        id: bleErrorDialog
        title: "Enable Location"
        modal: true
        anchors.centerIn: parent

        property string errorMessage: ""
        property bool isLocationError: false

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce("Error: " + errorMessage, true)
            }
        }

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: bleErrorDialog.errorMessage
                wrapMode: Text.Wrap
                width: parent.width
            }

            AccessibleButton {
                text: "Open Location Settings"
                accessibleName: "Open location settings"
                visible: bleErrorDialog.isLocationError
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    BLEManager.openLocationSettings()
                    bleErrorDialog.close()
                }
            }

            AccessibleButton {
                text: "OK"
                accessibleName: "Dismiss dialog"
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
    Dialog {
        id: flowScaleDialog
        title: "No Scale Found"
        modal: true
        anchors.centerIn: parent

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce("No Bluetooth scale detected. Using estimated weight from flow measurement.", true)
            }
        }

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: "No Bluetooth scale was detected.\n\nUsing estimated weight from DE1 flow measurement instead.\n\nYou can search for your scale in Settings → Bluetooth."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.labelFont
            }

            AccessibleButton {
                text: "OK"
                accessibleName: "Dismiss dialog"
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: flowScaleDialog.close()
            }
        }
    }

    // Scale disconnected dialog
    Dialog {
        id: scaleDisconnectedDialog
        title: "Scale Disconnected"
        modal: true
        anchors.centerIn: parent

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce("Warning: Scale disconnected", true)
            }
        }

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: "Your Bluetooth scale has disconnected.\n\nUsing estimated weight from DE1 flow measurement until the scale reconnects.\n\nCheck that your scale is powered on and in range."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.labelFont
            }

            AccessibleButton {
                text: "OK"
                accessibleName: "Dismiss dialog"
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: scaleDisconnectedDialog.close()
            }
        }
    }

    // Water tank refill dialog
    Dialog {
        id: refillDialog
        title: "Refill Water Tank"
        modal: true
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce("Warning: Water tank needs refill", true)
            }
        }

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: "The water tank is empty.\n\nPlease refill the water tank and press OK to continue."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: "OK"
                accessibleName: "Dismiss refill warning"
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

    // Completion overlay
    property string completionMessage: ""
    property string completionType: ""  // "steam", "hotwater", "flush"

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
                        return Math.max(0, ScaleDevice.weight).toFixed(0) + "g"
                    } else {
                        return MachineState.shotTime.toFixed(1) + "s"
                    }
                }
                color: Theme.textColor
                font: Theme.timerFont
            }
        }

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }

    Timer {
        id: completionTimer
        interval: 3000
        onTriggered: {
            completionOverlay.opacity = 0
            pageStack.replace(idlePage)
        }
    }

    function showCompletion(message, type) {
        completionMessage = message
        completionType = type
        completionOverlay.opacity = 1
        completionTimer.start()
    }

    // First-run welcome dialog
    Dialog {
        id: firstRunDialog
        title: "Welcome to Decenza DE1"
        modal: true
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose

        Column {
            spacing: Theme.spacingLarge
            width: Theme.dialogWidth

            Label {
                text: "Before we begin:\n\n• Turn on your DE1 by holding the middle stop button for a few seconds\n• Power on your Bluetooth scale\n\nThe app will search for your DE1 espresso machine and compatible scales."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: "Continue"
                accessibleName: "Continue to app"
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    Settings.setValue("firstRunComplete", true)
                    firstRunDialog.close()
                    startBluetoothScan()
                }
            }
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
        z: 100
        visible: !screensaverActive
    }

    // Connection state handler - auto navigate based on machine state
    Connections {
        target: MachineState

        function onPhaseChanged() {
            let phase = MachineState.phase
            let currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""

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
            } else if (phase === MachineStateType.Phase.Idle || phase === MachineStateType.Phase.Ready) {
                // DE1 went to idle - if we're on an operation page, show completion
                // Note: Don't check pageStack.busy here - completion must always be handled

                if (currentPage === "steamPage") {
                    showCompletion("Steam Complete", "steam")
                } else if (currentPage === "hotWaterPage") {
                    showCompletion("Hot Water Complete", "hotwater")
                } else if (currentPage === "flushPage") {
                    showCompletion("Flush Complete", "flush")
                }
            }
        }
    }

    // Helper functions for navigation
    function goToIdle() {
        pageStack.replace(idlePage)
    }

    function goToEspresso() {
        pageStack.replace(espressoPage)
    }

    function goToSteam() {
        announceNavigation("Steam settings")
        pageStack.replace(steamPage)
    }

    function goToHotWater() {
        announceNavigation("Hot water settings")
        pageStack.replace(hotWaterPage)
    }

    function goToSettings() {
        announceNavigation("Settings")
        pageStack.push(settingsPage)
    }

    function goBack() {
        if (pageStack.depth > 1) {
            pageStack.pop()
        }
    }

    function goToProfileEditor() {
        announceNavigation("Profile editor")
        pageStack.push(profileEditorPage)
    }

    function goToProfileSelector() {
        announceNavigation("Select profile")
        pageStack.push(profileSelectorPage)
    }

    function goToDescaling() {
        announceNavigation("Descaling wizard")
        pageStack.push(descalingPage)
    }

    function goToFlush() {
        announceNavigation("Flush settings")
        pageStack.push(flushPage)
    }

    function goToVisualizerBrowser() {
        announceNavigation("Visualizer browser")
        pageStack.push(visualizerBrowserPage)
    }

    // Helper to announce page navigation for accessibility
    function announceNavigation(pageName) {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(pageName)
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
        screensaverActive = true
        pageStack.replace(screensaverPage)
    }

    function goToIdleFromScreensaver() {
        screensaverActive = false
        pageStack.replace(idlePage)
    }

    Component {
        id: screensaverPage
        ScreensaverPage {}
    }

    // Touch capture to reset inactivity timer (transparent, doesn't block input)
    MouseArea {
        anchors.fill: parent
        z: 1000  // Above everything
        propagateComposedEvents: true
        onPressed: function(mouse) {
            resetInactivityTimer()
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

    // Simulation mode indicator banner
    Rectangle {
        visible: DE1Device.simulationMode
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 10
        width: simLabel.implicitWidth + 20
        height: simLabel.implicitHeight + 10
        radius: 4
        color: "#E65100"
        z: 999

        Text {
            id: simLabel
            anchors.centerIn: parent
            text: "SIMULATION MODE (Ctrl+D to toggle)"
            color: "white"
            font.pixelSize: 14
            font.bold: true
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
                    AccessibilityManager.announce("Going back")
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

    // ============ ACCESSIBILITY: Machine State Announcements ============
    Connections {
        target: MachineState
        enabled: AccessibilityManager.enabled

        function onPhaseChanged() {
            var phase = MachineState.phase
            var announcement = ""

            switch (phase) {
                case MachineStateType.Phase.Disconnected:
                    announcement = "Machine disconnected"
                    break
                case MachineStateType.Phase.Sleep:
                    announcement = "Machine sleeping"
                    break
                case MachineStateType.Phase.Idle:
                    announcement = "Machine idle"
                    break
                case MachineStateType.Phase.Heating:
                    announcement = "Heating"
                    break
                case MachineStateType.Phase.Ready:
                    announcement = "Ready"
                    break
                case MachineStateType.Phase.EspressoPreheating:
                    announcement = "Preheating for espresso"
                    break
                case MachineStateType.Phase.Preinfusion:
                    announcement = "Preinfusion started"
                    break
                case MachineStateType.Phase.Pouring:
                    announcement = "Pouring"
                    break
                case MachineStateType.Phase.Ending:
                    announcement = "Shot complete"
                    break
                case MachineStateType.Phase.Steaming:
                    announcement = "Steaming"
                    break
                case MachineStateType.Phase.HotWater:
                    announcement = "Dispensing hot water"
                    break
                case MachineStateType.Phase.Flushing:
                    announcement = "Flushing"
                    break
            }

            if (announcement.length > 0) {
                AccessibilityManager.announce(announcement, true)
            }
        }
    }

    // ============ ACCESSIBILITY: Connection Status Announcements ============
    Connections {
        target: DE1Device
        enabled: AccessibilityManager.enabled

        function onConnectedChanged() {
            if (DE1Device.connected) {
                AccessibilityManager.announce("Machine connected")
            } else {
                AccessibilityManager.announce("Machine disconnected", true)
            }
        }
    }

    Connections {
        target: ScaleDevice
        enabled: AccessibilityManager.enabled && ScaleDevice !== null

        function onConnectedChanged() {
            if (ScaleDevice && ScaleDevice.connected) {
                AccessibilityManager.announce("Scale connected: " + ScaleDevice.name)
            }
            // Disconnection is handled by scaleDisconnectedDialog
        }
    }
}
