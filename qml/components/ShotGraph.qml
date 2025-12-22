import QtQuick
import QtCharts
import DE1App

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.rgba(0, 0, 0, 0.3)
    legend.visible: false  // Using custom legend below

    margins.top: 10
    margins.bottom: 60  // Extra space for custom legend
    margins.left: 10
    margins.right: 10

    // Auto-zoom: start at 5 seconds, smoothly expand as data fills
    property double minVisibleTime: 5.0
    property double targetMaxTime: minVisibleTime
    property double currentMaxTime: minVisibleTime

    // Smooth zoom animation
    Behavior on currentMaxTime {
        NumberAnimation {
            duration: 300
            easing.type: Easing.OutQuad
        }
    }

    // Time axis (X) - auto-zooming
    ValueAxis {
        id: timeAxis
        min: 0
        max: currentMaxTime
        tickCount: Math.min(7, Math.max(3, Math.floor(currentMaxTime / 10) + 2))
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

    // Temperature axis (right Y)
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
    }

    // === PHASE MARKER LINES (vertical) ===

    // Extraction start marker (special - green vertical line)
    LineSeries {
        id: extractionStartMarker
        name: ""  // Hide from legend
        color: Theme.accentColor
        width: 2
        style: Qt.DashDotLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Frame change markers (rendered as vertical lines, hidden from legend)
    // Support up to 10 frames (DE1 supports up to 20, but 10 is typical max)
    LineSeries { id: frameMarker1; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker2; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker3; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker4; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker5; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker6; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker7; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker8; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker9; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker10; name: ""; color: Qt.rgba(255,255,255,0.4); width: 1; style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }

    // === GOAL LINES (dashed, behind actual) ===

    // Pressure goal line (dashed)
    LineSeries {
        id: pressureGoalSeries
        name: "P Goal"
        color: Theme.pressureGoalColor
        width: 2
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Flow goal line (dashed) - only shown when goal > 0 (flow-control mode)
    LineSeries {
        id: flowGoalSeries
        name: "F Goal"
        color: Theme.flowGoalColor
        width: 2
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Temperature goal line (dashed)
    LineSeries {
        id: temperatureGoalSeries
        name: "T Goal"
        color: Theme.temperatureGoalColor
        width: 2
        style: Qt.DashLine
        axisX: timeAxis
        axisYRight: tempAxis
    }

    // === ACTUAL LINES (solid, on top) ===

    // Pressure line
    LineSeries {
        id: pressureSeries
        name: "Pressure"
        color: Theme.pressureColor
        width: 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Flow line
    LineSeries {
        id: flowSeries
        name: "Flow"
        color: Theme.flowColor
        width: 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Temperature line
    LineSeries {
        id: temperatureSeries
        name: "Temp"
        color: Theme.temperatureColor
        width: 3
        axisX: timeAxis
        axisYRight: tempAxis
    }

    // Weight line (from scale)
    LineSeries {
        id: weightSeries
        name: "Weight"
        color: Theme.weightColor
        width: 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Frame marker labels overlay - vertical text along the marker line
    Repeater {
        id: markerLabels
        model: ShotDataModel.phaseMarkers

        delegate: Item {
            // Position the label at the marker time
            property double markerTime: modelData.time
            property string markerLabel: modelData.label
            property bool isStart: modelData.label === "Start"

            // Calculate x position based on chart geometry
            x: chart.plotArea.x + (markerTime / currentMaxTime) * chart.plotArea.width
            y: chart.plotArea.y
            height: chart.plotArea.height
            visible: markerTime <= currentMaxTime && markerTime >= 0

            // Vertical text (rotated 90 degrees), positioned to the right of the line
            Text {
                id: rotatedText
                text: markerLabel
                font.pixelSize: 18
                font.bold: isStart
                color: isStart ? Theme.accentColor : Qt.rgba(255, 255, 255, 0.8)
                rotation: -90
                transformOrigin: Item.TopLeft
                x: 4  // Offset to the right of the marker line
                y: 8 + width  // Top margin + text width (due to rotation)

                // Background for readability
                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: -2
                    color: Qt.rgba(0, 0, 0, 0.6)
                    radius: 2
                }
            }
        }
    }

    // Update data from model
    Connections {
        target: ShotDataModel

        function onDataChanged() {
            // Update pressure series
            pressureSeries.clear()
            var pData = ShotDataModel.pressureData
            var maxDataTime = 0
            for (var i = 0; i < pData.length; i++) {
                pressureSeries.append(pData[i].x, pData[i].y)
                if (pData[i].x > maxDataTime) maxDataTime = pData[i].x
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

            // Update pressure goal series - only include non-zero values (0 means flow-control mode)
            pressureGoalSeries.clear()
            var pgData = ShotDataModel.pressureGoalData
            for (var pi = 0; pi < pgData.length; pi++) {
                if (pgData[pi].y > 0) {
                    pressureGoalSeries.append(pgData[pi].x, pgData[pi].y)
                }
            }

            // Update flow goal series - only include non-zero values (0 means pressure-control mode)
            flowGoalSeries.clear()
            var fgData = ShotDataModel.flowGoalData
            for (var fi = 0; fi < fgData.length; fi++) {
                if (fgData[fi].y > 0) {
                    flowGoalSeries.append(fgData[fi].x, fgData[fi].y)
                }
            }

            // Update temperature goal series
            temperatureGoalSeries.clear()
            var tgData = ShotDataModel.temperatureGoalData
            for (var ti = 0; ti < tgData.length; ti++) {
                temperatureGoalSeries.append(tgData[ti].x, tgData[ti].y)
            }

            // Update weight series
            weightSeries.clear()
            var wData = ShotDataModel.weightData
            for (var wi = 0; wi < wData.length; wi++) {
                // Scale weight to fit on pressure axis (divide by 5 to get ~0-10 range)
                weightSeries.append(wData[wi].x, wData[wi].y / 5.0)
            }

            // Update extraction start marker (vertical line)
            extractionStartMarker.clear()
            var extractStart = ShotDataModel.extractionStartTime
            if (extractStart >= 0) {
                extractionStartMarker.append(extractStart, 0)
                extractionStartMarker.append(extractStart, 12)
            }

            // Update frame markers (vertical lines)
            var markers = ShotDataModel.phaseMarkers
            var markerSeries = [frameMarker1, frameMarker2, frameMarker3, frameMarker4, frameMarker5,
                                 frameMarker6, frameMarker7, frameMarker8, frameMarker9, frameMarker10]

            // Clear all marker series
            for (var ms = 0; ms < markerSeries.length; ms++) {
                markerSeries[ms].clear()
            }

            // Draw frame change markers (skip the first "Start" marker as it has its own line)
            var frameMarkerIndex = 0
            for (var mi = 0; mi < markers.length && frameMarkerIndex < markerSeries.length; mi++) {
                if (markers[mi].label !== "Start") {
                    markerSeries[frameMarkerIndex].append(markers[mi].time, 0)
                    markerSeries[frameMarkerIndex].append(markers[mi].time, 12)
                    frameMarkerIndex++
                }
            }

            // Smooth auto-zoom: keep data at ~80% of visible width
            if (pData.length === 0) {
                // Reset zoom when cleared
                currentMaxTime = minVisibleTime
            } else if (maxDataTime > currentMaxTime * 0.80) {
                // Smoothly expand to give 25% headroom beyond current data
                currentMaxTime = Math.max(maxDataTime * 1.25, currentMaxTime + 1)
            }
        }
    }

    // Custom legend at bottom
    Column {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 8
        spacing: 4

        // Legend items row
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 24

            // Pressure
            Row {
                spacing: 6
                Rectangle {
                    width: 24
                    height: 4
                    radius: 2
                    color: Theme.pressureColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Pressure"
                    color: Theme.textSecondaryColor
                    font.pixelSize: 13
                }
            }

            // Flow
            Row {
                spacing: 6
                Rectangle {
                    width: 24
                    height: 4
                    radius: 2
                    color: Theme.flowColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Flow"
                    color: Theme.textSecondaryColor
                    font.pixelSize: 13
                }
            }

            // Temperature
            Row {
                spacing: 6
                Rectangle {
                    width: 24
                    height: 4
                    radius: 2
                    color: Theme.temperatureColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Temp"
                    color: Theme.textSecondaryColor
                    font.pixelSize: 13
                }
            }

            // Weight
            Row {
                spacing: 6
                Rectangle {
                    width: 24
                    height: 4
                    radius: 2
                    color: Theme.weightColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Weight"
                    color: Theme.textSecondaryColor
                    font.pixelSize: 13
                }
            }
        }

        // Explanation text
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Solid = actual  · · · Dashed = target"
            color: Qt.rgba(255, 255, 255, 0.5)
            font.pixelSize: 11
        }
    }
}
