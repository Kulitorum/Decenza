import QtQuick
import Decenza

// The one milk-weight capture, used by the idle steam flow, the steam page and the shot
// review button. Net milk = settled load − empty-scale reading − the selected pitcher's
// saved weight, so it needs a saved pitcher weight and is robust to an un-zeroed scale.
// Each user sets only `active` (when its page may capture) and, if needed, `lockTime`.
StableWeightCapture {
    id: root

    // False keeps a manual ±5 steam time; the milk is still recorded for the baseline.
    property bool lockTime: true

    // After the milk is applied. `seconds` is the steam time set, 0 when none could be.
    signal milkCaptured(real milk, int seconds)

    // A settled load was rejected and has not moved since: lighter than the empty
    // pitcher (wrong pitcher selected, or its saved weight is wrong), or with less than
    // `minNet` of milk. Lets a page say why it is not capturing instead of looking stuck.
    readonly property real pitcherSlackG: 5
    readonly property bool _rejectedHolds: !isNaN(_rejectedRaw) && loadPresent
                                           && Math.abs(rawWeight - _rejectedRaw) <= tolerance
    readonly property bool lighterThanPitcher: _rejectedHolds && _rejectedNet < -pitcherSlackG
    readonly property bool belowMinimum: _rejectedHolds && _rejectedNet >= -pitcherSlackG && _rejectedNet < minNet
    property real _rejectedRaw: NaN
    property real _rejectedNet: NaN

    // A fresh steam attempt. Deliberately no tare: the capture measures from the empty
    // reading it sees settle, and the HDS over WiFi (fw 3.1.14) sends garbage frames of
    // roughly ±1,000 g around a tare. Milk from an earlier capture is set aside, so a
    // stale weight cannot scale this attempt, and given back by cancelAttempt().
    function startAttempt() {
        _log("attempt started")
        _milkBeforeAttempt = AppShell.sessionMeasuredMilkG
        _attemptOpen = true
        AppShell.sessionMeasuredMilkG = 0
    }
    function cancelAttempt() {
        if (!_attemptOpen)
            return
        _attemptOpen = false
        AppShell.sessionMeasuredMilkG = _milkBeforeAttempt
        _log("attempt cancelled")
    }
    property real _milkBeforeAttempt: 0
    property bool _attemptOpen: false

    // The load above the empty reading this capture settled on, published while it is
    // active so the pitcher pills and the Weight widget's Net milk mode read from the same
    // zero the capture uses — the scale's own zero is not the empty scale after a shot.
    // Owner-tracked: as one page's capture turns off another's may already have turned
    // on, and the first must not clear the second's value.
    function _publish() {
        if (active && _seeded) {
            AppShell.milkScaleLoadOwner = root
            AppShell.milkScaleLoadG = loadPresent ? rawWeight - virtualZero : 0
        } else if (AppShell.milkScaleLoadOwner === root) {
            AppShell.milkScaleLoadOwner = null
            AppShell.milkScaleLoadG = NaN
        }
    }
    onRawWeightChanged: _publish()
    on_SeededChanged: _publish()
    Component.onDestruction: if (AppShell.milkScaleLoadOwner === root) {
        AppShell.milkScaleLoadOwner = null
        AppShell.milkScaleLoadG = NaN
    }

    // DEBUG: the zero, load and capture sequence on a WiFi scale is only reconstructable
    // from a log.
    function _log(event) {
        WebDebugLogger.debug("Steam", "MilkCapture", [event, "raw=" + rawWeight.toFixed(1),
            "zero=" + virtualZero.toFixed(1), "pitcher=" + cupWeight.toFixed(1),
            "net=" + (rawWeight - virtualZero - cupWeight).toFixed(1)].join(" "))
    }
    property real _lastLoggedZero: NaN
    property bool _wasActive: false
    // The empty branch re-adopts on every settled wobble; log only a real move.
    onVirtualZeroChanged: {
        _publish()
        if (!active || (!isNaN(_lastLoggedZero) && Math.abs(virtualZero - _lastLoggedZero) < tolerance))
            return
        _lastLoggedZero = virtualZero
        _log("zero adopted")
    }
    onLoadPresentChanged: {
        _publish()
        if (active)
            _log(loadPresent ? "load placed" : "load removed")
    }
    onActiveChanged: {
        _lastLoggedZero = NaN
        _rejectedRaw = NaN
        if (active || _wasActive)
            _log(active ? "armed" : "disarmed")
        _wasActive = active
        _publish()
    }
    onStableRejected: function(net) {
        _rejectedRaw = rawWeight
        _rejectedNet = net
        _log("settled outside " + minNet + "-" + maxNet + " g, not captured")
    }

    rawWeight: (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) ? MachineState.scaleWeight : 0
    cupWeight: {
        void(Settings.brew.steamPitcherPresets)  // the lookup is an invokable, which records no dependency
        var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
        return (p && !p.disabled) ? (p.pitcherWeightG ?? 0) : 0
    }
    minNet: 50   // nobody steams < 50 g milk; the floor also keeps a bean cup from tripping it
    maxNet: 1500
    tolerance: 1.5
    stableMs: 2500

    // Records the milk even when no time can be set, so the first steam on an
    // uncalibrated rate can still be adopted as the baseline.
    function _apply(milk) {
        AppShell.sessionMeasuredMilkG = milk
        if (!root.lockTime)
            return 0
        var t = Settings.brew.scaledSteamTime(Settings.brew.selectedSteamPitcher, milk)
        if (t <= 0)
            return 0
        Settings.brew.steamTimeout = t
        // Sent now so a group-head steam uses it, not the machine's last-sent timeout.
        MainController.applySteamSettings()
        if (Settings.brew.doseCaptureSoundEnabled)
            AccessibilityManager.playCaptureDing()
        return t
    }

    onStableCaptured: function(milk) {
        _log("captured")
        _attemptOpen = false
        root.milkCaptured(milk, root._apply(milk))
    }

    // A tare moves the scale's zero, so the virtual zero has to be re-established.
    Connections {
        target: MachineState
        function onTareCompleted() {
            if (root.active)
                root._log("tare reported, re-zeroing")
            root.reset()
        }
    }
    // The set-aside milk belonged to the old pitcher; main.qml clears the session milk.
    Connections {
        target: Settings.brew
        function onSelectedSteamPitcherChanged() { root._attemptOpen = false }
    }
}
