import QtQuick
import QtGraphs
import Decenza
import "graphs"

// Outer Item wraps the GraphsView so all 30 trace overlays (3 shots × 10
// curves), the Canvas phase markers, the crosshair, and phase labels render
// as siblings on top of the chart. GraphsView's scene-graph paints over any
// direct QQuickItem children — overlays must be siblings, not children.
Item {
    id: chart

    // Shot comparison model
    property var comparisonModel: null

    // Visibility toggles for curve types (shared with HistoryShotGraph via Settings).
    property bool showPressure: Settings.boolValue("graph/showPressure", true)
    property bool showFlow: Settings.boolValue("graph/showFlow", true)
    property bool showTemperature: Settings.boolValue("graph/showTemperature", true)
    property bool showWeight: Settings.boolValue("graph/showWeight", true)
    property bool showWeightFlow: Settings.boolValue("graph/showWeightFlow", true)
    property bool showResistance: Settings.boolValue("graph/showResistance", false)
    property bool showConductance: Settings.boolValue("graph/showConductance", false)
    property bool showConductanceDerivative: Settings.boolValue("graph/showConductanceDerivative", false)
    property bool showDarcyResistance: Settings.boolValue("graph/showDarcyResistance", false)
    property bool showTemperatureMix: Settings.boolValue("graph/showTemperatureMix", false)

    property bool advancedMode: false

    // Per-shot visibility (window slot 0/1/2)
    property bool showShot0: true
    property bool showShot1: true
    property bool showShot2: true

    // Phase marker data: [{shotIdx, time, label, phaseIndex}]
    property var phaseData: []

    // Per-phase colors (index = phaseIndex % count)
    readonly property var phaseColors: ["#FFD600", "#E91E63", "#00E5FF", "#76FF03", "#FF6D00"]

    // Hidden phase labels: {label: true} means hidden
    property var hiddenPhaseLabels: ({})
    function togglePhaseLabel(label) {
        var h = Object.assign({}, hiddenPhaseLabels)
        h[label] = !h[label]
        hiddenPhaseLabels = h
    }

    // Crosshair / inspect state
    property bool inspecting: false
    property real inspectTime: 0
    readonly property real inspectPixelX: inspecting
        ? graphsView.plotArea.x + (inspectTime / timeAxis.max) * graphsView.plotArea.width
        : 0
    property var inspectShotValues: []

    // Alias so DashedLineSeries delegates can reach the GraphsView without
    // writing `graphsView: graphsView` — that RHS shadows the delegate's own
    // `graphsView` property (which defaults to `parent`) and resolves to null.
    readonly property alias graphsViewRef: graphsView

    // Re-export the GraphsView's plot rect for parent pages that hit-test
    // against it (e.g. the comparison page tap-to-inspect overlay).
    readonly property rect plotArea: graphsView.plotArea

    // === Curve definitions driving the 3-shot × 10-curve Repeater ===
    //
    // Each curve names the model accessor key, the axisY holder to map against,
    // its colour, stroke width, the show-flag gating visibility, and whether it
    // is advanced-mode-only. Per-curve y-value transforms (currently just the
    // /5 weight rescale) live in `_curvePoints()` as a key-dispatched branch.
    //
    // Axis is stored as a string key, not a direct id reference: this property
    // initialiser runs before GraphsView and its child ValueAxes are constructed,
    // so direct ids would resolve to null and the `var` binding wouldn't re-fire
    // when the axes appear. The delegate calls chart._axisFor(curveDef.axisKey)
    // at bind time, by which point every axis id is valid.
    readonly property var _curves: [
        { key: "pressure",              axisKey: "pressure", color: Theme.pressureColor,             width: Theme.graphLineWidth,                 advanced: false, showFlag: "showPressure" },
        { key: "flow",                  axisKey: "pressure", color: Theme.flowColor,                 width: Theme.graphLineWidth,                 advanced: false, showFlag: "showFlow" },
        { key: "temperature",           axisKey: "temp",     color: Theme.temperatureColor,          width: Theme.graphLineWidth,                 advanced: false, showFlag: "showTemperature" },
        { key: "weight",                axisKey: "weight",   color: Theme.weightColor,               width: Math.max(1, Theme.graphLineWidth-1),  advanced: false, showFlag: "showWeight" },
        { key: "weightFlow",            axisKey: "pressure", color: Theme.weightFlowColor,           width: Math.max(1, Theme.graphLineWidth-1),  advanced: false, showFlag: "showWeightFlow" },
        { key: "resistance",            axisKey: "pressure", color: Theme.resistanceColor,           width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showResistance" },
        { key: "conductance",           axisKey: "pressure", color: Theme.conductanceColor,          width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showConductance" },
        { key: "conductanceDerivative", axisKey: "dCdt",     color: Theme.conductanceDerivativeColor,width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showConductanceDerivative" },
        { key: "darcyResistance",       axisKey: "pressure", color: Theme.darcyResistanceColor,      width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showDarcyResistance" },
        { key: "temperatureMix",        axisKey: "temp",     color: Theme.temperatureMixColor,       width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showTemperatureMix" }
    ]

    function _axisFor(axisKey) {
        switch (axisKey) {
            case "pressure": return pressureAxis
            case "temp":     return tempAxis
            case "weight":   return weightAxis
            case "dCdt":     return dCdtAxis
            default:         return pressureAxis
        }
    }

    // Per-shot stroke styles. Shot 0 solid, 1 dashed, 2 dash-dot — matches the
    // legacy Qt Charts comparison view that used Qt.SolidLine / DashLine / DashDotLine.
    readonly property var _shotStyles: [
        { dashed: false, pattern: [4, 4] },
        { dashed: true,  pattern: [6, 5] },
        { dashed: true,  pattern: [10, 4, 2, 4] }
    ]

    // Fetch a curve's points for a given shot, applying any per-curve transform
    // (currently just the /5 weight rescale). _dataVersion is read so the
    // binding re-fires when comparisonModel.shotsChanged emits (shotCount is
    // its NOTIFY-attached read property). Static dispatch on the curve key —
    // QML's Q_INVOKABLE bracket-access doesn't reliably work cross-version,
    // so we call each accessor by name.
    function _curvePoints(shotIdx, key) {
        var _ = _dataVersion
        if (!comparisonModel) return []
        var data
        switch (key) {
            case "pressure":              data = comparisonModel.getPressureData(shotIdx); break
            case "flow":                  data = comparisonModel.getFlowData(shotIdx); break
            case "temperature":           data = comparisonModel.getTemperatureData(shotIdx); break
            case "weight":                data = comparisonModel.getWeightData(shotIdx); break
            case "weightFlow":            data = comparisonModel.getWeightFlowRateData(shotIdx); break
            case "resistance":            data = comparisonModel.getResistanceData(shotIdx); break
            case "conductance":           data = comparisonModel.getConductanceData(shotIdx); break
            case "conductanceDerivative": data = comparisonModel.getConductanceDerivativeData(shotIdx); break
            case "darcyResistance":       data = comparisonModel.getDarcyResistanceData(shotIdx); break
            case "temperatureMix":        data = comparisonModel.getTemperatureMixData(shotIdx); break
            default:                      return []
        }
        if (key === "weight") {
            var scaled = []
            for (var i = 0; i < data.length; i++) {
                scaled.push({ x: data[i].x, y: data[i].y / 5.0 })
            }
            return scaled
        }
        return data
    }

    // Monotonic counter bumped on every shotsChanged emission — used as a
    // read-dependency in every points binding so they re-evaluate then.
    // Binding to comparisonModel.shotCount is insufficient because the window
    // navigation (shiftWindowLeft/Right) often keeps the count constant while
    // the underlying displayShots vector is replaced.
    property int _dataVersion: 0

    // === Axis fitting (timeAxis stretches to maxTime + padding; dCdtAxis fits the data) ===

    function _niceTimeAxisStep(span) {
        if (span <= 5)  return 1
        if (span <= 10) return 2
        if (span <= 30) return 5
        return 10
    }

    function _updateTimeAxis() {
        if (!comparisonModel) return
        var markerMaxTime = 0
        for (var pmi = 0; pmi < comparisonModel.shotCount; pmi++) {
            var pmMarkers = comparisonModel.getPhaseMarkers(pmi)
            for (var pmj = 0; pmj < pmMarkers.length; pmj++) {
                if (pmMarkers[pmj].time > markerMaxTime) markerMaxTime = pmMarkers[pmj].time
            }
        }
        var axisEnd = Math.max(comparisonModel.maxTime, markerMaxTime)
        var plotWidth = Math.max(1, graphsView.plotArea.width)
        var paddingPx = Theme.scaled(5)
        var scale = plotWidth / Math.max(1, plotWidth - paddingPx)
        // Fit to data with a 5 s floor — short shots still fill the plot. Match
        // ShotGraph/SteamGraph dynamic tickInterval so labels stay readable from
        // a 5 s pour through a 60 s+ extraction.
        timeAxis.max = Math.max(5, axisEnd * scale)
        timeAxis.tickInterval = _niceTimeAxisStep(timeAxis.max)
    }

    function _updateDCdtAxis() {
        if (!comparisonModel) return
        var dCdtMax = 0, dCdtMin = 0
        for (var s = 0; s < comparisonModel.shotCount; s++) {
            var pts = comparisonModel.getConductanceDerivativeData(s)
            for (var i = 0; i < pts.length; i++) {
                if (pts[i].y > dCdtMax) dCdtMax = pts[i].y
                if (pts[i].y < dCdtMin) dCdtMin = pts[i].y
            }
        }
        var padded = dCdtMax * 1.15
        var posMax
        if (padded <= 2) posMax = 2
        else if (padded <= 3) posMax = 3
        else if (padded <= 5) posMax = 5
        else if (padded <= 8) posMax = 8
        else if (padded <= 10) posMax = 10
        else posMax = Math.ceil(padded / 5) * 5
        dCdtAxis.max = posMax
        dCdtAxis.min = dCdtMin < 0 ? -Math.abs(dCdtMin) * 1.15 : 0
    }

    // Build phaseData + hiddenPhaseLabels, and seed the inspect crosshair at
    // the second-to-last phase (the default-visible one).
    function _rebuildPhaseData() {
        if (!comparisonModel) { phaseData = []; return }

        var phases = []
        var phaseIndexMap = {}, nextPhaseIndex = 0
        for (var pi = 0; pi < comparisonModel.shotCount; pi++) {
            var markers = comparisonModel.getPhaseMarkers(pi)
            for (var mi = 0; mi < markers.length; mi++) {
                var lbl = markers[mi].label
                if (lbl === "Start") continue  // redundant — always 0.0s
                if (lbl === "End") continue    // only added on SAW stops; inconsistent
                if (phaseIndexMap[lbl] === undefined) phaseIndexMap[lbl] = nextPhaseIndex++
                phases.push({ shotIdx: pi, time: markers[mi].time, label: lbl, phaseIndex: phaseIndexMap[lbl] })
            }
        }
        phaseData = phases

        // Default visibility: hide all phase labels except the last 2 unique ones.
        var uniqueLabels = []
        var seenLabels = {}
        for (var ui = 0; ui < phases.length; ui++) {
            var ul = phases[ui].label
            if (!seenLabels[ul]) { seenLabels[ul] = true; uniqueLabels.push(ul) }
        }
        var hidden = {}
        for (var hi = 0; hi < uniqueLabels.length - 2; hi++) {
            hidden[uniqueLabels[hi]] = true
        }
        hiddenPhaseLabels = hidden

        // Seed crosshair at the default-visible second-to-last phase, averaged across shots.
        if (uniqueLabels.length >= 2) {
            var targetLabel = uniqueLabels[uniqueLabels.length - 2]
            var timeSum = 0, timeCount = 0
            for (var ti = 0; ti < phases.length; ti++) {
                if (phases[ti].label === targetLabel) { timeSum += phases[ti].time; timeCount++ }
            }
            if (timeCount > 0) Qt.callLater(inspectAtTime, timeSum / timeCount)
        } else {
            dismissInspect()
        }
    }

    function _refreshAll() {
        _dataVersion++       // invalidate all trace bindings
        _updateTimeAxis()
        _updateDCdtAxis()
        _rebuildPhaseData()
    }

    // === Inspect / crosshair ===

    function _timeAtPixel(pixelX) {
        var plot = graphsView.plotArea
        if (!plot || plot.width <= 0) return -1
        return timeAxis.min + (pixelX - plot.x) / plot.width * (timeAxis.max - timeAxis.min)
    }

    function inspectAtPosition(pixelX, pixelY) {
        if (!comparisonModel) return
        var time = _timeAtPixel(pixelX)
        if (time < 0 || time > timeAxis.max) {
            dismissInspect()
            return
        }

        inspectTime = time

        var shotValues = []
        for (var i = 0; i < comparisonModel.shotCount; i++) {
            shotValues.push(_buildShotValues(i, comparisonModel.getValuesAtTime(i, time),
                                              comparisonModel.getShotInfo(i)))
        }
        inspectShotValues = shotValues
        inspecting = true

        // Accessibility announcement
        var parts = [time.toFixed(1) + "s"]
        for (var s = 0; s < shotValues.length; s++) {
            var sv = shotValues[s]
            var metrics = []
            if (sv.hasPressure)    metrics.push(sv.pressure.toFixed(1) + " bar")
            if (sv.hasFlow)        metrics.push(sv.flow.toFixed(1) + " mL/s")
            if (sv.hasTemperature) metrics.push(sv.temperature.toFixed(1) + " degrees")
            if (sv.hasWeight)      metrics.push(sv.weight.toFixed(1) + " grams")
            if (metrics.length > 0)
                parts.push(sv.dateTime + ": " + metrics.join(", "))
        }
        if (typeof AccessibilityManager !== "undefined" && parts.length > 1)
            AccessibilityManager.announce(parts.join(". "), true)
    }

    function _buildShotValues(i, vals, info) {
        return {
            dateTime:       info.dateTime,
            hasPressure:    vals.hasPressure,   pressure:       vals.pressure,
            hasFlow:        vals.hasFlow,       flow:           vals.flow,
            hasTemperature: vals.hasTemperature,temperature:    vals.temperature,
            hasWeight:      vals.hasWeight,     weight:         vals.weight,
            hasWeightFlow:  vals.hasWeightFlow, weightFlow:     vals.weightFlow,
            hasResistance:  vals.hasResistance, resistance:     vals.resistance,
            hasConductance: vals.hasConductance,conductance:    vals.conductance,
            hasDarcyResistance:        vals.hasDarcyResistance,        darcyResistance:        vals.darcyResistance,
            hasConductanceDerivative:  vals.hasConductanceDerivative,  conductanceDerivative:  vals.conductanceDerivative,
            hasTemperatureMix:         vals.hasTemperatureMix,         temperatureMix:         vals.temperatureMix
        }
    }

    function inspectAtTime(time) {
        if (!comparisonModel || time < 0 || time > timeAxis.max) return
        inspectTime = time
        var shotValues = []
        for (var i = 0; i < comparisonModel.shotCount; i++) {
            shotValues.push(_buildShotValues(i, comparisonModel.getValuesAtTime(i, time),
                                              comparisonModel.getShotInfo(i)))
        }
        inspectShotValues = shotValues
        inspecting = true
    }

    function dismissInspect() {
        inspecting = false
        inspectShotValues = []
    }

    onComparisonModelChanged: _refreshAll()
    Component.onCompleted: _refreshAll()

    Connections {
        target: comparisonModel
        function onShotsChanged() { chart._refreshAll() }
    }

    GraphsView {
        id: graphsView
        anchors.fill: parent
        theme: DecenzaGraphsTheme {}

        axisX: timeAxis
        axisY: pressureAxis

        onPlotAreaChanged: chart._updateTimeAxis()

        ValueAxis {
            id: timeAxis
            min: 0
            max: 60
            tickInterval: 10
            subTickCount: 0
            labelFormat: "%.0f"
            titleText: "Time (s)"
        }

        // Pressure/Flow/WeightFlow axis (left Y). When resistance/conductance/Darcy
        // are enabled, expand the axis to [0, 20] so they don't visually clip.
        ValueAxis {
            id: pressureAxis
            readonly property bool hasAdvancedCurve: chart.advancedMode
                && (chart.showResistance || chart.showConductance || chart.showDarcyResistance)
            min: 0
            max: hasAdvancedCurve ? 20 : 12
            tickInterval: hasAdvancedCurve ? 5 : 3
            subTickCount: 0
            labelFormat: "%.0f"
            titleText: "bar / mL/s"
        }
    }

    // === HIDDEN RIGHT-AXIS HOLDERS ===
    // DashedLineSeries reads min/max for its data→pixel mapping; QtObject
    // suffices — no Qt Graphs ValueAxis needed.
    QtObject {
        id: tempAxis
        property real min: 40
        property real max: 100
    }

    QtObject {
        id: weightAxis
        property real min: 0
        // Weight points are pre-divided by 5 in _curvePoints() (so 60 g → 12),
        // then mapped against this 0–12 axis so 60 g lands at the top — same
        // visual height as 12 bar on the left axis. Matches the legacy
        // weightAxis range in the Qt Charts comparison view.
        property real max: 12
    }

    // Initial values only — _updateDCdtAxis() rewrites min/max from the actual
    // data range every time shotsChanged fires.
    QtObject {
        id: dCdtAxis
        property real min: 0
        property real max: 20
    }

    // === 30 trace overlays via a flat Repeater (3 shots × 10 curves) ===
    // Nested Repeaters inside a wrapper Item appear to confuse the scene-graph
    // parenting for Shape items inside DashedLineSeries — flattening to a
    // single Repeater with a pre-built model gets reliable rendering.

    readonly property var _allTraces: {
        var out = []
        for (var s = 0; s < 3; s++) {
            for (var c = 0; c < _curves.length; c++) {
                out.push({ shotIdx: s, curveIdx: c })
            }
        }
        return out
    }

    function _shotVisibleAt(shotIdx) {
        return shotIdx === 0 ? showShot0
             : shotIdx === 1 ? showShot1
                             : showShot2
    }

    Repeater {
        model: chart._allTraces
        delegate: DashedLineSeries {
            required property var modelData
            readonly property var curveDef: chart._curves[modelData.curveIdx]
            readonly property var shotStyle: chart._shotStyles[modelData.shotIdx]

            graphsView: chart.graphsViewRef
            axisX: timeAxis
            axisY: chart._axisFor(curveDef.axisKey)
            points: chart._curvePoints(modelData.shotIdx, curveDef.key)
            strokeColor: curveDef.color
            strokeWidth: curveDef.width
            dashed: shotStyle.dashed
            dashPattern: shotStyle.pattern
            visible: chart._shotVisibleAt(modelData.shotIdx)
                     && chart[curveDef.showFlag]
                     && (!curveDef.advanced || chart.advancedMode)
        }
    }

    // === Phase marker vertical lines — Canvas keeps the per-shot dash pattern ===

    Canvas {
        id: phaseCanvas
        x: graphsView.plotArea.x
        y: graphsView.plotArea.y
        width: graphsView.plotArea.width
        height: graphsView.plotArea.height
        z: 5
        onXChanged: requestPaint()
        onYChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: chart
            function onPhaseDataChanged()         { phaseCanvas.requestPaint() }
            function onHiddenPhaseLabelsChanged() { phaseCanvas.requestPaint() }
        }
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (var i = 0; i < chart.phaseData.length; i++) {
                var pd = chart.phaseData[i]
                if (chart.hiddenPhaseLabels[pd.label]) continue
                var x = (pd.time / timeAxis.max) * width
                ctx.strokeStyle = chart.phaseColors[pd.phaseIndex % chart.phaseColors.length]
                ctx.globalAlpha = 0.7
                ctx.lineWidth = 1.5
                if      (pd.shotIdx === 0) ctx.setLineDash([])
                else if (pd.shotIdx === 1) ctx.setLineDash([6, 5])
                else                       ctx.setLineDash([10, 4, 2, 4])
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
        }
    }

    // Phase label text (shown at top of plot area, shot 0 only to avoid duplicates)
    Repeater {
        model: chart.phaseData
        Text {
            required property var modelData
            visible: modelData.shotIdx === 0 && !chart.hiddenPhaseLabels[modelData.label]
            x: graphsView.plotArea.x + (modelData.time / timeAxis.max) * graphsView.plotArea.width + Theme.scaled(2)
            y: graphsView.plotArea.y
            text: modelData.label
            font: Theme.captionFont
            color: chart.phaseColors[modelData.phaseIndex % chart.phaseColors.length]
            opacity: 0.9
            z: 6
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
    }
}
