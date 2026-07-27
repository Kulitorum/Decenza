// AutoRangingAxis — Qt Graphs bridge component.
//
// Replaces Qt Charts' implicit auto-range behavior on `ValueAxis`. Qt Graphs
// requires `min`/`max` to be set explicitly; this component derives them from
// one or more attached XYSeries and re-fits when their point sets change.
//
// Usage:
//   AutoRangingAxis {
//       id: yAxis
//       series: [flowSeries, weightFlowSeries]
//       padding: 0.05      // 5% headroom above/below data
//       minFloor: 0        // never drop below 0
//       maxCeiling: null   // no upper clamp
//   }
//
// Replaces: Qt Charts `ValueAxis` used without explicit min/max.

import QtQuick
import QtGraphs

ValueAxis {
    id: root

    // One or more XYSeries (LineSeries, ScatterSeries, etc.) to track.
    property var series: []

    // Fractional padding above/below the data range (0.05 = 5%).
    property real padding: 0.05

    // Optional hard clamps; null disables.
    property var minFloor: null
    property var maxCeiling: null

    // Fallback range when no series have data yet.
    property real fallbackMin: 0
    property real fallbackMax: 1

    // When true and `tickInterval > 0`, snap the computed min/max to multiples
    // of `tickInterval` so the topmost/bottommost tick lands exactly at the
    // plot edge instead of leaving dead space past the last tick.
    property bool snapToTickInterval: true

    function _recompute() {
        var lo = Number.POSITIVE_INFINITY
        var hi = Number.NEGATIVE_INFINITY
        var seen = false

        var list = series
        if (!list) return
        for (var i = 0; i < list.length; ++i) {
            var s = list[i]
            if (!s || !s.count) continue
            for (var j = 0; j < s.count; ++j) {
                var p = s.at(j)
                if (!isFinite(p.y)) continue
                if (p.y < lo) lo = p.y
                if (p.y > hi) hi = p.y
                seen = true
            }
        }

        if (!seen) {
            root.min = fallbackMin
            root.max = fallbackMax
            return
        }

        var span = hi - lo
        if (span <= 0) span = Math.max(1, Math.abs(hi)) * 0.1
        var pad = span * padding

        var newMin = lo - pad
        var newMax = hi + pad

        // Snap to tick-interval boundaries first so a subsequent clamp to
        // minFloor / maxCeiling still wins.
        if (snapToTickInterval && tickInterval > 0) {
            newMin = Math.floor(newMin / tickInterval) * tickInterval
            newMax = Math.ceil(newMax / tickInterval) * tickInterval
        }

        if (minFloor !== null && newMin < minFloor) newMin = minFloor
        if (maxCeiling !== null && newMax > maxCeiling) newMax = maxCeiling

        root.min = newMin
        root.max = newMax
    }

    // Tracked series we've connected signal handlers on; managed by _rebind().
    property var _bound: []

    function _bindOne(s) {
        if (!s) return
        // ignoreUnknownSignals via try/catch — different series subclasses expose different signals.
        try { s.pointAdded.connect(_recompute) } catch (e) {}
        try { s.pointReplaced.connect(_recompute) } catch (e) {}
        try { s.pointRemoved.connect(_recompute) } catch (e) {}
        try { s.pointsReplaced.connect(_recompute) } catch (e) {}
    }

    function _unbindOne(s) {
        if (!s) return
        try { s.pointAdded.disconnect(_recompute) } catch (e) {}
        try { s.pointReplaced.disconnect(_recompute) } catch (e) {}
        try { s.pointRemoved.disconnect(_recompute) } catch (e) {}
        try { s.pointsReplaced.disconnect(_recompute) } catch (e) {}
    }

    function _rebind() {
        for (var i = 0; i < _bound.length; ++i) _unbindOne(_bound[i])
        _bound = []
        if (!series) return
        for (var k = 0; k < series.length; ++k) {
            _bindOne(series[k])
            _bound.push(series[k])
        }
        _recompute()
    }

    onSeriesChanged: _rebind()
    Component.onCompleted: _rebind()
}
