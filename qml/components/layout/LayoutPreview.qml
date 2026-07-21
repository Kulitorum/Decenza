import QtQuick
import QtQuick.Layouts
import Decenza
import ".."

// Live, scaled-down preview of the home screen built from the current layout
// configuration. Mirrors the real StatusBar.qml + IdlePage.qml structure (same
// zone components, margins, offsets, colors, and the lower-mid-bar height gate)
// at the device reference size (960x600), then scales to fit, so what you see
// matches what ships.
//
// Reactivity: the C++ getters (getZoneItems/getZoneOption/...) take only a zone
// string, so a QML binding that calls them gains NO dependency on the layout
// config and would never re-evaluate. Each helper therefore reads `_cfg` into an
// unused local `d` purely to register a binding dependency on
// `layoutConfiguration`; that `var d = _cfg` line is load-bearing — do not
// "clean it up" or the preview stops updating when the layout changes.
Item {
    id: previewRoot
    clip: true

    // Background shown behind the mockup — a preset id or an image path. Both
    // default to the real saved setting so every plain `LayoutPreview {}` (e.g.
    // SettingsLayoutTab's layout-only preview) automatically matches what the
    // actual idle screen looks like. BackgroundPickerDialog overrides them with
    // the currently highlighted (not yet saved) candidate while picking — an
    // explicit binding at the instantiation site always wins over this default.
    property string backgroundImageSource: Settings.theme.backgroundImagePath
    property string backgroundPresetSource: Settings.theme.backgroundPreset
    property string backgroundPatternSource: Settings.theme.backgroundPattern

    readonly property var _cfg: Settings.network.layoutConfiguration
    function _items(z) { var d = _cfg; return Settings.network.getZoneItems(z) }   // d: dependency tap, keep
    function _scale(z) { var d = _cfg; return Settings.network.getZoneScale(z) }   // d: dependency tap, keep
    function _offset(z) { var d = _cfg; return Settings.network.getZoneYOffset(z) } // d: dependency tap, keep
    function _opt(z, k, dv) { var d = _cfg; return Settings.network.getZoneOption(z, k, dv) } // d: dependency tap, keep

    Item {
        id: canvas
        width: Theme.scaled(960)
        height: Theme.scaled(600)
        anchors.centerIn: parent
        scale: Math.min(parent.width / Math.max(1, width),
                        parent.height / Math.max(1, height))
        transformOrigin: Item.Center

        // Same renderer the real page background uses, so the preview cannot drift
        // from the result.
        BackgroundSurface {
            anchors.fill: parent
            presetId: previewRoot.backgroundPresetSource
            patternId: previewRoot.backgroundPatternSource
            imagePath: previewRoot.backgroundImageSource
        }

        // ---- Status bar (StatusBar.qml) ----
        Rectangle {
            id: previewStatusBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.statusBarHeight
            property string _style: previewRoot._opt("statusBar", "style", "standard")
            property color _opaqueColor: _style !== "standard" ? Theme.zoneBackgroundColor(_style) : Theme.surfaceColor
            // Mirrors StatusBar.qml's scrim so the preview matches what ships. Keyed on
            // Theme.glassChrome like the real bar — an image-path test here meant the
            // preview ignored the glass switch and the colour presets entirely.
            color: Theme.glassChrome ? Theme.chromeFill(_opaqueColor) : _opaqueColor

            LayoutBarZone {
                anchors.fill: parent
                anchors.leftMargin: Theme.chartMarginSmall
                anchors.rightMargin: Theme.spacingLarge
                zoneName: "statusBar"
                items: previewRoot._items("statusBar")
                distribution: previewRoot._opt("statusBar", "distribution", "packed")
                alignment: previewRoot._opt("statusBar", "alignment", "center")
                zoneStyle: previewStatusBar._style
            }
        }

        // ---- Page area below the status bar (IdlePage coordinate space) ----
        Item {
            id: pageArea
            anchors.top: previewStatusBar.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right

            // Top info section (topLeft / topRight)
            ColumnLayout {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.standardMargin
                anchors.topMargin: Theme.pageTopMargin
                spacing: Theme.scaled(20)

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.scaled(50)

                    LayoutBarZone {
                        zoneName: "topLeft"
                        items: previewRoot._items("topLeft")
                        distribution: previewRoot._opt("topLeft", "distribution", "packed")
                        alignment: previewRoot._opt("topLeft", "alignment", "center")
                        zoneStyle: previewRoot._opt("topLeft", "style", "standard")
                        itemSize: previewRoot._opt("topLeft", "itemSize", "compact")
                    }
                    Item { Layout.fillWidth: true }
                    LayoutBarZone {
                        zoneName: "topRight"
                        items: previewRoot._items("topRight")
                        distribution: previewRoot._opt("topRight", "distribution", "packed")
                        alignment: previewRoot._opt("topRight", "alignment", "center")
                        zoneStyle: previewRoot._opt("topRight", "style", "standard")
                        itemSize: previewRoot._opt("topRight", "itemSize", "compact")
                    }
                }
            }

            // Center content (centerStatus / centerTop / centerMiddle)
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: Theme.scaled(50)
                anchors.leftMargin: Theme.standardMargin
                anchors.rightMargin: Theme.standardMargin
                spacing: Theme.scaled(20)

                LayoutCenterZone {
                    Layout.fillWidth: true
                    Layout.topMargin: previewRoot._offset("centerStatus")
                    zoneName: "centerStatus"
                    items: previewRoot._items("centerStatus")
                    zoneScale: previewRoot._scale("centerStatus")
                    alignment: previewRoot._opt("centerStatus", "alignment", "center")
                    zoneStyle: previewRoot._opt("centerStatus", "style", "standard")
                    visible: previewRoot._items("centerStatus").length > 0
                }
                LayoutCenterZone {
                    Layout.fillWidth: true
                    Layout.topMargin: previewRoot._offset("centerTop")
                    zoneName: "centerTop"
                    items: previewRoot._items("centerTop")
                    zoneScale: previewRoot._scale("centerTop")
                    alignment: previewRoot._opt("centerTop", "alignment", "center")
                    zoneStyle: previewRoot._opt("centerTop", "style", "standard")
                }
                LayoutCenterZone {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: previewRoot._offset("centerMiddle")
                    zoneName: "centerMiddle"
                    items: previewRoot._items("centerMiddle")
                    zoneScale: previewRoot._scale("centerMiddle")
                    alignment: previewRoot._opt("centerMiddle", "alignment", "center")
                    zoneStyle: previewRoot._opt("centerMiddle", "style", "standard")
                    visible: previewRoot._items("centerMiddle").length > 0
                }
            }

            // Lower-mid bar (full-width band above the bottom bar, height-gated).
            // Auto-grows to fit large item-size, matching IdlePage.
            property real lowerMidFullHeight: Math.max(Theme.scaled(82), lmbPreviewZone.implicitHeight)
            property bool lowerMidVisible: previewRoot._items("lowerMidBar").length > 0
                && (height - Theme.bottomBarHeight - lowerMidFullHeight) >= Theme.scaled(220)

            Rectangle {
                id: previewLowerMid
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: previewBottomBar.top
                anchors.bottomMargin: -previewRoot._offset("lowerMidBar")
                visible: pageArea.lowerMidVisible
                height: visible ? pageArea.lowerMidFullHeight : 0
                color: Theme.zoneBackgroundColor(previewRoot._opt("lowerMidBar", "style", "standard"))

                LayoutBarZone {
                    id: lmbPreviewZone
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    zoneName: "lowerMidBar"
                    items: previewRoot._items("lowerMidBar")
                    distribution: previewRoot._opt("lowerMidBar", "distribution", "packed")
                    alignment: previewRoot._opt("lowerMidBar", "alignment", "center")
                    zoneStyle: previewRoot._opt("lowerMidBar", "style", "standard")
                    itemSize: previewRoot._opt("lowerMidBar", "itemSize", "compact")
                    zoneScale: previewRoot._scale("lowerMidBar")
                }
            }

            // Bottom bar (bottomLeft / bottomRight)
            Rectangle {
                id: previewBottomBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                // Auto-grow to fit large item-size, matching IdlePage.
                height: Math.max(Theme.bottomBarHeight, blPreviewZone.implicitHeight, brPreviewZone.implicitHeight)
                // Mirrors IdlePage's bottom bar so the preview matches what ships:
                // neutral surface scrim over a background image (like StatusBar and
                // the cards), otherwise the standard bottom-bar hue.
                color: Theme.glassChrome ? Theme.chromeFill(Theme.surfaceColor)
                                         : Theme.bottomBarColor
                // opacity < 1 forces the scrim through the alpha pass (see
                // docs/CLAUDE_MD/QML_GOTCHAS.md "Translucent element renders opaque").
                opacity: Theme.glassChrome ? 0.99 : 1.0

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    LayoutBarZone {
                        id: blPreviewZone
                        zoneName: "bottomLeft"
                        items: previewRoot._items("bottomLeft")
                        Layout.fillHeight: true
                        distribution: previewRoot._opt("bottomLeft", "distribution", "packed")
                        alignment: previewRoot._opt("bottomLeft", "alignment", "center")
                        zoneStyle: previewRoot._opt("bottomLeft", "style", "standard")
                        itemSize: previewRoot._opt("bottomLeft", "itemSize", "compact")
                    }
                    Item { Layout.fillWidth: true }
                    LayoutBarZone {
                        id: brPreviewZone
                        zoneName: "bottomRight"
                        items: previewRoot._items("bottomRight")
                        Layout.fillHeight: true
                        distribution: previewRoot._opt("bottomRight", "distribution", "packed")
                        alignment: previewRoot._opt("bottomRight", "alignment", "center")
                        zoneStyle: previewRoot._opt("bottomRight", "style", "standard")
                        itemSize: previewRoot._opt("bottomRight", "itemSize", "compact")
                    }
                }
            }
        }
    }
}
