import QtQuick
import Decenza

// Base type for every widget under layout/items/.
//
// LayoutItemDelegate loads these through a Loader, so `loader.item` is only ever a QObject:
// each of the seven properties below was set or probed by unchecked member access, and a
// renamed one would have failed silently at runtime rather than at lint time. Declaring the
// contract once gives the delegate a type to cast to, and gives a new widget a single place
// to inherit from. Before this existed all 35 widgets re-declared some subset of these by
// hand — every copy byte-identical, but no file carried all seven and the most any one
// carried was five, so a widget silently opted out of zone styling by omission. That is how
// the styling bugs the deleted per-file comments describe kept happening.
//
// Adding a widget: root it at this type, register it in the three places CLAUDE.md lists
// (CMakeLists.txt, LayoutItemDelegate's switch, widgetCatalogTable()), and declare only what
// is specific to it.
Item {
    // Bar rendering (compact) vs center rendering (large). Set by the delegate from the
    // zone the widget landed in.
    property bool isCompact: false

    // The widget's entry from the layout model, carrying its per-instance options.
    property var modelData: ({})

    // Zone style propagation (composable-brew-bar). A styled zone passes down contrast
    // text colour and value emphasis; a widget that renders a filled chip also keys off
    // zoneStyle. Widgets that do not care simply never read them.
    property color zoneTextColor: Theme.textColor
    // Transparent = unset, use Theme. Set only by LayoutPreview, when it is previewing a
    // background colour that has not been applied and Theme's own values still describe the
    // CURRENT background.
    property color zoneFillOverride: "transparent"
    property bool zoneValueBold: false
    property string zoneStyle: "standard"
}
