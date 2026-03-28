import QtQuick
import QtCharts
import Decenza

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: Qt.darker(Theme.surfaceColor, 1.3)
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false
    Accessible.role: Accessible.Graphic
    Accessible.name: TranslationManager.translate("profileGraph.accessibleName", "Profile graph")

    margins.top: 0
    margins.bottom: Theme.scaled(32)
    margins.left: 0
    margins.right: 0

    // Properties
    property var frames: []
    property int selectedFrameIndex: -1
    property double targetWeight: 0
    property double targetVolume: 0

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

    // Frame display duration — uses the raw seconds value, matching de1app's
    // update_de1_plus_advanced_explanation_chart which uses $theseconds directly
    // with no adjustment for exit conditions or weight targets.
    function frameDisplaySeconds(frame, index) {
        return frame.seconds || 0
    }

    // Calculate total display duration from raw frame seconds
    property double totalDuration: {
        var total = 0
        for (var i = 0; i < frames.length; i++) {
            total += frameDisplaySeconds(frames[i], i)
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
        gridLineColor: Qt.rgba(1, 1, 1, 0.1)
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
        gridLineColor: Qt.rgba(1, 1, 1, 0.1)
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

    // Pressure curve (active during pressure-pump frames, drops to 0 otherwise)
    LineSeries {
        id: pressureSeries0
        name: "Pressure"
        color: Theme.pressureGoalColor
        width: Theme.graphLineWidth * 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Flow curve (active during flow-pump frames, drops to 0 otherwise)
    LineSeries {
        id: flowSeries0
        name: "Flow"
        color: Theme.flowGoalColor
        width: Theme.graphLineWidth * 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Temperature target curve (dashed) - always continuous
    LineSeries {
        id: temperatureGoalSeries
        name: "Temperature"
        color: Theme.temperatureGoalColor
        width: Theme.graphLineWidth * 2
        style: Qt.DashLine
        axisX: timeAxis
        axisYRight: tempAxis
    }

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

                Accessible.role: Accessible.ListItem
                Accessible.name: (frame ? (frame.name || ("Frame " + (index + 1))) : "") +
                                 (index === selectedFrameIndex ? ", selected" : "")
                Accessible.focusable: true
                property double frameStart: {
                    var start = 0
                    for (var i = 0; i < index; i++) {
                        start += frameDisplaySeconds(frames[i], i)
                    }
                    return start
                }
                property double frameDuration: frame ? frameDisplaySeconds(frame, index) : 0

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
                            Qt.rgba(1, 1, 1, 0.05) :
                            Qt.rgba(1, 1, 1, 0.02)
                    }
                    border.width: index === selectedFrameIndex ? Theme.scaled(2) : Theme.scaled(1)
                    border.color: index === selectedFrameIndex ?
                        Theme.accentColor : Qt.rgba(1, 1, 1, 0.2)
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
                        Accessible.ignored: true
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
                    Accessible.ignored: true
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

    // Generate target-value curves from frames.
    // Matches de1app's update_de1_plus_advanced_explanation_chart algorithm:
    //   - Pressure curve shown only during pressure-pump frames
    //   - Flow curve shown only during flow-pump frames
    //   - Inactive curve drops to 0 (de1app uses -1 sentinel clipped by Y axis min)
    //   - Raw frame seconds used for durations (no exit condition estimation)
    //   - Smooth transitions ramp from previous value to target
    //   - Fast transitions step instantly to target
    function updateCurves() {
        pressureSeries0.clear()
        flowSeries0.clear()
        temperatureGoalSeries.clear()

        if (frames.length === 0) return

        var time = 0
        var previousPressure = 0  // last pressure target from a pressure frame
        var previousFlow = 0      // last flow target from a flow frame
        var previousPump = ""     // pump type of last plotted frame

        // Initial point at origin (matches de1app)
        pressureSeries0.append(0, 0)
        flowSeries0.append(0, 0)

        for (var i = 0; i < frames.length; i++) {
            var frame = frames[i]
            var duration = frame.seconds || 0
            var startTime = time
            var endTime = time + duration
            var isSmooth = frame.transition === "smooth"
            var pump = frame.pump || "pressure"

            // Zero-duration frames: track values for smooth transitions but skip plotting
            if (duration <= 0) {
                if (pump === "pressure") previousPressure = frame.pressure || 0
                else if (pump === "flow") previousFlow = frame.flow || 0
                previousPump = pump
                time = endTime
                continue
            }

            if (pump === "pressure") {
                var pressure = frame.pressure || 0

                // Handle pump-type boundary: flow → pressure
                if (previousPump === "flow" && previousPump !== "") {
                    // Drop flow to 0 at boundary
                    flowSeries0.append(startTime, 0)
                }

                // Start point: smooth ramps from previous, fast steps to target
                var startP = (isSmooth && i > 0) ? previousPressure : pressure
                pressureSeries0.append(startTime, startP)
                flowSeries0.append(startTime, 0)

                // End point
                pressureSeries0.append(endTime, pressure)
                flowSeries0.append(endTime, 0)

                previousPressure = pressure
                previousPump = "pressure"

            } else if (pump === "flow") {
                var targetFlow = frame.flow || 0

                // flow=0 with smooth = continue at previous rate (A-Flow pattern)
                var effectiveFlow = targetFlow
                if (targetFlow <= 0 && isSmooth) {
                    effectiveFlow = previousFlow > 0 ? previousFlow : 0
                }

                // Handle pump-type boundary: pressure → flow
                if (previousPump === "pressure" && previousPump !== "") {
                    // Drop pressure to 0 at boundary
                    pressureSeries0.append(startTime, 0)
                }

                // Start point: smooth ramps from previous, fast steps to target
                var startF = (isSmooth && i > 0) ? previousFlow : effectiveFlow
                flowSeries0.append(startTime, startF)
                pressureSeries0.append(startTime, 0)

                // End point
                flowSeries0.append(endTime, effectiveFlow)
                pressureSeries0.append(endTime, 0)

                previousFlow = effectiveFlow
                previousPump = "flow"
            }

            // Temperature curve (always continuous across all frames)
            var prevTemp = i > 0 ? (frames[i-1].temperature || frame.temperature || 0) : (frame.temperature || 0)
            if (isSmooth && i > 0) {
                temperatureGoalSeries.append(startTime, prevTemp)
            } else {
                temperatureGoalSeries.append(startTime, frame.temperature || 0)
            }
            temperatureGoalSeries.append(endTime, frame.temperature || 0)

            time = endTime
        }
    }

    // Re-generate curves when frames change
    onFramesChanged: {
        updateCurves()
    }

    Component.onCompleted: {
        updateCurves()
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
            Text { text: TranslationManager.translate("profileGraph.pressure", "Pressure"); color: Theme.textSecondaryColor; font: Theme.captionFont }
        }
        Row {
            spacing: Theme.scaled(4)
            Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.flowGoalColor; anchors.verticalCenter: parent.verticalCenter }
            Text { text: TranslationManager.translate("profileGraph.flow", "Flow"); color: Theme.textSecondaryColor; font: Theme.captionFont }
        }
        Row {
            spacing: Theme.scaled(4)
            Rectangle { width: Theme.scaled(16); height: Theme.scaled(3); radius: Theme.scaled(1); color: Theme.temperatureGoalColor; anchors.verticalCenter: parent.verticalCenter }
            Text { text: TranslationManager.translate("profileGraph.temp", "Temp"); color: Theme.textSecondaryColor; font: Theme.captionFont }
        }
    }
}
