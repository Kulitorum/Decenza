// The trace, phase-marker, pump-mode and tick-label Repeater delegates read this file's
// ids (`chart`, `graphsView`, `timeAxis`, `pressureAxis`, `weightRange`, `tempRange`,
// `rightAxisLabels`); Bound makes them statically resolvable. Every one of them already
// declares each injected role it uses required, so Bound cannot break role injection
// here.
pragma ComponentBehavior: Bound

import QtQuick
import QtGraphs
import Decenza
import "GraphUtils.js" as GraphUtils

// Outer Item wraps GraphsView so the overlays — FastLineRenderer traces, marker labels,
// manual right-axis labels — render as siblings on top of the chart. GraphsView's
// scene-graph paints over any direct QQuickItem children, so overlays must be siblings.
// Goal curves and phase-marker lines are native series inside the GraphsView.
Item {
    id: chart

    // Alias so PortalGraphOverlay and the series instantiators can reach the GraphsView:
    // `graphsView: graphsView` would resolve the RHS to their own `graphsView` property.
    readonly property alias graphsViewRef: graphsView

    // The plot rect in THIS item's coordinates, which every sibling overlay below is
    // positioned in. GraphsView.plotArea is in the view's own coordinates, and the view
    // sits below a top margin, so overlays placed from it were drawn that margin too high
    // while the native series were not.
    readonly property rect plotArea: Qt.rect(graphsView.x + graphsView.plotArea.x,
                                             graphsView.y + graphsView.plotArea.y,
                                             graphsView.plotArea.width,
                                             graphsView.plotArea.height)

    // Series visibility is read straight off Settings.graph, which is a real binding: a
    // change anywhere — the legend, the options menu, the comparison table — reaches this
    // graph with no refresh handler. There used to be eleven mirror properties here (and
    // eleven more in each of the other two graphs) initialised from Settings.boolValue(), a
    // plain Q_INVOKABLE that records no dependency, so each was a one-shot read taken at
    // construction and something else had to poke them.

    property bool advancedMode: Settings.graph.advancedMode

    // Right axis: "weight", "temperature" or "flow".
    property string rightAxisMode: Settings.graph.rightAxisMode
    function toggleRightAxis() { Settings.graph.cycleRightAxisMode() }

    // Factor applied to the flow-family traces before plotting (1, 2 or 3). Flow, weight
    // flow rate and the dashed flow goal all take the same factor so they stay readable
    // against each other — the divergence between flow and weight flow is the tell this
    // graph exists to show, and scaling only one of them would fake it.
    property int flowMultiplier: Settings.graph.flowMultiplier

    // Auto-expanding time axis. timeAxis.max is set imperatively by recalcMax() to
    // avoid the binding-loop chain max → relayout → plotArea → cachedPlotWidth → recalcMax.
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double cachedPlotWidth: 1
    property double _lastAxisMax: 5.0

    // Pick a tickInterval that keeps the axis readable across the whole shot-length
    // range (5 s warm-up through a 60 s+ long pour) without leaving a huge dead
    // zone past the last tick. Mirrors the dynamic tickCount logic from Qt Charts.
    function recalcMax() {
        // Track rawTime continuously (no snap-to-tick) so live data always reaches
        // the right edge — matches the Qt Charts feel. Ticks land at multiples of
        // tickInterval; the rightmost one may sit short of the plot edge during the
        // shot, which is fine.
        var raw = GraphUtils.paddedAxisEnd(ShotDataModel.rawTime, cachedPlotWidth, paddingPixels)
        var newMax = Math.max(minTime, raw)
        var step = GraphUtils.niceTimeAxisStep(newMax)
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

    // Read once per change: the pump-mode bars each look up their neighbour, and every read
    // of ShotDataModel.phaseMarkers rebuilds the list in C++.
    readonly property var _phaseMarkers: ShotDataModel.phaseMarkers

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
        anchors.rightMargin: Theme.scaled(55) + portalOverlay.labelWidth
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
            // Caption goes on the axis, not in an overlay: Qt Graphs draws axis
            // titles itself AND reserves layout space for them (axisrenderer.cpp:662-698
            // counts titled axes into the margin math). The Qt Charts -> Qt Graphs
            // migration (#1146) carried this over as a Text positioned off `plotArea`
            // bottom-right, which floated it ON TOP of the plot, over any trace running
            // along the bottom.
            titleText: TranslationManager.translate("graph.axis.time", "Time (s)")
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
            // Names only the units it still reads for. At 2x/3x the flow-family traces are
            // mapped through a shrunken range, so this axis is true for pressure alone —
            // and this title is the only marker of that visible in a screenshot, which is
            // how graphs arrive in bug reports.
            titleText: chart.flowMultiplier === 1 ? "bar / mL·g/s" : "bar"
        }

        // Temperature goal, on the hidden temperature axis.
        LineSeries {
            axisY: tempRange
            values: ShotDataModel.temperatureGoalPoints
            color: Theme.temperatureGoalColor
            width: Theme.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: Settings.graph.showTemperature
        }

        // Mix temperature goal (SetMixTemp) — advanced, reads against the Mix temp line.
        LineSeries {
            id: mixGoalSeries
            axisY: tempRange
            values: ShotDataModel.temperatureMixGoalPoints
            color: Theme.temperatureMixGoalColor
            width: Theme.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: Settings.graph.showTemperatureMixGoal && chart.advancedMode && mixGoalSeries.count > 0
        }
    }

    PortalGraphOverlay {
        id: portalOverlay
        anchors.fill: parent
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        live: true
        advancedMode: chart.advancedMode
    }

    // === HIDDEN RIGHT-AXIS RANGES ===
    // Invisible axes: native series map through them (a series may carry its own axisY
    // since Qt 6.10), FastLineRenderer reads their min/max, and GraphRightAxisLabels
    // draws the visible labels. A hidden axis reserves no plot space
    // (axisrenderer.cpp:1082).
    ValueAxis {
        id: tempRange
        visible: false
        min: 40
        max: 100
    }

    // Flow-family mapping. Shrinking the range is what makes the trace grow: at 3x the
    // traces map 0-4 mL/s across the full plot height while the left axis still reads
    // 0-12 bar for pressure. The right-axis label column reads this object in flow mode,
    // so what it prints and what is drawn cannot drift apart.
    ValueAxis {
        id: flowRange
        visible: false
        min: pressureAxis.min
        max: pressureAxis.max / chart.flowMultiplier
    }

    ValueAxis {
        id: weightRange
        visible: false
        min: 0
        // Live shots may bump SAW past the configured target (#792 +10g button), so
        // take the larger of profile target and current MachineState target. Each
        // source uses an explicit > 0 check because targetWeight == 0 means SAW
        // disabled, and JS `||` would conflate that with "no data".
        max: Math.max(10, Math.max(
            ProfileManager.targetWeight > 0 ? ProfileManager.targetWeight : 0,
            MachineState.targetWeight > 0 ? MachineState.targetWeight : 0,
            36) * 1.1)
    }

    // === DASHED GOAL CURVES ===
    // One series per segment, keyed on the segment count so a flush that only extends a
    // segment updates its points instead of rebuilding the series.

    GraphSeriesInstantiator {
        graphsView: chart.graphsViewRef
        model: ShotDataModel.pressureGoalSegmentCount
        delegate: LineSeries {
            required property int index
            values: ShotDataModel.pressureGoalSegments[index]
            color: Theme.pressureGoalColor
            width: Theme.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: Settings.graph.showPressure
        }
    }

    GraphSeriesInstantiator {
        graphsView: chart.graphsViewRef
        model: ShotDataModel.flowGoalSegmentCount
        delegate: LineSeries {
            required property int index
            // Same multiplier as the flow trace it is the target for — a goal drawn at a
            // different scale than the curve chasing it would be worse than no goal.
            axisY: flowRange
            values: ShotDataModel.flowGoalSegments[index]
            color: Theme.flowGoalColor
            width: Theme.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: Settings.graph.showFlow
        }
    }

    // === VERTICAL PHASE / FRAME MARKER LINES ===

    GraphSeriesInstantiator {
        graphsView: chart.graphsViewRef
        model: chart._phaseMarkers
        delegate: LineSeries {
            required property var modelData
            readonly property string markerLabel: modelData.label
            readonly property bool isStart: markerLabel === "Start"
            readonly property bool isEnd: markerLabel === "End"

            values: [Qt.point(modelData.time, 0), Qt.point(modelData.time, 12)]
            color: isStart ? Theme.accentColor
                           : (isEnd ? Theme.stopMarkerColor : Theme.frameMarkerColor)
            width: (isStart || isEnd) ? Theme.scaled(2) : Theme.scaled(1)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            // DashDot for phase markers, Dot for inter-frame markers — closest equivalents
            // to Qt Charts' Qt.DashDotLine / Qt.DotLine.
            dashPattern: (isStart || isEnd) ? [4, 2, 1, 2] : [1, 3]
        }
    }

    // === ACTUAL LINES (solid) - FastLineRenderer with pre-allocated VBO ===
    // These render outside Qt Graphs via QSGGeometryNode for zero-copy GPU updates.

    FastLineRenderer {
        id: pressureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.pressureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: Settings.graph.showPressure
    }

    FastLineRenderer {
        id: flowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.flowColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        // Flow-family: the multiplier is applied by SHRINKING the mapped range, so the
        // trace grows without a per-point transform and the source data is untouched.
        //
        // clip is load-bearing at 2x/3x. FastLineRenderer maps y with no clamp
        // (fastlinerenderer.cpp: `h - (y - m_minY) * scaleY`) and adds no clip node, so a
        // sample above maxY paints as geometry ABOVE the plot area, out over the page. At
        // 1x the ceiling was 12 and pump flow never reached it; at the 2x default it is
        // 6.0 mL/s, which the puck-fill spike clears on most shots. The dashed flow goal
        // is a native series, clipped to the plot area (GraphsView.clipPlotArea), so
        // without this the goal and the trace disagree exactly where the trace escapes.
        clip: true
        minY: pressureAxis.min; maxY: pressureAxis.max / chart.flowMultiplier
        visible: Settings.graph.showFlow
    }

    FastLineRenderer {
        id: temperatureRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.temperatureColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempRange.min; maxY: tempRange.max
        visible: Settings.graph.showTemperature
    }

    FastLineRenderer {
        id: weightFlowRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.weightFlowColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        // Flow-family: the multiplier is applied by SHRINKING the mapped range, so the
        // trace grows without a per-point transform and the source data is untouched.
        //
        // clip is load-bearing at 2x/3x. FastLineRenderer maps y with no clamp
        // (fastlinerenderer.cpp: `h - (y - m_minY) * scaleY`) and adds no clip node, so a
        // sample above maxY paints as geometry ABOVE the plot area, out over the page. At
        // 1x the ceiling was 12 and pump flow never reached it; at the 2x default it is
        // 6.0 mL/s, which the puck-fill spike clears on most shots. The dashed flow goal
        // is a native series, clipped to the plot area (GraphsView.clipPlotArea), so
        // without this the goal and the trace disagree exactly where the trace escapes.
        clip: true
        minY: pressureAxis.min; maxY: pressureAxis.max / chart.flowMultiplier
        visible: Settings.graph.showWeightFlow
    }

    FastLineRenderer {
        id: resistanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.resistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: Settings.graph.showResistance && chart.advancedMode
    }

    FastLineRenderer {
        id: conductanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.conductanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: Settings.graph.showConductance && chart.advancedMode
    }

    FastLineRenderer {
        id: darcyResistanceRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.darcyResistanceColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: pressureAxis.min; maxY: pressureAxis.max
        visible: Settings.graph.showDarcyResistance && chart.advancedMode
    }

    FastLineRenderer {
        id: temperatureMixRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.temperatureMixColor
        lineWidth: Theme.scaled(2)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: tempRange.min; maxY: tempRange.max
        visible: Settings.graph.showTemperatureMix && chart.advancedMode
    }

    FastLineRenderer {
        id: weightRenderer
        x: chart.plotArea.x; y: chart.plotArea.y
        width: chart.plotArea.width; height: chart.plotArea.height
        color: Theme.weightColor
        lineWidth: Theme.scaled(3)
        minX: timeAxis.min; maxX: timeAxis.max
        minY: weightRange.min; maxY: weightRange.max
        visible: Settings.graph.showWeight
    }

    // Frame marker labels (rotated text)
    Repeater {
        id: markerLabels
        model: chart._phaseMarkers

        delegate: Item {
            id: markerDelegate
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property string markerLabel: modelData.label
            property string transitionReason: modelData.transitionReason || ""
            property bool isStart: modelData.label === "Start"
            property bool isEnd: modelData.label === "End"

            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y
            height: chart.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0

            Text {
                id: markerText
                text: {
                    if (markerDelegate.transitionReason === "" || markerDelegate.isStart || markerDelegate.isEnd) return markerDelegate.markerLabel
                    var suffix = ""
                    switch (markerDelegate.transitionReason) {
                        case "weight": suffix = " [W]"; break
                        case "pressure": suffix = " [P]"; break
                        case "pressure_unconfirmed": suffix = " [P]"; break
                        case "flow": suffix = " [F]"; break
                        case "flow_unconfirmed": suffix = " [F]"; break
                        case "time": suffix = " [T]"; break
                    }
                    return markerDelegate.markerLabel + suffix
                }
                font.pixelSize: Theme.scaled(18)
                font.bold: markerDelegate.isStart || markerDelegate.isEnd
                color: markerDelegate.isStart ? Theme.accentColor : (markerDelegate.isEnd ? Theme.stopMarkerColor : Qt.rgba(1, 1, 1, 0.8))
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
                visible: markerDelegate.isEnd
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
                    if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null) {
                        AccessibilityManager.announce(accessibleName)
                    }
                }
            }
        }
    }

    // Pump mode indicator bars at bottom of chart
    Repeater {
        id: pumpModeIndicators
        model: chart._phaseMarkers

        delegate: Rectangle {
            required property int index
            required property var modelData
            property double markerTime: modelData.time
            property bool isFlowMode: modelData.isFlowMode || false
            // Next marker time (or current rawTime if last marker, capped at visible area)
            property double nextTime: {
                var markers = chart._phaseMarkers
                if (index < markers.length - 1) {
                    return markers[index + 1].time
                }
                return Math.min(ShotDataModel.rawTime, timeAxis.max)
            }

            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y + chart.plotArea.height - Theme.scaled(4)
            width: Math.max(0, ((nextTime - markerTime) / timeAxis.max) * chart.plotArea.width)
            height: Theme.scaled(4)
            color: isFlowMode ? Theme.flowColor : Theme.pressureColor
            opacity: 0.8
            visible: markerTime <= timeAxis.max && modelData.label !== "Start"
        }
    }

    // Right-axis label column. Shared with the history graph — see GraphRightAxisLabels
    // for why the right axis is hand-drawn rather than a second ValueAxis.
    GraphRightAxisLabels {
        x: chart.plotArea.x + chart.plotArea.width + Theme.scaled(4)
        y: chart.plotArea.y
        width: chart.width - x
        height: chart.plotArea.height

        mode: chart.rightAxisMode
        weightAxis: weightRange
        tempAxis: tempRange
        flowAxis: flowRange
        onTapped: chart.toggleRightAxis()
    }
}
