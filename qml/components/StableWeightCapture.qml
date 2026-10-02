import QtQuick

// Delta-based stable-weight capture with an auto-tracked "virtual zero".
//
// Instead of trusting the scale's absolute zero, it watches the *empty* scale,
// adopts its settled reading as a virtual zero (any value, including negative),
// and then measures a placed load relative to that. The net dose is
// (load - virtualZero) - cupWeight, so it is robust to an un-zeroed or drifting
// scale at dose time. Capture only emits once a cup weight is saved
// (cupWeight > 0); the virtual zero keeps tracking even before then so a "weigh
// the cup" action can subtract the same baseline.
//
// Detection is driven by `rawWeight` changes. A constant (non-jittering) reading emits no
// change, so while a candidate is still short of its dwell a single-shot timer re-checks
// when the dwell would end; nothing runs in a steady state.
Item {
    id: root

    // --- Inputs (parent may override) ---
    property real rawWeight: 0      // absolute scale reading (NOT pre-tared; may be < 0)
    property bool active: true      // only detect while true
    property real cupWeight: 0      // saved empty-cup weight, subtracted from the load
    property real minNet: 5         // ignore net doses below this (noise)
    property real maxNet: 45        // ignore net doses above this (wrong vessel)
    property real tolerance: 0.5    // ± grams treated as "the same reading"
    property int  stableMs: 2500    // a loaded weight must hold this long to capture a dose
    property int  baselineMs: 1000  // an empty scale must hold this long to (re)adopt the zero
    property real loadThreshold: 3  // rise above the virtual zero that means "load placed"
    property real rearmDelta: 6     // net change after capture that re-arms in place

    // --- Outputs ---
    readonly property real virtualZero: _virtualZero
    readonly property bool loadPresent: _seeded && (rawWeight - _virtualZero) >= loadThreshold
    readonly property real netWeight: loadPresent ? (rawWeight - _virtualZero - cupWeight) : 0
    readonly property bool isCaptured: _captured

    signal stableCaptured(real grams)

    property real    _virtualZero: 0
    property bool    _seeded: false
    property bool    _captured: false
    property real    _capturedNet: 0
    property real    _cand: NaN
    property double  _candSince: 0

    function reset() {
        // _virtualZero intentionally kept: _seeded=false forces re-adoption from the
        // next stable empty reading (gated by _seeded), so the old value is never used.
        _seeded = false
        _captured = false
        _capturedNet = 0
        _cand = NaN
        settleTimer.stop()
    }

    onActiveChanged: if (!active) reset()
    onRawWeightChanged: _evaluate()

    // True once rawWeight has held within tolerance for `dwell` ms; otherwise it
    // (re)seeds the candidate run and returns false.
    function _settled(now, dwell) {
        if (isNaN(_cand) || Math.abs(rawWeight - _cand) > tolerance) {
            _cand = rawWeight
            _candSince = now
        }
        var remaining = dwell - (now - _candSince)
        if (remaining <= 0)
            return true
        settleTimer.interval = remaining
        settleTimer.restart()
        return false
    }

    function _evaluate() {
        if (!active)
            return

        var now = Date.now()

        if (!_seeded) {
            // Establish the first virtual zero only from a STABLE reading, so a
            // transient (jittering) reading can't become the zero. A load left on the
            // scale at startup can still be adopted as the baseline; taring the scale
            // (which calls reset()) re-establishes it from the true empty reading.
            if (_settled(now, baselineMs)) {
                _virtualZero = rawWeight
                _seeded = true
            }
            return
        }

        var net = rawWeight - _virtualZero - cupWeight

        if (_captured) {                                   // re-arm: removed OR materially changed
            if ((rawWeight - _virtualZero) < loadThreshold
                || Math.abs(net - _capturedNet) > rearmDelta) {
                _captured = false
                _cand = NaN
            }
            return
        }

        if ((rawWeight - _virtualZero) < loadThreshold) {  // empty: re-adopt the zero (baselineMs)
            if (_settled(now, baselineMs))                 // so an un-zeroed/drifting baseline stays good
                _virtualZero = rawWeight
            return
        }

        if (_settled(now, stableMs)) {                     // load held long enough: capture net
            if (cupWeight > 0 && net >= minNet && net <= maxNet) {
                _captured = true
                _capturedNet = net
                stableCaptured(net)
            }
        }
    }

    // Re-checks when the current candidate's dwell ends; a reading that does not change
    // never re-evaluates otherwise. Armed only by _settled().
    Timer {
        id: settleTimer
        repeat: false
        onTriggered: root._evaluate()
    }
}
