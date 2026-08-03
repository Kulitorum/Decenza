// The trace, phase-marker, pump-mode and tick-label Repeater delegates read this file's
// ids (`chart`, `graphsView`, `timeAxis`, `pressureAxis`, `weightAxis`, `tempAxis`,
// `rightAxisLabels`); Bound makes them statically resolvable. Every one of them already
// declares each injected role it uses required, so Bound cannot break role injection
// here.
pragma ComponentBehavior: Bound

import QtQuick
import QtGraphs
import Decenza
import "GraphUtils.js" as GraphUtils

// Outer Item wraps the GraphsView so dashed overlays, right-axis-mapped traces,
// inspect crosshair, marker labels, and the right-axis label column render as
// siblings on top of the chart. GraphsView's scene-graph paints over any direct
// QQuickItem children — overlays must be siblings, not children.
Item {
    id: chart

    // Alias so DashedLineSeries delegates can reach the GraphsView without
    // writing `graphsView: graphsView` — that RHS shadows the delegate's own
    // `graphsView` property (which defaults to `parent`) and resolves to null.
    readonly property alias graphsViewRef: graphsView

    // Re-export the GraphsView's plot rect so parent pages can hit-test against
    // it (e.g. the tap-to-inspect overlays distinguishing crosshair taps from
    // right-axis toggles). Matches the legacy Qt Charts ChartView.plotArea API.
    readonly property rect plotArea: graphsView.plotArea

    // Controls for compact/widget rendering
    property bool showLabels: true
    property bool showPhaseLabels: true

    // Series visibility binds straight to Settings.graph, so a toggle anywhere in the app
    // reaches every instance of this graph.
    //
    // What used to be here: eleven mirror properties initialised from Settings.boolValue()
    // — a Q_INVOKABLE, so the initialiser recorded no dependency and never re-evaluated —
    // plus a twelve-case switch over Settings.valueChanged to write them back by hand.
    // GraphLegend also assigned graph[key] directly on the one instance it was attached to,
    // which is why the review page appeared to work while the last-shot background chart
    // silently froze at its startup values after the first toggle.

    property bool advancedMode: Settings.graph.advancedMode

    // Right axis: "weight", "temperature" or "flow".
    property string rightAxisMode: Settings.graph.rightAxisMode

    function toggleRightAxis() { Settings.graph.cycleRightAxisMode() }

    // Factor applied to the flow-family traces before plotting (1, 2 or 3).
    property int flowMultiplier: Settings.graph.flowMultiplier

    // Inspect state (crosshair + tooltip)
    property bool inspecting: false
    property real inspectTime: 0
    property real inspectPixelX: 0
    property var inspectValues: ({})

    // Data to display (set from parent)
    property var pressureData: []
    property var flowData: []
    property var temperatureData: []
    property var weightData: []
    property var weightFlowRateData: []
    property var resistanceData: []
    property var conductanceData: []
    property var darcyResistanceData: []
    property var conductanceDerivativeData: []
    property var temperatureMixData: []
    property var pressureGoalData: []
    property var flowGoalData: []
    property var temperatureGoalData: []
    property var temperatureMixGoalData: []
    property var phaseMarkers: []
    property double maxTime: 60

    // === Reload pipeline ===
    // Qt.callLater coalesces sequential property reassignments from the parent
    // (pressureData, flowData, weightData…) so loadData() runs once with a
    // consistent snapshot rather than N times with mixed old/new arrays.
    function doReload() {
        dismissInspect()
        loadMainSeries()
        updateTimeAxis()
        // Goal-segment arrays + right-axis-mapped traces bind directly to QML
        // properties — no imperative load step needed.
    }

    // Load only the main-axis Qt Graphs LineSeries (pressure, flow, weight-flow,
    // resistance, conductance, darcy). Right-axis-mapped traces (temperature,
    // mix temp, weight, dC/dt) are rendered as DashedLineSeries overlays whose
    // `points` property binds straight to the data arrays.
    function loadMainSeries() {
        pressureSeries.clear()
        flowSeries.clear()
        weightFlowRateSeries.clear()
        resistanceSeries.clear()
        conductanceSeries.clear()
        darcyResistanceSeries.clear()

        for (var i = 0; i < pressureData.length; i++)
            pressureSeries.append(pressureData[i].x, pressureData[i].y)
        // Flow family carries the multiplier. These are native Qt Graphs LineSeries bound to
        // the view's shared axis, so unlike the live graph's renderers there is no per-series
        // range to shrink — the factor is applied as the points are appended. The source
        // arrays are NOT touched: pressureAxisMax and every readout still read true values
        // from them, and doReload() re-appends from scratch whenever the factor changes.
        var flowScale = chart.flowMultiplier
        for (i = 0; i < flowData.length; i++)
            flowSeries.append(flowData[i].x, flowData[i].y * flowScale)
        for (i = 0; i < weightFlowRateData.length; i++)
            weightFlowRateSeries.append(weightFlowRateData[i].x, weightFlowRateData[i].y * flowScale)
        for (i = 0; i < resistanceData.length; i++)
            resistanceSeries.append(resistanceData[i].x, resistanceData[i].y)
        for (i = 0; i < conductanceData.length; i++)
            conductanceSeries.append(conductanceData[i].x, conductanceData[i].y)
        for (i = 0; i < darcyResistanceData.length; i++)
            darcyResistanceSeries.append(darcyResistanceData[i].x, darcyResistanceData[i].y)
    }

    function updateTimeAxis() {
        // Clip to the later of maxTime (extraction duration) or the last phase-
        // marker time so any frame-transition marker landing just past duration
        // still renders. Pixel-based padding keeps markers off the right edge.
        // No early return on empty pressureData — temperature- or weight-only
        // rows (corrupt/partial) still get a sensible axis from maxTime alone.
        var markerMaxTime = 0
        for (var m = 0; m < phaseMarkers.length; m++) {
            if (phaseMarkers[m].time > markerMaxTime) markerMaxTime = phaseMarkers[m].time
        }
        var axisEnd = Math.max(maxTime, markerMaxTime)
        var plotWidth = Math.max(1, graphsView.plotArea.width)
        var paddingPx = Theme.scaled(5)
        var scale = plotWidth / Math.max(1, plotWidth - paddingPx)
        timeAxis.max = Math.max(5, axisEnd * scale)
    }

    // Split a goal data array into segments at time gaps (pump mode transitions).
    // During recording, goal=0 samples are skipped, creating natural time gaps
    // between segments. Normal sample interval is ~0.2 s; gaps > 0.5 s indicate
    // a mode switch boundary.
    function segmentGoalData(data, maxSegments) {
        if (!data || data.length === 0) return []
        var segments = [[data[0]]]
        for (var i = 1; i < data.length; i++) {
            if (data[i].x - data[i - 1].x > 0.5 && segments.length < maxSegments) {
                segments.push([data[i]])
            } else {
                segments[segments.length - 1].push(data[i])
            }
        }
        return segments
    }

    // Computed goal segments — bound by the dashed-overlay Repeaters below.
    readonly property var pressureGoalSegments: segmentGoalData(pressureGoalData, 5)
    readonly property var flowGoalSegments: segmentGoalData(flowGoalData, 5)

    // === Inspect: pixel → data, value lookup, accessibility ===

    function _timeAtPixel(pixelX) {
        return GraphUtils.timeAtPixel(pixelX, graphsView.plotArea, timeAxis.min, timeAxis.max)
    }

    // Find the y value in a data array closest to the given time.
    // Reads from data arrays (not series) so right-axis-mapped traces — which are
    // no longer Qt Graphs series — are queryable the same way as main-axis traces.
    function findValueAtTime(data, time) {
        if (!data || data.length === 0) return null
        var closest = data[0]
        var minDist = Math.abs(closest.x - time)
        for (var i = 1; i < data.length; i++) {
            var p = data[i]
            var dist = Math.abs(p.x - time)
            if (dist < minDist) {
                closest = p
                minDist = dist
            } else if (dist > minDist) {
                break  // sorted by x
            }
        }
        return minDist < 1.0 ? closest.y : null
    }

    function inspectAtPosition(pixelX, pixelY) {
        var time = _timeAtPixel(pixelX)
        if (time < 0 || time > timeAxis.max) return

        inspectTime = time
        inspectPixelX = graphsView.plotArea.x + (time / timeAxis.max) * graphsView.plotArea.width

        // Compute values for every curve — regardless of visibility — so the
        // inspect bar can react live when the user toggles curves on/off without
        // re-tapping the graph. GraphInspectBar filters by the current show*
        // flags at display time.
        //
        // These read the SOURCE arrays, never the plotted series, so the flow multiplier
        // cannot leak into a readout: a 2.4 mL/s sample reports 2.4 at every setting. Do
        // not "simplify" this to read back from flowSeries. Decaid ships that exact bug —
        // its weight trace is plotted at ÷10 and the tooltip prints the plotted number, so
        // a 36 g shot reads "Weight: 3.6 g" (lib/src/util/shot_chart.dart:105, :359).
        var vals = {}
        var curves = [
            { key: "pressure", name: "Pressure", data: pressureData, unit: "bar" },
            { key: "flow", name: "Flow", data: flowData, unit: "mL/s" },
            { key: "temperature", name: "Temp", data: temperatureData, unit: Theme.tempUnitSuffix(), convert: function(c){ return Theme.cToDisplay(c) } },
            { key: "mixTemp", name: "Mix temp", data: temperatureMixData, unit: Theme.tempUnitSuffix(), convert: function(c){ return Theme.cToDisplay(c) } },
            { key: "mixTempGoal", name: "Mix temp goal", data: temperatureMixGoalData, unit: Theme.tempUnitSuffix(), convert: function(c){ return Theme.cToDisplay(c) } },
            { key: "weight", name: "Weight", data: weightData, unit: "g" },
            { key: "weightFlow", name: "Weight flow", data: weightFlowRateData, unit: "g/s" },
            { key: "resistance", name: "Resistance", data: resistanceData, unit: "" },
            { key: "darcyResistance", name: "Darcy R", data: darcyResistanceData, unit: "" },
            { key: "conductance", name: "Conductance", data: conductanceData, unit: "" },
            { key: "dCdt", name: "dC/dt", data: conductanceDerivativeData, unit: "" }
        ]

        for (var i = 0; i < curves.length; i++) {
            var v = findValueAtTime(curves[i].data, time)
            if (v !== null) {
                vals[curves[i].key] = { name: curves[i].name, value: curves[i].convert ? curves[i].convert(v) : v, unit: curves[i].unit }
            }
        }

        inspectValues = vals
        inspecting = true
        announceAtPosition(pixelX, pixelY)
    }

    function dismissInspect() {
        inspecting = false
    }

    // Return the phase label active at the given time, or empty string if none.
    // Skips "Start"/"End" sentinels — they are structural, not user-facing.
    function getPhaseAtTime(time) {
        var label = ""
        for (var i = 0; i < phaseMarkers.length; i++) {
            var m = phaseMarkers[i]
            if (m.time <= time) {
                if (m.label !== "Start" && m.label !== "End")
                    label = m.label
            } else {
                break
            }
        }
        return label
    }

    function announceAtPosition(pixelX, pixelY) {
        var time = _timeAtPixel(pixelX)
        if (time < 0 || time > timeAxis.max) return

        var curves = [
            { name: "Pressure", data: pressureData, show: Settings.graph.showPressure, unit: "bar" },
            { name: "Flow", data: flowData, show: Settings.graph.showFlow, unit: "mL/s" },
            { name: "Temp", data: temperatureData, show: Settings.graph.showTemperature, unit: Theme.tempUnitSuffix(), convert: function(c){ return Theme.cToDisplay(c) } },
            { name: "Mix temp", data: temperatureMixData, show: Settings.graph.showTemperatureMix && chart.advancedMode, unit: Theme.tempUnitSuffix(), convert: function(c){ return Theme.cToDisplay(c) } },
            { name: "Mix temp goal", data: temperatureMixGoalData, show: Settings.graph.showTemperatureMixGoal && chart.advancedMode, unit: Theme.tempUnitSuffix(), convert: function(c){ return Theme.cToDisplay(c) } },
            { name: "Weight", data: weightData, show: Settings.graph.showWeight, unit: "g" },
            { name: "Weight flow", data: weightFlowRateData, show: Settings.graph.showWeightFlow, unit: "g/s" },
            { name: "Resistance", data: resistanceData, show: Settings.graph.showResistance && chart.advancedMode, unit: "" },
            { name: "Darcy R", data: darcyResistanceData, show: Settings.graph.showDarcyResistance && chart.advancedMode, unit: "" },
            { name: "Conductance", data: conductanceData, show: Settings.graph.showConductance && chart.advancedMode, unit: "" },
            { name: "dC/dt", data: conductanceDerivativeData, show: Settings.graph.showConductanceDerivative && chart.advancedMode, unit: "" }
        ]

        var parts = []
        for (var i = 0; i < curves.length; i++) {
            if (!curves[i].show) continue
            var v = findValueAtTime(curves[i].data, time)
            if (v !== null) {
                var dv = curves[i].convert ? curves[i].convert(v) : v
                var entry = curves[i].name + " " + dv.toFixed(1)
                if (curves[i].unit !== "") entry += " " + curves[i].unit
                parts.push(entry)
            }
        }

        if (parts.length === 0) return
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null) {
            var phase = getPhaseAtTime(time)
            var header = "At " + time.toFixed(1) + " seconds"
            if (phase !== "") header += ", " + phase + " phase"
            AccessibilityManager.announce(header + ". " + parts.join(". "), true)
        }
    }

    onPressureDataChanged: Qt.callLater(doReload)
    onFlowDataChanged: Qt.callLater(doReload)
    onWeightFlowRateDataChanged: Qt.callLater(doReload)
    onResistanceDataChanged: Qt.callLater(doReload)
    onConductanceDataChanged: Qt.callLater(doReload)
    onDarcyResistanceDataChanged: Qt.callLater(doReload)
    onPhaseMarkersChanged: Qt.callLater(doReload)
    // The multiplier is baked into the appended points, so a change has to re-append them.
    // The live graph needs no equivalent — its renderers are re-mapped by binding alone.
    onFlowMultiplierChanged: Qt.callLater(doReload)
    Component.onCompleted: doReload()

    // Dynamic max for pressure/flow axis based on all data (ignores visibility
    // toggles so the graph doesn't jump when toggling curves on/off).
    //
    // These walks MUST stay on the unscaled source arrays. The flow multiplier is applied
    // by shrinking the range each flow-family trace is mapped through, never by rewriting
    // points — precisely so this stays true. Feed multiplied values in here and the axis
    // grows by the multiplier, the traces land in identical pixels, and the zoom silently
    // does nothing while every test still passes. tst_historyshotgraph pins the invariant:
    // this value is identical at 1x, 2x and 3x for the same shot.
    property double pressureAxisMax: {
        var maxVal = 0
        for (var i = 0; i < pressureData.length; i++) {
            if (pressureData[i].y > maxVal) maxVal = pressureData[i].y
        }
        for (var i = 0; i < flowData.length; i++) {
            if (flowData[i].y > maxVal) maxVal = flowData[i].y
        }
        for (var i = 0; i < weightFlowRateData.length; i++) {
            if (weightFlowRateData[i].y > maxVal) maxVal = weightFlowRateData[i].y
        }
        for (i = 0; i < pressureGoalData.length; i++) {
            if (pressureGoalData[i].y > maxVal) maxVal = pressureGoalData[i].y
        }
        for (i = 0; i < flowGoalData.length; i++) {
            if (flowGoalData[i].y > maxVal) maxVal = flowGoalData[i].y
        }
        // Resistance excluded — values are clamped at source and clip at the
        // axis boundary, matching the live graph behaviour.
        if (maxVal < 0.1) return 12  // fallback when no data
        var padded = maxVal * 1.15
        if (padded <= 2) return 2
        if (padded <= 4) return 4
        if (padded <= 5) return 5
        if (padded <= 6) return 6
        if (padded <= 8) return 8
        if (padded <= 10) return 10
        if (padded <= 12) return 12
        return Math.ceil(padded / 5) * 5
    }

    // === HIDDEN RIGHT-AXIS HOLDERS ===
    // Plain QtObjects (not Qt Graphs ValueAxis) — Qt Graphs has no sanctioned
    // dual-Y-axis path here. DashedLineSeries reads `min`/`max` for its data→
    // pixel mapping, so a value-holder QObject is enough.

    // Flow-family mapping for the right-axis label column. The traces themselves carry the
    // factor in their appended points (see loadMainSeries), so this object exists to give
    // the labels the same numbers the plot is drawn against.
    QtObject {
        id: flowAxis
        property real min: 0
        property real max: chart.pressureAxisMax / chart.flowMultiplier
    }

    QtObject {
        id: tempAxis
        property real min: 40
        property real max: 100
    }

    QtObject {
        id: weightAxis
        property real min: 0
        property real max: {
            var maxW = 0
            for (var i = 0; i < chart.weightData.length; i++) {
                if (chart.weightData[i].y > maxW) maxW = chart.weightData[i].y
            }
            return Math.max(10, maxW * 1.1)
        }
    }

    QtObject {
        id: dCdtAxis
        property real min: {
            var minV = 0
            for (var i = 0; i < chart.conductanceDerivativeData.length; i++) {
                if (chart.conductanceDerivativeData[i].y < minV) minV = chart.conductanceDerivativeData[i].y
            }
            return minV < 0 ? -Math.abs(minV) * 1.15 : 0
        }
        property real max: {
            var maxV = 0
            for (var i = 0; i < chart.conductanceDerivativeData.length; i++) {
                if (chart.conductanceDerivativeData[i].y > maxV) maxV = chart.conductanceDerivativeData[i].y
            }
            var padded = maxV * 1.15
            if (padded <= 2) return 2
            if (padded <= 3) return 3
            if (padded <= 5) return 5
            if (padded <= 8) return 8
            if (padded <= 10) return 10
            return Math.ceil(padded / 5) * 5
        }
    }

    GraphsView {
        id: graphsView
        anchors.fill: parent
        anchors.rightMargin: chart.showLabels ? Theme.scaled(35) : 0
        theme: DecenzaGraphsTheme {}

        axisX: timeAxis
        axisY: pressureAxis

        onPlotAreaChanged: chart.updateTimeAxis()

        ValueAxis {
            id: timeAxis
            min: 0
            max: 60
            tickInterval: 10
            subTickCount: 0
            labelFormat: "%.0f"
            visible: chart.showLabels
            // Caption goes on the axis, not in an overlay: Qt Graphs draws axis
            // titles itself AND reserves layout space for them (axisrenderer.cpp:622
            // counts titled axes into the margin math). The Qt Charts -> Qt Graphs
            // migration (#1146) carried this over as a Text positioned off `plotArea`
            // bottom-right, which floated it ON TOP of the plot, over any trace running
            // along the bottom.
            titleText: TranslationManager.translate("graph.timeAxis", "Time (s)")
        }

        ValueAxis {
            id: pressureAxis
            min: 0
            max: chart.pressureAxisMax
            tickInterval: Math.max(1, Math.round(chart.pressureAxisMax / 4))
            subTickCount: 0
            labelFormat: "%.0f"
            visible: chart.showLabels
            // Pressure-only once a multiplier is active: the flow family no longer reads
            // against this axis. It is also the only sign of an active multiplier that
            // survives into a screenshot.
            titleText: !chart.showLabels ? ""
                                         : (chart.flowMultiplier === 1 ? "bar / mL/s" : "bar")
        }

        // === MAIN-AXIS DATA LINES (Qt Graphs native series) ===

        LineSeries {
            id: pressureSeries
            color: Theme.pressureColor
            width: Theme.scaled(3)
            visible: Settings.graph.showPressure
        }

        LineSeries {
            id: flowSeries
            color: Theme.flowColor
            width: Theme.scaled(3)
            visible: Settings.graph.showFlow
        }

        LineSeries {
            id: weightFlowRateSeries
            color: Theme.weightFlowColor
            width: Theme.scaled(2)
            visible: Settings.graph.showWeightFlow
        }

        LineSeries {
            id: resistanceSeries
            color: Theme.resistanceColor
            width: Theme.scaled(2)
            visible: Settings.graph.showResistance && chart.advancedMode
        }

        LineSeries {
            id: conductanceSeries
            color: Theme.conductanceColor
            width: Theme.scaled(2)
            visible: Settings.graph.showConductance && chart.advancedMode
        }

        LineSeries {
            id: darcyResistanceSeries
            color: Theme.darcyResistanceColor
            width: Theme.scaled(2)
            visible: Settings.graph.showDarcyResistance && chart.advancedMode
        }
    }

    // === RIGHT-AXIS DATA LINES (DashedLineSeries with solid stroke) ===
    // Qt Graphs has no axisYRight for these in our setup, so they render as
    // bridge overlays mapped against their own min/max value holders.

    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: tempAxis
        points: chart.temperatureData
        strokeColor: Theme.temperatureColor
        strokeWidth: Theme.scaled(3)
        dashed: false
        visible: Settings.graph.showTemperature
    }

    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: tempAxis
        points: chart.temperatureMixData
        strokeColor: Theme.temperatureMixColor
        strokeWidth: Theme.scaled(2)
        dashed: false
        visible: Settings.graph.showTemperatureMix && chart.advancedMode
    }

    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: weightAxis
        points: chart.weightData
        strokeColor: Theme.weightColor
        strokeWidth: Theme.scaled(3)
        dashed: false
        visible: Settings.graph.showWeight
    }

    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: dCdtAxis
        points: chart.conductanceDerivativeData
        strokeColor: Theme.conductanceDerivativeColor
        strokeWidth: Theme.scaled(2)
        dashed: false
        visible: Settings.graph.showConductanceDerivative && chart.advancedMode
    }

    // === DASHED GOAL CURVES ===

    Repeater {
        model: chart.pressureGoalSegments
        delegate: DashedLineSeries {
            required property var modelData
            graphsView: chart.graphsViewRef
            axisX: timeAxis
            axisY: pressureAxis
            points: modelData
            strokeColor: Theme.pressureGoalColor
            strokeWidth: Theme.scaled(2)
            visible: Settings.graph.showPressure
        }
    }

    Repeater {
        model: chart.flowGoalSegments
        delegate: DashedLineSeries {
            required property var modelData
            graphsView: chart.graphsViewRef
            axisX: timeAxis
            // Mapped through flowAxis so the dashed target moves with the trace chasing it.
            // DashedLineSeries only reads min/max, so a value holder is enough.
            axisY: flowAxis
            points: modelData
            strokeColor: Theme.flowGoalColor
            strokeWidth: Theme.scaled(2)
            visible: Settings.graph.showFlow
        }
    }

    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: tempAxis
        points: chart.temperatureGoalData
        strokeColor: Theme.temperatureGoalColor
        strokeWidth: Theme.scaled(2)
        visible: Settings.graph.showTemperature
    }

    // Mix temperature goal (SetMixTemp) — advanced. Shots recorded before this
    // series existed carry an empty array, so the line is hidden, not drawn at zero.
    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: tempAxis
        points: chart.temperatureMixGoalData
        strokeColor: Theme.temperatureMixGoalColor
        strokeWidth: Theme.scaled(2)
        visible: Settings.graph.showTemperatureMixGoal && chart.advancedMode && chart.temperatureMixGoalData.length > 0
    }

    // === VERTICAL PHASE / FRAME MARKER LINES ===

    Repeater {
        model: chart.phaseMarkers
        delegate: DashedLineSeries {
            required property var modelData
            readonly property string markerLabel: modelData.label
            readonly property bool isStart: markerLabel === "Start"
            readonly property bool isEnd: markerLabel === "End"

            graphsView: chart.graphsViewRef
            axisX: timeAxis
            axisY: pressureAxis
            points: [Qt.point(modelData.time, 0), Qt.point(modelData.time, 100)]
            strokeColor: isStart ? Theme.accentColor : Theme.frameMarkerColor
            strokeWidth: isStart ? Theme.scaled(2) : Theme.scaled(1)
            dashPattern: isStart ? [4, 2, 1, 2] : [1, 3]
            // "End" markers were inconsistently emitted in older history rows; the
            // last frame-transition marker already signals end of extraction.
            visible: !isEnd
        }
    }

    // Phase marker labels
    Repeater {
        id: markerLabels
        model: chart.phaseMarkers

        delegate: Item {
            id: markerDelegate
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property string markerLabel: modelData.label
            property string transitionReason: modelData.transitionReason || ""
            property bool isStart: modelData.label === "Start"

            x: graphsView.plotArea.x + (markerTime / timeAxis.max) * graphsView.plotArea.width
            y: graphsView.plotArea.y
            height: graphsView.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0 && chart.showPhaseLabels
                     && markerLabel !== "End"

            Text {
                text: {
                    if (markerDelegate.transitionReason === "") return markerDelegate.markerLabel
                    var suffix = ""
                    switch (markerDelegate.transitionReason) {
                        case "weight": suffix = " [W]"; break
                        case "pressure": suffix = " [P]"; break
                        case "pressure_unconfirmed": suffix = " [P]"; break
                        case "flow": suffix = " [F]"; break
                        case "flow_unconfirmed": suffix = " [F]"; break
                        case "time": suffix = " [T]"; break
                    }
                    return markerDelegate.markerLabel + suffix
                }
                font.pixelSize: Theme.scaled(14)
                font.bold: markerDelegate.isStart
                color: markerDelegate.isStart ? Theme.accentColor : Qt.rgba(1, 1, 1, 0.8)
                rotation: -90
                transformOrigin: Item.TopLeft
                x: Theme.scaled(3)
                y: Theme.scaled(6) + width

                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(-2)
                    color: Qt.darker(Theme.surfaceColor, 1.5)
                    radius: Theme.scaled(2)
                }
            }
        }
    }

    // Pump mode indicator bars at bottom of chart
    Repeater {
        id: pumpModeIndicators
        model: chart.phaseMarkers

        delegate: Rectangle {
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property bool isFlowMode: modelData.isFlowMode || false
            property double nextTime: {
                if (index < chart.phaseMarkers.length - 1) {
                    return chart.phaseMarkers[index + 1].time
                }
                return chart.maxTime
            }

            x: graphsView.plotArea.x + (markerTime / timeAxis.max) * graphsView.plotArea.width
            y: graphsView.plotArea.y + graphsView.plotArea.height - Theme.scaled(4)
            width: Math.max(0, ((nextTime - markerTime) / timeAxis.max) * graphsView.plotArea.width)
            height: Theme.scaled(4)
            color: isFlowMode ? Theme.flowColor : Theme.pressureColor
            opacity: 0.8
            visible: markerTime <= timeAxis.max && modelData.label !== "Start" && modelData.label !== "End"
            Accessible.ignored: true
        }
    }

    // Crosshair vertical line
    Rectangle {
        id: crosshairLine
        visible: chart.inspecting
        x: chart.inspectPixelX - width / 2
        y: graphsView.plotArea.y
        width: Theme.scaled(1)
        height: graphsView.plotArea.height
        color: Theme.textColor
        opacity: 0.6
        Accessible.ignored: true
    }

    // Right-axis label column, shared with the live graph.
    GraphRightAxisLabels {
        visible: chart.showLabels
        x: graphsView.plotArea.x + graphsView.plotArea.width + Theme.scaled(4)
        y: graphsView.plotArea.y
        width: chart.width - x
        height: graphsView.plotArea.height

        mode: chart.rightAxisMode
        weightAxis: weightAxis
        tempAxis: tempAxis
        flowAxis: flowAxis
        onTapped: chart.toggleRightAxis()
    }
}
