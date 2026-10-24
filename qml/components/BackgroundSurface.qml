pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Decenza

// The one place a custom app background is drawn — a preset colour (with its optional
// subtle pattern) or a background image. Used by ThemedPageBackground for the real page
// background, by BackgroundPickerDialog's tiles, and by LayoutPreview, so the chooser's
// promise ("this is what you will get") is kept by construction rather than by three
// renderings agreeing with each other.
//
// Both inputs default to the live settings; the chooser overrides them to preview a
// candidate that has not been applied yet.
Item {
    id: root

    // "" = no preset. Overridden by the chooser to preview a highlighted tile.
    property string presetId: Settings.theme.backgroundPreset
    // "" = no pattern. An independent axis: any pattern over any colour.
    property string patternId: Settings.theme.backgroundPattern
    // Resolved by the platform settings adapter; empty when no photo is selected.
    property url imageSource: Settings.theme.backgroundImageUrl
    // Whether this surface draws the last shot's chart. Overridden by the chooser to preview
    // an entry that has not been applied.
    property bool shotChart: Settings.theme.backgroundSource === "shot"

    // How strongly the chart is drawn as wallpaper.
    //
    // At full strength it competes with the page instead of sitting behind it: the shot-plan
    // line ("Brew 36.0g of Espresso, using…") crosses the weight ramp and the flow line, and
    // white text over bright hairlines is HARDER to read than white text over a photo — a
    // photo is blurry and low-frequency, a chart is high-contrast and thin. Dimming trades a
    // little of the scale's crispness for text that reads, which is the right way round for
    // something whose job is to be a background.
    //
    // Applied at DRAW time rather than baked into the render, so changing it costs nothing:
    // no re-render, and no opacity term needed in the cache key.
    readonly property real shotChartWallpaperOpacity: 0.55

    // Resolved catalogue entries ({} when the id is empty or unknown).
    readonly property var _preset: _lookup(Settings.theme.backgroundPresets, presetId)
    readonly property var _pattern: _lookup(Settings.theme.backgroundPatterns, patternId)

    readonly property bool _hasPreset: _preset.id !== undefined
    // A shot chart is a native-rendered image-provider output with different scaling and
    // opacity from a photo. It remains in this adapter below; the shared renderer handles
    // only flat fills, patterns, and photo backgrounds.
    // `!_hasPreset` is a safety net, not a nicety. Each source defaults from the LIVE
    // setting so the page background needs no configuration — but that means a caller which
    // sets one input and stays silent about the others INHERITS them, and with a shot
    // background active every colour tile in the chooser drew the last shot instead of its
    // colour. A surface told to show a specific colour must never show something else.
    readonly property bool _hasShotChart: shotChart && !_hasPreset
                                          && LastShotChartSource.imageSource.length > 0
    readonly property bool _hasPattern: _pattern.id !== undefined && !_hasShotChart
    readonly property bool _hasImage: !_hasPreset && (_hasShotChart || String(imageSource).length > 0)

    // The same fact under a public name, because one reader outside this file needs it and
    // reaching for an underscore across a file boundary is how a private becomes an API
    // nobody meant to publish. main.qml binds Theme.shotChartOnCurrentPage to this for the
    // page on top of the stack — see the note there for why the app-wide glass chrome has
    // to ask a PAGE whether a chart is behind it, rather than asking the setting.
    //
    // Deliberately the drawn state and not the stored one: it is false while the render is
    // still pending, false when a preset overrides, and false on a page that suppresses the
    // chart. Those are exactly the cases where the chrome must stay opaque.
    readonly property bool paintsShotChart: _hasShotChart

    // The flat colour actually being painted, and the ink that will read on it.
    //
    // Derived per instance, NOT taken from Theme.textColor. Theme's value follows the
    // APPLIED preset, so in the chooser — where a tile previews a candidate that has not
    // been applied — it is the wrong colour: highlighting a pale colour while a dark theme
    // is still active tinted the pattern white, and white ink on a near-white tile is
    // invisible. Same mistake as the tile captions and the bars: reading a global colour
    // where the surface's own is needed.
    readonly property color surfaceColour: _hasPreset ? _preset.value : Theme.backgroundColor
    readonly property color _patternInk: Theme.contrastColorFor(surfaceColour)

    function _lookup(list, id) {
        if (!id || id.length === 0)
            return ({})
        for (let i = 0; i < list.length; i++) {
            if (list[i].id === id)
                return list[i]
        }
        return ({})
    }

    SharedBackgroundSurface {
        anchors.fill: parent
        baseColor: root.surfaceColour
        patternEnabled: root._hasPattern
        patternSource: root._hasPattern ? root._pattern.asset : ""
        patternInkColor: root._patternInk
        patternTileSize: root._hasPattern ? root._pattern.tile : 0
        patternOpacity: root._hasPattern ? root._pattern.opacity : 0
        photoEnabled: root._hasImage && !root._hasShotChart
        photoSource: root._hasImage && !root._hasShotChart ? root.imageSource : ""
        photoDecodeWidth: Screen.width
        photoDecodeHeight: Screen.height
        onPhotoLoadFailed: function(source) {
            WebDebugLogger.warn("Theme", "BackgroundSurface", ["Background image failed to load:",
                String(source), "- falling back to the theme colour"].join(" "))
        }
    }

    // THE SHOT CHART. Its own element rather than sharing photo rendering,
    // because it was rendered at the surface's own size, so
    // it is stretched rather than cropped (cropping would drop the shot's last seconds off
    // the edge), and it must NOT be given a sourceSize — Qt refuses that on a grabToImage url
    // and warns, and resampling a correctly-sized raster would be pure loss anyway.
    Image {
        id: shotChartImage
        anchors.fill: parent
        visible: root._hasShotChart && status === Image.Ready
        source: root._hasShotChart ? LastShotChartSource.imageSource : ""
        fillMode: Image.Stretch
        opacity: root.shotChartWallpaperOpacity
        asynchronous: true
        Accessible.ignored: true
        // The url is an image-provider handle whose lifetime is the grab result the renderer
        // holds. If that is ever released while a surface is still bound to it, this is the
        // only place it would show — and silently, as a plain background.
        onStatusChanged: if (status === Image.Error)
            WebDebugLogger.warn("Theme", "BackgroundSurface", ["Shot-chart image failed to load:", source,
                         "- falling back to the theme colour"].map(String).join(" "))
    }
}
