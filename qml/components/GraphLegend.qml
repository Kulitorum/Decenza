import QtQuick
import QtQuick.Layouts
import Decenza

// Shot-graph series legend. Filters the shared `GraphSeries` list by advanced / live mode
// and renders it through the wrapping `CustomLegend` so all graphs share one legend layout.
// Tapping an entry toggles the curve.
Item {
    id: legendRoot

    property bool advancedMode: Settings.graph.advancedMode
    property bool portalAvailable: false
    property bool liveMode: false  // true = live shot graph (hides post-shot-only curves like dC/dt)

    Layout.fillWidth: true
    implicitHeight: legend.implicitHeight

    PreferenceWrite { id: preferenceWrite; onFailed: message => writeError.show(message) }
    StatusToast { id: writeError; error: true }

    function writeTracePreference(key, enabled) {
        preferenceWrite.submit(key, enabled)
    }

    // Filtered CustomLegend entries use the shared revisioned preference snapshot, so
    // native and browser graphs observe the same trace choices.
    readonly property var _visibleModel: {
        var out = []
        var all = GraphSeries.entries
        for (let i = 0; i < all.length; i++) {
            let m = all[i]
            let vis = (!m.advanced || legendRoot.advancedMode) && (!m.postShotOnly || !legendRoot.liveMode)
            if (!vis || (m.portal && !legendRoot.portalAvailable))
                continue
            out.push({
                key: m.key,
                label: m.label,
                color: m.sColor,
                active: RecipeContext.preferences[m.key] ?? false,
                tip: m.tip ?? ""
            })
        }
        return out
    }

    CustomLegend {
        id: legend
        width: legendRoot.width
        entries: legendRoot._visibleModel
        toggleEnabled: preferenceWrite.ready

        onEntryToggled: (index, nowActive) => {
            legendRoot.writeTracePreference(legendRoot._visibleModel[index].key, nowActive)
        }
    }
}
