import QtQuick
import QtCharts
import DecenzaDE1

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false

    margins.top: 0
    margins.bottom: 0
    margins.left: 0
    margins.right: 0

    // Shot comparison model
    property var comparisonModel: null

    // Visibility toggles for curve types (shared with HistoryShotGraph via Settings)
    property bool showPressure: Settings.value("graph/showPressure", true)
    property bool showFlow: Settings.value("graph/showFlow", true)
    property bool showTemperature: Settings.value("graph/showTemperature", true)
    property bool showWeight: Settings.value("graph/showWeight", true)
    property bool showWeightFlow: Settings.value("graph/showWeightFlow", true)
    property bool showResistance: Settings.value("graph/showResistance", false)

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
    // Computed binding so the line stays correct when plotArea resizes
    readonly property real inspectPixelX: inspecting
        ? chart.plotArea.x + (inspectTime / timeAxis.max) * chart.plotArea.width
        : 0
    // Array of { dateTime, hasTemperature, temperature, hasFlow, flow, hasWeight, weight }
    property var inspectShotValues: []

    // Load data from comparison model
    function loadData() {
        if (!comparisonModel) return

        // Bulk-populate all 3 shot slots via C++ QXYSeries::replace() — out-of-range
        // indices clear the series automatically.
        comparisonModel.populateSeries(0, pressure1, flow1, temp1, weight1, weightFlow1, resistance1)
        comparisonModel.populateSeries(1, pressure2, flow2, temp2, weight2, weightFlow2, resistance2)
        comparisonModel.populateSeries(2, pressure3, flow3, temp3, weight3, weightFlow3, resistance3)

        // Fit time axis to data
        timeAxis.max = Math.max(15, comparisonModel.maxTime + 0.5)

        // Build phase marker list (phaseIndex = stable color index per unique label)
        var phases = []
        var phaseIndexMap = {}, nextPhaseIndex = 0
        for (var pi = 0; pi < comparisonModel.shotCount; pi++) {
            var markers = comparisonModel.getPhaseMarkers(pi)
            for (var mi = 0; mi < markers.length; mi++) {
                var lbl = markers[mi].label
                if (lbl === "Start") continue  // redundant — always 0.0s
                if (phaseIndexMap[lbl] === undefined) phaseIndexMap[lbl] = nextPhaseIndex++
                phases.push({ shotIdx: pi, time: markers[mi].time, label: lbl, phaseIndex: phaseIndexMap[lbl] })
            }
        }
        phaseData = phases

        // Default visibility: hide all phases except End and the one before it
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

        // Set crosshair at the default-visible phase (second-to-last): average time across shots
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

    // Find the Y value in a LineSeries closest to the given time
    function findValueAtTime(series, time) {
        if (series.count === 0) return null
        var closest = series.at(0)
        var minDist = Math.abs(closest.x - time)
        for (var i = 1; i < series.count; i++) {
            var p = series.at(i)
            var dist = Math.abs(p.x - time)
            if (dist < minDist) {
                closest = p
                minDist = dist
            } else if (dist > minDist) {
                break  // sorted by x, so we passed the closest
            }
        }
        return minDist < 1.0 ? closest.y : null
    }

    // Show crosshair at a pixel position and build per-shot inspect values
    function inspectAtPosition(pixelX, pixelY) {
        if (!comparisonModel) return
        var dataPoint = chart.mapToValue(Qt.point(pixelX, pixelY), pressure1)
        var time = dataPoint.x
        if (time < 0 || time > timeAxis.max) {
            dismissInspect()
            return
        }

        inspectTime = time

        var shotValues = []
        for (var i = 0; i < comparisonModel.shotCount; i++) {
            var vals = comparisonModel.getValuesAtTime(i, time)
            var info = comparisonModel.getShotInfo(i)
            shotValues.push({
                dateTime:       info.dateTime,
                hasPressure:    vals.hasPressure,
                pressure:       vals.pressure,
                hasFlow:        vals.hasFlow,
                flow:           vals.flow,
                hasTemperature: vals.hasTemperature,
                temperature:    vals.temperature,
                hasWeight:      vals.hasWeight,
                weight:         vals.weight,
                hasWeightFlow:  vals.hasWeightFlow,
                weightFlow:     vals.weightFlow,
                hasResistance:  vals.hasResistance,
                resistance:     vals.resistance
            })
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

    // Place crosshair at a specific time (no pixel mapping needed)
    function inspectAtTime(time) {
        if (!comparisonModel || time < 0 || time > timeAxis.max) return
        inspectTime = time
        var shotValues = []
        for (var i = 0; i < comparisonModel.shotCount; i++) {
            var vals = comparisonModel.getValuesAtTime(i, time)
            var info = comparisonModel.getShotInfo(i)
            shotValues.push({
                dateTime:       info.dateTime,
                hasPressure:    vals.hasPressure,   pressure:    vals.pressure,
                hasFlow:        vals.hasFlow,        flow:        vals.flow,
                hasTemperature: vals.hasTemperature, temperature: vals.temperature,
                hasWeight:      vals.hasWeight,      weight:      vals.weight,
                hasWeightFlow:  vals.hasWeightFlow,  weightFlow:  vals.weightFlow,
                hasResistance:  vals.hasResistance,  resistance:  vals.resistance
            })
        }
        inspectShotValues = shotValues
        inspecting = true
    }

    // Dismiss the crosshair
    function dismissInspect() {
        inspecting = false
        inspectShotValues = []
    }

    onComparisonModelChanged: loadData()
    Component.onCompleted: loadData()

    Connections {
        target: comparisonModel
        function onShotsChanged() { loadData() }
    }

    // Time axis
    ValueAxis {
        id: timeAxis
        min: 0
        max: 60
        tickCount: 7
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        titleText: "Time (s)"
        titleBrush: Theme.textSecondaryColor
    }

    // Pressure/Flow/WeightFlow axis (left Y)
    ValueAxis {
        id: pressureAxis
        min: 0
        max: 12
        tickCount: 5
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        titleText: "bar / mL/s"
        titleBrush: Theme.textSecondaryColor
    }

    // Temperature axis (hidden right axis — range 40–100 °C, matches HistoryShotGraph)
    ValueAxis {
        id: tempAxis
        min: 40
        max: 100
        visible: false
    }

    // Weight axis (hidden — weight is scaled /5 onto pressureAxis)
    ValueAxis {
        id: weightAxis
        min: 0
        max: 12
        visible: false
    }

    // ── Shot 1: solid lines ──────────────────────────────────────────────────
    LineSeries { id: pressure1;    color: Theme.pressureColor;    width: Theme.graphLineWidth;                style: Qt.SolidLine;   axisX: timeAxis; axisY: pressureAxis;      visible: chart.showPressure    && chart.showShot0 }
    LineSeries { id: flow1;        color: Theme.flowColor;        width: Theme.graphLineWidth;                style: Qt.SolidLine;   axisX: timeAxis; axisY: pressureAxis;      visible: chart.showFlow        && chart.showShot0 }
    LineSeries { id: temp1;        color: Theme.temperatureColor; width: Theme.graphLineWidth;                style: Qt.SolidLine;   axisX: timeAxis; axisYRight: tempAxis;     visible: chart.showTemperature && chart.showShot0 }
    LineSeries { id: weight1;      color: Theme.weightColor;      width: Math.max(1, Theme.graphLineWidth-1); style: Qt.SolidLine;   axisX: timeAxis; axisY: weightAxis;        visible: chart.showWeight      && chart.showShot0 }
    LineSeries { id: weightFlow1;  color: Theme.weightFlowColor;  width: Math.max(1, Theme.graphLineWidth-1); style: Qt.SolidLine;   axisX: timeAxis; axisY: pressureAxis;      visible: chart.showWeightFlow  && chart.showShot0 }
    LineSeries { id: resistance1;  color: Theme.resistanceColor;  width: Math.max(1, Theme.graphLineWidth-1); style: Qt.SolidLine;   axisX: timeAxis; axisY: pressureAxis;      visible: chart.showResistance  && chart.showShot0 }

    // ── Shot 2: dashed lines ─────────────────────────────────────────────────
    LineSeries { id: pressure2;    color: Theme.pressureColor;    width: Theme.graphLineWidth;                style: Qt.DashLine;    axisX: timeAxis; axisY: pressureAxis;      visible: chart.showPressure    && chart.showShot1 }
    LineSeries { id: flow2;        color: Theme.flowColor;        width: Theme.graphLineWidth;                style: Qt.DashLine;    axisX: timeAxis; axisY: pressureAxis;      visible: chart.showFlow        && chart.showShot1 }
    LineSeries { id: temp2;        color: Theme.temperatureColor; width: Theme.graphLineWidth;                style: Qt.DashLine;    axisX: timeAxis; axisYRight: tempAxis;     visible: chart.showTemperature && chart.showShot1 }
    LineSeries { id: weight2;      color: Theme.weightColor;      width: Math.max(1, Theme.graphLineWidth-1); style: Qt.DashLine;    axisX: timeAxis; axisY: weightAxis;        visible: chart.showWeight      && chart.showShot1 }
    LineSeries { id: weightFlow2;  color: Theme.weightFlowColor;  width: Math.max(1, Theme.graphLineWidth-1); style: Qt.DashLine;    axisX: timeAxis; axisY: pressureAxis;      visible: chart.showWeightFlow  && chart.showShot1 }
    LineSeries { id: resistance2;  color: Theme.resistanceColor;  width: Math.max(1, Theme.graphLineWidth-1); style: Qt.DashLine;    axisX: timeAxis; axisY: pressureAxis;      visible: chart.showResistance  && chart.showShot1 }

    // ── Shot 3: dash-dot lines ───────────────────────────────────────────────
    LineSeries { id: pressure3;    color: Theme.pressureColor;    width: Theme.graphLineWidth;                style: Qt.DashDotLine; axisX: timeAxis; axisY: pressureAxis;      visible: chart.showPressure    && chart.showShot2 }
    LineSeries { id: flow3;        color: Theme.flowColor;        width: Theme.graphLineWidth;                style: Qt.DashDotLine; axisX: timeAxis; axisY: pressureAxis;      visible: chart.showFlow        && chart.showShot2 }
    LineSeries { id: temp3;        color: Theme.temperatureColor; width: Theme.graphLineWidth;                style: Qt.DashDotLine; axisX: timeAxis; axisYRight: tempAxis;     visible: chart.showTemperature && chart.showShot2 }
    LineSeries { id: weight3;      color: Theme.weightColor;      width: Math.max(1, Theme.graphLineWidth-1); style: Qt.DashDotLine; axisX: timeAxis; axisY: weightAxis;        visible: chart.showWeight      && chart.showShot2 }
    LineSeries { id: weightFlow3;  color: Theme.weightFlowColor;  width: Math.max(1, Theme.graphLineWidth-1); style: Qt.DashDotLine; axisX: timeAxis; axisY: pressureAxis;      visible: chart.showWeightFlow  && chart.showShot2 }
    LineSeries { id: resistance3;  color: Theme.resistanceColor;  width: Math.max(1, Theme.graphLineWidth-1); style: Qt.DashDotLine; axisX: timeAxis; axisY: pressureAxis;      visible: chart.showResistance  && chart.showShot2 }

    // Phase marker lines — Canvas for proper dashed patterns per shot style
    Canvas {
        id: phaseCanvas
        x: chart.plotArea.x
        y: chart.plotArea.y
        width: chart.plotArea.width
        height: chart.plotArea.height
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
            x: chart.plotArea.x + (modelData.time / timeAxis.max) * chart.plotArea.width + Theme.scaled(2)
            y: chart.plotArea.y
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
        y: chart.plotArea.y
        width: Theme.scaled(1)
        height: chart.plotArea.height
        color: Theme.textColor
        opacity: 0.6
    }
}
