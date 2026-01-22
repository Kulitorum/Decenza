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

    // Data to display (set from parent)
    property var pressureData: []
    property var flowData: []
    property var temperatureData: []
    property var weightData: []
    property var phaseMarkers: []
    property double maxTime: 60

    // Load data into series
    function loadData() {
        pressureSeries.clear()
        flowSeries.clear()
        temperatureSeries.clear()
        weightSeries.clear()

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

        // Update time axis
        if (pressureData.length > 0) {
            timeAxis.max = Math.max(5, maxTime + 2)
        }
    }

    onPressureDataChanged: loadData()
    Component.onCompleted: loadData()

    // Time axis
    ValueAxis {
        id: timeAxis
        min: 0
        max: 60
        tickCount: 7
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
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

    // Temperature axis (right Y) - hidden to make room for weight
    ValueAxis {
        id: tempAxis
        min: 80
        max: 100
        tickCount: 5
        labelFormat: "%.0f"
        labelsColor: Theme.temperatureColor
        gridLineColor: "transparent"
        titleText: "°C"
        titleBrush: Theme.temperatureColor
        visible: false
    }

    // Weight axis (right Y) - scaled to max weight in data + 10%
    property double maxWeight: {
        var max = 0
        for (var i = 0; i < weightData.length; i++) {
            if (weightData[i].y > max) max = weightData[i].y
        }
        return Math.max(10, max * 1.1)
    }

    ValueAxis {
        id: weightAxis
        min: 0
        max: maxWeight
        tickCount: 5
        labelFormat: "%.0f"
        labelsColor: Theme.weightColor
        gridLineColor: "transparent"
        titleText: "g"
        titleBrush: Theme.weightColor
    }

    // Pressure line
    LineSeries {
        id: pressureSeries
        name: "Pressure"
        color: Theme.pressureColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Flow line
    LineSeries {
        id: flowSeries
        name: "Flow"
        color: Theme.flowColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Temperature line
    LineSeries {
        id: temperatureSeries
        name: "Temperature"
        color: Theme.temperatureColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisYRight: tempAxis
    }

    // Weight line
    LineSeries {
        id: weightSeries
        name: "Weight"
        color: Theme.weightColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisYRight: weightAxis
    }
}
