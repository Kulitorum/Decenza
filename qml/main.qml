import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import Decenza
import "components/OperationNavigationPolicy.js" as OperationNavigationPolicy

// Rooted at QtQuick.Templates.ApplicationWindow for the same reason the pages and the
// button family are: a Controls root resolves to the style's composite, whose base chain
// qmlcachegen cannot walk, so every `root.<prop>` in this file lost AOT compilation --
// 583 skips, the whole of the remaining id class.
//
// Nothing is lost. Material's ApplicationWindow.qml is three lines and its only content is
// `color: Material.backgroundColor`, which the `color:` below already overrode. The Qt 6.9+
// safe-area padding the `topPadding` block fights is C++ (QQuickApplicationWindow), not the
// style, so those four lines are still doing their job.
//
// `import QtQuick.Controls` stays -- this file has 16 inline Dialogs and a StackView.
T.ApplicationWindow {
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

    // debugLiveView, pendingBrewDialog, userExitedFlush,
    // scaleDialogDeferred and stopReason now live on the AppShell singleton, which
    // is where the pages that share them can actually see the declaration.

    // True when the app is allowed to start machine operations on-screen.
    // The hardware Group Head Controller (GHC), when present and active, takes
    // exclusive control of starting shots/steam/etc., so on-screen start calls
    // are only valid in headless (no/inactive GHC) or simulation mode.
    readonly property bool canStartOperations: DE1Device.isHeadless || DE1Device.simulationMode


    // Single, global Brew Settings dialog — reachable from anywhere via
    // root.openBrewSettings() (home-screen content, the persistent status bar, the
    // "Open Brew Settings" action, layout previews). Lives at the root, not inside
    // IdlePage (a StackView page, whose children can't be reached from the status
    // bar outside the stack), so any host can open it regardless of the current page.
    function openBrewSettings() {
        globalBrewDialog.open()
    }
    BrewDialog {
        id: globalBrewDialog
    }



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
            let centerX = offsetX + item.width / 2
            let centerY = offsetY + item.height / 2
            results.push({ text: item, x: centerX, y: centerY })
        }

        // Check contentItem for ScrollView/Flickable
        if (item.contentItem && item.contentX !== undefined) {
            collectTexts(item.contentItem, offsetX + scrollOffsetX, offsetY + scrollOffsetY, results)
        }

        // Recurse into children
        var childList = item.children || item.contentChildren || []
        for (let i = 0; i < childList.length; i++) {
            let child = childList[i]
            if (!child || !child.visible || child.width === undefined) continue
            collectTexts(child, offsetX + child.x + scrollOffsetX, offsetY + child.y + scrollOffsetY, results)
        }
    }

    function findTextAt(item, tapX, tapY) {
        var results = []
        collectTexts(item, 0, 0, results)

        var closest = null
        var closestDist = accessibilitySearchRadius

        for (let i = 0; i < results.length; i++) {
            let dx = results[i].x - tapX
            let dy = results[i].y - tapY
            let dist = Math.sqrt(dx * dx + dy * dy)
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

        // Scale first (it's faster/simpler). "Keep scale on" covers closing the app
        // too: display off, as when the DE1 sleeps, but the scale stays on (#1981).
        if (ScaleDevice && ScaleDevice.connected) {
            if (Settings.keepScaleOn) {
                WebDebugLogger.debug("Scale", "main", "Turning the scale display off on app close (keep scale on)")
                ScaleDevice.disableLcd()
            } else {
                WebDebugLogger.debug("Scale", "main", "Sending scale to sleep on app close")
                ScaleDevice.sleep()
            }
        }

        // Small delay before sending DE1 sleep to let scale command go through
        close.accepted = false
        scaleSleepTimer.start()
    }

    DecenzaDialog {
        id: firmwareFlashExitDialog
        logName: "Firmware flash exit"
        onClosed: root.showNextPendingPopup()  // anyModalDialogVisible() lists this dialog
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
                // Dialog announces title + message via
                // AccessibilityManager.announce() in onOpened; ignore
                // the visible Text nodes so TalkBack/VoiceOver doesn't
                // re-read them on linear swipe.
                Accessible.ignored: true
            }

            Text {
                text: trFwExitMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
                Accessible.ignored: true
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
    // tab) as soon as the flash verifies, and also if the DE1 reconnects
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

    DecenzaDialog {
        id: firmwareRebootRequiredDialog
        logName: "Firmware reboot required"
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
        Tr { id: trFwRebootMessage; key: "main.dialog.firmwareRebootRequired.message"; fallback: "The new firmware is flashed and verified. Switch the DE1 off (back-panel switch or unplug), wait a few seconds, then switch it back on to load the new firmware. Decenza will finish the update automatically once the DE1 reconnects."; visible: false }
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
                // titleFont (24, bold) for emphasis — larger than the
                // subtitleFont other dialogs use because the power-cycle
                // instruction must be unmissable.
                text: trFwRebootTitle.text
                font: Theme.titleFont
                color: Theme.warningColor
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                // Dialog announces title + message via
                // AccessibilityManager.announce() in onOpened; ignore
                // the visible Text nodes so TalkBack/VoiceOver doesn't
                // re-read them on linear swipe.
                Accessible.ignored: true
            }

            Text {
                // subtitleFont (18, bold) for body — bolder than the
                // plain bodyFont used in other dialogs.
                text: trFwRebootMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.subtitleFont
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                Accessible.ignored: true
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
                WebDebugLogger.debug("DE1", "main", ["Sending DE1 to sleep on app close"].map(String).join(" "))
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

    // Sleep system:
    // - sleepCountdownNormal: resets on user activity, counts down from autoSleepMinutes
    // - the scheduled stay-awake window is evaluated continuously from the
    //   schedule + wall clock via AutoWakeManager.isWithinStayAwakeWindow(),
    //   so it survives app restarts / manual wakes / process suspension
    //   (was previously a one-shot counter armed only on the wake event,
    //   which silently failed whenever that exact tick wasn't observed —
    //   #1203). Sleep only when the normal countdown has expired AND we are
    //   not inside a scheduled stay-awake window.
    onAutoSleepMinutesChanged: {
        if (autoSleepMinutes > 0 && sleepCountdownNormal < 0) {
            sleepCountdownNormal = autoSleepMinutes
            stayAwakeSuppressionLogged = false
            WebDebugLogger.debug("AutoSleep", "main", ["Setting changed: normal=" + sleepCountdownNormal].map(String).join(" "))
        }
    }
    property int sleepCountdownNormal: -1      // Minutes remaining (-1 = not started)
    // True once this stay-awake window's "staying awake" line has been said. The
    // line reports a state that persists, so it is emitted on the edge into that
    // state and not on every tick of the one-minute timer — see the call site.
    // Cleared wherever the countdown restarts, which is what makes a LATER window
    // report itself rather than inheriting this one's silence.
    property bool stayAwakeSuppressionLogged: false

    // Active operation phases that should pause the sleep countdown
    property bool operationActive: {
        var phase = MachineState.phase
        // Treat an in-progress firmware flash as an active operation so the
        // auto-sleep countdown can't fire mid-upload and cut BLE.
        var fw = MainController.firmwareUpdater
        if (fw && fw.isFlashing) return true
        return phase === MachineState.Phase.EspressoPreheating ||
               phase === MachineState.Phase.Preinfusion ||
               phase === MachineState.Phase.Pouring ||
               phase === MachineState.Phase.Ending ||
               phase === MachineState.Phase.Steaming ||
               phase === MachineState.Phase.HotWater ||
               phase === MachineState.Phase.Flushing ||
               phase === MachineState.Phase.Descaling ||
               phase === MachineState.Phase.Cleaning ||
               phase === MachineState.Phase.Transport
    }

    // Sleep countdown timer - ticks every minute
    Timer {
        id: sleepCountdownTimer
        interval: 60 * 1000  // 1 minute
        running: !root.screensaverActive && !root.operationActive && root.autoSleepMinutes > 0
        repeat: true
        onTriggered: {
            if (root.sleepCountdownNormal > 0) root.sleepCountdownNormal--

            // The schedule takes priority over auto-off: never sleep while
            // inside a scheduled stay-awake window, regardless of the
            // inactivity countdown.
            if (root.sleepCountdownNormal <= 0) {
                if (AutoWakeManager.isWithinStayAwakeWindow()) {
                    // ONCE per window, on the edge — not once a minute.
                    //
                    // sleepCountdownNormal stays <= 0 once it reaches zero, so this
                    // branch is re-entered on every tick for as long as the window
                    // lasts. It logged each time: 489 lines in one submitted log,
                    // about eight hours of a timer saying the same thing, and the
                    // single largest repeater in the whole buffer. The state is what
                    // matters, so the transition into it is what gets reported.
                    //
                    // A flag rather than a collapse: this is not a periodic source
                    // whose repeats need counting, it is one fact that stays true.
                    // Cleared below and whenever the countdown is reset, so a later
                    // window reports itself again.
                    if (!root.stayAwakeSuppressionLogged) {
                        root.stayAwakeSuppressionLogged = true
                        WebDebugLogger.info("AutoSleep", "main", ["Inactivity elapsed but inside scheduled stay-awake window — staying awake until it ends"].map(String).join(" "))
                    }
                } else {
                    root.stayAwakeSuppressionLogged = false
                    WebDebugLogger.info("AutoSleep", "main", ["Inactivity elapsed, no stay-awake window — triggering sleep"].map(String).join(" "))
                    root.triggerAutoSleep()
                }
            }
        }
    }

    // Auto-load countdown (minutes remaining on the Idle page before reverting
    // to the pinned profile). -1 means "not running" — either the feature is
    // off, no profile is pinned, the revert-minutes is 0, or we're not on the
    // Idle page.
    property int autoLoadIdleCountdown: -1

    // Auto-load idle-revert tick. Lives in its own timer (not the sleep
    // countdown's) so the trigger keeps working when the user has set
    // auto-sleep to "Never" — sleepCountdownTimer is gated on
    // autoSleepMinutes > 0, which would otherwise silently kill this path.
    // Allowed under CLAUDE.md's UI-behaviour-timer carve-out.
    Timer {
        id: autoLoadCountdownTimer
        interval: 60 * 1000  // 1 minute
        running: root.autoLoadIdleCountdown > 0 && !root.screensaverActive && !root.operationActive
        repeat: true
        onTriggered: {
            if (root.autoLoadIdleCountdown <= 0) return
            root.autoLoadIdleCountdown--
            if (root.autoLoadIdleCountdown <= 0) {
                let pageName = pageStack.currentItem ? pageStack.currentItem.objectName : ""
                if (pageName === "idlePage") {
                    ProfileManager.loadAutoLoadProfileIfNeeded()
                    MainController.loadAutoLoadRecipeIfNeeded()
                }
                root.autoLoadIdleCountdown = root.autoLoadCountdownReload()
            }
        }
    }

    function autoLoadCountdownReload() {
        var pageName = pageStack.currentItem ? pageStack.currentItem.objectName : ""
        // 0 disables the idle-revert trigger only — startup and wake-from-sleep
        // still fire via their own paths. Nothing configured on EITHER side
        // (recipe-auto-load: the two are mutually exclusive, so at most one
        // of these is ever non-default) also disables it.
        if ((Settings.app.autoLoadProfileFilename === "" && Settings.dye.autoLoadRecipeId === -1)
            || Settings.app.autoLoadRevertMinutes <= 0
            || pageName !== "idlePage") {
            return -1
        }
        return Settings.app.autoLoadRevertMinutes
    }

    function autoLoadResetCountdown() {
        root.autoLoadIdleCountdown = autoLoadCountdownReload()
    }

    Connections {
        target: Settings.app
        function onAutoLoadProfileFilenameChanged() { root.autoLoadResetCountdown() }
        function onAutoLoadRevertMinutesChanged() { root.autoLoadResetCountdown() }
    }

    Connections {
        target: Settings.dye
        function onAutoLoadRecipeIdChanged() { root.autoLoadResetCountdown() }
    }

    // Trigger: DE1 wake from Sleep → Idle. Tracks previous state in QML since
    // DE1Device does not expose it. State values come from DE1::State (see
    // src/ble/protocol/de1characteristics.h): Sleep = 0x00, Idle = 0x02.
    //
    // BLE reconnect handling: a reconnect can replay a Sleep → Idle transition
    // even when the user didn't physically wake the machine. We reset the
    // previous-state tracker on every connected-change so the first state
    // notification after a (re)connect is treated as the initial state, not a
    // transition. A real user wake-from-sleep produces a second notification
    // while the machine stays connected; that one fires the auto-load.
    property int autoLoadPreviousDe1State: -1
    readonly property int de1StateSleep: 0x00
    readonly property int de1StateIdle: 0x02
    Connections {
        target: DE1Device
        function onConnectedChanged() {
            root.autoLoadPreviousDe1State = -1
        }
        function onStateChanged() {
            var prev = root.autoLoadPreviousDe1State
            var curr = DE1Device.state
            root.autoLoadPreviousDe1State = curr
            if (prev === root.de1StateSleep && curr === root.de1StateIdle) {
                ProfileManager.loadAutoLoadProfileIfNeeded()
                MainController.loadAutoLoadRecipeIfNeeded()
            }
        }
    }

    // Trigger: stale-target toast surface — ProfileManager clears the setting
    // and emits this signal when the pinned filename is no longer in the
    // Selected list.
    Connections {
        target: ProfileManager
        function onAutoLoadStaleCleared() {
            autoLoadStaleToast.show(trAutoLoadStaleToast.text)
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trAutoLoadStaleToast.text, true)
            }
        }
    }

    // Trigger: recipe-auto-load stale-target toast — MainController clears
    // Settings.dye.autoLoadRecipeId and emits this signal when the pinned
    // recipe no longer exists or was archived. Shares the toast surface
    // below with the profile version (message property swapped per-trigger).
    Connections {
        target: MainController
        function onHomeFlushStopRequested() { AppShell.userExitedFlush = true }
        function onAutoLoadRecipeStaleCleared() {
            autoLoadStaleToast.show(trAutoLoadRecipeStaleToast.text)
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trAutoLoadRecipeStaleToast.text, true)
            }
        }
    }

    // Reset normal countdown on user activity (phase change)
    Connections {
        target: MachineState
        function onPhaseChanged() {
            if (!root.screensaverActive && root.autoSleepMinutes > 0) {
                root.sleepCountdownNormal = root.autoSleepMinutes
                root.stayAwakeSuppressionLogged = false
            }
            // Phase change is also user activity for the auto-load countdown
            root.autoLoadResetCountdown()
        }
    }

    // Detect when DE1 enters Steam state (even during heating/FinalHeating substate)
    // This clears steamDisabled BEFORE applySteamSettings runs, so GHC-initiated
    // steaming works correctly even if keepWarmWhenIdle is false
    Connections {
        target: DE1Device
        function onStateChanged() {
            // DE1::State::Steam = 5
            if (DE1Device.state === 5) {
                // The DE1 enters Steam in firmware on a GHC press and the app
                // cannot refuse it, so steam it: event permission outranks a
                // "Heater off" selection. That entry carries no values of its
                // own, so the live settings — the last real pitcher's duration,
                // flow and temperature — are what get used. Say so rather than
                // silently substituting them.
                WebDebugLogger.debug("Steam", "main", ["DE1 entered Steam state - starting heater, navigating to SteamPage"].map(String).join(" "))
                MainController.startSteamHeating("de1-state-steam")
                if (Settings.brew.isHeaterOffPitcher(Settings.brew.selectedSteamPitcher)) {
                    steamHeaterOffToast.show(trSteamHeaterOffSteaming.text)
                    if (AccessibilityManager.enabled)
                        AccessibilityManager.announce(trSteamHeaterOffSteaming.text)
                }
                // Navigate to SteamPage immediately so user sees heating progress
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.steamPage, "steamPage")
            }
        }
        function onSubStateChanged() {
            // DE1::SubState::Puffing = 20
            if (DE1Device.state === 5 && DE1Device.subState === 20) {
                WebDebugLogger.debug("DE1", "main", ["DE1 entered Puffing substate"].map(String).join(" "))
            }
        }
    }


    // Handle settings changes
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "autoSleepMinutes") {
                let val = Settings.value("autoSleepMinutes", 60)
                root.autoSleepMinutes = (val === undefined || val === null) ? 60 : parseInt(val)
                // Update normal countdown to new value
                if (!root.screensaverActive && root.autoSleepMinutes > 0) {
                    root.sleepCountdownNormal = root.autoSleepMinutes
                    root.stayAwakeSuppressionLogged = false
                }
            } else if (key === "ui/configurePageScale") {
                let val = Settings.value("ui/configurePageScale", false)
                WebDebugLogger.debug("App", "main", ["configurePageScale changed:", val, "type:", typeof val].map(String).join(" "))
                Theme.configurePageScaleEnabled = (val === true || val === "true")
                WebDebugLogger.debug("App", "main", ["configurePageScaleEnabled set to:", Theme.configurePageScaleEnabled].map(String).join(" "))
            }
        }
    }

    function triggerAutoSleep() {
        WebDebugLogger.debug("AutoSleep", "main", ["triggerAutoSleep called — DE1 connected=" +
                   (DE1Device ? DE1Device.connected : "null") +
                   ", scale connected=" + (ScaleDevice ? ScaleDevice.connected : "null")].map(String).join(" "))
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

    // Flag to prevent premature UI display
    property bool appInitialized: false

    // Suppress screensaver until DE1 has been awake at least once since connecting
    // (on connect, MachineState sees default Sleep state before real state arrives)
    property bool startupGracePeriod: true

    // Suppress screensaver during shutdown — we send DE1 to sleep which triggers Sleep phase
    property bool shuttingDown: false


    // True while a previously-connected scale is disconnected (a mid-session
    // drop). Set on scaleDisconnected, cleared on scaleConnected. Lets us defer
    // the "Scale Disconnected" notice until a reconnect actually FAILS
    // (flowScaleFallback), so a transient drop that auto-reconnects never nags.
    property bool scaleDropPending: false

    // Shared translation strings for dialog buttons
    Tr { id: trCommonOk; key: "common.button.ok"; fallback: "OK"; visible: false }
    Tr { id: trCommonDismissDialog; key: "common.accessibility.dismissDialog"; fallback: "Dismiss dialog"; visible: false }

    // Popup queue: popups that arrived during screensaver, shown after wake
    property var pendingPopups: []

    function queuePopup(popupId, params) {
        // Deduplicate by popupId
        for (let i = 0; i < pendingPopups.length; i++) {
            if (pendingPopups[i].id === popupId) return
        }
        pendingPopups = pendingPopups.concat([{id: popupId, params: params || {}}])
    }

    // Is a blocking dialog already on screen? Used to QUEUE a popup rather than
    // open it over one the user is currently reading. Deliberately enumerated:
    // QML offers no "any Popup open" query, and a heuristic over children would
    // sweep in inline popups, pickers and the brew dialog, which are not the
    // same thing. Excludes globalBrewDialog and mcpConfirmDialog on purpose —
    // the first is a working surface the user drives, the second is itself an
    // approval prompt that must not be delayed behind an advisory message.
    //
    // Anything added here should also be added to a queue case in
    // showNextPendingPopup below, or it can block a popup that never reappears.
    function anyModalDialogVisible() {
        return flowScaleDialog.visible || scaleDisconnectedDialog.visible
            || updateDialog.visible || chargingMismatchDialog.visible
            || bleErrorDialog.visible || refillDialog.visible
            || globalSupportDialogs.profileRefusedDialog.visible
            || globalSupportDialogs.de1CommunicationErrorDialog.visible
            || firmwareFlashExitDialog.visible || firmwareRebootRequiredDialog.visible
            || noScaleAbortDialog.visible || globalSupportDialogs.crashReportDialog.visible
            || recipeActivationFailedDialog.visible || standbySwitchDialog.visible
            || localNetworkDeniedDialog.visible || decentMachineDialog.visible
            || quitConfirmDialog.visible
    }

    function showNextPendingPopup() {
        if (screensaverActive) return  // Don't show popups during screensaver
        if (pendingPopups.length === 0) return

        // While scale dialogs are deferred, skip scale popups and show others
        var queue = pendingPopups.slice()
        var next
        if (AppShell.scaleDialogDeferred) {
            let idx = -1
            for (let i = 0; i < queue.length; i++) {
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
            case "localNetworkDenied": localNetworkDeniedDialog.open(); break
            case "bleError":
                // Skip a stale generic connection error if the DE1 has since
                // reconnected (e.g. an overnight link drop that self-healed while
                // the popup sat behind the screensaver queue, #1423). Permission
                // errors still need the user to act, so they always show.
                if (!next.params.isLocationError && !next.params.isBluetoothError
                        && DE1Device && DE1Device.connected) {
                    showNextPendingPopup()  // Skip stale connection error, show next
                    break
                }
                bleErrorDialog.errorMessage = next.params.errorMessage || ""
                bleErrorDialog.isLocationError = next.params.isLocationError || false
                bleErrorDialog.isBluetoothError = next.params.isBluetoothError || false
                bleErrorDialog.open()
                break
            case "refill":
                // Only show if machine is still in Refill phase
                if (MachineState.phase === MachineState.Phase.Refill)
                    refillDialog.open()
                else
                    showNextPendingPopup()  // Skip stale refill, show next
                break
            case "standbySwitch":
                // Only show if the condition is still live
                if (MachineState.standbySwitchOpen)
                    standbySwitchDialog.open()
                else
                    showNextPendingPopup()  // Skip stale warning, show next
                break
            case "recipeActivationFailed":
                recipeActivationFailedDialog.missingProfileTitle =
                    next.params.missingProfileTitle || ""
                recipeActivationFailedDialog.open()
                break
            case "decentMachine":
                openDecentMachineDialog(next.params.serials, next.params.labels)
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

    ThemeViewport {
        viewportWidth: root.width
        viewportHeight: root.height
    }
    // GHCSimulator has TWO independent ways of not being usable, and a guard for one does not
    // cover the other. Both are live configurations, so both tests are required:
    //
    //   1. The TYPE is absent. Registration is inside `#ifdef DECENZA_SIMULATOR`
    //      (src/core/contextsingletons_qml.h), which a production Android/iOS build does not
    //      define — so the name resolves to nothing and reading a member off it throws
    //      `ReferenceError: GHCSimulator is not defined`. Only `typeof` survives this: an
    //      unresolvable identifier makes V4 clear the exception and answer "undefined"
    //      (qtdeclarative/src/qml/jsruntime/qv4runtime.cpp:1750-1758, Runtime::TypeofName::call).
    //   2. The type is registered but the INSTANCE is null — Linux any config, Windows/macOS
    //      Release, Android/iOS Debug. Here the name is a TRUTHY wrapper and only member reads
    //      come back undefined, so `typeof` alone passes and the call throws a TypeError. See
    //      decenzaOptionalSingleton() in src/core/contextsingletons_qml.h for the Qt sources.
    //
    // The member-only guard shipped in 2.0.1 and threw case 1 three times on every tablet
    // launch (two ReferenceErrors plus a dead Connections whose target never resolved). One
    // property, so the two call sites below cannot drift apart again.
    readonly property bool ghcSimulatorLive: typeof GHCSimulator !== "undefined"
                                             && GHCSimulator.mainWindowActivated !== undefined

    // Raise all application windows together when this window is activated.
    onActiveChanged: {
        if (active && root.ghcSimulatorLive) {
            GHCSimulator.mainWindowActivated()
        }
    }

    // Listen for GHC window activation to raise ourselves (simulator mode only).
    Connections {
        target: root.ghcSimulatorLive ? GHCSimulator : null
        function onRaiseMainWindow() {
            root.raise()
        }
    }

    Component.onCompleted: {
        // Restore window position on desktop (not size - keep default to match real device)
        if (Qt.platform.os !== "android" && Qt.platform.os !== "ios") {
            let savedX = Settings.value("mainWindow/x", -1)
            let savedY = Settings.value("mainWindow/y", -1)
            if (savedX >= 0 && savedY >= 0) {
                root.x = savedX
                root.y = savedY
            }
        }

        // Initialize per-page scale settings
        var configVal = Settings.value("ui/configurePageScale", false)
        // Handle both boolean and string values from QSettings
        Theme.configurePageScaleEnabled = (configVal === true || configVal === "true")

        // Keep screen on while app is running (Android only)
        // Prevent system screen timeout — the app has its own screensaver.
        // On Android: FLAG_KEEP_SCREEN_ON. On iOS: idleTimerDisabled = true.
        // Without this on iOS, the system auto-locks during screensaver,
        // suspending the app and breaking BLE comms and smart charging.
        ScreensaverManager.setKeepScreenOn(true)

        // GlobalSupportDialogs opens a prior crash report when the shared
        // service publishes its ready snapshot. Keep the host CrashReporter
        // check below to preserve startup-prompt ordering.

        // Check for first run and show welcome dialog or start scanning
        var firstRunComplete = Settings.value("firstRunComplete", false)
        if (!firstRunComplete) {
            firstRunDialog.open()
        } else {
            // On subsequent launches, still check if storage setup is needed
            // (e.g., after reinstall when QSettings was restored but SAF permission wasn't)
            checkStorageSetup()

            // If a crash dialog is about to open, defer the capability
            // warning until the shared crash prompt has been dismissed.
            // so the two modals don't stack on the same frame.
            if (!(CrashReporter.previousCrashLog && CrashReporter.previousCrashLog.length > 0)) {
                maybeShowLinuxBleCapabilityDialog()
            }

            // One-time prompt after a self-update where BAL blocked the
            // auto-relaunch. Safe to call unconditionally — the function
            // checks shouldShowAutoRelaunchPrompt and does nothing otherwise.
            maybeShowAutoRelaunchPrompt()

            // One-time offer to modernize the idle layout to recipes-first
            // (recipes-idle-layout-upgrade). Safe to call unconditionally —
            // the function checks the offered flag and does nothing otherwise.
            maybeShowRecipesUpgradeDialog()
        }

        // Initialize sleep countdown (fresh app start). The scheduled
        // stay-awake window is derived live from AutoWakeManager, so there
        // is nothing to arm here even if the app started mid-window.
        if (root.autoSleepMinutes > 0) {
            root.sleepCountdownNormal = root.autoSleepMinutes
            root.stayAwakeSuppressionLogged = false
        }

        // (Bean preset auto-matching removed: bags replaced presets, and the
        // legacy preset migration maps the selection to activeBagId in C++.
        // Free-text bean state with no bag is a legitimate "no bag selected"
        // condition now, not something to repair at startup.)

        // Mark app as initialized
        root.appInitialized = true

        // startupGracePeriod is cleared by the phaseChanged handler when the
        // machine first reaches a non-Sleep state (no timer needed)

        // Trigger: app startup — invoke auto-load once after ProfileManager
        // and Settings are ready. Q-callLater defers past initialItem mount
        // so the load sees the fully-initialised profile catalog.
        Qt.callLater(function() {
            ProfileManager.loadAutoLoadProfileIfNeeded()
            MainController.loadAutoLoadRecipeIfNeeded()
            root.autoLoadResetCountdown()
        })
    }

    // Global tap handler for accessibility - announces any Text tapped
    // Only announces text that is NOT inside an interactive element (buttons have their own announcements)
    MouseArea {
        id: accessibilityTapOverlay
        anchors.fill: parent
        z: 10000  // Above everything
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled
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
            var textItem = root.findTextAt(parent, mouse.x, mouse.y)
            if (textItem && textItem.text && !isInsideInteractive(textItem)) {
                AccessibilityManager.announceLabel(AccessibilityManager.cleanForSpeech(textItem.text))
            }
            mouse.accepted = false
        }

        onClicked: function(mouse) { mouse.accepted = false }
        onReleased: function(mouse) { mouse.accepted = false }
    }

    // Floating "Done Editing" button - appears when translation edit mode is active
    Rectangle {
        id: doneEditingButton
        visible: typeof TranslationManager !== "undefined" && TranslationManager !== null && TranslationManager.editModeEnabled
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
                Layout.preferredWidth: untranslatedText.width + Theme.scaled(12)
                Layout.preferredHeight: Theme.scaled(22)
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

    property bool pendingDisconnectNavigation: false
    // Retry a disconnect deferred while the shared stack was transitioning.
    Connections {
        target: pageStack
        function onBusyChanged() {
            if (!pageStack.busy) {
                // Retry deferred disconnect navigation (#575)
                if (root.pendingDisconnectNavigation) {
                    root.pendingDisconnectNavigation = false
                    WebDebugLogger.debug("App", "main", ["Retrying deferred disconnect navigation off the operation page"].map(String).join(" "))
                    navigation.leaveOperationPage()
                }
            }
        }
    }

    property alias pageStack: navigation.stackView
    SharedAppNavigation {
        id: navigation
        anchors.fill: parent
        screensaverActive: root.screensaverActive
        onScreensaverRequested: root.goToScreensaver()
        onWakeRequested: root.goToIdleFromScreensaver()
        onBrewSettingsRequested: root.openBrewSettings()
        onCompletionSuspendRequested: root.suspendCompletionForDialog()
        onCompletionFinishRequested: root.finishCompletion()
        onPageScalePersistenceRequested: (pageName, scale) => Settings.setValue("pageScale/" + pageName, scale)
    }

    // Update per-page scale when navigating between pages
    Connections {
        target: pageStack
        function onCurrentItemChanged() {
            root.updateCurrentPageScale()
            root.announceCurrentPage()
            if (MainController.shotServer && MainController.shotServer.themeEditorOpen)
                pageColorTimer.restart()  // Detect colors after page settles
            // Reset the auto-load countdown: clears off-Idle, full value back on Idle
            root.autoLoadResetCountdown()
            // Every route off the screensaver ends here, not only
            // goToIdleFromScreensaver(): navigation.goToIdle() and the phase handlers leave
            // it directly, and a flag left set keeps auto-sleep stopped and the
            // scale reconnect paused.
            if (root.screensaverActive && pageStack.currentItem
                    && pageStack.currentItem.objectName !== "screensaverPage")
                root.leaveScreensaverState("page changed to " + (pageStack.currentItem.objectName || "unnamed page"))
            root.recordPageStackForCrashReports()
        }
    }

    // Delay color detection slightly so page content is fully loaded. The scan walks the
    // whole page tree (~9 ms per page change on a Galaxy Tab A9+), so it runs only while
    // the web theme editor, its sole reader, is open.
    Timer {
        id: pageColorTimer
        interval: 300
        onTriggered: root.updatePageColors()
    }
    Connections {
        target: MainController.shotServer
        function onThemeEditorOpenChanged() {
            if (MainController.shotServer.themeEditorOpen)
                pageColorTimer.restart()
        }
    }

    // A page under the current one is still alive, so the whole stack is what a crash
    // report needs. DontLoad: a lazily-unloaded page must not be instantiated to name it.
    function recordPageStackForCrashReports() {
        const pages = []
        for (let i = 0; i < pageStack.depth; ++i) {
            const page = pageStack.get(i, StackView.DontLoad)
            pages.push(page ? (page.objectName || "unnamed") : "unloaded")
        }
        CrashReporter.setPageStack(pages)
    }

    // Announce page name for accessibility when page changes
    function announceCurrentPage() {
        if (typeof AccessibilityManager === "undefined" || AccessibilityManager === null || !AccessibilityManager.enabled) return
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
            "dflowEditorPage": TranslationManager.translate("main.pageDFlowEditor", "D-Flow profile editor"),
            "pressureEditorPage": TranslationManager.translate("main.pagePressureEditor", "Pressure profile editor"),
            "flowEditorPage": TranslationManager.translate("main.pageFlowEditor", "Flow profile editor"),
            "shotHistoryPage": TranslationManager.translate("main.pageShotHistory", "Shot history"),
            "descalingPage": TranslationManager.translate("main.pageDescalingScreen", "Descaling screen"),
            "transportPage": TranslationManager.translate("main.pageTransportScreen", "Transport mode screen"),
            "sensorCalibrationPage": TranslationManager.translate("main.pageSensorCalibration", "Sensor calibration screen"),
            "visualizerBrowserPage": TranslationManager.translate("main.pageVisualizerBrowser", "Visualizer browser"),
            "profileImportPage": TranslationManager.translate("main.pageImportProfiles", "Import profiles"),
            "postShotReviewPage": TranslationManager.translate("main.pageShotReview", "Shot review"),
            "beanInfoPage": TranslationManager.translate("main.pageBeanInfo", "Bean info"),
            "equipmentPage": TranslationManager.translate("main.pageEquipment", "Equipment"),
            "shotComparisonPage": TranslationManager.translate("main.pageShotComparison", "Shot comparison")
        }
        var displayName = pageNames[pageName] || pageName
        AccessibilityManager.announce(displayName)
    }

    function updateCurrentPageScale() {
        var pageName = pageStack.currentItem ? (pageStack.currentItem.objectName || "") : ""
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
            Settings.theme.currentPageColors = []
            return
        }

        // Collect all unique color hex values from visible items
        var foundColors = {}
        function walkItem(item) {
            if (!item || !item.visible) return
            // Check 'color' property (Rectangle, Text, etc.)
            if (item.color !== undefined) {
                let c = ("" + item.color).substring(0, 7).toLowerCase()
                if (c.charAt(0) === "#") foundColors[c] = true
            }
            for (let i = 0; i < item.children.length; i++) {
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
        for (let j = 0; j < themeColorNames.length; j++) {
            let name = themeColorNames[j]
            let themeVal = ("" + Theme[name]).substring(0, 7).toLowerCase()
            if (foundColors[themeVal]) {
                matched.push(name)
            }
        }
        Settings.theme.currentPageColors = matched
    }

    // Initialize page scale after pageStack is ready
    Timer {
        id: initPageScaleTimer
        interval: 100
        onTriggered: {
            root.updateCurrentPageScale()
        }
        Component.onCompleted: start()
    }

    // Global error dialog for BLE issues
    DecenzaDialog {
        id: bleErrorDialog
        logName: "Bluetooth error"
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

        // Title is dynamic per error type AND platform. On Android, the BLE
        // discovery agent requires Location permission for any kind of BLE
        // scanning, so "Enable Location" is the right call-to-action even
        // when the error string mentions Bluetooth. On macOS / iOS / Windows
        // / Linux, Bluetooth has its own permission separate from Location,
        // so we should distinguish — e.g. macOS Tahoe occasionally reports
        // MissingPermissionsError from QBluetoothDeviceDiscoveryAgent after
        // sleep/wake, which routes here, and "Enable Location" would be the
        // wrong instruction.
        Tr { id: trEnableLocation;     key: "main.dialog.enableLocation.title";  fallback: "Enable Location";    visible: false }
        Tr { id: trEnableBluetooth;    key: "main.dialog.enableBluetooth.title"; fallback: "Enable Bluetooth";   visible: false }
        // Generic title for non-permission errors. The Location /
        // Bluetooth-permission cases above keep their specific titles.
        Tr { id: trBleErrorGeneric;    key: "main.dialog.bleError.title";        fallback: "Connection Error";   visible: false }
        Tr { id: trErrorPrefix;        key: "common.accessibility.errorPrefix";  fallback: "Error:";             visible: false }

        readonly property bool isAndroidPlatform: Qt.platform.os === "android"

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trErrorPrefix.text + " " + errorMessage, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                text: {
                    if (bleErrorDialog.isLocationError) return trEnableLocation.text
                    if (bleErrorDialog.isBluetoothError) {
                        // On Android, Bluetooth scanning needs Location → keep
                        // the call-to-action pointing at Location. Elsewhere,
                        // Bluetooth has its own permission.
                        return bleErrorDialog.isAndroidPlatform
                               ? trEnableLocation.text
                               : trEnableBluetooth.text
                    }
                    return trBleErrorGeneric.text
                }
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

    // Android 17: a LAN feature started without the local network permission. The features
    // keep running and connect once the user allows it, so this only explains and links there.
    property var localNetworkDeniedFeatures: []

    DecenzaDialog {
        id: localNetworkDeniedDialog
        logName: "Local network denied"
        modal: true
        dim: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        onClosed: {
            // The screensaver closes and re-queues this dialog; keep the list for the re-show.
            if (!root.screensaverActive)
                root.localNetworkDeniedFeatures = []
            root.showNextPendingPopup()
        }

        Tr { id: trLocalNetworkTitle; key: "main.dialog.localNetwork.title"; fallback: "Allow Local Network Access"; visible: false }
        Tr { id: trLocalNetworkBody; key: "main.dialog.localNetwork.body"; fallback: "These features cannot reach devices on your network until you allow \"Nearby devices\" for Decenza in Android Settings:"; visible: false }
        Tr { id: trLocalNetworkWebServer; key: "main.dialog.localNetwork.feature.webServer"; fallback: "Remote access web server"; visible: false }
        Tr { id: trLocalNetworkMqtt; key: "main.dialog.localNetwork.feature.mqtt"; fallback: "MQTT (home automation)"; visible: false }
        Tr { id: trLocalNetworkWifiScale; key: "main.dialog.localNetwork.feature.wifiScale"; fallback: "WiFi scale"; visible: false }
        Tr { id: trLocalNetworkMigration; key: "main.dialog.localNetwork.feature.deviceMigration"; fallback: "Device-to-device transfer"; visible: false }
        Tr { id: trOpenAppSettings; key: "main.button.openAppSettings"; fallback: "Open App Settings"; visible: false }

        function featureLabel(feature) {
            switch (feature) {
            case LocalNetworkAccess.WebServer: return trLocalNetworkWebServer.text
            case LocalNetworkAccess.Mqtt: return trLocalNetworkMqtt.text
            case LocalNetworkAccess.WifiScale: return trLocalNetworkWifiScale.text
            case LocalNetworkAccess.DeviceMigration: return trLocalNetworkMigration.text
            }
            return ""
        }
        readonly property string featureList: root.localNetworkDeniedFeatures
            .map(f => "• " + featureLabel(f)).join("\n")

        onOpened: {
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(trLocalNetworkTitle.text + ". " + trLocalNetworkBody.text + " " + featureList, true)
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                text: trLocalNetworkTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: trLocalNetworkBody.text + "\n" + localNetworkDeniedDialog.featureList
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            AccessibleButton {
                text: trOpenAppSettings.text
                accessibleName: trOpenAppSettings.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    LocalNetworkAccess.openAppSettings()
                    localNetworkDeniedDialog.close()
                }
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: localNetworkDeniedDialog.close()
            }
        }
    }

    Connections {
        target: LocalNetworkAccess
        function onDenied(feature) {
            if (root.localNetworkDeniedFeatures.indexOf(feature) === -1)
                root.localNetworkDeniedFeatures = root.localNetworkDeniedFeatures.concat([feature])
            if (localNetworkDeniedDialog.visible)
                return
            if (root.screensaverActive || root.anyModalDialogVisible()) {
                root.queuePopup("localNetworkDenied")
                return
            }
            localNetworkDeniedDialog.open()
        }
    }

    function showBleError(message, isLocation, isBluetooth) {
        if (root.screensaverActive) {
            root.queuePopup("bleError", {errorMessage: message, isLocationError: isLocation, isBluetoothError: isBluetooth})
            return
        }
        bleErrorDialog.isLocationError = isLocation
        bleErrorDialog.isBluetoothError = isBluetooth
        bleErrorDialog.errorMessage = message
        bleErrorDialog.open()
    }

    Connections {
        target: BLEManager
        function onErrorOccurred(error) {
            root.showBleError(error, false, false)
        }
        function onPermissionDenied(location, message) {
            root.showBleError(message, location, !location)
        }
        function onFlowScaleFallback() {
            // Only show "No Scale Found" if user has a saved scale.
            // Users without a saved scale expect FlowScale — no need to nag them.
            if (Settings.primaryScaleAddress === "") return
            if (!Settings.showScaleDialogs) return
            // Don't nag if a USB scale is connected — it satisfies the requirement (not available on iOS)
            if (Qt.platform.os !== "ios" && UsbScaleManager.scaleConnected) return
            // This fires only after a (re)connect attempt has actually given up,
            // so it's the right moment to notify. A mid-session drop
            // (scaleDropPending) shows the "Scale Disconnected" notice; a
            // never-connected startup shows "No scale detected".
            var popupId = root.scaleDropPending ? "scaleDisconnected" : "flowScale"
            var dialog = root.scaleDropPending ? scaleDisconnectedDialog : flowScaleDialog
            if (root.screensaverActive) { root.queuePopup(popupId); return }
            if (AppShell.scaleDialogDeferred) { root.queuePopup(popupId); return }
            dialog.open()
        }
        function onScaleDisconnected() {
            // Don't nag on the disconnect itself — a transient drop (e.g. the
            // WiFi WebSocket dropping on screensaver wake) auto-reconnects within
            // seconds. Just record it; onFlowScaleFallback() surfaces the notice
            // only if the reconnect actually gives up, and onScaleConnected()
            // clears this + dismisses the notice once the scale is back.
            root.scaleDropPending = true
        }
        function onScaleConnected() {
            // Scale (re)connected — clear the pending-drop state and dismiss the
            // open scale-disconnect / no-scale notice so a recovered drop leaves no
            // trace. Queued-but-unshown popups are purged by the ScaleDevice
            // reconnect handler ("Discard stale scale popups…" below), so we don't
            // duplicate removeQueuedScalePopups() here.
            root.scaleDropPending = false
            if (scaleDisconnectedDialog.opened) scaleDisconnectedDialog.close()
            if (flowScaleDialog.opened) flowScaleDialog.close()
        }
        function onDisconnectScaleRequested() {
            // A deliberate scale switch/forget ends the mid-session-drop context, so
            // the next FlowScale fallback isn't mislabeled "Scale Disconnected" for a
            // freshly-selected (never-connected) scale. disconnectScaleRequested
            // fires from connectToScale / clearSavedScale / setDisabled.
            root.scaleDropPending = false
        }
    }

    // FlowScale fallback dialog (no scale found at startup)
    DecenzaDialog {
        id: flowScaleDialog
        logName: "Using FlowScale"
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
        Tr { id: trNoScaleFoundAnnounce; key: "main.dialog.noScaleFound.announce"; fallback: "No scale detected. Using estimated weight from flow measurement."; visible: false }

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
                fallback: "No scale was detected.\n\nUsing estimated weight from DE1 flow measurement instead.\n\nYou can search for your scale in Settings → Connections."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    flowScaleDialog.close()
                    BLEManager.requestScaleReconnectRampRestart(
                        "No-scale notice dismissed")
                }
            }
        }
    }

    // Scale disconnected dialog
    DecenzaDialog {
        id: scaleDisconnectedDialog
        logName: "Scale disconnected"
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
                fallback: "Your scale has disconnected.\n\nUsing estimated weight from DE1 flow measurement until the scale reconnects.\n\nCheck that your scale is powered on and in range."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            AccessibleButton {
                text: trCommonOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    scaleDisconnectedDialog.close()
                    BLEManager.requestScaleReconnectRampRestart(
                        "Scale-disconnected notice dismissed")
                }
            }
        }
    }

    // Opt-in confirmation for Sleep's long-press quit (its "Ask before quitting" option).
    DecenzaDialog {
        id: quitConfirmDialog
        logName: "Quit confirmation"
        // anyModalDialogVisible() lists this dialog. If Quit handed over to the flash warning,
        // that warning drains the queue when it closes instead.
        onClosed: if (!firmwareFlashExitDialog.visible) root.showNextPendingPopup()
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        closePolicy: Dialog.CloseOnEscape

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        Tr { id: trQuitConfirmTitle; key: "main.dialog.quitConfirm.title"; fallback: "Quit Decenza?"; visible: false }

        onOpened: {
            // Cancel is the safe default, for a stray tap and for a screen reader alike.
            quitConfirmCancelButton.forceActiveFocus()
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(trQuitConfirmTitle.text, true)
        }

        contentItem: Column {
            spacing: Theme.spacingMedium

            Text {
                text: trQuitConfirmTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            FocusScope {
                anchors.horizontalCenter: parent.horizontalCenter
                width: quitConfirmButtons.width
                height: quitConfirmButtons.height

                Row {
                    id: quitConfirmButtons
                    spacing: Theme.spacingMedium

                    AccessibleButton {
                        id: quitConfirmCancelButton
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("main.dialog.quitConfirm.cancelAccessible", "Keep Decenza open")
                        activeFocusOnTab: true
                        KeyNavigation.tab: quitConfirmQuitButton
                        KeyNavigation.backtab: quitConfirmQuitButton
                        onClicked: quitConfirmDialog.close()
                    }

                    AccessibleButton {
                        id: quitConfirmQuitButton
                        text: TranslationManager.translate("main.dialog.quitConfirm.quit", "Quit")
                        accessibleName: TranslationManager.translate("main.dialog.quitConfirm.quitAccessible", "Quit Decenza")
                        destructive: true
                        activeFocusOnTab: true
                        KeyNavigation.tab: quitConfirmCancelButton
                        KeyNavigation.backtab: quitConfirmCancelButton
                        onClicked: {
                            quitConfirmDialog.close()
                            if (MainController.firmwareUpdater && MainController.firmwareUpdater.isFlashing)
                                firmwareFlashExitDialog.open()
                            else
                                Qt.quit()
                        }
                    }
                }
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

    DecenzaDialog {
        id: noScaleAbortDialog
        logName: "No scale, shot aborted"
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
                // A new key, so translations of the old path (a Bluetooth tab and a "Forget Scale"
                // button, both since renamed) stop rendering instead of pointing nowhere.
                key: "main.dialog.noScale.messageConnections"
                fallback: "Your saved scale is not connected.\n\nPlease turn on your scale and wait for it to connect before starting a shot.\n\nTo use the app without a scale, go to Settings \u2192 Connections and tap \u0022Forget\u0022 next to the scale."
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
    DecenzaDialog {
        id: chargingMismatchDialog
        logName: "Charging mismatch"
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
            if (root.screensaverActive) {
                root.queuePopup("chargingMismatch")
                return
            }
            chargingMismatchDialog.open()
        }

        function onChargingMismatchResolved() {
            chargingMismatchDialog.close()
            // Remove any queued instance so it doesn't appear after screensaver wake
            // when the condition has already cleared.
            root.pendingPopups = root.pendingPopups.filter(function(p) { return p.id !== "chargingMismatch" })
        }
    }

    // Water tank refill dialog
    DecenzaDialog {
        id: refillDialog
        logName: "Refill water tank"
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
            if (MachineState.phase === MachineState.Phase.Refill) {
                if (root.screensaverActive) { root.queuePopup("refill"); return }
                refillDialog.open()
            } else if (refillDialog.opened) {
                refillDialog.close()
            }
        }
    }

    // Standby switch (front AC) warning. Full-screen and tap-anywhere-dismiss, matching
    // de1app's "Push the switch on" page: while it holds, the machine has no AC power and
    // cannot do anything at all. Driven by MachineState.standbySwitchOpen, which is already
    // false whenever disconnected or on old firmware — see machinestate.cpp.
    DecenzaDialog {
        id: standbySwitchDialog
        logName: "Standby switch"
        modal: true
        dim: true
        x: 0
        y: 0
        width: Overlay.overlay ? Overlay.overlay.width : Theme.scaled(800)
        height: Overlay.overlay ? Overlay.overlay.height : Theme.scaled(480)
        padding: 0
        closePolicy: Dialog.NoAutoClose
        onClosed: root.showNextPendingPopup()

        background: Rectangle {
            color: Theme.surfaceColor
        }

        Tr { id: trStandbySwitchAnnounce; key: "main.dialog.standbySwitch.announce"; fallback: "Warning: push the switch on"; visible: false }

        onOpened: {
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trStandbySwitchAnnounce.text, true)
            }
        }

        contentItem: AccessibleMouseArea {
            id: standbySwitchTapArea

            Tr { id: trStandbySwitchMessage; key: "main.dialog.standbySwitch.message"; fallback: "Push the switch on"; visible: false }
            Tr { id: trDismissStandbySwitch; key: "main.accessibility.dismissStandbySwitchWarning"; fallback: "Dismiss standby switch warning"; visible: false }

            accessibleName: trDismissStandbySwitch.text
            onAccessibleClicked: standbySwitchDialog.close()

            Text {
                anchors.centerIn: parent
                text: trStandbySwitchMessage.text
                font: Theme.titleFont
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                width: parent.width * 0.8
            }
        }
    }

    // Show/hide the standby switch warning based on machine state. Reacts only to the
    // CHANGE, not to standbySwitchOpen staying true — a tap-dismiss during one occurrence
    // must not be immediately reopened by the same still-true condition; a fresh
    // false->true transition (the condition recurring) opens it again.
    Connections {
        target: MachineState
        function onStandbySwitchOpenChanged() {
            if (MachineState.standbySwitchOpen) {
                if (root.screensaverActive) { root.queuePopup("standbySwitch"); return }
                standbySwitchDialog.open()
            } else if (standbySwitchDialog.opened) {
                standbySwitchDialog.close()
            }
        }
    }


    // Update notification dialog
    DecenzaDialog {
        id: updateDialog
        logName: "App update"
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
                        navigation.goToSettings("about")  // About tab has the update controls

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
            if (root.screensaverActive) { root.queuePopup("update"); return }
            updateDialog.open()
        }
    }

    // Completion presentation/timing is shared with the Wasm shell. Navigation
    // after completion still belongs to this shell's StackView.
    readonly property bool completionPending: completionOverlay.pending

    // Translatable completion messages
    Tr { id: trSteamComplete; key: "main.completion.steam"; fallback: "Steam Complete"; visible: false }
    Tr { id: trHotWaterComplete; key: "main.completion.hotwater"; fallback: "Hot Water Complete"; visible: false }
    Tr { id: trFlushComplete; key: "main.completion.flush"; fallback: "Flush Complete"; visible: false }

    OperationCompletionOverlay {
        id: completionOverlay
        onFinished: navigation.leaveOperationPage()
    }

    // When a post-session dialog (e.g. SteamHealthTracker scaleBuildupWarning)
    // takes over from the completion overlay, the shared overlay holds its timer.
    // The page's Dialog.onClosed drives navigation explicitly.
    // A new operation cancels the held completion; closing the post-session
    // dialog finishes it and leaves the operation page.
    function showCompletion(message, type) {
        completionOverlay.showCompletion(message, type)
    }

    // Hide the completion overlay and mark the timer's onTriggered as inert.
    // Used when an operation page needs to hold the user on the page while a
    // post-session modal dialog is up. The page is responsible for calling
    // finishCompletion() from the dialog's onClosed.
    function suspendCompletionForDialog() {
        completionOverlay.suspend()
    }

    function finishCompletion() {
        completionOverlay.finish()
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

        visible: root.sawBypassedVisible || sawBypassedFadeOut.running
        opacity: root.sawBypassedVisible ? 1 : 0
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
            enabled: root.sawBypassedVisible
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
        onTriggered: root.sawBypassedVisible = false
    }

    EspressoCompletionCoordinator {
        id: espressoCompletion
        isCalibrationShot: navigation.isCalibrationShot()
        postShotReviewTimeout: Number(Settings.value("postShotReviewTimeout", 31))
        onLeaveOperationPageRequested: navigation.leaveOperationPage()
        onReviewRequested: shotId => navigation.goToShotMetadata(shotId)
        onShotStarted: {
            root.sawBypassedVisible = false
            sawBypassedTimer.stop()
        }
    }

    Connections {
        target: MachineState
        function onSawBypassed() {
            root.sawBypassedVisible = true
            sawBypassedPopIn.start()
            sawBypassedTimer.start()
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null) {
                AccessibilityManager.announce(sawBypassedText.text, true)
            }
        }
    }

    GlobalSupportDialogs {
        id: globalSupportDialogs
        onCrashPromptFinished: {
            root.maybeShowLinuxBleCapabilityDialog()
            root.maybeShowAutoRelaunchPrompt()
        }
    }
    // A recipe that could not be activated. Same reasoning as the refusal
    // dialog above: activation is reached from the recipe pills, the MCP
    // recipe_activate tool, the web activate route and auto-load, so the
    // failure has to be visible whichever surface asked.
    //
    // Before this, the only report was a pill that lit and reverted —
    // recipeActivated(id, false) was emitted and nothing in QML handled it.
    Tr {
        id: trRecipeActivationFailedTitle
        key: "recipes.activationFailed.title"
        fallback: "Can't use this recipe"
        visible: false
    }
    Tr {
        id: trRecipeActivationFailedProfile
        key: "recipes.activationFailed.missingProfile"
        fallback: "Its profile \"%1\" isn't installed. Edit the recipe and pick another profile, or import that one again."
        visible: false
    }
    Tr {
        id: trRecipeActivationFailedOther
        key: "recipes.activationFailed.other"
        fallback: "The recipe could not be loaded."
        visible: false
    }
    Tr {
        id: trRecipeActivationFailedOk
        key: "common.button.ok"
        fallback: "OK"
        visible: false
    }
    DecenzaDialog {
        id: recipeActivationFailedDialog
        logName: "Recipe activation failed"
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        padding: Theme.dialogPadding
        // Let a popup that queued behind this one through, like every other
        // dialog here. Without it this dialog swallows the queue.
        onClosed: root.showNextPendingPopup()

        property string missingProfileTitle: ""
        readonly property string bodyText:
            missingProfileTitle !== ""
            ? trRecipeActivationFailedProfile.text.arg(missingProfileTitle)
            : trRecipeActivationFailedOther.text

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        onOpened: {
            recipeActivationFailedOkButton.forceActiveFocus()
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(
                    trRecipeActivationFailedTitle.text + ". "
                    + recipeActivationFailedDialog.bodyText, true)
        }

        contentItem: Column {
            spacing: Theme.spacingLarge

            Text {
                text: trRecipeActivationFailedTitle.text
                font: Theme.subtitleFont
                color: Theme.warningColor
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                // Announced together in onOpened; don't re-read on swipe.
                Accessible.ignored: true
            }

            Text {
                text: recipeActivationFailedDialog.bodyText
                font: Theme.bodyFont
                color: Theme.textColor
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Accessible.ignored: true
            }

            AccessibleButton {
                id: recipeActivationFailedOkButton
                text: trRecipeActivationFailedOk.text
                accessibleName: trCommonDismissDialog.text
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: recipeActivationFailedDialog.close()
            }
        }
    }
    Connections {
        target: MainController
        function onRecipeActivationFailed(recipeId, recipeName, missingProfileTitle) {
            // Not queued for after the screensaver, unlike the update and
            // refill prompts. Those stay true while asleep; this one reports a
            // single failed attempt, and surfacing it minutes later next to
            // whatever the user is doing then would be noise.
            //
            // Reaching here with the screensaver up means a REMOTE caller (MCP
            // or web) — a pill tap needs the screen awake — and both of those
            // report the failure through their own response, so nothing is lost
            // for the requester. For the missing-profile case the recipe also
            // keeps its marking in the list. The one gap is a recipe row that
            // vanished concurrently: no durable marker exists for that, so a
            // local user would never learn of it. Accepted — it needs a delete
            // racing an in-flight remote activation.
            if (root.screensaverActive)
                return
            // Queue behind a dialog that is already up rather than opening on
            // top of it. Draining the queue in onClosed is not enough on its
            // own — that only helps popups already waiting, and this one would
            // still stack over whatever the user is currently reading (it was
            // landing over "No Scale Found"). Queued, it opens when that one is
            // dismissed, via the same showNextPendingPopup path.
            if (root.anyModalDialogVisible()) {
                root.queuePopup("recipeActivationFailed",
                                {missingProfileTitle: missingProfileTitle})
                return
            }
            recipeActivationFailedDialog.missingProfileTitle = missingProfileTitle
            if (!recipeActivationFailedDialog.visible)
                recipeActivationFailedDialog.open()
        }
    }

    // A DE1 that reports no serial number, in an account with several DE1s: which
    // one its shots are filed under. DecentAccount asks once per app run and sign-in.
    DecentMachineChooser {
        id: decentMachineDialog
        logName: "Decent account machine"
        onSerialChosen: function(serial) { ShotUploadService.execute("chooseDecentMachine", { serial: serial }) }
        onClosed: root.showNextPendingPopup()
    }
    function openDecentMachineDialog(serials, labels) {
        decentMachineDialog.openWith(serials, labels)
    }
    Connections {
        target: MainController.decentAccount
        function onMachineChoiceNeeded(serials, labels) {
            if (root.screensaverActive || root.anyModalDialogVisible()) {
                root.queuePopup("decentMachine", {serials: serials, labels: labels})
                return
            }
            root.openDecentMachineDialog(serials, labels)
        }
    }

    // Linux BLE capability warning — shown on Linux when the binary lacks
    // CAP_NET_ADMIN. Without it, BlueZ can't determine whether a BLE address
    // is random or public and rejects connections to the DE1 (which uses a
    // random static address) with UnknownRemoteDeviceError. The capability
    // is granted via `sudo setcap` and is frequently cleared by OS updates.
    DecenzaDialog {
        id: linuxBleCapabilityDialog
        logName: "Linux Bluetooth capability"
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
                    font.family: Theme.monoFontFamily
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

    DecenzaDialog {
        id: linuxBleBluezCacheDialog
        logName: "Linux BlueZ cache"
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
                    font.family: Theme.monoFontFamily
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
    DecenzaDialog {
        id: firstRunDialog
        logName: "First run"
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
                    // Fresh installs already get the recipes-first default
                    // layout — never show the upgrade offer to them.
                    Settings.network.recipesUpgradeOffered = true
                    firstRunDialog.close()
                    root.checkStorageSetup()
                    root.maybeShowLinuxBleCapabilityDialog()
                }
            }
        }
    }

    // Storage setup dialog (Android 11+ - request MANAGE_EXTERNAL_STORAGE permission)
    DecenzaDialog {
        id: storageSetupDialog
        logName: "Storage setup"
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
                        root.startBluetoothScan()
                        root.maybeShowAutoRelaunchPrompt()
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
            if (state !== Qt.ApplicationActive) return
            if (storageSetupDialog.opened) {
                // User returned from settings - check if permission was granted
                ProfileStorage.checkPermissionAndNotify()
            }
            if (MainController.updateChecker.autoRelaunchSupported) {
                // User may have just granted/revoked SAW in Android Settings.
                // Refresh so subsequent calls to shouldShowAutoRelaunchPrompt
                // see the current state. The prompt dialog does not auto-close
                // on permission grant — the dialog's "Open Settings" button
                // closes it explicitly before sending the user out, so on
                // return the dialog is already gone.
                MainController.updateChecker.refreshAutoRelaunchPermission()
            }
        }
    }

    // One-time post-update prompt: if the receiver fired on this startup but
    // SAW wasn't granted (BAL blocked the auto-relaunch), offer to enable it
    // so next week's update reopens automatically. Same pattern as the GPS /
    // storage permission prompts: surfaced at the teachable moment, no
    // permanent in-app UI. Dismissed permanently after either button.
    DecenzaDialog {
        id: autoRelaunchPromptDialog
        logName: "Auto relaunch prompt"
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

        Tr { id: trAutoRelaunchTitle; key: "main.dialog.autorelaunch.title"; fallback: "Reopen Decenza automatically after updates?"; visible: false }
        Tr { id: trAutoRelaunchMessage; key: "main.dialog.autorelaunch.message"; fallback: "Decenza was updated, but Android didn’t bring the app back to the foreground. Grant the \"Display over other apps\" permission (Samsung calls this \"Appear on top\") and future updates will reopen Decenza automatically."; visible: false }

        onOpened: {
            // Park focus on the safe default ("Not now") so screen-reader
            // navigation lands on dismiss rather than the action button.
            // ACCESSIBILITY.md Rule 3: Component.onCompleted fires before
            // the dialog is open and is unreliable for focus.
            autoRelaunchNotNowButton.forceActiveFocus()
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(
                    trAutoRelaunchTitle.text + ". " + trAutoRelaunchMessage.text, true)
            }
        }

        contentItem: Column {
            spacing: Theme.spacingLarge

            Text {
                text: trAutoRelaunchTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: trAutoRelaunchMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            Row {
                spacing: Theme.spacingLarge
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    id: autoRelaunchNotNowButton
                    Tr { id: trAutoRelaunchNotNow; key: "common.button.notnow"; fallback: "Not now"; visible: false }
                    Tr { id: trAutoRelaunchDismiss; key: "main.accessibility.dismissAutoRelaunch"; fallback: "Dismiss auto-reopen prompt"; visible: false }
                    text: trAutoRelaunchNotNow.text
                    accessibleName: trAutoRelaunchDismiss.text
                    onClicked: {
                        MainController.updateChecker.dismissAutoRelaunchPrompt()
                        autoRelaunchPromptDialog.close()
                    }
                }

                AccessibleButton {
                    Tr { id: trAutoRelaunchOpen; key: "main.dialog.autorelaunch.openSettings"; fallback: "Open Settings"; visible: false }
                    Tr { id: trAutoRelaunchOpenA11y; key: "main.accessibility.openAndroidSettings"; fallback: "Open Android Settings"; visible: false }
                    text: trAutoRelaunchOpen.text
                    accessibleName: trAutoRelaunchOpenA11y.text
                    primary: true
                    onClicked: {
                        MainController.updateChecker.dismissAutoRelaunchPrompt()
                        MainController.updateChecker.requestAutoRelaunchPermission()
                        autoRelaunchPromptDialog.close()
                    }
                }
            }
        }
    }

    // One-time recipes-first layout upgrade offer (recipes-idle-layout-upgrade).
    // Shown once per install to existing users (maybeShowRecipesUpgradeDialog);
    // fresh installs already start on the new default and never see it. Accept
    // applies the layout transform (+ a starter recipe when the user has none)
    // via MainController; decline/dismiss just records that the offer was
    // answered. Dismiss (escape) counts as decline (recipes-idle-layout-upgrade
    // design.md decision 8) — it must be dismissible, per ACCESSIBILITY.md.
    DecenzaDialog {
        id: recipesUpgradeDialog
        logName: "Recipes upgrade"
        modal: true
        dim: true
        anchors.centerIn: parent
        width: Theme.dialogWidth + 2 * padding
        closePolicy: Dialog.CloseOnEscape
        padding: Theme.dialogPadding

        property bool willCreateStarterRecipe: false
        property bool hasMilkChoice: false

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.primaryContrastColor
        }

        Tr { id: trUpgradeTitle; key: "main.dialog.recipesUpgrade.title"; fallback: "Try the recipes-first layout?"; visible: false }
        Tr {
            id: trUpgradeMessage
            key: "main.dialog.recipesUpgrade.message"
            fallback: "Decenza now leads with Recipes on the idle screen. This one-time update:\n\n• Puts Recipes front and center\n• Moves Profiles to the bottom bar — it still works exactly as before\n• Removes the Auto-Favorites button (still reachable via the layout editor)\n\nEverything else about your layout stays exactly as you left it."
            visible: false
        }
        Tr { id: trUpgradeStarterLine; key: "main.dialog.recipesUpgrade.starterLine"; fallback: "We'll also create a starter recipe from your last shot:"; visible: false }
        Tr { id: trUpgradeEspresso; key: "main.dialog.recipesUpgrade.espresso"; fallback: "Espresso"; visible: false }
        Tr { id: trUpgradeMilk; key: "main.dialog.recipesUpgrade.milk"; fallback: "Milk drink"; visible: false }
        Tr { id: trUpgradeAccept; key: "main.dialog.recipesUpgrade.accept"; fallback: "Upgrade layout"; visible: false }
        Tr { id: trUpgradeDecline; key: "main.dialog.recipesUpgrade.decline"; fallback: "Keep my layout"; visible: false }
        Tr { id: trStarterEspressoName; key: "upgrade.recipe.espressoName"; fallback: "My espresso"; visible: false }
        Tr { id: trStarterMilkName; key: "upgrade.recipe.milkName"; fallback: "My milk drink"; visible: false }
        Tr { id: trUpgradeAcceptA11y; key: "main.accessibility.acceptRecipesUpgrade"; fallback: "Upgrade to the recipes-first layout"; visible: false }
        Tr { id: trUpgradeDeclineA11y; key: "main.accessibility.declineRecipesUpgrade"; fallback: "Keep the current layout"; visible: false }
        Tr { id: trUpgradeEspressoA11y; key: "main.accessibility.recipesUpgradeEspresso"; fallback: "Starter recipe: Espresso"; visible: false }
        Tr { id: trUpgradeMilkA11y; key: "main.accessibility.recipesUpgradeMilk"; fallback: "Starter recipe: Milk drink"; visible: false }

        onOpened: {
            // Park focus on the safe default (Accept, but "Keep my layout" is
            // equally one tab away) — mirrors autoRelaunchPromptDialog.
            // ACCESSIBILITY.md Rule 3: Component.onCompleted is unreliable
            // for focus, onOpened is the correct hook.
            recipesUpgradeAcceptButton.forceActiveFocus()
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trUpgradeTitle.text + ". " + trUpgradeMessage.text, true)
            }
        }

        onClosed: {
            // Any close (button or escape) means the offer was answered —
            // the accept path already sets this itself, so this is a
            // harmless no-op there and the decline path for everything else.
            Settings.network.recipesUpgradeOffered = true
        }

        contentItem: Column {
            spacing: Theme.spacingLarge
            width: parent.width

            Text {
                text: trUpgradeTitle.text
                font: Theme.subtitleFont
                color: Theme.textColor
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: trUpgradeMessage.text
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
                color: Theme.textColor
            }

            Column {
                visible: recipesUpgradeDialog.willCreateStarterRecipe
                width: parent.width
                spacing: Theme.spacingMedium

                Text {
                    text: trUpgradeStarterLine.text
                    wrapMode: Text.Wrap
                    width: parent.width
                    font: Theme.bodyFont
                    color: Theme.textColor
                }

                Row {
                    spacing: Theme.spacingMedium
                    anchors.horizontalCenter: parent.horizontalCenter

                    AccessibleButton {
                        text: trUpgradeEspresso.text
                        accessibleName: trUpgradeEspressoA11y.text
                            + (!recipesUpgradeDialog.hasMilkChoice
                               ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        primary: !recipesUpgradeDialog.hasMilkChoice
                        onClicked: recipesUpgradeDialog.hasMilkChoice = false
                    }

                    AccessibleButton {
                        text: trUpgradeMilk.text
                        accessibleName: trUpgradeMilkA11y.text
                            + (recipesUpgradeDialog.hasMilkChoice
                               ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        primary: recipesUpgradeDialog.hasMilkChoice
                        onClicked: recipesUpgradeDialog.hasMilkChoice = true
                    }
                }
            }

            Row {
                spacing: Theme.spacingLarge
                anchors.horizontalCenter: parent.horizontalCenter

                AccessibleButton {
                    text: trUpgradeDecline.text
                    accessibleName: trUpgradeDeclineA11y.text
                    onClicked: recipesUpgradeDialog.close()
                }

                AccessibleButton {
                    id: recipesUpgradeAcceptButton
                    text: trUpgradeAccept.text
                    accessibleName: trUpgradeAcceptA11y.text
                    primary: true
                    onClicked: {
                        var starterName = recipesUpgradeDialog.hasMilkChoice
                            ? trStarterMilkName.text : trStarterEspressoName.text
                        MainController.acceptRecipesFirstUpgrade(starterName, recipesUpgradeDialog.hasMilkChoice)
                        recipesUpgradeDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: MainController
        function onRecipesUpgradeOfferReady(willCreateStarterRecipe, milkPreselected) {
            // Don't stack on top of another modal that might still be
            // resolving (crash report, storage setup, auto-relaunch prompt).
            if (CrashReporter.previousCrashLog && CrashReporter.previousCrashLog.length > 0) return
            if (storageSetupDialog.opened || autoRelaunchPromptDialog.opened) return
            recipesUpgradeDialog.willCreateStarterRecipe = willCreateStarterRecipe
            recipesUpgradeDialog.hasMilkChoice = milkPreselected
            recipesUpgradeDialog.open()
        }
        function onRecipesUpgradeApplied(recipeName, starterRecipeFailed) {
            var upgradeMessage = starterRecipeFailed
                ? trRecipesUpgradeToastRecipeFailed.text
                : (recipeName.length > 0
                    ? trRecipesUpgradeToastWithRecipe.text.arg(recipeName)
                    : trRecipesUpgradeToastLayoutOnly.text)
            recipesUpgradeToast.show(upgradeMessage)
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(upgradeMessage, starterRecipeFailed)
            }
        }
    }

    Tr {
        id: trRecipesUpgradeToastWithRecipe
        key: "main.toast.recipesUpgradeWithRecipe"
        fallback: "Layout updated · Created '%1'"
        visible: false
    }
    Tr { id: trRecipesUpgradeToastLayoutOnly; key: "main.toast.recipesUpgradeLayoutOnly"; fallback: "Layout updated"; visible: false }
    // The layout transform always succeeds by this point — only the
    // starter-recipe creation can fail (RecipeStorage write error). Distinct
    // from trRecipesUpgradeToastLayoutOnly so a real failure isn't mistaken
    // for the normal "no starter recipe was eligible" case.
    Tr {
        id: trRecipesUpgradeToastRecipeFailed
        key: "main.toast.recipesUpgradeRecipeFailed"
        fallback: "Layout updated · Couldn't create starter recipe"
        visible: false
    }
    StatusToast { id: recipesUpgradeToast }

    // Shown when the DE1 enters Steam with the built-in "Heater off" entry
    // selected. That entry carries no values, so the live settings — the last
    // real pitcher's — are what run. Explaining beats silently substituting.
    Tr {
        id: trSteamHeaterOffSteaming
        key: "steam.toast.heaterOffSteaming"
        fallback: "Heater off is selected — steaming with the last used pitcher settings"
        visible: false
    }
    StatusToast { id: steamHeaterOffToast }

    // Auto flow calibration changed the multiplier the machine pours at. The
    // signal existed and nothing listened to it, so the one setting that changes
    // how much water a profile actually delivers moved silently — and since the
    // DE1 servos its CALIBRATED flow, a shift here shifts shot times at the same
    // grind. A courtesy toast, not a dialog: it is a correction working as
    // designed, and the number is in Settings if anyone wants it.
    Tr {
        id: trFlowCalUpdated
        key: "calibration.toast.flowCalUpdated"
        fallback: "Flow calibration for %1 updated: %2 → %3"
        visible: false
    }
    StatusToast { id: flowCalUpdatedToast }
    Connections {
        target: MainController
        function onFlowCalibrationAutoUpdated(profileTitle, oldValue, newValue) {
            flowCalUpdatedToast.show(trFlowCalUpdated.text
                .replace("%1", profileTitle)
                .replace("%2", oldValue.toFixed(3))
                .replace("%3", newValue.toFixed(3)))
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(flowCalUpdatedToast.message)
        }
    }

    // Recipe relink toast (recipe-bag-lifecycle): every automatic recipe
    // move — roll-on-finish or wake-on-restock — is silent (no dialog, no
    // setting) but announced with a courtesy toast naming the count and the
    // target bag. Cards and pills always show the current bag afterwards.
    Connections {
        target: MainController.recipeStorage
        function onRecipesRelinked(movedRecipeIds, targetBagId, targetBagName) {
            // A bag with no roaster/coffee text would leave a dangling
            // "moved to " — fall back to a generic phrase.
            var bagName = targetBagName !== "" ? targetBagName : trRecipesRelinkBagFallback.text
            var relinkMessage = movedRecipeIds.length === 1
                ? trRecipesRelinkOne.text.arg(bagName)
                : trRecipesRelinkMany.text.arg(movedRecipeIds.length).arg(bagName)
            recipesRelinkToast.show(relinkMessage)
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(relinkMessage)
        }
    }
    Tr {
        id: trRecipesRelinkOne
        key: "main.toast.recipeRelinkedOne"
        fallback: "1 recipe moved to %1"
        visible: false
    }
    Tr {
        id: trRecipesRelinkMany
        key: "main.toast.recipeRelinkedMany"
        fallback: "%1 recipes moved to %2"
        visible: false
    }
    Tr {
        id: trRecipesRelinkBagFallback
        key: "main.toast.recipeRelinkedBagFallback"
        fallback: "another bag"
        visible: false
    }
    StatusToast { id: recipesRelinkToast }

    function maybeShowAutoRelaunchPrompt() {
        if (!MainController.updateChecker.shouldShowAutoRelaunchPrompt) return
        // Defer until any pre-empting modals resolve themselves, so we don't
        // stack on top of the crash report or storage setup dialog. Each of
        // those dialogs calls maybeShowAutoRelaunchPrompt() in its close
        // handler, so the prompt eventually shows.
        if (CrashReporter.previousCrashLog && CrashReporter.previousCrashLog.length > 0) return
        if (storageSetupDialog.opened) return
        autoRelaunchPromptDialog.open()
    }

    // One-time recipes-first layout upgrade offer (recipes-idle-layout-upgrade).
    // "Existing user" = the offered flag hasn't been set yet (fresh installs
    // set it the moment first-run completes, so they never reach here). The
    // eligibility check runs in the background; the dialog opens once
    // recipesUpgradeOfferReady arrives (see the Connections block below).
    function maybeShowRecipesUpgradeDialog() {
        if (Settings.network.recipesUpgradeOffered) return
        MainController.checkRecipesUpgradeEligibility()
    }

    // Handle permission result
    Connections {
        target: ProfileStorage
        function onFolderSelected(success) {
            if (storageSetupDialog.opened) {
                storageSetupDialog.close()
                root.checkFirstRunRestore()
                root.maybeShowAutoRelaunchPrompt()
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
                root.startBluetoothScan();
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
    property int previousPhase: MachineState.Phase.Disconnected

    // Connection state handler - auto navigate based on machine state
    Connections {
        target: MachineState

        function onPhaseChanged() {
            let phase = MachineState.phase
            let currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""
            let wasIdle = (root.previousPhase === MachineState.Phase.Idle ||
                          root.previousPhase === MachineState.Phase.Ready ||
                          root.previousPhase === MachineState.Phase.Heating)

            // Suppress Sleep reactions until DE1 has been awake at least once.
            // On connect, MachineState sees default Sleep state before real state arrives.
            // Reset on disconnect so reconnections are also protected.
            if (phase === MachineState.Phase.Disconnected) {
                root.startupGracePeriod = true
                // An operation page cannot run without the machine (#575)
                if (OperationNavigationPolicy.isOperationPage(currentPage)) {
                    WebDebugLogger.debug("App", "main", ["Disconnected while on operation page (" + currentPage + ") - leaving it"].map(String).join(" "))
                    if (!pageStack.busy) {
                        navigation.leaveOperationPage()
                    } else {
                        root.pendingDisconnectNavigation = true
                    }
                }
            } else {
                // Clear deferred disconnect navigation on reconnect — machine is back,
                // don't navigate away from whatever page the user is on now.
                if (root.pendingDisconnectNavigation) root.pendingDisconnectNavigation = false
                if (root.startupGracePeriod &&
                       phase !== MachineState.Phase.Sleep) {
                    root.startupGracePeriod = false
                }
            }

            // Apply settings when entering operations (to handle GHC-initiated starts)
            if (phase === MachineState.Phase.Steaming && wasIdle) {
                // Note: the DE1Device.onStateChanged handler above already called
                // startSteamHeating when state reached Steam, which must happen before
                // phase can reach Steaming — so we don't re-call it here. The dedup
                // would elide the redundant BLE write, but the reason tags in the log
                // showed it firing on every steam session for no benefit.
            } else if (phase === MachineState.Phase.HotWater && wasIdle) {
                MainController.applyHotWaterSettings()
                WebDebugLogger.debug("DE1", "main", ["Applied hot water settings on phase change"].map(String).join(" "))
            } else if (phase === MachineState.Phase.Flushing && wasIdle) {
                MainController.applyFlushSettings()
                WebDebugLogger.debug("DE1", "main", ["Applied flush settings on phase change"].map(String).join(" "))
            }

            // Update previous phase tracking
            root.previousPhase = phase

            // Reset completion suspension too when a new operation starts. A
            // post-session dialog may have hidden the overlay before the phase
            // transition delivered its completion message.
            if (phase === MachineState.Phase.Flushing ||
                phase === MachineState.Phase.Steaming ||
                phase === MachineState.Phase.HotWater ||
                phase === MachineState.Phase.EspressoPreheating ||
                phase === MachineState.Phase.Preinfusion ||
                phase === MachineState.Phase.Pouring) {
                if (root.completionPending) {
                    WebDebugLogger.debug("App", "main", ["Cancelling pending completion - new operation started (phase=" + phase + ")"].map(String).join(" "))
                }
                completionOverlay.cancel()
            }

            // Clear scale dialog deferral when machine reaches Ready or an active phase
            if (AppShell.scaleDialogDeferred) {
                if (phase === MachineState.Phase.Idle ||
                    phase === MachineState.Phase.Ready ||
                    phase === MachineState.Phase.EspressoPreheating ||
                    phase === MachineState.Phase.Steaming ||
                    phase === MachineState.Phase.HotWater ||
                    phase === MachineState.Phase.Flushing) {
                    AppShell.scaleDialogDeferred = false
                    // If a real physical scale connected during warmup, discard queued scale popups
                    // (FlowScale is always "connected" so don't let it suppress dialogs)
                    if (ScaleDevice.connected && !ScaleDevice.isFlowScale) {
                        root.removeQueuedScalePopups()
                    } else if (Settings.primaryScaleAddress !== "") {
                        root.showNextPendingPopup()  // Show deferred dialog now
                    }
                } else if (phase === MachineState.Phase.Sleep) {
                    AppShell.scaleDialogDeferred = false
                }
            }

            // Navigate to active operation pages (skip during page transition)
            if (phase === MachineState.Phase.EspressoPreheating ||
                phase === MachineState.Phase.Preinfusion ||
                phase === MachineState.Phase.Pouring ||
                phase === MachineState.Phase.Ending) {
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.espressoPage, "espressoPage")
            } else if (phase === MachineState.Phase.Steaming) {
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.steamPage, "steamPage")
            } else if (phase === MachineState.Phase.HotWater) {
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.hotWaterPage, "hotWaterPage")
            } else if (phase === MachineState.Phase.Flushing) {
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.flushPage, "flushPage")
            } else if (phase === MachineState.Phase.Descaling) {
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.descalingPage, "descalingPage")
            } else if (phase === MachineState.Phase.Transport) {
                if (!pageStack.busy)
                    navigation.showOperationPage(navigation.transportPage, "transportPage")
            } else if (phase === MachineState.Phase.Cleaning) {
                // For now, cleaning uses the built-in machine routine
                // Could navigate to a cleaning page in the future
            } else if (phase === MachineState.Phase.Sleep) {
                // Machine was put to sleep (e.g. via GHC stop button hold) - show screensaver
                // Skip if machine has never been awake since connecting (initial connect reports
                // Sleep before the wake command takes effect)
                if (root.wakePending) {
                    // Woken before the machine got there: this is the sleep request finishing,
                    // and the wake already queued behind it brings it back to Idle.
                    WebDebugLogger.debug("Screensaver", "main", ["Machine entered Sleep after the screensaver was woken - not showing it"].map(String).join(" "))
                } else if (!root.screensaverActive && !root.startupGracePeriod && !root.shuttingDown) {
                    WebDebugLogger.debug("Screensaver", "main", ["Machine entered Sleep - showing screensaver"].map(String).join(" "))
                    // Scale LCD disable is handled by C++ phaseChanged handler in main.cpp
                    root.goToScreensaver()
                }
            } else if (phase === MachineState.Phase.Idle || phase === MachineState.Phase.Ready) {
                root.wakePending = false
                // DE1 went to idle - if we're on an operation page, show completion.
                // Don't check pageStack.busy: completion must be handled, except when
                // the user explicitly exited a flush (userExitedFlush below).
                WebDebugLogger.debug("App", "main", ["Phase Idle/Ready: currentPage=" + currentPage + " completionOverlay.opacity=" + completionOverlay.opacity].map(String).join(" "))

                if (currentPage === "steamPage") {
                    root.showCompletion(trSteamComplete.text, "steam")
                } else if (currentPage === "hotWaterPage") {
                    root.showCompletion(trHotWaterComplete.text, "hotwater")
                } else if (currentPage === "flushPage") {
                    if (AppShell.userExitedFlush) {
                        WebDebugLogger.debug("DE1", "main", ["Phase Idle/Ready: flush exited by user, skipping completion overlay"].map(String).join(" "))
                    } else {
                        root.showCompletion(trFlushComplete.text, "flush")
                    }
                }

                // Always clear the flag, even when currentPage is no longer flushPage
                // (synchronous back-handler navigation typically changes the page
                // before this async phase signal arrives). Without this, the flag
                // would strand and suppress a later legitimate flush completion.
                AppShell.userExitedFlush = false
            }
        }
    }

    // Application exit belongs to the native window/device lifecycle.
    Connections {
        target: AppShell
        function onImmediateQuitRequested() { Qt.quit() }
        function onQuitRequested() {
            if (MainController.firmwareUpdater && MainController.firmwareUpdater.isFlashing)
                firmwareFlashExitDialog.open()
            else
                quitConfirmDialog.open()
        }
    }

    // Helper to announce arbitrary text for accessibility (used for non-page announcements)
    function announceNavigation(text) {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
            AccessibilityManager.announce(text)
        }
    }

    // Clean up text for TTS (replace underscores, expand units, etc.)

    property bool screensaverActive: false

    function goToScreensaver() {
        // Already showing (the P shortcut is unguarded): replacing the page with
        // itself would only restart the video. The flag is the one thing to check:
        // left clear under a showing page, auto-sleep re-slept the machine every
        // minute for four hours, since this early return never raised it again.
        if (pageStack.currentItem && pageStack.currentItem.objectName === "screensaverPage") {
            if (!screensaverActive) {
                WebDebugLogger.warn("Screensaver", "main", "Screensaver showing with its flag clear - raising it")
                raiseScreensaverFlag()
            }
            return
        }
        WebDebugLogger.debug("Screensaver", "main", ["goToScreensaver called, type:", ScreensaverManager.screensaverType].map(String).join(" "))
        // Before raising the flag: onCurrentItemChanged treats any other page under a raised
        // flag as having left the screensaver, so popping to home after it cleared the flag
        // at once, leaving the status bar over the screensaver and auto-sleep re-firing.
        navigation.showHome()
        raiseScreensaverFlag()

        // Close any open popups to prevent burn-in (Qt Popup renders above the
        // StackView on the overlay layer, so the screensaver can't cover them).
        // Re-queue so they show after wake. screensaverActive is already true,
        // so showNextPendingPopup() (called by onClosed) is a no-op.
        var popups = [
            { dialog: updateDialog,            id: "update" },
            { dialog: flowScaleDialog,         id: "flowScale" },
            { dialog: scaleDisconnectedDialog, id: "scaleDisconnected" },
            { dialog: refillDialog,            id: "refill" },
            { dialog: standbySwitchDialog,     id: "standbySwitch" },
            { dialog: bleErrorDialog,          id: "bleError" },
            { dialog: chargingMismatchDialog,  id: "chargingMismatch" },
            { dialog: localNetworkDeniedDialog, id: "localNetworkDenied" },
            { dialog: noScaleAbortDialog,      id: null },
            { dialog: globalSupportDialogs.crashReportDialog, id: null },
            { dialog: emptyDatabaseDialog,     id: null },
            { dialog: quitConfirmDialog,       id: null },
        ]
        for (let i = 0; i < popups.length; i++) {
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
        if (Qt.platform.os === "ios") Settings.network.dismissDiscussOverlay()

        // Navigate to screensaver page for all modes (including "disabled")
        // For "disabled" mode, ScreensaverPage dims the backlight to minimum
        // and shows a black overlay. We keep FLAG_KEEP_SCREEN_ON set to avoid
        // potential EGL surface issues (QTBUG-45019 class of bugs).
        // Pushed over the home screen, not replacing the stack, so waking is a pop rather
        // than a rebuild (~190 ms per wake on a Galaxy Tab A9+, #1976). The home screen's
        // own popups would stay above the screensaver, so they are closed too.
        PopupCloser.closeAllUnder(pageStack.currentItem)
        pageStack.push(navigation.screensaverPage)
        // Seen cleared between the raise above and here (an Oct 2026 log: 20 ms
        // after this function started, before the page finished loading).
        if (!screensaverActive && pageStack.currentItem
                && pageStack.currentItem.objectName === "screensaverPage") {
            WebDebugLogger.warn("Screensaver", "main", "Screensaver flag cleared while it opened - raising it again")
            raiseScreensaverFlag()
        }
    }

    function raiseScreensaverFlag() {
        screensaverActive = true
        // Mirror to C++ so subsystems (BLE scan-reconnect loops) can pause work
        // for the duration the user is away. See ScreensaverVideoManager::screensaverActive.
        ScreensaverManager.screensaverActive = true
        // Reset sleep counter (stopped state)
        root.sleepCountdownNormal = 0
    }

    // Both screensaver flags, the auto-sleep countdown and queued popups, cleared
    // together. Brightness is restored in ScreensaverPage.StackView.onRemoved.
    // The scheduled stay-awake window is evaluated live, so waking here
    // (manually or via auto-wake) needs no separate arming.
    // `reason` names the route out, so a log shows what cleared the flag.
    function leaveScreensaverState(reason) {
        screensaverActive = false
        ScreensaverManager.screensaverActive = false
        root.sleepCountdownNormal = root.autoSleepMinutes
        root.stayAwakeSuppressionLogged = false
        WebDebugLogger.debug("Screensaver", "main", "Waking from screensaver (" + reason + "): normal countdown="
                    + root.sleepCountdownNormal + " pendingPopups=" + pendingPopups.length)
        // Show any popups that arrived during screensaver
        if (pendingPopups.length > 0) {
            Qt.callLater(root.showNextPendingPopup)
        }
    }

    // Set when the screensaver is woken, cleared when the machine reports Idle. See the
    // Sleep phase handler.
    property bool wakePending: false

    function goToIdleFromScreensaver() {
        root.wakePending = true
        leaveScreensaverState("wake requested")
        navigation.showHome()
    }

    // Touch capture to reset sleep countdown (transparent, doesn't block input)
    MouseArea {
        anchors.fill: parent
        z: 1000  // Above everything
        propagateComposedEvents: true
        onPressed: function(mouse) {
            // Reset the inactivity countdown on user touch (the scheduled
            // stay-awake window is independent of activity)
            if (root.autoSleepMinutes > 0 && !root.screensaverActive) {
                let prev = root.sleepCountdownNormal
                root.sleepCountdownNormal = root.autoSleepMinutes
                root.stayAwakeSuppressionLogged = false
                if (prev <= 5) WebDebugLogger.debug("AutoSleep", "main", ["Reset by touch: " + prev + " -> " + root.sleepCountdownNormal].map(String).join(" "))
            }
            // Touch also resets the auto-load countdown so reading on the
            // Idle page doesn't silently swap the active profile.
            root.autoLoadResetCountdown()
            mouse.accepted = false  // Let the touch through
        }
        onReleased: function(mouse) { mouse.accepted = false }
        onClicked: function(mouse) { mouse.accepted = false }
    }

    // Keyboard shortcut for simulation mode (Ctrl+D). Inert on builds with no
    // simulator compiled in, so it can't strand a user in a mode that has no
    // engine behind it and no Settings card to switch it back off.
    Shortcut {
        sequence: "Ctrl+D"
        enabled: Settings.app.simulatorAvailable
        onActivated: {
            var newState = !DE1Device.simulationMode
            WebDebugLogger.debug("App", "main", ["Toggling simulation mode:", newState ? "ON" : "OFF"].map(String).join(" "))
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
            if (MachineState.isReady && root.canStartOperations) {
                WebDebugLogger.info("Keyboard", "main", ["Starting espresso via 'E' key"].map(String).join(" "))
                DE1Device.startEspresso()
            } else {
                WebDebugLogger.info("Keyboard", "main", ["Cannot start espresso - machine not ready or GHC active, phase:", MachineState.phase].map(String).join(" "))
            }
        }
    }

    // S = Steam
    Shortcut {
        sequence: "S"
        onActivated: {
            if (MachineState.isReady && root.canStartOperations) {
                WebDebugLogger.info("Keyboard", "main", ["Starting steam via 'S' key"].map(String).join(" "))
                DE1Device.startSteam()
            } else {
                WebDebugLogger.info("Keyboard", "main", ["Cannot start steam - machine not ready or GHC active, phase:", MachineState.phase].map(String).join(" "))
            }
        }
    }

    // W = Hot Water
    Shortcut {
        sequence: "W"
        onActivated: {
            if (MachineState.isReady && root.canStartOperations) {
                WebDebugLogger.info("Keyboard", "main", ["Starting hot water via 'W' key"].map(String).join(" "))
                DE1Device.startHotWater()
            } else {
                WebDebugLogger.info("Keyboard", "main", ["Cannot start hot water - machine not ready or GHC active, phase:", MachineState.phase].map(String).join(" "))
            }
        }
    }

    // F = Flush
    Shortcut {
        sequence: "F"
        onActivated: {
            if (MachineState.isReady && root.canStartOperations) {
                WebDebugLogger.info("Keyboard", "main", ["Starting flush via 'F' key"].map(String).join(" "))
                DE1Device.startFlush()
            } else {
                WebDebugLogger.info("Keyboard", "main", ["Cannot start flush - machine not ready or GHC active, phase:", MachineState.phase].map(String).join(" "))
            }
        }
    }

    // Space = Stop: leaves an operation page the way its Stop does, else goes home. The
    // window-level shortcut takes the key before any page's own Keys handler.
    Shortcut {
        sequence: "Space"
        onActivated: {
            WebDebugLogger.info("Keyboard", "main", ["Stop/Idle via Space key, phase:", MachineState.phase].map(String).join(" "))
            DE1Device.stopOperation()
            if (OperationNavigationPolicy.isOperationPage(
                        pageStack.currentItem ? pageStack.currentItem.objectName : ""))
                navigation.leaveOperationPage()
            else
                navigation.goToIdle()
        }
    }

    // P = Sleep
    Shortcut {
        sequence: "P"
        onActivated: {
            WebDebugLogger.info("Keyboard", "main", ["Going to sleep via 'P' key"].map(String).join(" "))
            // Put scale to LCD-off mode (keep connected for wake)
            if (ScaleDevice && ScaleDevice.connected) {
                ScaleDevice.disableLcd()
            }
            DE1Device.goToSleep()
            root.goToScreensaver()
        }
    }

    // 2-finger swipe detection for back gesture (accessibility)
    MultiPointTouchArea {
        anchors.fill: parent
        z: -1  // Behind all controls
        minimumTouchPoints: 2
        maximumTouchPoints: 2
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled

        property var startPoints: []

        onPressed: function(touchPoints) {
            if (touchPoints.length === 2) {
                startPoints = [{x: touchPoints[0].x, y: touchPoints[0].y},
                               {x: touchPoints[1].x, y: touchPoints[1].y}]
            }
        }

        onReleased: function(touchPoints) {
            // Check for 2-finger swipe left (back gesture) when accessibility is on
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled &&
                startPoints.length === 2 && touchPoints.length === 2) {
                let deltaX1 = touchPoints[0].x - startPoints[0].x
                let deltaX2 = touchPoints[1].x - startPoints[1].x
                let avgDeltaX = (deltaX1 + deltaX2) / 2

                // Swipe left threshold: -100 pixels
                if (avgDeltaX < -100) {
                    // Two-finger swipe left = go back
                    AccessibilityManager.announce(trAnnounceGoingBack.text)
                    // Was `stackView.depth`/`stackView.pop()` — no such id exists in this file
                    // (the StackView is `pageStack`), so this threw a ReferenceError and took the
                    // whole handler with it, else-branch included. The two-finger back gesture has
                    // never worked, and it only runs with a screen reader active, which is why
                    // nobody hit it.
                    //
                    // Same idiom as onDismissRequested: at depth 1 navigation.goBack() alone would do
                    // nothing after the announcement above had already said it went back.
                    if (pageStack.depth > 1)
                        navigation.goBack()
                    else
                        navigation.goToIdle()
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

    // Auto-wake: Exit screensaver when scheduled wake time is reached
    Connections {
        target: MainController

        function onAutoWakeTriggered() {
            WebDebugLogger.info("AutoSleep", "main", ["Auto-wake triggered"].map(String).join(" "))
            if (root.screensaverActive) {
                root.goToIdleFromScreensaver()
            }
            // No stay-awake arming here: the window is evaluated live from
            // the schedule by AutoWakeManager.isWithinStayAwakeWindow(), so
            // it applies even when this signal isn't observed (app started
            // after the wake minute, manual wake, process suspended) — #1203.
        }

        function onShotDiscarded(durationSec, finalWeightG) {
            // Aborted-shot classifier dropped the just-finished espresso shot.
            // Notification only — the shot is gone for good.
            discardedShotToast.show(trDiscardedShotToast.text)
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(trDiscardedShotToast.text, true)
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

    StatusToast {
        id: reconnectingToast
        message: root.shotStoppedForProfileRetry
                 ? trShotStoppedReloading.text
                 : trReconnectingToast.text
        opacity: (ProfileManager.profileUploadRetrying
                  && !ProfileManager.de1CommunicationFailure) ? 1 : 0
    }

    Connections {
        target: ProfileManager
        function onProfileUploadRetryingChanged() {
            if (ProfileManager.profileUploadRetrying) {
                if (AccessibilityManager.enabled) {
                    AccessibilityManager.announce(reconnectingToast.message)
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

    // ============ SHOT EXPORT BULK COMPLETION TOAST ============
    // Shown once the initial "export all shots" pass triggered by enabling
    // Settings.network.exportShotsToFile has finished writing files to the user
    // history folder. Silent-until-done so the toggle behaves like a plain
    // boolean preference.

    StatusToast { id: shotExportToast }

    // ============ BAG PUSH REJECTED TOAST ============
    // Shown when a bag edit's Visualizer sync is rejected by server validation
    // (HTTP 422 — e.g. renamed bag collides with an existing roaster+name+
    // roast_date). One-shot, not retried; the local edit is kept as-is
    // (add-bag-detail-editing).
    Tr { id: trBagPushRejected; key: "main.toast.bagPushRejected"; fallback: "Visualizer did not accept the update for %1: %2"; visible: false }

    Connections {
        target: MainController.visualizer

        function onBagPushRejected(localBagId, bagName, message) {
            // Name the bag: a 422 can arrive from the retry drain long after
            // the edit, when a bare "the bag update" identifies nothing.
            bagPushToast.show(trBagPushRejected.text.arg(bagName).arg(message))
            if (AccessibilityManager.enabled) {
                // Assertive: this is the only trace that the local and remote
                // bags have permanently diverged.
                AccessibilityManager.announce(bagPushToast.message, true)
            }
        }
    }

    StatusToast { id: bagPushToast; durationMs: 5000 }

    // ============ DISCARDED ABORTED SHOT TOAST ============
    // Shown when MainController's aborted-shot classifier drops a shot that did
    // not start (extraction < 10 s AND yield < 5 g). Notification only — there
    // is no recovery path; the shot is intentionally not recorded. See issue
    // #899 and openspec/changes/add-discard-aborted-shots.
    Tr { id: trDiscardedShotToast; key: "main.toast.shotDiscarded"; fallback: "Shot did not start — not recorded"; visible: false }

    StatusToast { id: discardedShotToast }

    // ============ AUTO-LOAD STALE TOAST ============
    // Shown when ProfileManager.loadAutoLoadProfileIfNeeded() finds the pinned
    // filename no longer resolves to a Selected-list profile (deleted, hidden,
    // or imported from another device), or when MainController's recipe-side
    // equivalent finds the pinned recipe no longer exists or was archived
    // (recipe-auto-load). Setting is cleared automatically in both cases;
    // this one toast surface is shared, with `message` swapped per-trigger.
    Tr { id: trAutoLoadStaleToast; key: "profileselector.toast.auto_load_stale"; fallback: "Auto-load profile is no longer available"; visible: false }
    Tr { id: trAutoLoadRecipeStaleToast; key: "recipes.toast.auto_load_stale"; fallback: "Auto-load recipe is no longer available"; visible: false }

    StatusToast { id: autoLoadStaleToast }

    // Surface storage errors to the user. Both ShotHistoryStorage::errorOccurred
    // and CoffeeBagStorage::errorOccurred previously had NO consumer, so every DB
    // failure — failed shot save, failed metadata write (e.g. a taste tap or
    // rating that never persisted), failed delete/import, failed bean edit — was
    // silent: the user assumed their data was saved when it wasn't. One toast
    // surfaces every emit site from both stores, non-blocking.
    StatusToast { id: homeActionError; error: true; durationMs: 5000 }
    Connections {
        target: HomePresetActions
        function onFailed(message) { homeActionError.show(message) }
    }

    StatusToast { id: storageErrorToast; error: true; durationMs: 5000 }
    function showStorageError(message) {
        storageErrorToast.show(message)
        if (AccessibilityManager.enabled)
            AccessibilityManager.announce(message, true)
    }

    Connections {
        target: MainController.shotHistory
        function onErrorOccurred(message) {
            // An advisor metadata write emits errorOccurred ("please try again")
            // and THEN shotMetadataCaptureFailed, both in the same block. Showing
            // the first would be overwritten visually by the second, but
            // showStorageError() also announces assertively — so a
            // screen-reader user heard the wrong advice in full, spoken first, and
            // "try again" cannot work for a shot that does not exist. Let the
            // advisor handler below own that case.
            if (MainController.aiManager && MainController.aiManager.hasPendingShotMetadataWrite())
                return
            root.showStorageError(message)
        }
    }

    Connections {
        target: MainController.bagStorage
        function onErrorOccurred(message) {
            root.showStorageError(message)
        }
    }

    // The advisor's own metadata writes. ShotHistoryStorage::errorOccurred
    // already toasts "Couldn't save your shot changes — please try again", which
    // is the wrong advice for this failure: the usual cause is a conversation
    // turn naming a shot that is not on this device, and retrying cannot fix
    // that. Say what was lost instead of offering a remedy that does not work.
    Tr {
        id: trAdvisorMetadataLost
        key: "ai.error.shotMetadataNotSaved"
        fallback: "What you told the advisor wasn't saved to that shot"
        visible: false
    }
    Connections {
        target: MainController.aiManager
        function onShotMetadataCaptureFailed(shotId) {
            root.showStorageError(trAdvisorMetadataLost.text)
        }
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
                shotExportToast.show(TranslationManager.translate(
                    "main.toast.exportShotsPartial",
                    "Exported %1 shots; %2 failed").arg(written).arg(failed))
            } else {
                shotExportToast.show(TranslationManager.translate(
                    "main.toast.exportShotsDone",
                    "Exported %1 shots").arg(written))
            }
            if (AccessibilityManager.enabled) {
                AccessibilityManager.announce(shotExportToast.message)
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
    Tr { id: trAnnounceTransport; key: "main.accessibility.transport"; fallback: "Draining water for transport"; visible: false }

    Connections {
        target: MachineState
        enabled: AccessibilityManager.enabled

        function onPhaseChanged() {
            var phase = MachineState.phase
            var announcement = ""

            switch (phase) {
                case MachineState.Phase.Disconnected:
                    announcement = trAnnounceDisconnected.text
                    break
                case MachineState.Phase.Sleep:
                    announcement = trAnnounceSleeping.text
                    break
                case MachineState.Phase.Idle:
                    announcement = trAnnounceIdle.text
                    break
                case MachineState.Phase.Heating:
                    announcement = trAnnounceHeating.text
                    break
                case MachineState.Phase.Ready:
                    announcement = trAnnounceReady.text
                    break
                case MachineState.Phase.EspressoPreheating:
                    announcement = trAnnouncePreheating.text
                    break
                case MachineState.Phase.Preinfusion:
                    announcement = trAnnouncePreinfusion.text
                    break
                case MachineState.Phase.Pouring:
                    announcement = trAnnouncePouring.text
                    break
                case MachineState.Phase.Ending:
                    announcement = trAnnounceShotComplete.text
                    break
                case MachineState.Phase.Steaming:
                    announcement = trAnnounceSteaming.text
                    break
                case MachineState.Phase.HotWater:
                    announcement = trAnnounceHotWater.text
                    break
                case MachineState.Phase.Flushing:
                    announcement = trAnnounceFlushing.text
                    break
                case MachineState.Phase.Descaling:
                    announcement = trAnnounceDescaling.text
                    break
                case MachineState.Phase.Cleaning:
                    announcement = trAnnounceCleaning.text
                    break
                case MachineState.Phase.Transport:
                    announcement = trAnnounceTransport.text
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
        // Not `ScaleDevice !== null`: the singleton proxy always exists, so that test was always
        // true and guarded nothing. Gate on the state, never on the object.
        enabled: AccessibilityManager.enabled

        function onConnectedChanged() {
            if (ScaleDevice.connected) {
                AccessibilityManager.announce(trAnnounceScaleConnected.text + " " + ScaleDevice.name)
            }
            // Disconnection is handled by scaleDisconnectedDialog
        }
    }

    // Discard stale scale popups when scale reconnects
    Connections {
        target: ScaleDevice

        function onConnectedChanged() {
            if (ScaleDevice.connected && !ScaleDevice.isFlowScale) {
                root.removeQueuedScalePopups()
            }
        }
    }

    // ============ PER-PAGE SCALE CONFIGURATION OVERLAY ============
    // Floating centered control for adjusting page scale
    // Uses scaledBase() to maintain consistent size regardless of current page scale
    Rectangle {
        id: pageScaleOverlay
        visible: root.appInitialized && Theme.configurePageScaleEnabled && !root.screensaverActive && Theme.currentPageObjectName !== ""
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
    // Keyboard.visible is unreliable on Android (goes false after 1s),
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
        for (let p = item; p; p = p.parent) {
            if (p === overlay)
                return false
        }
        return true
    }
    Rectangle {
        id: globalHideKeyboardButton
        visible: root._textInputFocused && (Qt.platform.os === "android" || Qt.platform.os === "ios")
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
                Keyboard.hide()
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
            root.triggerLibraryThumbnailCapture(entryId)
        }
        function onRequestThumbnailCapture(entryId) {
            root.triggerLibraryThumbnailCapture(entryId)
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
    DecenzaDialog {
        id: emptyDatabaseDialog
        logName: "Empty database"
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
                        root.startBluetoothScan();
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("main.emptydb.restore", "Restore")
                    primary: true
                    accessibleName: TranslationManager.translate("main.emptydb.restoreAccessible", "Open restore settings")
                    onClicked: {
                        emptyDatabaseDialog.close();
                        root.startBluetoothScan();
                        navigation.goToSettings("historyData");  // History & Data tab has restore
                    }
                }
            }
        }
    }

    SessionAppearancePublisher { }
}
