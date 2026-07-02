import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../components"

Page {
    id: steamPage
    objectName: "steamPage"
    background: Rectangle { color: Theme.backgroundColor }

    property string pageTitle: steamPageTitle.text
    Tr { id: steamPageTitle; key: "steam.title"; fallback: "Steam"; visible: false }

    // Use StackView.onActivated (not Component.onCompleted) so side effects
    // run when the page is actually shown, not during construction. This
    // also re-fires on pop-back if the page is ever pushed below another.
    // Gate the preset-reset and heater-start on !isSteaming so a re-activation
    // mid-session doesn't clobber the user's in-progress settings or re-issue
    // a redundant BLE write.
    StackView.onActivated: {
        root.currentPageTitle = pageTitle
        if (!isSteaming) {
            var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
            if (preset && preset.disabled) {
                // Selected preset is an "Off" pill — leave the heater off rather
                // than kicking it on as the page activates. Don't forceActiveFocus
                // on the hidden duration slider; let the first visible interactive
                // element (the pill row) take default focus.
                MainController.turnOffSteamHeater()
            } else {
                // Sync Settings with selected preset
                steamPage.syncSteamTimeout()
                Settings.brew.steamFlow = getCurrentPitcherFlow()
                Settings.brew.steamTemperature = getCurrentPitcherTemperature()
                steamTempSlider.value = getCurrentPitcherTemperature()
                // Start heating steam heater (ignores keepSteamHeaterOn - user wants to steam)
                // startSteamHeating clears steamDisabled flag automatically
                MainController.startSteamHeating("steampage-activated")
                durationSlider.forceActiveFocus()
            }
        }
    }

    property bool isSteaming: MachineState.phase === MachineStateType.Phase.Steaming || root.debugLiveView
    property int editingPitcherIndex: -1  // For the edit popup
    property bool steamSoftStopped: false  // For two-stage stop on headless machines
    property bool wasSteaming: false  // Track if we were steaming (to turn off heater after)

    // Check if steam heater needs heating
    readonly property real currentSteamTemp: DE1Device.steamTemperature
    readonly property real targetSteamTemp: Settings.brew.steamTemperature
    readonly property bool isHeatingUp: !isSteaming && currentSteamTemp < (targetSteamTemp - 5)  // 5°C tolerance

    // Check if DE1 is in Steam state but still heating (FinalHeating/Heating substate)
    // DE1::State::Steam = 5
    readonly property bool isSteamHeating: DE1Device.state === 5 && !isSteaming

    // Check if DE1 is in Puffing state (waiting for user to press STOP or auto-flush)
    // DE1::SubState::Puffing = 20
    readonly property bool isPuffing: DE1Device.state === 5 && DE1Device.subState === 20

    // Reactive: is the currently selected preset an "Off" pill? Duration/flow/temp
    // sliders don't apply to Off presets so they're hidden in the settings frame.
    // References selectedSteamPitcher and steamPitcherPresets so the binding
    // re-evaluates on selection change or when the preset list itself is edited.
    readonly property bool currentPitcherDisabled: {
        var _selected = Settings.brew.selectedSteamPitcher
        var _list = Settings.brew.steamPitcherPresets
        var preset = Settings.brew.getSteamPitcherPreset(_selected)
        return preset ? preset.disabled === true : false
    }

    // Debug logging for steam phase issues
    Connections {
        target: MachineState
        function onPhaseChanged() {
            console.log("SteamPage: MachineState.phase changed to", MachineState.phase, "isSteaming=", isSteaming)
        }
    }
    Connections {
        target: DE1Device
        function onStateChanged() {
            console.log("SteamPage: DE1Device.state changed to", DE1Device.stateString, "(", DE1Device.state, ")")
        }
        function onSubStateChanged() {
            console.log("SteamPage: DE1Device.subState changed to", DE1Device.subStateString)
        }
    }

    // Last net-milk reading while the pitcher rested on the scale this session, used to
    // apply the weight-scaled steam time at steam-start even after the pitcher is lifted
    // to the wand. Decoupled from the auto-capture's settle detector, whose virtual zero
    // can fail to seed when the loaded pitcher never leaves the scale. Reset at session end.
    property real lastOnScaleMilk: 0
    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            if (!steamPage.isSteaming) {
                var m = steamPage.currentMeasuredMilk()
                if (m > 0) steamPage.lastOnScaleMilk = m
            }
        }
    }

    // Reset state when steaming starts/ends
    onIsSteamingChanged: {
        console.log("SteamPage: isSteaming changed to", isSteaming, "phase=", MachineState.phase, "steamSoftStopped=", steamSoftStopped)
        if (isSteaming) {
            wasSteaming = true
            steamSoftStopped = false
            _lastAnnouncedSteamWeight = 0
            // Apply the weight-scaled steam time NOW, at steam-start, using the same direct
            // calc as the "Expected steam time" readout (falling back to the last on-scale
            // reading once the pitcher is lifted). The auto-capture can't be relied on here,
            // so this is what actually makes the steam scale. setSteamTimeoutImmediate pushes
            // it to the DE1 and takes effect even mid-steam.
            // Respect a manual ±5 override: if the user dialed the time by hand, don't
            // overwrite it with the weight-scaled value at steam-start. Otherwise use the
            // live scaled time, falling back to the milk captured this session once the
            // pitcher is lifted to the wand.
            var _scaledNow = steamPage.steamTimeoutUserAdjusted ? 0 : steamPage.scaledSteamTimeout()
            if (_scaledNow <= 0 && !steamPage.steamTimeoutUserAdjusted) {
                var _cm = steamPage.capturedMilkForScaling()
                if (_cm > 0) _scaledNow = steamPage.steamTimeForMilk(_cm)
            }
            if (_scaledNow > 0) {
                Settings.brew.steamTimeout = _scaledNow
                steamPage.steamTimeoutScaled = true
                MainController.setSteamTimeoutImmediate(_scaledNow)
            }
            // Drop any banner left over from a prior session so it doesn't
            // carry into the new one. SteamHealthTracker re-arms its per-session
            // latch at the first sample of the new session, so if the new
            // session also trips a threshold, the banner will re-show.
            warningVisible = false
            // Same for the post-session modal warning. A new session takes
            // precedence; the dialog's onClosed checks isSteaming and skips
            // the navigation back to idle so we stay on the steam page.
            if (steamWarningDialog.opened) {
                steamWarningDialog.close()
            }
            // Preset reset runs in the else branch (at session end), not here.
            // Resetting at session start meant Settings was stale during the
            // window between state=Steam (when main.qml's onStateChanged fires
            // startSteamHeating with current Settings) and the isSteaming=true
            // transition. If the user stayed on SteamPage and GHC-started a
            // new session, that window carried prior +5s adjustments to the DE1
            // silently. Resetting at session end means Settings is already at
            // preset before the next onStateChanged fires, so the BLE write
            // that session uses the correct values.
        } else {
            console.log("SteamPage: Settings view now visible (isSteaming=false)")
            if (wasSteaming) {
                // Discard +5s/-5s adjustments made during this session so the
                // next one starts from the pitcher preset.
                //
                // When keepSteamHeaterOn=false the sendSteamTemperature(0) call
                // below re-writes ShotSettings with the reset timeout as a side
                // effect (it reads Settings.brew.steamTimeout after the assignment),
                // so the DE1 is in sync immediately. When keepSteamHeaterOn=true
                // the reset just persists to Settings; the next session's
                // onStateChanged->startSteamHeating picks it up and writes it
                // (the DE1's commanded state between sessions is idle anyway,
                // so the lag is invisible).
                //
                // A fresh capture is required for the next session — clear the
                // scaled flag so syncSteamTimeout() falls back to the preset
                // duration here instead of reusing this session's scaled time
                // (otherwise a small pour after a large one would over-steam).
                // Also drop any manual ±5 override: it applied to the session that
                // just ended, so the next pour re-arms weight scaling.
                steamPage.steamTimeoutScaled = false
                steamPage.steamTimeoutUserAdjusted = false
                steamPage.lastOnScaleMilk = 0
                steamPage.syncSteamTimeout()
                Settings.brew.steamFlow = getCurrentPitcherFlow()
                if (!Settings.brew.keepSteamHeaterOn) {
                    console.log("SteamPage: Turning off steam heater (keepSteamHeaterOn=false)")
                    MainController.sendSteamTemperature(0)  // also sets steamDisabled=true
                }
            }
            wasSteaming = false
        }
    }

    // Helper to format flow as readable value (handles undefined/NaN)
    // Steam flow is stored as 0.01 ml/s units (e.g., 150 = 1.5 ml/s)
    function flowToDisplay(flow) {
        if (flow === undefined || flow === null || isNaN(flow)) {
            return "1.50"  // Default
        }
        return (flow / 100).toFixed(2)
    }

    // Get current pitcher's values with defaults. "Off" (disabled) presets
    // don't carry duration/flow, so fall back to the current Settings so the
    // sliders don't jump when the user switches to an Off preset.
    function getCurrentPitcherDuration() {
        var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        if (!preset || preset.disabled) return Settings.brew.steamTimeout ?? 30
        return preset.duration
    }

    function getCurrentPitcherFlow() {
        var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        if (!preset || preset.disabled) return Settings.brew.steamFlow ?? 150
        return (preset.flow !== undefined) ? preset.flow : 150
    }

    // Per-pitcher steam temperature. Disabled ("Off") presets and legacy presets
    // with no stored temperature fall back to the current global steam temperature.
    function getCurrentPitcherTemperature() {
        var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        if (!preset || preset.disabled) return Settings.brew.steamTemperature
        return (preset.temperature !== undefined) ? preset.temperature : Settings.brew.steamTemperature
    }

    function getCurrentPitcherName() {
        var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        return preset ? preset.name : ""
    }

    function isCurrentPitcherDisabled() {
        var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        return preset ? preset.disabled === true : false
    }

    // Save current pitcher with new values. No-op for disabled presets — their
    // duration/flow are meaningless, and writing them via updateSteamPitcherPreset
    // would let slider drags silently bake values into the saved JSON.
    function saveCurrentPitcher(duration, flow, temperature) {
        var name = getCurrentPitcherName()
        if (name && !isCurrentPitcherDisabled()) {
            var temp = (temperature !== undefined) ? temperature : steamTempSlider.value
            Settings.brew.updateSteamPitcherPreset(Settings.brew.selectedSteamPitcher, name, duration, flow, temp)
        }
    }

    // --- Weight-scaled steaming (calibrated presets) -------------------------
    // Thin QML wrappers over the single source of truth in SettingsBrew, so the
    // scaling math, bounds, clamp, and the weight-timing toggle live in one place.

    // Net milk currently on the scale for the selected pitcher (0 if none/invalid).
    function currentMeasuredMilk() {
        if (!ScaleDevice || !ScaleDevice.connected) return 0
        return Settings.brew.netMilkForPitcher(Settings.brew.selectedSteamPitcher, MachineState.scaleWeight)
    }

    // Scaled steam time for the milk currently on the scale (0 → use fixed duration).
    function scaledSteamTimeout() {
        return Settings.brew.scaledSteamTime(Settings.brew.selectedSteamPitcher, currentMeasuredMilk())
    }

    // Scaled steam time for a specific captured milk weight (0 → use fixed duration).
    function steamTimeForMilk(milk) {
        return Settings.brew.scaledSteamTime(Settings.brew.selectedSteamPitcher, milk)
    }

    // Selected preset's reference milk weight (the baseline paired with its duration).
    function getCurrentPitcherCalibMilk() {
        var _ = Settings.brew.steamPitcherPresets  // track changes so the field refreshes after Weigh
        var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        return (preset && !preset.disabled) ? (preset.calibMilkG ?? 0) : 0
    }

    // True once a milk capture has scaled steamTimeout for the CURRENT pitcher
    // selection. syncSteamTimeout() preserves that scaled value while the pitcher
    // is lifted to the wand, but a stale value (e.g. after switching pitcher) is
    // dropped back to the preset's baseline duration instead of being reused.
    property bool steamTimeoutScaled: false
    Connections {
        target: Settings.brew
        function onSelectedSteamPitcherChanged() {
            steamPage.steamTimeoutScaled = false
            steamPage.steamTimeoutUserAdjusted = false
            steamPage.lastOnScaleMilk = 0   // captured net milk is pitcher-specific
        }
    }

    // Set when the user nudges the steam time with the ±5 buttons. While set, the
    // auto milk-capture won't overwrite their chosen time. Cleared when the pitcher
    // leaves the scale (capture re-arms) or the pitcher selection changes, so a fresh
    // placement re-enables weight scaling. Event-based, not a timer.
    property bool steamTimeoutUserAdjusted: false

    // Confirm before storing the empty-pitcher weight (footgun: weighing a pitcher
    // that still has milk in it would skew every net-milk reading thereafter).
    property real pendingPitcherWeight: 0
    Dialog {
        id: pitcherWeighConfirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(Theme.scaled(380), parent ? parent.width * 0.9 : Theme.scaled(380))
        standardButtons: Dialog.Save | Dialog.Cancel
        contentItem: Text {
            text: TranslationManager.translate("steam.confirmPitcherWeight",
                "Store %1 g as the empty pitcher weight? Make sure the pitcher is empty (no milk).")
                .arg(steamPage.pendingPitcherWeight.toFixed(1))
            wrapMode: Text.WordWrap
            color: Theme.textColor
            font: Theme.bodyFont
            padding: Theme.spacingLarge
        }
        onAccepted: Settings.brew.setSteamPitcherWeight(Settings.brew.selectedSteamPitcher, steamPage.pendingPitcherWeight)
    }

    // Net milk to scale against once the pitcher is off the scale: prefer the value
    // captured THIS session (set by the home-screen OR steam auto-capture, shared via the
    // window) and fall back to the last reading seen on the steam page. 0 = none yet.
    function capturedMilkForScaling() {
        if (typeof Window !== "undefined" && Window.window && Window.window.sessionMeasuredMilkG > 0)
            return Window.window.sessionMeasuredMilkG
        return steamPage.lastOnScaleMilk
    }

    // Sync steamTimeout to the selected preset WITHOUT clobbering a weight-scaled
    // value. If milk is on the scale now, use the scaled time. If the preset is
    // calibrated but milk isn't on the scale (e.g. lifted to the wand / arriving
    // from the home-screen steam flow), keep the current steamTimeout — it was
    // already scaled from the measured milk. Only fall back to the fixed reference
    // duration when the preset isn't calibrated.
    function syncSteamTimeout() {
        var live = scaledSteamTimeout()
        if (live <= 0) {
            var cap = capturedMilkForScaling()   // pitcher lifted: use the captured milk
            if (cap > 0) live = steamTimeForMilk(cap)
        }
        if (live > 0) {
            Settings.brew.steamTimeout = live
            steamPage.steamTimeoutScaled = true
        } else if (getCurrentPitcherCalibMilk() <= 0 || !steamPage.steamTimeoutScaled) {
            Settings.brew.steamTimeout = getCurrentPitcherDuration()
        }
        // else: calibrated AND scaled for this selection, pitcher just lifted -> keep it
    }

    // Steam view mode: "timer" (default) or "chart"
    property string steamViewMode: Settings.value("steam/steamView", "timer")
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "steam/steamView")
                steamViewMode = Settings.value("steam/steamView", "timer")
        }
    }

    // Live safety warning banner. Persists until the user taps it. The two
    // signals that drive it (pressureTooHigh / temperatureTooHigh) are each
    // latched to fire at most once per session in SteamHealthTracker, so an
    // auto-dismiss would risk the user missing the only alert they get.
    // (The three other SteamHealthTracker signals handled below are
    // post-session modal dialogs, not this banner.)
    property string warningText: ""
    property bool warningVisible: false

    // Warning connections
    Connections {
        target: SteamHealthTracker

        function onPressureTooHigh() {
            warningText = TranslationManager.translate("steam.warning.pressureHigh",
                "Warning: steam pressure is too high")
            warningVisible = true
        }
        function onTemperatureTooHigh() {
            warningText = TranslationManager.translate("steam.warning.temperatureHigh",
                "Warning: steam temperature is too high")
            warningVisible = true
        }
        function onDescaleWarning() {
            openPostSessionWarning(TranslationManager.translate("steam.warning.descale",
                "Your machine may need descaling. Steam pressure was consistently too high."))
        }
        function onTemperatureWarning(message) {
            openPostSessionWarning(message)
        }
        function onScaleBuildupWarning(message) {
            openPostSessionWarning(message)
        }
    }

    // The three SteamHealthTracker post-session signals above fire after the
    // session ends, which is the same moment main.qml's completion overlay is
    // counting down to navigate back to idle (#1302). If we let that timer
    // navigate, the dialog gets destroyed mid-display along with this page.
    // Suspend the completion overlay and let Dialog.onClosed drive the
    // navigation once the user acknowledges the warning.
    //
    // checkTrend() can emit both the pressure and the temperature trend warning
    // in the same call. Dialog.open() on an already-open Dialog is a no-op, so
    // simply setting warningMessage would silently drop the second message —
    // concatenate instead so the user sees every warning that was emitted.
    function openPostSessionWarning(msg) {
        if (steamWarningDialog.opened) {
            if (steamWarningDialog.warningMessage.indexOf(msg) < 0) {
                steamWarningDialog.warningMessage += "\n\n" + msg
            }
            return
        }
        steamWarningDialog.warningMessage = msg
        root.suspendCompletionForDialog()
        steamWarningDialog.open()
    }

    // Post-session warning dialog
    Dialog {
        id: steamWarningDialog
        property string warningMessage: ""
        title: TranslationManager.translate("steam.warning.title", "Steam Warning")
        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(360))
        padding: Theme.spacingMedium
        onOpened: warningOkButton.forceActiveFocus()
        onClosed: {
            // If a new session started while the dialog was up (user hit the
            // hardware GHC button while still acknowledging the prior session's
            // warning), don't navigate to idle — they're steaming again.
            if (isSteaming) return
            root.finishCompletion()
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: steamWarningDialog.warningMessage
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.ignored: true
            }

            AccessibleButton {
                id: warningOkButton
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.button.ok", "OK")
                Layout.alignment: Qt.AlignRight
                onClicked: steamWarningDialog.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin  // Space for bottom bar
        spacing: Theme.scaled(15)

        // === STEAMING VIEW ===
        // Also shown during steam heating (DE1 in Steam state but FinalHeating substate)
        // Stay visible during soft-stop (waiting for purge) and Puffing (auto-flush countdown)
        ColumnLayout {
            visible: isSteaming || steamSoftStopped || isSteamHeating || isPuffing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(20)

            // Top row: preset pills + view toggle button
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Item { Layout.fillWidth: true }

                // Preset pills for quick switching during steaming
                Row {
                    spacing: Theme.scaled(8)

                    Repeater {
                        id: livePresetRepeater
                        model: Settings.brew.steamPitcherPresets

                        Rectangle {
                            id: livePitcherPill
                            readonly property bool pitcherDisabled: modelData.disabled === true
                            readonly property bool pitcherSelected: index === Settings.brew.selectedSteamPitcher

                            // Hide "Off" presets from the mid-session preset row — there's no
                            // meaningful action when tapped mid-steam, so don't show the pill.
                            visible: !(isSteaming && pitcherDisabled)

                            width: livePitcherText.implicitWidth + 24
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: pitcherSelected
                                ? (pitcherDisabled ? Theme.textSecondaryColor : Theme.primaryColor)
                                : Theme.surfaceColor
                            border.color: pitcherSelected && !pitcherDisabled ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: {
                                var label = modelData.name + " " + TranslationManager.translate("steam.accessibility.preset", "preset")
                                if (livePitcherPill.pitcherDisabled)
                                    label += ", " + TranslationManager.translate("steam.accessibility.presetOff", "turns steam heater off")
                                var pitcherWt = modelData.pitcherWeightG ?? 0
                                if (pitcherWt > 0 && !livePitcherPill.pitcherDisabled)
                                    label += ", " + TranslationManager.translate("steam.accessibility.pitcherWeight", "pitcher") + " " + pitcherWt.toFixed(0) + "g"
                                if (livePitcherPill.pitcherSelected)
                                    label += ", " + TranslationManager.translate("accessibility.selected", "selected")
                                return label
                            }
                            Accessible.focusable: true
                            Accessible.onPressAction: livePitcherMa.clicked(null)

                            Keys.onReturnPressed: { livePitcherMa.clicked(null); event.accepted = true }
                            Keys.onSpacePressed:  { livePitcherMa.clicked(null); event.accepted = true }
                            Keys.onLeftPressed: {
                                if (index > 0) livePresetRepeater.itemAt(index - 1).forceActiveFocus()
                                event.accepted = true
                            }
                            Keys.onRightPressed: {
                                if (index < livePresetRepeater.count - 1) livePresetRepeater.itemAt(index + 1).forceActiveFocus()
                                event.accepted = true
                            }
                            Keys.onTabPressed: {
                                if (index < livePresetRepeater.count - 1)
                                    livePresetRepeater.itemAt(index + 1).forceActiveFocus()
                                else if (steamStopButton.visible)
                                    steamStopButton.forceActiveFocus()
                                else
                                    livePresetRepeater.itemAt(0).forceActiveFocus()
                                event.accepted = true
                            }
                            Keys.onBacktabPressed: {
                                if (index > 0)
                                    livePresetRepeater.itemAt(index - 1).forceActiveFocus()
                                else if (steamStopButton.visible)
                                    steamStopButton.forceActiveFocus()
                                else
                                    livePresetRepeater.itemAt(livePresetRepeater.count - 1).forceActiveFocus()
                                event.accepted = true
                            }

                            Text {
                                id: livePitcherText
                                anchors.centerIn: parent
                                text: modelData.name
                                color: livePitcherPill.pitcherSelected
                                    ? Theme.primaryContrastColor
                                    : (livePitcherPill.pitcherDisabled ? Theme.textSecondaryColor : Theme.textColor)
                                font: Theme.bodyFont
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: livePitcherMa
                                anchors.fill: parent
                                onClicked: {
                                    // Off pills are hidden during steaming (visible binding
                                    // above), so ignore any tap that slips through mid-session.
                                    if (isSteaming && livePitcherPill.pitcherDisabled) return
                                    Settings.brew.selectedSteamPitcher = index
                                    if (livePitcherPill.pitcherDisabled) {
                                        MainController.turnOffSteamHeater()
                                        return
                                    }
                                    var flow = modelData.flow !== undefined ? modelData.flow : 150
                                    // Compute the (weight-scaled) target ONCE and use it for both the
                                    // persisted Settings value and the value pushed to the DE1, so the
                                    // UI countdown target and the firmware TargetSteamLength agree.
                                    var targetTimeout = scaledSteamTimeout() > 0 ? scaledSteamTimeout() : modelData.duration
                                    Settings.brew.steamTimeout = targetTimeout
                                    Settings.brew.steamFlow = flow
                                    if (!isSteaming) {
                                        MainController.startSteamHeating("live-pitcher-click")
                                    } else {
                                        // Re-bind the live slider to Settings.brew.steamFlow using
                                        // Qt.binding. The user's first drag on the slider
                                        // imperatively writes value=newVal in onValueModified,
                                        // which permanently destroys the original declarative
                                        // binding — any later Settings.brew.steamFlow change (like
                                        // this preset tap) won't reach the slider. Qt.binding
                                        // restores reactivity without falling back to a bare
                                        // imperative assignment.
                                        steamingFlowSlider.value = Qt.binding(function() { return Settings.brew.steamFlow })
                                        // Push both the new duration (via ShotSettings) and the
                                        // new flow (via MMR) so a mid-session preset switch
                                        // actually changes when steaming stops — matching the
                                        // hot water page's vessel-pill behaviour and the
                                        // existing +5s/-5s buttons. If the new duration is less
                                        // than elapsed, the DE1 firmware will end the session,
                                        // which is the user's intent when picking a smaller
                                        // preset.
                                        MainController.setSteamTimeoutImmediate(targetTimeout)
                                        MainController.setSteamFlowImmediate(flow)
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // View toggle button (graph/timer)
                Rectangle {
                    id: viewToggleBtn
                    width: Theme.scaled(44)
                    height: Theme.scaled(44)
                    radius: Theme.cardRadius
                    color: viewToggleMa.containsMouse ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor

                    activeFocusOnTab: true
                    Accessible.ignored: true
                    Keys.onReturnPressed: { viewToggleMa.accessibleClicked(); event.accepted = true }
                    Keys.onSpacePressed:  { viewToggleMa.accessibleClicked(); event.accepted = true }
                    Keys.onTabPressed: {
                        if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(0).forceActiveFocus()
                        else if (steamStopButton.visible) steamStopButton.forceActiveFocus()
                        event.accepted = true
                    }
                    Keys.onBacktabPressed: {
                        if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(livePresetRepeater.count - 1).forceActiveFocus()
                        else if (steamStopButton.visible) steamStopButton.forceActiveFocus()
                        event.accepted = true
                    }

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/Graph.svg"
                        sourceSize.width: Theme.scaled(24)
                        sourceSize.height: Theme.scaled(24)

                        layer.enabled: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Theme.textColor
                        }
                    }

                    AccessibleMouseArea {
                        id: viewToggleMa
                        anchors.fill: parent
                        hoverEnabled: true
                        accessibleName: TranslationManager.translate("steam.viewToggle.accessibility",
                            "Switch between timer and chart view")
                        accessibleItem: parent
                        onAccessibleClicked: {
                            var newMode = steamViewMode === "timer" ? "chart" : "timer"
                            steamViewMode = newMode
                            Settings.setValue("steam/steamView", newMode)
                        }
                    }
                }
            }

            // Warning banner (live warnings during steaming). Tap to dismiss.
            Rectangle {
                id: warningBanner
                Layout.fillWidth: true
                Layout.preferredHeight: warningBannerText.implicitHeight + Theme.spacingSmall * 2
                visible: warningVisible
                radius: Theme.cardRadius
                color: Theme.errorColor

                // Composed banner text — a single translatable template so
                // translators control word order relative to the warning.
                readonly property string composedText: TranslationManager.translate(
                    "steam.warning.withTapHint", "%1  (tap to dismiss)").arg(warningText)

                Text {
                    id: warningBannerText
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingMedium * 2
                    text: warningBanner.composedText
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleItem: warningBanner
                    accessibleName: warningBanner.composedText
                    onAccessibleClicked: warningVisible = false
                }
            }

            // === TIMER VIEW (default) ===
            ColumnLayout {
                visible: steamViewMode === "timer"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(20)

            Item { Layout.fillHeight: true }

            // Timer with target and adjustment buttons
            Column {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.spacingMedium

                    // Decrease time button (hidden during heating and puffing)
                    Rectangle {
                        id: decreaseTimeBtn
                        visible: !isSteamHeating && !isPuffing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scaled(48)
                        height: width
                        radius: Theme.cardRadius
                        color: decreaseMouseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: 1

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("steam.decreaseTime", "Decrease steam time by 5 seconds")
                        Accessible.focusable: true
                        Accessible.onPressAction: decreaseMouseArea.clicked(null)
                        Keys.onReturnPressed: { decreaseMouseArea.clicked(null); event.accepted = true }
                        Keys.onSpacePressed:  { decreaseMouseArea.clicked(null); event.accepted = true }
                        KeyNavigation.tab: increaseTimeBtn
                        KeyNavigation.backtab: steamingFlowSlider

                        Text {
                            anchors.centerIn: parent
                            text: "-5s"
                            color: Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: decreaseMouseArea
                            anchors.fill: parent
                            onClicked: {
                                var newTime = Math.max(5, Settings.brew.steamTimeout - 5)
                                Settings.brew.steamTimeout = newTime
                                steamPage.steamTimeoutUserAdjusted = true  // don't let auto-capture overwrite a manual nudge
                                if (isSteaming)
                                    MainController.setSteamTimeoutImmediate(newTime)
                                else
                                    MainController.startSteamHeating("decrease-5s")
                            }
                        }
                    }

                    Text {
                        id: steamProgressText
                        // Show temperature during heating, countdown during puffing, time during steaming
                        text: {
                            if (isSteamHeating) {
                                return Math.round(currentSteamTemp) + "°C / " + Math.round(targetSteamTemp) + "°C"
                            } else if (isPuffing && root.steamAutoFlushCountdown > 0) {
                                return root.steamAutoFlushCountdown.toFixed(1) + "s / " + Settings.brew.steamAutoFlushSeconds + "s"
                            } else {
                                return MachineState.shotTime.toFixed(1) + "s / " + Settings.brew.steamTimeout + "s"
                            }
                        }
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Increase time button (hidden during heating and puffing)
                    Rectangle {
                        id: increaseTimeBtn
                        visible: !isSteamHeating && !isPuffing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scaled(48)
                        height: width
                        radius: Theme.cardRadius
                        color: increaseMouseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                        border.color: Theme.borderColor
                        border.width: 1

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("steam.increaseTime", "Increase steam time by 5 seconds")
                        Accessible.focusable: true
                        Accessible.onPressAction: increaseMouseArea.clicked(null)
                        Keys.onReturnPressed: { increaseMouseArea.clicked(null); event.accepted = true }
                        Keys.onSpacePressed:  { increaseMouseArea.clicked(null); event.accepted = true }
                        KeyNavigation.tab: steamingFlowSlider
                        KeyNavigation.backtab: decreaseTimeBtn

                        Text {
                            anchors.centerIn: parent
                            text: "+5s"
                            color: Theme.textColor
                            font: Theme.bodyFont
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: increaseMouseArea
                            anchors.fill: parent
                            onClicked: {
                                var newTime = Math.min(120, Settings.brew.steamTimeout + 5)
                                Settings.brew.steamTimeout = newTime
                                steamPage.steamTimeoutUserAdjusted = true  // don't let auto-capture overwrite a manual nudge
                                if (isSteaming)
                                    MainController.setSteamTimeoutImmediate(newTime)
                                else
                                    MainController.startSteamHeating("increase-5s")
                            }
                        }
                    }
                }

                // Progress bar
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: steamProgressText.width + decreaseTimeBtn.width + increaseTimeBtn.width + Theme.spacingMedium * 2
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    color: Theme.surfaceColor

                    Rectangle {
                        // Show temperature progress during heating, countdown during puffing, time during steaming
                        width: {
                            if (isSteamHeating) {
                                return parent.width * Math.min(1, currentSteamTemp / targetSteamTemp)
                            } else if (isPuffing && Settings.brew.steamAutoFlushSeconds > 0) {
                                // Countdown: progress goes from full to empty
                                return parent.width * Math.min(1, root.steamAutoFlushCountdown / Settings.brew.steamAutoFlushSeconds)
                            } else {
                                return parent.width * Math.min(1, MachineState.shotTime / Settings.brew.steamTimeout)
                            }
                        }
                        height: parent.height
                        radius: Theme.scaled(4)
                        color: isSteamHeating ? Theme.warningColor : (isPuffing ? Theme.secondaryColor : Theme.primaryColor)
                    }
                }

                // Purge button on the live steaming view — stops steam and triggers
                // the DE1 steam-wand purge.
                Rectangle {
                    visible: isSteaming
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.scaled(150)
                    height: Theme.scaled(48)
                    radius: Theme.buttonRadius
                    color: livePurgeMa.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("steam.accessible.purge", "Purge the steam wand")
                    Accessible.focusable: true
                    Accessible.onPressAction: livePurgeMa.clicked(null)
                    Keys.onReturnPressed: { livePurgeMa.clicked(null); event.accepted = true }
                    Keys.onSpacePressed:  { livePurgeMa.clicked(null); event.accepted = true }
                    Tr {
                        anchors.centerIn: parent
                        key: "steam.label.purge"; fallback: "Purge"
                        color: Theme.primaryContrastColor; font: Theme.bodyFont
                        Accessible.ignored: true
                    }
                    MouseArea { id: livePurgeMa; anchors.fill: parent; onClicked: DE1Device.requestIdle() }
                }
            }

            Item { Layout.fillHeight: true }

            // Full-width Steam Flow control
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(40)
                Layout.rightMargin: Theme.scaled(40)
                spacing: Theme.scaled(12)

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    key: "steam.label.steamFlow"
                    fallback: "Steam Flow"
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                }

                ValueInput {
                    id: steamingFlowSlider
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(80)
                    from: 40
                    to: 250
                    stepSize: 5
                    decimals: 0
                    value: Settings.brew.steamFlow
                    displayText: flowToDisplay(value)
                    accessibleName: TranslationManager.translate("steam.label.steamFlow", "Steam Flow")
                    KeyNavigation.tab: steamStopButton.visible ? steamStopButton : (livePresetRepeater.count > 0 ? livePresetRepeater.itemAt(0) : steamingFlowSlider)
                    KeyNavigation.backtab: increaseTimeBtn
                    // BLE write deferred to commit (PR #782 pattern). The single
                    // commit-time MMR write is reliable because setSteamFlowImmediate
                    // routes through writeMMRVerified — write, read-back, retry on
                    // mismatch — so a single caller-side call is enough even when
                    // the firmware's sample-tick loop misses the first write.
                    onValueModified: function(newValue) {
                        steamingFlowSlider.value = newValue
                        saveCurrentPitcher(getCurrentPitcherDuration(), newValue)
                    }
                    onValueCommitted: function(newValue) {
                        MainController.setSteamFlowImmediate(newValue)
                    }
                }

                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    key: "steam.hint.flowHint"
                    fallback: "Low = flat, High = foamy"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            } // end timer view ColumnLayout

            // === CHART VIEW ===
            ColumnLayout {
                visible: steamViewMode === "chart"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(8)

                SteamGraph {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                // Condensed info row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.spacingLarge

                    Text {
                        text: {
                            if (isSteamHeating) {
                                return Math.round(currentSteamTemp) + "°C / " + Math.round(targetSteamTemp) + "°C"
                            } else if (isPuffing && root.steamAutoFlushCountdown > 0) {
                                return root.steamAutoFlushCountdown.toFixed(1) + "s / " + Settings.brew.steamAutoFlushSeconds + "s"
                            } else {
                                return MachineState.shotTime.toFixed(1) + "s / " + Settings.brew.steamTimeout + "s"
                            }
                        }
                        color: Theme.textColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }

                    Text {
                        text: flowToDisplay(Settings.brew.steamFlow) + " mL/s"
                        color: Theme.flowColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }

                    Text {
                        text: Math.round(currentSteamTemp) + "°C"
                        color: Theme.temperatureColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }
                }
            }

            // Stop button for headless machines.
            // When Settings.hardware.steamTwoTapStop is on (default off), behaves as a
            // two-stage button: first tap soft-stops, second tap purges.
            // When off, a single tap stops and triggers the hose purge.
            Rectangle {
                id: steamStopButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(200)
                Layout.preferredHeight: Theme.scaled(60)
                visible: DE1Device.isHeadless
                radius: Theme.cardRadius
                color: stopTapHandler.isPressed
                    ? Qt.darker((steamSoftStopped && Settings.hardware.steamTwoTapStop) ? Theme.primaryColor : Theme.errorColor, 1.2)
                    : ((steamSoftStopped && Settings.hardware.steamTwoTapStop) ? Theme.primaryColor : Theme.errorColor)
                border.color: Theme.primaryContrastColor
                border.width: Theme.scaled(2)

                activeFocusOnTab: true
                Keys.onReturnPressed: { stopTapHandler.accessibleClicked(); event.accepted = true }
                Keys.onSpacePressed:  { stopTapHandler.accessibleClicked(); event.accepted = true }
                Keys.onTabPressed: {
                    if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(0).forceActiveFocus()
                    event.accepted = true
                }
                Keys.onBacktabPressed: {
                    if (livePresetRepeater.count > 0) livePresetRepeater.itemAt(livePresetRepeater.count - 1).forceActiveFocus()
                    event.accepted = true
                }

                Text {
                    id: stopButtonText
                    anchors.centerIn: parent
                    text: (steamSoftStopped && Settings.hardware.steamTwoTapStop) ? "PURGE" : "STOP"
                    color: Theme.primaryContrastColor
                    font.pixelSize: Theme.scaled(24)
                    font.weight: Font.Bold
                    Accessible.ignored: true
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: stopTapHandler
                    anchors.fill: parent
                    accessibleName: steamSoftStopped ? TranslationManager.translate("steam.accessible.purge", "Purge steam wand") : TranslationManager.translate("steam.accessible.stop", "Stop steaming")
                    accessibleItem: steamStopButton
                    onAccessibleClicked: {
                        if (!Settings.hardware.steamTwoTapStop) {
                            // Single-tap mode: stop immediately and trigger auto-purge
                            DE1Device.requestIdle()
                            root.goToIdle()
                        } else if (steamSoftStopped) {
                            // Two-tap mode, second tap: request Idle to trigger purge
                            steamSoftStopped = false  // Reset before navigating
                            DE1Device.requestIdle()
                            root.goToIdle()
                        } else {
                            // Two-tap mode, first tap: soft stop steam without purge
                            // Sends 1-second timeout which triggers elapsed > target stop
                            MainController.softStopSteam()
                            steamSoftStopped = true
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(20) }
        }

        // === SETTINGS VIEW ===
        // Hide during soft-stop (waiting for purge), steam heating, and puffing
        ColumnLayout {
            visible: !isSteaming && !steamSoftStopped && !isSteamHeating && !isPuffing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Steam heater heating indicator
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(60)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: isHeatingUp

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(15)

                    // Heating icon (animated)
                    Text {
                        text: "\ue88a"  // heating icon (whatshot)
                        font.family: "Material Icons"
                        font.pixelSize: Theme.scaled(28)
                        color: Theme.warningColor

                        SequentialAnimation on opacity {
                            running: isHeatingUp
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.4; to: 1.0; duration: 600 }
                            NumberAnimation { from: 1.0; to: 0.4; duration: 600 }
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "steam.label.heatingUp"
                            fallback: "Heating steam..."
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }

                        // Progress bar
                        Rectangle {
                            width: parent.width
                            height: Theme.scaled(6)
                            radius: Theme.scaled(3)
                            color: Theme.backgroundColor

                            Rectangle {
                                width: parent.width * Math.min(1, Math.max(0, currentSteamTemp / targetSteamTemp))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.warningColor
                            }
                        }
                    }

                    // Temperature display
                    Text {
                        text: currentSteamTemp.toFixed(0) + " / " + targetSteamTemp.toFixed(0) + "°C"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(14)
                    }
                }
            }

            // Pitcher Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "steam.label.pitcherPreset"
                        fallback: "Pitcher Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Pitcher preset buttons with drag-and-drop
                    Row {
                        id: pitcherPresetsRow
                        spacing: Theme.scaled(8)

                        property int draggedIndex: -1

                        Repeater {
                            id: pitcherRepeater
                            model: Settings.brew.steamPitcherPresets

                            Item {
                                id: pitcherDelegate
                                width: pitcherPill.width
                                height: Theme.scaled(36)

                                property int pitcherIndex: index
                                property Item focusTarget: pitcherPill

                                Rectangle {
                                    id: pitcherPill
                                    readonly property bool pitcherDisabled: modelData.disabled === true
                                    readonly property bool pitcherSelected: pitcherDelegate.pitcherIndex === Settings.brew.selectedSteamPitcher

                                    width: pitcherText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: pitcherSelected
                                        ? (pitcherDisabled ? Theme.textSecondaryColor : Theme.primaryColor)
                                        : Theme.backgroundColor
                                    border.color: pitcherSelected && !pitcherDisabled ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    function applyPitcher(reason) {
                                        Settings.brew.selectedSteamPitcher = pitcherDelegate.pitcherIndex
                                        if (pitcherDisabled) {
                                            MainController.turnOffSteamHeater()
                                            return
                                        }
                                        var flow = modelData.flow !== undefined ? modelData.flow : 150
                                        durationSlider.value = modelData.duration
                                        flowSlider.value = flow
                                        // Compute once so the test and the value can't disagree on a fresh telemetry tick.
                                        var scaled = scaledSteamTimeout()
                                        Settings.brew.steamTimeout = scaled > 0 ? scaled : modelData.duration
                                        Settings.brew.steamFlow = flow
                                        MainController.startSteamHeating(reason)
                                    }

                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: {
                                        var label = modelData.name + " " + TranslationManager.translate("steam.accessibility.preset", "preset")
                                        if (pitcherDisabled)
                                            label += ", " + TranslationManager.translate("steam.accessibility.presetOff", "turns steam heater off")
                                        var pitcherWt = modelData.pitcherWeightG ?? 0
                                        if (pitcherWt > 0 && !pitcherDisabled)
                                            label += ", " + TranslationManager.translate("steam.accessibility.pitcherWeight", "pitcher") + " " + pitcherWt.toFixed(0) + "g"
                                        if (pitcherSelected)
                                            label += ", " + TranslationManager.translate("accessibility.selected", "selected")
                                        return label
                                    }
                                    Accessible.description: TranslationManager.translate("steam.accessibility.pitcherEditHint", "Double-tap or long-press to edit preset.")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: pitcherPill.applyPitcher("pitcher-a11y")

                                    Keys.onReturnPressed: {
                                        pitcherPill.applyPitcher("pitcher-return")
                                        event.accepted = true
                                    }
                                    Keys.onSpacePressed: {
                                        pitcherPill.applyPitcher("pitcher-space")
                                        event.accepted = true
                                    }
                                    Keys.onLeftPressed: {
                                        if (index > 0) pitcherRepeater.itemAt(index - 1).focusTarget.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onRightPressed: {
                                        if (index < pitcherRepeater.count - 1) pitcherRepeater.itemAt(index + 1).focusTarget.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onTabPressed: {
                                        if (index < pitcherRepeater.count - 1)
                                            pitcherRepeater.itemAt(index + 1).focusTarget.forceActiveFocus()
                                        else
                                            addPitcherButton.forceActiveFocus()
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: {
                                        if (index > 0)
                                            pitcherRepeater.itemAt(index - 1).focusTarget.forceActiveFocus()
                                        else if (ScaleDevice.connected && !ScaleDevice.isFlowScale)
                                            savePitcherWeightBtn.forceActiveFocus()
                                        else
                                            steamTempSlider.forceActiveFocus()
                                        event.accepted = true
                                    }

                                    Drag.active: dragArea.drag.active
                                    Drag.source: pitcherDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: dragArea.drag.active
                                        ParentChange { target: pitcherPill; parent: pitcherPresetsRow }
                                        AnchorChanges { target: pitcherPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: pitcherText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: pitcherPill.pitcherSelected
                                            ? Theme.primaryContrastColor
                                            : (pitcherPill.pitcherDisabled ? Theme.textSecondaryColor : Theme.textColor)
                                        font: Theme.bodyFont
                                        Accessible.ignored: true
                                    }

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: pitcherPill
                                        drag.axis: Drag.XAxis

                                        property bool held: false
                                        property bool moved: false

                                        onPressed: {
                                            held = false
                                            moved = false
                                            holdTimer.start()
                                        }

                                        onReleased: {
                                            holdTimer.stop()
                                            if (!moved && !held) {
                                                // Simple click - select the pitcher
                                                pitcherPill.applyPitcher("pitcher-click")
                                            }
                                            pitcherPill.Drag.drop()
                                            pitcherPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                pitcherPresetsRow.draggedIndex = pitcherDelegate.pitcherIndex
                                            }
                                        }

                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            held = true  // Prevent single-click selection on release
                                            editingPitcherIndex = pitcherDelegate.pitcherIndex
                                            editPitcherNameInput.text = modelData.name
                                            editPitcherPopup.open()
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved) {
                                                    dragArea.held = true
                                                    editingPitcherIndex = pitcherDelegate.pitcherIndex
                                                    editPitcherNameInput.text = modelData.name
                                                    editPitcherPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.pitcherIndex
                                        var toIndex = pitcherDelegate.pitcherIndex
                                        if (fromIndex !== toIndex) {
                                            Settings.brew.moveSteamPitcherPreset(fromIndex, toIndex)
                                        }
                                    }
                                }
                            }
                        }

                        // Add button
                        Rectangle {
                            id: addPitcherButton
                            width: Theme.scaled(36)
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            activeFocusOnTab: true
                            KeyNavigation.tab: durationSlider
                            KeyNavigation.backtab: pitcherRepeater.count > 0
                                ? pitcherRepeater.itemAt(pitcherRepeater.count - 1).focusTarget
                                : durationSlider
                            Keys.onReturnPressed: { addPitcherDialog.open(); event.accepted = true }
                            Keys.onSpacePressed:  { addPitcherDialog.open(); event.accepted = true }

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                                Accessible.ignored: true
                            }

                            // Using TapHandler for better touch responsiveness
                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("steam.accessible.addPreset", "Add new steam preset")
                                accessibleItem: addPitcherButton
                                onAccessibleClicked: addPitcherDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        key: "steam.hint.presetReorder"
                        fallback: "Drag to reorder, hold or double-click to edit"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
            }

            // Settings frame
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                Flickable {
                    id: editorFlick
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(16)
                    contentWidth: width
                    contentHeight: editorColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: editorColumn
                    width: editorFlick.width
                    spacing: Theme.scaled(8)

                    // Centered placeholder when the selected preset is an "Off" pill —
                    // duration/flow/temperature/weight don't apply, so the slider rows
                    // below are all hidden and we show this in their place.
                    Item {
                        visible: steamPage.currentPitcherDisabled
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(160)

                        Tr {
                            anchors.centerIn: parent
                            key: "steam.offPresetNotice"
                            fallback: "Steam heater off for this preset."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(20)
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    // Purge: clear water / condensation from the steam wand.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !steamPage.currentPitcherDisabled
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(150)
                            Layout.preferredHeight: Theme.scaled(48)
                            radius: Theme.buttonRadius
                            color: purgeMa.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("steam.accessible.purge", "Purge the steam wand")
                            Accessible.focusable: true
                            Accessible.onPressAction: purgeMa.clicked(null)
                            Keys.onReturnPressed: { purgeMa.clicked(null); event.accepted = true }
                            Keys.onSpacePressed:  { purgeMa.clicked(null); event.accepted = true }
                            Tr {
                                anchors.centerIn: parent
                                key: "steam.label.purge"; fallback: "Purge"
                                color: Theme.primaryContrastColor; font: Theme.bodyFont
                                Accessible.ignored: true
                            }
                            MouseArea { id: purgeMa; anchors.fill: parent; onClicked: DE1Device.requestIdle() }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Duration (per-pitcher, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled

                        Tr {
                            key: "steam.label.duration"
                            fallback: "Duration"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(24)
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: durationSlider
                            Layout.preferredWidth: Theme.scaled(180)
                            from: 1
                            to: 120
                            stepSize: 1
                            decimals: 0
                            suffix: " s"
                            value: getCurrentPitcherDuration()
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.duration", "Duration")
                            KeyNavigation.tab: flowSlider
                            KeyNavigation.backtab: addPitcherButton
                            onValueModified: function(newValue) {
                                durationSlider.value = newValue
                                Settings.brew.steamTimeout = newValue
                                saveCurrentPitcher(newValue, flowSlider.value)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Steam Flow (per-pitcher, auto-saves)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled

                        Column {
                            Tr {
                                key: "steam.label.steamFlow"
                                fallback: "Steam Flow"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                            }
                            Tr {
                                key: "steam.hint.flowHint"
                                fallback: "Low = flat, High = foamy"
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: flowSlider
                            Layout.preferredWidth: Theme.scaled(180)
                            from: 40
                            to: 250
                            stepSize: 5
                            decimals: 0
                            value: getCurrentPitcherFlow()
                            displayText: flowToDisplay(value)
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.steamFlow", "Steam Flow")
                            KeyNavigation.tab: steamTempSlider
                            KeyNavigation.backtab: durationSlider
                            onValueModified: function(newValue) {
                                flowSlider.value = newValue
                                saveCurrentPitcher(durationSlider.value, newValue)
                            }
                            onValueCommitted: function(newValue) {
                                MainController.setSteamFlowImmediate(newValue)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Temperature (global setting)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled

                        Column {
                            Tr {
                                key: "steam.label.temperature"
                                fallback: "Temperature"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                            }
                            Tr {
                                key: "steam.hint.temperatureHint"
                                fallback: "Higher = drier steam"
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: steamTempSlider
                            Layout.preferredWidth: Theme.scaled(180)
                            from: 120
                            to: 170
                            stepSize: 1
                            decimals: 0
                            suffix: "°C"
                            value: Settings.brew.steamTemperature
                            valueColor: Theme.temperatureColor
                            accessibleName: TranslationManager.translate("steam.label.temperature", "Steam Temperature")
                            KeyNavigation.tab: pitcherWeightInput
                            KeyNavigation.backtab: flowSlider
                            onValueModified: function(newValue) {
                                steamTempSlider.value = newValue
                                saveCurrentPitcher(durationSlider.value, flowSlider.value, newValue)
                            }
                            onValueCommitted: function(newValue) {
                                MainController.setSteamTemperatureImmediate(newValue)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Milk pitcher (per-pitcher): the empty-pitcher weight, used to
                    // work out net milk (scale − pitcher) and, when a reference milk is
                    // set, to scale the steam time. Type it, or Tare then Weigh the empty
                    // pitcher on the scale (Weigh confirms; with the scale at ~0 it Clears).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled

                        Column {
                            spacing: Theme.scaled(4)
                            Tr {
                                key: "steam.label.pitcherWeight"
                                fallback: "Milk pitcher"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                                Accessible.ignored: true
                            }
                            Tr {
                                key: "steam.hint.pitcherWeight"
                                fallback: "Empty pitcher weight"
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                            Text {
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                                visible: text.length > 0
                                text: {
                                    // Track preset changes via steamPitcherPresetsChanged
                                    var _ = Settings.brew.steamPitcherPresets
                                    var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
                                    var cal = preset ? (preset.calibMilkG ?? 0) : 0
                                    if (cal > 0)
                                        return TranslationManager.translate("steam.hint.weightTimed", "Weight-timed") + ": " + cal.toFixed(0) + "g → " + preset.duration + "s"
                                    return ""
                                }
                                Accessible.ignored: true
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Editable empty-pitcher weight (type or ±). Writes through to the
                        // same per-preset storage as the Weigh button below.
                        ValueInput {
                            id: pitcherWeightInput
                            Layout.preferredWidth: Theme.scaled(150)
                            from: 0
                            to: 1000
                            stepSize: 0.1
                            decimals: 1
                            suffix: " g"
                            value: {
                                var _ = Settings.brew.steamPitcherPresets
                                var preset = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
                                return preset ? (preset.pitcherWeightG ?? 0) : 0
                            }
                            valueColor: Theme.weightColor
                            accessibleName: TranslationManager.translate("steam.label.pitcherWeight", "Milk pitcher weight")
                            // tareBtn lives in the scale-gated sub-row; skip straight to
                            // the reference-milk field when no scale is connected so Tab
                            // never lands on a hidden element.
                            KeyNavigation.tab: (ScaleDevice.connected && !ScaleDevice.isFlowScale) ? tareBtn : refMilkInput
                            KeyNavigation.backtab: steamTempSlider
                            onValueModified: function(newValue) {
                                Settings.brew.setSteamPitcherWeight(Settings.brew.selectedSteamPitcher, newValue)
                            }
                        }

                        // Scale controls: live reading + Tare + Weigh/Clear (only with a scale).
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: ScaleDevice.connected && !ScaleDevice.isFlowScale

                            Text {
                                text: MachineState.scaleWeight.toFixed(0) + "g"
                                color: Theme.textSecondaryColor
                                font: Theme.bodyFont
                                Accessible.ignored: true
                            }

                            // Tare button
                            Rectangle {
                                id: tareBtn
                                width: Theme.scaled(80)
                                height: Theme.scaled(44)
                                radius: Theme.cardRadius
                                color: tareBtnMa.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                                border.color: Theme.borderColor
                                border.width: 1

                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("steam.accessible.tare", "Tare scale")
                                Accessible.focusable: true
                                Accessible.onPressAction: tareBtnMa.clicked(null)
                                Keys.onReturnPressed: { tareBtnMa.clicked(null); event.accepted = true }
                                Keys.onSpacePressed:  { tareBtnMa.clicked(null); event.accepted = true }
                                KeyNavigation.tab: savePitcherWeightBtn
                                KeyNavigation.backtab: pitcherWeightInput

                                Tr {
                                    anchors.centerIn: parent
                                    key: "steam.label.tare"
                                    fallback: "Tare"
                                    color: Theme.textColor
                                    font: Theme.bodyFont
                                    Accessible.ignored: true
                                }

                                MouseArea { id: tareBtnMa; anchors.fill: parent; onClicked: MachineState.tareScale() }
                            }

                            // Weigh the empty pitcher from the scale (with an empty-pitcher
                            // confirm); when the scale reads ~0 it becomes "Clear" (saving 0
                            // disables the feature). Saving 0 needs no confirm.
                            Rectangle {
                                id: savePitcherWeightBtn
                                readonly property bool isClear: MachineState.scaleWeight < 5.0
                                width: Theme.scaled(80)
                                height: Theme.scaled(44)
                                radius: Theme.cardRadius
                                color: {
                                    var base = isClear ? Theme.surfaceColor : Theme.primaryColor
                                    return savePitcherWtMa.pressed ? Qt.darker(base, 1.2) : base
                                }
                                border.color: isClear ? Theme.borderColor : "transparent"
                                border.width: isClear ? 1 : 0

                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: isClear
                                    ? TranslationManager.translate("steam.label.clearPitcherWeight", "Clear pitcher weight")
                                    : TranslationManager.translate("steam.accessible.weighPitcher", "Weigh empty pitcher from the scale")
                                Accessible.focusable: true
                                Accessible.onPressAction: savePitcherWtMa.clicked(null)
                                Keys.onReturnPressed: { savePitcherWtMa.clicked(null); event.accepted = true }
                                Keys.onSpacePressed:  { savePitcherWtMa.clicked(null); event.accepted = true }
                                KeyNavigation.tab: refMilkInput
                                KeyNavigation.backtab: tareBtn

                                Text {
                                    anchors.centerIn: parent
                                    text: savePitcherWeightBtn.isClear
                                        ? TranslationManager.translate("steam.label.clear", "Clear")
                                        : TranslationManager.translate("steam.label.weigh", "Weigh")
                                    color: savePitcherWeightBtn.isClear ? Theme.textColor : Theme.primaryContrastColor
                                    font: Theme.bodyFont
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: savePitcherWtMa
                                    anchors.fill: parent
                                    onClicked: {
                                        if (savePitcherWeightBtn.isClear) {
                                            Settings.brew.setSteamPitcherWeight(Settings.brew.selectedSteamPitcher, 0.0)
                                        } else {
                                            steamPage.pendingPitcherWeight = MachineState.scaleWeight
                                            pitcherWeighConfirm.open()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // ── Weight-timed steaming: section header + one-line explanation,
                    // then the master on/off, shown above the controls it governs. ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)
                        visible: !steamPage.currentPitcherDisabled

                        Tr {
                            Layout.fillWidth: true
                            key: "steam.weightTimed.title"
                            fallback: "Weight-timed steaming"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(26)
                            font.bold: true
                            Accessible.ignored: true
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "steam.weightTimed.summary"
                            fallback: "Stops steaming by milk weight instead of a fixed time, so a small or large pitcher both finish at the same temperature. Calibrate once — steam a weighed pitcher and tap Use last steam, or set the reference by hand — then just rest the pitcher on the scale and steam; the bell rings when it has the weight."
                            wrapMode: Text.WordWrap
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }
                    }

                    // Master on/off (off by default; calibrating turns it on). Off = plain
                    // fixed-duration steaming; the calibration is kept for when you re-enable.
                    StyledSwitch {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("steam.weightTimed.enable", "Enable weight-timed steaming")
                        accessibleName: text
                        checked: Settings.brew.milkAutoCaptureEnabled
                        onToggled: Settings.brew.milkAutoCaptureEnabled = checked
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Reference milk weight: the baseline paired with this preset's
                    // duration for weight-timed steaming. steamTime = duration ×
                    // (measuredMilk / referenceMilk). 0 disables weight scaling.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled

                        Column {
                            Tr {
                                key: "steam.label.referenceMilk"
                                fallback: "Reference milk"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                            }
                            Tr {
                                key: "steam.hint.referenceMilk"
                                fallback: "The milk weight the duration above is tuned for. Other amounts scale from it. 0 = off."
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ValueInput {
                            id: refMilkInput
                            Layout.preferredWidth: Theme.scaled(150)
                            from: 0
                            to: 1500
                            stepSize: 0.1
                            decimals: 1
                            suffix: " g"
                            value: getCurrentPitcherCalibMilk()
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.referenceMilk", "Reference milk weight")
                            KeyNavigation.tab: pitcherRepeater.count > 0 ? pitcherRepeater.itemAt(0).focusTarget : addPitcherButton
                            // savePitcherWeightBtn is in the scale-gated sub-row; fall back
                            // to the always-visible field when no scale is connected.
                            KeyNavigation.backtab: (ScaleDevice.connected && !ScaleDevice.isFlowScale) ? savePitcherWeightBtn : pitcherWeightInput
                            onValueModified: function(newValue) {
                                // Don't reassign value here — it would break the declarative
                                // binding to getCurrentPitcherCalibMilk(), so a later "Use as
                                // baseline"/Weigh update wouldn't refresh the field. The setter
                                // writes through and the reactive getter re-evaluates the binding.
                                Settings.brew.setSteamPitcherCalibration(Settings.brew.selectedSteamPitcher, newValue)
                            }
                        }

                        // Weigh the milk now on the scale and use it as the reference.
                        // Requires a saved empty-pitcher weight (net milk = scale - pitcher).
                        Rectangle {
                            id: refMilkWeighBtn
                            visible: ScaleDevice.connected && !ScaleDevice.isFlowScale
                            readonly property bool btnEnabled: steamPage.currentMeasuredMilk() > 0
                            opacity: btnEnabled ? 1.0 : 0.4
                            width: Theme.scaled(84); height: Theme.scaled(44)
                            radius: Theme.cardRadius
                            color: refMilkWeighMa.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("steam.accessible.setRefMilk", "Set reference milk from the scale")
                            Accessible.focusable: true
                            Accessible.onPressAction: refMilkWeighMa.clicked(null)
                            Tr {
                                anchors.centerIn: parent
                                key: "steam.label.weigh"; fallback: "Weigh"
                                color: Theme.primaryContrastColor; font: Theme.bodyFont
                                Accessible.ignored: true
                            }
                            MouseArea {
                                id: refMilkWeighMa
                                anchors.fill: parent
                                enabled: refMilkWeighBtn.btnEnabled
                                onClicked: Settings.brew.setSteamPitcherCalibration(
                                    Settings.brew.selectedSteamPitcher, steamPage.currentMeasuredMilk())
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Adopt the last actual steam session (milk + time) as this
                    // pitcher's reference baseline — sets reference milk AND duration.
                    RowLayout {
                        id: useLastSteamRow
                        Layout.fillWidth: true
                        visible: !steamPage.currentPitcherDisabled
                        spacing: Theme.scaled(12)
                        readonly property bool hasLast: Settings.brew.lastSteamMilkG > 0 && Settings.brew.lastSteamTimeS > 0

                        Column {
                            Tr {
                                key: "steam.lastSteam.title"
                                fallback: "Calibrate from your last steam"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                            }
                            Text {
                                text: useLastSteamRow.hasLast
                                      ? TranslationManager.translate("steam.lastSteam.values", "Last steam: %1 g milk → %2 s — sets duration + reference")
                                            .arg(Settings.brew.lastSteamMilkG.toFixed(0)).arg(Math.round(Settings.brew.lastSteamTimeS))
                                      : TranslationManager.translate("steam.lastSteam.none", "Steam a weighed pitcher to your liking first, then come back here.")
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            Layout.preferredHeight: Theme.scaled(44)
                            text: TranslationManager.translate("steam.lastSteam.use", "Use last steam")
                            accessibleName: TranslationManager.translate("steam.lastSteam.useAccessible", "Use last steam session as this pitcher's duration and reference milk")
                            primary: true
                            enabled: useLastSteamRow.hasLast
                            onClicked: {
                                var idx = Settings.brew.selectedSteamPitcher
                                var p = Settings.brew.getSteamPitcherPreset(idx)
                                if (!p || p.disabled) return
                                Settings.brew.updateSteamPitcherPreset(idx, p.name, Math.round(Settings.brew.lastSteamTimeS), p.flow ?? 150)
                                Settings.brew.setSteamPitcherCalibration(idx, Settings.brew.lastSteamMilkG)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Live expected steam time for the milk currently on the scale
                    // (only when the preset is calibrated and milk is present).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled
                                 && ScaleDevice.connected && !ScaleDevice.isFlowScale
                                 && steamPage.scaledSteamTimeout() > 0

                        Column {
                            Tr {
                                key: "steam.label.expectedSteamTime"
                                fallback: "Expected steam time"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(24)
                            }
                            Text {
                                text: TranslationManager.translate("steam.hint.forMilkOnScale", "for the %1 g now on the scale").arg(steamPage.currentMeasuredMilk().toFixed(0))
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: steamPage.scaledSteamTimeout() + " s"
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(28)
                            font.bold: true
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled && ScaleDevice.connected && !ScaleDevice.isFlowScale && steamPage.scaledSteamTimeout() > 0 }

                }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: isSteaming || steamSoftStopped }
    }

    // Weight-timed steaming: auto-capture the milk weight while it rests on the
    // scale (before steaming). When the net milk holds steady ~2.5s, lock the
    // steam time proportional to it, ding, and show a confirmation. The value
    // stays locked while you lift the pitcher to steam (the detector re-arms only
    // when the pitcher is removed or the load changes). This is a programmatic
    // write to steamTimeout, so it never bakes the scaled value into the preset.
    StableWeightCapture {
        id: milkCapture
        // Virtual-zero model: raw scale reading minus the saved empty-pitcher weight
        // (cupWeight) = net milk, robust to an un-zeroed scale. Auto-capture requires
        // a saved pitcher weight (cupWeight > 0).
        rawWeight: (ScaleDevice.connected && !ScaleDevice.isFlowScale) ? MachineState.scaleWeight : 0
        cupWeight: {
            var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
            return (p && !p.disabled) ? (p.pitcherWeightG ?? 0) : 0
        }
        // Opt-in (Settings.brew.milkAutoCaptureEnabled, default OFF — calibrating a
        // pitcher turns it on) — disabling it stops the scale from auto-changing the
        // steam stop time.
        active: Settings.brew.milkAutoCaptureEnabled
                && !isSteaming && !steamSoftStopped
                && ScaleDevice.connected && !ScaleDevice.isFlowScale
        minNet: 50   // nobody steams < 50 g milk; floor also keeps a bean cup from tripping milk capture
        maxNet: 1500
        tolerance: 1.5
        stableMs: 2500
        // Lifting the pitcher off the scale clears a manual ±5 override, so the next
        // placement re-arms weight scaling for a fresh pour. Guard on !isSteaming so the
        // reset() at steam-start (active→false) can't clear the latch before
        // onIsSteamingChanged reads it — otherwise a manual nudge would be overwritten.
        onLoadPresentChanged: if (!loadPresent && !isSteaming) steamPage.steamTimeoutUserAdjusted = false
        onStableCaptured: function(milk) {
            // Latch the measured milk for the baseline pair (committed atomically with
            // the duration at session end, in main.qml) — recorded even when the preset
            // isn't calibrated yet so the calibration-bootstrap steam can be adopted.
            if (Window.window) Window.window.sessionMeasuredMilkG = milk
            // Respect a manual ±5 adjustment: still record the milk (above) for the
            // baseline, but don't overwrite the time the user dialed in by hand.
            if (steamPage.steamTimeoutUserAdjusted)
                return
            var t = steamPage.steamTimeForMilk(milk)
            if (t <= 0)
                return  // preset not calibrated (no reference milk) — nothing to lock
            Settings.brew.steamTimeout = t
            // Push to the DE1 now (same as onPresetSelected) — without this the machine
            // keeps its last-sent timeout, so a GHC-started steam wouldn't use the
            // scaled value and the steam time appears not to scale.
            MainController.applySteamSettings()
            steamPage.steamTimeoutScaled = true
            steamPage.captureBannerText =
                TranslationManager.translate("steam.capture.locked", "Steam time set: %1s for %2g milk")
                    .arg(t).arg(milk.toFixed(0))
            steamPage.captureBannerVisible = true
            captureBannerTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                if (Settings.brew.doseCaptureSoundEnabled)
                    AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(steamPage.captureBannerText)
            }
        }
    }
    // Re-zero the milk capture when the scale is tared (so the old offset isn't
    // double-counted by the virtual-zero baseline).
    Connections {
        target: MachineState
        function onTareCompleted() { milkCapture.reset() }
    }

    // Confirmation banner shown briefly after a milk-weight capture (auto-dismiss).
    property string captureBannerText: ""
    property bool   captureBannerVisible: false
    Timer { id: captureBannerTimer; interval: 3500; onTriggered: steamPage.captureBannerVisible = false }

    Rectangle {
        visible: steamPage.captureBannerVisible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(12)
        z: 1000
        width: bannerLabel.implicitWidth + Theme.scaled(32)
        height: bannerLabel.implicitHeight + Theme.scaled(20)
        radius: Theme.cardRadius
        color: Theme.primaryColor
        opacity: steamPage.captureBannerVisible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Text {
            id: bannerLabel
            anchors.centerIn: parent
            text: steamPage.captureBannerText
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }

    // Live during-steam coaching cues (stretch -> roll -> almost -> stop), driven
    // by LiveSteamCoach off the elapsed steam time. Self-gates on an active cue and
    // speaks via AccessibilityManager; only shows while steaming.
    LiveCoachingBanner {
        z: 1000
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(110)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        coach: MainController.liveSteamCoach
        enabled: Settings.app.liveSteamCoachingEnabled
    }

    // Small flashing reminder while the milk pitcher is settling on the scale
    // (something is on the scale but the capture hasn't fired yet). Disappears
    // the instant it captures (the bell rings).
    Text {
        id: steamWaitForBellHint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(70)
        z: 1000
        horizontalAlignment: Text.AlignHCenter
        visible: milkCapture.active && !milkCapture.isCaptured
                 && milkCapture.cupWeight > 0
                 && milkCapture.loadPresent
                 && milkCapture.netWeight >= milkCapture.minNet
                 && milkCapture.netWeight <= milkCapture.maxNet
        text: TranslationManager.translate("scale.waitForBell", "Wait for the bell before you take it off the scale")
        color: Theme.warningColor
        font: Theme.labelFont
        SequentialAnimation on opacity {
            running: steamWaitForBellHint.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 450 }
            NumberAnimation { to: 1.0; duration: 450 }
        }
    }

    // Accessibility: announce scale weight at intervals while weighing milk (settings view, not steaming)
    property real _lastAnnouncedSteamWeight: 0

    Connections {
        target: MachineState
        enabled: !isSteaming && !steamSoftStopped
                 && ScaleDevice.connected && !ScaleDevice.isFlowScale
                 && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
                 && AccessibilityManager.extractionAnnouncementsEnabled
        function onScaleWeightChanged() {
            var w = MachineState.scaleWeight
            // Reset milestone tracker after taring
            if (w < 1.0) { _lastAnnouncedSteamWeight = 0; return }
            var mode = AccessibilityManager.extractionAnnouncementMode
            if (mode !== "milestones_only" && mode !== "both") return
            // Announce every 10g milestone while weighing milk
            if (Math.floor(w / 10) > Math.floor(_lastAnnouncedSteamWeight / 10)) {
                AccessibilityManager.announce(Math.floor(w) + " " +
                    TranslationManager.translate("espresso.accessibility.grams", "grams"))
                _lastAnnouncedSteamWeight = w
            }
        }
    }

    Timer {
        id: steamWeightAnnounceTimer
        interval: AccessibilityManager.extractionAnnouncementInterval * 1000
        repeat: true
        running: !isSteaming && !steamSoftStopped
                 && ScaleDevice.connected && !ScaleDevice.isFlowScale
                 && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
                 && AccessibilityManager.extractionAnnouncementsEnabled
                 && (AccessibilityManager.extractionAnnouncementMode === "timed" ||
                     AccessibilityManager.extractionAnnouncementMode === "both")
        onTriggered: {
            if (MachineState.scaleWeight < 1.0) return
            var weight = MachineState.scaleWeight.toFixed(0)
            AccessibilityManager.announce(
                TranslationManager.translate("espresso.accessibility.weight", "weight") + " " + weight + " " +
                TranslationManager.translate("espresso.accessibility.grams", "grams"))
        }
    }

    // Hidden translation helper for "No pitcher"
    Tr { id: noPitcherText; key: "steam.label.noPitcher"; fallback: "No pitcher"; visible: false }

    // Bottom bar (hide during soft-stop waiting for purge, steam heating, and puffing)
    BottomBar {
        visible: !isSteaming && !steamSoftStopped && !isSteamHeating && !isPuffing
        title: getCurrentPitcherName() || noPitcherText.text
        onBackClicked: {
            // Turn off heater if keepSteamHeaterOn is false, otherwise keep it warm
            if (!Settings.brew.keepSteamHeaterOn) {
                MainController.sendSteamTemperature(0)  // This sets steamDisabled=true
            } else {
                MainController.applySteamSettings()
            }
            root.goToIdle()
        }

        Text {
            text: durationSlider.value.toFixed(0) + "s"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3 }
        Tr {
            id: flowLabelText
            key: "steam.label.flow"
            fallback: "Flow"
            visible: false
        }
        Text {
            text: flowLabelText.text + " " + flowToDisplay(flowSlider.value)
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle { width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3 }
        Text {
            text: steamTempSlider.value.toFixed(0) + "°C"
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }


    // Edit Pitcher Popup (rename/delete)
    Dialog {
        id: editPitcherPopup
        x: (parent.width - width) / 2
        y: editPitcherPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool editPitcherPopupAtTop: false
        onOpened: {
            editPitcherPopupAtTop = false
            editPitcherNameInput.forceActiveFocus()
        }
        onClosed: editPitcherPopupAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && editPitcherPopup.opened) {
                    editPitcherPopup.editPitcherPopupAtTop = true
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(10)
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "steam.popup.editPitcher"
                fallback: "Edit Pitcher"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Tr { id: pitcherNamePlaceholder; key: "steam.placeholder.pitcherName"; fallback: "Pitcher name"; visible: false }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: editPitcherNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("steam.accessible.renamePitcher", "Rename pitcher preset")
                    Accessible.description: text
                    Accessible.focusable: true
                    KeyNavigation.tab: editDeleteButton
                    KeyNavigation.backtab: editSaveButton

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: pitcherNamePlaceholder.text
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            Tr { id: deleteButtonText; key: "steam.button.delete"; fallback: "Delete"; visible: false }
            Tr { id: cancelButtonText; key: "steam.button.cancel"; fallback: "Cancel"; visible: false }
            Tr { id: saveButtonText; key: "steam.button.save"; fallback: "Save"; visible: false }

            RowLayout {
                spacing: Theme.scaled(10)

                AccessibleButton {
                    id: editDeleteButton
                    text: deleteButtonText.text
                    accessibleName: TranslationManager.translate("steam.deletePitcherPreset", "Delete this pitcher preset")
                    destructive: true
                    KeyNavigation.tab: editCancelButton
                    KeyNavigation.backtab: editPitcherNameInput
                    onClicked: {
                        Settings.brew.removeSteamPitcherPreset(editingPitcherIndex)
                        editPitcherPopup.close()
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: editCancelButton
                    text: cancelButtonText.text
                    accessibleName: TranslationManager.translate("steam.cancelEditingPitcher", "Cancel editing pitcher preset")
                    KeyNavigation.tab: editSaveButton
                    KeyNavigation.backtab: editDeleteButton
                    onClicked: editPitcherPopup.close()
                }

                AccessibleButton {
                    id: editSaveButton
                    primary: true
                    text: saveButtonText.text
                    accessibleName: TranslationManager.translate("steam.savePitcherChanges", "Save changes to pitcher preset")
                    KeyNavigation.tab: editPitcherNameInput
                    KeyNavigation.backtab: editCancelButton
                    onClicked: {
                        Qt.inputMethod.commit()
                        var preset = Settings.brew.getSteamPitcherPreset(editingPitcherIndex)
                        Settings.brew.updateSteamPitcherPreset(editingPitcherIndex, editPitcherNameInput.text, preset.duration, preset.flow)
                        editPitcherPopup.close()
                    }
                }
            }
        }
    }

    // Add Pitcher Dialog
    Dialog {
        id: addPitcherDialog
        x: (parent.width - width) / 2
        y: addPitcherDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property bool addPitcherDialogAtTop: false
        onOpened: {
            addPitcherDialogAtTop = false
            newPitcherName.text = ""
            newPitcherName.forceActiveFocus()
        }
        onClosed: addPitcherDialogAtTop = false

        Connections {
            target: Qt.inputMethod
            function onVisibleChanged() {
                if (Qt.inputMethod.visible && addPitcherDialog.opened) {
                    addPitcherDialog.addPitcherDialogAtTop = true
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(10)
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "steam.popup.addPitcherPreset"
                fallback: "Add Pitcher Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Tr { id: addPitcherNamePlaceholder; key: "steam.placeholder.pitcherName"; fallback: "Pitcher name"; visible: false }
            Tr { id: addCancelButtonText; key: "steam.button.cancel"; fallback: "Cancel"; visible: false }
            Tr { id: addButtonText; key: "steam.button.add"; fallback: "Add"; visible: false }
            Tr { id: addOffButtonText; key: "steam.button.addOff"; fallback: "Add Off"; visible: false }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: newPitcherName
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("steam.accessible.newPitcherName", "New pitcher preset name")
                    Accessible.description: text
                    Accessible.focusable: true
                    KeyNavigation.tab: addCancelPitcherButton
                    KeyNavigation.backtab: addPitcherConfirmButton

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: addPitcherNamePlaceholder.text
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                        Accessible.ignored: true
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: addCancelPitcherButton
                    text: addCancelButtonText.text
                    accessibleName: TranslationManager.translate("steam.cancelAddingPitcher", "Cancel adding new pitcher preset")
                    KeyNavigation.tab: addPitcherOffButton
                    KeyNavigation.backtab: newPitcherName
                    onClicked: addPitcherDialog.close()
                }

                AccessibleButton {
                    id: addPitcherOffButton
                    text: addOffButtonText.text
                    accessibleName: TranslationManager.translate("steam.addNewPitcherOff", "Add new preset that turns the steam heater off")
                    KeyNavigation.tab: addPitcherConfirmButton
                    KeyNavigation.backtab: addCancelPitcherButton
                    onClicked: {
                        Qt.inputMethod.commit()
                        if (newPitcherName.text.trim() !== "") {
                            var presetCount = Settings.brew.steamPitcherPresets.length
                            Settings.brew.addSteamPitcherPresetDisabled(newPitcherName.text.trim())
                            Settings.brew.selectedSteamPitcher = presetCount
                            newPitcherName.text = ""
                            addPitcherDialog.close()
                        }
                    }
                }

                AccessibleButton {
                    id: addPitcherConfirmButton
                    primary: true
                    text: addButtonText.text
                    accessibleName: TranslationManager.translate("steam.addNewPitcher", "Add new pitcher preset with entered name")
                    KeyNavigation.tab: newPitcherName
                    KeyNavigation.backtab: addPitcherOffButton
                    onClicked: {
                        Qt.inputMethod.commit()
                        if (newPitcherName.text.trim() !== "") {
                            var presetCount = Settings.brew.steamPitcherPresets.length
                            Settings.brew.addSteamPitcherPreset(newPitcherName.text.trim(), 30, 150, Settings.brew.steamTemperature)
                            // Selecting the new preset fires onSelectedSteamPitcherChanged,
                            // which loads its temperature into the slider/active temp.
                            Settings.brew.selectedSteamPitcher = presetCount
                            newPitcherName.text = ""
                            addPitcherDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Update sliders when selected pitcher changes
    Connections {
        target: Settings.brew
        function onSelectedSteamPitcherChanged() {
            durationSlider.value = getCurrentPitcherDuration()
            flowSlider.value = getCurrentPitcherFlow()
            // Load the newly-selected pitcher's temperature into both the slider and
            // the active steam temperature. This runs synchronously when a pill tap
            // sets selectedSteamPitcher, so the subsequent startSteamHeating/
            // applySteamSettings push the per-pitcher temperature to the machine.
            var temp = getCurrentPitcherTemperature()
            steamTempSlider.value = temp
            Settings.brew.steamTemperature = temp
        }
        function onSteamPitcherPresetsChanged() {
            durationSlider.value = getCurrentPitcherDuration()
            flowSlider.value = getCurrentPitcherFlow()
            // Keep the active steam temperature in sync (not just the slider) when the
            // selected pitcher is edited from anywhere — e.g. via MCP — so a later
            // applySteamSettings (back-navigation, keepSteamHeaterOn) pushes the
            // current value rather than a stale one.
            var temp = getCurrentPitcherTemperature()
            steamTempSlider.value = temp
            Settings.brew.steamTemperature = temp
        }
    }
}
