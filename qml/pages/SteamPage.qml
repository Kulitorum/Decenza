// The two pitcher Repeaters below declare their model roles with `required property`
// instead of taking them from an injected delegate context, so `steamPage` and the
// file's other ids resolve statically inside them. See PresetPillRow.qml for the same
// treatment. This page also names its own root explicitly so bindings do not depend on
// the StackView creation context.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza

T.Page {
    id: steamPage
    objectName: "steamPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    property string pageTitle: steamPageTitle.text
    Tr { id: steamPageTitle; key: "steam.title"; fallback: "Steam"; visible: false }

    // Use StackView.onActivated (not Component.onCompleted) so side effects
    // run when the page is actually shown, not during construction. This
    // also re-fires on pop-back if the page is ever pushed below another.
    // Gate selected-preset application and heater-start on !isSteaming so a re-activation
    // mid-session doesn't clobber the user's in-progress settings or re-issue
    // a redundant BLE write.
    StackView.onActivated: {
        steamPage.syncActiveSafetyWarnings()
        if (!isSteaming) {
            const heaterOff = steamPage.currentPitcherDisabled
            if (steamPage.requestPageAction("steam-prepare", {}) !== "" && !heaterOff)
                durationSlider.forceActiveFocus()
        }
    }
    StackView.onDeactivating: {
        // A late acknowledgement may still update machine state, but it must not
        // dismiss whatever page the user navigated to in the meantime.
        steamPage.pendingNavigationAction = ""
        steamPage.pendingNavigationRequestId = ""
    }

    Connections {
        target: SteamService
        function onCompleted(requestId, operation, success, message, result, newRevision) {
            if (requestId === steamPage.pendingNavigationRequestId) {
                const navigationAction = steamPage.pendingNavigationAction
                steamPage.pendingNavigationRequestId = ""
                steamPage.pendingNavigationAction = ""
                if (!success) {
                    if (navigationAction === "steam-back")
                        AppShell.browserBackCancelled()
                    if (steamPage.StackView.status === StackView.Active)
                        steamPage.showPitcherToast(steamPage.actionFailureText(message))
                } else if (result.dismissPage === true) {
                    AppShell.dismissRequested()
                }
                return
            }

            if (requestId === steamPage.pendingEditorRequestId) {
                steamPage.finishEditorRequest(success, message, result)
                return
            }

            const pageAction = operation === "steam-prepare"
                    || operation === "steam-select-pitcher"
                    || operation === "steam-adjust-time"
            const settingAction = operation === "set-steam-view-mode"
                    || operation === "set-steam-coach-visual"
                    || operation === "set-steam-coach-audio"
                    || operation === "set-steam-graph-trace"
                    || operation === "tare"
            if (!success && (pageAction || settingAction)
                    && steamPage.StackView.status === StackView.Active)
                steamPage.showPitcherToast(steamPage.actionFailureText(message))
        }
    }

    property bool isSteaming: SteamService.state.isSteaming || AppShell.debugLiveView
    property int editingPitcherIndex: -1  // For the edit popup
    property string pendingNavigationAction: ""
    property string pendingNavigationRequestId: ""
    property string pendingEditorRequestId: ""
    property string pendingEditorOperation: ""
    property string pendingEditorPresetName: ""
    readonly property bool editorReady: SteamService.ready && !SteamService.busy
        && pendingEditorRequestId.length === 0
    property bool debugSteamSoftStopped: false
    readonly property bool debugStopPreview: AppShell.debugLiveView && SteamService.state.deviceState !== 5
    readonly property bool steamSoftStopped: debugStopPreview
                                                ? debugSteamSoftStopped
                                                : SteamService.state.steamSoftStopped

    function stopSteamFromPage() {
        if (pendingNavigationAction !== "")
            return
        if (debugStopPreview) {
            if (SteamService.state.steamTwoTapStop && !debugSteamSoftStopped) {
                debugSteamSoftStopped = true
            } else {
                debugSteamSoftStopped = false
                AppShell.dismissRequested()
            }
            return
        }
        // Completion is queued by the service so local navigation state exists
        // before either native or browser acknowledgements arrive.
        pendingNavigationAction = "steam-stop"
        pendingNavigationRequestId = requestPageAction("steam-stop",
                                                       { expectedSoftStopped: steamSoftStopped })
        if (pendingNavigationRequestId === "")
            pendingNavigationAction = ""
    }

    function leaveSteamFromPage() {
        if (pendingNavigationAction !== "")
            return
        pendingNavigationAction = "steam-back"
        pendingNavigationRequestId = requestPageAction("steam-back", {})
        if (pendingNavigationRequestId === "")
            pendingNavigationAction = ""
    }

    function requestPageAction(operation, fields) {
        return SteamService.request(operation, fields, SteamService.state.controlContextId)
    }

    function actionFailureText(message) {
        return message || SteamService.errorMessage
                || TranslationManager.translate("steam.action.failed", "The Steam action could not be completed.")
    }

    function beginEditorRequest(operation, fields, presetName) {
        if (!editorReady) {
            steamPage.showPitcherToast(TranslationManager.translate(
                "steam.editor.notReady", "The steam setting was not saved. Wait for the current request to finish or reconnect, then try again."))
            return ""
        }
        pendingEditorOperation = operation
        pendingEditorPresetName = presetName ?? ""
        pendingEditorRequestId = SteamService.request("steam-editor",
            { editorOperation: operation, fields: fields }, SteamService.editorRevision)
        if (pendingEditorRequestId === "") {
            pendingEditorOperation = ""
            pendingEditorPresetName = ""
        }
        return pendingEditorRequestId
    }

    function finishEditorRequest(success, message, result) {
        const pendingOperation = pendingEditorOperation
        const presetName = pendingEditorPresetName
        pendingEditorRequestId = ""
        pendingEditorOperation = ""
        pendingEditorPresetName = ""
        if (!success) {
            if (steamPage.StackView.status === StackView.Active)
                steamPage.showPitcherToast(steamPage.actionFailureText(message))
            return
        }
        if (pendingOperation === "rename-preset" || pendingOperation === "delete-preset")
            editPitcherPopup.close()
        else if (pendingOperation === "create-preset") {
            newPitcherName.text = ""
            addPitcherDialog.close()
        } else if (pendingOperation === "store-current-pitcher-weight") {
            const weight = result.weightG
            steamPage.showPitcherToast(TranslationManager.translate(
                "steam.toast.pitcherWeightStored",
                "%1 g stored as empty %2 pitcher weight")
                .arg(Number(weight).toFixed(1)).arg(presetName))
        } else if (pendingOperation === "clear-pitcher-weight") {
            steamPage.showPitcherToast(TranslationManager.translate(
                "steam.toast.pitcherWeightCleared",
                "Empty %1 pitcher weight cleared").arg(presetName))
        }
    }

    function requestCurrentPresetDuration(value) {
        const index = SteamService.state.selectedSteamPitcher
        const preset = SteamService.getSteamPitcherPreset(index)
        beginEditorRequest("set-preset-duration",
                           { index: index, expectedName: preset ? preset.name : "",
                             durationSec: Math.round(value) }, preset ? preset.name : "")
    }

    function requestCurrentPresetFlow(value) {
        const index = SteamService.state.selectedSteamPitcher
        const preset = SteamService.getSteamPitcherPreset(index)
        beginEditorRequest("set-preset-flow",
                           { index: index, expectedName: preset ? preset.name : "",
                             flow: Math.round(value) }, preset ? preset.name : "")
    }

    function requestCurrentPresetTemperature(valueC) {
        const index = SteamService.state.selectedSteamPitcher
        const preset = SteamService.getSteamPitcherPreset(index)
        beginEditorRequest("set-preset-temperature",
                           { index: index, expectedName: preset ? preset.name : "",
                             temperatureC: valueC }, preset ? preset.name : "")
    }

    function requestCurrentPitcherWeight(value) {
        const index = SteamService.state.selectedSteamPitcher
        const preset = SteamService.getSteamPitcherPreset(index)
        beginEditorRequest("set-pitcher-weight",
                           { index: index, expectedName: preset ? preset.name : "",
                             weightG: value }, preset ? preset.name : "")
    }

    // A real (weighing) scale is present. Keep the policy in one place so each
    // scale-dependent widget agrees on what readings are usable.
    readonly property bool realScaleConnected: SteamService.state.scaleConnected && !SteamService.state.scaleIsFlowScale

    // Repeater.itemAt() is typed QQuickItem, so reaching the delegate's own `focusTarget`
    // needs the cast to its declared root type. Kept to this one place rather than
    // repeated at the sites that need it.
    function pitcherFocusTarget(i: int): Item {
        // Range-checked as well as null-checked: Repeater.count is the MODEL size and is
        // emitted before the delegates exist (regenerate() returns early until
        // componentComplete(), qquickrepeater.cpp:434-438), so `count > 0` with a null
        // itemAt() is normal while a creation-time binding first evaluates.
        if (i < 0 || i >= pitcherRepeater.count) return null
        var it = pitcherRepeater.itemAt(i) as RepeaterDelegateItem
        return it ? it.focusTarget : null
    }

    function focusPitcherAt(i: int) {
        var target = steamPage.pitcherFocusTarget(i)
        if (target)
            target.forceActiveFocus()
    }

    // Check if steam heater needs heating
    readonly property real currentSteamTemp: SteamService.state.steamTemperatureActual
    // Resolved state, not the measured temperature — a boiler that was switched
    // off minutes ago still reads hot on its way down.
    readonly property bool steamHeaterOff: !SteamService.state.steamHeaterOn
    readonly property string heaterOffLabel: SteamLabels.offReadout
    // The bare current-temperature readout, and the "current / target" pair.
    readonly property string currentSteamTempText:
        SteamLabels.temperatureText(steamPage.steamHeaterOff, steamPage.currentSteamTemp)
    readonly property string steamTempPairText:
        SteamLabels.temperaturePairText(steamPage.steamHeaterOff,
                                        steamPage.currentSteamTemp, steamPage.targetSteamTemp)
    readonly property real targetSteamTemp: SteamService.state.steamTemperature
    // A cold boiler nobody asked to heat is not "heating up". Without the
    // steamHeaterOff term this banner animated "Heating steam..." over a
    // progress bar climbing towards a target that was never commanded — on the
    // same row whose right-hand readout said "Off".
    readonly property bool isHeatingUp: !isSteaming && !steamHeaterOff
                                        && currentSteamTemp < (targetSteamTemp - 5)  // 5°C tolerance

    // Check if DE1 is in Steam state but still heating (FinalHeating/Heating substate)
    // DE1::State::Steam = 5
    readonly property bool isSteamHeating: SteamService.state.deviceState === 5 && !isSteaming

    // Check if DE1 is in Puffing state (waiting for user to press STOP or auto-flush)
    // DE1::SubState::Puffing = 20
    readonly property bool isPuffing: SteamService.state.deviceState === 5 && SteamService.state.deviceSubState === 20

    // Reactive: is the currently selected preset an "Off" pill? Duration/flow/temp
    // sliders don't apply to Off presets so they're hidden in the settings frame.
    // References selectedSteamPitcher and steamPitcherPresets so the binding
    // re-evaluates on selection change or when the preset list itself is edited.
    readonly property bool currentPitcherDisabled: {
        var _selected = SteamService.state.selectedSteamPitcher
        var _list = SteamService.state.steamPitcherPresets
        var preset = SteamService.getSteamPitcherPreset(_selected)
        return preset ? preset.disabled === true : false
    }

    // PreparationController applies steam timing and preset reset once per host phase
    // transition. This page owns only its transient warnings and presentation state.
    onIsSteamingChanged: {
        if (isSteaming) {
            debugSteamSoftStopped = false
            _lastAnnouncedSteamWeight = 0
            // Same for the post-session modal warning. A new session takes
            // precedence; the dialog's onClosed checks isSteaming and skips
            // the navigation back to idle so we stay on the steam page.
            if (steamWarningDialog.opened) {
                steamWarningDialog.close()
            }
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
    // don't carry duration/flow, so fall back to the global values so the
    // sliders don't jump when the user switches to an Off preset.
    function getCurrentPitcherDuration() {
        var preset = SteamService.getSteamPitcherPreset(SteamService.state.selectedSteamPitcher)
        if (!preset || preset.disabled) return SteamService.state.steamTimeout ?? 30
        return preset.duration
    }

    function getCurrentPitcherFlow() {
        var preset = SteamService.getSteamPitcherPreset(SteamService.state.selectedSteamPitcher)
        if (!preset || preset.disabled) return SteamService.state.steamFlow ?? 150
        return (preset.flow !== undefined) ? preset.flow : 150
    }

    function getCurrentPitcherName() {
        return SteamLabels.pitcherName(
            SteamService.getSteamPitcherPreset(SteamService.state.selectedSteamPitcher))
    }

    // --- Live-view action row focus targets ----------------------------------
    // The action row holds only the headless Stop button (GHC machines stop from
    // the physical button, so the row is empty there). Every tab/backtab handler
    // that enters the row asks here for the button that is actually showing, so the
    // choice lives once instead of being repeated at each call site. Both return
    // null when the row is empty; callers fall back.
    function firstLiveActionButton() {
        if (steamStopButton.visible) return steamStopButton
        return null
    }

    function lastLiveActionButton() {
        if (steamStopButton.visible) return steamStopButton
        return null
    }

    // An "Off" preset's pill is hidden while steaming, and KeyNavigation silently
    // refuses to focus an invisible target — Tab then does nothing at all, because
    // the pills navigate via Keys handlers and so offer no chain to fall through to.
    // Never target a pill by raw index; ask for one that is actually showing.
    function firstVisiblePresetPill() {
        for (let i = 0; i < livePresetRepeater.count; i++) {
            let pill = livePresetRepeater.itemAt(i)
            if (pill && pill.visible) return pill
        }
        return null
    }

    function lastVisiblePresetPill() {
        for (let i = livePresetRepeater.count - 1; i >= 0; i--) {
            let pill = livePresetRepeater.itemAt(i)
            if (pill && pill.visible) return pill
        }
        return null
    }



    // What follows the preset pills going forward. The timer view leads into the
    // -5s/+5s pair (which hide during heating and puffing, leaving the flow slider
    // as the first stop); the chart view has neither, so focus goes straight to the
    // action row. Without this the pills jumped over the whole timer column and the
    // -5s/+5s buttons were reachable only by Shift+Tab.
    function firstLiveControlAfterPresets() {
        if (steamViewMode === "timer")
            return decreaseTimeBtn.visible ? decreaseTimeBtn : steamingFlowSlider
        return firstLiveActionButton()
    }

    // --- Weight-scaled steaming (calibrated presets) -------------------------
    // The native host computes these values from the selected preset and scale;
    // the shared page only presents the replicated result.

    // Net milk currently on the scale for the selected pitcher (0 if none/invalid).
    function currentMeasuredMilk() {
        return SteamService.state.measuredMilkG
    }

    // Scaled steam time for the milk currently on the scale (0 → use fixed duration).
    function scaledSteamTimeout() {
        return SteamService.state.scaledSteamTimeout
    }

    // Weighing an empty pitcher stores the reading straight away and confirms with a
    // toast — the value also appears in the pitcher-weight field, so no dialog is
    // needed. (The old confirm dialog rendered white-on-white in dark themes and just
    // added friction.) The button's accessible name and the toast carry the "empty
    // pitcher" wording that the confirm used to state.
    function showPitcherToast(message) {
        pitcherToastText.text = message
        pitcherToast.opacity = 1
        pitcherToastTimer.restart()
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled)
            AccessibilityManager.announce(message, true)
    }
    Rectangle {
        id: pitcherToast
        parent: Overlay.overlay
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        visible: opacity > 0
        anchors.bottom: parent ? parent.bottom : undefined
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaled(12)
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        width: pitcherToastText.implicitWidth + Theme.scaled(32)
        height: Theme.scaled(40)
        radius: Theme.scaled(20)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1
        z: 10
        Text {
            id: pitcherToastText
            anchors.centerIn: parent
            color: Theme.textColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }
    }
    Timer {
        id: pitcherToastTimer
        interval: 2500
        onTriggered: pitcherToast.opacity = 0
    }

    // Steam view mode: "timer" (default) or "chart".
    readonly property string steamViewMode: SteamService.state.steamViewMode

    // Live safety warning banner. Restore it from the tracker's active state on
    // activation and snapshot changes; the live signals only prompt a refresh.
    // Each warning is latched once per steam session, so dismissal is session-scoped.
    // (The three other SteamService signals handled below are
    // post-session modal dialogs, not this banner.)
    property string warningText: ""
    property bool warningVisible: false
    property string warningSessionId: ""
    property bool pressureWarningDismissed: false
    property bool temperatureWarningDismissed: false
    property bool visiblePressureWarning: false
    property bool visibleTemperatureWarning: false

    function syncActiveSafetyWarnings() {
        const sessionId = SteamService.state.activeWarningSessionId
        if (sessionId !== "") {
            if (sessionId !== warningSessionId) {
                warningSessionId = sessionId
                pressureWarningDismissed = false
                temperatureWarningDismissed = false
            }
        }

        if (sessionId === "") {
            visiblePressureWarning = false
            visibleTemperatureWarning = false
            warningText = ""
            warningVisible = false
            return
        }

        const kinds = SteamService.state.activeWarningKinds
        const messages = []
        visiblePressureWarning = !pressureWarningDismissed
                && kinds.indexOf("pressure-too-high") !== -1
        visibleTemperatureWarning = !temperatureWarningDismissed
                && kinds.indexOf("temperature-too-high") !== -1
        if (visiblePressureWarning) {
            messages.push(TranslationManager.translate("steam.warning.pressureHigh",
                "Warning: steam pressure is too high"))
        }
        if (visibleTemperatureWarning) {
            messages.push(TranslationManager.translate("steam.warning.temperatureHigh",
                "Warning: steam temperature is too high"))
        }
        warningText = messages.join("\n")
        warningVisible = warningText !== ""
    }

    function dismissSafetyWarning() {
        if (warningSessionId !== "") {
            if (visiblePressureWarning)
                pressureWarningDismissed = true
            if (visibleTemperatureWarning)
                temperatureWarningDismissed = true
        }
        warningVisible = false
    }

    // Switch controls update their own checked value on activation. Re-evaluate
    // these host-backed bindings immediately after dispatch so a browser toggle
    // stays at the confirmed value until the next session snapshot arrives.
    property bool coachVisualBindingRefresh: false
    property bool coachAudioBindingRefresh: false
    readonly property bool coachVisualChecked: {
        if (coachVisualBindingRefresh)
            return SteamService.state.steamCoachVisualEnabled
        return SteamService.state.steamCoachVisualEnabled
    }
    readonly property bool coachAudioChecked: {
        if (coachAudioBindingRefresh)
            return SteamService.state.steamCoachAudioEnabled
        return SteamService.state.steamCoachAudioEnabled
    }

    // Warning connections
    Connections {
        target: SteamService

        function onPressureTooHigh() {
            steamPage.syncActiveSafetyWarnings()
        }
        function onTemperatureTooHigh() {
            steamPage.syncActiveSafetyWarnings()
        }
        function onWarningEventsMissed(count) {
            if (steamPage.StackView.status === StackView.Active)
                steamPage.showPitcherToast(TranslationManager.translate("steam.warning.eventsMissed",
                    "%1 earlier Steam warnings could not be recovered after reconnecting.").arg(count))
        }
        function onDescaleWarning() {
            steamPage.openPostSessionWarning(TranslationManager.translate("steam.warning.descale",
                "Your machine may need descaling. Steam pressure was consistently too high."))
        }
        function onTemperatureWarning(message) {
            steamPage.openPostSessionWarning(message)
        }
        function onScaleBuildupWarning(message) {
            steamPage.openPostSessionWarning(message)
        }
    }

    Connections {
        target: SteamService.state
        function onActiveWarningKindsChanged() { steamPage.syncActiveSafetyWarnings() }
        function onActiveWarningSessionIdChanged() { steamPage.syncActiveSafetyWarnings() }
    }

    // The three SteamService post-session signals above fire after the
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
        AppShell.completionSuspendRequested()
        steamWarningDialog.open()
    }

    // Post-session warning dialog
    DecenzaDialog {
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
            if (steamPage.isSteaming) return
            AppShell.completionFinishRequested()
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
            visible: steamPage.isSteaming || steamPage.steamSoftStopped || steamPage.isSteamHeating || steamPage.isPuffing
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
                        model: SteamService.state.steamPitcherPresets

                        Rectangle {
                            id: livePitcherPill

                            required property int index
                            required property var modelData

                            readonly property bool pitcherDisabled: livePitcherPill.modelData.disabled === true
                            readonly property bool pitcherSelected: SteamLabels.pitcherIsSelected(livePitcherPill.index)

                            // Hide "Off" presets from the mid-session preset row — there's no
                            // meaningful action when tapped mid-steam, so don't show the pill.
                            visible: !(steamPage.isSteaming && livePitcherPill.pitcherDisabled)

                            width: livePitcherText.implicitWidth + 24
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: livePitcherPill.pitcherSelected
                                ? (livePitcherPill.pitcherDisabled ? Theme.textSecondaryColor : Theme.primaryColor)
                                : Theme.surfaceColor
                            border.color: livePitcherPill.pitcherSelected && !livePitcherPill.pitcherDisabled ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: {
                                var label = SteamLabels.pitcherName(livePitcherPill.modelData) + " " + TranslationManager.translate("steam.accessibility.preset", "preset")
                                if (livePitcherPill.pitcherDisabled)
                                    label += ", " + TranslationManager.translate("steam.accessibility.presetOff", "turns steam heater off")
                                var pitcherWt = livePitcherPill.modelData.pitcherWeightG ?? 0
                                if (pitcherWt > 0 && !livePitcherPill.pitcherDisabled)
                                    label += ", " + TranslationManager.translate("steam.accessibility.pitcherWeight", "pitcher") + " " + pitcherWt.toFixed(0) + "g"
                                if (livePitcherPill.pitcherSelected)
                                    label += ", " + TranslationManager.translate("accessibility.selected", "selected")
                                return label
                            }
                            Accessible.focusable: true
                            Accessible.onPressAction: livePitcherMa.clicked(null)

                            Keys.onReturnPressed: function(event) { livePitcherMa.clicked(null); event.accepted = true }
                            Keys.onSpacePressed:  function(event) { livePitcherMa.clicked(null); event.accepted = true }
                            // Step over hidden ("Off") pills the same way Tab does, rather
                            // than parking focus on one the user cannot see.
                            Keys.onLeftPressed: function(event) {
                                for (let i = livePitcherPill.index - 1; i >= 0; i--) {
                                    let prior = livePresetRepeater.itemAt(i)
                                    if (prior && prior.visible) { prior.forceActiveFocus(Qt.TabFocusReason); break }
                                }
                                event.accepted = true
                            }
                            Keys.onRightPressed: function(event) {
                                for (let j = livePitcherPill.index + 1; j < livePresetRepeater.count; j++) {
                                    let candidate = livePresetRepeater.itemAt(j)
                                    if (candidate && candidate.visible) { candidate.forceActiveFocus(Qt.TabFocusReason); break }
                                }
                                event.accepted = true
                            }
                            Keys.onTabPressed: function(event) {
                                var nextPill = null
                                for (let i = livePitcherPill.index + 1; i < livePresetRepeater.count && !nextPill; i++) {
                                    let candidate = livePresetRepeater.itemAt(i)
                                    if (candidate && candidate.visible) nextPill = candidate
                                }
                                var target = nextPill
                                             ?? steamPage.firstLiveControlAfterPresets()
                                             ?? steamPage.firstVisiblePresetPill()
                                if (target) {
                                    target.forceActiveFocus(Qt.TabFocusReason)
                                    event.accepted = true
                                }
                            }
                            Keys.onBacktabPressed: function(event) {
                                var prevPill = null
                                for (let j = livePitcherPill.index - 1; j >= 0 && !prevPill; j--) {
                                    let prior = livePresetRepeater.itemAt(j)
                                    if (prior && prior.visible) prevPill = prior
                                }
                                var back = prevPill
                                           ?? steamPage.lastLiveActionButton()
                                           ?? steamPage.lastVisiblePresetPill()
                                if (back) {
                                    back.forceActiveFocus(Qt.TabFocusReason)
                                    event.accepted = true
                                }
                            }

                            Text {
                                id: livePitcherText
                                anchors.centerIn: parent
                                text: SteamLabels.pitcherName(livePitcherPill.modelData)
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
                                    if (steamPage.isSteaming && livePitcherPill.pitcherDisabled) return
                                    // A debug live preview is presentation only. Do not let
                                    // it dispatch a machine operation while the real machine
                                    // is idle or in another phase.
                                    if (AppShell.debugLiveView
                                            && !SteamService.state.isSteaming)
                                        return
                                    steamPage.requestPageAction("steam-select-pitcher", { index: livePitcherPill.index })
                                    steamingFlowSlider.value = Qt.binding(function() { return SteamService.state.steamFlow })
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // View toggle button (graph/timer)
                Rectangle {
                    id: viewToggleBtn
                    Layout.preferredWidth: Theme.scaled(44)
                    Layout.preferredHeight: Theme.scaled(44)
                    radius: Theme.cardRadius
                    color: viewToggleMa.containsMouse ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor

                    activeFocusOnTab: true
                    Accessible.ignored: true
                    Keys.onReturnPressed: function(event) { viewToggleMa.accessibleClicked(); event.accepted = true }
                    Keys.onSpacePressed:  function(event) { viewToggleMa.accessibleClicked(); event.accepted = true }
                    Keys.onTabPressed: function(event) {
                        var next = steamPage.firstVisiblePresetPill()
                                   ?? steamPage.firstLiveControlAfterPresets()
                        if (next) {
                            next.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                    }
                    Keys.onBacktabPressed: function(event) {
                        var prev = steamPage.lastVisiblePresetPill()
                                   ?? steamPage.lastLiveActionButton()
                        if (prev) {
                            prev.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
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
                            var newMode = steamPage.steamViewMode === "timer" ? "chart" : "timer"
                            SteamService.request("set-steam-view-mode", { value: newMode, expectedValue: steamPage.steamViewMode }, "")
                        }
                    }
                }
            }

            // Warning banner (live warnings during steaming). Tap to dismiss.
            Rectangle {
                id: warningBanner
                Layout.fillWidth: true
                Layout.preferredHeight: warningBannerText.implicitHeight + Theme.spacingSmall * 2
                visible: steamPage.warningVisible
                radius: Theme.cardRadius
                color: Theme.errorColor

                // Composed banner text — a single translatable template so
                // translators control word order relative to the warning.
                readonly property string composedText: TranslationManager.translate(
                    "steam.warning.withTapHint", "%1  (tap to dismiss)").arg(steamPage.warningText)

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
                    onAccessibleClicked: steamPage.dismissSafetyWarning()
                }
            }

            // Live during-steam coaching cues (stretch -> roll -> almost -> done).
            // In-layout right below the warning banner slot — between the preset
            // pills and the countdown — so it never overlaps other content. It
            // self-hides when no cue is active (visible tracks its fade), so it
            // takes no space most of the time. Purely visual, gated on its own
            // opt-in; voice is the coach service's job (separate
            // steamCoachAudioEnabled setting).
            LiveCoachingBanner {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                coach: SteamService.state
                coachEnabled: SteamService.state.steamCoachVisualEnabled
            }

            // === TIMER VIEW (default) ===
            ColumnLayout {
                visible: steamPage.steamViewMode === "timer"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(20)

            Item { Layout.fillHeight: true }

            // Timer with target and adjustment buttons
            Column {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)

                RowLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.spacingMedium

                    // Hidden during heating and puffing
                    LiveAdjustButton {
                        id: decreaseTimeBtn
                        visible: !steamPage.isSteamHeating && !steamPage.isPuffing
                        text: TranslationManager.translate("steam.button.subtract5", "-5 s")
                        accessibleName: TranslationManager.translate("steam.decreaseTime", "Decrease steam time by 5 seconds")
                        KeyNavigation.tab: increaseTimeBtn
                        // Back to the pills, closing the loop. This used to point at the
                        // flow slider, which sits AFTER this button going forward, so the
                        // chain doubled back on itself and never reached the pills.
                        KeyNavigation.backtab: steamPage.lastVisiblePresetPill() ?? steamingFlowSlider
                        enabled: !SteamService.busy
                        onClicked: {
                            if (AppShell.debugLiveView && !SteamService.state.isSteaming)
                                return
                            steamPage.requestPageAction("steam-adjust-time", { delta: -5 })
                        }
                    }

                    Text {
                        id: steamProgressText
                        // Show temperature during heating, countdown during puffing, time during steaming
                        text: {
                            if (steamPage.isSteamHeating) {
                                return steamPage.steamTempPairText
                            } else if (steamPage.isPuffing && SteamService.state.steamAutoFlushCountdown > 0) {
                                return SteamService.state.steamAutoFlushCountdown.toFixed(1) + "s / " + SteamService.state.steamAutoFlushSeconds + "s"
                            } else {
                                return SteamService.state.shotTime.toFixed(1) + "s / " + SteamService.state.steamTimeout + "s"
                            }
                        }
                        color: Theme.textColor
                        font: Theme.timerFont
                    }

                    // Hidden during heating and puffing
                    LiveAdjustButton {
                        id: increaseTimeBtn
                        visible: !steamPage.isSteamHeating && !steamPage.isPuffing
                        text: TranslationManager.translate("steam.button.add5", "+5 s")
                        accessibleName: TranslationManager.translate("steam.increaseTime", "Increase steam time by 5 seconds")
                        KeyNavigation.tab: steamingFlowSlider
                        KeyNavigation.backtab: decreaseTimeBtn
                        enabled: !SteamService.busy
                        onClicked: {
                            if (AppShell.debugLiveView && !SteamService.state.isSteaming)
                                return
                            steamPage.requestPageAction("steam-adjust-time", { delta: 5 })
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
                            if (steamPage.isSteamHeating) {
                                return parent.width * Math.min(1, steamPage.currentSteamTemp / steamPage.targetSteamTemp)
                            } else if (steamPage.isPuffing && SteamService.state.steamAutoFlushSeconds > 0) {
                                // Countdown: progress goes from full to empty
                                return parent.width * Math.min(1, SteamService.state.steamAutoFlushCountdown / SteamService.state.steamAutoFlushSeconds)
                            } else {
                                return parent.width * Math.min(1, SteamService.state.shotTime / SteamService.state.steamTimeout)
                            }
                        }
                        height: parent.height
                        radius: Theme.scaled(4)
                        color: steamPage.isSteamHeating ? Theme.warningColor : (steamPage.isPuffing ? Theme.secondaryColor : Theme.primaryColor)
                    }
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
                    enabled: steamPage.editorReady
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(80)
                    from: 40
                    to: 250
                    stepSize: 5
                    decimals: 0
                    value: SteamService.state.steamFlow
                    displayText: steamPage.flowToDisplay(value)
                    accessibleName: TranslationManager.translate("steam.label.steamFlow", "Steam Flow")
                    KeyNavigation.tab: steamPage.firstLiveActionButton()
                                       ?? steamPage.firstVisiblePresetPill()
                                       ?? steamingFlowSlider
                    // +5s hides during heating and puffing; fall back to the pills rather
                    // than at an invisible target, which KeyNavigation refuses to focus.
                    KeyNavigation.backtab: increaseTimeBtn.visible
                                           ? increaseTimeBtn
                                           : (steamPage.lastVisiblePresetPill() ?? steamingFlowSlider)
                    // BLE write deferred to commit (PR Kulitorum/Decenza#782 pattern): the drag
                    // updates the local value and the pitcher record, and only
                    // the commit writes MMR.
                    //
                    // This used to say the single commit-time write was
                    // "reliable because setSteamFlowImmediate routes through
                    // writeMMRVerified — write, read-back, retry on mismatch".
                    // That mechanism is gone (it cost a second a005 write plus
                    // a retry ladder on the link whose failures were the only
                    // reason to want it, and neither de1app nor decaid verifies
                    // an MMR write at all). What holds the single write up now
                    // is narrower and worth stating plainly: on-device testing
                    // across many slider drags recorded zero retries needed,
                    // and a write that IS abandoned after its retries now drops
                    // its dedup-cache entry (DE1Device::onWriteAbandoned), so
                    // the next commit of the same value is actually re-sent
                    // rather than skipped as unchanged. A lost write costs one
                    // more commit, not a stuck setting.
                    onValueCommitted: function(newValue) { steamPage.requestCurrentPresetFlow(newValue) }
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
                visible: steamPage.steamViewMode === "chart"
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
                            if (steamPage.isSteamHeating) {
                                return steamPage.steamTempPairText
                            } else if (steamPage.isPuffing && SteamService.state.steamAutoFlushCountdown > 0) {
                                return SteamService.state.steamAutoFlushCountdown.toFixed(1) + "s / " + SteamService.state.steamAutoFlushSeconds + "s"
                            } else {
                                return SteamService.state.shotTime.toFixed(1) + "s / " + SteamService.state.steamTimeout + "s"
                            }
                        }
                        color: Theme.textColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }

                    Text {
                        text: steamPage.flowToDisplay(SteamService.state.steamFlow) + " mL/s"
                        color: Theme.flowColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }

                    Text {
                        text: steamPage.currentSteamTempText
                        color: Theme.temperatureColor
                        font: Theme.subtitleFont
                        Accessible.ignored: true
                    }
                }
            }

            // The action row holds only the headless Stop button. On machines with a
            // group-head controller (GHC) steam is stopped from the physical button, so
            // there is no on-screen stop control here. This view has no Flickable — the
            // only one on the page wraps the settings editor — so content past the bottom
            // edge clips instead of scrolling. The row sits outside the timer/chart view
            // switch, so Stop is available in both views.
            RowLayout {
                id: liveActionRow
                Layout.alignment: Qt.AlignHCenter
                // Only the headless Stop button lives here, so the row is shown exactly
                // when that button is. Mirrors the child's own condition rather than
                // reading its `.visible`: an EFFECTIVE-visibility read would latch the row
                // off permanently the first time it existed while hidden and never re-arm.
                visible: SteamService.state.isHeadless
                spacing: Theme.spacingMedium

                // Defines the Stop button box size.
                readonly property int buttonWidth: Theme.scaled(200)
                readonly property int buttonHeight: Theme.scaled(60)

            // Stop button for headless machines.
            // When SteamService.state.steamTwoTapStop is on (default off), behaves as a
            // two-stage button: first tap soft-stops, second tap purges.
            // When off, a single tap stops and triggers the hose purge.
            Rectangle {
                id: steamStopButton
                Layout.preferredWidth: liveActionRow.buttonWidth
                Layout.preferredHeight: liveActionRow.buttonHeight
                visible: SteamService.state.isHeadless
                radius: Theme.cardRadius
                color: stopTapHandler.isPressed
                    ? Qt.darker((steamPage.steamSoftStopped && SteamService.state.steamTwoTapStop) ? Theme.primaryColor : Theme.errorColor, 1.2)
                    : ((steamPage.steamSoftStopped && SteamService.state.steamTwoTapStop) ? Theme.primaryColor : Theme.errorColor)
                border.color: Theme.primaryContrastColor
                border.width: Theme.scaled(2)

                activeFocusOnTab: true
                // stopTapHandler carries role/name/focusable, so this Rectangle must step
                // out of the accessibility tree rather than sit in it unnamed.
                Accessible.ignored: true
                Keys.onReturnPressed: function(event) { stopTapHandler.accessibleClicked(); event.accepted = true }
                Keys.onSpacePressed:  function(event) { stopTapHandler.accessibleClicked(); event.accepted = true }
                // Only swallow the key when there is somewhere to send focus. Accepting
                // it unconditionally traps focus on this button when no preset pill
                // exists to receive it.
                Keys.onTabPressed: function(event) {
                    var next = steamPage.firstVisiblePresetPill()
                    if (next) {
                        next.forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                }
                Keys.onBacktabPressed: function(event) {
                    // Mirror the forward path into this button. In timer view the flow
                    // slider (and the -5s/+5s pair before it) precede Stop, so reversing
                    // out of it must land on the flow slider — otherwise Shift+Tab would
                    // skip those controls. The chart view has neither, so fall back to the
                    // last preset pill. (This mirrors the backtab the removed Purge button
                    // used to carry.)
                    var back = steamPage.steamViewMode === "timer"
                               ? steamingFlowSlider
                               : steamPage.lastVisiblePresetPill()
                    if (back) {
                        back.forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                }

                Text {
                    id: stopButtonText
                    anchors.centerIn: parent
                    // DO NOT use steam.button.purge here. It labelled this button's
                    // second-press state back when that state meant "stop", so the shipped
                    // community translations for it literally say Stop — de "Stopp",
                    // da "Stop", ar "إيقاف". Only steam.label.purge is translated as a
                    // purge: de "Reinigen", fr "Purger", da "Rens", ar "تنظيف".
                    // steam.button.stop is correct and matches espresso/flush/hotwater/
                    // descaling/transport.
                    text: (steamPage.steamSoftStopped && SteamService.state.steamTwoTapStop)
                          ? TranslationManager.translate("steam.label.purge", "Purge")
                          : TranslationManager.translate("steam.button.stop", "STOP")
                    color: Theme.primaryContrastColor
                    font.pixelSize: Theme.scaled(24)
                    font.weight: Font.Bold
                    // The shouted look is applied here rather than baked into the source
                    // string, so the sentence-case purge key can be shouted for this
                    // button's second-tap state without baking the casing in. Case-less
                    // scripts (ar) are unaffected.
                    font.capitalization: Font.AllUppercase
                    Accessible.ignored: true
                }

                // Using TapHandler for better touch responsiveness
                AccessibleTapHandler {
                    id: stopTapHandler
                    anchors.fill: parent
                    enabled: steamPage.pendingNavigationAction === ""
                    accessibleName: steamPage.steamSoftStopped ? TranslationManager.translate("steam.accessible.purge", "Purge the steam wand") : TranslationManager.translate("steam.accessible.stop", "Stop steaming")
                    accessibleItem: steamStopButton
                    onAccessibleClicked: steamPage.stopSteamFromPage()
                }
            }

            } // end Stop action row

            Item { Layout.preferredHeight: Theme.scaled(20) }
        }

        // === SETTINGS VIEW ===
        // Hide during soft-stop (waiting for purge), steam heating, and puffing
        ColumnLayout {
            visible: !steamPage.isSteaming && !steamPage.steamSoftStopped && !steamPage.isSteamHeating && !steamPage.isPuffing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(12)

            // Steam heater heating indicator
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(60)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                visible: steamPage.isHeatingUp

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(15)

                    // Heating icon (animated).
                    //
                    // Was `text: "\ue88a"` in font.family "Material Icons" — a
                    // private-use codepoint in a font this app does not bundle
                    // (resources/fonts/ carries only Decenza Sans and Noto Sans
                    // Math). It was the ONLY Material Icons reference left in the
                    // tree, so nothing supplied that family and the glyph
                    // resolved to whatever the host happened to have, or to a
                    // missing-glyph box. Now a themed SVG, which is what the rest
                    // of the app's chrome uses and which follows Theme colours.
                    ThemedIcon {
                        source: "qrc:/icons/temperature.svg"
                        iconSize: Theme.scaled(28)
                        color: Theme.warningColor

                        SequentialAnimation on opacity {
                            running: steamPage.isHeatingUp
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
                                width: parent.width * Math.min(1, Math.max(0, steamPage.currentSteamTemp / steamPage.targetSteamTemp))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.warningColor
                            }
                        }
                    }

                    // Temperature display
                    Text {
                        text: steamPage.steamTempPairText
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(14)
                    }
                }
            }

            // Pitcher Presets Section
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.cardBackgroundColor
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
                            model: SteamService.state.steamPitcherPresets

                            RepeaterDelegateItem {
                                id: pitcherDelegate

                                required property int index
                                required property var modelData

                                width: pitcherPill.width
                                height: Theme.scaled(36)

                                itemIndex: pitcherDelegate.index
                                focusTarget: pitcherPill

                                Rectangle {
                                    id: pitcherPill
                                    readonly property bool pitcherDisabled: pitcherDelegate.modelData.disabled === true
                                    readonly property bool pitcherSelected: SteamLabels.pitcherIsSelected(pitcherDelegate.itemIndex)

                                    width: pitcherText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: pitcherPill.pitcherSelected
                                        ? (pitcherPill.pitcherDisabled ? Theme.textSecondaryColor : Theme.primaryColor)
                                        : Theme.insetBackgroundColor
                                    border.color: pitcherPill.pitcherSelected && !pitcherPill.pitcherDisabled ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: dragArea.drag.active ? 0.8 : 1.0

                                    function applyPitcher() {
                                        if (AppShell.debugLiveView
                                                && !SteamService.state.isSteaming)
                                            return
                                        steamPage.requestPageAction("steam-select-pitcher", { index: pitcherDelegate.itemIndex })
                                    }

                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: {
                                        var label = SteamLabels.pitcherName(pitcherDelegate.modelData) + " " + TranslationManager.translate("steam.accessibility.preset", "preset")
                                        if (pitcherDisabled)
                                            label += ", " + TranslationManager.translate("steam.accessibility.presetOff", "turns steam heater off")
                                        var pitcherWt = pitcherDelegate.modelData.pitcherWeightG ?? 0
                                        if (pitcherWt > 0 && !pitcherDisabled)
                                            label += ", " + TranslationManager.translate("steam.accessibility.pitcherWeight", "pitcher") + " " + pitcherWt.toFixed(0) + "g"
                                        if (pitcherSelected)
                                            label += ", " + TranslationManager.translate("accessibility.selected", "selected")
                                        return label
                                    }
                                    // Don't invite a gesture that will be refused: the
                                    // built-in "Heater off" entry cannot be edited.
                                    Accessible.description: pitcherPill.pitcherDisabled
                                        ? ""
                                        : TranslationManager.translate("steam.accessibility.pitcherEditHint", "Double-tap or long-press to edit preset.")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: pitcherPill.applyPitcher()

                                    Keys.onReturnPressed: function(event) {
                                        pitcherPill.applyPitcher()
                                        event.accepted = true
                                    }
                                    Keys.onSpacePressed: function(event) {
                                        pitcherPill.applyPitcher()
                                        event.accepted = true
                                    }
                                    Keys.onLeftPressed: function(event) {
                                        if (pitcherDelegate.index > 0) steamPage.focusPitcherAt(pitcherDelegate.index - 1)
                                        event.accepted = true
                                    }
                                    Keys.onRightPressed: function(event) {
                                        if (pitcherDelegate.index < pitcherRepeater.count - 1) steamPage.focusPitcherAt(pitcherDelegate.index + 1)
                                        event.accepted = true
                                    }
                                    Keys.onTabPressed: function(event) {
                                        if (pitcherDelegate.index < pitcherRepeater.count - 1)
                                            steamPage.focusPitcherAt(pitcherDelegate.index + 1)
                                        else
                                            addPitcherButton.forceActiveFocus(Qt.TabFocusReason)
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: function(event) {
                                        if (pitcherDelegate.index > 0)
                                            steamPage.focusPitcherAt(pitcherDelegate.index - 1)
                                        else if (steamPage.realScaleConnected)
                                            savePitcherWeightBtn.forceActiveFocus(Qt.TabFocusReason)
                                        else
                                            steamTempSlider.forceActiveFocus(Qt.TabFocusReason)
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
                                        text: SteamLabels.pitcherName(pitcherDelegate.modelData)
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
                                            dragArea.held = false
                                            dragArea.moved = false
                                            holdTimer.start()
                                        }

                                        onReleased: {
                                            holdTimer.stop()
                                            if (!dragArea.moved && !dragArea.held) {
                                                // Simple click - select the pitcher
                                                pitcherPill.applyPitcher()
                                            }
                                            pitcherPill.Drag.drop()
                                            pitcherPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (dragArea.drag.active) {
                                                dragArea.moved = true
                                                pitcherPresetsRow.draggedIndex = pitcherDelegate.itemIndex
                                            }
                                        }

                                        // The built-in "Heater off" entry is not in the
                                        // stored array: SettingsBrew refuses to rename,
                                        // delete or reorder it with a qWarning and a void
                                        // return. That refusal is invisible from here — the
                                        // dialog opened on a nameless entry and CLOSED as if
                                        // Save/Delete had worked. Refuse at the gesture, so
                                        // the affordance is never offered.
                                        onDoubleClicked: {
                                            holdTimer.stop()
                                            if (pitcherPill.pitcherDisabled)
                                                return
                                            dragArea.held = true  // Prevent single-click selection on release
                                            steamPage.editingPitcherIndex = pitcherDelegate.itemIndex
                                            editPitcherNameInput.text = pitcherDelegate.modelData.name
                                            editPitcherPopup.open()
                                        }

                                        Timer {
                                            id: holdTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!dragArea.moved && !pitcherPill.pitcherDisabled) {
                                                    dragArea.held = true
                                                    steamPage.editingPitcherIndex = pitcherDelegate.itemIndex
                                                    editPitcherNameInput.text = pitcherDelegate.modelData.name
                                                    editPitcherPopup.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        // Guarded like the DelegateModel drop targets in
                                        // FavoritesListView / LayoutEditorZone. `itemIndex` is a
                                        // SHARED property now, so a drag from an unrelated
                                        // reorderable list would answer with a plausible integer
                                        // instead of undefined and silently reorder this list.
                                        var src = drag.source as RepeaterDelegateItem
                                        if (!src || src === pitcherDelegate) return
                                        var fromIndex = src.itemIndex
                                        var toIndex = pitcherDelegate.itemIndex
                                        if (fromIndex !== toIndex) {
                                            const presets = SteamService.state.steamPitcherPresets
                                            steamPage.beginEditorRequest("move-preset",
                                                { fromIndex: fromIndex,
                                                  expectedFromName: presets[fromIndex].name,
                                                  toIndex: toIndex,
                                                  expectedToName: presets[toIndex].name }, "")
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
                            // Its AccessibleTapHandler carries role/name/focusable, so this
                            // Rectangle must step out of the accessibility tree rather than
                            // sit in it unnamed.
                            Accessible.ignored: true
                            KeyNavigation.tab: durationSlider
                            KeyNavigation.backtab: pitcherRepeater.count > 0
                                ? steamPage.pitcherFocusTarget(pitcherRepeater.count - 1)
                                : durationSlider
                            Keys.onReturnPressed: function(event) { addPitcherDialog.open(); event.accepted = true }
                            Keys.onSpacePressed:  function(event) { addPitcherDialog.open(); event.accepted = true }

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
                color: Theme.cardBackgroundColor
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
                    enabled: steamPage.editorReady
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
                            value: steamPage.getCurrentPitcherDuration()
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.duration", "Duration")
                            KeyNavigation.tab: flowSlider
                            KeyNavigation.backtab: addPitcherButton
                            onValueCommitted: function(newValue) {
                                steamPage.requestCurrentPresetDuration(newValue)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

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
                            value: steamPage.getCurrentPitcherFlow()
                            displayText: steamPage.flowToDisplay(value)
                            valueColor: Theme.primaryColor
                            accessibleName: TranslationManager.translate("steam.label.steamFlow", "Steam Flow")
                            KeyNavigation.tab: steamTempSlider
                            KeyNavigation.backtab: durationSlider
                            onValueCommitted: function(newValue) {
                                steamPage.requestCurrentPresetFlow(newValue)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

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
                            from: Theme.cToDisplay(120)
                            to: Theme.cToDisplay(170)
                            stepSize: 1
                            decimals: 0
                            suffix: Theme.tempUnitSuffix()
                            // Stored in Celsius; shown and entered in the user's unit.
                            value: Theme.cToDisplay(SteamService.state.steamTemperature)
                            valueColor: Theme.temperatureColor
                            accessibleName: TranslationManager.translate("steam.label.temperature.accessible", "Steam Temperature")
                            KeyNavigation.tab: pitcherWeightInput
                            KeyNavigation.backtab: flowSlider
                            onValueCommitted: function(newValue) {
                                steamPage.requestCurrentPresetTemperature(Theme.displayToC(newValue))
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

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
                                    // Preview the weight-timed steam at your last measured milk, under
                                    // the current global rate. Tracks preset + global-rate + last-milk
                                    // changes so it recomputes. Only shown while weight-timing is actually
                                    // active (auto-capture enabled AND a global rate calibrated) and there IS a
                                    // last measured milk — in that regime effectiveSteamDurationSec returns
                                    // the weight-scaled time, so the hint matches real behaviour. Driven by
                                    // the live lastSteamMilkG (not the retired per-preset calibMilkG, which
                                    // has no UI left to edit and would freeze on a stale value).
                                    var milk = SteamService.state.lastSteamMilkG
                                    var duration = SteamService.state.lastSteamEffectiveDurationSec
                                    if (milk > 0 && duration > 0)
                                        return TranslationManager.translate("steam.hint.weightTimed", "Weight-timed") + ": " + milk.toFixed(0) + "g → " + duration + "s"
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
                                var _ = SteamService.state.steamPitcherPresets
                                var preset = SteamService.getSteamPitcherPreset(SteamService.state.selectedSteamPitcher)
                                return preset ? (preset.pitcherWeightG ?? 0) : 0
                            }
                            valueColor: Theme.weightColor
                            accessibleName: TranslationManager.translate("steam.label.pitcherWeight.accessible", "Milk pitcher weight")
                            // tareBtn lives in the scale-gated sub-row; skip straight to
                            // the steam-rate field when no scale is connected so Tab
                            // never lands on a hidden element.
                            KeyNavigation.tab: (steamPage.realScaleConnected) ? tareBtn : steamRateInput
                            KeyNavigation.backtab: steamTempSlider
                            onValueCommitted: function(newValue) {
                                steamPage.requestCurrentPitcherWeight(newValue)
                            }
                        }

                        // Scale controls: live reading + Tare + Weigh/Clear (only with a scale).
                        RowLayout {
                            spacing: Theme.scaled(8)
                            visible: steamPage.realScaleConnected

                            Text {
                                text: SteamService.state.scaleWeight.toFixed(0) + "g"
                                color: Theme.textSecondaryColor
                                font: Theme.bodyFont
                                Accessible.ignored: true
                            }

                            // Tare button
                            Rectangle {
                                id: tareBtn
                                Layout.preferredWidth: Theme.scaled(80)
                                Layout.preferredHeight: Theme.scaled(44)
                                radius: Theme.cardRadius
                                color: tareBtnMa.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                                border.color: Theme.borderColor
                                border.width: 1

                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("steam.accessible.tare", "Tare scale")
                                Accessible.focusable: true
                                Accessible.onPressAction: tareBtnMa.clicked(null)
                                Keys.onReturnPressed: function(event) { tareBtnMa.clicked(null); event.accepted = true }
                                Keys.onSpacePressed:  function(event) { tareBtnMa.clicked(null); event.accepted = true }
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

                                MouseArea { id: tareBtnMa; anchors.fill: parent; onClicked: SteamService.request("tare", {}, SteamService.state.controlContextId) }
                            }

                            // Weigh the empty pitcher from the scale: the reading is stored
                            // immediately and confirmed with a toast. When the scale reads ~0
                            // the button becomes "Clear" (storing 0 disables the feature).
                            Rectangle {
                                id: savePitcherWeightBtn
                                readonly property bool isClear: SteamService.state.scaleWeight < 5.0
                                Layout.preferredWidth: Theme.scaled(80)
                                Layout.preferredHeight: Theme.scaled(44)
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
                                Keys.onReturnPressed: function(event) { savePitcherWtMa.clicked(null); event.accepted = true }
                                Keys.onSpacePressed:  function(event) { savePitcherWtMa.clicked(null); event.accepted = true }
                                KeyNavigation.tab: steamRateInput
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
                                        var idx = SteamService.state.selectedSteamPitcher
                                        var preset = SteamService.getSteamPitcherPreset(idx)
                                        var pName = SteamLabels.pitcherName(preset)
                                        if (steamPage.pendingEditorRequestId !== "") return
                                        const operation = savePitcherWeightBtn.isClear
                                            ? "clear-pitcher-weight" : "store-current-pitcher-weight"
                                        steamPage.beginEditorRequest(operation,
                                            { index: idx, expectedName: preset ? preset.name : "" }, pName)
                                    }
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

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
                        checked: SteamService.state.milkAutoCaptureEnabled
                        onToggled: {
                            steamPage.beginEditorRequest("set-milk-auto-capture",
                                { enabled: checked }, "")
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // ── Steam rate calibration (GLOBAL — applies to every pitcher).
                    // One "seconds per gram of milk" replaces the old per-pitcher
                    // reference-milk scaling: calibrate once, every pitcher (same steam
                    // flow) finishes at the same temperature. Shows the current rate,
                    // a "Calibrate from last steam" button, and a manual ± adjust. ──
                    ColumnLayout {
                        id: steamRateSection
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        visible: !steamPage.currentPitcherDisabled
                        readonly property bool calibrated: SteamService.state.steamSecondsPerGram > 0
                        readonly property bool hasLast: SteamService.state.lastSteamMilkG > 0 && SteamService.state.lastSteamTimeS > 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(16)

                            Column {
                                Tr {
                                    key: "steam.rate.title"
                                    fallback: "Steam rate"
                                    color: Theme.textColor
                                    font.pixelSize: Theme.scaled(24)
                                }
                                Text {
                                    text: steamRateSection.calibrated
                                          ? TranslationManager.translate("steam.rate.value", "%1 sec per gram of milk")
                                                .arg(SteamService.state.steamSecondsPerGram.toFixed(2))
                                          : TranslationManager.translate("steam.rate.uncalibrated", "Not yet calibrated — weight-timed steaming is off until you calibrate.")
                                    color: Theme.textSecondaryColor
                                    font: Theme.labelFont
                                }
                            }

                            Item { Layout.fillWidth: true }

                            ValueInput {
                                id: steamRateInput
                                Layout.preferredWidth: Theme.scaled(170)
                                from: 0
                                to: 2.0
                                stepSize: 0.01
                                decimals: 2
                                suffix: TranslationManager.translate("steam.rate.suffix", " s/g")
                                value: SteamService.state.steamSecondsPerGram
                                valueColor: Theme.primaryColor
                                accessibleName: TranslationManager.translate("steam.rate.title.accessible", "Steam rate seconds per gram")
                                KeyNavigation.tab: pitcherRepeater.count > 0 ? steamPage.pitcherFocusTarget(0) : addPitcherButton
                                KeyNavigation.backtab: (steamPage.realScaleConnected) ? savePitcherWeightBtn : pitcherWeightInput
                                onValueCommitted: function(newValue) {
                                    steamPage.beginEditorRequest("set-steam-seconds-per-gram",
                                        { value: newValue }, "")
                                }
                            }
                        }

                        // Honest framing: one global rate across all pitchers is a
                        // simplification, since presets still carry independent flow/temp.
                        Tr {
                            key: "steam.rate.note"
                            fallback: "Simplification: the rate is calibrated once and applied to every pitcher. Presets can still carry their own flow and temperature, so this is a simpler mental model rather than an exact physical guarantee."
                            Layout.fillWidth: true
                            color: Theme.textSecondaryColor
                            font: Theme.labelFont
                            opacity: 0.85
                            wrapMode: Text.WordWrap
                        }

                        // Calibrate from the last actual steam session (milk + time) —
                        // derives the global seconds-per-gram and turns weight-timing on.
                        RowLayout {
                            id: useLastSteamRow
                            Layout.fillWidth: true
                            spacing: Theme.scaled(12)

                            Text {
                                Layout.fillWidth: true
                                text: steamRateSection.hasLast
                                      ? TranslationManager.translate("steam.lastSteam.valuesGlobal", "Last steam: %1 g milk → %2 s")
                                            .arg(SteamService.state.lastSteamMilkG.toFixed(0)).arg(Math.round(SteamService.state.lastSteamTimeS))
                                      : TranslationManager.translate("steam.lastSteam.none", "Steam a weighed pitcher to your liking first, then come back here.")
                                color: Theme.textSecondaryColor
                                font: Theme.labelFont
                                wrapMode: Text.WordWrap
                            }

                            AccessibleButton {
                                Layout.preferredHeight: Theme.scaled(44)
                                text: TranslationManager.translate("steam.lastSteam.use", "Use last steam")
                                accessibleName: TranslationManager.translate("steam.lastSteam.useAccessibleGlobal", "Calibrate the global steam rate from your last steam session")
                                primary: true
                                enabled: steamRateSection.hasLast
                                onClicked: {
                                    steamPage.beginEditorRequest("calibrate-from-last-steam", {}, "")
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled }

                    // Live expected steam time for the milk currently on the scale
                    // (only when the preset is calibrated and milk is present).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(16)
                        visible: !steamPage.currentPitcherDisabled
                                 && steamPage.realScaleConnected
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

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.textSecondaryColor; opacity: 0.3; visible: !steamPage.currentPitcherDisabled && steamPage.realScaleConnected && steamPage.scaledSteamTimeout() > 0 }

                    // ── Coaching (GLOBAL — not per-pitcher): live cues while steaming.
                    // Every row above changes with the selected pitcher preset; these two
                    // toggles bind to Settings.app and stay put when presets switch, so
                    // they are rendered as their own headed group, not more preset rows.
                    // Banner and voice are independent opt-ins, both off by default. ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)
                        visible: !steamPage.currentPitcherDisabled

                        Tr {
                            Layout.fillWidth: true
                            key: "steam.coaching.title"
                            fallback: "Coaching"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(26)
                            font.bold: true
                            Accessible.ignored: true
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "steam.coaching.summary"
                            fallback: "Short cues while steaming — stretch, roll, almost there, done. Coaching needs the milk weight: enable weight-timed steaming and rest the pitcher on the scale before steaming, or no cues are given. Applies to every pitcher. The banner and the voice are separate switches; the voice speaks even when accessibility is off."
                            wrapMode: Text.WordWrap
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }
                    }

                    StyledSwitch {
                        Layout.fillWidth: true
                        visible: !steamPage.currentPitcherDisabled
                        text: TranslationManager.translate("steam.coaching.banner", "Show coaching banner")
                        accessibleName: text
                        checked: steamPage.coachVisualChecked
                        onToggled: {
                            SteamService.request("set-steam-coach-visual",
                                { value: checked,
                                  expectedValue: SteamService.state.steamCoachVisualEnabled }, "")
                            steamPage.coachVisualBindingRefresh = !steamPage.coachVisualBindingRefresh
                        }
                    }

                    StyledSwitch {
                        Layout.fillWidth: true
                        visible: !steamPage.currentPitcherDisabled
                        text: TranslationManager.translate("steam.coaching.voice", "Speak coaching cues")
                        accessibleName: text
                        checked: steamPage.coachAudioChecked
                        onToggled: {
                            SteamService.request("set-steam-coach-audio",
                                { value: checked,
                                  expectedValue: SteamService.state.steamCoachAudioEnabled }, "")
                            steamPage.coachAudioBindingRefresh = !steamPage.coachAudioBindingRefresh
                        }
                    }

                }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: steamPage.isSteaming || steamPage.steamSoftStopped }
    }

    // Present confirmation for a milk capture reported by the host. Capture,
    // locking, and steam-time updates are owned by the preparation service.
    MilkCapture {
        id: milkCapture
        onMilkCaptured: function(milk, t) {
            if (steamPage.StackView.status !== StackView.Active
                    || !SteamService.state.milkAutoCaptureEnabled || t <= 0)
                return
            steamPage.captureBannerText =
                TranslationManager.translate("steam.capture.locked", "Steam time set: %1s for %2g milk")
                    .arg(t).arg(milk.toFixed(0))
            steamPage.captureBannerVisible = true
            captureBannerTimer.restart()
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(steamPage.captureBannerText)
        }
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

    // (During-steam coaching banner is provided by upstream's LiveCoachingBanner
    // near the top of this page, gated on SteamService.state.steamCoachVisualEnabled.)

    // Small flashing reminder while the milk pitcher is settling on the scale, or why a
    // settled load is not being captured (SteamLabels.captureHint). Disappears the
    // instant it captures (the bell rings).
    Text {
        id: steamWaitForBellHint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(70)
        z: 1000
        horizontalAlignment: Text.AlignHCenter
        readonly property string milkHint: SteamLabels.captureHint(milkCapture)
        visible: milkHint !== "" || (milkCapture.active && !milkCapture.isCaptured
                 && milkCapture.cupWeight > 0
                 && milkCapture.loadPresent
                 && milkCapture.netWeight >= milkCapture.minNet
                 && milkCapture.netWeight <= milkCapture.maxNet)
        text: milkHint !== "" ? milkHint
            : TranslationManager.translate("scale.waitForBell", "Wait for the bell before you take it off the scale")
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
        target: SteamService.state
        enabled: !steamPage.isSteaming && !steamPage.steamSoftStopped
                 && steamPage.realScaleConnected
                 && typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled
                 && AccessibilityManager.extractionAnnouncementsEnabled
        function onScaleWeightChanged() {
            var w = SteamService.state.scaleWeight
            // Reset milestone tracker after taring
            if (w < 1.0) { steamPage._lastAnnouncedSteamWeight = 0; return }
            var mode = AccessibilityManager.extractionAnnouncementMode
            if (mode !== "milestones_only" && mode !== "both") return
            // Announce every 10g milestone while weighing milk
            if (Math.floor(w / 10) > Math.floor(steamPage._lastAnnouncedSteamWeight / 10)) {
                AccessibilityManager.announce(Math.floor(w) + " " +
                    TranslationManager.translate("espresso.accessibility.grams", "grams"))
                steamPage._lastAnnouncedSteamWeight = w
            }
        }
    }

    Timer {
        id: steamWeightAnnounceTimer
        interval: AccessibilityManager.extractionAnnouncementInterval * 1000
        repeat: true
        running: !steamPage.isSteaming && !steamPage.steamSoftStopped
                 && steamPage.realScaleConnected
                 && typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled
                 && AccessibilityManager.extractionAnnouncementsEnabled
                 && (AccessibilityManager.extractionAnnouncementMode === "timed" ||
                     AccessibilityManager.extractionAnnouncementMode === "both")
        onTriggered: {
            if (SteamService.state.scaleWeight < 1.0) return
            var weight = SteamService.state.scaleWeight.toFixed(0)
            AccessibilityManager.announce(
                TranslationManager.translate("espresso.accessibility.weight", "weight") + " " + weight + " " +
                TranslationManager.translate("espresso.accessibility.grams", "grams"))
        }
    }

    // Hidden translation helper for "No pitcher"
    Tr { id: noPitcherText; key: "steam.label.noPitcher"; fallback: "No pitcher"; visible: false }

    // Bottom bar (hide during soft-stop waiting for purge, steam heating, and puffing)
    BottomBar {
        visible: !steamPage.isSteaming && !steamPage.steamSoftStopped && !steamPage.isSteamHeating && !steamPage.isPuffing
        title: steamPage.getCurrentPitcherName() || noPitcherText.text
        onBackClicked: steamPage.leaveSteamFromPage()

        // The built-in "Heater off" entry carries no duration, flow or
        // temperature. The sliders keep the LAST real pitcher's values while
        // hidden, so these three read them out and the bar claimed "60s, Flow
        // 1.00, 160°C" for an entry with none — numbers belonging to whichever
        // pitcher happened to be selected before. Show the same word the
        // readouts use instead. Keyed on the PRESET, not on the resolved heater
        // state: with a real pitcher selected and the boiler merely idle-cold,
        // these values still describe what you would steam with.
        Text {
            text: steamPage.heaterOffLabel
            visible: steamPage.currentPitcherDisabled
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Text {
            text: durationSlider.value.toFixed(0) + "s"
            visible: !steamPage.currentPitcherDisabled
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle {
            width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3
            visible: !steamPage.currentPitcherDisabled
        }
        Tr {
            id: flowLabelText
            key: "steam.label.flow"
            fallback: "Flow"
            visible: false
        }
        Text {
            text: flowLabelText.text + " " + steamPage.flowToDisplay(flowSlider.value)
            visible: !steamPage.currentPitcherDisabled
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
        Rectangle {
            width: 1; height: Theme.scaled(30); color: Theme.primaryContrastColor; opacity: 0.3
            visible: !steamPage.currentPitcherDisabled
        }
        Text {
            text: steamTempSlider.value.toFixed(0) + Theme.tempUnitSuffix()
            visible: !steamPage.currentPitcherDisabled
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }


    // Edit Pitcher Popup (rename/delete)
    DecenzaDialog {
        id: editPitcherPopup
        closePolicy: steamPage.pendingEditorRequestId.length > 0
            ? Dialog.NoAutoClose : Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        x: (parent.width - width) / 2
        y: editPitcherPopupAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true

        property bool editPitcherPopupAtTop: false
        onOpened: {
            editPitcherPopupAtTop = false
            editPitcherNameInput.forceActiveFocus()
        }
        onClosed: editPitcherPopupAtTop = false

        Connections {
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && editPitcherPopup.opened) {
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
                    enabled: steamPage.editorReady
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
                    enabled: steamPage.editorReady
                    text: deleteButtonText.text
                    accessibleName: TranslationManager.translate("steam.deletePitcherPreset", "Delete this pitcher preset")
                    destructive: true
                    KeyNavigation.tab: editCancelButton
                    KeyNavigation.backtab: editPitcherNameInput
                    onClicked: {
                        const preset = SteamService.getSteamPitcherPreset(
                            steamPage.editingPitcherIndex)
                        steamPage.beginEditorRequest("delete-preset",
                            { index: steamPage.editingPitcherIndex,
                              expectedName: preset ? preset.name : "" },
                            preset ? preset.name : "")
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    id: editCancelButton
                    enabled: steamPage.pendingEditorRequestId.length === 0
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
                    // Recipes snapshot the pitcher BY NAME, so a blank name is
                    // one nothing can refer to and a duplicate makes two
                    // pitchers indistinguishable. ignoreIndex is this preset —
                    // keeping its own name is not a clash.
                    enabled: steamPage.editorReady && editPitcherNameInput.text.trim().length > 0
                        && !SteamService.steamPitcherNameTaken(editPitcherNameInput.text, steamPage.editingPitcherIndex)
                    KeyNavigation.tab: editPitcherNameInput
                    KeyNavigation.backtab: editCancelButton
                    onClicked: {
                        Keyboard.commit()
                        if (editPitcherNameInput.text.trim().length === 0
                                || SteamService.steamPitcherNameTaken(editPitcherNameInput.text, steamPage.editingPitcherIndex))
                            return
                        const preset = SteamService.getSteamPitcherPreset(
                            steamPage.editingPitcherIndex)
                        const newName = editPitcherNameInput.text.trim()
                        steamPage.beginEditorRequest("rename-preset",
                            { index: steamPage.editingPitcherIndex,
                              expectedName: preset ? preset.name : "", newName: newName },
                            preset ? preset.name : "")
                    }
                }
            }
        }
    }

    // Add Pitcher Dialog
    DecenzaDialog {
        id: addPitcherDialog
        closePolicy: steamPage.pendingEditorRequestId.length > 0
            ? Dialog.NoAutoClose : Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        x: (parent.width - width) / 2
        y: addPitcherDialogAtTop ? Theme.scaled(40) : (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true

        property bool addPitcherDialogAtTop: false
        onOpened: {
            addPitcherDialogAtTop = false
            newPitcherName.text = ""
            newPitcherName.forceActiveFocus()
        }
        onClosed: addPitcherDialogAtTop = false

        Connections {
            target: Keyboard
            function onVisibleChanged() {
                if (Keyboard.visible && addPitcherDialog.opened) {
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

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: newPitcherName
                    enabled: steamPage.editorReady
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

                // No "Off" button here any more: every machine has one built-in
                // "Heater off" entry in the pitcher row, so a user-created one
                // would be a duplicate switch with its own name to keep track of.
                AccessibleButton {
                    id: addCancelPitcherButton
                    enabled: steamPage.pendingEditorRequestId.length === 0
                    text: addCancelButtonText.text
                    accessibleName: TranslationManager.translate("steam.cancelAddingPitcher", "Cancel adding new pitcher preset")
                    KeyNavigation.tab: addPitcherConfirmButton
                    KeyNavigation.backtab: newPitcherName
                    onClicked: addPitcherDialog.close()
                }

                AccessibleButton {
                    id: addPitcherConfirmButton
                    primary: true
                    text: addButtonText.text
                    accessibleName: TranslationManager.translate("steam.addNewPitcher", "Add new pitcher preset with entered name")
                    enabled: steamPage.editorReady && newPitcherName.text.trim().length > 0
                        && !SteamService.steamPitcherNameTaken(newPitcherName.text, -1)
                    KeyNavigation.tab: newPitcherName
                    KeyNavigation.backtab: addCancelPitcherButton
                    onClicked: {
                        Keyboard.commit()
                        if (newPitcherName.text.trim() !== "") {
                            steamPage.beginEditorRequest("create-preset",
                                { name: newPitcherName.text.trim() }, "")
                        }
                    }
                }
            }
        }
    }

    // Update sliders when the selected pitcher changes. SettingsBrew owns the
    // corresponding temperature synchronization so this shared page stays read-only.
    Connections {
        target: SteamService.state
        function onSelectedSteamPitcherChanged() {
            durationSlider.value = steamPage.getCurrentPitcherDuration()
            flowSlider.value = steamPage.getCurrentPitcherFlow()
        }
        function onSteamPitcherPresetsChanged() {
            durationSlider.value = steamPage.getCurrentPitcherDuration()
            flowSlider.value = steamPage.getCurrentPitcherFlow()
        }
        function onSteamTimeoutChanged() {
            durationSlider.value = SteamService.state.steamTimeout
        }
    }
}
