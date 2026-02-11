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

    onPressureDataChanged: { loadData(); loadMarkers() }
    onPhaseMarkersChanged: loadMarkers()
    Component.onCompleted: { loadData(); loadMarkers() }

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

    // Pressure/Flow axis (left Y)
    ValueAxis {
        id: pressureAxis
        min: 0
        max: 12
        tickCount: 5
        labelFormat: "%.0f"
        labelsVisible: chart.showLabels
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        titleText: chart.showLabels ? "bar / mL/s" : ""
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
        titleText: chart.showLabels ? "°C" : ""
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
        labelsVisible: chart.showLabels
        labelsColor: Theme.weightColor
        gridLineColor: "transparent"
        titleText: chart.showLabels ? "g" : ""
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

    // Phase marker vertical lines (up to 10 markers)
    LineSeries { id: markerLine1; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine2; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine3; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine4; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine5; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine6; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine7; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine8; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine9; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: markerLine10; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }

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
            _markerLines[m].append(t, 12)
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
                color: isEnd ? "#FF6B6B" : Qt.rgba(255, 255, 255, 0.8)
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
}
