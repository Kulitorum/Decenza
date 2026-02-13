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

    // Visibility toggles for curve types
    property bool showPressure: true
    property bool showFlow: true
    property bool showWeight: true

    // Colors for each shot (primary, flow/light, weight/lighter)
    readonly property var shotColorSets: [
        { primary: "#4CAF50", flow: "#81C784", weight: "#A5D6A7" },  // Green
        { primary: "#2196F3", flow: "#64B5F6", weight: "#90CAF9" },  // Blue
        { primary: "#FF9800", flow: "#FFB74D", weight: "#FFCC80" }   // Orange
    ]

    // Load data from comparison model
    function loadData() {
        if (!comparisonModel) return

        // Clear all series
        pressure1.clear(); flow1.clear(); weight1.clear()
        pressure2.clear(); flow2.clear(); weight2.clear()
        pressure3.clear(); flow3.clear(); weight3.clear()

        var windowStart = comparisonModel.windowStart

        // Load each shot's data with colors based on global position
        for (var i = 0; i < comparisonModel.shotCount; i++) {
            var pressureData = comparisonModel.getPressureData(i)
            var flowData = comparisonModel.getFlowData(i)
            var weightData = comparisonModel.getWeightData(i)

            var pSeries = i === 0 ? pressure1 : (i === 1 ? pressure2 : pressure3)
            var fSeries = i === 0 ? flow1 : (i === 1 ? flow2 : flow3)
            var wSeries = i === 0 ? weight1 : (i === 1 ? weight2 : weight3)

            // Set colors based on global position (windowStart + i) % 3
            var colorIndex = (windowStart + i) % 3
            var colors = shotColorSets[colorIndex]
            pSeries.color = colors.primary
            fSeries.color = colors.flow
            wSeries.color = colors.weight

            for (var j = 0; j < pressureData.length; j++) {
                pSeries.append(pressureData[j].x, pressureData[j].y)
            }
            for (j = 0; j < flowData.length; j++) {
                fSeries.append(flowData[j].x, flowData[j].y)
            }
            for (j = 0; j < weightData.length; j++) {
                wSeries.append(weightData[j].x, weightData[j].y / 5)  // Scale for display
            }
        }

        // Update axes - fit to data with small padding (minimum 15s for very short shots)
        timeAxis.max = Math.max(15, comparisonModel.maxTime + 0.5)
    }

    onComparisonModelChanged: loadData()
    Component.onCompleted: loadData()

    // Connect to model changes
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

    // Pressure/Flow axis (left Y)
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

    // Weight axis (hidden)
    ValueAxis {
        id: weightAxis
        min: 0
        max: 12
        visible: false
    }

    // Shot 1 series (Green)
    LineSeries {
        id: pressure1
        name: "Pressure 1"
        color: "#4CAF50"
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showPressure
    }
    LineSeries {
        id: flow1
        name: "Flow 1"
        color: "#81C784"
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showFlow
    }
    LineSeries {
        id: weight1
        name: "Weight 1"
        color: "#A5D6A7"
        width: Math.max(1, Theme.graphLineWidth - 1)
        style: Qt.DotLine
        axisX: timeAxis
        axisY: weightAxis
        visible: chart.showWeight
    }

    // Shot 2 series (Blue)
    LineSeries {
        id: pressure2
        name: "Pressure 2"
        color: "#2196F3"
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showPressure
    }
    LineSeries {
        id: flow2
        name: "Flow 2"
        color: "#64B5F6"
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showFlow
    }
    LineSeries {
        id: weight2
        name: "Weight 2"
        color: "#90CAF9"
        width: Math.max(1, Theme.graphLineWidth - 1)
        style: Qt.DotLine
        axisX: timeAxis
        axisY: weightAxis
        visible: chart.showWeight
    }

    // Shot 3 series (Orange)
    LineSeries {
        id: pressure3
        name: "Pressure 3"
        color: "#FF9800"
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showPressure
    }
    LineSeries {
        id: flow3
        name: "Flow 3"
        color: "#FFB74D"
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
        visible: chart.showFlow
    }
    LineSeries {
        id: weight3
        name: "Weight 3"
        color: "#FFCC80"
        width: Math.max(1, Theme.graphLineWidth - 1)
        style: Qt.DotLine
        axisX: timeAxis
        axisY: weightAxis
        visible: chart.showWeight
    }
}
