pragma Singleton
import QtQuick
import Decenza

// The one description of the shot graph's series: label, colour, settings property,
// tooltip, and the two visibility gates.
//
// This list existed four times in four shapes — as the legend's model, as a name→key map in
// ComparisonDataTable, as a key+default array in LastShotChartSource, and as eleven property
// declarations repeated in each of the three graphs. Nothing kept them in agreement, and the
// LastShotChartSource copy had already shipped with an entry missing, which left the home
// screen chart rendering a stale image whenever the right axis was toggled.
//
// `key` is both the SettingsGraph property name and, prefixed with `graph/`, its storage key.
// `dataKey` names the series in the comparison model's per-shot value objects, and
// `shortLabel` is the abbreviated column heading the comparison table needs — both live here
// so that table derives its columns rather than restating the list.
QtObject {
    id: graphSeries

    // `advanced` entries appear only in advanced mode; `postShotOnly` entries are hidden on
    // the live graph, where the curve cannot be computed until the shot is complete.
    readonly property var entries: [
        { label: TranslationManager.translate("graph.pressure", "Pressure"), sColor: Theme.pressureColor, key: "showPressure", dataKey: "pressure", shortLabel: "P",
          tip: TranslationManager.translate("graph.tip.pressure", "Pump pressure in bar. Shows the machine's intent — what it's trying to do.") },
        { label: TranslationManager.translate("graph.flow", "Flow"), sColor: Theme.flowColor, key: "showFlow", dataKey: "flow", shortLabel: "F",
          tip: TranslationManager.translate("graph.tip.flow", "Water flow rate in mL/s. Shows the coffee's response — how easily water passes through the puck.") },
        // Keys bumped to *2 when the fallback gained the %1 unit placeholder:
        // a cached translation of the old key has a literal "°C" and no %1,
        // so .arg() would warn and the unit would never switch to °F.
        { label: TranslationManager.translate("graph.temp", "Temp"), sColor: Theme.temperatureColor, key: "showTemperature", dataKey: "temp", shortLabel: "T",
          tip: TranslationManager.translate("graph.tip.temp2", "Basket temperature in %1. The temperature at the group head thermocouple.").arg(Theme.tempUnitSuffix()) },
        { label: TranslationManager.translate("graph.mixTemp", "Mix temp"), sColor: Theme.temperatureMixColor, key: "showTemperatureMix", dataKey: "mixTemp", shortLabel: "Tmix", advanced: true,
          tip: TranslationManager.translate("graph.tip.mixTemp2", "Mix temperature in %1. The actual water temperature reaching the puck. Difference from basket temp reveals group head thermal stability.").arg(Theme.tempUnitSuffix()) },
        { label: TranslationManager.translate("graph.mixTempGoal", "Mix temp goal"), sColor: Theme.temperatureMixGoalColor, key: "showTemperatureMixGoal", dataKey: "mixTempGoal", shortLabel: "Tmixg", advanced: true,
          tip: TranslationManager.translate("graph.tip.mixTempGoal", "Mix temperature target in %1. What the machine aimed the incoming water at. Read against Mix temp to see how well it held that target. Not recorded on older shots.").arg(Theme.tempUnitSuffix()) },
        { label: TranslationManager.translate("graph.weight", "Weight"), sColor: Theme.weightColor, key: "showWeight", dataKey: "weight", shortLabel: "W",
          tip: TranslationManager.translate("graph.tip.weight", "Cumulative beverage weight in grams from the scale.") },
        { label: TranslationManager.translate("graph.wtFlow", "Wt flow"), sColor: Theme.weightFlowColor, key: "showWeightFlow", dataKey: "weightFlow", shortLabel: "WF",
          tip: TranslationManager.translate("graph.tip.wtFlow", "Weight-based flow rate in g/s from the scale. More accurate than pump flow for measuring actual output.") },
        { label: TranslationManager.translate("graph.resistance", "Resist(P/F)"), sColor: Theme.resistanceColor, key: "showResistance", dataKey: "resistance", shortLabel: "R", advanced: true,
          tip: TranslationManager.translate("graph.tip.resistance", "Puck resistance (P/F). Rising = puck tightening. Falling = puck opening. Erratic = channeling.") },
        { label: TranslationManager.translate("graph.darcyResistance", "Resist(P/F²)"), sColor: Theme.darcyResistanceColor, key: "showDarcyResistance", dataKey: "darcyR", shortLabel: "dR", advanced: true,
          tip: TranslationManager.translate("graph.tip.darcyResistance", "Darcy resistance (P/F²). Physics-based puck resistance for laminar flow. Inverse of conductance.") },
        { label: TranslationManager.translate("graph.conductance", "Conduct(F²/P)"), sColor: Theme.conductanceColor, key: "showConductance", dataKey: "conductance", shortLabel: "C", advanced: true,
          tip: TranslationManager.translate("graph.tip.conductance", "Conductance (F²/P, Darcy's law). Rising = puck opening up. Stable = consistent extraction. Spike = channeling.") },
        { label: TranslationManager.translate("graph.dCdt", "dC/dt"), sColor: Theme.conductanceDerivativeColor, key: "showConductanceDerivative", dataKey: "dCdt", shortLabel: "dC/dt", advanced: true, postShotOnly: true,
          tip: TranslationManager.translate("graph.tip.dCdt", "Rate of change of conductance. The best channeling detector — spikes reveal transient channels that are invisible in other curves.") }
    ]

    // Property names only, for consumers that need to enumerate the settings rather than
    // render them.
    readonly property var keys: {
        var out = []
        for (var i = 0; i < graphSeries.entries.length; i++)
            out.push(graphSeries.entries[i].key)
        return out
    }
}
