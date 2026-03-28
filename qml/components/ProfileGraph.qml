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
        recomputeFrameDurations()
        updateCurves()
        var savedFrames = frames
        frameRepeater.model = []
        frameRepeater.model = savedFrames
    }

    // === Profile graph simulation ===
    // Produces realistic-looking graphs using normalized shape tables derived from
    // de1app D-Flow demo_graph empirical data. Preinfusion frames use the absorption
    // curve shape (flow spike then decay, pressure S-curve buildup). Subsequent
    // pressure frames use smooth ramps. Flow-pump frames show pressure at the limiter
    // (or 70% of current pressure if no limiter is set).

    // Normalized preinfusion absorption shape — derived from de1app D-Flow demo_graph
    // empirical data. Scaled to fit any pressure-pump preinfusion frame.
    readonly property var simTimeFrac: [0, 0.066, 0.135, 0.201, 0.267, 0.333, 0.402, 0.469, 0.535, 0.600, 1.0]
    readonly property var simFlowFrac: [0, 0.675, 0.916, 1.000, 0.988, 0.723, 0.349, 0.157, 0.072, 0.048, 0.036]
    readonly property var simPresFrac: [0, 0,     0,     0,     0.700, 0.930, 1.000, 1.000, 1.000, 1.000, 1.000]

    // Cached frame durations — recomputed explicitly when frames change.
    // Using a plain property (not a binding) avoids QML binding-loop issues
    // with var arrays that can fail to re-evaluate on frames assignment.
    property var frameDurations: []

    function recomputeFrameDurations() {
        var durations = []
        for (var i = 0; i < frames.length; i++) {
            durations.push(estimateFrameDuration(frames[i], i))
        }
        frameDurations = durations
    }

    function estimateFrameDuration(frame, index) {
        var secs = frame.seconds || 0
        if (secs <= 0) return 0
        var pump = frame.pump || "pressure"

        // First frame is preinfusion (pressure or flow pump) — use full simulation
        // duration (up to 15s) regardless of exit conditions. De1app's demo_graph
        // always shows 15s for preinfusion; the exit condition affects the real shot
        // but the chart shows the idealized curve.
        if (index === 0 && secs > 2) {
            return Math.min(secs, 15)
        }

        // Exit conditions on other frames: cap at realistic durations
        if (frame.exit_if) {
            if (pump === "flow" && secs > 8) return 8
            if (secs > 15) return 15
        }

        // Long pour frames: estimate from target weight/volume
        if (secs >= 60 && pump === "flow") {
            var flowRate = frame.flow || 0
            if (flowRate > 0) {
                var target = targetWeight > 0 ? targetWeight : targetVolume
                if (target > 0) {
                    return Math.min(secs, Math.max(target / flowRate + 3, 10))
                }
            }
            return Math.min(secs, 60)
        }

        return secs
    }

    property double totalDuration: {
        var total = 0
        for (var i = 0; i < frameDurations.length; i++) {
            total += frameDurations[i]
        }
        return Math.max(total, 5)
    }

    // Time axis (X)
    ValueAxis {
        id: timeAxis
        min: 0
        max: totalDuration * 1.1
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

    // Pressure curve
    LineSeries {
        id: pressureSeries0
        name: "Pressure"
        color: Theme.pressureGoalColor
        width: Theme.graphLineWidth * 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Flow curve
    LineSeries {
        id: flowSeries0
        name: "Flow"
        color: Theme.flowGoalColor
        width: Theme.graphLineWidth * 3
        axisX: timeAxis
        axisY: pressureAxis
    }

    // Temperature curve
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
                        start += (frameDurations[i] || 0)
                    }
                    return start
                }
                property double frameDuration: frameDurations[index] || 0

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

    // Simulate extraction physics across all frames.
    // Pressure-pump frames use the preinfusion absorption shape (scaled to frame params).
    // Flow-pump frames show target flow with pressure at the limiter.
    // Smooth transitions ramp between values.
    function updateCurves() {
        pressureSeries0.clear()
        flowSeries0.clear()
        temperatureGoalSeries.clear()

        if (frames.length === 0) return

        var time = 0
        var currentPressure = 0
        var currentFlow = 0
        var hadPreinfusion = false    // whether we've drawn the absorption curve
        var residualFlow = 0.3       // flow after preinfusion saturation

        // Initial point
        pressureSeries0.append(0, 0)
        flowSeries0.append(0, 0)

        for (var i = 0; i < frames.length; i++) {
            var frame = frames[i]
            var duration = frameDurations[i] || 0
            var startTime = time
            var endTime = time + duration
            var isSmooth = frame.transition === "smooth"
            var pump = frame.pump || "pressure"
            var temp = frame.temperature || 93

            if (duration <= 0) {
                time = endTime
                continue
            }

            // Flow-pump preinfusion: machine targets flow rate, pressure builds
            // until exit condition triggers. Show flow holding at target with
            // pressure ramping up (opposite of pressure-pump preinfusion).
            if (pump === "flow" && !hadPreinfusion && i === 0 && frame.exit_if && duration >= 2) {
                hadPreinfusion = true
                var piFlow = frame.flow || 4.0
                var piExitP = frame.exit_pressure_over || 4.0

                // Find pour flow for residual
                for (var jp = i + 1; jp < frames.length; jp++) {
                    if ((frames[jp].pump || "pressure") === "flow" && (frames[jp].flow || 0) > 0) {
                        residualFlow = frames[jp].flow
                        break
                    }
                }

                // Flow ramps from 0 to target, pressure builds gradually
                var piSteps = Math.min(10, Math.max(4, Math.round(duration)))
                for (var kp = 0; kp <= piSteps; kp++) {
                    var fracPI = kp / piSteps
                    var tPI = startTime + fracPI * duration
                    // Flow: ramps up quickly then holds
                    var flowFracPI = Math.min(1.0, fracPI * 3)  // reaches target at 1/3 through
                    var flowPI = piFlow * flowFracPI
                    // Pressure: builds with S-curve toward exit pressure
                    var pEase = fracPI * fracPI * (3 - 2 * fracPI)
                    var pressPI = pEase * piExitP
                    pressureSeries0.append(tPI, pressPI)
                    flowSeries0.append(tPI, flowPI)
                    temperatureGoalSeries.append(tPI, temp)
                }
                currentPressure = piExitP
                currentFlow = piFlow
                time = endTime
                continue
            }

            if (pump === "pressure") {
                var targetP = frame.pressure || 0

                if (!hadPreinfusion && duration >= 2) {
                    // First significant pressure frame: draw absorption curve
                    hadPreinfusion = true
                    var peakFlow = Math.min(frame.flow || 8.0, 8.5)

                    // Find pour flow from the next flow frame (for residual estimation)
                    for (var j = i + 1; j < frames.length; j++) {
                        if ((frames[j].pump || "pressure") === "flow" && (frames[j].flow || 0) > 0) {
                            residualFlow = frames[j].flow
                            break
                        }
                    }

                    // Draw preinfusion using normalized shape scaled to this frame
                    for (var k = 0; k < simTimeFrac.length; k++) {
                        var t = startTime + simTimeFrac[k] * duration
                        var p = simPresFrac[k] * targetP
                        // Scale flow: absorption peak, then decay toward residual
                        var rawFlow = simFlowFrac[k] * peakFlow
                        var f = Math.max(rawFlow, residualFlow * simPresFrac[k])
                        pressureSeries0.append(t, p)
                        flowSeries0.append(t, f)
                        temperatureGoalSeries.append(t, temp)
                    }
                    currentPressure = targetP
                    currentFlow = residualFlow
                } else {
                    // Subsequent pressure frame: ramp/step pressure
                    // For smooth transitions (e.g., A-Flow Pressure Up/Decline),
                    // interpolate pressure over the full frame duration with multiple
                    // points to produce a visible curve (not just start/end).
                    var startP = (isSmooth && currentPressure > 0) ? currentPressure : targetP

                    if (isSmooth && Math.abs(startP - targetP) > 0.5 && duration >= 2) {
                        // Smooth ramp with intermediate points
                        var steps = Math.min(8, Math.max(3, Math.round(duration / 2)))
                        for (var s = 0; s <= steps; s++) {
                            var frac = s / steps
                            var tPt = startTime + frac * duration
                            // Ease-in-out: cubic hermite for natural pressure ramp
                            var ease = frac * frac * (3 - 2 * frac)
                            var pPt = startP + ease * (targetP - startP)
                            // Flow responds to changing pressure: higher pressure → more flow through puck
                            var flowPt = residualFlow + (pPt / Math.max(1, targetP)) * residualFlow * 0.5
                            pressureSeries0.append(tPt, pPt)
                            flowSeries0.append(tPt, flowPt)
                            temperatureGoalSeries.append(tPt, temp)
                        }
                    } else {
                        pressureSeries0.append(startTime, startP)
                        pressureSeries0.append(endTime, targetP)
                        flowSeries0.append(startTime, residualFlow)
                        flowSeries0.append(endTime, residualFlow)
                        temperatureGoalSeries.append(startTime, temp)
                        temperatureGoalSeries.append(endTime, temp)
                    }
                    currentPressure = targetP
                    currentFlow = residualFlow
                }

            } else if (pump === "flow") {
                var targetFlow = frame.flow || 0
                var limiter = frame.max_flow_or_pressure || 0

                // flow=0 with smooth = continue at previous rate (A-Flow pattern)
                var effectiveFlow = targetFlow
                if (targetFlow <= 0 && isSmooth) {
                    effectiveFlow = currentFlow > 0 ? currentFlow : 0
                }

                // Pressure during flow: limiter if set, otherwise residual from peak
                var flowPressure = limiter > 0 ? limiter : (currentPressure > 0 ? currentPressure * 0.7 : 0)

                // Smooth transitions ramp over the full frame duration (matching machine
                // behavior with transition=smooth). Fast transitions settle quickly (~2s).
                if (isSmooth && duration >= 2) {
                    // Smooth: interpolate over full duration with intermediate points
                    var steps2 = Math.min(8, Math.max(3, Math.round(duration / 2)))
                    for (var s2 = 0; s2 <= steps2; s2++) {
                        var frac2 = s2 / steps2
                        var tPt2 = startTime + frac2 * duration
                        var ease2 = frac2 * frac2 * (3 - 2 * frac2)
                        pressureSeries0.append(tPt2, currentPressure + ease2 * (flowPressure - currentPressure))
                        flowSeries0.append(tPt2, currentFlow + ease2 * (effectiveFlow - currentFlow))
                        temperatureGoalSeries.append(tPt2, temp)
                    }
                } else {
                    // Fast: quick ramp then hold
                    var rampTime = Math.min(2.0, duration * 0.3)
                    var rampEnd = startTime + rampTime

                    pressureSeries0.append(startTime, currentPressure)
                    flowSeries0.append(startTime, currentFlow)
                    temperatureGoalSeries.append(startTime, temp)

                    pressureSeries0.append(rampEnd, flowPressure)
                    flowSeries0.append(rampEnd, effectiveFlow)
                    temperatureGoalSeries.append(rampEnd, temp)

                    pressureSeries0.append(endTime, flowPressure)
                    flowSeries0.append(endTime, effectiveFlow)
                    temperatureGoalSeries.append(endTime, temp)
                }

                currentPressure = flowPressure
                currentFlow = effectiveFlow
            }

            time = endTime
        }
    }

    onFramesChanged: { recomputeFrameDurations(); updateCurves() }
    Component.onCompleted: { recomputeFrameDurations(); updateCurves() }

    // Custom legend
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
