import QtQuick
import QtCharts
import DecenzaDE1

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false

    // Controls for compact/widget rendering
    property bool showLabels: true
    property bool showPhaseLabels: true

    // Persisted visibility toggles (tappable legend)
    property bool showPressure: Settings.value("graph/showPressure", true)
    property bool showFlow: Settings.value("graph/showFlow", true)
    property bool showTemperature: Settings.value("graph/showTemperature", true)
    property bool showWeight: Settings.value("graph/showWeight", true)
    property bool showWeightFlow: Settings.value("graph/showWeightFlow", true)
    property bool showResistance: Settings.value("graph/showResistance", false)

    // Which right-side axis labels to display (tap axis to swap)
    property bool showWeightAxis: Settings.value("graph/showWeightAxis", true)

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
    function doReload() { dismissInspect(); loadData(); loadMarkers() }

    // Inspect at a pixel position: show crosshair + tooltip, announce for TTS
    function inspectAtPosition(pixelX, pixelY) {
        var dataPoint = chart.mapToValue(Qt.point(pixelX, pixelY), pressureSeries)
        var time = dataPoint.x
        if (time < 0 || time > timeAxis.max) return

        inspectTime = time
        // Calculate pixel X from time (clamped to plot area)
        inspectPixelX = chart.plotArea.x + (time / timeAxis.max) * chart.plotArea.width

        var vals = {}
        var curves = [
            { key: "pressure", name: "Pressure", series: pressureSeries, unit: "bar", show: showPressure },
            { key: "flow", name: "Flow", series: flowSeries, unit: "mL/s", show: showFlow },
            { key: "temperature", name: "Temp", series: temperatureSeries, unit: "\u00B0C", show: showTemperature },
            { key: "weight", name: "Weight", series: weightSeries, unit: "g", show: showWeight },
            { key: "weightFlow", name: "Weight flow", series: weightFlowRateSeries, unit: "g/s", show: showWeightFlow },
            { key: "resistance", name: "Resistance", series: resistanceSeries, unit: "", show: showResistance }
        ]

        for (var i = 0; i < curves.length; i++) {
            if (!curves[i].show) continue
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
            { name: "Weight flow", series: weightFlowRateSeries, show: showWeightFlow },
            { name: "Weight", series: weightSeries, show: showWeight },
            { name: "Temp", series: temperatureSeries, show: showTemperature },
            { name: "Resistance", series: resistanceSeries, show: showResistance }
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

    // Pressure line
    LineSeries {
        id: pressureSeries
        name: "Pressure"
        color: Theme.pressureColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showPressure
    }

    // Flow line
    LineSeries {
        id: flowSeries
        name: "Flow"
        color: Theme.flowColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showFlow
    }

    // Temperature line
    LineSeries {
        id: temperatureSeries
        name: "Temperature"
        color: Theme.temperatureColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisYRight: tempAxis
        visible: chart.showTemperature
    }

    // Weight line
    LineSeries {
        id: weightSeries
        name: "Weight"
        color: Theme.weightColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisYRight: weightAxis
        visible: chart.showWeight
    }

    // Weight flow rate (delta) line - shows g/s from scale
    LineSeries {
        id: weightFlowRateSeries
        name: "Weight Flow"
        color: Theme.weightFlowColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showWeightFlow
    }

    // Puck resistance line (P/F)
    LineSeries {
        id: resistanceSeries
        name: "Resistance"
        color: Theme.resistanceColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showResistance
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

        // Draw vertical lines for each phase marker
        for (var m = 0; m < phaseMarkers.length && m < _markerLines.length; m++) {
            var marker = phaseMarkers[m]
            var t = marker.time
            if (marker.label === "Start" || marker.label === "End") continue
            _markerLines[m].append(t, 0)
            _markerLines[m].append(t, 100)  // large value; clipped to axis range
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
            visible: markerTime <= timeAxis.max && markerTime >= 0 && !isStart && chart.showPhaseLabels

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
                font.bold: isEnd
                color: isEnd ? Theme.stopMarkerColor : Qt.rgba(255, 255, 255, 0.8)
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
        Accessible.name: chart.showWeightAxis ? "Right axis: Weight. Tap to show Temperature"
                                              : "Right axis: Temperature. Tap to show Weight"
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
