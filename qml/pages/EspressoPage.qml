import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../components"

Page {
    id: espressoPage
    objectName: "espressoPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Local weight property - updated directly in signal handler for immediate display
    property real currentWeight: 0.0

    // Frame name display — only updates when the same frame name is seen on
    // consecutive frame-change events, filtering out short transitional frames.
    property string displayedFrameName: ""
    property string pendingFrameName: ""
    property int pendingFrameCount: 0

    // Accessibility: value announcement cycling (swipe left/right)
    property int accessibilityValueIndex: 0
    readonly property var accessibilityValueNames: ["Frame", "Time", "Pressure", "Flow", "Temperature", "Weight"]

    // Enable keyboard focus for the page
    focus: true

    Component.onCompleted: {
        // Only set title if this page is actually in the StackView (not during preload)
        if (StackView.status === StackView.Active)
            root.currentPageTitle = ProfileManager.currentProfileName
    }
    StackView.onActivated: {
        root.currentPageTitle = ProfileManager.currentProfileName
        espressoPage.forceActiveFocus()  // Ensure keyboard focus
    }

    // Accessibility: get current value for announcement
    function getAccessibilityValue(index) {
        switch (index) {
            case 0: // Frame
                var frameInfo = MainController.currentFrameName || "Starting"
                return "Frame: " + frameInfo
            case 1: // Time
                return "Time: " + MachineState.shotTime.toFixed(1) + " seconds"
            case 2: // Pressure
                return "Pressure: " + DE1Device.pressure.toFixed(1) + " bar"
            case 3: // Flow
                return "Flow: " + DE1Device.flow.toFixed(1) + " milliliters per second"
            case 4: // Temperature
                return "Temperature: " + DE1Device.temperature.toFixed(1) + " degrees"
            case 5: // Weight and/or Volume
                var parts = []
                if (MachineState.targetWeight > 0) parts.push(TranslationManager.translate("espresso.accessible.weight", "Weight:") + " " + espressoPage.currentWeight.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.of", "of") + " " + ProfileManager.targetWeight.toFixed(0) + " " + TranslationManager.translate("espresso.accessible.grams", "grams"))
                if (MachineState.targetVolume > 0) parts.push(TranslationManager.translate("espresso.accessible.volume", "Volume:") + " " + MachineState.pourVolume.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.of", "of") + " " + MachineState.targetVolume.toFixed(0) + " " + TranslationManager.translate("espresso.accessible.milliliters", "milliliters"))
                return parts.join(", ") || TranslationManager.translate("espresso.noStopTarget", "No stop target")
            default:
                return ""
        }
    }

    // Accessibility: announce next value
    function announceNextValue() {
        accessibilityValueIndex = (accessibilityValueIndex + 1) % accessibilityValueNames.length
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(getAccessibilityValue(accessibilityValueIndex), true)
        }
    }

    // Accessibility: announce previous value
    function announcePreviousValue() {
        accessibilityValueIndex = (accessibilityValueIndex - 1 + accessibilityValueNames.length) % accessibilityValueNames.length
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(getAccessibilityValue(accessibilityValueIndex), true)
        }
    }

    // Accessibility: announce full status
    function announceFullStatus() {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var status = "Shot status. "
            status += getAccessibilityValue(0) + ". "  // Frame
            status += getAccessibilityValue(1) + ". "  // Time
            status += getAccessibilityValue(2) + ". "  // Pressure
            status += getAccessibilityValue(3) + ". "  // Flow
            status += getAccessibilityValue(5)         // Weight
            AccessibilityManager.announce(status, true)
        }
    }

    // Keyboard shortcuts to stop and go back
    Keys.onEscapePressed: {
        root.stopReason = "manual"
        DE1Device.stopOperation()
        root.goToIdle()
    }

    Keys.onSpacePressed: {
        root.stopReason = "manual"
        DE1Device.stopOperation()
        root.goToIdle()
    }

    // Additional keyboard navigation for accessibility
    Keys.onPressed: function(event) {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            if (event.key === Qt.Key_Left) {
                announcePreviousValue()
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                announceNextValue()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                announceFullStatus()
                event.accepted = true
            }
        }
        // Backspace also goes back
        if (event.key === Qt.Key_Backspace) {
            root.stopReason = "manual"
            DE1Device.stopOperation()
            root.goToIdle()
            event.accepted = true
        }
    }

    // Force immediate weight update on signal (bypasses lazy binding evaluation)
    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            espressoPage.currentWeight = MachineState.scaleWeight
        }
    }

    // ========== ACCESSIBILITY ANNOUNCEMENTS ==========

    // Track last announced weight for milestone announcements
    property real lastAnnouncedWeight: 0

    // Helper to check if accessibility announcements are enabled
    function accessibilityEnabled() {
        return typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
    }

    // Get phase announcement text
    function getPhaseAnnouncement(phase) {
        switch (phase) {
            case MachineStateType.Phase.EspressoPreheating:
                return TranslationManager.translate("espresso.accessibility.preheating", "Preheating")
            case MachineStateType.Phase.Preinfusion:
                return TranslationManager.translate("espresso.accessibility.preinfusion", "Preinfusion started")
            case MachineStateType.Phase.Pouring:
                return TranslationManager.translate("espresso.accessibility.pouring", "Pouring")
            case MachineStateType.Phase.Ending:
                return TranslationManager.translate("espresso.accessibility.ending", "Extraction ending")
            default:
                return ""
        }
    }

    // Announce phase transitions
    Connections {
        target: MachineState
        function onPhaseChanged() {
            if (!accessibilityEnabled()) return
            var announcement = getPhaseAnnouncement(MachineState.phase)
            if (announcement) {
                AccessibilityManager.announce(announcement, true)
            }
        }
    }

    // Announce extraction start
    Connections {
        target: MachineState
        function onShotStarted() {
            // Reset frame name so previous shot's last frame doesn't linger
            espressoPage.displayedFrameName = ""
            espressoPage.pendingFrameName = ""
            espressoPage.pendingFrameCount = 0

            if (!accessibilityEnabled()) return
            lastAnnouncedWeight = 0
            AccessibilityManager.announce(
                TranslationManager.translate("espresso.accessibility.started", "Extraction started"),
                true
            )
        }
    }

    // Announce extraction end with full status
    Connections {
        target: MachineState
        function onShotEnded() {
            if (!accessibilityEnabled()) return
            accessibilityUpdateTimer.stop()
            // Wait a moment then announce final status
            Qt.callLater(function() {
                var finalStatus = TranslationManager.translate("espresso.accessibility.ended", "Extraction finished") + ". "
                finalStatus += getAccessibilityValue(1) + ". "  // Time
                finalStatus += getAccessibilityValue(5)         // Weight
                AccessibilityManager.announce(finalStatus, true)
            })
        }
    }

    // Announce target weight reached
    Connections {
        target: MachineState
        function onTargetWeightReached() {
            if (!accessibilityEnabled()) return
            AccessibilityManager.announce(
                TranslationManager.translate("espresso.accessibility.targetReached", "Target weight reached"),
                true
            )
        }
    }

    // Announce weight milestones (every 10g) - respects user settings
    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            if (!accessibilityEnabled()) return
            if (!AccessibilityManager.extractionAnnouncementsEnabled) return
            // Only announce milestones if mode includes milestones
            var mode = AccessibilityManager.extractionAnnouncementMode
            if (mode !== "milestones_only" && mode !== "both") return

            var w = MachineState.scaleWeight
            // Announce every 10g milestone
            if (Math.floor(w / 10) > Math.floor(lastAnnouncedWeight / 10) && w > 0) {
                AccessibilityManager.announce(Math.floor(w) + " " +
                    TranslationManager.translate("espresso.accessibility.grams", "grams"))
                lastAnnouncedWeight = w
            }
        }
    }

    // Periodic update timer - uses configurable interval from AccessibilityManager
    Timer {
        id: accessibilityUpdateTimer
        interval: AccessibilityManager.extractionAnnouncementInterval * 1000
        repeat: true
        running: accessibilityEnabled() &&
                 AccessibilityManager.extractionAnnouncementsEnabled &&
                 (AccessibilityManager.extractionAnnouncementMode === "timed" ||
                  AccessibilityManager.extractionAnnouncementMode === "both") &&
                 (MachineState.phase === MachineStateType.Phase.Preinfusion ||
                  MachineState.phase === MachineStateType.Phase.Pouring)
        onTriggered: {
            var time = MachineState.shotTime.toFixed(0)
            var weight = espressoPage.currentWeight.toFixed(1)
            AccessibilityManager.announce(
                TranslationManager.translate("espresso.accessibility.time", "Time") + " " + time + " " +
                TranslationManager.translate("espresso.accessibility.seconds", "seconds") + ", " +
                TranslationManager.translate("espresso.accessibility.weight", "weight") + " " + weight + " " +
                TranslationManager.translate("espresso.accessibility.grams", "grams")
            )
        }
    }

    // ========== END ACCESSIBILITY ANNOUNCEMENTS ==========

    // Show animated notification on frame transitions
    Connections {
        target: MainController
        function onFrameChanged(frameIndex, frameName, transitionReason) {
            // Frame name filtering: show immediately if it matches the pending name
            // (confirming it's a real phase, not a brief transition), or queue it.
            if (frameName !== "") {
                if (frameName === espressoPage.pendingFrameName) {
                    espressoPage.pendingFrameCount++
                    // Second consecutive event with same name — it's stable, show it
                    if (espressoPage.pendingFrameCount >= 2 && espressoPage.displayedFrameName !== frameName) {
                        espressoPage.displayedFrameName = frameName
                    }
                } else {
                    // New frame name — if it's the first frame or display is empty, show immediately
                    espressoPage.pendingFrameName = frameName
                    espressoPage.pendingFrameCount = 1
                    if (espressoPage.displayedFrameName === "") {
                        espressoPage.displayedFrameName = frameName
                    }
                }
            }

            if (transitionReason === "" || frameName === "") return
            var text = _transitionText(transitionReason)
            frameTransitionLifecycle.stop()
            frameTransitionLabel.text = text
            frameTransitionPill.color = _transitionColor(transitionReason)
            frameTransitionPill.opacity = 1
            frameTransitionPill.scale = 1.0
            frameTransitionLifecycle.start()
            if (accessibilityEnabled()) {
                AccessibilityManager.announce(text)
            }
        }
    }

    function _transitionText(reason) {
        switch (reason) {
            case "weight": return TranslationManager.translate("espresso.transition.weight", "Weight exit")
            case "pressure": return TranslationManager.translate("espresso.transition.pressure", "Pressure exit")
            case "flow": return TranslationManager.translate("espresso.transition.flow", "Flow exit")
            case "time": return TranslationManager.translate("espresso.transition.time", "Time exit")
            default: return TranslationManager.translate("espresso.transition.next", "Next frame")
        }
    }

    function _transitionColor(reason) {
        switch (reason) {
            case "weight": return Theme.weightColor
            case "pressure": return Theme.pressureColor
            case "flow": return Theme.flowColor
            case "time": return Theme.textSecondaryColor
            default: return Theme.textSecondaryColor
        }
    }

    // Extraction view mode setting
    property string extractionViewMode: Settings.value("espresso/extractionView", "chart")
    property bool showPhaseIndicator: {
        var v = Settings.value("espresso/showPhaseIndicator", true)
        return v === true || v === "true"
    }
    property bool showStats: {
        var v = Settings.value("espresso/showStats", true)
        return v === true || v === "true"
    }
    // Shared with Post-Shot Review and Shot Detail pages via the same setting.
    property bool advancedMode: Settings.value("shotReview/advancedMode", false) === true

    // Sync from Settings changes made elsewhere (e.g. SettingsMachineTab)
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "espresso/extractionView")
                espressoPage.extractionViewMode = Settings.value("espresso/extractionView", "chart")
            else if (key === "espresso/showPhaseIndicator") {
                var v = Settings.value("espresso/showPhaseIndicator", true)
                espressoPage.showPhaseIndicator = (v === true || v === "true")
            }
            else if (key === "espresso/showStats") {
                var vs = Settings.value("espresso/showStats", true)
                espressoPage.showStats = (vs === true || vs === "true")
            }
            else if (key === "shotReview/advancedMode") {
                espressoPage.advancedMode = Settings.value("shotReview/advancedMode", false) === true
            }
        }
    }

    // Extraction view switcher (Loader swaps between ShotGraph and CupFill)
    Loader {
        id: extractionViewLoader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: graphLegend.visible ? graphLegend.top
                      : (espressoStopButton.visible ? espressoStopButton.top : infoBar.top)
        anchors.topMargin: Theme.scaled(50)
        sourceComponent: {
            switch (espressoPage.extractionViewMode) {
                case "cupFill": return cupFillComponent
                case "chart": return shotGraphComponent
                default:
                    console.warn("Unknown extraction view mode:", espressoPage.extractionViewMode, "— falling back to chart")
                    return shotGraphComponent
            }
        }
    }

    Component {
        id: shotGraphComponent
        ShotGraph { }
    }

    Component {
        id: cupFillComponent
        CupFillView {
            currentWeight: espressoPage.currentWeight
            targetWeight: ProfileManager.targetWeight
            currentFlow: DE1Device.flow
            currentPressure: DE1Device.pressure
            goalPressure: MainController.filteredGoalPressure
            goalFlow: MainController.filteredGoalFlow
            shotTime: MachineState.shotTime
            phase: MachineState.phase
        }
    }

    // View mode selector button (top-right)
    Rectangle {
        id: viewModeButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.pageTopMargin + Theme.scaled(16)
        anchors.rightMargin: Theme.spacingMedium
        z: 10
        width: Theme.scaled(44)
        height: Theme.scaled(44)
        radius: Theme.scaled(22)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: Theme.scaled(1)

        Accessible.ignored: true

        Image {
            anchors.centerIn: parent
            source: "qrc:/icons/Graph.svg"
            sourceSize.width: Theme.scaled(22)
            sourceSize.height: Theme.scaled(22)

            layer.enabled: true
            layer.smooth: true
            layer.effect: MultiEffect {
                colorization: 1.0
                colorizationColor: Theme.textColor
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("espresso.viewMode.button", "Change extraction view")
            accessibleItem: viewModeButton
            onAccessibleClicked: viewSelectorDialog.open()
        }
    }

    // View selector dialog
    ExtractionViewSelector {
        id: viewSelectorDialog
        currentMode: espressoPage.extractionViewMode
        showPhaseIndicator: espressoPage.showPhaseIndicator
        showStats: espressoPage.showStats
        advancedMode: espressoPage.advancedMode
        onModeSelected: function(mode) {
            espressoPage.extractionViewMode = mode
            Settings.setValue("espresso/extractionView", mode)
        }
        onPhaseIndicatorToggled: function(enabled) {
            espressoPage.showPhaseIndicator = enabled
            Settings.setValue("espresso/showPhaseIndicator", enabled)
        }
        onStatsToggled: function(enabled) {
            espressoPage.showStats = enabled
            Settings.setValue("espresso/showStats", enabled)
        }
        onAdvancedModeToggled: function(enabled) {
            espressoPage.advancedMode = enabled
            Settings.setValue("shotReview/advancedMode", enabled)
        }
    }

    // Phase indicator (centered over chart, top-left over cup fill)
    Rectangle {
        id: statusBanner
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin + Theme.scaled(20)
        x: espressoPage.extractionViewMode === "chart"
           ? (parent.width - width) / 2
           : Theme.spacingMedium
        z: 10
        width: phaseRow.width + Theme.spacingMedium * 2
        height: Theme.scaled(36)
        radius: Theme.scaled(18)
        color: {
            switch (MachineState.phase) {
                case MachineStateType.Phase.EspressoPreheating: return Theme.accentColor
                case MachineStateType.Phase.Preinfusion: return Theme.pressureColor
                case MachineStateType.Phase.Pouring: return Theme.flowColor
                case MachineStateType.Phase.Ending: return Theme.successColor
                default: return "transparent"
            }
        }
        opacity: 0.85
        visible: espressoPage.showPhaseIndicator &&
                 (MachineState.phase === MachineStateType.Phase.EspressoPreheating ||
                  MachineState.phase === MachineStateType.Phase.Preinfusion ||
                  MachineState.phase === MachineStateType.Phase.Pouring ||
                  MachineState.phase === MachineStateType.Phase.Ending)

        Accessible.role: Accessible.Alert
        Accessible.name: phaseLabelText.text

        Row {
            id: phaseRow
            anchors.centerIn: parent
            spacing: Theme.scaled(6)

            // Phase dot
            Rectangle {
                id: phaseDot
                width: Theme.scaled(8)
                height: Theme.scaled(8)
                radius: Theme.scaled(4)
                color: Theme.textColor
                anchors.verticalCenter: parent.verticalCenter
                opacity: 1.0

                property bool animating: MachineState.phase === MachineStateType.Phase.EspressoPreheating ||
                                         MachineState.phase === MachineStateType.Phase.Pouring

                onAnimatingChanged: {
                    if (!animating) phaseDot.opacity = 1.0
                }

                SequentialAnimation on opacity {
                    running: phaseDot.animating
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }

            Text {
                id: phaseLabelText
                text: {
                    switch (MachineState.phase) {
                        case MachineStateType.Phase.EspressoPreheating:
                            return TranslationManager.translate("espresso.phase.preheating", "Preheating")
                        case MachineStateType.Phase.Preinfusion:
                        case MachineStateType.Phase.Pouring:
                        case MachineStateType.Phase.Ending:
                            // Show frame name if available (advanced/flow profiles),
                            // fall back to generic phase name
                            var frameName = espressoPage.displayedFrameName
                            if (frameName)
                                return frameName
                            if (MachineState.phase === MachineStateType.Phase.Preinfusion)
                                return TranslationManager.translate("espresso.phase.preinfusion", "Pre-infusion")
                            if (MachineState.phase === MachineStateType.Phase.Pouring)
                                return TranslationManager.translate("espresso.phase.pouring", "Pouring")
                            return TranslationManager.translate("espresso.phase.ending", "Ending")
                        default: return ""
                    }
                }
                color: Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.weight: Font.Bold
                Accessible.ignored: true
            }
        }
    }

    // Frame transition notification (anchored next to phase pill)
    Rectangle {
        id: frameTransitionPill
        Accessible.ignored: true
        anchors.verticalCenter: statusBanner.verticalCenter
        anchors.left: statusBanner.right
        anchors.leftMargin: Theme.scaled(8)
        z: 10

        width: frameTransitionLabel.width + Theme.spacingLarge * 2
        height: Theme.scaled(36)
        radius: Theme.scaled(18)
        color: Theme.textSecondaryColor
        opacity: 0
        scale: 1.0

        visible: espressoPage.showPhaseIndicator && opacity > 0

        SequentialAnimation {
            id: frameTransitionLifecycle
            NumberAnimation {
                target: frameTransitionPill
                property: "scale"
                from: 0.8
                to: 1.08
                duration: 150
                easing.type: Easing.OutBack
                easing.overshoot: 1.5
            }
            NumberAnimation {
                target: frameTransitionPill
                property: "scale"
                from: 1.08
                to: 1.0
                duration: 300
                easing.type: Easing.OutBack
                easing.overshoot: 1.5
            }
            PauseAnimation { duration: 1500 }
            NumberAnimation {
                target: frameTransitionPill
                property: "opacity"
                to: 0
                duration: 800
                easing.type: Easing.InQuad
            }
        }

        Text {
            id: frameTransitionLabel
            anchors.centerIn: parent
            color: Theme.textColor
            font.family: Theme.bodyFont.family
            font.pixelSize: Theme.bodyFont.pixelSize
            Accessible.ignored: true
        }
    }

    // Stop button for headless machines (prominently placed above info bar)
    Rectangle {
        id: espressoStopButton
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: infoBar.top
        anchors.bottomMargin: Theme.scaled(20)
        width: Theme.scaled(200)
        height: Theme.scaled(60)
        visible: DE1Device.isHeadless
        radius: Theme.cardRadius
        color: stopTapHandler.isPressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
        border.color: "white"
        border.width: Theme.scaled(2)

        Text {
            anchors.centerIn: parent
            text: TranslationManager.translate("espresso.button.stop", "STOP")
            color: "white"
            font.pixelSize: Theme.scaled(24)
            font.weight: Font.Bold
            Accessible.ignored: true
        }

        AccessibleTapHandler {
            id: stopTapHandler
            anchors.fill: parent
            accessibleName: TranslationManager.translate("espresso.accessible.stopShot", "Stop espresso shot")
            accessibleItem: espressoStopButton
            onAccessibleClicked: {
                root.stopReason = "manual"
                DE1Device.stopOperation()
                root.goToIdle()
            }
        }
    }

    // Tappable legend to toggle graph lines (only visible in chart mode)
    GraphLegend {
        id: graphLegend
        graph: extractionViewLoader.item || {}
        liveMode: true
        advancedMode: espressoPage.advancedMode
        visible: espressoPage.extractionViewMode === "chart"
        width: parent.width
        anchors.bottom: espressoStopButton.visible ? espressoStopButton.top : infoBar.top
        anchors.bottomMargin: Theme.spacingSmall
    }

    // Bottom info bar with live values
    Rectangle {
        id: infoBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.scaled(100)
        color: Qt.darker(Theme.surfaceColor, 1.3)

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium

            // Back button (square hitbox, full height)
            Item {
                id: espressoBackButton
                Layout.fillHeight: true
                Layout.preferredWidth: height

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("espresso.accessible.stop", "Stop and go back")
                Accessible.focusable: true

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/back.svg"
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textColor
                    }
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate("espresso.accessible.stopAndGoBack", "Stop shot and go back")
                    accessibleItem: espressoBackButton
                    onAccessibleClicked: {
                        root.stopReason = "manual"
                        DE1Device.stopOperation()
                        root.goToIdle()
                    }
                }
            }

            // Timer
            ColumnLayout {
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(100)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.time", "Time:") + " " + MachineState.shotTime.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.seconds", "seconds")

                Text {
                    text: MachineState.shotTime.toFixed(1) + "s"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(36)
                    font.weight: Font.Bold
                    Accessible.ignored: true
                }
                Tr {
                    key: "espresso.label.time"
                    fallback: "Time"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
            }

            // Divider
            Rectangle {
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(1)
                Layout.fillHeight: true
                Layout.topMargin: Theme.chartMarginSmall
                Layout.bottomMargin: Theme.chartMarginSmall
                color: Theme.textSecondaryColor
                opacity: 0.3
            }

            // Pressure
            ColumnLayout {
                id: pressureColumn
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                property real goal: MainController.filteredGoalPressure
                property bool trackReady: goal > 0
                property real delta: trackReady ? Math.abs(DE1Device.pressure - goal) : 0
                property color trackColor: trackReady ? Theme.trackingColor(delta, goal, true) : Theme.textSecondaryColor

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.pressure", "Pressure:") + " " + DE1Device.pressure.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.bar", "bar")

                Text {
                    text: DE1Device.pressure.toFixed(1)
                    color: Theme.pressureColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                RowLayout {
                    spacing: Theme.scaled(4)
                    Rectangle {
                        width: Theme.scaled(6)
                        height: Theme.scaled(6)
                        radius: Theme.scaled(3)
                        color: pressureColumn.trackColor
                        visible: pressureColumn.trackReady
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Text {
                        text: pressureColumn.trackReady
                            ? "→" + pressureColumn.goal.toFixed(1) + " " + TranslationManager.translate("espresso.unit.bar", "bar")
                            : TranslationManager.translate("espresso.unit.bar", "bar")
                        color: pressureColumn.trackReady ? pressureColumn.trackColor : Theme.textSecondaryColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }
                }
            }

            // Flow
            ColumnLayout {
                id: flowColumn
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                property real goal: MainController.filteredGoalFlow
                property bool trackReady: goal > 0
                property real delta: trackReady ? Math.abs(DE1Device.flow - goal) : 0
                property color trackColor: trackReady ? Theme.trackingColor(delta, goal, false) : Theme.textSecondaryColor

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.flow", "Flow:") + " " + DE1Device.flow.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.mlPerSec", "milliliters per second")

                Text {
                    text: DE1Device.flow.toFixed(1)
                    color: Theme.flowColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                RowLayout {
                    spacing: Theme.scaled(4)
                    Rectangle {
                        width: Theme.scaled(6)
                        height: Theme.scaled(6)
                        radius: Theme.scaled(3)
                        color: flowColumn.trackColor
                        visible: flowColumn.trackReady
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Text {
                        text: flowColumn.trackReady
                            ? "→" + flowColumn.goal.toFixed(1) + " " + TranslationManager.translate("espresso.unit.flowRate", "mL/s")
                            : TranslationManager.translate("espresso.unit.flowRate", "mL/s")
                        color: flowColumn.trackReady ? flowColumn.trackColor : Theme.textSecondaryColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }
                }
            }

            // Temperature
            ColumnLayout {
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.temperature", "Temperature:") + " " + DE1Device.temperature.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.degrees", "degrees")

                Text {
                    text: DE1Device.temperature.toFixed(1)
                    color: Theme.temperatureColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                Tr {
                    key: "espresso.unit.celsius"
                    fallback: "°C"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
            }

            // Weight flow rate (delta weight from scale)
            ColumnLayout {
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.weightFlow", "Weight flow:") + " " + MachineState.smoothedScaleFlowRate.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.gramsPerSecond", "grams per second")

                Text {
                    text: MachineState.smoothedScaleFlowRate.toFixed(1)
                    color: Theme.weightFlowColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                Tr {
                    key: "espresso.unit.weightFlow"
                    fallback: "g/s"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
            }

            // Divider
            Rectangle {
                visible: espressoPage.showStats
                Layout.preferredWidth: Theme.scaled(1)
                Layout.fillHeight: true
                Layout.topMargin: Theme.chartMarginSmall
                Layout.bottomMargin: Theme.chartMarginSmall
                color: Theme.textSecondaryColor
                opacity: 0.3
            }

            // Weight or Volume with progress
            ColumnLayout {
                id: weightVolumeColumn
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                // Show volume display when only volume target is set (no weight target)
                readonly property bool isVolumeMode: MachineState.targetVolume > 0 && MachineState.targetWeight <= 0
                readonly property double currentValue: isVolumeMode ? MachineState.pourVolume : espressoPage.currentWeight
                readonly property double targetValue: isVolumeMode ? MachineState.targetVolume : ProfileManager.targetWeight
                readonly property string unit: isVolumeMode ? "ml" : "g"
                readonly property color displayColor: isVolumeMode ? Theme.flowColor : Theme.weightColor

                Accessible.role: Accessible.StaticText
                Accessible.name: {
                    var parts = []
                    if (MachineState.targetWeight > 0)
                        parts.push(TranslationManager.translate("espresso.accessible.weight", "Weight:") + " " + espressoPage.currentWeight.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.of", "of") + " " + ProfileManager.targetWeight.toFixed(0) + " " + TranslationManager.translate("espresso.accessible.grams", "grams"))
                    if (MachineState.targetVolume > 0)
                        parts.push(TranslationManager.translate("espresso.accessible.volume", "Volume:") + " " + MachineState.pourVolume.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.of", "of") + " " + MachineState.targetVolume.toFixed(0) + " " + TranslationManager.translate("espresso.accessible.milliliters", "milliliters"))
                    return parts.join(", ") || TranslationManager.translate("espresso.noStopTarget", "No stop target")
                }

                RowLayout {
                    visible: espressoPage.showStats
                    spacing: Theme.spacingSmall

                    Text {
                        text: weightVolumeColumn.currentValue.toFixed(1)
                        color: weightVolumeColumn.displayColor
                        font.pixelSize: Theme.scaled(28)
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignBaseline
                        Accessible.ignored: true
                    }
                    Text {
                        text: ProfileManager.brewByRatioActive && !weightVolumeColumn.isVolumeMode
                            ? "1:" + ProfileManager.brewByRatio.toFixed(1) + " (" + ProfileManager.targetWeight.toFixed(0) + "g)"
                            : "/ " + weightVolumeColumn.targetValue.toFixed(0) + " " + weightVolumeColumn.unit
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(18)
                        Layout.alignment: Qt.AlignBaseline
                        Accessible.ignored: true
                    }
                }

                RowLayout {
                    visible: espressoPage.showStats
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    ProgressBar {
                        id: weightVolumeProgressBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.spacingSmall
                        from: 0
                        to: weightVolumeColumn.targetValue
                        value: weightVolumeColumn.currentValue

                        background: Rectangle {
                            color: Theme.surfaceColor
                            radius: Theme.scaled(4)
                        }

                        contentItem: Item {
                            Rectangle {
                                width: weightVolumeProgressBar.visualPosition * parent.width
                                height: parent.height
                                radius: Theme.scaled(4)
                                color: weightVolumeColumn.displayColor
                            }
                        }
                    }
                }
            }

            // Skip to next profile frame
            Rectangle {
                id: skipFrameButton
                Layout.preferredWidth: Theme.scaled(56)
                Layout.preferredHeight: Theme.scaled(24)
                Layout.alignment: Qt.AlignVCenter
                radius: Theme.scaled(12)
                color: skipFrameTapHandler.pressed ? Qt.darker(Theme.accentColor, 1.3) : Theme.accentColor
                visible: MachineState.phase === MachineStateType.Phase.Preinfusion ||
                         MachineState.phase === MachineStateType.Phase.Pouring

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("espresso.accessible.skipFrame", "Skip to next frame")
                Accessible.focusable: true
                Accessible.onPressAction: skipFrameTapHandler.tapped(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("espresso.button.skip", "Skip")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(11)
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }

                TapHandler {
                    id: skipFrameTapHandler
                    onTapped: DE1Device.skipToNextFrame()
                }
            }
        }
    }

    // Accessibility: Swipe gesture detection on info bar (excludes back button)
    MouseArea {
        id: infoBarSwipeArea
        anchors.fill: infoBar
        anchors.leftMargin: Theme.scaled(80)  // Don't cover back button
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        propagateComposedEvents: true

        property real startX: 0
        property real startY: 0
        property bool swiped: false

        onPressed: function(mouse) {
            startX = mouse.x
            startY = mouse.y
            swiped = false
        }

        onReleased: function(mouse) {
            var deltaX = mouse.x - startX
            var deltaY = mouse.y - startY
            var threshold = 50

            // Horizontal swipe (left or right)
            if (Math.abs(deltaX) > threshold && Math.abs(deltaX) > Math.abs(deltaY)) {
                swiped = true
                if (deltaX > 0) {
                    espressoPage.announceNextValue()
                } else {
                    espressoPage.announcePreviousValue()
                }
            }
        }

        onClicked: function(mouse) {
            // Pass through non-swipe taps to elements below
            if (!swiped) {
                mouse.accepted = false
            }
        }
    }

    // Two-finger tap for full status announcement (excludes back button)
    MultiPointTouchArea {
        anchors.fill: infoBar
        anchors.leftMargin: Theme.scaled(80)  // Don't cover back button
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        minimumTouchPoints: 2
        maximumTouchPoints: 2

        onPressed: function(touchPoints) {
            if (touchPoints.length === 2) {
                espressoPage.announceFullStatus()
            }
        }
    }

    // Swipe gestures on chart for accessibility (swipe left/right to navigate values)
    MouseArea {
        id: chartTapArea
        anchors.fill: extractionViewLoader
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

        property real startX: 0
        property real startY: 0

        onPressed: function(mouse) {
            startX = mouse.x
            startY = mouse.y
        }

        onReleased: function(mouse) {
            var deltaX = mouse.x - startX
            var deltaY = mouse.y - startY
            var threshold = 50

            // Swipe gesture for accessibility navigation
            if (Math.abs(deltaX) > threshold && Math.abs(deltaX) > Math.abs(deltaY)) {
                if (deltaX > 0) {
                    espressoPage.announceNextValue()
                } else {
                    espressoPage.announcePreviousValue()
                }
            }
        }
    }

    // Two-finger tap on chart for full status announcement
    MultiPointTouchArea {
        anchors.fill: extractionViewLoader
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        minimumTouchPoints: 2
        maximumTouchPoints: 2

        onPressed: function(touchPoints) {
            if (touchPoints.length === 2) {
                espressoPage.announceFullStatus()
            }
        }
    }

}
