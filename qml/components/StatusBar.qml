import QtQuick
import QtQuick.Layouts
import Decenza
import "layout"

Rectangle {
    id: statusBarRoot

    // Parse the statusBar zone + its per-zone options from layout config so the
    // top bar honors distribution / alignment / style like every other bar zone
    // (composable-status-bar — no longer a special-cased renderer).
    property var _layout: {
        try { return JSON.parse(Settings.network.layoutConfiguration) || ({}) }
        catch (e) { return ({}) }
    }
    property var statusBarItems: (_layout.zones && _layout.zones.statusBar) || []
    property var zoneOpts: (_layout.zoneOptions && _layout.zoneOptions.statusBar) || ({})

    // Default keeps the surface background; a non-standard style overrides it.
    // When a custom background image is active, go semi-transparent (keeping
    // the bar's own hue as a scrim) so the image extends behind the bar
    // instead of stopping at its edge.
    readonly property color _opaqueColor: (zoneOpts.style && zoneOpts.style !== "standard")
           ? Theme.zoneBackgroundColor(zoneOpts.style)
           : Theme.surfaceColor
    color: Settings.theme.backgroundImagePath.length > 0
           ? Theme.scrimColor(_opaqueColor)
           : _opaqueColor

    LayoutBarZone {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        zoneName: "statusBar"
        items: statusBarRoot.statusBarItems
        distribution: statusBarRoot.zoneOpts.distribution || "packed"
        alignment: statusBarRoot.zoneOpts.alignment || "center"
        zoneStyle: statusBarRoot.zoneOpts.style || "standard"
    }
}
