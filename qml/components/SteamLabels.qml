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

    // The label for a pitcher preset map, as returned by
    // Settings.brew.steamPitcherPresets or getSteamPitcherPreset().
    function pitcherName(preset) {
        if (!preset)
            return ""
        return preset.disabled === true
            ? TranslationManager.translate("steam.pitcher.heaterOff", "Heater off")
            : (preset.name || "")
    }

    // The pitcher pill row's label and live net-milk suffix, shared by the idle
    // row and the SteamItem popup. Both used to carry their own copy with a
    // "twin of ... keep in sync" comment on each, which is a note that they will
    // drift, not a mechanism that stops them.
    function pitcherPillLabel(index, name) {
        var preset = Settings.brew.steamPitcherPresets[index]
        if (preset && preset.disabled === true)
            return pitcherName(preset)
        if (!name || name.toLowerCase().indexOf("pitcher") >= 0)
            return name
        return name + " " + TranslationManager.translate("idle.label.pitcherSuffix", "Pitcher")
    }

    // The net milk currently on the scale for pitcher `index`, as a pill suffix.
    // Empty unless a weighing scale is connected AND the pitcher has a stored
    // empty weight — a flow scale reports no absolute weight to subtract from.
    function pitcherPillSuffix(index) {
        if (!ScaleDevice.connected || ScaleDevice.isFlowScale)
            return ""
        var preset = Settings.brew.steamPitcherPresets[index]
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
    // position and a second copy here would be free to drift from it.
    function pitcherDisplayIndex() {
        return Settings.brew.selectedSteamPitcherDisplayIndex
    }

    // True when the pill at `index` (a display position) is the selection.
    function pitcherIsSelected(index) {
        return index === Settings.brew.selectedSteamPitcherDisplayIndex
    }

    // What the steam READOUTS show in place of a temperature when the resolved
    // target is off. Deliberately terser than the pitcher label: it sits where a
    // number would, in widgets a few characters wide.
    readonly property string offReadout: TranslationManager.translate("steam.heaterOff", "Off")
}
