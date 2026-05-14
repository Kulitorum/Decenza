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

    function _recompute() {
        var lo = Number.POSITIVE_INFINITY
        var hi = Number.NEGATIVE_INFINITY
        var seen = false

        for (var i = 0; i < series.length; ++i) {
            var s = series[i]
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
        if (minFloor !== null && newMin < minFloor) newMin = minFloor
        if (maxCeiling !== null && newMax > maxCeiling) newMax = maxCeiling

        root.min = newMin
        root.max = newMax
    }

    onSeriesChanged: _recompute()
    Component.onCompleted: _recompute()

    // Re-fit whenever any tracked series emits pointsReplaced/pointAdded.
    Connections {
        target: null
    }

    // Wire signal connections per-series with a Repeater-like Instantiator.
    Instantiator {
        model: root.series
        delegate: QtObject {
            required property var modelData
            property var _conn: Connections {
                target: modelData
                ignoreUnknownSignals: true
                function onPointAdded() { root._recompute() }
                function onPointReplaced() { root._recompute() }
                function onPointRemoved() { root._recompute() }
                function onPointsReplaced() { root._recompute() }
            }
        }
    }
}
