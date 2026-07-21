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
    visible: renderWanted
    enabled: false

    // The key the CURRENT image was rendered from. A mismatch against the live key is what
    // triggers a re-render — see the note on cacheKey in LastShotChartSource.
    property string _renderedKey: ""
    // Holds the grab result alive: its `url` is only valid while the object is.
    property var _grab: null

    function _renderIfStale() {
        if (!renderWanted) return
        if (LastShotChartSource.cacheKey === _renderedKey) return
        // Let the chart lay out and paint before grabbing it. Grabbing in the same frame the
        // series were assigned yields a blank or half-drawn image, because the scene graph
        // has not run yet.
        grabTimer.restart()
    }

    onRenderWantedChanged: _renderIfStale()

    readonly property Connections _keyWatch: Connections {
        target: LastShotChartSource
        function onCacheKeyChanged() { renderer._renderIfStale() }
        function onHasShotChanged() { renderer._renderIfStale() }
    }

    // Not a guard or a workaround — the chart genuinely needs frames to draw itself, and
    // there is no "I have finished painting" signal on GraphsView to wait for instead.
    Timer {
        id: grabTimer
        interval: 250
        repeat: false
        onTriggered: {
            if (!renderer.renderWanted) return
            const key = LastShotChartSource.cacheKey
            const ok = renderer.grabToImage(function(result) {
                // `result.image` is a QImage, which is NOT a QML-accessible value type —
                // reading .width off it yields undefined, so a check written against it
                // passes for every result including a failed one. The url is the property
                // QML actually gets, and it is empty exactly when the grab produced nothing.
                if (!result || String(result.url).length === 0) {
                    // Loud on purpose. A silent failure here is a background that simply
                    // never appears, with nothing in the log to say why — and users' AI
                    // assistants read these logs.
                    console.warn("[Background] Shot-chart grab produced no image; "
                                 + "the background falls back to the theme colour")
                    return
                }
                renderer._grab = result
                renderer._renderedKey = key
                LastShotChartSource._renderedUrl = result.url
                console.info("[Background] Shot-chart rendered ->", result.url)
                if (Settings.boolValue("debug/dumpShotChartBackground", false))
                    result.saveToFile(Settings.value("debug/dumpShotChartBackgroundPath", ""))
            })
            if (!ok) {
                console.warn("[Background] Shot-chart grabToImage() was refused — "
                             + "the item has no window or no size")
            }
        }
    }

    HistoryShotGraph {
        // Inset by the app's own chrome, so the axis scale lands in the page's CONTENT area
        // rather than underneath the status and bottom bars. Without this the y labels are
        // readable but the time ticks are drawn behind the bottom bar — a chart with half a
        // scale, which is worse than one with none because it looks like a bug.
        anchors.fill: parent
        anchors.topMargin: Theme.statusBarHeight
        anchors.bottomMargin: Theme.bottomBarHeight
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
