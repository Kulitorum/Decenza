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

    // Detect D-Flow profiles: 3 frames, first two pressure-pump, last flow-pump with limiter.
    // De1app's D-Flow plugin uses a completely different demo_graph with simulated absorption
    // curves instead of the standard raw-setpoint chart.
    property bool isDFlowProfile: {
        if (frames.length !== 3) return false
        var f0 = frames[0], f1 = frames[1], f2 = frames[2]
        return (f0.pump || "pressure") === "pressure"
            && (f1.pump || "pressure") === "pressure"
            && (f2.pump || "flow") === "flow"
            && (f2.max_flow_or_pressure || 0) > 0
    }

    // Frame display duration — uses the raw seconds value for standard profiles,
    // or estimated durations for D-Flow profiles (matching de1app's demo_graph).
    function frameDisplaySeconds(frame) {
        if (isDFlowProfile) {
            // D-Flow demo_graph uses simulated times: 15s preinfusion, remainder is pour
            var idx = -1
            for (var i = 0; i < frames.length; i++) {
                if (frames[i] === frame) { idx = i; break }
            }
            if (idx === 0) return 15           // Filling: simulated 0-15s
            if (idx === 1) return 1            // Infusing: brief hold (visible as narrow bar)
            if (idx === 2) return dflowShotEndTime() - 16  // Pouring: 16s to shotEnd
            return frame.seconds || 0
        }
        return frame.seconds || 0
    }

    // D-Flow demo graph estimates total shot time from target weight and pour flow
    function dflowShotEndTime() {
        if (frames.length < 3) return 30
        var pourFlow = frames[2].flow || 1.0
        var target = targetWeight > 0 ? targetWeight : (targetVolume > 0 ? targetVolume : 36)
        return target / pourFlow + 16
    }

    // Calculate total display duration from raw frame seconds
    property double totalDuration: {
        if (isDFlowProfile) return Math.max(dflowShotEndTime(), 20)
        var total = 0
        for (var i = 0; i < frames.length; i++) {
            total += frameDisplaySeconds(frames[i])
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

    // Temperature target curve - always continuous
    LineSeries {
        id: temperatureGoalSeries
        name: "Temperature"
        color: Theme.temperatureGoalColor
        width: Theme.graphLineWidth * 2
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
                        start += frameDisplaySeconds(frames[i])
                    }
                    return start
                }
                property double frameDuration: frame ? frameDisplaySeconds(frame) : 0

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

    // D-Flow demo graph — matches de1app's D_Flow_Espresso_Profile::demo_graph.
    // Uses hardcoded preinfusion absorption simulation (0-15s) and estimated pour duration.
    function updateCurvesDFlow() {
        pressureSeries0.clear()
        flowSeries0.clear()
        temperatureGoalSeries.clear()

        if (frames.length < 3) return

        var fillFrame = frames[0]
        var infuseFrame = frames[1]
        var pourFrame = frames[2]

        var soakPressure = fillFrame.pressure || 6.0
        var pourFlow = pourFrame.flow || 1.8
        var pourPressure = pourFrame.max_flow_or_pressure || 6.0
        var fillTemp = fillFrame.temperature || 84
        var pourTemp = pourFrame.temperature || 94
        var shotEnd = dflowShotEndTime()

        // Hardcoded simulation data from de1app D-Flow demo_graph
        var simElapsed = [0, 0.994, 2.03, 3.015, 4.004, 4.994, 6.036, 7.03, 8.017, 8.999, 15]
        var simFlow =    [0, 5.6,   7.6,  8.3,   8.2,   6.0,   2.9,   1.3,  0.6,   0.4,   0.3]
        var sp_a = soakPressure * 0.7
        var sp_b = soakPressure * 0.93
        var simPressure = [0, 0, 0, 0, sp_a, sp_b, soakPressure, soakPressure, soakPressure, soakPressure, soakPressure]

        // Preinfusion phase (0-15s): absorption curve
        for (var k = 0; k < simElapsed.length; k++) {
            pressureSeries0.append(simElapsed[k], simPressure[k])
            flowSeries0.append(simElapsed[k], simFlow[k])
            temperatureGoalSeries.append(simElapsed[k], fillTemp)
        }

        // Pour phase (16s to shotEnd): constant flow at limiter pressure
        pressureSeries0.append(16, pourPressure)
        flowSeries0.append(16, pourFlow)
        temperatureGoalSeries.append(16, pourTemp)

        pressureSeries0.append(shotEnd, pourPressure)
        flowSeries0.append(shotEnd, pourFlow)
        temperatureGoalSeries.append(shotEnd, pourTemp)
    }

    // Generate target-value curves from frames.
    // For D-Flow profiles, uses the demo_graph simulation (see above).
    // For all other profiles, matches de1app's update_de1_plus_advanced_explanation_chart:
    //   - Pressure curve shown during pressure-pump frames, limiter during flow-pump frames
    //   - Flow curve shown during flow-pump frames, drops to 0 during pressure-pump frames
    //   - Raw frame seconds used for durations (no exit condition estimation)
    //   - Smooth transitions ramp from previous value to target
    //   - Fast transitions step instantly to target
    function updateCurves() {
        if (isDFlowProfile) {
            updateCurvesDFlow()
            return
        }

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
                if (previousPump === "flow") {
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

                // Pressure during flow frames: show limiter if set, otherwise 0.
                // max_flow_or_pressure on flow-pump frames = pressure cap — the machine
                // actively maintains up to this pressure during flow control.
                var limiter = frame.max_flow_or_pressure || 0
                var flowFramePressure = limiter > 0 ? limiter : 0

                // Handle pump-type boundary: pressure → flow
                if (previousPump === "pressure") {
                    if (flowFramePressure <= 0) {
                        pressureSeries0.append(startTime, 0)
                    }
                    // If limiter is set, pressure transitions smoothly from previous
                }

                // Start point: smooth ramps from previous, fast steps to target
                var startF = (isSmooth && i > 0) ? previousFlow : effectiveFlow
                flowSeries0.append(startTime, startF)
                var startPressureInFlow = (flowFramePressure > 0 && previousPressure > 0)
                    ? previousPressure : flowFramePressure
                pressureSeries0.append(startTime, startPressureInFlow)

                // End point
                flowSeries0.append(endTime, effectiveFlow)
                pressureSeries0.append(endTime, flowFramePressure)

                previousFlow = effectiveFlow
                if (flowFramePressure > 0) previousPressure = flowFramePressure
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
