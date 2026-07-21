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

    // Resolved catalogue entries ({} when the id is empty or unknown).
    readonly property var _preset: _lookup(Settings.theme.backgroundPresets, presetId)
    readonly property var _pattern: _lookup(Settings.theme.backgroundPatterns, patternId)

    readonly property bool _hasPreset: _preset.id !== undefined
    // A shot chart, once rendered, IS an image — so it flows down the image path below and
    // inherits its scrim, its decode-in-progress fallback and its stale-source behaviour
    // rather than getting a parallel implementation of each. The chart is drawn by
    // LastShotChartRenderer; nothing here builds one.
    readonly property bool _hasShotChart: shotChart && LastShotChartSource.imageSource.length > 0
    readonly property bool _hasPattern: _pattern.id !== undefined && !_hasShotChart
    readonly property bool _hasImage: !_hasPreset && (_hasShotChart || imagePath.length > 0)
    // Where the image comes from. A grab result carries its own url; a photo is a file path.
    readonly property string _imageSource: _hasShotChart
        ? LastShotChartSource.imageSource
        : (imagePath.length > 0 ? "file:///" + imagePath : "")

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
        visible: !root._hasImage || bgImage.status !== Image.Ready
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

    Image {
        id: bgImage
        anchors.fill: parent
        visible: root._hasImage && status === Image.Ready
        source: root._hasImage ? root._imageSource : ""
        // A photo is cropped to fill; the chart is rendered AT the surface size and must not
        // be cropped, or its last seconds fall off the edge.
        fillMode: root._hasShotChart ? Image.Stretch : Image.PreserveAspectCrop
        asynchronous: true
        // Pinning sourceSize is a decode-cost guard for a multi-megapixel photo. The chart
        // was already rendered at the size it will be drawn at, so pinning it here would
        // resample a correctly-sized raster for nothing.
        sourceSize.width: root._hasShotChart ? 0 : Screen.width
        sourceSize.height: root._hasShotChart ? 0 : Screen.height
        Accessible.ignored: true
    }
}
