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

    // Update scale factor when window resizes
    onWidthChanged: updateScale()
    onHeightChanged: updateScale()
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

        updateScale()

        // Check for first run and show welcome dialog or start scanning
        var firstRunComplete = Settings.value("firstRunComplete", false)
        if (!firstRunComplete) {
            firstRunDialog.open()
        } else {
            // On subsequent launches, still check if storage setup is needed
            // (e.g., after reinstall when QSettings was restored but SAF permission wasn't)
            checkStorageSetup()
        }
    }

    function updateScale() {
        // Scale based on window size vs reference (960x600 tablet dp)
        var scaleX = width / Theme.refWidth
        var scaleY = height / Theme.refHeight
        Theme.scale = Math.min(scaleX, scaleY)
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

        Component {
            id: shotMetadataPage
            ShotMetadataPage {}
        }
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

                Button {
                    text: TranslationManager.translate("update.later", "Later")
                    onClicked: {
                        if (MainController.updateChecker) {
                            MainController.updateChecker.dismissUpdate()
                        }
                        updateDialog.close()
                    }
                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.buttonRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textColor
                        font: Theme.labelFont
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Button {
                    text: TranslationManager.translate("update.updatenow", "Update Now")
                    onClicked: {
                        if (MainController.updateChecker) {
                            MainController.updateChecker.downloadAndInstall()
                        }
                        updateDialog.close()
                        // Navigate to settings update tab (index 9) to show progress
                        goToSettings(9)
                    }
                    background: Rectangle {
                        color: Theme.primaryColor
                        radius: Theme.buttonRadius
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.labelFont
                        horizontalAlignment: Text.AlignHCenter
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
                // Apply steam settings immediately when entering steam (from GHC button)
                MainController.applySteamSettings()
                console.log("Applied steam settings on phase change to Steaming")
            } else if (phase === MachineStateType.Phase.HotWater && wasIdle) {
                MainController.applyHotWaterSettings()
                console.log("Applied hot water settings on phase change")
            } else if (phase === MachineStateType.Phase.Flushing && wasIdle) {
                MainController.applyFlushSettings()
                console.log("Applied flush settings on phase change")
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

                if (currentPage === "steamPage") {
                    showCompletion(trSteamComplete.text, "steam")
                } else if (currentPage === "hotWaterPage") {
                    showCompletion(trHotWaterComplete.text, "hotwater")
                } else if (currentPage === "flushPage") {
                    showCompletion(trFlushComplete.text, "flush")
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

    function goToSettings(tabIndex) {
        announceNavigation("Settings")
        if (tabIndex !== undefined && tabIndex >= 0) {
            settingsPage.requestedTabIndex = tabIndex
        }
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

    function goToShotMetadata(hasPending) {
        announceNavigation("Shot info")
        pageStack.push(shotMetadataPage)
        // Set hasPendingShot on the page if we came from a shot
        if (hasPending && pageStack.currentItem) {
            pageStack.currentItem.hasPendingShot = true
        }
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
            console.log("Shot ended, showing metadata page")
            // Record the shot timestamp
            Settings.dyeShotDateTime = Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm")
            // Restart timer to ensure overlay survives page change
            stopOverlayTimer.restart()
            goToShotMetadata(true)
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
