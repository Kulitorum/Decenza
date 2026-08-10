pragma Singleton
import QtQuick
import Decenza

// The single QML source for steam-pitcher presentation.
//
// The built-in "Heater off" entry deliberately carries NO stored name: a name
// would be stale after a language change, and `name` is what recipes
// historically matched a pitcher on, so a translated one would break matching in
// every locale but the one it was saved in. The consequence is that the view has
// to supply the label — and the moment more than one view does that, they drift.
// This is that one place.
QtObject {

    // The built-in's label on its own, for callers that have no preset map to
    // hand. Without it they fabricated one — `pitcherName({ disabled: true })` —
    // which means knowing the map's internal shape to get a string, the exact
    // thing this singleton exists to prevent.
    readonly property string heaterOffName:
        TranslationManager.translate("steam.pitcher.heaterOff", "Heater off")

    // The label for a pitcher preset map, as returned by
    // Settings.brew.steamPitcherPresets or getSteamPitcherPreset().
    function pitcherName(preset) {
        if (!preset)
            return ""
        return preset.disabled === true ? heaterOffName : (preset.name || "")
    }

    // The pitcher pill row's label and live net-milk suffix, shared by the idle
    // row and the SteamItem popup. Both used to carry their own copy with a
    // "twin of ... keep in sync" comment on each, which is a note that they will
    // drift, not a mechanism that stops them.
    function pitcherPillLabel(preset, name) {
        if (preset && preset.disabled === true)
            return pitcherName(preset)
        if (!name || name.toLowerCase().indexOf("pitcher") >= 0)
            return name
        return name + " " + TranslationManager.translate("idle.label.pitcherSuffix", "Pitcher")
    }

    // The net milk currently on the scale for pitcher `index`, as a pill suffix.
    // Empty unless a weighing scale is connected AND the pitcher has a stored
    // empty weight — a flow scale reports no absolute weight to subtract from.
    function pitcherPillSuffix(preset) {
        if (!ScaleDevice.connected || ScaleDevice.isFlowScale)
            return ""
        if (!preset || preset.disabled === true)
            return ""
        var pitcherWeight = preset.pitcherWeightG ?? 0
        if (pitcherWeight <= 0)
            return ""
        return " (" + Math.round(Math.max(0, MachineState.scaleWeight - pitcherWeight)) + "g)"
    }

    // The current selection as a DISPLAY POSITION.
    //
    // The built-in is STORED as a positionless sentinel (-1), but every pill row
    // addresses its delegates by position and the built-in's position is one past
    // the last real preset. So a positional `index === selectedSteamPitcher` never
    // matches it: normalising the write side without this left "Heater off"
    // highlighted nowhere, and announced as unselected to a screen reader.
    //
    // The rule itself lives in C++ (SettingsBrew::selectedSteamPitcherDisplayIndex),
    // because the MCP `list` and `select` responses have to report the same
    // position and a second copy here would be free to drift from it. Read that
    // property directly where you need the position; this only adds the
    // comparison.
    function pitcherIsSelected(index) {
        return index === Settings.brew.selectedSteamPitcherDisplayIndex
    }

    // What the steam READOUTS show in place of a temperature when the resolved
    // target is off. Deliberately terser than the pitcher label: it sits where a
    // number would, in widgets a few characters wide.
    readonly property string offReadout: TranslationManager.translate("steam.heaterOff", "Off")

    // The steam temperature readout, and the "current / target" pair — "Off" in
    // place of the number whenever the resolved target is off.
    //
    // The OFF DECISION is what lives here. It was written out at four readouts,
    // and the measured temperature cannot substitute for it: a boiler switched
    // off five minutes ago still reads hot on its way down. (CustomItem keeps
    // its own formatting — its %TOKEN% family renders bare numbers by
    // convention — but takes the same decision from `offReadout`.)
    function temperatureText(heaterOff, tempC) {
        return heaterOff ? offReadout : Theme.formatTemperature(tempC, 0)
    }

    function temperaturePairText(heaterOff, currentC, targetC) {
        return heaterOff ? offReadout
                         : Theme.formatTemperature(currentC, 0) + " / " + Theme.formatTemperature(targetC, 0)
    }
}
