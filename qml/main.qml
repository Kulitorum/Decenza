import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    title: "DE1 Controller"
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
        running: root.autoSleepMinutes > 0 && pageStack.currentItem &&
                 pageStack.currentItem.objectName !== "screensaverPage"
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

    // Update scale factor when window resizes
    onWidthChanged: updateScale()
    onHeightChanged: updateScale()
    Component.onCompleted: {
        updateScale()
        console.log("Auto-sleep setting:", root.autoSleepMinutes, "minutes (0 = never)")

        // Check for first run and show welcome dialog or start scanning
        var firstRunComplete = Settings.value("firstRunComplete", false)
        if (!firstRunComplete) {
            firstRunDialog.open()
        } else {
            startBluetoothScan()
        }
    }

    function updateScale() {
        // Scale based on the smaller ratio to maintain aspect ratio
        var scaleX = width / Theme.refWidth
        var scaleY = height / Theme.refHeight
        Theme.scale = Math.min(scaleX, scaleY)
    }

    // Page stack for navigation
    StackView {
        id: pageStack
        anchors.fill: parent
        initialItem: idlePage

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

        Component {
            id: screensaverPage
            ScreensaverPage {}
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
            spacing: 16
            width: 320

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
    }

    // First-run welcome dialog
    Dialog {
        id: firstRunDialog
        title: "Welcome to DE1 Controller"
        modal: true
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose

        Column {
            spacing: 20
            width: 400

            Label {
                text: "Before we begin, please make sure your Bluetooth scale is powered on and ready to pair.\n\nThe app will search for your DE1 espresso machine and compatible scales."
                wrapMode: Text.Wrap
                width: parent.width
                font.pixelSize: 16
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
        // Try direct connect first if we have a saved scale
        if (BLEManager.hasSavedScale) {
            BLEManager.tryDirectConnectToScale()
        }
        // Start scanning after a short delay
        scanDelayTimer.start()
    }

    Timer {
        id: scanDelayTimer
        interval: 500
        onTriggered: BLEManager.startScan()
    }

    // Status bar overlay (hidden during screensaver)
    StatusBar {
        id: statusBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 70
        z: 100
        visible: !pageStack.currentItem || pageStack.currentItem.objectName !== "screensaverPage"
    }

    // Connection state handler - auto navigate based on machine state
    Connections {
        target: MachineState

        function onPhaseChanged() {
            // Don't navigate while a transition is in progress
            if (pageStack.busy) return

            let phase = MachineState.phase
            let currentPage = pageStack.currentItem ? pageStack.currentItem.objectName : ""

            // Only auto-navigate during active operations
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
            } else if (phase === MachineStateType.Phase.HotWater ||
                       phase === MachineStateType.Phase.Flushing) {
                if (currentPage !== "hotWaterPage") {
                    pageStack.replace(hotWaterPage)
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

    function goToScreensaver() {
        pageStack.replace(screensaverPage)
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
            text: "⚠ SIMULATION MODE (Shift+D to toggle)"
            color: "white"
            font.pixelSize: 14
            font.bold: true
        }
    }
}
