// DashedLineSeries — Qt Graphs bridge component.
//
// Qt Graphs `LineSeries` is solid-stroke only; Qt Charts supported
// `Qt.DashLine` / `Qt.DotLine` directly. This component draws a `ShapePath`
// overlay aligned to a parent `GraphsView`'s plot area, mapping data-space
// points (matching the attached `axisX` / `axisY`) into pixel space.
//
// Standard placement is as a SIBLING of `GraphsView` inside an outer wrapper
// Item, with `graphsView` set explicitly via the chart's `graphsViewRef`
// alias. Children of `GraphsView` are painted over by its own scene-graph
// pass and don't show up. Inside-the-GraphsView placement still works if no
// overlap with axis grids/labels is needed — `graphsView` then defaults to
// `parent`.
//
// Usage (sibling pattern, used by every Decenza chart in this codebase):
//   Item {
//       id: chart
//       readonly property alias graphsViewRef: graphsView
//       GraphsView { id: graphsView; axisX: timeAxis; axisY: valueAxis; ... }
//       DashedLineSeries {
//           graphsView: chart.graphsViewRef
//           axisX: timeAxis
//           axisY: valueAxis
//           points: shotModel.goalPoints   // [Qt.point(x, y), ...]
//           strokeColor: Theme.flowGoalColor
//           strokeWidth: Theme.graphLineWidth
//           dashPattern: [4, 4]
//       }
//   }
//
// Replaces: Qt Charts `LineSeries { style: Qt.DashLine }`.

import QtQuick
import QtQuick.Shapes
import QtGraphs
import Decenza

Item {
    id: root

    // The GraphsView whose plotArea this overlay aligns to.
    // Defaults to the parent (use when this item is a direct child of GraphsView).
    property var graphsView: parent

    // Axes used for data → pixel mapping. Read `min` / `max` for current visible range.
    property var axisX: null
    property var axisY: null

    // Data points: array of Qt.point(x, y) in axis units.
    property var points: []

    // Stroke style.
    property color strokeColor: Theme.textColor
    property real strokeWidth: Theme.graphLineWidth
    property bool dashed: true
    property var dashPattern: [4, 4]  // length pairs in stroke widths; ignored when dashed: false

    // The overlay fills the plot area of the parent GraphsView.
    readonly property var _plotArea: graphsView && graphsView.plotArea ? graphsView.plotArea : null
    visible: _plotArea !== null && points && points.length >= 2

    x: _plotArea ? _plotArea.x : 0
    y: _plotArea ? _plotArea.y : 0
    width: _plotArea ? _plotArea.width : 0
    height: _plotArea ? _plotArea.height : 0
    clip: true

    function _axisRange(axis, defaultMin, defaultMax) {
        if (!axis) return [defaultMin, defaultMax]
        // Prefer visualMin/visualMax (Qt 6.11+) which track zoom/pan; fall back to min/max.
        var lo = axis.visualMin !== undefined ? axis.visualMin : axis.min
        var hi = axis.visualMax !== undefined ? axis.visualMax : axis.max
        if (lo === undefined || lo === null) lo = defaultMin
        if (hi === undefined || hi === null) hi = defaultMax
        return [lo, hi]
    }

    // Read all dependent properties up-front so QML's dependency tracker
    // re-runs this binding when any of them change.
    readonly property var pixelPoints: {
        var pts = points
        var w = width
        var h = height
        var xRange = _axisRange(axisX,
            axisX ? axisX.min : 0,
            axisX ? axisX.max : 1)
        var yRange = _axisRange(axisY,
            axisY ? axisY.min : 0,
            axisY ? axisY.max : 1)

        if (!pts || pts.length === 0 || w <= 0 || h <= 0) return []
        var xSpan = xRange[1] - xRange[0]
        var ySpan = yRange[1] - yRange[0]
        if (xSpan === 0 || ySpan === 0) return []

        var result = []
        for (var i = 0; i < pts.length; ++i) {
            var p = pts[i]
            var px = ((p.x - xRange[0]) / xSpan) * w
            var py = h - ((p.y - yRange[0]) / ySpan) * h  // flip Y
            result.push(Qt.point(px, py))
        }
        return result
    }

    Shape {
        anchors.fill: parent
        antialiasing: true

        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            strokeStyle: root.dashed ? ShapePath.DashLine : ShapePath.SolidLine
            dashPattern: root.dashPattern
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            startX: root.pixelPoints.length > 0 ? root.pixelPoints[0].x : 0
            startY: root.pixelPoints.length > 0 ? root.pixelPoints[0].y : 0

            PathPolyline {
                path: root.pixelPoints.length > 0 ? root.pixelPoints : [Qt.point(0, 0)]
            }
        }
    }
}
