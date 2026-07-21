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

    // Resolved catalogue entries ({} when the id is empty or unknown).
    readonly property var _preset: _lookup(Settings.theme.backgroundPresets, presetId)
    readonly property var _pattern: _lookup(Settings.theme.backgroundPatterns, patternId)

    readonly property bool _hasPreset: _preset.id !== undefined
    readonly property bool _hasPattern: _pattern.id !== undefined
    readonly property bool _hasImage: !_hasPreset && imagePath.length > 0

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
        color: root._hasPreset ? root._preset.value : Theme.backgroundColor
        visible: !root._hasImage || bgImage.status !== Image.Ready
    }

    // Subtle pattern above the flat colour. One monochrome asset serves both light and
    // dark because it is tinted with the theme's text colour rather than baked — see
    // BackgroundPresets. sourceSize pins the SVG to its authored tile size so the tiling
    // grid lands on whole pixels; a fractional tile shimmers.
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
            colorizationColor: Theme.textColor
        }
    }

    Image {
        id: bgImage
        anchors.fill: parent
        visible: root._hasImage && status === Image.Ready
        source: root._hasImage ? "file:///" + root.imagePath : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Screen.width
        sourceSize.height: Screen.height
        Accessible.ignored: true
    }
}
