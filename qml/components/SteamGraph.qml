// The right-axis-label Repeater delegate reads this file's ids (`chart`, `rightAxisLabels`);
// Bound makes them statically resolvable. That delegate already declares its one injected
// role, `index`, required, so Bound cannot break role injection here.
pragma ComponentBehavior: Bound

import QtQuick
import QtGraphs
import Decenza

// Outer Item wraps the GraphsView so the FastLineRenderer traces can render as
// siblings on top of the chart. GraphsView swallows scene-graph
// children that aren't its own series/axes — overlays must be siblings, not
// children, to draw above its plot-area background and grid.
Item {
    id: chart

    // Persisted typed properties keep native and paired-browser bindings synchronized.
    property bool showPressure: SteamService.state.showSteamPressure
    property bool showFlow: SteamService.state.showSteamFlow
    property bool showTemperature: SteamService.state.showSteamTemperature

    // Right-axis temperature range. Temperature is a FastLineRenderer, not a series, so
    // there is no series to carry an axis: it maps against these scalars directly and its
    // labels are drawn on the right margin.
    property real tempMin: 100
    property real tempMax: 180

    // Auto-expanding time axis (same pattern as ShotGraph)
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double cachedPlotWidth: 1
    property double _lastAxisMax: 5.0
    property bool _recalcInProgress: false  // Re-entry guard: axis changes trigger plotArea updates

    // The plot rect in THIS item's coordinates, which every sibling overlay below is
    // positioned in. GraphsView.plotArea is in the view's own coordinates, and the view
    // sits below a top margin, so overlays placed from it were drawn that margin too high
    // while the native series were not.
    readonly property rect plotArea: Qt.rect(graphsView.x + graphsView.plotArea.x,
                                             graphsView.y + graphsView.plotArea.y,
                                             graphsView.plotArea.width,
                                             graphsView.plotArea.height)

    Component.onCompleted: {
        SteamService.graph.registerFastSeries(pressureRenderer, flowRenderer, temperatureRenderer)
        recalcMax()
    }

    // Pick a tickInterval that keeps a 5 s warm-up through a 60 s+ steam session
    // readable without leaving a dead zone past the rightmost tick.
    function _niceTimeAxisStep(span) {
        if (span <= 5)  return 1
        if (span <= 10) return 2
        if (span <= 30) return 5
        return 10
    }

    function recalcMax() {
        if (_recalcInProgress) return
        _recalcInProgress = true
        // Track rawTime continuously (no snap-to-tick) so live data reaches the
        // right edge — matches the Qt Charts feel.
        var raw = SteamService.graph.rawTime * cachedPlotWidth / Math.max(1, cachedPlotWidth - paddingPixels)
        var newMax = Math.max(minTime, raw)
        var step = _niceTimeAxisStep(newMax)
        if (newMax !== _lastAxisMax || timeAxis.tickInterval !== step) {
            _lastAxisMax = newMax
            timeAxis.max = newMax
            timeAxis.tickInterval = step
        }
        _recalcInProgress = false
    }

    Connections {
        target: SteamService.graph
        function onRawTimeChanged() { chart.recalcMax() }
    }

    GraphsView {
        id: graphsView
        anchors.fill: parent
        // Reserve room on the right for the manual temperature labels; Qt Graphs
        // doesn't have a sanctioned dual-Y-axis path here and won't carve out
        // right-margin space the way Qt Charts' margins.right did.
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

        // Time axis (X)
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
            titleText: TranslationManager.translate("graph.axis.time", "Time (s)")
        }

        // Pressure/Flow axis (left Y) — steam pressure is typically 0–4 bar.
        // tickInterval 2 reproduces the original tickCount: 4 (labels at 0, 2, 4, 6).
        ValueAxis {
            id: pressureAxis
            min: 0
            max: 6
            tickInterval: 2
            subTickCount: 0
            labelFormat: "%.0f"
            titleText: "bar / mL/s"
        }

        // Flow goal
        LineSeries {
            values: SteamService.graph.flowGoalPoints
            color: Theme.flowGoalColor
            width: Theme.scaled(2)
            strokeStyle: LineSeries.StrokeStyle.DashLine
            dashPattern: [4, 4]
            visible: chart.showFlow
        }
    }

    // === LIVE DATA — FastLineRenderer (pre-allocated VBO) ===

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
        minY: chart.tempMin; maxY: chart.tempMax
        visible: chart.showTemperature
    }

    // Manual right-axis labels for temperature (see tempMin/tempMax)
    Item {
        id: rightAxisLabels
        x: chart.plotArea.x + chart.plotArea.width + Theme.scaled(4)
        y: chart.plotArea.y
        width: chart.width - x
        height: chart.plotArea.height

        Accessible.role: Accessible.StaticText
        Accessible.name: TranslationManager.translate("steamGraph.rightAxis", "Temperature axis")
        Accessible.ignored: true

        Repeater {
            model: 5
            Text {
                required property int index
                property real value: chart.tempMax - index * (chart.tempMax - chart.tempMin) / 4
                text: Theme.cToDisplay(value).toFixed(0)
                x: 0
                y: index / 4 * rightAxisLabels.height - height / 2
                font: Theme.captionFont
                color: Theme.temperatureColor
                Accessible.ignored: true
            }
        }

        Text {
            text: Theme.tempUnitSuffix()
            font: Theme.captionFont
            color: Theme.temperatureColor
            rotation: 90
            transformOrigin: Item.Center
            x: Theme.scaled(24)
            y: rightAxisLabels.height / 2 - height / 2
            Accessible.ignored: true
        }
    }

    // === TAPPABLE LEGEND ===

    CustomLegend {
        id: legend
        x: chart.plotArea.x
        y: chart.plotArea.y + Theme.scaled(4)
        // Content-width (and left-aligned at the plot edge) when it fits; bounded
        // to the plot width so it wraps instead of overflowing when it doesn't.
        width: Math.min(implicitWidth, chart.plotArea.width)

        entries: [
            { label: TranslationManager.translate("steamGraph.legend.pressure", "Pressure"), color: Theme.pressureColor, active: chart.showPressure },
            { label: TranslationManager.translate("steamGraph.legend.flow", "Flow"), color: Theme.flowColor, active: chart.showFlow },
            { label: TranslationManager.translate("steamGraph.legend.temperature", "Temperature"), color: Theme.temperatureColor, active: chart.showTemperature }
        ]

        onEntryToggled: (index, nowActive) => {
            if (index === 0)
                SteamService.request("set-steam-graph-trace",
                                     { trace: "pressure", enabled: nowActive,
                                       expectedEnabled: chart.showPressure },
                                     SteamService.state.controlContextId)
            else if (index === 1)
                SteamService.request("set-steam-graph-trace",
                                     { trace: "flow", enabled: nowActive,
                                       expectedEnabled: chart.showFlow },
                                     SteamService.state.controlContextId)
            else if (index === 2)
                SteamService.request("set-steam-graph-trace",
                                     { trace: "temperature", enabled: nowActive,
                                       expectedEnabled: chart.showTemperature },
                                     SteamService.state.controlContextId)
        }
    }
}
