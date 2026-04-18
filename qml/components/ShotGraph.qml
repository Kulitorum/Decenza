import QtQuick
import QtCharts
import Decenza
import "."  // For AccessibleMouseArea

ChartView {
    id: chart
    antialiasing: true
    backgroundColor: "transparent"
    plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
    legend.visible: false

    // Persisted visibility toggles (tappable legend). Settings.boolValue() coerces
    // QSettings' INI-backend strings ("true"/"false") to real booleans — plain
    // Settings.value() returns the raw QString which JavaScript treats as truthy,
    // so toggled-off states wouldn't survive between shots.
    property bool showPressure: Settings.boolValue("graph/showPressure", true)
    property bool showFlow: Settings.boolValue("graph/showFlow", true)
    property bool showTemperature: Settings.boolValue("graph/showTemperature", true)
    property bool showWeight: Settings.boolValue("graph/showWeight", true)
    property bool showWeightFlow: Settings.boolValue("graph/showWeightFlow", true)
    property bool showResistance: Settings.boolValue("graph/showResistance", false)
    property bool showConductance: Settings.boolValue("graph/showConductance", false)
    property bool showConductanceDerivative: Settings.boolValue("graph/showConductanceDerivative", false)
    property bool showDarcyResistance: Settings.boolValue("graph/showDarcyResistance", false)
    property bool showTemperatureMix: Settings.boolValue("graph/showTemperatureMix", false)

    property bool advancedMode: false

    // Right axis toggle (weight vs temperature)
    property bool showWeightAxis: Settings.boolValue("graph/showWeightAxis", true)
    function toggleRightAxis() {
        showWeightAxis = !showWeightAxis
        Settings.setValue("graph/showWeightAxis", showWeightAxis)
    }

    margins.top: Theme.scaled(10)
    margins.bottom: 0
    margins.left: Theme.scaled(40)
    margins.right: Theme.scaled(55)

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
            weightRenderer, weightFlowRenderer, resistanceRenderer,
            conductanceRenderer, darcyResistanceRenderer, temperatureMixRenderer
        )
        recalcMax()
    }

    // Calculate axis max: data fills frame with exactly 5 scaled pixels padding at right
    // Solve: max = rawTime + paddingPixels * (max / plotWidth)
    // => max = rawTime * plotWidth / (plotWidth - paddingPixels)
    //
    // timeAxis.max and tickCount are set imperatively in recalcMax() to avoid a
    // binding loop: max → ChartView relayout → plotArea → cachedPlotWidth → recalcMax → max.
    // Qt detects the circular dependency chain even when guards prevent infinite recursion,
    // so we break it by removing all declarative bindings on timeAxis properties.
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double cachedPlotWidth: 1
    property double _lastAxisMax: 5.0  // Internal change-detection cache — do NOT bind to this (causes binding loop)

    function recalcMax() {
        var raw = ShotDataModel.rawTime * cachedPlotWidth / Math.max(1, cachedPlotWidth - paddingPixels)
        var newMax = Math.max(minTime, raw)
        if (newMax !== _lastAxisMax) {
            _lastAxisMax = newMax
            // Assign imperatively — see block comment above for binding loop explanation
            timeAxis.max = newMax
            timeAxis.tickCount = Math.min(7, Math.max(3, Math.floor(newMax / 10) + 2))
        }
    }

    Connections {
        target: ShotDataModel
        function onRawTimeChanged() { chart.recalcMax() }
    }

    onPlotAreaChanged: {
        var w = Math.max(1, chart.plotArea.width)
        if (Math.abs(w - cachedPlotWidth) > 1) {
            cachedPlotWidth = w
            recalcMax()
        }
    }

    // Time axis - fills frame, expands only when data pushes against right edge
    ValueAxis {
        id: timeAxis
        min: 0
        // max and tickCount are set imperatively by recalcMax() to avoid binding loop
        max: minTime
        tickCount: 3
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

    // Temperature axis (right Y) - hidden; labels provided by rightAxisLabels
    ValueAxis {
        id: tempAxis
        min: 40
        max: 100
        tickCount: 5
        visible: false
    }

    // Weight axis (right Y) - hidden; labels provided by rightAxisLabels
    ValueAxis {
        id: weightAxis
        min: 0
        // Live shots may bump SAW past the configured target (#792 +10g button), so
        // take the larger of profile target and current MachineState target. Each
        // source uses an explicit > 0 check because targetWeight == 0 means SAW
        // disabled, and JS `||` would conflate that with "no data".
        max: Math.max(10, Math.max(
            ProfileManager.targetWeight > 0 ? ProfileManager.targetWeight : 0,
            MachineState.targetWeight > 0 ? MachineState.targetWeight : 0,
            36) * 1.1)
        tickCount: 5
        visible: false
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
    LineSeries { id: pressureGoal1; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal2; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal3; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal4; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }
    LineSeries { id: pressureGoal5; name: ""; color: Theme.pressureGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showPressure }

    // Flow goal segments (up to 5 segments for mode switches)
    LineSeries { id: flowGoal1; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal2; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal3; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal4; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }
    LineSeries { id: flowGoal5; name: ""; color: Theme.flowGoalColor; width: Theme.scaled(2); style: Qt.DashLine; axisX: timeAxis; axisY: pressureAxis; visible: chart.showFlow }

    LineSeries {
        id: temperatureGoalSeries
        name: "T Goal"
        color: Theme.temperatureGoalColor
        width: Theme.scaled(2)
        style: Qt.DashLine
        axisX: timeAxis
        axisYRight: tempAxis
        visible: chart.showTemperature
    }

    // Empty anchor series to keep the weight axis registered with ChartView
    // (required for weightAxis min/max properties to update correctly)
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
        visible: chart.showPressure
    }

    FastLineRenderer {
        id: flowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.flowColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showFlow
    }

    FastLineRenderer {
        id: temperatureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.temperatureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempAxis.min; maxY: tempAxis.max
        visible: chart.showTemperature
    }

    FastLineRenderer {
        id: weightFlowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.weightFlowColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showWeightFlow
    }

    FastLineRenderer {
        id: resistanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.resistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showResistance && chart.advancedMode
    }

    FastLineRenderer {
        id: conductanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.conductanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showConductance && chart.advancedMode
    }

    FastLineRenderer {
        id: darcyResistanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.darcyResistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showDarcyResistance && chart.advancedMode
    }

    FastLineRenderer {
        id: temperatureMixRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.temperatureMixColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempAxis.min; maxY: tempAxis.max
        visible: chart.showTemperatureMix && chart.advancedMode
    }

    FastLineRenderer {
        id: weightRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.weightColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: weightAxis.min; maxY: weightAxis.max
        visible: chart.showWeight
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

    // Time axis label - inside graph at bottom right
    Text {
        x: chart.plotArea.x + chart.plotArea.width - width - Theme.spacingSmall
        y: chart.plotArea.y + chart.plotArea.height - height - Theme.scaled(12)
        text: TranslationManager.translate("graph.axis.time", "Time (s)")
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        opacity: 0.7
        Accessible.ignored: true
    }

    // Manual right-axis labels — built-in Qt Charts axes cause layout shift
    // (plotArea resizes when axis visibility toggles), so we draw labels at a fixed position
    Item {
        id: rightAxisLabels
        x: chart.plotArea.x + chart.plotArea.width + Theme.scaled(4)
        y: chart.plotArea.y
        width: chart.width - x
        height: chart.plotArea.height

        Accessible.role: Accessible.Button
        Accessible.name: chart.showWeightAxis ? TranslationManager.translate("graph.rightAxisWeight", "Right axis: Weight. Tap for Temperature")
                                              : TranslationManager.translate("graph.rightAxisTemp", "Right axis: Temperature. Tap for Weight")
        Accessible.focusable: true
        Accessible.onPressAction: axisToggleArea.clicked(null)

        property color labelColor: chart.showWeightAxis ? Theme.weightColor : Theme.temperatureColor

        // Tick labels — count must match tickCount on tempAxis/weightAxis
        Repeater {
            model: 5
            Text {
                required property int index
                property real value: {
                    var axisMin = chart.showWeightAxis ? weightAxis.min : tempAxis.min
                    var axisMax = chart.showWeightAxis ? weightAxis.max : tempAxis.max
                    return axisMax - index * (axisMax - axisMin) / 4
                }
                text: value.toFixed(0)
                x: 0
                y: index / 4 * rightAxisLabels.height - height / 2
                font: Theme.captionFont
                color: rightAxisLabels.labelColor
                Accessible.ignored: true
            }
        }

        // Axis title
        Text {
            text: chart.showWeightAxis ? "g" : "°C"
            font: Theme.captionFont
            color: rightAxisLabels.labelColor
            rotation: 90
            transformOrigin: Item.Center
            x: Theme.scaled(24)
            y: rightAxisLabels.height / 2 - height / 2
            Accessible.ignored: true
        }

        MouseArea {
            id: axisToggleArea
            anchors.fill: parent
            onClicked: chart.toggleRightAxis()
        }
    }
}
