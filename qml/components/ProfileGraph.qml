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

    // Estimate display duration for a frame.
    // De1app plugins use: shotendtime = target / pour_flow + 16
    // We approximate per-frame to match that holistic estimate.
    function estimateFrameSeconds(frame, index) {
        var secs = frame.seconds || 0
        if (secs <= 0) return 0

        // Machine-side exit conditions — frame exits before its timeout
        if (frame.exit_if) {
            // Flow-pump preinfusion (exits on pressure_over): typically 3-8s
            if (frame.pump === "flow" && secs > 8) {
                return 8
            }
            // Other exit types (pressure ramps, etc.): cap at 15s
            if (secs > 15) {
                return 15
            }
        }

        // App-side weight exit: frame exits when scale reaches target grams
        if ((frame.exit_weight || 0) > 0 && secs > 5) {
            return 5
        }

        // Long-timeout flow frames: estimate pour duration from target weight/volume
        if (secs >= 60 && frame.pump === "flow") {
            var flowRate = frame.flow || 0
            // flow=0 with smooth = continue at previous rate; fast = actual pause
            if (flowRate <= 0 && frame.transition === "smooth") {
                for (var j = index - 1; j >= 0; j--) {
                    if (frames[j].pump === "flow" && (frames[j].flow || 0) > 0) {
                        flowRate = frames[j].flow
                        break
                    }
                }
            }
            if (flowRate > 0) {
                var target = targetWeight > 0 ? targetWeight : targetVolume
                if (target > 0) {
                    return Math.min(secs, Math.max(target / flowRate, 10))
                }
            }
            return 60  // No target or flow rate — cap at 60s
        }

        return secs
    }

    // Calculate total duration using estimated frame durations
    property double totalDuration: {
        var total = 0
        for (var i = 0; i < frames.length; i++) {
            total += estimateFrameSeconds(frames[i], i)
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

    // Pressure curve (continuous across entire shot)
    LineSeries {
        id: pressureSeries0
        name: "Pressure"
        color: Theme.pressureGoalColor
        width: Math.max(3, Theme.graphLineWidth)
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Flow curve (continuous across entire shot)
    LineSeries {
        id: flowSeries0
        name: "Flow"
        color: Theme.flowGoalColor
        width: Math.max(3, Theme.graphLineWidth)
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
                        start += estimateFrameSeconds(frames[i], i)
                    }
                    return start
                }
                property double frameDuration: frame ? estimateFrameSeconds(frame, index) : 0

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

    // Generate simulation curves from frames.
    // Shows BOTH pressure and flow as continuous curves across the entire shot.
    // Handles multiple profile patterns:
    //   - D-Flow: declining pressure during flow control (explicit flow rate)
    //   - A-Flow: flat pressure at limiter during flow control (flow=0 + limiter)
    //   - Simple profiles: pressure/flow preinfusion + extraction
    function updateCurves() {
        pressureSeries0.clear()
        flowSeries0.clear()
        temperatureGoalSeries.clear()

        if (frames.length === 0) {
            return
        }

        var time = 0
        var peakPressure = 0   // highest pressure seen (for D-Flow decline simulation)
        var lastPressure = 0   // last pressure value for curve continuity
        var lastFlow = 0       // last flow value for curve continuity
        var hadPreinfusionSim = false  // whether we've shown the absorption curve

        // Hardcoded preinfusion simulation data (from de1app D-Flow demo_graph).
        // Time fractions (0-1) across the frame duration
        var simTimeFracs = [0, 0.067, 0.133, 0.2, 0.267, 0.333, 0.4, 0.467, 0.6, 0.7, 1.0]
        // Pressure build-up factors (0-1 of target pressure)
        var simPressureFactors = [0, 0, 0, 0, 0.7, 0.93, 1.0, 1.0, 1.0, 1.0, 1.0]
        // Flow absorption curve (mL/s) — water rushes into dry puck then drops
        var simFlowValues = [0, 5.6, 7.6, 8.3, 8.2, 6.0, 2.9, 1.3, 0.6, 0.4, 0.3]

        for (var i = 0; i < frames.length; i++) {
            var frame = frames[i]
            var duration = estimateFrameSeconds(frame, i)
            var startTime = time
            var endTime = time + duration
            var isSmooth = frame.transition === "smooth"

            // Skip zero-duration frames (transition markers like "Pressure Decline" in A-Flow).
            // Don't let them poison tracking variables or add spurious curve points.
            if (duration <= 0) {
                if (frame.pump === "flow" && (frame.flow || 0) > 0) {
                    lastFlow = frame.flow
                }
                time = endTime
                continue
            }

            if (frame.pump === "pressure") {
                var sp = frame.pressure || 0
                peakPressure = Math.max(peakPressure, sp)

                if (!hadPreinfusionSim && duration >= 3 && lastPressure <= 0) {
                    // First pressure frame starting from unpressurized:
                    // simulate preinfusion with absorption curve (D-Flow pattern)
                    hadPreinfusionSim = true

                    // Find pour flow target by looking ahead to next flow frame
                    var pourFlow = 1.0
                    for (var j = i + 1; j < frames.length; j++) {
                        if (frames[j].pump === "flow" && (frames[j].flow || 0) > 0) {
                            pourFlow = frames[j].flow
                            break
                        }
                    }

                    for (var k = 0; k < simTimeFracs.length; k++) {
                        var t = startTime + simTimeFracs[k] * duration
                        pressureSeries0.append(t, simPressureFactors[k] * sp)
                        // Flow: absorption curve, but cap declining portion at pour flow
                        var fv = k < 3 ? simFlowValues[k] : Math.max(simFlowValues[k], pourFlow)
                        flowSeries0.append(t, fv)
                    }
                    lastPressure = sp
                    lastFlow = pourFlow
                } else {
                    // Already pressurized or short frame: simple ramp/hold
                    var startP = (isSmooth && lastPressure > 0) ? lastPressure : sp
                    pressureSeries0.append(startTime, startP)
                    pressureSeries0.append(endTime, sp)
                    lastPressure = sp

                    // Flow during pressure frames: residual at pour rate
                    var residualFlow = 1.0
                    for (var j2 = i + 1; j2 < frames.length; j2++) {
                        if (frames[j2].pump === "flow" && (frames[j2].flow || 0) > 0) {
                            residualFlow = frames[j2].flow
                            break
                        }
                    }
                    flowSeries0.append(startTime, residualFlow)
                    flowSeries0.append(endTime, residualFlow)
                    lastFlow = residualFlow
                }

            } else if (frame.pump === "flow") {
                var targetFlow = frame.flow || 0

                // flow=0 with smooth transition = continue at previous rate (A-Flow pattern)
                // flow=0 with fast transition = stop flow (bloom pause pattern)
                var effectiveFlow = targetFlow
                if (targetFlow <= 0 && isSmooth) {
                    for (var jf = i - 1; jf >= 0; jf--) {
                        if (frames[jf].pump === "flow" && (frames[jf].flow || 0) > 0) {
                            effectiveFlow = frames[jf].flow
                            break
                        }
                    }
                    if (effectiveFlow <= 0) effectiveFlow = lastFlow
                }

                // Flow curve
                var startFlow = (isSmooth && lastFlow > 0) ? lastFlow : effectiveFlow
                flowSeries0.append(startTime, startFlow)
                flowSeries0.append(endTime, effectiveFlow)
                lastFlow = effectiveFlow

                // Pressure during flow frames — depends on profile pattern
                var limiter = frame.max_flow_or_pressure || 0
                var exitP = frame.exit_pressure_over || 0

                if (frame.exit_if && exitP > 0 && lastPressure < exitP) {
                    // Fill pattern: pressure builds toward exit target
                    pressureSeries0.append(startTime, lastPressure)
                    pressureSeries0.append(endTime, exitP)
                    lastPressure = exitP
                    peakPressure = Math.max(peakPressure, exitP)
                } else if (targetFlow <= 0 && limiter >= 3) {
                    // A-Flow limiter pattern: flow=0 means machine drives pressure
                    // to the limiter value (max_flow_or_pressure on flow frames = pressure cap)
                    var pStart = isSmooth ? lastPressure : limiter
                    pressureSeries0.append(startTime, pStart)
                    pressureSeries0.append(endTime, limiter)
                    lastPressure = limiter
                    peakPressure = Math.max(peakPressure, limiter)
                } else {
                    // D-Flow pattern: explicit flow rate, pressure declines from peak
                    var pDecStart = lastPressure > 0 ? lastPressure : (peakPressure > 0 ? peakPressure : 6)
                    var pDecEnd = (peakPressure > 0 ? peakPressure : 6) * 0.4
                    pressureSeries0.append(startTime, pDecStart)
                    pressureSeries0.append(endTime, pDecEnd)
                    lastPressure = pDecEnd
                }
            }

            // Temperature curve (always shown, continuous)
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
