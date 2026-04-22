import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
import Decenza
import "components"
import "components/library"

ApplicationWindow {
    id: root
    visible: true
    visibility: Qt.platform.os === "android" ? Window.FullScreen : Window.AutomaticVisibility
    // On desktop use reference size; on Android let system control fullscreen
    width: 960
    height: 600
    title: "Decenza"
    color: Theme.backgroundColor

    // Override Qt 6.9+ automatic safe area padding on ApplicationWindow.
    // Qt reads Android system bar insets and offsets contentItem, even in
    // fullscreen immersive mode. This causes a gap on some tablets (e.g.
    // Lenovo Tab One #582). Since we run fullscreen with system bars hidden,
    // there is nothing to protect content from — use the full window.
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0

    // Debug flag to force live view on operation pages (for development)
    property bool debugLiveView: false

    // Flag to open BrewDialog when IdlePage becomes active (set by AutoFavoritesPage)
    property bool pendingBrewDialog: false

    // Track page to return to after steam/flush/water operations complete
    // This allows returning to postShotReviewPage instead of always going to idlePage
    property string returnToPageName: ""
    property int returnToShotId: 0

    // True while the first-run restore dialog is active (prevents SettingsHistoryDataTab from also handling restore signals)

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
        // Block close during firmware flash — quitting mid-flash can brick
        // the DE1. Show a confirmation dialog instead and let the user
        // decide. If they confirm, the dialog calls Qt.quit() directly
        // without the normal sleep sequence (which would try to talk to
        // the DE1 mid-bootloader anyway).
        if (MainController.firmwareUpdater && MainController.firmwareUpdater.isFlashing) {
            close.accepted = false
            firmwareFlashExitDialog.open()
            return
        }

        // Save window position on desktop (not size - keep default to match real device)
        if (Qt.platform.os !== "android" && Qt.platform.os !== "ios") {
            Settings.setValue("mainWindow/x", root.x)
            Settings.setValue("mainWindow/y", root.y)
        }

        // Mark as shutting down to suppress screensaver from DE1 sleep response
        root.shuttingDown = true

        // Send scale sleep first (it's faster/simpler)
        if (ScaleDevice && ScaleDevice.connected) {
            console.log("Sending scale to sleep on app close")
            ScaleDevice.sleep()
        }

        // Small delay before sending DE1 sleep to let scale command go through
        close.accepted = false
        scaleSleepTimer.start()
    }

    Dialog {
        id: firmwareFlashExitDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.errorColor
        }

        Tr { id: trFwExitTitle; key: "main.dialog.firmwareFlashExit.title"; fallback: "Firmware update in progress"; visible: false }
        Tr { id: trFwExitMessage; key: "main.dialog.firmwareFlashExit.message"; fallback: "Quitting now can leave the DE1 in a partially flashed state and may require manual bootloader recovery. Wait for the update to finish before closing the app."; visible: false }
        Tr { id: trFwExitKeepOpen; key: "main.dialog.firmwareFlashExit.keepOpen"; fallback: "Keep app open"; visible: false }
        Tr { id: trFwExitQuitAnyway; key: "main.dialog.firmwareFlashExit.quitAnyway"; fallback: "Quit anyway"; visible: false }

        onOpened: {
            // Park focus on the safe default so a stray screen-reader tap
            // can't trigger "Quit anyway" — which would brick mid-flash.
            fwExitKeepOpenButton.forceActiveFocus()
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trFwExitTitle.text + ". " + trFwExitMessage.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingLarge

            Text {
                text: trFwExitTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: trFwExitMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            Row {
                spacing: Theme.spacingMedium
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    id: fwExitKeepOpenButton
                    text: trFwExitKeepOpen.text
                    accessibleName: trFwExitKeepOpen.text
                    onClicked: firmwareFlashExitDialog.close()
                }

                AccessibleButton {
                    text: trFwExitQuitAnyway.text
                    accessibleName: trFwExitQuitAnyway.text
                    onClicked: {
                        firmwareFlashExitDialog.close()
                        // Skip the normal sleep-and-quit sequence — sleep is
                        // already blocked in C++ mid-flash, and we don't want
                        // any further BLE traffic either. Just exit.
                        root.shuttingDown = true
                        Qt.quit()
                    }
                }
            }
        }
    }

    // Firmware power-cycle prompt. Opens globally (not just on the Firmware
    // tab) when the auto-reboot grace window expires or the DE1 reconnects
    // still running the old firmware after a flash. Auto-dismisses once the
    // updater transitions out of AwaitingReboot (success or failure).
    Connections {
        target: MainController.firmwareUpdater
        function onNeedsManualRebootChanged() {
            if (MainController.firmwareUpdater.needsManualReboot) {
                firmwareRebootRequiredDialog.open()
            } else {
                firmwareRebootRequiredDialog.close()
            }
        }
        function onStateChanged() {
            // Belt-and-suspenders: if we leave AwaitingReboot for any reason,
            // close the dialog so it doesn't linger on Succeeded/Failed.
            if (!MainController.firmwareUpdater.needsManualReboot) {
                firmwareRebootRequiredDialog.close()
            }
        }
    }

    Dialog {
        id: firmwareRebootRequiredDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        Tr { id: trFwRebootTitle; key: "main.dialog.firmwareRebootRequired.title"; fallback: "Power-cycle the DE1"; visible: false }
        Tr { id: trFwRebootMessage; key: "main.dialog.firmwareRebootRequired.message"; fallback: "The firmware is flashed but the DE1 didn't restart on its own. Switch the DE1 off (back-panel switch or unplug), wait a few seconds, then switch it back on. Decenza will finish the update automatically once the DE1 reconnects."; visible: false }
        Tr { id: trFwRebootAck; key: "main.dialog.firmwareRebootRequired.ack"; fallback: "OK, I'll do it"; visible: false }

        onOpened: {
            fwRebootAckButton.forceActiveFocus()
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trFwRebootTitle.text + ". " + trFwRebootMessage.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingLarge

            Text {
                text: trFwRebootTitle.text
                font.family: Theme.subtitleFont.family
                font.pixelSize: Math.round(Theme.subtitleFont.pixelSize * 1.1)
                font.bold: true
                color: Theme.warningColor
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: trFwRebootMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font.family: Theme.bodyFont.family
                font.pixelSize: Math.round(Theme.bodyFont.pixelSize * 1.1)
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
            }

            Row {
                spacing: Theme.spacingMedium
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    id: fwRebootAckButton
                    text: trFwRebootAck.text
                    accessibleName: trFwRebootAck.text
                    onClicked: firmwareRebootRequiredDialog.close()
                }
            }
        }
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
    onAutoSleepMinutesChanged: {
        if (autoSleepMinutes > 0 && sleepCountdownNormal < 0) {
            sleepCountdownNormal = autoSleepMinutes
            sleepCountdownStayAwake = 0
            console.log("[AutoSleep] Setting changed: normal=" + sleepCountdownNormal)
        }
    }
    property int sleepCountdownNormal: -1      // Minutes remaining (-1 = not started)
    property int sleepCountdownStayAwake: -1   // Minutes remaining (-1 = already satisfied)

    // Active operation phases that should pause the sleep countdown
    property bool operationActive: {
        var phase = MachineState.phase
        // Treat an in-progress firmware flash as an active operation so the
        // auto-sleep countdown can't fire mid-upload and cut BLE.
        var fw = MainController.firmwareUpdater
        if (fw && fw.isFlashing) return true
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

            // Sleep when BOTH <= 0
            if (root.sleepCountdownNormal <= 0 && root.sleepCountdownStayAwake <= 0) {
                console.log("[AutoSleep] Both counters expired — triggering sleep")
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
                console.log("[AutoSleep] Reset by phase change: normal=" + root.sleepCountdownNormal)
            }
        }
    }

    // Detect when DE1 enters Steam state (even during heating/FinalHeating substate)
    // This clears steamDisabled BEFORE applySteamSettings runs, so GHC-initiated
    // steaming works correctly even if keepSteamHeaterOn is false
    Connections {
        target: DE1Device
        function onStateChanged() {
            // DE1::State::Steam = 5
            if (DE1Device.state === 5) {
                // Match de1app: if the currently selected steam preset is an "Off" pill,
                // leave the heater target at 0 (already pushed via sendMachineSettings /
                // turnOffSteamHeater). The DE1 still enters Steam state on GHC press, but
                // with TargetSteamTemp=0 no steam is produced — consistent with the user's
                // intent to keep the boiler off.
                var currentPitcher = Settings.getSteamPitcherPreset(Settings.selectedSteamPitcher)
                var currentPitcherDisabled = currentPitcher && currentPitcher.disabled === true
                if (currentPitcherDisabled) {
                    console.log("DE1 entered Steam state but Off preset selected — heater stays off")
                    MainController.turnOffSteamHeater()
                } else {
                    console.log("DE1 entered Steam state - starting heater, navigating to SteamPage")
                    MainController.startSteamHeating("de1-state-steam")  // This clears steamDisabled flag
                }
                // Navigate to SteamPage immediately so user sees heating progress
                var currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""
                if (currentPage !== "steamPage" && !pageStack.busy) {
                    saveReturnToPage(currentPage)
                    pageStack.replace(null, steamPage)
                }
            }
        }
        function onSubStateChanged() {
            // DE1::SubState::Puffing = 20
            // When entering Puffing, start the auto-flush countdown if enabled
            if (DE1Device.state === 5 && DE1Device.subState === 20) {
                console.log("DE1 entered Puffing substate")
                if (Settings.steamAutoFlushSeconds > 0) {
                    console.log("Starting auto-flush countdown:", Settings.steamAutoFlushSeconds, "seconds")
                    root.steamAutoFlushCountdown = Settings.steamAutoFlushSeconds
                    steamAutoFlushTimer.restart()
                }
            }
        }
    }

    // Auto-flush countdown value (for display on SteamPage)
    property real steamAutoFlushCountdown: 0

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
        console.log("[AutoSleep] triggerAutoSleep called — DE1 connected=" +
                   (DE1Device ? DE1Device.connected : "null") +
                   ", scale connected=" + (ScaleDevice ? ScaleDevice.connected : "null"))
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

    // Suppress screensaver until DE1 has been awake at least once since connecting
    // (on connect, MachineState sees default Sleep state before real state arrives)
    property bool startupGracePeriod: true

    // Suppress screensaver during shutdown — we send DE1 to sleep which triggers Sleep phase
    property bool shuttingDown: false


    // Defer scale dialogs until machine reaches Ready (event-driven, not timer-based)
    property bool scaleDialogDeferred: false

    // Shared translation strings for dialog buttons
    Tr { id: trCommonOk; key: "common.button.ok"; fallback: "OK"; visible: false }
    Tr { id: trCommonDismissDialog; key: "common.accessibility.dismissDialog"; fallback: "Dismiss dialog"; visible: false }

    // Popup queue: popups that arrived during screensaver, shown after wake
    property var pendingPopups: []

    function queuePopup(popupId, params) {
        // Deduplicate by popupId
        for (var i = 0; i < pendingPopups.length; i++) {
            if (pendingPopups[i].id === popupId) return
        }
        pendingPopups = pendingPopups.concat([{id: popupId, params: params || {}}])
    }

    function showNextPendingPopup() {
        if (screensaverActive) return  // Don't show popups during screensaver
        if (pendingPopups.length === 0) return

        // While scale dialogs are deferred, skip scale popups and show others
        var queue = pendingPopups.slice()
        var next
        if (root.scaleDialogDeferred) {
            var idx = -1
            for (var i = 0; i < queue.length; i++) {
                if (queue[i].id !== "flowScale" && queue[i].id !== "scaleDisconnected") {
                    idx = i
                    break
                }
            }
            if (idx < 0) return  // Only scale popups remain — wait for deferral to clear
            next = queue.splice(idx, 1)[0]
            pendingPopups = queue
        } else {
            next = queue.shift()
            pendingPopups = queue
        }

        switch (next.id) {
            case "flowScale": flowScaleDialog.open(); break
            case "scaleDisconnected": scaleDisconnectedDialog.open(); break

            case "update": updateDialog.open(); break
            case "chargingMismatch": chargingMismatchDialog.open(); break
            case "bleError":
                bleErrorDialog.errorMessage = next.params.errorMessage || ""
                bleErrorDialog.isLocationError = next.params.isLocationError || false
                bleErrorDialog.isBluetoothError = next.params.isBluetoothError || false
                bleErrorDialog.open()
                break
            case "refill":
                // Only show if machine is still in Refill phase
                if (MachineState.phase === MachineStateType.Phase.Refill)
                    refillDialog.open()
                else
                    showNextPendingPopup()  // Skip stale refill, show next
                break
        }
    }

    function removeQueuedScalePopups() {
        pendingPopups = pendingPopups.filter(function(p) {
            return p.id !== "flowScale" && p.id !== "scaleDisconnected"
        })
    }

    // Shows the Linux CAP_NET_ADMIN warning when the binary is missing the
    // capability. Suppressed in simulator mode (BLE disabled) to avoid a
    // spurious startup modal for devs running the simulator on Linux.
    function maybeShowLinuxBleCapabilityDialog() {
        if (!BLEManager.disabled && BLEManager.linuxBleCapabilityMissing) {
            Qt.callLater(function() { linuxBleCapabilityDialog.open() })
        }
    }

    // No timer needed — page transitions are instant (empty Transition{}),
    // so Qt.callLater suffices to let the event loop finish the replace().

    // Track if we were just steaming (for auto-flush timer)
    property bool wasSteaming: false

    // Auto-flush steam wand timer - counts down smoothly during Puffing state
    Timer {
        id: steamAutoFlushTimer
        interval: 100  // 100ms for smooth countdown
        running: false
        repeat: true
        onTriggered: {
            root.steamAutoFlushCountdown -= 0.1
            if (root.steamAutoFlushCountdown <= 0) {
                root.steamAutoFlushCountdown = 0
                steamAutoFlushTimer.stop()
                console.log("Steam auto-flush countdown complete, requesting Idle state")
                // Turn off steam heater if keepSteamHeaterOn is false
                if (!Settings.keepSteamHeaterOn) {
                    console.log("Auto-flush complete, turning off steam heater (keepSteamHeaterOn=false)")
                    MainController.sendSteamTemperature(0)  // This sets steamDisabled=true
                }
                if (DE1Device && DE1Device.connected) {
                    DE1Device.requestIdle()  // Triggers steam purge
                }
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
        // Handle both boolean and string values from QSettings
        Theme.configurePageScaleEnabled = (configVal === true || configVal === "true")

        updateScale()

        // Keep screen on while app is running (Android only)
        // Prevent system screen timeout — the app has its own screensaver.
        // On Android: FLAG_KEEP_SCREEN_ON. On iOS: idleTimerDisabled = true.
        // Without this on iOS, the system auto-locks during screensaver,
        // suspending the app and breaking BLE comms and smart charging.
        ScreensaverManager.setKeepScreenOn(true)

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

            // If a crash dialog is about to open, defer the capability
            // warning until it's dismissed (see crashReportDialog handlers)
            // so the two modals don't stack on the same frame.
            if (!(PreviousCrashLog && PreviousCrashLog.length > 0)) {
                maybeShowLinuxBleCapabilityDialog()
            }
        }

        // Initialize sleep countdowns (fresh app start, not auto-woken)
        if (root.autoSleepMinutes > 0) {
            root.sleepCountdownNormal = root.autoSleepMinutes
            root.sleepCountdownStayAwake = 0  // Already satisfied on fresh start
        } else {
        }

        // Auto-match current bean data to a preset so the bean button
        // doesn't appear yellow when the data already matches a saved preset
        if (Settings.selectedBeanPreset === -1 && Settings.dyeBeanBrand.length > 0) {
            var matchIndex = Settings.findBeanPresetByContent(Settings.dyeBeanBrand, Settings.dyeBeanType)
            if (matchIndex >= 0) {
                Settings.selectedBeanPreset = matchIndex
            }
        }

        // Mark app as initialized
        root.appInitialized = true

        // startupGracePeriod is cleared by the phaseChanged handler when the
        // machine first reaches a non-Sleep state (no timer needed)
    }

    function updateScale() {
        // Scale based on window size vs reference (960x600 tablet dp)
        var scaleX = width / Theme.refWidth
        var scaleY = height / Theme.refHeight
        var autoScale = Math.min(scaleX, scaleY)

        // Apply global multiplier and per-page multiplier
        Theme.scale = autoScale * Theme.scaleMultiplier * Theme.pageScaleMultiplier

        // Update window dimensions for responsive sizing (dialogs, popups)
        Theme.windowWidth = width
        Theme.windowHeight = height
    }

    // Global tap handler for accessibility - announces any Text tapped
    // Only announces text that is NOT inside an interactive element (buttons have their own announcements)
    MouseArea {
        id: accessibilityTapOverlay
        anchors.fill: parent
        z: 10000  // Above everything
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        propagateComposedEvents: true
        Accessible.ignored: true

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

        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("main.doneediting", "Done Editing")
        Accessible.focusable: true
        Accessible.onPressAction: doneEditingArea.clicked(null)

        RowLayout {
            id: doneEditingRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/tick.svg"
                sourceSize.width: Theme.scaled(14)
                sourceSize.height: Theme.scaled(14)
                Accessible.ignored: true
            }

            Tr {
                key: "main.doneediting"
                fallback: "Done Editing"
                font: Theme.bodyFont
                color: Theme.primaryContrastColor
                Accessible.ignored: true
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
                    color: Theme.primaryContrastColor
                    Accessible.ignored: true
                }
            }
        }

        MouseArea {
            id: doneEditingArea
            anchors.fill: parent
            onClicked: TranslationManager.editModeEnabled = false
        }
    }

    // QML compilation is cached at build time by qmlcachegen (enabled by
    // default in qt_add_qml_module since Qt 6.5), so the previous "preload
    // every page in a hidden Loader" pattern no longer warmed anything
    // useful — it just instantiated the full QML object tree, ran every
    // page's Component.onCompleted (firing real BLE/scale side effects
    // before the user opened the page), then destroyed it. Removed.


    // Navigation guard to prevent double-taps during page transitions
    property bool navigationInProgress: false
    property bool pendingDisconnectNavigation: false
    // Clears the guard when animated transitions finish (currently all
    // transitions are empty Transition{}, so this is a no-op today but
    // will activate automatically if animations are added later).
    Connections {
        target: pageStack
        function onBusyChanged() {
            if (!pageStack.busy) {
                navigationInProgress = false
                // Retry deferred disconnect navigation (#575)
                if (pendingDisconnectNavigation) {
                    pendingDisconnectNavigation = false
                    console.log("Retrying deferred disconnect navigation to idle")
                    pageStack.replace(null, idlePage)
                    root.returnToPageName = ""
                    root.returnToShotId = 0
                }
            }
        }
    }

    function startNavigation() {
        if (navigationInProgress || pageStack.busy) return false
        navigationInProgress = true
        // With empty transitions, busy never becomes true and the
        // Connections handler above never fires. Use Qt.callLater to
        // clear the flag after the caller's push/pop/replace completes.
        Qt.callLater(function() {
            if (!pageStack.busy) {
                navigationInProgress = false
            }
        })
        return true
    }

    // Page stack for navigation
    StackView {
        id: pageStack
        anchors.fill: parent
        focus: true
        initialItem: idlePage

        // Android system back button / Escape key → go back.
        // Pages that need custom back handling (e.g. BeanInfoPage's unsaved-changes
        // dialog) intercept Key_Back first and set event.accepted = true.
        Keys.onReleased: function(event) {
            if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
                if (pageStack.depth > 1) {
                    event.accepted = true
                    goBack()
                }
            }
        }

        // CRT shader: render to FBO and process through GPU shader
        layer.enabled: Settings.activeShader === "crt"
        layer.effect: CrtShaderEffect {}

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
            id: pressureEditorPage
            PressureEditorPage {}
        }

        Component {
            id: flowEditorPage
            FlowEditorPage {}
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

        // Status bar (inside pageStack so it's included in the CRT shader FBO)
        StatusBar {
            id: statusBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.statusBarHeight
            z: 600
            visible: !root.screensaverActive
        }
    }

    // Update per-page scale when navigating between pages
    Connections {
        target: pageStack
        function onCurrentItemChanged() {
            updateCurrentPageScale()
            announceCurrentPage()
            pageColorTimer.restart()  // Detect colors after page settles
        }
    }

    // Delay color detection slightly so page content is fully loaded
    Timer {
        id: pageColorTimer
        interval: 300
        onTriggered: updatePageColors()
    }

    // Announce page name for accessibility when page changes
    function announceCurrentPage() {
        if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
        var pageName = pageStack.currentItem ? (pageStack.currentItem.objectName || "") : ""
        if (!pageName) return

        // Map objectNames to human-readable page names
        // Use "screen" or "settings" suffix to distinguish from button names
        var pageNames = {
            "idlePage": TranslationManager.translate("main.pageHomeScreen", "Home screen"),
            "espressoPage": TranslationManager.translate("main.pageEspressoScreen", "Espresso screen"),
            "steamPage": TranslationManager.translate("main.pageSteamSettings", "Steam settings"),
            "hotWaterPage": TranslationManager.translate("main.pageHotWaterSettings", "Hot water settings"),
            "flushPage": TranslationManager.translate("main.pageFlushSettings", "Flush settings"),
            "settingsPage": TranslationManager.translate("main.pageSettings", "Settings"),
            "profileSelectorPage": TranslationManager.translate("main.pageProfileSelector", "Profile selector"),
            "profileEditorPage": TranslationManager.translate("main.pageProfileEditor", "Profile editor"),
            "recipeEditorPage": TranslationManager.translate("main.pageRecipeEditor", "Recipe editor"),
            "pressureEditorPage": TranslationManager.translate("main.pagePressureEditor", "Pressure profile editor"),
            "flowEditorPage": TranslationManager.translate("main.pageFlowEditor", "Flow profile editor"),
            "shotHistoryPage": TranslationManager.translate("main.pageShotHistory", "Shot history"),
            "descalingPage": TranslationManager.translate("main.pageDescalingScreen", "Descaling screen"),
            "visualizerBrowserPage": TranslationManager.translate("main.pageVisualizerBrowser", "Visualizer browser"),
            "profileImportPage": TranslationManager.translate("main.pageImportProfiles", "Import profiles"),
            "postShotReviewPage": TranslationManager.translate("main.pageShotReview", "Shot review"),
            "beanInfoPage": TranslationManager.translate("main.pageBeanInfo", "Bean info"),
            "dialingAssistantPage": TranslationManager.translate("main.pageAiAssistant", "AI assistant"),
            "shotDetailPage": TranslationManager.translate("main.pageShotDetail", "Shot detail"),
            "shotComparisonPage": TranslationManager.translate("main.pageShotComparison", "Shot comparison")
        }
        var displayName = pageNames[pageName] || pageName
        AccessibilityManager.announce(displayName)
    }

    function updateCurrentPageScale() {
        var pageName = pageStack.currentItem ? (pageStack.currentItem.objectName || "") : ""
        Theme.currentPageObjectName = pageName
        if (pageName) {
            Theme.pageScaleMultiplier = parseFloat(Settings.value("pageScale/" + pageName, 1.0)) || 1.0
        } else {
            Theme.pageScaleMultiplier = 1.0
        }
    }

    // Detect which Theme colors are used on the current page
    // Walks the QML item tree and matches resolved color values against Theme properties
    function updatePageColors() {
        var page = pageStack.currentItem
        if (!page) {
            Settings.currentPageColors = []
            return
        }

        // Collect all unique color hex values from visible items
        var foundColors = {}
        function walkItem(item) {
            if (!item || !item.visible) return
            // Check 'color' property (Rectangle, Text, etc.)
            if (item.color !== undefined) {
                var c = ("" + item.color).substring(0, 7).toLowerCase()
                if (c.charAt(0) === "#") foundColors[c] = true
            }
            for (var i = 0; i < item.children.length; i++) {
                walkItem(item.children[i])
            }
        }
        walkItem(page)

        // Match against all Theme color properties
        var themeColorNames = [
            "backgroundColor", "surfaceColor", "primaryColor", "secondaryColor",
            "textColor", "textSecondaryColor", "accentColor", "successColor",
            "warningColor", "highlightColor", "errorColor", "borderColor",
            "pressureColor", "pressureGoalColor", "flowColor", "flowGoalColor",
            "temperatureColor", "temperatureGoalColor", "weightColor", "weightFlowColor",
            "dyeDoseColor", "dyeOutputColor", "dyeTdsColor", "dyeEyColor",
            "buttonDisabled", "stopMarkerColor", "frameMarkerColor",
            "modifiedIndicatorColor", "simulationIndicatorColor",
            "warningButtonColor", "successButtonColor",
            "rowAlternateColor", "rowAlternateLightColor",
            "sourceBadgeBlueColor", "sourceBadgeGreenColor", "sourceBadgeOrangeColor"
        ]

        var matched = []
        for (var j = 0; j < themeColorNames.length; j++) {
            var name = themeColorNames[j]
            var themeVal = ("" + Theme[name]).substring(0, 7).toLowerCase()
            if (foundColors[themeVal]) {
                matched.push(name)
            }
        }
        Settings.currentPageColors = matched
    }

    // Initialize page scale after pageStack is ready
    Timer {
        id: initPageScaleTimer
        interval: 100
        onTriggered: {
            updateCurrentPageScale()
        }
        Component.onCompleted: start()
    }

    // Global error dialog for BLE issues
    Dialog {
        id: bleErrorDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        onClosed: root.showNextPendingPopup()

        property string errorMessage: ""
        property bool isLocationError: false
        property bool isBluetoothError: false

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
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
                Tr { id: trOpenBluetoothSettings; key: "main.button.openBluetoothSettings"; fallback: "Open Bluetooth Settings"; visible: false }
                text: trOpenBluetoothSettings.text
                accessibleName: trOpenBluetoothSettings.text
                visible: bleErrorDialog.isBluetoothError
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    BLEManager.openBluetoothSettings()
                    bleErrorDialog.close()
                }
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: bleErrorDialog.close()
            }
        }
    }

    Connections {
        target: BLEManager
        function onErrorOccurred(error) {
            var isLocation = error.indexOf("Location") !== -1
            var isBluetooth = error.indexOf("Bluetooth") !== -1 && error.indexOf("permission") !== -1
            var msg = isLocation
                ? "Please enable Location services.\nAndroid requires Location for Bluetooth scanning."
                : error
            if (screensaverActive) {
                queuePopup("bleError", {errorMessage: msg, isLocationError: isLocation, isBluetoothError: isBluetooth})
                return
            }
            bleErrorDialog.isLocationError = isLocation
            bleErrorDialog.isBluetoothError = isBluetooth
            bleErrorDialog.errorMessage = msg
            bleErrorDialog.open()
        }
        function onFlowScaleFallback() {
            // Only show "No Scale Found" if user has a saved scale.
            // Users without a saved scale expect FlowScale — no need to nag them.
            if (Settings.primaryScaleAddress === "") return
            if (!Settings.showScaleDialogs) return
            // Don't nag if a USB scale is connected — it satisfies the requirement (not available on iOS)
            if (Qt.platform.os !== "ios" && UsbScaleManager.scaleConnected) return
            if (screensaverActive) { queuePopup("flowScale"); return }
            if (root.scaleDialogDeferred) { queuePopup("flowScale"); return }
            flowScaleDialog.open()
        }
        function onScaleDisconnected() {
            if (!Settings.showScaleDialogs) return
            if (screensaverActive) { queuePopup("scaleDisconnected"); return }
            if (root.scaleDialogDeferred) { queuePopup("scaleDisconnected"); return }
            scaleDisconnectedDialog.open()
        }
    }

    // FlowScale fallback dialog (no scale found at startup)
    Dialog {
        id: flowScaleDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
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

            Text {
                text: trNoScaleFoundTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.noScaleFound.message"
                fallback: "No Bluetooth scale was detected.\n\nUsing estimated weight from DE1 flow measurement instead.\n\nYou can search for your scale in Settings → Connections."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: flowScaleDialog.close()
            }
        }
    }

    // Scale disconnected dialog
    Dialog {
        id: scaleDisconnectedDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
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
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: scaleDisconnectedDialog.close()
            }
        }
    }

    // Shot aborted: scale not connected
    Connections {
        target: MainController
        function onShotAbortedNoScale() {
            noScaleAbortDialog.open()
        }
    }

    Dialog {
        id: noScaleAbortDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        closePolicy: Dialog.CloseOnEscape
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.errorColor
        }

        Tr { id: trNoScaleTitle; key: "main.dialog.noScale.title"; fallback: "Shot Stopped"; visible: false }
        Tr { id: trNoScaleAnnounce; key: "main.dialog.noScale.announce"; fallback: "Shot stopped. Scale is not connected."; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trNoScaleAnnounce.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                text: trNoScaleTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.noScale.message"
                fallback: "Your saved scale is not connected.\n\nPlease turn on your scale and wait for it to connect before starting a shot.\n\nTo use the app without a scale, go to Settings \u2192 Bluetooth and tap \u0022Forget Scale\u0022."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: noScaleAbortDialog.close()
            }
        }
    }

    // Charging mismatch warning dialog
    // Shown when smart charging commands the DE1 USB port ON but Android still reports
    // DISCHARGING — the port is not delivering power (DE1 asleep, BLE command failed, cable issue).
    Dialog {
        id: chargingMismatchDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        closePolicy: Dialog.CloseOnEscape
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        Tr { id: trChargingMismatchTitle; key: "main.dialog.chargingMismatch.title"; fallback: "Charging Not Detected"; visible: false }
        Tr { id: trChargingMismatchAnnounce; key: "main.dialog.chargingMismatch.announce"; fallback: "Warning: Charging not detected"; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trChargingMismatchAnnounce.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                text: trChargingMismatchTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Tr {
                key: "main.dialog.chargingMismatch.message"
                fallback: "Smart charging is set to ON but the tablet is not receiving power from the DE1.\n\nPossible causes:\n\u2022 DE1 went to sleep and cut its USB port\n\u2022 BLE command failed \u2014 retrying automatically\n\u2022 USB cable is disconnected"
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: chargingMismatchDialog.close()
            }
        }
    }

    Connections {
        target: BatteryManager

        function onChargingMismatchDetected() {
            if (screensaverActive) {
                queuePopup("chargingMismatch")
                return
            }
            chargingMismatchDialog.open()
        }

        function onChargingMismatchResolved() {
            chargingMismatchDialog.close()
            // Remove any queued instance so it doesn't appear after screensaver wake
            // when the condition has already cleared.
            pendingPopups = pendingPopups.filter(function(p) { return p.id !== "chargingMismatch" })
        }
    }

    // Water tank refill dialog
    Dialog {
        id: refillDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
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
                Tr { id: trDismissRefill; key: "main.accessibility.dismissRefillWarning"; fallback: "Dismiss refill warning"; visible: false }
                text: trCommonOk.text
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
                if (screensaverActive) { queuePopup("refill"); return }
                refillDialog.open()
            } else if (refillDialog.opened) {
                refillDialog.close()
            }
        }
    }


    // Update notification dialog
    Dialog {
        id: updateDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

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
                    accessibleName: TranslationManager.translate("main.dismissUpdate", "Dismiss update and remind me later")
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
                    accessibleName: TranslationManager.translate("main.downloadAndInstallUpdate", "Download and install the update now")
                    onClicked: {
                        if (MainController.updateChecker) {
                            MainController.updateChecker.downloadAndInstall()
                        }
                        updateDialog.close()
                        goToSettings(11)  // Navigate to About tab (has update controls)

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
            if (screensaverActive) { queuePopup("update"); return }
            updateDialog.open()
        }
    }

    // Completion overlay
    property string completionMessage: ""
    property string completionType: ""  // "steam", "hotwater", "flush"
    property bool completionPending: false

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

                ColoredIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/tick.svg"
                    iconWidth: 50
                    iconHeight: 50
                    iconColor: Theme.primaryColor
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
                        return Math.max(0, MachineState.scaleWeight).toFixed(0) + "g"
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
        interval: 1500  // 1.5s: short enough not to feel slow (reduced from 3s per user feedback)
        onTriggered: {
            completionPending = false
            completionOverlay.opacity = 0

            // Return to saved page if set, otherwise go to idlePage
            if (root.returnToPageName === "postShotReviewPage") {
                var shotId = root.returnToShotId > 0 ? root.returnToShotId : MainController.lastSavedShotId
                pageStack.replace(null, idlePage)
                pageStack.push(postShotReviewPage, { editShotId: shotId })
            } else {
                if (pageStack.currentItem && pageStack.currentItem.objectName !== "idlePage") {
                    pageStack.replace(null, idlePage)
                }
            }

            // Clear return-to tracking
            root.returnToPageName = ""
            root.returnToShotId = 0
        }
    }

    function showCompletion(message, type) {
        completionMessage = message
        completionType = type
        completionPending = true
        completionOverlay.opacity = 1  // Instant (Behavior disabled when opacity is 0)
        completionTimer.restart()
    }

    // Save current page info before navigating to operation pages (steam/flush/water)
    // so we can return there after the operation completes
    function saveReturnToPage(pageName) {
        // Only save if we're on a "return-worthy" page
        // If we're on an operation page (steam/flush/water), preserve any existing return tracking
        // This handles chained operations like: postShotReview → steam → flush → postShotReview
        if (pageName === "postShotReviewPage") {
            root.returnToPageName = pageName
            // Get the editShotId from the current page, fallback to lastSavedShotId
            var currentItem = pageStack.currentItem
            var hasEditShotId = currentItem && typeof currentItem.editShotId !== "undefined"
            if (hasEditShotId && currentItem.editShotId > 0) {
                root.returnToShotId = currentItem.editShotId
            } else {
                root.returnToShotId = MainController.lastSavedShotId
            }
        } else if (pageName === "steamPage" || pageName === "hotWaterPage" || pageName === "flushPage") {
            // On an operation page - preserve existing return tracking (if any)
        } else {
            // For other pages (like idlePage), clear the return tracking
            root.returnToPageName = ""
            root.returnToShotId = 0
        }
    }

    // CRT / Pip-Boy shader overlay (renders above all content including status bar)
    CrtOverlay {
        id: crtOverlay
        anchors.fill: parent
        z: 950  // Above statusBar (600), below touch capture (1000)
    }

    // SAW bypassed warning (untared cup detected during extraction)
    property bool sawBypassedVisible: false

    Rectangle {
        id: sawBypassedOverlay
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(80)
        anchors.horizontalCenter: parent.horizontalCenter
        z: 500

        width: sawBypassedText.width + Theme.spacingLarge * 2
        height: Theme.scaled(44)
        radius: Theme.scaled(22)
        color: Theme.errorColor

        visible: sawBypassedVisible || sawBypassedFadeOut.running
        opacity: sawBypassedVisible ? 1 : 0
        scale: 1.0

        SequentialAnimation {
            id: sawBypassedPopIn
            NumberAnimation {
                target: sawBypassedOverlay; property: "scale"
                from: 1.0; to: 1.10; duration: 150
                easing.type: Easing.OutBack; easing.overshoot: 1.5
            }
            NumberAnimation {
                target: sawBypassedOverlay; property: "scale"
                from: 1.10; to: 1.0; duration: 350
                easing.type: Easing.OutBack; easing.overshoot: 1.5
            }
        }

        Behavior on opacity {
            enabled: sawBypassedVisible
            NumberAnimation { id: sawBypassedFadeOut; duration: 2000 }
        }

        Text {
            id: sawBypassedText
            anchors.centerIn: parent
            text: TranslationManager.translate("espresso.sawBypassed", "Scale not tared — auto-stop disabled")
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }

        Accessible.role: Accessible.AlertMessage
        Accessible.name: sawBypassedText.text
        Accessible.focusable: true
    }

    Timer {
        id: sawBypassedTimer
        interval: 5000
        onTriggered: sawBypassedVisible = false
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

        Accessible.role: Accessible.AlertMessage
        Accessible.name: stopReasonText.text
        Accessible.focusable: true

        Text {
            id: stopReasonText
            anchors.centerIn: parent
            text: getStopReasonText()
            color: "black"
            font: Theme.bodyFont
            Accessible.ignored: true
        }
    }

    Timer {
        id: stopOverlayTimer
        interval: 3000
        onTriggered: {
            stopOverlayVisible = false
            if (root.pendingMetadataNavigation) {
                root.pendingMetadataNavigation = false
                // Settings.value() may return string on Windows (REG_SZ), coerce to Number
                var timeout = Number(Settings.value("postShotReviewTimeout", 31))
                if (timeout === 0) {
                    console.log("Post-shot review timeout is Instant, skipping review page")
                    goToIdle()
                    return
                }
                if (root.pendingShotId > 0) {
                    goToShotMetadata(root.pendingShotId)
                } else {
                    console.warn("Post-shot navigation: no valid pendingShotId, going to idle")
                    goToIdle()
                }
            } else {
                // pendingMetadataNavigation is set by onShotEndedShowMetadata only when
                // the overlay was still visible at signal time. False here means either
                // Edit After Shot is OFF, or the shot save arrived after the overlay
                // expired (SAW settling outlasted 3s) and was handled directly.
                goToIdle()
            }
        }
    }

    Connections {
        target: MachineState
        function onTargetWeightReached() {
            root.stopReason = "weight"
        }
        function onSawBypassed() {
            root.sawBypassedVisible = true
            sawBypassedPopIn.start()
            sawBypassedTimer.start()
            if (typeof AccessibilityManager !== "undefined") {
                AccessibilityManager.announce(sawBypassedText.text, true)
            }
        }
        function onShotStarted() {
            // Track if this is an espresso operation (check current phase)
            var phase = MachineState.phase
            root.wasEspressoOperation = (phase === MachineStateType.Phase.EspressoPreheating ||
                                         phase === MachineStateType.Phase.Preinfusion ||
                                         phase === MachineStateType.Phase.Pouring)
            root.stopReason = ""
            root.stopOverlayVisible = false
            root.sawBypassedVisible = false
            sawBypassedTimer.stop()
            root.pendingMetadataNavigation = false
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
            maybeShowLinuxBleCapabilityDialog()
        }
        onReported: {
            // Clear the crash log file after successful report
            MainController.clearCrashLog()
            maybeShowLinuxBleCapabilityDialog()
        }
    }

    // DE1 communication-failure dialog — shown when ProfileManager has
    // exhausted its retry budget for BLE profile uploads. Opens/closes
    // purely from ProfileManager.de1CommunicationFailure so there's no
    // imperative showDialog()/hideDialog() coupling to maintain.
    De1CommunicationErrorDialog {
        id: de1CommunicationErrorDialog
    }
    Connections {
        target: ProfileManager
        function onDe1CommunicationFailureChanged() {
            if (ProfileManager.de1CommunicationFailure) {
                if (!de1CommunicationErrorDialog.visible)
                    de1CommunicationErrorDialog.open()
            } else {
                if (de1CommunicationErrorDialog.visible)
                    de1CommunicationErrorDialog.close()
            }
        }
    }

    // MCP confirmation dialog — shown when an AI assistant triggers a machine start operation
    McpConfirmDialog {
        id: mcpConfirmDialog
        onConfirmed: function(sessionId) {
            McpServer.confirmationResolved(sessionId, true)
        }
        onDenied: function(sessionId) {
            McpServer.confirmationResolved(sessionId, false)
        }
    }

    Connections {
        target: McpServer
        function onConfirmationRequested(toolName, toolDescription, sessionId) {
            if (mcpConfirmDialog.visible) {
                // Suppress denied signal for the superseded request (C++ already handled it)
                mcpConfirmDialog.userResponded = true
                mcpConfirmDialog.close()
            }
            mcpConfirmDialog.toolDescription = toolDescription
            mcpConfirmDialog.sessionId = sessionId
            mcpConfirmDialog.open()
        }
    }

    // Linux BLE capability warning — shown on Linux when the binary lacks
    // CAP_NET_ADMIN. Without it, BlueZ can't determine whether a BLE address
    // is random or public and rejects connections to the DE1 (which uses a
    // random static address) with UnknownRemoteDeviceError. The capability
    // is granted via `sudo setcap` and is frequently cleared by OS updates.
    Dialog {
        id: linuxBleCapabilityDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.errorColor
        }

        Tr { id: trLinuxBleCapTitle; key: "main.dialog.linuxBleCapability.title"; fallback: "Bluetooth permission missing"; visible: false }
        Tr { id: trLinuxBleCapMessage; key: "main.dialog.linuxBleCapability.message"; fallback: "Decenza needs the CAP_NET_ADMIN Linux capability to connect to the DE1 over Bluetooth. Without it, the DE1 is discovered by scans but connections fail with \"Remote device not found\".\n\nThis often happens after a system update clears file capabilities.\n\nRun this command in a terminal, then restart Decenza:"; visible: false }
        Tr { id: trLinuxBleCapCopy; key: "main.dialog.linuxBleCapability.copy"; fallback: "Copy command"; visible: false }
        Tr { id: trLinuxBleCapCommandField; key: "main.dialog.linuxBleCapability.commandField"; fallback: "Setcap command"; visible: false }

        contentItem: Column {
            spacing: Theme.spacingLarge

            Text {
                text: trLinuxBleCapTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: trLinuxBleCapMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            Rectangle {
                width: parent.width
                color: Theme.backgroundColor
                radius: Theme.cardRadius / 2
                border.width: 1
                border.color: Theme.primaryContrastColor
                height: cmdText.implicitHeight + 2 * Theme.spacingMedium

                TextEdit {
                    id: cmdText
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    text: BLEManager.linuxBleSetcapCommand
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    font.pixelSize: Theme.bodyFont.pixelSize
                    color: Theme.textColor

                    Accessible.role: Accessible.EditableText
                    Accessible.name: trLinuxBleCapCommandField.text
                    Accessible.readOnly: true
                    Accessible.focusable: true
                }
            }

            Row {
                spacing: Theme.spacingMedium
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    text: trLinuxBleCapCopy.text
                    accessibleName: trLinuxBleCapCopy.text
                    onClicked: {
                        cmdText.selectAll()
                        cmdText.copy()
                        cmdText.deselect()
                    }
                }

                AccessibleButton {
                    text: trCommonOk.text
                    accessibleName: trCommonDismissDialog.text
                    onClicked: linuxBleCapabilityDialog.close()
                }
            }
        }
    }

    // Linux BLE BlueZ-cache recovery hint — shown when UnknownRemoteDeviceError
    // fires on a DE1 or scale connection attempt while CAP_NET_ADMIN is
    // effective. That combination almost always means the host-side BlueZ
    // state is stale (cached address type, expired pairing) after an OS
    // upgrade. Gated to Linux + caps-OK; the setcap dialog above covers the
    // caps-missing case.
    Connections {
        target: BLEManager
        function onLinuxBlueZCacheHintNeeded() {
            if (BLEManager.disabled) return
            if (BLEManager.linuxBleCapabilityMissing) return  // setcap dialog takes precedence
            Qt.callLater(function() { linuxBleBluezCacheDialog.open() })
        }
    }

    Dialog {
        id: linuxBleBluezCacheDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.errorColor
        }

        Tr { id: trBluezCacheTitle; key: "main.dialog.linuxBleBluezCache.title"; fallback: "Can't connect to DE1 — try clearing the Bluetooth cache"; visible: false }
        Tr { id: trBluezCacheMessage; key: "main.dialog.linuxBleBluezCache.message"; fallback: "The DE1 was discovered but the Bluetooth stack rejected the connection (\"Remote device not found\"). This usually means BlueZ cached stale pairing or address information after an OS update.\n\nIn a terminal, remove the cached entry and restart the Bluetooth service, then power-cycle the DE1 and try again:"; visible: false }
        Tr { id: trBluezCacheCopy; key: "main.dialog.linuxBleBluezCache.copy"; fallback: "Copy commands"; visible: false }
        Tr { id: trBluezCacheCommandField; key: "main.dialog.linuxBleBluezCache.commandField"; fallback: "BlueZ recovery commands"; visible: false }

        contentItem: Column {
            spacing: Theme.spacingLarge

            Text {
                text: trBluezCacheTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: trBluezCacheMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            Rectangle {
                width: parent.width
                color: Theme.backgroundColor
                radius: Theme.cardRadius / 2
                border.width: 1
                border.color: Theme.primaryContrastColor
                height: bluezCmdText.implicitHeight + 2 * Theme.spacingMedium

                TextEdit {
                    id: bluezCmdText
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    text: "MAC=$(bluetoothctl devices | awk '/DE1/ {print $2; exit}')\nbluetoothctl remove \"$MAC\"\nsudo systemctl restart bluetooth"
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    font.pixelSize: Theme.bodyFont.pixelSize
                    color: Theme.textColor

                    Accessible.role: Accessible.EditableText
                    Accessible.name: trBluezCacheCommandField.text
                    Accessible.readOnly: true
                    Accessible.focusable: true
                }
            }

            Row {
                spacing: Theme.spacingMedium
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    text: trBluezCacheCopy.text
                    accessibleName: trBluezCacheCopy.text
                    onClicked: {
                        bluezCmdText.selectAll()
                        bluezCmdText.copy()
                        bluezCmdText.deselect()
                    }
                }

                AccessibleButton {
                    text: trCommonOk.text
                    accessibleName: trCommonDismissDialog.text
                    onClicked: linuxBleBluezCacheDialog.close()
                }
            }
        }
    }

    // First-run welcome dialog
    Dialog {
        id: firstRunDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        Tr { id: trWelcomeTitle; key: "main.dialog.welcome.title"; fallback: "Welcome to Decenza"; visible: false }

        contentItem: Column {
            spacing: Theme.spacingLarge

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
                    maybeShowLinuxBleCapabilityDialog()
                }
            }
        }
    }

    // Storage setup dialog (Android 11+ - request MANAGE_EXTERNAL_STORAGE permission)
    Dialog {
        id: storageSetupDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.NoAutoClose
        padding: Theme.dialogPadding

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        Tr { id: trStorageTitle; key: "main.dialog.storage.title"; fallback: "Save profiles to Documents?"; visible: false }

        contentItem: Column {
            spacing: Theme.spacingLarge

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
                checkFirstRunRestore()
            }
        }
    }

    // Check if storage setup is needed (Android only)
    function checkStorageSetup() {
        if (ProfileStorage.needsSetup) {
            storageSetupDialog.open()
        } else {
            checkFirstRunRestore()
        }
    }

    // Check if database is empty but backups exist (e.g. after reinstall)
    function checkFirstRunRestore() {
        if (MainController.backupManager) {
            MainController.backupManager.checkFirstRunRestore();
        } else {
            startBluetoothScan();
        }
    }

    Connections {
        target: MainController.backupManager
        function onFirstRunRestoreResult(shouldOffer) {
            if (shouldOffer) {
                emptyDatabaseDialog.open();
            } else {
                startBluetoothScan();
            }
        }
    }

    // Start BLE scanning (called after first-run dialog or on subsequent launches)
    function startBluetoothScan() {
        // Try direct connect to DE1 if we have a saved address (this also starts scanning)
        if (BLEManager.hasSavedDE1) {
            BLEManager.tryDirectConnectToDE1()
        }

        if (Settings.primaryScaleAddress !== "") {
            // Try direct connect if we have a saved scale (this also starts scanning)
            BLEManager.tryDirectConnectToScale()
        } else {
            // First run or no saved scale - scan for scales so user can pair one
            BLEManager.scanForDevices()
        }
        // Always start scanning after a delay (startScan is safe to call multiple times)
        scanDelayTimer.start()
    }

    Timer {
        id: scanDelayTimer
        interval: 1000  // Give direct connect time, then ensure scan is running
        onTriggered: BLEManager.startScan()
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

            // Suppress Sleep reactions until DE1 has been awake at least once.
            // On connect, MachineState sees default Sleep state before real state arrives.
            // Reset on disconnect so reconnections are also protected.
            if (phase === MachineStateType.Phase.Disconnected) {
                root.startupGracePeriod = true
                // If we're on an operation page, navigate to idle (#575)
                if (currentPage === "espressoPage" || currentPage === "steamPage" || currentPage === "hotWaterPage" || currentPage === "flushPage" || currentPage === "descalingPage") {
                    console.log("Disconnected while on operation page (" + currentPage + ") - navigating to idle")
                    if (!pageStack.busy) {
                        pageStack.replace(null, idlePage)
                    } else {
                        pendingDisconnectNavigation = true
                    }
                }
            } else {
                // Clear deferred disconnect navigation on reconnect — machine is back,
                // don't navigate away from whatever page the user is on now.
                if (pendingDisconnectNavigation) pendingDisconnectNavigation = false
                if (root.startupGracePeriod &&
                       phase !== MachineStateType.Phase.Sleep) {
                    root.startupGracePeriod = false
                }
            }

            // Apply settings when entering operations (to handle GHC-initiated starts)
            if (phase === MachineStateType.Phase.Steaming && wasIdle) {
                // Note: the DE1Device.onStateChanged handler above already called
                // startSteamHeating when state reached Steam, which must happen before
                // phase can reach Steaming — so we don't re-call it here. The dedup
                // would elide the redundant BLE write, but the reason tags in the log
                // showed it firing on every steam session for no benefit.
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
                    MainController.sendSteamTemperature(0)  // This sets steamDisabled=true
                }
                // Stop and reset auto-flush timer (steaming fully ended)
                steamAutoFlushTimer.stop()
                root.steamAutoFlushCountdown = 0
            }

            // Update previous phase tracking
            root.previousPhase = phase

            // Cancel any pending completion overlay when a new operation starts
            // (e.g., second GHC flush within 3s of first flush ending)
            if (phase === MachineStateType.Phase.Flushing ||
                phase === MachineStateType.Phase.Steaming ||
                phase === MachineStateType.Phase.HotWater ||
                phase === MachineStateType.Phase.EspressoPreheating ||
                phase === MachineStateType.Phase.Preinfusion ||
                phase === MachineStateType.Phase.Pouring) {
                if (completionPending) {
                    console.log("Cancelling pending completion - new operation started (phase=" + phase + ")")
                    completionPending = false
                    completionTimer.stop()
                    completionOverlay.opacity = 0
                }
            }

            // Clear scale dialog deferral when machine reaches Ready or an active phase
            if (root.scaleDialogDeferred) {
                if (phase === MachineStateType.Phase.Idle ||
                    phase === MachineStateType.Phase.Ready ||
                    phase === MachineStateType.Phase.EspressoPreheating ||
                    phase === MachineStateType.Phase.Steaming ||
                    phase === MachineStateType.Phase.HotWater ||
                    phase === MachineStateType.Phase.Flushing) {
                    root.scaleDialogDeferred = false
                    // If a real physical scale connected during warmup, discard queued scale popups
                    // (FlowScale is always "connected" so don't let it suppress dialogs)
                    if (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) {
                        removeQueuedScalePopups()
                    } else if (Settings.primaryScaleAddress !== "") {
                        showNextPendingPopup()  // Show deferred dialog now
                    }
                } else if (phase === MachineStateType.Phase.Sleep) {
                    root.scaleDialogDeferred = false
                }
            }

            // Navigate to active operation pages (skip during page transition)
            if (phase === MachineStateType.Phase.EspressoPreheating ||
                phase === MachineStateType.Phase.Preinfusion ||
                phase === MachineStateType.Phase.Pouring ||
                phase === MachineStateType.Phase.Ending) {
                if (currentPage !== "espressoPage" && !pageStack.busy) {
                    pageStack.replace(null, espressoPage)
                }
            } else if (phase === MachineStateType.Phase.Steaming) {
                if (currentPage !== "steamPage" && !pageStack.busy) {
                    saveReturnToPage(currentPage)
                    pageStack.replace(null, steamPage)
                }
            } else if (phase === MachineStateType.Phase.HotWater) {
                if (currentPage !== "hotWaterPage" && !pageStack.busy) {
                    saveReturnToPage(currentPage)
                    pageStack.replace(null, hotWaterPage)
                }
            } else if (phase === MachineStateType.Phase.Flushing) {
                if (currentPage !== "flushPage" && !pageStack.busy) {
                    saveReturnToPage(currentPage)
                    pageStack.replace(null, flushPage)
                }
            } else if (phase === MachineStateType.Phase.Descaling) {
                if (currentPage !== "descalingPage" && !pageStack.busy) {
                    pageStack.replace(null, descalingPage)
                }
            } else if (phase === MachineStateType.Phase.Cleaning) {
                // For now, cleaning uses the built-in machine routine
                // Could navigate to a cleaning page in the future
            } else if (phase === MachineStateType.Phase.Sleep) {
                // Machine was put to sleep (e.g. via GHC stop button hold) - show screensaver
                // Skip if machine has never been awake since connecting (initial connect reports
                // Sleep before the wake command takes effect)
                if (!screensaverActive && !root.startupGracePeriod && !root.shuttingDown) {
                    console.log("Machine entered Sleep - showing screensaver")
                    // Scale LCD disable is handled by C++ phaseChanged handler in main.cpp
                    goToScreensaver()
                }
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
            }
        }
    }

    // Helper functions for navigation
    // Note: startNavigation() guard prevents double-taps on user-initiated navigation
    // Note: Page announcements are handled centrally by announceCurrentPage() on page change
    function goToIdle() {
        if (!startNavigation()) return
        var currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""

        // When leaving operation pages, check if we should return to a saved page
        if ((currentPage === "steamPage" || currentPage === "hotWaterPage" || currentPage === "flushPage") &&
            root.returnToPageName === "postShotReviewPage") {
            var shotId = root.returnToShotId > 0 ? root.returnToShotId : MainController.lastSavedShotId
            pageStack.replace(null, idlePage)
            pageStack.push(postShotReviewPage, { editShotId: shotId })
            root.returnToPageName = ""
            root.returnToShotId = 0
            return
        }

        if (currentPage !== "idlePage") {
            pageStack.replace(null, idlePage)
        }
        // Clear return tracking when going to idle from non-operation pages
        root.returnToPageName = ""
        root.returnToShotId = 0
    }

    function goToEspresso() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "espressoPage") {
            pageStack.replace(null, espressoPage)
        }
    }

    function goToSteam() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "steamPage") {
            pageStack.replace(null, steamPage)
        }
    }

    function goToHotWater() {
        if (!startNavigation()) return
        if (pageStack.currentItem && pageStack.currentItem.objectName !== "hotWaterPage") {
            pageStack.replace(null, hotWaterPage)
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
        // Route to appropriate editor based on editor type
        var editorType = ProfileManager.currentEditorType
        if (editorType === "pressure") {
            pageStack.push(pressureEditorPage)
        } else if (editorType === "flow") {
            pageStack.push(flowEditorPage)
        } else if (editorType === "dflow" || editorType === "aflow") {
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

    function goToPressureEditor() {
        if (!startNavigation()) return
        pageStack.push(pressureEditorPage)
    }

    function goToFlowEditor() {
        if (!startNavigation()) return
        pageStack.push(flowEditorPage)
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
        // Put idlePage on the stack so back button returns to idle, not the mid-shot graph
        pageStack.replace(null, idlePage)
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

        // Close any open popups to prevent burn-in (Qt Popup renders above the
        // StackView on the overlay layer, so the screensaver can't cover them).
        // Re-queue so they show after wake. screensaverActive is already true,
        // so showNextPendingPopup() (called by onClosed) is a no-op.
        var popups = [
            { dialog: updateDialog,            id: "update" },
            { dialog: flowScaleDialog,         id: "flowScale" },
            { dialog: scaleDisconnectedDialog, id: "scaleDisconnected" },
            { dialog: refillDialog,            id: "refill" },
            { dialog: bleErrorDialog,          id: "bleError" },
            { dialog: chargingMismatchDialog,  id: "chargingMismatch" },
            { dialog: noScaleAbortDialog,      id: null },
            { dialog: crashReportDialog,       id: null },
            { dialog: emptyDatabaseDialog,     id: null },
        ]
        for (var i = 0; i < popups.length; i++) {
            if (popups[i].dialog.visible) {
                if (popups[i].id) {
                    // Preserve bleError dialog state when re-queuing
                    if (popups[i].id === "bleError") {
                        queuePopup("bleError", {
                            errorMessage: bleErrorDialog.errorMessage,
                            isLocationError: bleErrorDialog.isLocationError,
                            isBluetoothError: bleErrorDialog.isBluetoothError
                        })
                    } else {
                        queuePopup(popups[i].id)
                    }
                }
                popups[i].dialog.close()
            }
        }

        // Dismiss any in-app Safari view (iOS Claude Desktop discuss overlay)
        // so the screensaver can render without being covered by a modal.
        if (Qt.platform.os === "ios") Settings.dismissDiscussOverlay()

        // Navigate to screensaver page for all modes (including "disabled")
        // For "disabled" mode, ScreensaverPage dims the backlight to minimum
        // and shows a black overlay. We keep FLAG_KEEP_SCREEN_ON set to avoid
        // potential EGL surface issues (QTBUG-45019 class of bugs).
        pageStack.replace(null, screensaverPage)
    }

    function goToIdleFromScreensaver() {
        screensaverActive = false
        // Brightness is restored in ScreensaverPage.StackView.onRemoved
        // Initialize sleep countdown (stayAwake is set separately by onAutoWakeTriggered if needed)
        root.sleepCountdownNormal = root.autoSleepMinutes
        root.sleepCountdownStayAwake = 0  // Already satisfied unless auto-wake sets it
        console.log("Waking from screensaver: normal countdown=" + root.sleepCountdownNormal +
                    " pendingPopups=" + pendingPopups.length)
        pageStack.replace(null, idlePage)
        // Show any popups that arrived during screensaver
        if (pendingPopups.length > 0) {
            Qt.callLater(root.showNextPendingPopup)
        }
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
                var prev = root.sleepCountdownNormal
                root.sleepCountdownNormal = root.autoSleepMinutes
                if (prev <= 5) console.log("[AutoSleep] Reset by touch: " + prev + " -> " + root.sleepCountdownNormal)
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

        function onFrameChanged(frameIndex, frameName, transitionReason) {
            AccessibilityManager.playTick()
        }
    }

    // DYE: Navigate to shot metadata page after stop overlay dismisses (event-driven)
    property int pendingShotId: -1
    property bool pendingMetadataNavigation: false

    Connections {
        target: MainController

        function onShotEndedShowMetadata() {
            root.pendingShotId = MainController.lastSavedShotId
            console.log("Shot ended, navigate to review. shotId:", root.pendingShotId,
                        "overlayVisible:", root.stopOverlayVisible)

            if (root.stopOverlayVisible) {
                // Stop overlay still showing — defer navigation to when it expires
                root.pendingMetadataNavigation = true
                stopOverlayTimer.restart()
            } else {
                // Stop overlay already expired (e.g. SAW settling outlasted the
                // 3s overlay timer). Navigate directly to shot review instead of
                // restarting the timer for another 3s.

                // Guard: if a new shot started while the old one was still saving,
                // onShotStarted cleared the overlay but this stale signal arrived
                // late. Don't interrupt the active shot.
                var currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""
                if (currentPage === "espressoPage") {
                    console.log("Post-shot navigation: new shot in progress, skipping stale review")
                    return
                }

                var timeout = Number(Settings.value("postShotReviewTimeout", 31))
                if (timeout === 0) {
                    console.log("Post-shot review: Instant timeout, going to idle")
                    goToIdle()
                } else if (root.pendingShotId > 0) {
                    goToShotMetadata(root.pendingShotId)
                } else {
                    console.warn("Post-shot navigation: no valid pendingShotId after overlay expired")
                }
            }
        }
    }

    // Auto-wake: Exit screensaver when scheduled wake time is reached
    Connections {
        target: MainController

        function onAutoWakeTriggered() {
            console.log("[Main] Auto-wake triggered")
            if (screensaverActive) {
                goToIdleFromScreensaver()
            }
            // Arm on every auto-wake, not only when exiting screensaver — the app
            // may already be awake at the scheduled time, and skipping would let
            // the machine auto-sleep before the stay-awake window applies.
            if (Settings.autoWakeStayAwakeEnabled && Settings.autoWakeStayAwakeMinutes > 0) {
                root.sleepCountdownStayAwake = Settings.autoWakeStayAwakeMinutes
                console.log("Auto-wake: stayAwake countdown=" + root.sleepCountdownStayAwake)
            } else {
                console.log("Auto-wake: stayAwake not armed (enabled=" + Settings.autoWakeStayAwakeEnabled +
                            ", minutes=" + Settings.autoWakeStayAwakeMinutes + ")")
            }
        }

        function onRemoteSleepRequested() {
            console.log("[Main] Remote sleep requested via MQTT/REST API")
            if (!screensaverActive) {
                goToScreensaver()
            }
        }

        function onFlowCalibrationAutoUpdated(profileTitle, oldValue, newValue) {
            flowCalToastText = TranslationManager.translate("main.flowCalUpdated",
                "Flow cal updated for %1: %2 → %3").arg(profileTitle).arg(oldValue.toFixed(2)).arg(newValue.toFixed(2))
            flowCalToast.opacity = 1
            flowCalToastTimer.restart()
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(flowCalToastText)
            }
        }
    }

    // ============ PROFILE UPLOAD RETRY TOAST ============
    // Reconnecting… toast shown while ProfileManager is retrying a failed
    // profile upload. Bound reactively to ProfileManager.profileUploadRetrying
    // so it appears within one frame of the first retry arming and clears
    // immediately on success or when the communication-failure dialog takes
    // over.
    Tr { id: trReconnectingToast; key: "main.toast.reconnecting"; fallback: "Reconnecting…"; visible: false }
    Tr { id: trShotStoppedReloading; key: "main.toast.shotStoppedReloading"; fallback: "Shot stopped while profile reloads…"; visible: false }

    // Latched true when ProfileManager aborts a shot during the retry window;
    // cleared when the retry state clears (success or exhaustion). Drives the
    // toast text so the user knows *why* the shot stopped, not just that
    // something is retrying.
    property bool shotStoppedForProfileRetry: false

    Rectangle {
        id: reconnectingToast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(40)
        anchors.horizontalCenter: parent.horizontalCenter
        width: reconnectingToastLabel.implicitWidth + Theme.scaled(32)
        height: reconnectingToastLabel.implicitHeight + Theme.scaled(16)
        radius: Theme.cardRadius
        color: Theme.surfaceColor
        opacity: (ProfileManager.profileUploadRetrying
                  && !ProfileManager.de1CommunicationFailure) ? 1 : 0
        visible: opacity > 0
        z: 600
        Accessible.ignored: true

        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }

        Text {
            id: reconnectingToastLabel
            anchors.centerIn: parent
            text: root.shotStoppedForProfileRetry
                  ? trShotStoppedReloading.text
                  : trReconnectingToast.text
            color: Theme.textColor
            font.pixelSize: Theme.scaled(13)
            Accessible.ignored: true
        }
    }

    Connections {
        target: ProfileManager
        function onProfileUploadRetryingChanged() {
            if (ProfileManager.profileUploadRetrying) {
                if (AccessibilityManager.enabled) {
                    AccessibilityManager.announce(reconnectingToastLabel.text)
                }
            } else {
                // Retry window closed (either succeeded or exhausted) —
                // reset the shot-stopped message for next time.
                root.shotStoppedForProfileRetry = false
            }
        }
        function onShotAbortedProfileUploadRetrying() {
            root.shotStoppedForProfileRetry = true
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trShotStoppedReloading.text, true)
            }
        }
    }

    // ============ AUTO FLOW CALIBRATION TOAST ============
    property string flowCalToastText: ""

    Rectangle {
        id: flowCalToast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(40)
        anchors.horizontalCenter: parent.horizontalCenter
        width: flowCalToastLabel.implicitWidth + Theme.scaled(32)
        height: flowCalToastLabel.implicitHeight + Theme.scaled(16)
        radius: Theme.cardRadius
        color: Theme.surfaceColor
        opacity: 0
        visible: opacity > 0
        z: 600
        Accessible.ignored: true

        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }

        Text {
            id: flowCalToastLabel
            anchors.centerIn: parent
            text: flowCalToastText
            color: Theme.textColor
            font.pixelSize: Theme.scaled(13)
            Accessible.ignored: true
        }
    }

    Timer {
        id: flowCalToastTimer
        interval: 4000
        onTriggered: flowCalToast.opacity = 0
    }

    // ============ SHOT EXPORT BULK COMPLETION TOAST ============
    // Shown once the initial "export all shots" pass triggered by enabling
    // Settings.exportShotsToFile has finished writing files to the user
    // history folder. Silent-until-done so the toggle behaves like a plain
    // boolean preference.
    property string shotExportToastText: ""

    Rectangle {
        id: shotExportToast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(40)
        anchors.horizontalCenter: parent.horizontalCenter
        width: shotExportToastLabel.implicitWidth + Theme.scaled(32)
        height: shotExportToastLabel.implicitHeight + Theme.scaled(16)
        radius: Theme.cardRadius
        color: Theme.surfaceColor
        opacity: 0
        visible: opacity > 0
        z: 600
        Accessible.ignored: true

        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }

        Text {
            id: shotExportToastLabel
            anchors.centerIn: parent
            text: shotExportToastText
            color: Theme.textColor
            font.pixelSize: Theme.scaled(13)
            Accessible.ignored: true
        }
    }

    Timer {
        id: shotExportToastTimer
        interval: 4000
        onTriggered: shotExportToast.opacity = 0
    }

    Connections {
        target: ShotHistoryExporter
        function onBulkExportFinished(written, skipped, failed) {
            // Suppress the toast when nothing was actually written — a warm
            // startup where every file is already current is the common case
            // and a silent no-op is the honest signal there.
            if (written === 0 && failed === 0) {
                return
            }
            if (failed > 0) {
                shotExportToastText = TranslationManager.translate(
                    "main.toast.exportShotsPartial",
                    "Exported %1 shots; %2 failed").arg(written).arg(failed)
            } else {
                shotExportToastText = TranslationManager.translate(
                    "main.toast.exportShotsDone",
                    "Exported %1 shots").arg(written)
            }
            shotExportToast.opacity = 1
            shotExportToastTimer.restart()
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(shotExportToastText)
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

    // Discard stale scale popups when scale reconnects
    Connections {
        target: ScaleDevice
        enabled: ScaleDevice !== null

        function onConnectedChanged() {
            if (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) {
                removeQueuedScalePopups()
            }
        }
    }

    // ============ PER-PAGE SCALE CONFIGURATION OVERLAY ============
    // Floating centered control for adjusting page scale
    // Uses scaledBase() to maintain consistent size regardless of current page scale
    Rectangle {
        id: pageScaleOverlay
        visible: root.appInitialized && Theme.configurePageScaleEnabled && !screensaverActive && Theme.currentPageObjectName !== ""
        z: 800  // Above most content, below dialogs


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
                text: TranslationManager.translate("settings.scale.label", "Scale:")
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
                accessibleName: TranslationManager.translate("main.pageScale", "Page scale")

                onValueModified: function(newValue) {
                    Theme.pageScaleMultiplier = newValue
                    Settings.setValue("pageScale/" + Theme.currentPageObjectName, newValue)
                }
            }
        }
    }

    // ============ GLOBAL HIDE KEYBOARD BUTTON ============
    // Appears when a text input has focus (= keyboard should be showing).
    // Qt.inputMethod.visible is unreliable on Android (goes false after 1s),
    // so we check if the active focus item has a cursorPosition property
    // (present on TextInput/TextArea but not on Text or Button).
    property bool _textInputFocused: {
        var item = root.activeFocusItem
        if (!item) return false
        if (!("cursorPosition" in item)) return false
        // Suppress when focus is inside a popup/dialog — the global button sits
        // behind the modal overlay and can't be tapped. Dialogs with text inputs
        // should provide their own hide-keyboard button (see HideKeyboardButton.qml).
        // Walk the visual parent chain: popup content goes through the Overlay item,
        // regular page content does not.
        var overlay = root.Overlay.overlay
        for (var p = item; p; p = p.parent) {
            if (p === overlay)
                return false
        }
        return true
    }
    Rectangle {
        id: globalHideKeyboardButton
        visible: _textInputFocused && (Qt.platform.os === "android" || Qt.platform.os === "ios")
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin + 4
        width: Theme.scaled(36)
        height: Theme.scaled(36)
        radius: Theme.scaled(18)
        color: Theme.primaryColor
        z: 9999  // Above everything

        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("main.hidekeyboard", "Hide keyboard")
        Accessible.focusable: true
        Accessible.onPressAction: hideKeyboardArea.clicked(null)

        Image {
            anchors.centerIn: parent
            width: Theme.scaled(20)
            height: Theme.scaled(20)
            source: "qrc:/icons/hide-keyboard.svg"
            sourceSize: Qt.size(width, height)
            Accessible.ignored: true
        }

        MouseArea {
            id: hideKeyboardArea
            anchors.fill: parent
            onClicked: {
                // Must clear focus BEFORE hiding keyboard, otherwise
                // KeyboardAwareContainer sees focus + no keyboard and reopens it
                var window = globalHideKeyboardButton.Window.window
                if (window && window.activeFocusItem)
                    window.activeFocusItem.focus = false
                Qt.inputMethod.hide()
            }
        }
    }

    // ── Library thumbnail capture (always available, even when LibraryPanel is not loaded) ──
    // Off-screen renderer: uses layer.enabled to force FBO rendering (Android GPU skips off-screen items)
    Item {
        id: libThumbContainer
        visible: false
        width: Theme.scaled(280)
        height: Math.max(libThumbFull.height, libThumbCompact.height)
        layer.enabled: visible

        LibraryItemCard {
            id: libThumbFull
            width: parent.width
            displayMode: 0
            entryData: ({})
            isSelected: false
            showBadge: false
            livePreview: true
        }

        LibraryItemCard {
            id: libThumbCompact
            y: libThumbFull.height + Theme.scaled(4)
            width: parent.width
            displayMode: 1
            entryData: ({})
            isSelected: false
            showBadge: false
            livePreview: true
        }
    }

    Timer {
        id: libCaptureTimer
        interval: 200
        repeat: false
        property string captureEntryId: ""
        onTriggered: {
            libThumbFull.grabToImage(function(fullResult) {
                WidgetLibrary.saveThumbnail(captureEntryId, fullResult.image)
                libThumbCompact.grabToImage(function(compactResult) {
                    WidgetLibrary.saveThumbnailCompact(captureEntryId, compactResult.image)
                    libThumbContainer.visible = false
                }, Qt.size(Theme.scaled(280), libThumbCompact.height))
            }, Qt.size(Theme.scaled(280), libThumbFull.height))
        }
    }

    Connections {
        target: WidgetLibrary
        function onEntryAdded(entryId) {
            triggerLibraryThumbnailCapture(entryId)
        }
        function onRequestThumbnailCapture(entryId) {
            triggerLibraryThumbnailCapture(entryId)
        }
    }

    function triggerLibraryThumbnailCapture(entryId) {
        var data = WidgetLibrary.getEntryData(entryId)
        if (!data || !data.type) return

        // Theme entries generate their own color-grid thumbnail in C++;
        // skip QML screenshot capture which would overwrite it
        if (data.type === "theme") return

        libThumbFull.entryData = data
        libThumbCompact.entryData = data
        libThumbContainer.z = -1
        libThumbContainer.visible = true

        libCaptureTimer.captureEntryId = entryId
        libCaptureTimer.start()
    }

    // Empty database + backups exist: ask user if they want to restore
    Dialog {
        id: emptyDatabaseDialog
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        closePolicy: Dialog.NoAutoClose

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                text: TranslationManager.translate("main.emptydb.title", "Restore Backup?")
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: TranslationManager.translate("main.emptydb.message",
                    "We found backups from a previous installation. Would you like to restore your data?")
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.Wrap
                width: parent.width
            }

            Row {
                spacing: Theme.spacingMedium
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    text: TranslationManager.translate("main.emptydb.skip", "Skip")
                    accessibleName: TranslationManager.translate("main.emptydb.skipAccessible", "Skip restore and start fresh")
                    onClicked: {
                        emptyDatabaseDialog.close();
                        startBluetoothScan();
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("main.emptydb.restore", "Restore")
                    primary: true
                    accessibleName: TranslationManager.translate("main.emptydb.restoreAccessible", "Open restore settings")
                    onClicked: {
                        emptyDatabaseDialog.close();
                        startBluetoothScan();
                        goToSettings(3);  // Navigate to History & Data tab
                    }
                }
            }
        }
    }
}
