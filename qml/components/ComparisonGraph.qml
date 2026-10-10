// The 30-trace and phase-label delegates read this file's ids (`chart`,
// `timeAxis`, `graphsView`); Bound makes them statically resolvable. Both already
// declare their one injected role, `modelData`, required, so Bound cannot break role
// injection here.
pragma ComponentBehavior: Bound

import QtQuick
import QtGraphs
import Decenza
import "GraphUtils.js" as GraphUtils

// Outer Item wraps the GraphsView so every trace overlay (visible shots × curves),
// the Canvas phase markers, the crosshair and the phase labels render
// as siblings on top of the chart. GraphsView's scene-graph paints over any
// direct QQuickItem children — overlays must be siblings, not children.
Item {
    id: chart

    // Shot comparison model
    property var comparisonModel: null

    // Curve visibility is shared with the live and history graphs — it binds straight to
    // SettingsGraph rather than mirroring it into local properties.

    property bool advancedMode: Settings.graph.advancedMode

    // Factor applied to the flow-family traces before plotting (1, 2 or 3).
    property int flowMultiplier: Settings.graph.flowMultiplier

    // Shots hidden from the graph, by shot id: paging and re-basing reuse column
    // indices, and a hidden shot must not hand its state to whichever lands there.
    property var hiddenShots: ({})
    function _shotIdAt(i) {
        var _ = _dataVersion
        return comparisonModel ? comparisonModel.getShotInfo(i).id : undefined
    }
    function shotVisible(i) { return !hiddenShots[_shotIdAt(i)] }
    function toggleShot(i) {
        var id = _shotIdAt(i)
        if (id === undefined) return
        var h = Object.assign({}, hiddenShots)
        h[id] = !h[id]
        hiddenShots = h
    }

    // Line each shot's pour start up with the base's, so a longer preinfusion does
    // not offset every later curve. Shots with no pour marker stay where they are.
    property bool alignAtPourStart: false
    function _offsetFor(i) {
        var _ = _dataVersion
        if (!alignAtPourStart || !comparisonModel || i === 0) return 0
        var base = comparisonModel.getShotInfo(0).pourStartSec || 0
        var own = comparisonModel.getShotInfo(i).pourStartSec || 0
        return base > 0 && own > 0 ? base - own : 0
    }
    onAlignAtPourStartChanged: _refreshAll()

    // Phase marker data: [{shotIdx, time, label, phaseIndex}]
    property var phaseData: []

    // Per-phase colors (index = phaseIndex % count)
    readonly property var phaseColors: ["#FFD600", "#E91E63", "#00E5FF", "#76FF03", "#FF6D00"]

    // Hidden phase labels: {label: true} means hidden
    property var hiddenPhaseLabels: ({})
    function togglePhaseLabel(label) {
        hiddenPhaseLabels = GraphUtils.toggledPhaseLabels(hiddenPhaseLabels, label)
    }

    // Crosshair / inspect state
    property bool inspecting: false
    property real inspectTime: 0
    readonly property real inspectPixelX: inspecting
        ? graphsView.plotArea.x + (inspectTime / timeAxis.max) * graphsView.plotArea.width
        : 0
    property var inspectShotValues: []

    // Alias so the series instantiator can reach the GraphsView: `graphsView: graphsView`
    // would resolve the RHS to its own `graphsView` property.
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
        { key: "flow",                  axisKey: "flow"    , color: Theme.flowColor,                 width: Theme.graphLineWidth,                 advanced: false, showFlag: "showFlow" },
        { key: "temperature",           axisKey: "temp",     color: Theme.temperatureColor,          width: Theme.graphLineWidth,                 advanced: false, showFlag: "showTemperature" },
        { key: "weight",                axisKey: "weight",   color: Theme.weightColor,               width: Math.max(1, Theme.graphLineWidth-1),  advanced: false, showFlag: "showWeight" },
        { key: "weightFlow",            axisKey: "flow"    , color: Theme.weightFlowColor,           width: Math.max(1, Theme.graphLineWidth-1),  advanced: false, showFlag: "showWeightFlow" },
        { key: "resistance",            axisKey: "pressure", color: Theme.resistanceColor,           width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showResistance" },
        { key: "conductance",           axisKey: "pressure", color: Theme.conductanceColor,          width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showConductance" },
        { key: "conductanceDerivative", axisKey: "dCdt",     color: Theme.conductanceDerivativeColor,width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showConductanceDerivative" },
        { key: "darcyResistance",       axisKey: "pressure", color: Theme.darcyResistanceColor,      width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showDarcyResistance" },
        { key: "temperatureMix",        axisKey: "temp",     color: Theme.temperatureMixColor,       width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showTemperatureMix" },
        { key: "temperatureMixGoal",    axisKey: "temp",     color: Theme.temperatureMixGoalColor,   width: Math.max(1, Theme.graphLineWidth-1),  advanced: true,  showFlag: "showTemperatureMixGoal" }
    ]

    function _axisFor(axisKey) {
        switch (axisKey) {
            case "pressure": return pressureAxis
            // Flow family maps through a shrunken range so the multiplier scales the trace
            // without touching the data or the pressure axis everything else reads.
            case "flow":     return flowAxis
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
            case "temperatureMixGoal":    data = comparisonModel.getTemperatureMixGoalData(shotIdx); break
            default:                      return []
        }
        var dx = _offsetFor(shotIdx)
        var dy = key === "weight" ? 5.0 : 1.0
        if (dx === 0 && dy === 1.0) return data
        let moved = []
        for (let i = 0; i < data.length; i++)
            moved.push(Qt.point(data[i].x + dx, data[i].y / dy))
        return moved
    }

    // Monotonic counter bumped on every shotsChanged emission — used as a
    // read-dependency in every points binding so they re-evaluate then.
    // Binding to comparisonModel.shotCount is insufficient because the window
    // navigation (shiftWindowLeft/Right) often keeps the count constant while
    // the underlying displayShots vector is replaced.
    property int _dataVersion: 0

    // === Axis fitting (timeAxis stretches to maxTime + padding; dCdtAxis fits the data) ===

    function _updateTimeAxis() {
        if (!comparisonModel) return
        var markerMaxTime = 0
        for (let pmi = 0; pmi < comparisonModel.shotCount; pmi++) {
            let pmMarkers = comparisonModel.getPhaseMarkers(pmi)
            for (let pmj = 0; pmj < pmMarkers.length; pmj++) {
                if (pmMarkers[pmj].time + _offsetFor(pmi) > markerMaxTime) markerMaxTime = pmMarkers[pmj].time + _offsetFor(pmi)
            }
        }
        var maxOffset = 0
        for (let oi = 0; oi < comparisonModel.shotCount; oi++) maxOffset = Math.max(maxOffset, _offsetFor(oi))
        var axisEnd = Math.max(comparisonModel.maxTime + maxOffset, markerMaxTime)
        // Fit to data with a 5 s floor — short shots still fill the plot. Match
        // ShotGraph/SteamGraph dynamic tickInterval so labels stay readable from
        // a 5 s pour through a 60 s+ extraction.
        timeAxis.max = Math.max(5, GraphUtils.paddedAxisEnd(axisEnd, graphsView.plotArea.width, Theme.scaled(5)))
        timeAxis.tickInterval = GraphUtils.niceTimeAxisStep(timeAxis.max)
    }

    function _updateDCdtAxis() {
        if (!comparisonModel) return
        var dCdtMax = 0, dCdtMin = 0
        for (let s = 0; s < comparisonModel.shotCount; s++) {
            let pts = comparisonModel.getConductanceDerivativeData(s)
            for (let i = 0; i < pts.length; i++) {
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
        for (let pi = 0; pi < comparisonModel.shotCount; pi++) {
            let markers = comparisonModel.getPhaseMarkers(pi)
            for (let mi = 0; mi < markers.length; mi++) {
                let lbl = markers[mi].label
                if (lbl === "Start") continue  // redundant — always 0.0s
                if (lbl === "End") continue    // only added on SAW stops; inconsistent
                if (phaseIndexMap[lbl] === undefined) phaseIndexMap[lbl] = nextPhaseIndex++
                phases.push({ shotIdx: pi, time: markers[mi].time + _offsetFor(pi), label: lbl, phaseIndex: phaseIndexMap[lbl] })
            }
        }
        phaseData = phases

        // Default visibility: hide all phase labels except the last 2 unique ones.
        var uniqueLabels = []
        var seenLabels = {}
        for (let ui = 0; ui < phases.length; ui++) {
            let ul = phases[ui].label
            if (!seenLabels[ul]) { seenLabels[ul] = true; uniqueLabels.push(ul) }
        }
        var hidden = {}
        for (let hi = 0; hi < uniqueLabels.length - 2; hi++) {
            hidden[uniqueLabels[hi]] = true
        }
        hiddenPhaseLabels = hidden

        // Seed crosshair at the default-visible second-to-last phase, averaged across shots.
        if (uniqueLabels.length >= 2) {
            let targetLabel = uniqueLabels[uniqueLabels.length - 2]
            let timeSum = 0, timeCount = 0
            for (let ti = 0; ti < phases.length; ti++) {
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
        return GraphUtils.timeAtPixel(pixelX, graphsView.plotArea, timeAxis.min, timeAxis.max)
    }

    function inspectAtPosition(pixelX, pixelY) {
        if (!comparisonModel) return
        var time = _timeAtPixel(pixelX)
        if (time < 0 || time > timeAxis.max) {
            dismissInspect()
            return
        }

        inspectTime = time

        var shotValues = _valuesAt(time)
        inspectShotValues = shotValues
        inspecting = true

        // Accessibility announcement
        var parts = [time.toFixed(1) + "s"]
        for (let s = 0; s < shotValues.length; s++) {
            let sv = shotValues[s]
            let metrics = []
            if (sv.hasPressure)    metrics.push(sv.pressure.toFixed(1) + " bar")
            if (sv.hasFlow)        metrics.push(sv.flow.toFixed(1) + " mL/s")
            if (sv.hasTemperature) metrics.push(Theme.cToDisplay(sv.temperature).toFixed(1) + " " + Theme.tempUnitSuffix())
            if (sv.hasWeight)      metrics.push(sv.weight.toFixed(1) + " grams")
            if (metrics.length > 0)
                parts.push(sv.dateTime + ": " + metrics.join(", "))
        }
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && parts.length > 1)
            AccessibilityManager.announce(parts.join(". "), true)
    }

    function _valuesAt(time) {
        var out = []
        for (let i = 0; i < comparisonModel.shotCount; i++)
            out.push(_buildShotValues(i, comparisonModel.getValuesAtTime(i, time - _offsetFor(i)),
                                      comparisonModel.getShotInfo(i)))
        return out
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
            hasTemperatureMix:         vals.hasTemperatureMix,         temperatureMix:         vals.temperatureMix,
            hasTemperatureMixGoal:     vals.hasTemperatureMixGoal,     temperatureMixGoal:     vals.temperatureMixGoal
        }
    }

    function inspectAtTime(time) {
        if (!comparisonModel || time < 0 || time > timeAxis.max) return
        inspectTime = time
        inspectShotValues = _valuesAt(time)
        inspecting = true
    }

    function dismissInspect() {
        inspecting = false
        inspectShotValues = []
    }

    onComparisonModelChanged: _refreshAll()
    Component.onCompleted: _refreshAll()

    Connections {
        target: chart.comparisonModel
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
                && (Settings.graph.showResistance || Settings.graph.showConductance || Settings.graph.showDarcyResistance)
            min: 0
            max: hasAdvancedCurve ? 20 : 12
            tickInterval: hasAdvancedCurve ? 5 : 3
            subTickCount: 0
            labelFormat: "%.0f"
            // Pressure-only once a multiplier is active — the flow family no longer reads
            // against this axis.
            titleText: chart.flowMultiplier === 1 ? "bar / mL/s" : "bar"
        }
    }

    // === HIDDEN RIGHT-AXIS RANGES ===
    // Invisible axes the series map through (a series may carry its own axisY since
    // Qt 6.10). A hidden axis reserves no plot space (axisrenderer.cpp:1082).

    // Flow-family mapping, tracking the pressure axis so one multiplier drives both.
    //
    // Note this axis also inherits the pressure axis's expansion to 20 under advanced
    // curves, which shrinks the flow trace by 40% — inherited behaviour from when flow was
    // plotted on the pressure axis directly, not something the multiplier introduces.
    ValueAxis {
        id: flowAxis
        visible: false
        min: pressureAxis.min
        max: pressureAxis.max / chart.flowMultiplier
    }

    ValueAxis {
        id: tempAxis
        visible: false
        min: 40
        max: 100
    }

    ValueAxis {
        id: weightAxis
        visible: false
        min: 0
        // Weight points are pre-divided by 5 in _curvePoints() (so 60 g → 12),
        // then mapped against this 0–12 axis so 60 g lands at the top — same
        // visual height as 12 bar on the left axis. Matches the legacy
        // weightAxis range in the Qt Charts comparison view.
        max: 12
    }

    // Initial values only — _updateDCdtAxis() rewrites min/max from the actual
    // data range every time shotsChanged fires.
    ValueAxis {
        id: dCdtAxis
        visible: false
        min: 0
        max: 20
    }

    // === One native series per (visible shot slot × curve) ===

    readonly property var _allTraces: {
        var out = []
        var slots = comparisonModel ? comparisonModel.otherWindowSize + 1 : 0
        for (let s = 0; s < slots; s++) {
            for (let c = 0; c < _curves.length; c++) {
                out.push({ shotIdx: s, curveIdx: c })
            }
        }
        return out
    }

    GraphSeriesInstantiator {
        graphsView: chart.graphsViewRef
        model: chart._allTraces
        delegate: LineSeries {
            required property var modelData
            readonly property var curveDef: chart._curves[modelData.curveIdx]
            readonly property var shotStyle: chart._shotStyles[modelData.shotIdx]

            axisY: chart._axisFor(curveDef.axisKey)
            values: chart._curvePoints(modelData.shotIdx, curveDef.key)
            color: curveDef.color
            // The base is drawn heavier, as Decent's comparison does: the reference reads
            // as the reference without a legend.
            width: curveDef.width + (modelData.shotIdx === 0 ? Theme.scaled(1) : 0)
            strokeStyle: shotStyle.dashed ? LineSeries.StrokeStyle.DashLine : LineSeries.StrokeStyle.SolidLine
            dashPattern: shotStyle.pattern
            visible: chart.shotVisible(modelData.shotIdx)
                     && modelData.shotIdx < (chart.comparisonModel ? chart.comparisonModel.shotCount : 0)
                     && Settings.graph[curveDef.showFlag]
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
            function onHiddenShotsChanged()       { phaseCanvas.requestPaint() }
        }
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (let i = 0; i < chart.phaseData.length; i++) {
                let pd = chart.phaseData[i]
                if (chart.hiddenPhaseLabels[pd.label] || !chart.shotVisible(pd.shotIdx)) continue
                let x = (pd.time / timeAxis.max) * width
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

    // Phase labels sit on line 0 unless they would run into the label before
    // them, then drop a line: two phases a second apart used to print on top of
    // each other ("Infu" over "Pouring").
    FontMetrics { id: phaseLabelMetrics; font: Theme.captionFont }
    readonly property var phaseLabelLines: {
        var shown = []
        for (let i = 0; i < phaseData.length; i++) {
            let pd = phaseData[i]
            if (pd.shotIdx === 0 && !hiddenPhaseLabels[pd.label]) shown.push({ i: i, time: pd.time, label: pd.label })
        }
        shown.sort(function(a, b) { return a.time - b.time })
        var lines = {}, lineEnds = []
        for (let k = 0; k < shown.length; k++) {
            let x = (shown[k].time / timeAxis.max) * graphsView.plotArea.width
            let w = phaseLabelMetrics.advanceWidth(shown[k].label) + Theme.scaled(6)
            let line = 0
            while (line < lineEnds.length && lineEnds[line] > x) line++
            lineEnds[line] = x + w
            lines[shown[k].i] = line
        }
        return { lines: lines, count: lineEnds.length }
    }

    // Phase label text (shown at top of plot area, shot 0 only to avoid duplicates)
    Repeater {
        model: chart.phaseData
        Text {
            required property var modelData
            required property int index
            visible: modelData.shotIdx === 0 && !chart.hiddenPhaseLabels[modelData.label]
            x: graphsView.plotArea.x + (modelData.time / timeAxis.max) * graphsView.plotArea.width + Theme.scaled(2)
            y: graphsView.plotArea.y + (chart.phaseLabelLines.lines[index] || 0) * phaseLabelMetrics.height
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

    // === Crosshair values, read by GraphReadout under the plot ===

    // The curves currently drawn, from the one series table the chips also read.
    readonly property var readoutCurves: {
        var out = []
        var all = GraphSeries.entries
        for (let i = 0; i < all.length; i++) {
            if (all[i].portal) continue  // PORTAL is shown on single-shot graphs only
            if (all[i].advanced && !advancedMode) continue
            if (!Settings.graph[all[i].key]) continue
            out.push(all[i])
        }
        return out
    }

    readonly property int readoutRowCount: comparisonModel ? comparisonModel.shotCount : 0
    function readoutRowVisible(shotIdx) { return shotVisible(shotIdx) }

    // "" when the shot has no value there (not inspecting, or the shot had ended).
    function readoutText(shotIdx, dataKey) {
        if (!inspecting || shotIdx >= inspectShotValues.length) return ""
        var sv = inspectShotValues[shotIdx]
        switch (dataKey) {
            case "pressure":    return sv.hasPressure    ? sv.pressure.toFixed(1)    : ""
            case "flow":        return sv.hasFlow        ? sv.flow.toFixed(1)        : ""
            case "temp":        return sv.hasTemperature ? Theme.cToDisplay(sv.temperature).toFixed(1) : ""
            case "weight":      return sv.hasWeight      ? sv.weight.toFixed(1)      : ""
            case "weightFlow":  return sv.hasWeightFlow  ? sv.weightFlow.toFixed(1)  : ""
            case "resistance":  return sv.hasResistance  ? sv.resistance.toFixed(1)  : ""
            case "conductance": return sv.hasConductance ? sv.conductance.toFixed(1) : ""
            case "dCdt":        return sv.hasConductanceDerivative ? sv.conductanceDerivative.toFixed(1) : ""
            case "darcyR":      return sv.hasDarcyResistance       ? sv.darcyResistance.toFixed(1)       : ""
            case "mixTemp":     return sv.hasTemperatureMix        ? Theme.cToDisplay(sv.temperatureMix).toFixed(1) : ""
            case "mixTempGoal": return sv.hasTemperatureMixGoal    ? Theme.cToDisplay(sv.temperatureMixGoal).toFixed(1) : ""
        }
        return ""
    }
}
