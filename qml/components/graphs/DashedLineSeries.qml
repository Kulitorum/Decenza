// DashedLineSeries — Qt Graphs bridge component.
//
// Qt Graphs `LineSeries` is solid-stroke only; Qt Charts supported
// `Qt.DashLine` / `Qt.DotLine` directly. This component draws a `ShapePath`
// overlay aligned to a parent `GraphsView`'s plot area, mapping data-space
// points (matching the attached `axisX` / `axisY`) into pixel space.
//
// Place this *inside* a `GraphsView` (so `plotArea` is reachable as
// `plotArea` on the parent) or pass an explicit `graphsView` reference.
//
// Usage:
//   DashedLineSeries {
//       graphsView: chart           // optional; defaults to parent
//       axisX: timeAxis
//       axisY: valueAxis
//       points: FlowCalibrationModel.goalCurve   // [Qt.point(x, y), ...]
//       strokeColor: Theme.flowGoalColor
//       strokeWidth: Theme.graphLineWidth
//       dashPattern: [4, 4]
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
    property var dashPattern: [4, 4]  // length pairs in stroke widths

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

    function _mapToPixels() {
        var result = []
        if (!points || points.length === 0) return result
        var xr = _axisRange(axisX, 0, 1)
        var yr = _axisRange(axisY, 0, 1)
        var xSpan = xr[1] - xr[0]
        var ySpan = yr[1] - yr[0]
        if (xSpan === 0 || ySpan === 0) return result
        for (var i = 0; i < points.length; ++i) {
            var p = points[i]
            var px = ((p.x - xr[0]) / xSpan) * width
            var py = height - ((p.y - yr[0]) / ySpan) * height  // flip Y
            result.push(Qt.point(px, py))
        }
        return result
    }

    Shape {
        id: shape
        anchors.fill: parent
        antialiasing: true

        property var pixelPoints: root._mapToPixels()

        // Recompute when inputs change.
        property var _trigger: [root.points, root.width, root.height,
                                 root.axisX ? root.axisX.min : null,
                                 root.axisX ? root.axisX.max : null,
                                 root.axisY ? root.axisY.min : null,
                                 root.axisY ? root.axisY.max : null]
        on_TriggerChanged: pixelPoints = root._mapToPixels()

        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            strokeStyle: ShapePath.DashLine
            dashPattern: root.dashPattern
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            startX: shape.pixelPoints.length > 0 ? shape.pixelPoints[0].x : 0
            startY: shape.pixelPoints.length > 0 ? shape.pixelPoints[0].y : 0

            PathPolyline {
                path: shape.pixelPoints.length > 0 ? shape.pixelPoints : [Qt.point(0, 0)]
            }
        }
    }
}
