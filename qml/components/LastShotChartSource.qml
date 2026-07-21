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
    // NOT covered, and deliberately named rather than left for someone to discover:
    //   - Settings.app.temperatureUnit. The right-hand axis renders through
    //     Theme.cToDisplay/tempUnitSuffix, so a °C↔°F switch leaves stale numbers and a
    //     stale suffix on the wallpaper until the next re-render.
    //   - Theme.scale. It carries pageScaleMultiplier, which changes with NAVIGATION, so
    //     keying on it would re-render the wallpaper every time you opened a page. Stroke
    //     widths and label sizes therefore reflect whichever page was active at grab time.
    //   - The UI font family. A language change into a locale served by a platform fallback
    //     repaints the axis labels at different metrics.
    // All three are cosmetic and self-correct at the next shot. The header used to read
    // "Everything the render depends on", which is exactly the kind of claim that stops the
    // next reader from checking.
    //
    // Rounded sizes so a one-pixel resize does not trigger a re-render.
    readonly property string cacheKey: [
        _shotId,
        Settings.theme.backgroundShotAdvanced ? "adv" : "basic",
        _themeKey,
        _visibilityKey,
        Math.round(_targetWidth), Math.round(_targetHeight)
    ].join("|")

    // The palette the curves and axes are drawn from. The theme NAME is not enough: editing
    // a colour in the theme editor repaints the chart without changing which theme is
    // active, so the palette itself is part of the identity.
    readonly property string _themeKey:
        Settings.theme.activeThemeName + "/" + Settings.theme.isDarkMode + "/"
        + JSON.stringify(Settings.theme.customThemeColors)

    readonly property real _targetWidth: targetWidth > 0 ? targetWidth : 1280
    readonly property real _targetHeight: targetHeight > 0 ? targetHeight : 800

    // The curve set, as a plain property rather than a binding over Settings.boolValue().
    //
    // boolValue() is a Q_INVOKABLE, so a binding that calls it records NO dependency and
    // would never re-evaluate when a curve is toggled — the key would stay put and the app
    // would keep drawing the old chart. This is the exact trap the project notes warn about,
    // and here its symptom is invisible rather than merely wrong, so the value is recomputed
    // from the Settings.valueChanged signal instead.
    property string _visibilityKey: ""

    // Named individually rather than hashed over a prefix, so a curve added later fails
    // visibly — a missing entry here is a chart that silently stops updating. Paired with
    // its default, because reading the wrong default flips a bit in the key and forces one
    // needless re-render on first run.
    //
    // tst_backgroundpresets compares this list against the settings HistoryShotGraph
    // actually reads, and it earned that on its first run: showWeightAxis was missing here,
    // so toggling the right-hand axis would have left the previous render in place.
    readonly property var _visibilityKeys: [
        { key: "graph/showPressure",              dflt: true  },
        { key: "graph/showFlow",                  dflt: true  },
        { key: "graph/showTemperature",           dflt: true  },
        { key: "graph/showWeight",                dflt: true  },
        { key: "graph/showWeightFlow",            dflt: true  },
        { key: "graph/showWeightAxis",            dflt: true  },
        { key: "graph/showResistance",            dflt: false },
        { key: "graph/showConductance",           dflt: false },
        { key: "graph/showConductanceDerivative", dflt: false },
        { key: "graph/showDarcyResistance",       dflt: false },
        { key: "graph/showTemperatureMix",        dflt: false },
        { key: "graph/showTemperatureMixGoal",    dflt: false }
    ]

    function _computeVisibilityKey() {
        var parts = []
        for (var i = 0; i < _visibilityKeys.length; i++)
            parts.push(Settings.boolValue(_visibilityKeys[i].key, _visibilityKeys[i].dflt) ? "1" : "0")
        _visibilityKey = parts.join("")
    }

    function _watchesKey(key) {
        for (var i = 0; i < _visibilityKeys.length; i++) {
            if (_visibilityKeys[i].key === key)
                return true
        }
        return false
    }

    readonly property Connections _settingsWatch: Connections {
        target: Settings
        function onValueChanged(key) {
            if (root._watchesKey(key))
                root._computeVisibilityKey()
        }
    }

    // --- Internal state ------------------------------------------------------

    property string _loadState: "loading"   // "loading" | "loaded" | "empty"
    property var shotData: ({})
    property real _shotId: 0
    property string _renderedUrl: ""

    readonly property var _storage: MainController.shotHistory

    function _refresh() {
        _loadState = "loading"
        if (!_storage) {
            console.warn("[Background] No shot storage — the last-shot background cannot load")
            _loadState = "empty"
            return
        }
        _storage.requestMostRecentShotId()
    }

    // The shot the chart is drawn from, and the events that change it.
    readonly property Connections _storageWatch: Connections {
        target: root._storage

        // 0 and -1 are NOT the same answer. ShotHistoryStorage emits -1 for "storage is not
        // ready" and 0 for "there are genuinely no shots". Collapsing them told a user with
        // eight hundred shots and an unopenable database "No shots yet — this fills in after
        // your first shot", which is not merely unhelpful, it is false, and it sends whoever
        // reads the log next in precisely the wrong direction.
        function onMostRecentShotIdReady(shotId) {
            if (shotId > 0) {
                root._shotId = shotId
                root._storage.requestShot(shotId)
                return
            }
            if (shotId < 0) {
                console.warn("[Background] Shot history is not available (storage not ready) — "
                             + "keeping the current background; it will retry when a shot is saved")
                return   // do NOT wipe the render, and do NOT claim the history is empty
            }
            console.info("[Background] No shots in history — the last-shot background falls "
                         + "back to the theme colour")
            root._shotId = 0
            root.shotData = ({})
            root._renderedUrl = ""
            root._loadState = "empty"
        }

        function onShotReady(id, shot) {
            if (id !== root._shotId) return
            // A shot deleted between the id query and this load still emits, carrying a
            // default projection. Accepting it drew an empty 60-second grid — axes, no
            // curves — which reads as a design choice rather than a failure.
            if (!shot || (shot.pressure || []).length === 0) {
                console.warn("[Background] Shot", id, "loaded with no samples; it was probably "
                             + "removed between the lookup and the load. Falling back to the "
                             + "theme colour.")
                root.shotData = ({})
                root._renderedUrl = ""
                root._loadState = "empty"
                return
            }
            root.shotData = shot
            root._loadState = "loaded"
        }

        // A new shot is the point of the feature. A deleted one matters only when it is the
        // one being drawn — otherwise the picture on screen is still correct.
        function onShotSaved(shotId) {
            if (shotId > 0)
                root._refresh()
        }
        // Without this, none of the not-ready states above can ever recover: a user who
        // pulls no shot stays on a fallback background until the app restarts.
        function onReadyChanged() {
            if (root._storage.isReady)
                root._refresh()
        }

        function onShotsDeleted(shotIds) {
            for (var i = 0; i < shotIds.length; i++) {
                if (Number(shotIds[i]) === Number(root._shotId)) {
                    root._refresh()
                    return
                }
            }
        }
    }

    Component.onCompleted: {
        _computeVisibilityKey()
        _refresh()
    }
}
