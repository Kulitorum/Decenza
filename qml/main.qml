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
        var val = Settings.value("autoSleepMinutes", 0)
        return (val === undefined || val === null) ? 0 : parseInt(val)
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
                var val = Settings.value("autoSleepMinutes", 0)
                root.autoSleepMinutes = (val === undefined || val === null) ? 0 : parseInt(val)
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
            id: flushPage
            FlushPage {}
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

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: bleErrorDialog.errorMessage
                wrapMode: Text.Wrap
                width: parent.width
            }

            Button {
                text: "Open Location Settings"
                visible: bleErrorDialog.isLocationError
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    BLEManager.openLocationSettings()
                    bleErrorDialog.close()
                }
            }

            Button {
                text: "OK"
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

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: "No Bluetooth scale was detected.\n\nUsing estimated weight from DE1 flow measurement instead.\n\nYou can search for your scale in Settings → Bluetooth."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.labelFont
            }

            Button {
                text: "OK"
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

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: "Your Bluetooth scale has disconnected.\n\nUsing estimated weight from DE1 flow measurement until the scale reconnects.\n\nCheck that your scale is powered on and in range."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.labelFont
            }

            Button {
                text: "OK"
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

        Column {
            spacing: Theme.spacingMedium
            width: Theme.dialogWidth

            Label {
                text: "The water tank is empty.\n\nPlease refill the water tank and press OK to continue."
                wrapMode: Text.Wrap
                width: parent.width
                font: Theme.bodyFont
            }

            Button {
                text: "OK"
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

            Button {
                text: "Continue"
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

        property int previousPhase: -1

        function onPhaseChanged() {
            // Don't navigate while a transition is in progress
            if (pageStack.busy) return

            let phase = MachineState.phase
            let currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""

            // Check if we just finished an operation (was active, now idle/ready)
            let wasActive = (previousPhase === MachineStateType.Phase.Steaming ||
                            previousPhase === MachineStateType.Phase.HotWater ||
                            previousPhase === MachineStateType.Phase.Flushing)
            let isNowIdle = (phase === MachineStateType.Phase.Idle ||
                            phase === MachineStateType.Phase.Ready ||
                            phase === MachineStateType.Phase.Sleep ||
                            phase === MachineStateType.Phase.Heating)

            // Navigate to active operation pages
            if (phase === MachineStateType.Phase.EspressoPreheating ||
                phase === MachineStateType.Phase.Preinfusion ||
                phase === MachineStateType.Phase.Pouring ||
                phase === MachineStateType.Phase.Ending) {
                if (currentPage !== "espressoPage") {
                    pageStack.replace(espressoPage)
                }
            } else if (phase === MachineStateType.Phase.Steaming) {
                if (currentPage !== "steamPage") {
                    pageStack.replace(steamPage)
                }
            } else if (phase === MachineStateType.Phase.HotWater) {
                if (currentPage !== "hotWaterPage") {
                    pageStack.replace(hotWaterPage)
                }
            } else if (phase === MachineStateType.Phase.Flushing) {
                if (currentPage !== "flushPage") {
                    pageStack.replace(flushPage)
                }
            } else if (wasActive && isNowIdle) {
                // Operation finished - show completion then return to idle
                debugLiveView = false  // Reset debug mode

                if (previousPhase === MachineStateType.Phase.Steaming) {
                    showCompletion("Steam Complete", "steam")
                } else if (previousPhase === MachineStateType.Phase.HotWater) {
                    showCompletion("Hot Water Complete", "hotwater")
                } else if (previousPhase === MachineStateType.Phase.Flushing) {
                    showCompletion("Flush Complete", "flush")
                } else {
                    pageStack.replace(idlePage)
                }
            }

            previousPhase = phase
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
        pageStack.replace(steamPage)
    }

    function goToHotWater() {
        pageStack.replace(hotWaterPage)
    }

    function goToSettings() {
        pageStack.push(settingsPage)
    }

    function goBack() {
        if (pageStack.depth > 1) {
            pageStack.pop()
        }
    }

    function goToProfileEditor() {
        pageStack.push(profileEditorPage)
    }

    function goToProfileSelector() {
        pageStack.push(profileSelectorPage)
    }

    function goToFlush() {
        pageStack.push(flushPage)
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

    // Debug page cycling (only in simulation mode)
    property var debugPages: ["idle", "steam", "hotWater", "flush"]
    property int debugPageIndex: 0
    property bool debugLiveView: false  // Forces live view on debug pages

    function cycleDebugPage() {
        debugPageIndex = (debugPageIndex + 1) % debugPages.length
        var page = debugPages[debugPageIndex]
        console.log("Debug: switching to", page, "page (live view)")
        debugLiveView = (page !== "idle")  // Show live view for non-idle pages
        switch (page) {
            case "idle": pageStack.replace(idlePage); break
            case "steam": pageStack.replace(steamPage); break
            case "hotWater": pageStack.replace(hotWaterPage); break
            case "flush": pageStack.replace(flushPage); break
        }
    }

    // Double-tap detector for debug mode
    MouseArea {
        visible: DE1Device.simulationMode
        anchors.fill: parent
        z: 999  // Below inactivity timer but above most content
        propagateComposedEvents: true

        property real lastClickTime: 0

        onPressed: function(mouse) {
            var now = Date.now()
            if (now - lastClickTime < 300) {
                // Double-tap detected
                cycleDebugPage()
            }
            lastClickTime = now
            mouse.accepted = false  // Let the touch through
        }
        onReleased: function(mouse) { mouse.accepted = false }
        onClicked: function(mouse) { mouse.accepted = false }
    }

    // Keyboard shortcut handler (Shift+D for simulation mode)
    Item {
        focus: true
        Keys.onPressed: function(event) {
            // Shift+D toggles simulation mode for GUI development
            if (event.key === Qt.Key_D && (event.modifiers & Qt.ShiftModifier)) {
                var newState = !DE1Device.simulationMode
                console.log("Toggling simulation mode:", newState ? "ON" : "OFF")
                DE1Device.simulationMode = newState
                if (ScaleDevice) {
                    ScaleDevice.simulationMode = newState
                }
                // Reset debug state when turning off simulation mode
                if (!newState) {
                    debugLiveView = false
                    debugPageIndex = 0
                }
                event.accepted = true
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
            text: "⚠ SIMULATION MODE (Shift+D toggle, double-tap cycle pages)"
            color: "white"
            font.pixelSize: 14
            font.bold: true
        }
    }
}
