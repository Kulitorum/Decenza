import QtQuick
import QtCharts
import DecenzaDE1
import "."  // For AccessibleMouseArea

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

    // Register goal/marker LineSeries with C++ model (infrequent updates, replace() is fine)
    Component.onCompleted: {
        ShotDataModel.registerSeries(
            [pressureGoal1, pressureGoal2, pressureGoal3, pressureGoal4, pressureGoal5],
            [flowGoal1, flowGoal2, flowGoal3, flowGoal4, flowGoal5],
            temperatureGoalSeries,
            extractionStartMarker, stopMarker,
            [frameMarker1, frameMarker2, frameMarker3, frameMarker4, frameMarker5,
             frameMarker6, frameMarker7, frameMarker8, frameMarker9, frameMarker10]
        )
        // Register fast renderers (QSGGeometryNode, pre-allocated VBO - no rebuilds)
        ShotDataModel.registerFastSeries(
            pressureRenderer, flowRenderer, temperatureRenderer,
            weightRenderer, weightFlowRenderer, resistanceRenderer
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

        // Animation disabled: causes visible lag and curves extending past plot area
        // Behavior on max {
        //     NumberAnimation { duration: 100; easing.type: Easing.Linear }
        // }
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
        titleText: "bar / mL·g/s"
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

    LineSeries {
        id: stopMarker
        name: ""
        color: Theme.stopMarkerColor
        width: Theme.scaled(2)
        style: Qt.DashDotLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    LineSeries { id: frameMarker1; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker2; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker3; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker4; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker5; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker6; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker7; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker8; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker9; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }
    LineSeries { id: frameMarker10; name: ""; color: Theme.frameMarkerColor; width: Theme.scaled(1); style: Qt.DotLine; axisX: timeAxis; axisY: pressureAxis }

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

    // Empty anchor series to keep the weight axis registered with ChartView
    // (axes only display when at least one series references them)
    LineSeries {
        name: ""
        axisX: timeAxis
        axisYRight: weightAxis
    }

    // === ACTUAL LINES (solid) - FastLineRenderer with pre-allocated VBO ===
    // These render outside Qt Charts via QSGGeometryNode for zero-copy GPU updates

    FastLineRenderer {
        id: pressureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.pressureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
    }

    FastLineRenderer {
        id: flowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.flowColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
    }

    FastLineRenderer {
        id: temperatureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.temperatureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempAxis.min; maxY: tempAxis.max
    }

    FastLineRenderer {
        id: weightFlowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.weightFlowColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
    }

    FastLineRenderer {
        id: resistanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.resistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: Settings.value("graph/showResistance", false)
    }

    FastLineRenderer {
        id: weightRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.weightColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: weightAxis.min; maxY: weightAxis.max
    }

    // Frame marker labels
    Repeater {
        id: markerLabels
        model: ShotDataModel.phaseMarkers

        delegate: Item {
            id: markerDelegate
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property string markerLabel: modelData.label
            property string transitionReason: modelData.transitionReason || ""
            property bool isStart: modelData.label === "Start"
            property bool isEnd: modelData.label === "End"

            // Calculate position using timeAxis.max for consistent scaling with smooth scroll
            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y
            height: chart.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0

            Text {
                id: markerText
                text: {
                    if (transitionReason === "" || isStart || isEnd) return markerLabel
                    var suffix = ""
                    switch (transitionReason) {
                        case "weight": suffix = " [W]"; break
                        case "pressure": suffix = " [P]"; break
                        case "flow": suffix = " [F]"; break
                        case "time": suffix = " [T]"; break
                    }
                    return markerLabel + suffix
                }
                font.pixelSize: Theme.scaled(18)
                font.bold: isStart || isEnd
                color: isStart ? Theme.accentColor : (isEnd ? Theme.stopMarkerColor : Qt.rgba(255, 255, 255, 0.8))
                rotation: -90
                transformOrigin: Item.TopLeft
                x: Theme.scaled(4)
                y: Theme.scaled(8) + width
                // Decorative - accessibility handled by tap area below
                Accessible.ignored: true

                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(-2)
                    color: Qt.darker(Theme.surfaceColor, 1.5)
                    radius: Theme.scaled(2)
                }
            }

            // Accessible tap area for End marker - announces weight at stop vs final weight
            AccessibleMouseArea {
                visible: isEnd
                x: markerText.x - Theme.scaled(10)
                y: markerText.y - markerText.width - Theme.scaled(10)
                width: markerText.height + Theme.scaled(20)
                height: markerText.width + Theme.scaled(20)

                accessibleName: {
                    var stopWeight = ShotDataModel.weightAtStop.toFixed(1)
                    var finalWeight = ShotDataModel.finalWeight.toFixed(1)
                    var diff = (ShotDataModel.finalWeight - ShotDataModel.weightAtStop).toFixed(1)
                    return "End marker. Weight at stop: " + stopWeight + " grams. " +
                           "Final settled weight: " + finalWeight + " grams. " +
                           "Drip amount: " + diff + " grams."
                }

                onAccessibleClicked: {
                    if (typeof AccessibilityManager !== "undefined") {
                        AccessibilityManager.announce(accessibleName)
                    }
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
                Text { text: "Pressure"; color: Theme.textSecondaryColor; font: Theme.captionFont; Accessible.ignored: true }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.flowColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Flow"; color: Theme.textSecondaryColor; font: Theme.captionFont; Accessible.ignored: true }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.temperatureColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Temp"; color: Theme.textSecondaryColor; font: Theme.captionFont; Accessible.ignored: true }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.weightColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Weight"; color: Theme.textSecondaryColor; font: Theme.captionFont; Accessible.ignored: true }
            }
            Row {
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(2); radius: Theme.scaled(1); color: Theme.weightFlowColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Weight flow"; color: Theme.textSecondaryColor; font: Theme.captionFont; Accessible.ignored: true }
            }
            Row {
                visible: resistanceRenderer.visible
                spacing: Theme.spacingSmall
                Rectangle { width: Theme.scaled(16); height: Theme.scaled(2); radius: Theme.scaled(1); color: Theme.resistanceColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Resistance"; color: Theme.textSecondaryColor; font: Theme.captionFont; Accessible.ignored: true }
            }

            // Solid/dashed indicator
            Row {
                spacing: Theme.scaled(4)
                Rectangle { width: Theme.scaled(8); height: Theme.scaled(2); color: Theme.textSecondaryColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "actual"; color: Qt.rgba(255, 255, 255, 0.5); font: Theme.captionFont; Accessible.ignored: true }
            }
            Row {
                spacing: Theme.scaled(4)
                Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textSecondaryColor; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { width: Theme.scaled(3); height: Theme.scaled(2); color: Theme.textSecondaryColor; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "target"; color: Qt.rgba(255, 255, 255, 0.5); font: Theme.captionFont; Accessible.ignored: true }
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
        Accessible.ignored: true
    }
}
