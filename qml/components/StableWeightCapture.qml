import QtQuick

// Event-driven stable-weight detector for auto-capturing a scale reading.
//
// The parent binds `weight` to a net value (e.g. scaleWeight - tare). When that
// value holds within `tolerance` of a candidate for `stableMs`, this emits
// stableCaptured(grams) exactly once and latches. It re-arms automatically when
// the load is removed (drops below `removeThreshold`) or changes materially from
// the captured value (so topping up milk / re-dosing beans re-captures).
//
// Detection is driven by `weight` changes; a 150 ms poll Timer additionally
// re-runs the check while arming, so a scale that streams a perfectly constant
// (non-jittering) value still reaches `stableMs` and graduates. The poll stops
// once captured.
Item {
    id: root

    // --- Inputs (parent may override) ---
    property real weight: 0           // net weight to watch
    property bool active: true        // only detect while true
    property real minWeight: 20       // ignore readings below this (noise / empty)
    property real maxWeight: 100000   // ignore readings above this (e.g. wrong vessel)
    property real tolerance: 1.0      // ± grams treated as "the same reading"
    property int  stableMs: 2500      // must hold this long to count as stable
    property real removeThreshold: 5  // below this = load removed -> re-arm
    property real rearmDelta: 6        // change this much from captured value -> re-arm

    // --- Outputs ---
    readonly property bool isCaptured: _captured
    readonly property real capturedValue: _capturedValue

    signal stableCaptured(real grams)

    property bool   _captured: false
    property real   _capturedValue: 0
    property real   _candidate: NaN
    property double _candidateSince: 0

    function reset() {
        _captured = false
        _capturedValue = 0
        _candidate = NaN
    }

    onActiveChanged: if (!active) reset()
    onWeightChanged: _evaluate()

    function _evaluate() {
        if (!active)
            return

        if (_captured) {
            // Re-arm only if the load was removed or materially changed.
            if (weight < removeThreshold || Math.abs(weight - _capturedValue) > rearmDelta)
                reset()
            else
                return
        }

        if (weight < minWeight || weight > maxWeight) {
            _candidate = NaN
            return
        }

        var now = Date.now()
        if (isNaN(_candidate) || Math.abs(weight - _candidate) > tolerance) {
            _candidate = weight
            _candidateSince = now
            return
        }
        if (now - _candidateSince >= stableMs) {
            _captured = true
            _capturedValue = weight
            stableCaptured(weight)
        }
    }

    // Periodic re-check so a constant (non-jittering) stream still graduates.
    Timer {
        interval: 150
        repeat: true
        running: root.active && !root._captured
        onTriggered: root._evaluate()
    }
}
