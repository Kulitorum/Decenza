import QtQuick
import QtQuick.Layouts
import Decenza

// Shot-graph series legend. Filters the shared `GraphSeries` list by advanced / live mode
// and renders it through the wrapping `CustomLegend` so all graphs share one legend layout.
// Tapping an entry toggles the curve.
Item {
    id: legendRoot

    property bool advancedMode: Settings.graph.advancedMode
    property bool liveMode: false  // true = live shot graph (hides post-shot-only curves like dC/dt)

    Layout.fillWidth: true
    implicitHeight: legend.implicitHeight

    // Filtered CustomLegend entries. Reads advancedMode/liveMode and the SettingsGraph
    // property behind each key, so it re-evaluates when the mode changes or a series is
    // toggled from anywhere — including the other graphs.
    readonly property var _visibleModel: {
        var out = []
        var all = GraphSeries.entries
        for (var i = 0; i < all.length; i++) {
            var m = all[i]
            var vis = (!m.advanced || legendRoot.advancedMode) && (!m.postShotOnly || !legendRoot.liveMode)
            if (!vis)
                continue
            out.push({
                key: m.key,
                label: m.label,
                color: m.sColor,
                active: Settings.graph[m.key] ?? false,
                tip: m.tip ?? ""
            })
        }
        return out
    }

    CustomLegend {
        id: legend
        width: legendRoot.width
        entries: legendRoot._visibleModel

        // One write. This used to set the graph's mirror property AND the setting, because
        // the mirrors were one-shot reads that no signal reached; now the setting is the
        // state and every graph binds to it.
        onEntryToggled: (index, nowActive) => {
            Settings.graph[legendRoot._visibleModel[index].key] = nowActive
        }
    }
}
