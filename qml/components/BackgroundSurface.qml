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
    // Absolute filesystem path, "" = none. Mutually exclusive with presetId.
    property string imagePath: Settings.theme.backgroundImagePath

    // Resolved catalogue entry ({} when presetId is empty or unknown). Read from
    // Settings for the live case so the colour follows light/dark mode; looked up for a
    // previewed candidate.
    readonly property var _preset: presetId.length === 0
        ? ({})
        : (presetId === Settings.theme.backgroundPreset
            ? Settings.theme.activeBackgroundPreset
            : _lookup(presetId))

    readonly property bool _hasPreset: _preset && _preset.id !== undefined
    readonly property bool _hasImage: !_hasPreset && imagePath.length > 0

    function _lookup(id) {
        var list = Settings.theme.backgroundPresets
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
        color: root._hasPreset ? root._preset.color : Theme.backgroundColor
        visible: !root._hasImage || bgImage.status !== Image.Ready
    }

    // Subtle pattern above the flat colour. One monochrome asset serves both light and
    // dark because it is tinted with the theme's text colour rather than baked — see
    // BackgroundPresets. sourceSize pins the SVG to its authored tile size so the tiling
    // grid lands on whole pixels; a fractional tile shimmers.
    Image {
        id: patternTile
        anchors.fill: parent
        visible: root._hasPreset && root._preset.overlayKind === "tile"
        source: visible ? root._preset.overlayAsset : ""
        fillMode: Image.Tile
        sourceSize.width: root._hasPreset ? root._preset.overlayTile : 0
        sourceSize.height: root._hasPreset ? root._preset.overlayTile : 0
        opacity: root._hasPreset ? root._preset.overlayOpacity : 0
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
