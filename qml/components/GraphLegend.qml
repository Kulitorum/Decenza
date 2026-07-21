import QtQuick
import QtQuick.Layouts
import Decenza

// Shot-graph series legend. Builds the shot-specific entry model (translated
// labels, series colors, `graph/*` persistence keys, tooltips, and advanced /
// live-mode filtering) and renders it through the shared, wrapping `CustomLegend`
// so all graphs share one legend layout. Tapping an entry toggles the curve and
// persists the choice to Settings.
Item {
    id: legendRoot

    required property var graph
    property bool advancedMode: false
    property bool liveMode: false  // true = live shot graph (hides post-shot-only curves like dC/dt)

    Layout.fillWidth: true
    implicitHeight: legend.implicitHeight

    // Full series model. `advanced` entries appear only in advanced mode;
    // `postShotOnly` entries are hidden on the live graph.
    readonly property var _model: [
        { label: TranslationManager.translate("graph.pressure", "Pressure"), sColor: Theme.pressureColor, key: "showPressure",
          tip: TranslationManager.translate("graph.tip.pressure", "Pump pressure in bar. Shows the machine's intent — what it's trying to do.") },
        { label: TranslationManager.translate("graph.flow", "Flow"), sColor: Theme.flowColor, key: "showFlow",
          tip: TranslationManager.translate("graph.tip.flow", "Water flow rate in mL/s. Shows the coffee's response — how easily water passes through the puck.") },
        // Keys bumped to *2 when the fallback gained the %1 unit placeholder:
        // a cached translation of the old key has a literal "°C" and no %1,
        // so .arg() would warn and the unit would never switch to °F.
        { label: TranslationManager.translate("graph.temp", "Temp"), sColor: Theme.temperatureColor, key: "showTemperature",
          tip: TranslationManager.translate("graph.tip.temp2", "Basket temperature in %1. The temperature at the group head thermocouple.").arg(Theme.tempUnitSuffix()) },
        { label: TranslationManager.translate("graph.mixTemp", "Mix temp"), sColor: Theme.temperatureMixColor, key: "showTemperatureMix", advanced: true,
          tip: TranslationManager.translate("graph.tip.mixTemp2", "Mix temperature in %1. The actual water temperature reaching the puck. Difference from basket temp reveals group head thermal stability.").arg(Theme.tempUnitSuffix()) },
        { label: TranslationManager.translate("graph.mixTempGoal", "Mix temp goal"), sColor: Theme.temperatureMixGoalColor, key: "showTemperatureMixGoal", advanced: true,
          tip: TranslationManager.translate("graph.tip.mixTempGoal", "Mix temperature target in %1. What the machine aimed the incoming water at. Read against Mix temp to see how well it held that target. Not recorded on older shots.").arg(Theme.tempUnitSuffix()) },
        { label: TranslationManager.translate("graph.weight", "Weight"), sColor: Theme.weightColor, key: "showWeight",
          tip: TranslationManager.translate("graph.tip.weight", "Cumulative beverage weight in grams from the scale.") },
        { label: TranslationManager.translate("graph.wtFlow", "Wt flow"), sColor: Theme.weightFlowColor, key: "showWeightFlow",
          tip: TranslationManager.translate("graph.tip.wtFlow", "Weight-based flow rate in g/s from the scale. More accurate than pump flow for measuring actual output.") },
        { label: TranslationManager.translate("graph.resistance", "Resist(P/F)"), sColor: Theme.resistanceColor, key: "showResistance", advanced: true,
          tip: TranslationManager.translate("graph.tip.resistance", "Puck resistance (P/F). Rising = puck tightening. Falling = puck opening. Erratic = channeling.") },
        { label: TranslationManager.translate("graph.darcyResistance", "Resist(P/F²)"), sColor: Theme.darcyResistanceColor, key: "showDarcyResistance", advanced: true,
          tip: TranslationManager.translate("graph.tip.darcyResistance", "Darcy resistance (P/F²). Physics-based puck resistance for laminar flow. Inverse of conductance.") },
        { label: TranslationManager.translate("graph.conductance", "Conduct(F²/P)"), sColor: Theme.conductanceColor, key: "showConductance", advanced: true,
          tip: TranslationManager.translate("graph.tip.conductance", "Conductance (F²/P, Darcy's law). Rising = puck opening up. Stable = consistent extraction. Spike = channeling.") },
        { label: TranslationManager.translate("graph.dCdt", "dC/dt"), sColor: Theme.conductanceDerivativeColor, key: "showConductanceDerivative", advanced: true, postShotOnly: true,
          tip: TranslationManager.translate("graph.tip.dCdt", "Rate of change of conductance. The best channeling detector — spikes reveal transient channels that are invisible in other curves.") }
    ]

    // Filtered CustomLegend entries. Reads advancedMode/liveMode and graph[key],
    // so it re-evaluates when the mode changes or a series is toggled.
    readonly property var _visibleModel: {
        var out = []
        for (var i = 0; i < _model.length; i++) {
            var m = _model[i]
            var vis = (!m.advanced || legendRoot.advancedMode) && (!m.postShotOnly || !legendRoot.liveMode)
            if (!vis)
                continue
            out.push({
                key: m.key,
                label: m.label,
                color: m.sColor,
                active: legendRoot.graph ? (legendRoot.graph[m.key] ?? false) : false,
                tip: m.tip ?? ""
            })
        }
        return out
    }

    CustomLegend {
        id: legend
        width: legendRoot.width
        entries: legendRoot._visibleModel

        onEntryToggled: (index, nowActive) => {
            var key = legendRoot._visibleModel[index].key
            legendRoot.graph[key] = nowActive
            Settings.setValue("graph/" + key, nowActive)
        }
    }
}
