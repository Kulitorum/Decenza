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

    // Only exists while a shot background is selected AND there is a shot to draw. Nothing
    // is instantiated, and no chart is built, for users who never choose this background.
    readonly property bool renderWanted: Settings.theme.backgroundSource === "shot"
                                    && LastShotChartSource.hasShot

    width: Math.round(LastShotChartSource._targetWidth)
    height: Math.round(LastShotChartSource._targetHeight)
    x: -width - 100
    y: 0
    // Visible while the SOURCE is a shot chart, not while there happens to be a shot loaded.
    // A refresh briefly clears hasShot, and flipping the item out of the scene and back in
    // right before a grab is the sort of timing this does not need.
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
        _confirmPending = true
        grabTimer.restart()
        confirmTimer.stop()
    }

    onRenderWantedChanged: _renderIfStale()

    readonly property Connections _keyWatch: Connections {
        target: LastShotChartSource
        function onCacheKeyChanged() { renderer._renderIfStale() }
        function onHasShotChanged() { renderer._renderIfStale() }
    }

    // WHEN to take the picture.
    //
    // GraphsView gives no "I have finished drawing" signal, and the work it does after the
    // data lands is proportional to the sample count — HistoryShotGraph reloads on
    // Qt.callLater, then the scene graph builds a geometry node per series. So there is no
    // correct fixed delay: a 250ms one was enough for a 129-sample shot and not enough for a
    // 292-sample one, which is exactly how this presented — the short shot updated, the long
    // one silently kept the previous picture.
    //
    // So take it TWICE and keep the later one. The first grab makes the new shot appear
    // promptly; the confirming grab, long after any plausible build, is the one that is
    // actually guaranteed to be right. Two grabs per shot is nothing — this runs once when a
    // shot ends, not per frame — and it converges rather than relying on a number that has
    // already been wrong once.
    property bool _confirmPending: false

    Timer {
        id: grabTimer
        interval: 300
        repeat: false
        onTriggered: renderer._grabNow()
    }

    Timer {
        id: confirmTimer
        interval: 2000
        repeat: false
        onTriggered: renderer._grabNow()
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
            // Says what the SOURCE held, which is not the same as what the chart had drawn —
            // the gap between those two is the bug this scheme exists to survive. Treat it as
            // "a grab happened for this shot", not as proof the pixels match.
            console.info("[Background] Shot-chart grab ->", result.url,
                         "source shot", LastShotChartSource._shotId,
                         "samples", (LastShotChartSource.shotData.pressure || []).length,
                         "duration", LastShotChartSource.shotData.durationSec,
                         renderer._confirmPending ? "(first)" : "(confirming)")
            if (Settings.boolValue("debug/dumpShotChartBackground", false))
                result.saveToFile(Settings.value("debug/dumpShotChartBackgroundPath", ""))
        })
        if (!ok) {
            console.warn("[Background] Shot-chart grabToImage() was refused — "
                         + "the item has no window or no size")
        }
        if (_confirmPending) {
            _confirmPending = false
            confirmTimer.restart()
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
        showSpines: true

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
