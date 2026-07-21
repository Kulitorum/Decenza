import QtQuick
import Decenza

// Renders the last shot's chart to an image, once per change, for use as the app background
// (add-last-shot-chart-background). Instantiated ONCE, in main.qml, because grabToImage()
// needs a live scene graph and a window — an item that has never been rendered grabs empty.
//
// It is deliberately a visual Item rather than part of the LastShotChartSource singleton: a
// QtObject singleton has no place in the scene, and without a place in the scene there is
// nothing to grab.
//
// Positioned outside the window rather than hidden. `visible: false` items are not rendered
// at all, so grabbing one returns nothing; moving it off to the left keeps it in the scene
// graph while keeping it off the user's screen. It also carries no input handlers, so there
// is nothing to intercept touches even for the instant it exists.
Item {
    id: renderer

    // Gates the GRAB, and (via `visible`) per-frame rendering. It does not gate
    // construction: main.qml instantiates this unconditionally and the chart is a direct
    // child, so both exist for every user. See the Loader note in main.qml.
    readonly property bool renderWanted: Settings.theme.backgroundSource === "shot"
                                    && LastShotChartSource.hasShot

    width: Math.round(LastShotChartSource._targetWidth)
    height: Math.round(LastShotChartSource._targetHeight)

    // Parked outside the window. This was briefly moved inside on a theory that an
    // offscreen item gets culled and grabToImage then photographs stale scene-graph nodes —
    // which would have explained a frozen background perfectly. It was wrong: dumps taken
    // either side of a shot, with the item offscreen, differ from each other and match the
    // shot they should. Moved back, because being inside the window means rendering a
    // full-size chart behind every page on every frame, which is the cost this whole design
    // exists to avoid.
    x: -width - 100
    y: 0
    visible: Settings.theme.backgroundSource === "shot"
    enabled: false

    // The key the CURRENT image was rendered from. A mismatch against the live key is what
    // triggers a re-render — see the note on cacheKey in LastShotChartSource.
    property string _renderedKey: ""
    // Holds the grab result alive: its `url` is only valid while the object is.
    property var _grab: null

    function _renderIfStale() {
        if (!renderWanted) return
        if (LastShotChartSource.cacheKey === _renderedKey) return
        _framesUntilGrab = 2
    }

    onRenderWantedChanged: {
        if (renderWanted) {
            _renderIfStale()
        } else if (Settings.theme.backgroundSource !== "shot") {
            // Switching away from the shot chart: drop the grab. It is a full-window-sized
            // image and nothing draws it any more; holding it for the life of the process
            // was several megabytes resident for a background the user turned off.
            _grab = null
            _renderedKey = ""
            LastShotChartSource._renderedUrl = ""
        }
    }

    readonly property Connections _keyWatch: Connections {
        target: LastShotChartSource
        function onCacheKeyChanged() { renderer._renderIfStale() }
        function onHasShotChanged() { renderer._renderIfStale() }
    }

    // WHEN to take the picture.
    //
    // Not on a timer. GraphsView reloads on Qt.callLater and then builds a geometry node per
    // series, so the work after the data lands scales with the sample count — a 250ms delay
    // cleared a 129-sample shot and did not clear a 292-sample one, which is precisely the
    // "fragile heuristic that breaks on slow devices" the project rules warn about. There is
    // no delay that is correct, because the thing being waited for is not a duration.
    //
    // The event that IS available: the window tells us when it has finished drawing a frame.
    // Waiting on that adapts to whatever the chart costs — a heavier shot simply takes a
    // longer frame, and the signal still arrives only once the work is done.
    //
    // TWO frames, not one, because a frame may already have been in flight when the data
    // changed; its completion says nothing about our new geometry. The second is the first
    // one guaranteed to have begun afterwards.
    //
    // FrameAnimation, NOT QQuickWindow::frameSwapped. frameSwapped is emitted on the
    // RENDERING thread under the threaded render loop, so a QML handler attached to it runs
    // there — and calling grabToImage from the render thread is asking for a picture of a
    // frame that is in the middle of being built. FrameAnimation ticks once per frame on the
    // GUI thread, which is the same event with a safe place to stand.
    property int _framesUntilGrab: 0

    FrameAnimation {
        // Idle unless a grab is pending: this fires every frame and has no business waking
        // for any of the others.
        running: renderer._framesUntilGrab > 0
        onTriggered: {
            if (renderer._framesUntilGrab <= 0) return
            renderer._framesUntilGrab -= 1
            if (renderer._framesUntilGrab === 0)
                renderer._grabNow()
        }
    }

    function _grabNow() {
        if (!renderWanted) return
        const key = LastShotChartSource.cacheKey
        const ok = renderer.grabToImage(function(result) {
            // `result.image` is a QImage, which is NOT a QML-accessible value type — reading
            // .width off it yields undefined, so a check written against it passes for every
            // result including a failed one. The url is the property QML actually gets, and
            // it is empty exactly when the grab produced nothing.
            if (!result || String(result.url).length === 0) {
                // Loud on purpose. A silent failure here is a background that simply never
                // appears, with nothing in the log to say why — and users' AI assistants
                // read these logs.
                console.warn("[Background] Shot-chart grab produced no image; "
                             + "the background falls back to the theme colour")
                return
            }
            renderer._grab = result
            renderer._renderedKey = key
            LastShotChartSource._renderedUrl = result.url
            // Reports what the CHART was fed, not what the source holds, and still not the
            // pixels — only an image dump is that. Worth stating because the earlier version
            // logged the source, which meant it agreed with itself no matter what was drawn:
            // "a grab happened for shot N" was read as "the picture is of shot N" twice
            // during development, and only dumping the actual image settled it.
            console.info("[Background] Shot-chart grab ->", result.url,
                         "chart samples", backgroundChart.pressureData.length,
                         "maxTime", backgroundChart.maxTime,
                         "| source shot", LastShotChartSource._shotId,
                         "samples", (LastShotChartSource.shotData.pressure || []).length)
        })
        if (!ok) {
            console.warn("[Background] Shot-chart grabToImage() was refused — "
                         + "the item has no window or no size")
        }
    }

    HistoryShotGraph {
        id: backgroundChart

        // The chart reloads itself from onPressureDataChanged via Qt.callLater, so the data
        // reaching the SOURCE and the chart actually holding it are two different moments.
        // Starting the grab timer from the cache key alone timed it from the first of those,
        // which is how a refresh could capture the chart as it was BEFORE the new shot —
        // and then record the new key against it, so it never corrected itself.
        onPressureDataChanged: renderer._renderIfStale()

        // Inset by the app's own chrome, so the axis scale lands in the page's CONTENT area
        // rather than underneath the status and bottom bars. Without this the y labels are
        // readable but the time ticks are drawn behind the bottom bar — a chart with half a
        // scale, which is worse than one with none because it looks like a bug.
        anchors.fill: parent
        anchors.topMargin: Theme.statusBarHeight
        // TWICE the bottom bar, not once. The bottom of a page is two rows deep — the bar
        // itself plus the readouts strip above it (Profile / Ratio / Beans / Milk / Grind) —
        // and the x-axis scale is drawn BELOW the plot, so clearing only the bar left the
        // time ticks sitting among the readout labels where they were unreadable. The y
        // scales were fine throughout, which is why this looked like "the horizontal one is
        // missing" rather than "the inset is too small".
        //
        // A heuristic about the default idle layout, and deliberately so: the page's content
        // is user-arrangeable, so there is no exact rect to ask for. Erring large costs some
        // chart height and keeps the scale legible, which is the trade this feature wants.
        anchors.bottomMargin: Theme.bottomBarHeight * 2
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        enabled: false

        // Axis labels and spines stay ON. They were off in the first pass, on the theory
        // that wallpaper wants no furniture — but a chart with no scale is a picture of some
        // squiggles: you cannot tell 9 bar from 4, or a 25-second shot from a 45-second one.
        // The scale is what makes it worth looking at.
        //
        // Phase labels stay off: they are per-frame annotations for inspecting one shot, and
        // they are the part that genuinely does clutter a full-screen background.
        showLabels: true
        showPhaseLabels: false

        // The chosen ENTRY decides this, not shotReview/advancedMode — that toggle is for
        // inspecting one shot and must not repaint the app.
        advancedMode: Settings.theme.backgroundShotAdvanced

        // The SAME set the review page feeds it, deliberately complete. An earlier version
        // passed only the eight obvious series and left out maxTime, so every shot drew
        // against a 60-second axis and a 26-second shot used the left half of the wallpaper.
        pressureData: LastShotChartSource.shotData.pressure || []
        flowData: LastShotChartSource.shotData.flow || []
        temperatureData: LastShotChartSource.shotData.temperature || []
        weightData: LastShotChartSource.shotData.weight || []
        weightFlowRateData: LastShotChartSource.shotData.weightFlowRate || []
        resistanceData: LastShotChartSource.shotData.resistance || []
        conductanceData: LastShotChartSource.shotData.conductance || []
        darcyResistanceData: LastShotChartSource.shotData.darcyResistance || []
        conductanceDerivativeData: LastShotChartSource.shotData.conductanceDerivative || []
        temperatureMixData: LastShotChartSource.shotData.temperatureMix || []
        pressureGoalData: LastShotChartSource.shotData.pressureGoal || []
        flowGoalData: LastShotChartSource.shotData.flowGoal || []
        temperatureGoalData: LastShotChartSource.shotData.temperatureGoal || []
        temperatureMixGoalData: LastShotChartSource.shotData.temperatureMixGoal || []
        phaseMarkers: LastShotChartSource.shotData.phases || []
        maxTime: LastShotChartSource.shotData.durationSec || 60
    }
}
