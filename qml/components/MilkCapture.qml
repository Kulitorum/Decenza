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

    // The load on the scale settled with less than `minNet` of milk, and has not moved
    // since. Lets a page say "add milk" instead of looking stuck.
    readonly property bool belowMinimum: !isNaN(_belowMinRaw) && loadPresent
                                         && Math.abs(rawWeight - _belowMinRaw) <= tolerance
    property real _belowMinRaw: NaN

    // A fresh steam attempt: drop milk captured by an abandoned one. Deliberately no
    // tare — the capture measures from the empty reading it sees settle, and the HDS
    // over WiFi (fw 3.1.14) sends garbage frames of roughly ±1,000 g around a tare.
    function startAttempt() {
        _log("attempt started")
        AppShell.sessionMeasuredMilkG = 0
    }

    // DEBUG: the zero, load and tare sequence is timing-dependent on a WiFi scale and
    // only reconstructable from a log.
    function _log(event) {
        WebDebugLogger.debug("Steam", "MilkCapture", [event, "raw=" + rawWeight.toFixed(1),
            "zero=" + virtualZero.toFixed(1), "pitcher=" + cupWeight.toFixed(1),
            "net=" + (rawWeight - virtualZero - cupWeight).toFixed(1), "active=" + active].join(" "))
    }
    property real _lastRejected: NaN
    property real _lastLoggedZero: NaN
    // The empty branch re-adopts on every settled wobble; log only a real move.
    onVirtualZeroChanged: {
        if (!active || (!isNaN(_lastLoggedZero) && Math.abs(virtualZero - _lastLoggedZero) < tolerance))
            return
        _lastLoggedZero = virtualZero
        _log("zero adopted")
    }
    onLoadPresentChanged: if (active) _log(loadPresent ? "load placed" : "load removed")
    onActiveChanged: {
        _lastRejected = NaN
        _lastLoggedZero = NaN
        _belowMinRaw = NaN
        _log(active ? "armed" : "disarmed")
    }
    onStableRejected: function(net) {
        _belowMinRaw = net < minNet ? rawWeight : NaN
        if (!isNaN(_lastRejected) && Math.abs(net - _lastRejected) < 1) return  // once per settled load
        _lastRejected = net
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
        root.milkCaptured(milk, root._apply(milk))
    }

    // A tare moves the scale's zero, so the virtual zero has to be re-established.
    Connections {
        target: MachineState
        function onTareCompleted() {
            root._log("tare reported, re-zeroing")
            root.reset()
        }
    }
}
