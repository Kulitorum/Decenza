import QtQuick
import QtGraphs
import Decenza
import "graphs"

// Outer Item wraps the GraphsView so the FastLineRenderer / dashed overlay
// can render as siblings on top of the chart. GraphsView swallows scene-graph
// children that aren't its own series/axes — overlays must be siblings, not
// children, to draw above its plot-area background and grid.
Item {
    id: chart

    // Alias so DashedLineSeries delegates can reach the GraphsView without
    // writing `graphsView: graphsView` — that RHS shadows the delegate's own
    // `graphsView` property (which defaults to `parent`) and resolves to null.
    readonly property alias graphsViewRef: graphsView

    // Persisted visibility toggles (tappable legend). Settings.boolValue() coerces
    // QSettings' INI-backed strings to real booleans; see Settings.h.
    property bool showPressure: Settings.boolValue("steamGraph/showPressure", true)
    property bool showFlow: Settings.boolValue("steamGraph/showFlow", true)
    property bool showTemperature: Settings.boolValue("steamGraph/showTemperature", true)

    // Right-axis temperature range. Qt Graphs lacks a sanctioned dual-Y-axis path;
    // temperature labels are drawn manually on the right margin, and FastLineRenderer
    // maps coordinates against these scalars directly — no ValueAxis needed.
    property real tempMin: 100
    property real tempMax: 180

    // Auto-expanding time axis (same pattern as ShotGraph)
    property double minTime: 5.0
    property double paddingPixels: Theme.scaled(5)
    property double cachedPlotWidth: 1
    property double _lastAxisMax: 5.0
    property bool _recalcInProgress: false  // Re-entry guard: axis changes trigger plotArea updates

    // Convenience pass-through for overlays that need the GraphsView's plot rect.
    readonly property rect plotArea: graphsView.plotArea

    Component.onCompleted: {
        SteamDataModel.registerFastSeries(pressureRenderer, flowRenderer, temperatureRenderer)
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
        var raw = SteamDataModel.rawTime * cachedPlotWidth / Math.max(1, cachedPlotWidth - paddingPixels)
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
        target: SteamDataModel
        function onRawTimeChanged() { chart.recalcMax() }
    }

    // Sync visibility toggles from Settings (e.g., changed on another page).
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "steamGraph/showPressure") chart.showPressure = Settings.boolValue("steamGraph/showPressure", true)
            if (key === "steamGraph/showFlow") chart.showFlow = Settings.boolValue("steamGraph/showFlow", true)
            if (key === "steamGraph/showTemperature") chart.showTemperature = Settings.boolValue("steamGraph/showTemperature", true)
        }
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
            titleText: "s"
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
    }

    // Flow goal (dashed line) — bridge overlay; Qt Graphs LineSeries has no dash style.
    DashedLineSeries {
        graphsView: chart.graphsViewRef
        axisX: timeAxis
        axisY: pressureAxis
        points: SteamDataModel.flowGoalPoints
        strokeColor: Theme.flowGoalColor
        strokeWidth: Theme.scaled(2)
        visible: chart.showFlow
    }

    // === LIVE DATA — FastLineRenderer (pre-allocated VBO) ===

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
        minY: chart.tempMin; maxY: chart.tempMax
        visible: chart.showTemperature
    }

    // Time axis label — inside graph at bottom right
    Text {
        x: graphsView.plotArea.x + graphsView.plotArea.width - width - Theme.spacingSmall
        y: graphsView.plotArea.y + graphsView.plotArea.height - height - Theme.scaled(12)
        text: TranslationManager.translate("graph.axis.time", "Time (s)")
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        opacity: 0.7
        Accessible.ignored: true
    }

    // Manual right-axis labels for temperature (Qt Graphs has no second Y axis here)
    Item {
        id: rightAxisLabels
        x: graphsView.plotArea.x + graphsView.plotArea.width + Theme.scaled(4)
        y: graphsView.plotArea.y
        width: chart.width - x
        height: graphsView.plotArea.height

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
        x: graphsView.plotArea.x
        y: graphsView.plotArea.y + Theme.scaled(4)
        width: implicitWidth

        readonly property var _keys: ["steamGraph/showPressure", "steamGraph/showFlow", "steamGraph/showTemperature"]

        entries: [
            { label: TranslationManager.translate("steamGraph.legend.pressure", "Pressure"), color: Theme.pressureColor, active: chart.showPressure },
            { label: TranslationManager.translate("steamGraph.legend.flow", "Flow"), color: Theme.flowColor, active: chart.showFlow },
            { label: TranslationManager.translate("steamGraph.legend.temperature", "Temperature"), color: Theme.temperatureColor, active: chart.showTemperature }
        ]

        onEntryToggled: (index, nowActive) => {
            Settings.setValue(_keys[index], nowActive)
            // Direct update for immediate feedback
            if (index === 0) chart.showPressure = nowActive
            else if (index === 1) chart.showFlow = nowActive
            else chart.showTemperature = nowActive
        }
    }
}
