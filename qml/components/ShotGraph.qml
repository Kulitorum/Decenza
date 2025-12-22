import QtQuick
import QtCharts
import DE1App

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.rgba(0, 0, 0, 0.3)
    legend.visible: true
    legend.labelColor: Theme.textSecondaryColor
    legend.alignment: Qt.AlignBottom

    margins.top: 10
    margins.bottom: 10
    margins.left: 10
    margins.right: 10

    // Time axis (X)
    ValueAxis {
        id: timeAxis
        min: 0
        max: ShotDataModel.maxTime
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
        labelsColor: Theme.pressureColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        titleText: "bar / mL/s"
        titleBrush: Theme.textSecondaryColor
    }

    // Temperature axis (right Y) - hidden but used for scaling
    ValueAxis {
        id: tempAxis
        min: 80
        max: 100
        visible: false
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
        name: "Temp"
        color: Theme.temperatureColor
        width: Theme.graphLineWidth
        axisX: timeAxis
        axisYRight: tempAxis
        visible: false  // Hidden by default to reduce clutter
    }

    // Weight line (if scale connected)
    LineSeries {
        id: weightSeries
        name: "Weight"
        color: Theme.weightColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
        visible: false  // TODO: Show when scale connected
    }

    // Update data from model
    Connections {
        target: ShotDataModel

        function onDataChanged() {
            // Update pressure series
            pressureSeries.clear()
            var pData = ShotDataModel.pressureData
            for (var i = 0; i < pData.length; i++) {
                pressureSeries.append(pData[i].x, pData[i].y)
            }

            // Update flow series
            flowSeries.clear()
            var fData = ShotDataModel.flowData
            for (var j = 0; j < fData.length; j++) {
                flowSeries.append(fData[j].x, fData[j].y)
            }

            // Update temperature series
            temperatureSeries.clear()
            var tData = ShotDataModel.temperatureData
            for (var k = 0; k < tData.length; k++) {
                temperatureSeries.append(tData[k].x, tData[k].y)
            }

            // Update axes max values
            if (ShotDataModel.maxTime > timeAxis.max - 10) {
                timeAxis.max = ShotDataModel.maxTime + 10
            }
        }
    }
}
