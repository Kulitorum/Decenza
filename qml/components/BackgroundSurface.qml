// Binds file-scope ids inside nested components, so the `root._patternInk` reference from
// the layer.effect below is statically resolved rather than relying on the looser default
// lookup. Safe here: the only nested component in this file is that effect.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects
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
    // Absolute filesystem path, "" = none. Mutually exclusive with presetId.
    property string imagePath: Settings.theme.backgroundImagePath
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
    // A shot chart, once rendered, IS an image — so it flows down the image path below and
    // inherits its scrim, its decode-in-progress fallback and its stale-source behaviour
    // rather than getting a parallel implementation of each. The chart is drawn by
    // LastShotChartRenderer; nothing here builds one.
    // `!_hasPreset` is a safety net, not a nicety. Each source defaults from the LIVE
    // setting so the page background needs no configuration — but that means a caller which
    // sets one input and stays silent about the others INHERITS them, and with a shot
    // background active every colour tile in the chooser drew the last shot instead of its
    // colour. A surface told to show a specific colour must never show something else.
    readonly property bool _hasShotChart: shotChart && !_hasPreset
                                          && LastShotChartSource.imageSource.length > 0
    readonly property bool _hasPattern: _pattern.id !== undefined && !_hasShotChart
    readonly property bool _hasImage: !_hasPreset && (_hasShotChart || imagePath.length > 0)

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
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === id)
                return list[i]
        }
        return ({})
    }

    // Flat fill. Falls back to Theme.backgroundColor, which is what every page painted
    // before backgrounds existed — and is also what shows while an image decodes, and if
    // a stored image path no longer resolves to a readable file.
    Rectangle {
        anchors.fill: parent
        color: root.surfaceColour
        // Also shows while EITHER image is still decoding, which is what makes the fallback
        // a fallback: the flat colour holds the page until there is something to draw.
        //
        // And it stays UNDER the shot chart permanently, because that chart is drawn
        // translucent (see shotChartWallpaperOpacity) and needs a page colour to sit on
        // rather than the bare window.
        visible: !root._hasImage
                 || root._hasShotChart
                 || bgImage.status !== Image.Ready
    }

    // Pattern above the flat colour. One monochrome asset serves every colour because it
    // is tinted at runtime rather than baked — light ink on a dark surface, dark ink on a
    // light one. sourceSize pins the SVG to its authored tile size so the tiling grid lands
    // on whole pixels; a fractional tile shimmers.
    Image {
        id: patternTile
        anchors.fill: parent
        // Drawn over whatever flat colour is showing — a preset, or the theme's own
        // background when no preset is set. Never over an image, where it would fight the
        // photo rather than texture a surface.
        visible: root._hasPattern && !root._hasImage
        source: visible ? root._pattern.asset : ""
        fillMode: Image.Tile
        sourceSize.width: root._hasPattern ? root._pattern.tile : 0
        sourceSize.height: root._hasPattern ? root._pattern.tile : 0
        opacity: root._hasPattern ? root._pattern.opacity : 0
        asynchronous: true
        cache: true
        Accessible.ignored: true

        // Same colorization ThemedIcon uses for monochrome SVGs. The layer is a
        // screen-sized FBO, but this content is static — it is rasterised once and
        // costs nothing per frame.
        layer.enabled: visible
        layer.effect: MultiEffect {
            colorization: 1.0
            colorizationColor: root._patternInk
        }
    }

    // A PHOTO. sourceSize caps decode cost for a multi-megapixel file, and
    // PreserveAspectCrop fills the surface without distorting it.
    Image {
        id: bgImage
        anchors.fill: parent
        visible: root._hasImage && !root._hasShotChart && status === Image.Ready
        source: root._hasImage && !root._hasShotChart ? "file:///" + root.imagePath : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Screen.width
        sourceSize.height: Screen.height
        Accessible.ignored: true
    }

    // THE SHOT CHART. Its own element rather than a pile of ternaries on the one above,
    // because every property it wants differs: it was rendered at the surface's own size, so
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
    }
}
