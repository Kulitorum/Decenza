import QtQuick
import QtGraphs
import Decenza
import "."  // For AccessibleMouseArea
import "graphs"

// Outer Item wraps GraphsView so all overlays — FastLineRenderer traces, dashed
// goal curves, phase-marker vertical lines, manual right-axis labels — render as
// siblings on top of the chart. GraphsView's scene-graph paints over any direct
// QQuickItem children, so overlays must be siblings, not children.
Item {
    id: chart

    // Alias so DashedLineSeries delegates can reach the GraphsView without
    // writing `graphsView: graphsView` — that RHS shadows the delegate's own
    // `graphsView` property (which defaults to `parent`) and resolves to null.
    readonly property alias graphsViewRef: graphsView

    // Re-export the GraphsView's plot rect for parent pages that hit-test
    // against it (e.g. right-axis toggle overlays). Matches the legacy
    // Qt Charts ChartView.plotArea API.
    readonly property rect plotArea: graphsView.plotArea

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
    property bool showTemperatureMixGoal: Settings.boolValue("graph/showTemperatureMixGoal", false)

    property bool advancedMode: false

    // Right axis toggle (weight vs temperature)
    property bool showWeightAxis: Settings.boolValue("graph/showWeightAxis", true)
    function toggleRightAxis() {
        showWeightAxis = !showWeightAxis
        Settings.setValue("graph/showWeightAxis", showWeightAxis)
    }

    // Auto-expanding time axis. timeAxis.max is set imperatively by recalcMax() to
    // avoid the binding-loop chain max → relayout → plotArea → cachedPlotWidth → recalcMax.
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double cachedPlotWidth: 1
    property double _lastAxisMax: 5.0

    // Pick a tickInterval that keeps the axis readable across the whole shot-length
    // range (5 s warm-up through a 60 s+ long pour) without leaving a huge dead
    // zone past the last tick. Mirrors the dynamic tickCount logic from Qt Charts.
    function _niceTimeAxisStep(span) {
        if (span <= 5)  return 1
        if (span <= 10) return 2
        if (span <= 30) return 5
        return 10
    }

    function recalcMax() {
        // Track rawTime continuously (no snap-to-tick) so live data always reaches
        // the right edge — matches the Qt Charts feel. Ticks land at multiples of
        // tickInterval; the rightmost one may sit short of the plot edge during the
        // shot, which is fine.
        var raw = ShotDataModel.rawTime * cachedPlotWidth / Math.max(1, cachedPlotWidth - paddingPixels)
        var newMax = Math.max(minTime, raw)
        var step = _niceTimeAxisStep(newMax)
        if (newMax !== _lastAxisMax || timeAxis.tickInterval !== step) {
            _lastAxisMax = newMax
            timeAxis.max = newMax
            timeAxis.tickInterval = step
        }
    }

    Connections {
        target: ShotDataModel
        function onRawTimeChanged() { chart.recalcMax() }
    }

    Component.onCompleted: {
        ShotDataModel.registerFastSeries(
            pressureRenderer, flowRenderer, temperatureRenderer,
            weightRenderer, weightFlowRenderer, resistanceRenderer,
            conductanceRenderer, darcyResistanceRenderer, temperatureMixRenderer
        )
        recalcMax()
    }

    GraphsView {
        id: graphsView
        anchors.fill: parent
        // Reserve room on the right for the manual temperature/weight labels and
        // on top for the legend. Qt Graphs doesn't carve out a right margin the
        // way Qt Charts' margins.right did.
        anchors.rightMargin: Theme.scaled(55)
        anchors.topMargin: Theme.scaled(10)
        theme: DecenzaGraphsTheme {}

        axisX: timeAxis
        axisY: pressureAxis

        onPlotAreaChanged: {
            var w = Math.max(1, graphsView.plotArea.width)
            if (Math.abs(w - chart.cachedPlotWidth) > 1) {
                chart.cachedPlotWidth = w
                chart.recalcMax()
            }
        }

        // Time axis (X). max is set imperatively by recalcMax() — declarative binding
        // forms a feedback loop with the plotArea-driven recalc.
        ValueAxis {
            id: timeAxis
            min: 0
            max: chart.minTime
            tickInterval: 10
            subTickCount: 0
            labelFormat: "%.0f"
        }

        // Pressure/Flow axis (left Y).
        // tickInterval 3 reproduces the original tickCount: 5 (labels at 0, 3, 6, 9, 12).
        ValueAxis {
            id: pressureAxis
            min: 0
            max: 12
            tickInterval: 3
            subTickCount: 0
            labelFormat: "%.0f"
            titleText: "bar / mL·g/s"
        }
    }

    // === HIDDEN RIGHT-AXIS HOLDERS ===
    // QtObject value holders that DashedLineSeries / FastLineRenderer can read for
    // coordinate mapping. Qt Graphs has no sanctioned dual-Y-axis path here.
    QtObject {
        id: tempAxis
        property real min: 40
        property real max: 100
    }

    QtObject {
        id: weightAxis
        property real min: 0
        // Live shots may bump SAW past the configured target (#792 +10g button), so
        // take the larger of profile target and current MachineState target. Each
        // source uses an explicit > 0 check because targetWeight == 0 means SAW
        // disabled, and JS `||` would conflate that with "no data".
        property real max: Math.max(10, Math.max(
            ProfileManager.targetWeight > 0 ? ProfileManager.targetWeight : 0,
            MachineState.targetWeight > 0 ? MachineState.targetWeight : 0,
            36) * 1.1)
    }

    // === DASHED GOAL CURVES (bridge overlays) ===

    // Pressure goal segments
    Repeater {
        model: ShotDataModel.pressureGoalSegments
        delegate: DashedLineSeries {
            required property var modelData
            graphsView: chart.graphsViewRef
            axisX: timeAxis
            axisY: pressureAxis
            points: modelData
            strokeColor: Theme.pressureGoalColor
            strokeWidth: Theme.scaled(2)
            visible: chart.showPressure
        }
    }

    // Flow goal segments
    Repeater {
        model: ShotDataModel.flowGoalSegments
        delegate: DashedLineSeries {
            required property var modelData
            graphsView: chart.graphsViewRef
            axisX: timeAxis
            axisY: pressureAxis
            points: modelData
            strokeColor: Theme.flowGoalColor
            strokeWidth: Theme.scaled(2)
            visible: chart.showFlow
        }
    }

    // Temperature goal — mapped to the right tempAxis.
    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: tempAxis
        points: ShotDataModel.temperatureGoalPoints
        strokeColor: Theme.temperatureGoalColor
        strokeWidth: Theme.scaled(2)
        visible: chart.showTemperature
    }

    // Mix temperature goal (SetMixTemp) — advanced, reads against the Mix temp line.
    // Hidden when the shot carries no mix goal data rather than drawn at zero.
    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: tempAxis
        points: ShotDataModel.temperatureMixGoalPoints
        strokeColor: Theme.temperatureMixGoalColor
        strokeWidth: Theme.scaled(2)
        visible: chart.showTemperatureMixGoal && chart.advancedMode && points.length > 0
    }

    // === VERTICAL PHASE / FRAME MARKER LINES ===

    Repeater {
        model: ShotDataModel.phaseMarkers
        delegate: DashedLineSeries {
            required property var modelData
            readonly property string markerLabel: modelData.label
            readonly property bool isStart: markerLabel === "Start"
            readonly property bool isEnd: markerLabel === "End"

            graphsView: chart.graphsViewRef
            axisX: timeAxis
            axisY: pressureAxis
            points: [Qt.point(modelData.time, 0), Qt.point(modelData.time, 12)]
            strokeColor: isStart ? Theme.accentColor
                                 : (isEnd ? Theme.stopMarkerColor : Theme.frameMarkerColor)
            strokeWidth: (isStart || isEnd) ? Theme.scaled(2) : Theme.scaled(1)
            // DashDot for phase markers, Dot for inter-frame markers — closest equivalents
            // to Qt Charts' Qt.DashDotLine / Qt.DotLine on a ShapePath dash pattern.
            dashPattern: (isStart || isEnd) ? [4, 2, 1, 2] : [1, 3]
        }
    }

    // === ACTUAL LINES (solid) - FastLineRenderer with pre-allocated VBO ===
    // These render outside Qt Graphs via QSGGeometryNode for zero-copy GPU updates.

    FastLineRenderer {
        id: pressureRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.pressureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showPressure
    }

    FastLineRenderer {
        id: flowRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.flowColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showFlow
    }

    FastLineRenderer {
        id: temperatureRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.temperatureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempAxis.min; maxY: tempAxis.max
        visible: chart.showTemperature
    }

    FastLineRenderer {
        id: weightFlowRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.weightFlowColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showWeightFlow
    }

    FastLineRenderer {
        id: resistanceRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.resistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showResistance && chart.advancedMode
    }

    FastLineRenderer {
        id: conductanceRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.conductanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showConductance && chart.advancedMode
    }

    FastLineRenderer {
        id: darcyResistanceRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.darcyResistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: chart.showDarcyResistance && chart.advancedMode
    }

    FastLineRenderer {
        id: temperatureMixRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.temperatureMixColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempAxis.min; maxY: tempAxis.max
        visible: chart.showTemperatureMix && chart.advancedMode
    }

    FastLineRenderer {
        id: weightRenderer
        x: graphsView.plotArea.x; y: graphsView.plotArea.y
        width: graphsView.plotArea.width; height: graphsView.plotArea.height
        color: Theme.weightColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: weightAxis.min; maxY: weightAxis.max
        visible: chart.showWeight
    }

    // Frame marker labels (rotated text)
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

            x: graphsView.plotArea.x + (markerTime / timeAxis.max) * graphsView.plotArea.width
            y: graphsView.plotArea.y
            height: graphsView.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0

            Text {
                id: markerText
                text: {
                    if (transitionReason === "" || isStart || isEnd) return markerLabel
                    var suffix = ""
                    switch (transitionReason) {
                        case "weight": suffix = " [W]"; break
                        case "pressure": suffix = " [P]"; break
                        case "pressure_unconfirmed": suffix = " [P]"; break
                        case "flow": suffix = " [F]"; break
                        case "flow_unconfirmed": suffix = " [F]"; break
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
                return Math.min(ShotDataModel.rawTime, timeAxis.max)
            }

            x: graphsView.plotArea.x + (markerTime / timeAxis.max) * graphsView.plotArea.width
            y: graphsView.plotArea.y + graphsView.plotArea.height - Theme.scaled(4)
            width: Math.max(0, ((nextTime - markerTime) / timeAxis.max) * graphsView.plotArea.width)
            height: Theme.scaled(4)
            color: isFlowMode ? Theme.flowColor : Theme.pressureColor
            opacity: 0.8
            visible: markerTime <= timeAxis.max && modelData.label !== "Start"
        }
    }

    // Time axis label - inside graph at bottom right
    Text {
        x: graphsView.plotArea.x + graphsView.plotArea.width - width - Theme.spacingSmall
        y: graphsView.plotArea.y + graphsView.plotArea.height - height - Theme.scaled(12)
        text: TranslationManager.translate("graph.axis.time", "Time (s)")
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        opacity: 0.7
        Accessible.ignored: true
    }

    // Manual right-axis labels — toggling visibility on Qt Graphs ValueAxis would
    // resize the plot area, so we draw labels at a fixed position instead.
    Item {
        id: rightAxisLabels
        x: graphsView.plotArea.x + graphsView.plotArea.width + Theme.scaled(4)
        y: graphsView.plotArea.y
        width: chart.width - x
        height: graphsView.plotArea.height

        Accessible.role: Accessible.Button
        Accessible.name: chart.showWeightAxis ? TranslationManager.translate("graph.rightAxisWeight", "Right axis: Weight. Tap for Temperature")
                                              : TranslationManager.translate("graph.rightAxisTemp", "Right axis: Temperature. Tap for Weight")
        Accessible.focusable: true
        Accessible.onPressAction: axisToggleArea.clicked(null)

        property color labelColor: chart.showWeightAxis ? Theme.weightColor : Theme.temperatureColor

        // Tick labels — five evenly spaced labels mirror the original tickCount: 5.
        Repeater {
            model: 5
            Text {
                required property int index
                property real value: {
                    var axisMin = chart.showWeightAxis ? weightAxis.min : tempAxis.min
                    var axisMax = chart.showWeightAxis ? weightAxis.max : tempAxis.max
                    return axisMax - index * (axisMax - axisMin) / 4
                }
                text: chart.showWeightAxis ? value.toFixed(0) : Theme.cToDisplay(value).toFixed(0)
                x: 0
                y: index / 4 * rightAxisLabels.height - height / 2
                font: Theme.captionFont
                color: rightAxisLabels.labelColor
                Accessible.ignored: true
            }
        }

        // Axis title
        Text {
            text: chart.showWeightAxis ? "g" : Theme.tempUnitSuffix()
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
