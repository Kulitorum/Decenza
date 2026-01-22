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

    // Register series with C++ model on completion
    Component.onCompleted: {
        ShotDataModel.registerSeries(
            pressureSeries, flowSeries, temperatureSeries,
            [pressureGoal1, pressureGoal2, pressureGoal3, pressureGoal4, pressureGoal5],
            [flowGoal1, flowGoal2, flowGoal3, flowGoal4, flowGoal5],
            temperatureGoalSeries,
            weightSeries, extractionStartMarker,
            [frameMarker1, frameMarker2, frameMarker3, frameMarker4, frameMarker5,
             frameMarker6, frameMarker7, frameMarker8, frameMarker9, frameMarker10]
        )
    }

    // Calculate axis max: data fills frame with exactly 5 scaled pixels padding at right
    // Solve: max = rawTime + paddingPixels * (max / plotWidth)
    // => max = rawTime * plotWidth / (plotWidth - paddingPixels)
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double plotWidth: Math.max(1, chart.plotArea.width)
    property double calculatedMax: ShotDataModel.rawTime * plotWidth / Math.max(1, plotWidth - paddingPixels)

    // Time axis - fills frame, expands only when data pushes against right edge
    ValueAxis {
        id: timeAxis
        min: 0
        // Use calculated max (with 5px padding) or minimum 5 seconds
        max: Math.max(minTime, calculatedMax)
        tickCount: Math.min(7, Math.max(3, Math.floor(max / 10) + 2))
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
        // Title moved inside graph to save vertical space

        Behavior on max {
            NumberAnimation { duration: 100; easing.type: Easing.Linear }
        }
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

    // Temperature axis (right Y) - hidden during shots to make room for weight
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
        visible: false  // Hide temp axis - weight is more important during shots
    }

    // Weight axis (right Y) - scaled to target weight + 10%
    ValueAxis {
        id: weightAxis
        min: 0
        max: Math.max(10, (MainController.targetWeight || 36) * 1.1)
        tickCount: 5
        labelFormat: "%.0f"
        labelsColor: Theme.weightColor
        gridLineColor: "transparent"
        titleText: "g"
        titleBrush: Theme.weightColor
    }

    // === PHASE MARKER LINES ===

    LineSeries {
        id: extractionStartMarker
        name: ""
        color: Theme.accentColor
        width: Theme.scaled(2)
        style: Qt.DashDotLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    LineSeries { id: frameMarker1; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker2; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker3; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker4; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker5; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker6; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker7; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker8; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker9; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker10; name: ""; color: Qt.rgba(255,255,255,0.4); width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }

    // === GOAL LINES (dashed) - Multiple segments for clean breaks ===

    // Pressure goal segments (up to 5 segments for mode switches)
    LineSeries { id: pressureGoal1; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: pressureGoal2; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: pressureGoal3; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: pressureGoal4; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: pressureGoal5; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }

    // Flow goal segments (up to 5 segments for mode switches)
    LineSeries { id: flowGoal1; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: flowGoal2; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: flowGoal3; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: flowGoal4; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: flowGoal5; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis }

    LineSeries {
        id: temperatureGoalSeries
        name: "T Goal"
        color: Theme.temperatureGoalColor
        width: Theme.scaled(2)
        style: Qt.DashLine
        axisX: timeAxis
        axisYRight: tempAxis
    }

    // === ACTUAL LINES (solid) ===

    LineSeries {
        id: pressureSeries
        name: "Pressure"
        color: Theme.pressureColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisY: pressureAxis
    }

    LineSeries {
        id: flowSeries
        name: "Flow"
        color: Theme.flowColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisY: pressureAxis
    }

    LineSeries {
        id: temperatureSeries
        name: "Temp"
        color: Theme.temperatureColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisYRight: tempAxis
    }

    LineSeries {
        id: weightSeries
        name: "Weight"
        color: Theme.weightColor
        width: Theme.scaled(3)
        axisX: timeAxis
        axisYRight: weightAxis
    }

    // Frame marker labels
    Repeater {
        id: markerLabels
        model: ShotDataModel.phaseMarkers

        delegate: Item {
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property string markerLabel: modelData.label
            property bool isStart: modelData.label === "Start"

            // Calculate position using timeAxis.max for consistent scaling with smooth scroll
            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y
            height: chart.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0

            Text {
                text: markerLabel
                font.pixelSize: Theme.scaled(18)
                font.bold: isStart
                color: isStart ? Theme.accentColor : Qt.rgba(255, 255, 255, 0.8)
                rotation: -90
                transformOrigin: Item.TopLeft
                x: Theme.scaled(4)
                y: Theme.scaled(8) + width

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
        model: ShotDataModel.phaseMarkers

        delegate: Rectangle {
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property bool isFlowMode: modelData.isFlowMode || false
            // Next marker time (or current rawTime if last marker, capped at visible area)
            property double nextTime: {
                var markers = ShotDataModel.phaseMarkers
                if (index < markers.length - 1) {
                    return markers[index + 1].time
                }
                // For the last marker, extend to the current data position
                return Math.min(ShotDataModel.rawTime, timeAxis.max)
            }

            // Position and size based on marker time range
            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y + chart.plotArea.height - Theme.scaled(4)
            width: Math.max(0, ((nextTime - markerTime) / timeAxis.max) * chart.plotArea.width)
            height: Theme.scaled(4)
            color: isFlowMode ? Theme.flowColor : Theme.pressureColor
            opacity: 0.8
            visible: markerTime <= timeAxis.max && modelData.label !== "Start"
        }
    }

    // Custom legend - vertical, top-left, overlaying graph
    Rectangle {
        id: legendBackground
        x: chart.plotArea.x + Theme.spacingSmall
        y: chart.plotArea.y + Theme.spacingSmall
        width: legendColumn.width + Theme.spacingSmall * 2
        height: legendColumn.height + Theme.spacingSmall * 2
        color: Qt.rgba(Theme.surfaceColor.r, Theme.surfaceColor.g, Theme.surfaceColor.b, 0.85)
        radius: Theme.scaled(4)

        Column {
            id: legendColumn
            anchors.centerIn: parent
            spacing: Theme.scaled(2)

            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.pressureColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Pressure"; color: Theme.textSecondaryColor; font: Theme.captionFont }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.flowColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Flow"; color: Theme.textSecondaryColor; font: Theme.captionFont }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.temperatureColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Temp"; color: Theme.textSecondaryColor; font: Theme.captionFont }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.weightColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Weight"; color: Theme.textSecondaryColor; font: Theme.captionFont }
            }

            // Solid/dashed indicator
            Row {
                spacing: Theme.scaled(4)
                Rectangle { width: Theme.scaled(8); height: Theme.scaled(2); color: Theme.textSecondaryColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "actual"; color: Qt.rgba(255, 255, 255, 0.5); font: Theme.captionFont }
            }
            Row {
                spacing: Theme.scaled(4)
                Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textSecondaryColor; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textSecondaryColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "target"; color: Qt.rgba(255, 255, 255, 0.5); font: Theme.captionFont }
            }
        }
    }

    // Time axis label - inside graph at bottom right
    Text {
        x: chart.plotArea.x + chart.plotArea.width - width - Theme.spacingSmall
        y: chart.plotArea.y + chart.plotArea.height - height - Theme.scaled(12)
        text: "Time (s)"
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        opacity: 0.7
    }
}
