import QtQuick
import QtCharts
import DecenzaDE1

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: Qt.darker(Theme.surfaceColor, 1.3)
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false

    margins.top: 0
    margins.bottom: Theme.scaled(32)
    margins.left: 0
    margins.right: 0

    // Properties
    property var frames: []
    property int selectedFrameIndex: -1

    // Signals
    signal frameSelected(int index)
    signal frameDoubleClicked(int index)

    // Force refresh the graph (call when frame properties change in place)
    function refresh() {
        updateCurves()
        // Force Repeater to refresh by toggling model
        var savedFrames = frames
        frameRepeater.model = []
        frameRepeater.model = savedFrames
    }

    // Calculate total duration from frames
    property double totalDuration: {
        var total = 0
        for (var i = 0; i < frames.length; i++) {
            total += frames[i].seconds || 0
        }
        return Math.max(total, 5)  // Minimum 5 seconds
    }

    // Time axis (X)
    ValueAxis {
        id: timeAxis
        min: 0
        max: totalDuration * 1.1  // 10% padding
        tickCount: Math.min(10, Math.max(3, Math.floor(totalDuration / 5) + 1))
        labelFormat: "%.0f"
        labelsColor: Theme.textSecondaryColor
        labelsFont.pixelSize: Theme.scaled(12)
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
        labelsFont.pixelSize: Theme.scaled(12)
        gridLineColor: Qt.rgba(255, 255, 255, 0.1)
    }

    // Temperature axis (right Y)
    ValueAxis {
        id: tempAxis
        min: 80
        max: 100
        tickCount: 5
        labelFormat: "%.0f"
        labelsColor: Theme.temperatureColor
        labelsFont.pixelSize: Theme.scaled(12)
        gridLineColor: "transparent"
    }

    // Static pressure series (up to 3 segments for discontinuous curves)
    LineSeries {
        id: pressureSeries0
        name: "Pressure"
        color: Theme.pressureGoalColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }
    LineSeries {
        id: pressureSeries1
        color: Theme.pressureGoalColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }
    LineSeries {
        id: pressureSeries2
        color: Theme.pressureGoalColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Static flow series (up to 3 segments for discontinuous curves)
    LineSeries {
        id: flowSeries0
        name: "Flow"
        color: Theme.flowGoalColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }
    LineSeries {
        id: flowSeries1
        color: Theme.flowGoalColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }
    LineSeries {
        id: flowSeries2
        color: Theme.flowGoalColor
        width: Theme.graphLineWidth
        style: Qt.DashLine
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Temperature target curve (dashed) - always continuous
    LineSeries {
        id: temperatureGoalSeries
        name: "Temperature"
        color: Theme.temperatureGoalColor
        width: Math.max(2, Theme.graphLineWidth - 1)
        style: Qt.DashLine
        axisX: timeAxis
        axisYRight: tempAxis
    }

    // Arrays to track which static series to use
    property var pressureSeriesPool: [pressureSeries0, pressureSeries1, pressureSeries2]
    property var flowSeriesPool: [flowSeries0, flowSeries1, flowSeries2]

    // Frame region overlays
    Item {
        id: frameOverlays
        anchors.fill: parent

        Repeater {
            id: frameRepeater
            model: frames

            delegate: Item {
                id: frameDelegate

                required property int index
                required property var modelData
                property var frame: modelData
                property double frameStart: {
                    var start = 0
                    for (var i = 0; i < index; i++) {
                        start += frames[i].seconds || 0
                    }
                    return start
                }
                property double frameDuration: frame ? frame.seconds || 0 : 0

                x: chart.plotArea.x + (frameStart / (totalDuration * 1.1)) * chart.plotArea.width
                y: chart.plotArea.y
                width: (frameDuration / (totalDuration * 1.1)) * chart.plotArea.width
                height: chart.plotArea.height

                Rectangle {
                    anchors.fill: parent
                    color: {
                        if (index === selectedFrameIndex) {
                            return Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.3)
                        }
                        return index % 2 === 0 ?
                            Qt.rgba(255, 255, 255, 0.05) :
                            Qt.rgba(255, 255, 255, 0.02)
                    }
                    border.width: index === selectedFrameIndex ? Theme.scaled(2) : Theme.scaled(1)
                    border.color: index === selectedFrameIndex ?
                        Theme.accentColor : Qt.rgba(255, 255, 255, 0.2)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Theme.scaled(4)
                        color: frame && frame.pump === "flow" ? Theme.flowColor : Theme.pressureColor
                        opacity: 0.8
                    }
                }

                Item {
                    id: labelContainer
                    property real visualWidth: labelText.implicitHeight + Theme.scaled(8)
                    property real visualHeight: labelText.implicitWidth + Theme.scaled(8)

                    x: parent.width / 2 - visualWidth / 2
                    y: Theme.scaled(4)
                    width: visualWidth
                    height: visualHeight

                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: frame ? (frame.name || ("Frame " + (index + 1))) : ""
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: index === selectedFrameIndex
                        rotation: -90
                        transformOrigin: Item.Center
                        opacity: 0.9
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            selectedFrameIndex = index
                            frameSelected(index)
                        }
                        onDoubleClicked: {
                            frameDoubleClicked(index)
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: {
                        selectedFrameIndex = index
                        frameSelected(index)
                    }
                    onDoubleClicked: {
                        frameDoubleClicked(index)
                    }
                }
            }
        }
    }

    // Generate target curves from frames
    function updateCurves() {
        // Clear all static series
        for (var p = 0; p < pressureSeriesPool.length; p++) {
            pressureSeriesPool[p].clear()
        }
        for (var f = 0; f < flowSeriesPool.length; f++) {
            flowSeriesPool[f].clear()
        }
        temperatureGoalSeries.clear()

        if (frames.length === 0) {
            return
        }

        var time = 0
        var pressureSeriesIndex = 0
        var flowSeriesIndex = 0
        var currentPressureSeries = null
        var currentFlowSeries = null

        for (var i = 0; i < frames.length; i++) {
            var frame = frames[i]
            var startTime = time
            var endTime = time + (frame.seconds || 0)
            var isSmooth = frame.transition === "smooth"

            var prevFrame = i > 0 ? frames[i-1] : null
            var prevSamePump = prevFrame && prevFrame.pump === frame.pump

            var prevPressure = prevSamePump ? prevFrame.pressure : frame.pressure
            var prevFlow = prevSamePump ? prevFrame.flow : frame.flow
            var prevTemp = i > 0 ? frames[i-1].temperature : frame.temperature

            // Pressure curve (only for pressure-control frames)
            if (frame.pump === "pressure") {
                // Need new series if coming from flow mode
                if (!currentPressureSeries || (prevFrame && prevFrame.pump !== "pressure")) {
                    if (pressureSeriesIndex < pressureSeriesPool.length) {
                        currentPressureSeries = pressureSeriesPool[pressureSeriesIndex]
                        pressureSeriesIndex++
                    }
                }

                if (currentPressureSeries) {
                    if (isSmooth) {
                        var startPressure = prevSamePump ? prevPressure : 0
                        currentPressureSeries.append(startTime, startPressure)
                    } else {
                        currentPressureSeries.append(startTime, frame.pressure)
                    }
                    currentPressureSeries.append(endTime, frame.pressure)
                }
            }

            // Flow curve (only for flow-control frames)
            if (frame.pump === "flow") {
                // Need new series if coming from pressure mode
                if (!currentFlowSeries || (prevFrame && prevFrame.pump !== "flow")) {
                    if (flowSeriesIndex < flowSeriesPool.length) {
                        currentFlowSeries = flowSeriesPool[flowSeriesIndex]
                        flowSeriesIndex++
                    }
                }

                if (currentFlowSeries) {
                    if (isSmooth) {
                        var startFlow = prevSamePump ? prevFlow : 0
                        currentFlowSeries.append(startTime, startFlow)
                    } else {
                        currentFlowSeries.append(startTime, frame.flow)
                    }
                    currentFlowSeries.append(endTime, frame.flow)
                }
            }

            // Temperature curve (always shown, continuous across all frames)
            if (isSmooth && i > 0) {
                temperatureGoalSeries.append(startTime, prevTemp)
            } else {
                temperatureGoalSeries.append(startTime, frame.temperature)
            }
            temperatureGoalSeries.append(endTime, frame.temperature)

            time = endTime
        }
    }

    // Re-generate curves when frames change
    onFramesChanged: {
        updateCurves()
    }

    // Timer for delayed initial update (ensures chart is fully ready)
    Timer {
        id: initialUpdateTimer
        interval: 100
        onTriggered: updateCurves()
    }

    Component.onCompleted: {
        updateCurves()
        // Also schedule a delayed update to catch any initialization issues
        initialUpdateTimer.start()
    }

    // Custom legend - horizontal, below graph
    Row {
        id: legendRow
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.scaled(2)
        spacing: Theme.spacingLarge

        Row {
            spacing: Theme.scaled(4)
            Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.pressureGoalColor; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Pressure"; color: Theme.textSecondaryColor; font: Theme.captionFont }
        }
        Row {
            spacing: Theme.scaled(4)
            Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.flowGoalColor; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Flow"; color: Theme.textSecondaryColor; font: Theme.captionFont }
        }
        Row {
            spacing: Theme.scaled(4)
            Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.temperatureGoalColor; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Temp"; color: Theme.textSecondaryColor; font: Theme.captionFont }
        }
    }
}
