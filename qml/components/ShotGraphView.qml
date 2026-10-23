// The delegates below read this file's ids; Bound makes them statically resolvable. Each
// declares every injected role it uses required, so Bound cannot break role injection.
pragma ComponentBehavior: Bound

import QtQuick
import QtGraphs
import "GraphUtils.js" as GraphUtils

// Outer Item wraps GraphsView so the overlays — FastLineRenderer traces, marker labels,
// manual right-axis labels — render as siblings on top of the chart. GraphsView's
// scene-graph paints over any direct QQuickItem children, so overlays must be siblings.
// Goal curves and phase-marker lines are native series inside the GraphsView.
Item {
    id: chart

    required property ShotGraphData graphData
    required property ShotGraphOptions graphOptions
    required property ShotGraphStyle graphStyle
    required property real portalLabelWidth
    property Component portalOverlayComponent: null
    property bool accessibilityEnabled: false
    property var lastAnnouncedItem: null

    signal fastSeriesReady(FastLineRenderer pressure, FastLineRenderer flow,
                           FastLineRenderer temperature, FastLineRenderer weight,
                           FastLineRenderer weightFlow, FastLineRenderer resistance,
                           FastLineRenderer conductance, FastLineRenderer darcyResistance,
                           FastLineRenderer temperatureMix)
    signal rightAxisTapped()
    signal accessibilityAnnouncementRequested(string announcement)
    signal accessibilityExplorationRequested(string announcement, var item)
    signal accessibilityExplorationCleared(var item)

    // Alias so a host-owned optional portal overlay can reach this GraphsView.
    readonly property alias graphsViewRef: graphsView
    readonly property alias timeAxisRef: timeAxis
    readonly property alias pressureAxisRef: pressureAxis
    readonly property alias temperatureAxisRef: tempRange
    readonly property alias flowAxisRef: flowRange
    readonly property alias weightAxisRef: weightRange

    // The plot rect in THIS item's coordinates, which every sibling overlay below is
    // positioned in. GraphsView.plotArea is in the view's own coordinates, and the view
    // sits below a top margin, so overlays placed from it were drawn that margin too high
    // while the native series were not.
    readonly property rect plotArea: Qt.rect(graphsView.x + graphsView.plotArea.x,
                                             graphsView.y + graphsView.plotArea.y,
                                             graphsView.plotArea.width,
                                             graphsView.plotArea.height)

    readonly property bool advancedMode: chart.graphOptions.advancedMode

    // Right axis: "weight", "temperature" or "flow".
    property string rightAxisMode: chart.graphOptions.rightAxisMode
    function toggleRightAxis() { chart.rightAxisTapped() }

    // Factor applied to the flow-family traces before plotting (1, 2 or 3). Flow, weight
    // flow rate and the dashed flow goal all take the same factor so they stay readable
    // against each other — the divergence between flow and weight flow is the tell this
    // graph exists to show, and scaling only one of them would fake it.
    property int flowMultiplier: chart.graphOptions.flowMultiplier

    // Auto-expanding time axis. timeAxis.max is set imperatively by recalcMax() to
    // avoid the binding-loop chain max → relayout → plotArea → cachedPlotWidth → recalcMax.
    property double minTime: 5.0
    property double paddingPixels: chart.graphStyle.scaled(5)
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
        var raw = GraphUtils.paddedAxisEnd(chart.graphData.rawTime, cachedPlotWidth, paddingPixels)
        var newMax = Math.max(minTime, raw)
        var step = GraphUtils.niceTimeAxisStep(newMax)
        if (newMax !== _lastAxisMax || timeAxis.tickInterval !== step) {
            _lastAxisMax = newMax
            timeAxis.max = newMax
            timeAxis.tickInterval = step
        }
    }

    Connections {
        target: chart.graphData
        function onRawTimeChanged() { chart.recalcMax() }
    }

    // Read once per change: the pump-mode bars each look up their neighbour, and every read
    // of graphData.phaseMarkers rebuilds the list in C++.
    readonly property var _phaseMarkers: chart.graphData.phaseMarkers

    // Vertical lines for the markers labelled `label` ("" = every frame transition), as one
    // series' points with a NaN between lines.
    function _markerLines(label: string): var {
        var pts = []
        for (const m of chart._phaseMarkers) {
            const edge = m.label === "Start" || m.label === "End"
            if (label ? m.label !== label : edge) continue
            if (pts.length) pts.push(Qt.point(NaN, NaN))
            pts.push(Qt.point(m.time, 0), Qt.point(m.time, 12))
        }
        return pts
    }

    Component.onCompleted: {
        chart.fastSeriesReady(
            traceLayer.pressureRenderer, traceLayer.flowRenderer, traceLayer.temperatureRenderer,
            traceLayer.weightRenderer, traceLayer.weightFlowRenderer, traceLayer.resistanceRenderer,
            traceLayer.conductanceRenderer, traceLayer.darcyResistanceRenderer,
            traceLayer.temperatureMixRenderer
        )
        recalcMax()
    }

    GraphsView {
        id: graphsView
        anchors.fill: parent
        // Reserve room on the right for the manual temperature/weight labels and
        // on top for the legend. Qt Graphs doesn't carve out a right margin the
        // way Qt Charts' margins.right did.
        anchors.rightMargin: chart.graphStyle.scaled(55) + chart.portalLabelWidth
        anchors.topMargin: chart.graphStyle.scaled(10)
        theme: GraphsTheme {
            colorScheme: chart.graphStyle.darkMode ? GraphsTheme.ColorScheme.Dark
                                                   : GraphsTheme.ColorScheme.Light
            backgroundColor: "transparent"
            plotAreaBackgroundColor: Qt.darker(chart.graphStyle.surfaceColor, 1.6)
            grid.mainColor: Qt.rgba(1, 1, 1, 0.35)
            grid.subColor: Qt.rgba(1, 1, 1, 0.18)
            grid.mainWidth: 1
            grid.subWidth: 1
            axisX.mainColor: Qt.rgba(1, 1, 1, 0.85)
            axisY.mainColor: Qt.rgba(1, 1, 1, 0.85)
            axisX.mainWidth: 1
            axisY.mainWidth: 1
            axisX.subWidth: 0
            axisY.subWidth: 0
            labelTextColor: chart.graphStyle.textSecondaryColor
            axisXLabelFont: chart.graphStyle.captionFont
            axisYLabelFont: chart.graphStyle.captionFont
        }

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
            // migration (PR Kulitorum/Decenza#1146) carried this over as a Text positioned off `plotArea`
            // bottom-right, which floated it ON TOP of the plot, over any trace running
            // along the bottom.
            titleText: chart.graphStyle.timeAxisTitle
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

        // === DASHED GOAL CURVES AND MARKER LINES ===
        // As few series as possible: any series change makes Qt Graphs re-render every
        // series, and goals change with each sample during a shot. A hidden series gets no
        // points, so it triggers nothing.

        // Each goal is one series; its segments are separated by a NaN point.
        LineSeries {
            id: pressureGoalSeries
            values: pressureGoalSeries.visible ? chart.graphData.pressureGoalPoints : []
            color: chart.graphStyle.pressureGoalColor
            width: chart.graphStyle.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: chart.graphOptions.showPressure
        }

        LineSeries {
            id: flowGoalSeries
            // Same multiplier as the flow trace it is the target for — a goal drawn at a
            // different scale than the curve chasing it would be worse than no goal.
            axisY: flowRange
            values: flowGoalSeries.visible ? chart.graphData.flowGoalPoints : []
            color: chart.graphStyle.flowGoalColor
            width: chart.graphStyle.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: chart.graphOptions.showFlow
        }

        LineSeries {
            id: temperatureGoalSeries
            axisY: tempRange
            values: temperatureGoalSeries.visible ? chart.graphData.temperatureGoalPoints : []
            color: chart.graphStyle.temperatureGoalColor
            width: chart.graphStyle.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: chart.graphOptions.showTemperature
        }

        // Mix temperature goal (SetMixTemp) — advanced, reads against the Mix temp line.
        LineSeries {
            id: mixGoalSeries
            axisY: tempRange
            values: mixGoalSeries.visible ? chart.graphData.temperatureMixGoalPoints : []
            color: chart.graphStyle.temperatureMixGoalColor
            width: chart.graphStyle.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: chart.graphOptions.showTemperatureMixGoal && chart.advancedMode
        }

        // Phase markers: Start and End dash-dot, frame transitions dotted — the closest
        // equivalents to Qt Charts' Qt.DashDotLine / Qt.DotLine.
        LineSeries {
            values: chart._markerLines("Start")
            color: chart.graphStyle.accentColor
            width: chart.graphStyle.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 2, 1, 2]
        }

        LineSeries {
            values: chart._markerLines("End")
            color: chart.graphStyle.stopMarkerColor
            width: chart.graphStyle.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 2, 1, 2]
        }

        LineSeries {
            values: chart._markerLines("")
            color: chart.graphStyle.frameMarkerColor
            width: chart.graphStyle.scaled(1)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [1, 3]
        }
    }

    Loader {
        id: portalOverlay
        anchors.fill: parent
        sourceComponent: chart.portalOverlayComponent
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
            chart.graphData.profileTargetWeight > 0 ? chart.graphData.profileTargetWeight : 0,
            chart.graphData.machineTargetWeight > 0 ? chart.graphData.machineTargetWeight : 0,
            36) * 1.1)
    }

    // The same typed trace layer is compiled by the browser feasibility target.
    // Its native callers supply the existing singleton-derived values and retain
    // ownership of sample registration through the host-facing signal.
    FastLineTraceLayer {
        id: traceLayer
        x: 0; y: 0
        width: chart.width; height: chart.height
        plotX: chart.plotArea.x; plotY: chart.plotArea.y
        plotWidth: chart.plotArea.width; plotHeight: chart.plotArea.height
        minX: timeAxis.min; maxX: timeAxis.max
        pressureMin: pressureAxis.min; pressureMax: pressureAxis.max
        temperatureMin: tempRange.min; temperatureMax: tempRange.max
        weightMin: weightRange.min; weightMax: weightRange.max
        flowMin: pressureAxis.min; flowMax: pressureAxis.max / chart.flowMultiplier
        pressureColor: chart.graphStyle.pressureColor
        flowColor: chart.graphStyle.flowColor
        temperatureColor: chart.graphStyle.temperatureColor
        weightColor: chart.graphStyle.weightColor
        weightFlowColor: chart.graphStyle.weightFlowColor
        resistanceColor: chart.graphStyle.resistanceColor
        conductanceColor: chart.graphStyle.conductanceColor
        darcyResistanceColor: chart.graphStyle.darcyResistanceColor
        temperatureMixColor: chart.graphStyle.temperatureMixColor
        thickLineWidth: chart.graphStyle.scaled(3)
        thinLineWidth: chart.graphStyle.scaled(2)
        showPressure: chart.graphOptions.showPressure
        showFlow: chart.graphOptions.showFlow
        showTemperature: chart.graphOptions.showTemperature
        showWeight: chart.graphOptions.showWeight
        showWeightFlow: chart.graphOptions.showWeightFlow
        showResistance: chart.graphOptions.showResistance
        showConductance: chart.graphOptions.showConductance
        showDarcyResistance: chart.graphOptions.showDarcyResistance
        showTemperatureMix: chart.graphOptions.showTemperatureMix
        advancedMode: chart.advancedMode
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
            property bool isStart: modelData.label === "Start"
            property bool isEnd: modelData.label === "End"

            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y
            height: chart.plotArea.height
            visible: markerTime <= timeAxis.max && markerTime >= 0

            Text {
                id: markerText
                textFormat: Text.PlainText
                text: (markerDelegate.isStart || markerDelegate.isEnd)
                      ? markerDelegate.markerLabel
                      : markerDelegate.markerLabel + (markerDelegate.modelData.transitionSuffix || "")
                font.pixelSize: chart.graphStyle.scaled(18)
                font.bold: markerDelegate.isStart || markerDelegate.isEnd
                color: markerDelegate.isStart ? chart.graphStyle.accentColor : (markerDelegate.isEnd ? chart.graphStyle.stopMarkerColor : Qt.rgba(1, 1, 1, 0.8))
                rotation: -90
                transformOrigin: Item.TopLeft
                x: chart.graphStyle.scaled(4)
                y: chart.graphStyle.scaled(8) + width
                Accessible.ignored: true

                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: chart.graphStyle.scaled(-2)
                    color: Qt.darker(chart.graphStyle.surfaceColor, 1.5)
                    radius: chart.graphStyle.scaled(2)
                }
            }

            // Accessible tap area for End marker - announces weight at stop vs final weight
            GraphEndMarkerArea {
                visible: markerDelegate.isEnd
                x: markerText.x - chart.graphStyle.scaled(10)
                y: markerText.y - markerText.width - chart.graphStyle.scaled(10)
                width: markerText.height + chart.graphStyle.scaled(20)
                height: markerText.width + chart.graphStyle.scaled(20)

                accessibleName: {
                    var stopWeight = chart.graphData.weightAtStop.toFixed(1)
                    var finalWeight = chart.graphData.finalWeight.toFixed(1)
                    var diff = (chart.graphData.finalWeight - chart.graphData.weightAtStop).toFixed(1)
                    return "End marker. Weight at stop: " + stopWeight + " grams. " +
                           "Final settled weight: " + finalWeight + " grams. " +
                           "Drip amount: " + diff + " grams."
                }
                accessibilityItem: markerDelegate
                accessibilityEnabled: chart.accessibilityEnabled
                previouslyAnnounced: chart.lastAnnouncedItem === markerDelegate

                onAccessibleClicked: chart.accessibilityAnnouncementRequested(accessibleName)
                onExplorationRequested: function(announcement, item) {
                    chart.accessibilityExplorationRequested(announcement, item)
                }
                onExplorationCleared: function(item) {
                    chart.accessibilityExplorationCleared(item)
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
                return Math.min(chart.graphData.rawTime, timeAxis.max)
            }

            x: chart.plotArea.x + (markerTime / timeAxis.max) * chart.plotArea.width
            y: chart.plotArea.y + chart.plotArea.height - chart.graphStyle.scaled(4)
            width: Math.max(0, ((nextTime - markerTime) / timeAxis.max) * chart.plotArea.width)
            height: chart.graphStyle.scaled(4)
            color: isFlowMode ? chart.graphStyle.flowColor : chart.graphStyle.pressureColor
            opacity: 0.8
            visible: markerTime <= timeAxis.max && modelData.label !== "Start"
        }
    }

    // Right-axis label column. Shared with the history graph — see GraphRightAxisLabels
    // for why the right axis is hand-drawn rather than a second ValueAxis.
    GraphRightAxisLabelsView {
        style: chart.graphStyle
        x: chart.plotArea.x + chart.plotArea.width + chart.graphStyle.scaled(4)
        y: chart.plotArea.y
        width: chart.width - x
        height: chart.plotArea.height

        mode: chart.rightAxisMode
        weightMin: weightRange.min
        weightMax: weightRange.max
        tempMin: tempRange.min
        tempMax: tempRange.max
        flowMin: flowRange.min
        flowMax: flowRange.max
        onTapped: chart.toggleRightAxis()
    }
}
