import QtQuick
import QtCharts
import Decenza

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false

    // Controls for compact/widget rendering
    property bool showLabels: true
    property bool showPhaseLabels: true

    // Persisted visibility toggles (tappable legend). Using Settings.boolValue()
    // coerces QSettings' INI-backed strings to real booleans; see Settings.h.
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

    // Which right-side axis labels to display (tap axis to swap)
    property bool showWeightAxis: Settings.boolValue("graph/showWeightAxis", true)

    function toggleRightAxis() {
        showWeightAxis = !showWeightAxis
        Settings.setValue("graph/showWeightAxis", showWeightAxis)
    }

    // Inspect state (crosshair + tooltip)
    property bool inspecting: false
    property real inspectTime: 0
    property real inspectPixelX: 0
    property var inspectValues: ({})

    margins.top: 0
    margins.bottom: 0
    margins.left: 0
    margins.right: chart.showLabels ? Theme.scaled(35) : 0

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
    property var phaseMarkers: []
    property double maxTime: 60

    // Load data into series
    function loadData() {
        pressureSeries.clear()
        flowSeries.clear()
        temperatureSeries.clear()
        weightSeries.clear()
        weightFlowRateSeries.clear()
        resistanceSeries.clear()

        for (var i = 0; i < pressureData.length; i++) {
            pressureSeries.append(pressureData[i].x, pressureData[i].y)
        }
        for (i = 0; i < flowData.length; i++) {
            flowSeries.append(flowData[i].x, flowData[i].y)
        }
        for (i = 0; i < temperatureData.length; i++) {
            temperatureSeries.append(temperatureData[i].x, temperatureData[i].y)
        }
        for (i = 0; i < weightData.length; i++) {
            weightSeries.append(weightData[i].x, weightData[i].y)
        }
        for (i = 0; i < weightFlowRateData.length; i++) {
            weightFlowRateSeries.append(weightFlowRateData[i].x, weightFlowRateData[i].y)
        }
        for (i = 0; i < resistanceData.length; i++) {
            resistanceSeries.append(resistanceData[i].x, resistanceData[i].y)
        }
        conductanceSeries.clear()
        for (i = 0; i < conductanceData.length; i++) {
            conductanceSeries.append(conductanceData[i].x, conductanceData[i].y)
        }
        darcyResistanceSeries.clear()
        for (i = 0; i < darcyResistanceData.length; i++) {
            darcyResistanceSeries.append(darcyResistanceData[i].x, darcyResistanceData[i].y)
        }
        conductanceDerivativeSeries.clear()
        for (i = 0; i < conductanceDerivativeData.length; i++) {
            conductanceDerivativeSeries.append(conductanceDerivativeData[i].x, conductanceDerivativeData[i].y)
        }
        temperatureMixSeries.clear()
        for (i = 0; i < temperatureMixData.length; i++) {
            temperatureMixSeries.append(temperatureMixData[i].x, temperatureMixData[i].y)
        }

        // Update time axis
        if (pressureData.length > 0) {
            timeAxis.max = Math.max(5, maxTime + 2)
        }
    }

    // Use Qt.callLater to coalesce reloads when multiple properties change at once.
    // When parent reassigns shotData, QML re-evaluates each binding (pressureData,
    // flowData, weightData, etc.) sequentially — not atomically. Without callLater,
    // the first property change would trigger loadData() while other properties still
    // hold stale values from the previous shot, causing a mix of old/new data.
    // Qt.callLater deduplicates: N calls before execution → 1 actual invocation.
    function doReload() { dismissInspect(); loadData(); loadMarkers(); loadGoalData() }

    // Split a goal data array into segments at time gaps (pump mode transitions).
    // During recording, goal=0 samples are skipped, creating natural time gaps
    // between segments. Normal sample interval is ~0.2s; gaps > 0.5s indicate
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

    // Load goal/target lines into their LineSeries
    function loadGoalData() {
        // Clear all goal series
        for (var i = 0; i < _pressureGoalLines.length; i++)
            _pressureGoalLines[i].clear()
        for (i = 0; i < _flowGoalLines.length; i++)
            _flowGoalLines[i].clear()
        temperatureGoalSeries.clear()

        // Segment and load pressure goal
        var pSegs = segmentGoalData(pressureGoalData, 5)
        for (i = 0; i < pSegs.length && i < _pressureGoalLines.length; i++) {
            for (var j = 0; j < pSegs[i].length; j++)
                _pressureGoalLines[i].append(pSegs[i][j].x, pSegs[i][j].y)
        }

        // Segment and load flow goal
        var fSegs = segmentGoalData(flowGoalData, 5)
        for (i = 0; i < fSegs.length && i < _flowGoalLines.length; i++) {
            for (j = 0; j < fSegs[i].length; j++)
                _flowGoalLines[i].append(fSegs[i][j].x, fSegs[i][j].y)
        }

        // Temperature goal is continuous across all modes — no segmentation
        for (i = 0; i < temperatureGoalData.length; i++)
            temperatureGoalSeries.append(temperatureGoalData[i].x, temperatureGoalData[i].y)
    }

    // Inspect at a pixel position: show crosshair + tooltip, announce for TTS
    function inspectAtPosition(pixelX, pixelY) {
        var dataPoint = chart.mapToValue(Qt.point(pixelX, pixelY), pressureSeries)
        var time = dataPoint.x
        if (time < 0 || time > timeAxis.max) return

        inspectTime = time
        // Calculate pixel X from time (clamped to plot area)
        inspectPixelX = chart.plotArea.x + (time / timeAxis.max) * chart.plotArea.width

        // Compute values for every curve — regardless of visibility — so the
        // inspect bar can react live when the user toggles curves on/off without
        // re-tapping the graph. GraphInspectBar filters by the current show*
        // flags at display time.
        var vals = {}
        var curves = [
            { key: "pressure", name: "Pressure", series: pressureSeries, unit: "bar" },
            { key: "flow", name: "Flow", series: flowSeries, unit: "mL/s" },
            { key: "temperature", name: "Temp", series: temperatureSeries, unit: "\u00B0C" },
            { key: "mixTemp", name: "Mix temp", series: temperatureMixSeries, unit: "\u00B0C" },
            { key: "weight", name: "Weight", series: weightSeries, unit: "g" },
            { key: "weightFlow", name: "Weight flow", series: weightFlowRateSeries, unit: "g/s" },
            { key: "resistance", name: "Resistance", series: resistanceSeries, unit: "" },
            { key: "darcyResistance", name: "Darcy R", series: darcyResistanceSeries, unit: "" },
            { key: "conductance", name: "Conductance", series: conductanceSeries, unit: "" },
            { key: "dCdt", name: "dC/dt", series: conductanceDerivativeSeries, unit: "" }
        ]

        for (var i = 0; i < curves.length; i++) {
            var v = findValueAtTime(curves[i].series, time)
            if (v !== null) {
                vals[curves[i].key] = { name: curves[i].name, value: v, unit: curves[i].unit }
            }
        }

        inspectValues = vals
        inspecting = true
        announceAtPosition(pixelX, pixelY)
    }

    // Dismiss the inspect crosshair/tooltip
    function dismissInspect() {
        inspecting = false
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
                break  // sorted by x
            }
        }
        return minDist < 1.0 ? closest.y : null
    }

    // Announce curve values at a pixel position (called on tap)
    function announceAtPosition(pixelX, pixelY) {
        var dataPoint = chart.mapToValue(Qt.point(pixelX, pixelY), pressureSeries)
        var time = dataPoint.x
        if (time < 0 || time > timeAxis.max) return

        var curves = [
            { name: "Pressure", series: pressureSeries, show: showPressure },
            { name: "Flow", series: flowSeries, show: showFlow },
            { name: "Temp", series: temperatureSeries, show: showTemperature },
            { name: "Mix temp", series: temperatureMixSeries, show: showTemperatureMix && advancedMode },
            { name: "Weight", series: weightSeries, show: showWeight },
            { name: "Weight flow", series: weightFlowRateSeries, show: showWeightFlow },
            { name: "Resistance", series: resistanceSeries, show: showResistance && advancedMode },
            { name: "Darcy R", series: darcyResistanceSeries, show: showDarcyResistance && advancedMode },
            { name: "Conductance", series: conductanceSeries, show: showConductance && advancedMode },
            { name: "dC/dt", series: conductanceDerivativeSeries, show: showConductanceDerivative && advancedMode }
        ]

        var parts = []
        for (var i = 0; i < curves.length; i++) {
            if (!curves[i].show) continue
            var v = findValueAtTime(curves[i].series, time)
            if (v !== null) {
                parts.push(curves[i].name + " " + v.toFixed(1))
            }
        }

        if (parts.length === 0) return
        if (typeof AccessibilityManager !== "undefined") {
            AccessibilityManager.announce(time.toFixed(1) + ". " + parts.join(". "), true)
        }
    }

    onPressureDataChanged: Qt.callLater(doReload)
    onFlowDataChanged: Qt.callLater(doReload)
    onTemperatureDataChanged: Qt.callLater(doReload)
    onWeightDataChanged: Qt.callLater(doReload)
    onWeightFlowRateDataChanged: Qt.callLater(doReload)
    onResistanceDataChanged: Qt.callLater(doReload)
    onPressureGoalDataChanged: Qt.callLater(doReload)
    onFlowGoalDataChanged: Qt.callLater(doReload)
    onTemperatureGoalDataChanged: Qt.callLater(doReload)
    onPhaseMarkersChanged: Qt.callLater(doReload)
    Component.onCompleted: doReload()

    // Time axis
    ValueAxis {
        id: timeAxis
        min: 0
        max: 60
        tickCount: 7
        labelFormat: "%.0f"
        labelsVisible: chart.showLabels
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
    }

    // Dynamic max for pressure/flow axis based on all data (ignores visibility
    // toggles so the graph doesn't jump when toggling curves on/off)
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
        // Resistance excluded from axis scaling — values are clamped at source
        // and clip at the axis boundary, matching the live graph behavior
        if (maxVal < 0.1) return 12  // fallback when no data
        // Round up to nice tick-friendly value
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

    // Pressure/Flow axis (left Y)
    ValueAxis {
        id: pressureAxis
        min: 0
        max: pressureAxisMax
        tickCount: 5
        labelFormat: "%.0f"
        labelsVisible: chart.showLabels
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        titleText: chart.showLabels ? "bar / mL/s" : ""
        titleBrush: Theme.textSecondaryColor
    }

    // Two hidden right axes — used only for correct series mapping (no labels)
    property double maxWeight: {
        var max = 0
        for (var i = 0; i < weightData.length; i++) {
            if (weightData[i].y > max) max = weightData[i].y
        }
        return Math.max(10, max * 1.1)
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
        max: maxWeight
    }

    // === EXTRACTION START / STOP MARKERS (styled differently from frame markers) ===

    LineSeries {
        id: extractionStartMarker
        name: ""
        color: Theme.accentColor
        width: Theme.scaled(2)
        style: Qt.DashDotLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    LineSeries {
        id: stopMarker
        name: ""
        color: Theme.stopMarkerColor
        width: Theme.scaled(2)
        style: Qt.DashDotLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    // === GOAL LINES (dashed) — segments for clean breaks at pump mode transitions ===

    // Pressure goal segments (up to 5 for mode switches)
    LineSeries { id: pressureGoal1; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal2; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal3; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal4; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal5; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }

    property var _pressureGoalLines: [pressureGoal1, pressureGoal2, pressureGoal3, pressureGoal4, pressureGoal5]

    // Flow goal segments (up to 5 for mode switches)
    LineSeries { id: flowGoal1; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal2; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal3; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal4; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal5; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }

    property var _flowGoalLines: [flowGoal1, flowGoal2, flowGoal3, flowGoal4, flowGoal5]

    // Temperature goal (single line — continuous across all modes)
    LineSeries {
        id: temperatureGoalSeries
        name: ""
        color: Theme.temperatureGoalColor
        width: Theme.scaled(2)
        style: Qt.DashLine
        axisX: timeAxis
        axisYRight: tempAxis
        visible: chart.showTemperature
    }

    // === ACTUAL DATA LINES ===

    // Pressure line
    LineSeries {
        id: pressureSeries
        name: "Pressure"
        color: Theme.pressureColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showPressure
    }

    // Flow line
    LineSeries {
        id: flowSeries
        name: "Flow"
        color: Theme.flowColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showFlow
    }

    // Temperature line
    LineSeries {
        id: temperatureSeries
        name: "Temperature"
        color: Theme.temperatureColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisYRight: tempAxis
        visible: chart.showTemperature
    }

    // Weight line
    LineSeries {
        id: weightSeries
        name: "Weight"
        color: Theme.weightColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisYRight: weightAxis
        visible: chart.showWeight
    }

    // Weight flow rate (delta) line - shows g/s from scale
    LineSeries {
        id: weightFlowRateSeries
        name: "Weight Flow"
        color: Theme.weightFlowColor
        width: Theme.scaled(2)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showWeightFlow
    }

    // Puck resistance line (P/F)
    LineSeries {
        id: resistanceSeries
        name: "Resistance"
        color: Theme.resistanceColor
        width: Theme.scaled(2)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showResistance && chart.advancedMode
    }

    LineSeries {
        id: conductanceSeries
        name: "Conductance"
        color: Theme.conductanceColor
        width: Theme.scaled(2)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showConductance && chart.advancedMode
    }

    LineSeries {
        id: darcyResistanceSeries
        name: "Darcy Resistance"
        color: Theme.darcyResistanceColor
        width: Theme.scaled(2)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showDarcyResistance && chart.advancedMode
    }

    LineSeries {
        id: conductanceDerivativeSeries
        name: "dC/dt"
        color: Theme.conductanceDerivativeColor
        width: Theme.scaled(2)
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showConductanceDerivative && chart.advancedMode
    }

    LineSeries {
        id: temperatureMixSeries
        name: "Mix Temp"
        color: Theme.temperatureMixColor
        width: Theme.scaled(2)
        axisX: timeAxis
        axisYRight: tempAxis
        visible: chart.showTemperatureMix && chart.advancedMode
    }

    // Phase marker vertical lines (up to 10 markers)
    LineSeries { id: markerLine1; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine2; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine3; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine4; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine5; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine6; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine7; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine8; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine9; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine10; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }

    property var _markerLines: [markerLine1, markerLine2, markerLine3, markerLine4, markerLine5,
                                markerLine6, markerLine7, markerLine8, markerLine9, markerLine10]

    function loadMarkers() {
        // Clear all marker lines
        for (var i = 0; i < _markerLines.length; i++) {
            _markerLines[i].clear()
        }
        extractionStartMarker.clear()
        stopMarker.clear()

        // Draw vertical lines for each phase marker
        var markerIdx = 0
        for (var m = 0; m < phaseMarkers.length; m++) {
            var marker = phaseMarkers[m]
            var t = marker.time
            if (marker.label === "Start") {
                extractionStartMarker.append(t, 0)
                extractionStartMarker.append(t, 100)
            } else if (marker.label === "End") {
                stopMarker.append(t, 0)
                stopMarker.append(t, 100)
            } else if (markerIdx < _markerLines.length) {
                _markerLines[markerIdx].append(t, 0)
                _markerLines[markerIdx].append(t, 100)
                markerIdx++
            }
        }
    }

    // Phase marker labels
    Repeater {
        id: markerLabels
        model: phaseMarkers

        delegate: Item {
            id: markerDelegate
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property string markerLabel: modelData.label
            property string transitionReason: modelData.transitionReason || ""
            property bool isStart: modelData.label === "Start"
            property bool isEnd: modelData.label === "End"

            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y
            height: chart.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0 && chart.showPhaseLabels

            Text {
                text: {
                    if (transitionReason === "" || isEnd) return markerLabel
                    var suffix = ""
                    switch (transitionReason) {
                        case "weight": suffix = " [W]"; break
                        case "pressure": suffix = " [P]"; break
                        case "flow": suffix = " [F]"; break
                        case "time": suffix = " [T]"; break
                    }
                    return markerLabel + suffix
                }
                font.pixelSize: Theme.scaled(14)
                font.bold: isStart || isEnd
                color: isStart ? Theme.accentColor : (isEnd ? Theme.stopMarkerColor : Qt.rgba(255, 255, 255, 0.8))
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
        model: phaseMarkers

        delegate: Rectangle {
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property bool isFlowMode: modelData.isFlowMode || false
            property double nextTime: {
                if (index < phaseMarkers.length - 1) {
                    return phaseMarkers[index + 1].time
                }
                return maxTime
            }

            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y + chart.plotArea.height - Theme.scaled(4)
            width: Math.max(0, ((nextTime - markerTime) / timeAxis.max) * chart.plotArea.width)
            height: Theme.scaled(4)
            color: isFlowMode ? Theme.flowColor : Theme.pressureColor
            opacity: 0.8
            visible: markerTime <= timeAxis.max && modelData.label !== "Start" && modelData.label !== "End"
            Accessible.ignored: true
        }
    }

    // Time axis label - inside graph at bottom right
    Text {
        x: chart.plotArea.x + chart.plotArea.width - width - Theme.spacingSmall
        y: chart.plotArea.y + chart.plotArea.height - height - Theme.scaled(12)
        text: TranslationManager.translate("graph.timeAxis", "Time (s)")
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        opacity: 0.7
        Accessible.ignored: true
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
        Accessible.ignored: true
    }

    // Manual right-axis labels (fixed position — no layout shift when swapping)
    Item {
        id: rightAxisLabels
        visible: chart.showLabels
        x: chart.plotArea.x + chart.plotArea.width + Theme.scaled(4)
        y: chart.plotArea.y
        width: chart.width - x
        height: chart.plotArea.height

        Accessible.role: Accessible.Button
        Accessible.name: chart.showWeightAxis ? TranslationManager.translate("graph.rightAxisWeight", "Right axis: Weight. Tap for Temperature")
                                              : TranslationManager.translate("graph.rightAxisTemp", "Right axis: Temperature. Tap for Weight")
        Accessible.focusable: true
        Accessible.onPressAction: chart.toggleRightAxis()

        property color labelColor: chart.showWeightAxis ? Theme.weightColor : Theme.temperatureColor

        // Tick labels
        Repeater {
            model: 5
            Text {
                required property int index
                property real value: {
                    var axisMin = chart.showWeightAxis ? weightAxis.min : tempAxis.min
                    var axisMax = chart.showWeightAxis ? weightAxis.max : tempAxis.max
                    return axisMax - index * (axisMax - axisMin) / 4
                }
                text: value.toFixed(0)
                x: 0
                y: index / 4 * rightAxisLabels.height - height / 2
                font: Theme.captionFont
                color: rightAxisLabels.labelColor
                Accessible.ignored: true
            }
        }

        // Axis title (rotated, centered vertically — mirrors the left axis title)
        Text {
            text: chart.showWeightAxis ? "g" : "°C"
            font: Theme.captionFont
            color: rightAxisLabels.labelColor
            rotation: 90
            transformOrigin: Item.Center
            x: Theme.scaled(24)
            y: rightAxisLabels.height / 2 - height / 2
            Accessible.ignored: true
        }
    }

}
