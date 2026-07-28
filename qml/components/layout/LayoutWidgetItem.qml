import QtQuick
import Decenza

// Base type for every widget under layout/items/.
//
// LayoutItemDelegate loads these through a Loader, so `loader.item` is only ever a QObject:
// each of the seven properties below was set or probed by unchecked member access, and a
// renamed one would have failed silently at runtime rather than at lint time. Declaring the
// contract once gives the delegate a type to cast to, and gives a new widget a single place
// to inherit it from instead of copying seven lines (all 35 widgets had byte-identical
// copies before this existed).
//
// Adding a widget: root it at this type, register it in the three places CLAUDE.md lists
// (CMakeLists.txt, LayoutItemDelegate's switch, widgetCatalogTable()), and declare only what
// is specific to it.
Item {
    // Bar rendering (compact) vs center rendering (large). Set by the delegate from the
    // zone the widget landed in.
    property bool isCompact: false

    // The widget instance's layout id, unique within the layout.
    property string itemId: ""

    // The widget's entry from the layout model, carrying its per-instance options.
    property var modelData: ({})

    // Zone style propagation (composable-brew-bar). A styled zone passes down contrast
    // text colour and value emphasis; a widget that renders a filled chip also keys off
    // zoneStyle. Widgets that do not care simply never read them.
    property color zoneTextColor: Theme.textColor
    property color zoneFillOverride: "transparent"
    property bool zoneValueBold: false
    property string zoneStyle: "standard"
}
