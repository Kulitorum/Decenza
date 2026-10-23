pragma Singleton

import QtQuick
import Decenza

// The last shot's chart, as a background — one loaded shot and one rendered image for the
// whole app (add-last-shot-chart-background).
//
// WHY A SINGLETON. BackgroundSurface is instantiated once per page, once per chooser tile
// and once in the chooser's preview. If each of those loaded the shot, the app would query
// SQLite several times to draw the same picture; if each of them rendered the chart, it
// would run a GraphsView behind every page on a tablet. Both are held here instead.
//
// WHY AN IMAGE. The chart is static between shots — nothing on it moves while the machine
// sits idle — so it is rendered ONCE into an image and blitted from then on. Steady-state
// cost is one texture, the same as the photo background, and everything downstream of the
// grab is an ordinary Image: inert by construction, with no handler to forget to omit.
//
// What that trades away is freshness, and that is the real hazard here. A cached render is
// only correct until something it was drawn from changes — see `cacheKey`.
QtObject {
    id: root

    // --- What the app reads --------------------------------------------------

    // The rendered chart, or "" when there is nothing to draw. BackgroundSurface feeds this
    // straight into the same Image path a background photo takes.
    readonly property string imageSource: _renderedUrl

    // Distinguishes "no shot exists" from "not fetched yet", so the fallback does not flash
    // on a cold start: nothing is drawn until we know there is nothing to draw.
    readonly property bool ready: _loadState !== "loading"
    readonly property bool hasShot: _loadState === "loaded" && _shotId > 0

    // Set by the page background so the render matches the surface it will fill.
    property real targetWidth: 0
    property real targetHeight: 0

    // --- What the cache key covers -------------------------------------------
    //
    // A cached image is only valid while the things it was drawn from hold still. Keying on
    // them and re-rendering on a mismatch is deliberate: the alternative is remembering to
    // invalidate at each of the places below, and the one that gets forgotten is the theme,
    // whose symptom is the PREVIOUS theme's curve colours behind the current theme. That
    // does not look like a bug — it looks like a colour you half-remember choosing.
    //
    // Theme colors, displayed temperature units, graph visibility and font roles are
    // part of the key. Theme.scale is intentionally excluded: it carries
    // pageScaleMultiplier, which changes with navigation, so including it would re-render
    // the wallpaper every time a page opened. Stroke widths and label sizes therefore
    // reflect the scale at grab time.
    //
    // The UI font family and effective role sizes are included because a language/font
    // change can repaint axis labels at different metrics.
    //
    // Theme NAME is not part of the key: the active palette and dark mode fully identify
    // the colors the graph renders.
    // Rounded sizes so a one-pixel resize does not trigger a re-render.
    readonly property string cacheKey: [
        _shotId, _dataRevision,
        Settings.theme.backgroundShotAdvanced ? "adv" : "basic",
        _themeKey,
        _visibilityKey,
        Settings.app.temperatureUnit,
        JSON.stringify(Theme.fontFamilies),
        JSON.stringify(Settings.theme.effectiveFontSizes),
        Math.round(_targetWidth), Math.round(_targetHeight)
    ].join("|")

    // The palette the curves and axes are drawn from. The theme NAME is not enough: editing
    // a colour in the theme editor repaints the chart without changing which theme is
    // active, so the palette itself is part of the identity.
    readonly property string _themeKey:
        Settings.theme.isDarkMode + "/" + JSON.stringify(Settings.theme.customThemeColors)

    readonly property real _targetWidth: targetWidth > 0 ? targetWidth : 1280
    readonly property real _targetHeight: targetHeight > 0 ? targetHeight : 800

    // Identifies the current curve set, so a cached render is discarded when it no longer
    // matches what the graph would draw.
    //
    // This is now a plain binding: every input is a SettingsGraph Q_PROPERTY, so the
    // dependencies are recorded and it re-evaluates by itself. It used to be a property
    // recomputed from Settings.valueChanged against a hand-maintained list of keys — because
    // the old Settings.boolValue() was a Q_INVOKABLE and a binding over it recorded no
    // dependency at all. That list had already shipped one entry short (showWeightAxis), and
    // the symptom of an omission here is invisible: the chart simply keeps drawing the
    // previous image. Deriving the set from GraphSeries removes the omission entirely.
    //
    // The right-axis mode and flow multiplier join the curve flags because both change what
    // is drawn, not merely how it is labelled.
    readonly property string _visibilityKey: {
        var parts = []
        var entries = GraphSeries.entries
        var hasPortal = shotData.portalSamples && shotData.portalSamples.length > 0
        for (let i = 0; i < entries.length; i++) {
            if (entries[i].portal && !hasPortal) continue
            parts.push(Settings.graph[entries[i].key] ? "1" : "0")
        }
        parts.push(Settings.graph.rightAxisMode)
        parts.push(String(Settings.graph.flowMultiplier))
        return parts.join("")
    }

    // --- Internal state ------------------------------------------------------

    property string _loadState: "loading"   // "loading" | "loaded" | "empty"
    property var shotData: ({})
    property real _shotId: 0
    property string _renderedUrl: ""

    property string _pendingMostRecentRequestId: ""
    property string _pendingShotRequestId: ""
    property bool _loadRequested: false
    property int _dataRevision: 0

    function _clearShot() {
        _shotId = 0
        shotData = ({})
        _renderedUrl = ""
    }

    function _refresh() {
        _loadRequested = true
        _pendingMostRecentRequestId = ""
        _pendingShotRequestId = ""
        _clearShot()
        _loadState = "loading"
        if (HistoryService.ready)
            _pendingMostRecentRequestId = HistoryService.requestMostRecentShotId()
    }

    // Several facade notifications can describe the same revision. Binding to the
    // state avoids duplicate loads while still invalidating requests across reconnects.
    readonly property string historyStateKey: HistoryService.ready + "/" + HistoryService.historyRevision
    onHistoryStateKeyChanged: {
        if (!HistoryService.ready) {
            _pendingMostRecentRequestId = ""
            _pendingShotRequestId = ""
            _clearShot()
            _loadState = "loading"
        } else if (shotBackgroundSelected || _loadRequested) {
            _refresh()
        }
    }

    readonly property Connections _historyWatch: Connections {
        target: HistoryService

        // The facade also emits legacy shotReady signals for other callers. Only
        // our own correlated completions may update the background.
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === root._pendingMostRecentRequestId
                    && operation === "mostRecentShotId") {
                root._pendingMostRecentRequestId = ""
                const shotId = success ? Number(result.shotId) : 0
                if (shotId > 0) {
                    root._shotId = shotId
                    root._pendingShotRequestId = HistoryService.requestShot(shotId)
                    return
                }
                if (!success) {
                    WebDebugLogger.warn("Theme", "LastShotChartSource",
                                        "Could not load latest shot: " + reason)
                }
                root._clearShot()
                root._loadState = "empty"
                root._loadRequested = false
            } else if (requestId === root._pendingShotRequestId && operation === "shot") {
                root._pendingShotRequestId = ""
                const shot = success && Number(result.shotId) === root._shotId ? result.shot : null
                // Deletion between lookup and load, or a shot without samples,
                // must fall back to the theme instead of drawing an empty grid.
                if (!shot || (shot.pressure || []).length === 0) {
                    WebDebugLogger.warn("Theme", "LastShotChartSource",
                                        "Could not draw latest shot " + root._shotId + ": "
                                        + (reason || "no samples") + ". Falling back to the theme colour.")
                    root._clearShot()
                    root._loadState = "empty"
                } else {
                    root.shotData = shot
                    root._dataRevision += 1
                    root._loadState = "loaded"
                }
                root._loadRequested = false
            }
        }
    }

    // Only touch the database when this background is actually selected. The singleton is
    // instantiated as soon as anything reads it — including the chooser — so an unguarded
    // load meant two SQLite queries at startup for every user, and two more on every shot,
    // for a feature they may never turn on.
    // No leading underscore: a property named `_wanted` has no usable change-handler name
    // (it would be `on_wantedChanged`), so the handler below silently never fires. qmllint
    // catches it; the same trap already cost a round trip in the renderer.
    readonly property bool shotBackgroundSelected: Settings.theme.backgroundSource === "shot"

    onShotBackgroundSelectedChanged: {
        if (shotBackgroundSelected) {
            // Unconditionally, not "only if not already loaded": shots saved while another
            // background was active were ignored by the history watcher, so the held
            // shot is stale on the way back. _refresh() is idempotent.
            _refresh()
        } else {
            // The renderer is behind a Loader keyed on this same selection, so it is
            // destroyed on deselect and its own cleanup branch never runs. The grab url it
            // left in _renderedUrl now points at a dead image provider; clearing it here
            // stops BackgroundSurface binding the dead handle on the next selection.
            _renderedUrl = ""
        }
    }

    // Load the shot even when this background is not the active one, so the chooser can show
    // "you have no shots" vs "apply to see it" rather than two blank tiles. Does not cause a
    // render — the renderer's Loader stays inactive until the background is actually chosen.
    function ensureLoaded() {
        if (_loadState !== "loaded")
            _refresh()
    }

    Component.onCompleted: {
        if (shotBackgroundSelected)
            _refresh()
    }
}
