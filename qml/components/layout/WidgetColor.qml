pragma Singleton
import QtQuick
import Decenza // for the Theme singleton — without this, Theme is undefined here

// Per-instance color override for readout layout widgets (any type whose
// capability schema in settings_network.cpp includes "color"). "default" (or
// unset) keeps the widget's own natural color — which may be dynamic, e.g.
// machine status colored by phase or scale weight by tap/ratio state, battery
// widgets by charge level. The named values
// force a static tint mapped to the same semantic chart colors the rest of the
// page uses, so the widget matches its surroundings and honours custom themes.
QtObject {
    id: widgetColor

    // Picker choices, in display order. "default" leads so the non-destructive
    // option is first. Reading translationVersion makes the labels re-evaluate on
    // a language switch — required because this singleton is never recreated.
    readonly property var choices: {
        var _v = TranslationManager.translationVersion
        return [
            { value: "default", label: TranslationManager.translate("layoutEditor.colorDefault", "Default") },
            { value: "white",   label: TranslationManager.translate("layoutEditor.colorWhite", "White") },
            { value: "green",   label: TranslationManager.translate("layoutEditor.colorGreen", "Green") },
            { value: "red",     label: TranslationManager.translate("layoutEditor.colorRed", "Red") },
            { value: "blue",    label: TranslationManager.translate("layoutEditor.colorBlue", "Blue") },
            { value: "orange",  label: TranslationManager.translate("layoutEditor.colorOrange", "Orange") }
        ]
    }

    // The override color for a named choice, or `fallback` (the widget's own
    // natural color) for "default"/unset/unknown.
    function resolve(name, fallback) {
        switch (name) {
        case "white":  return Theme.textColor
        case "green":  return Theme.pressureColor
        case "red":    return Theme.temperatureColor
        case "blue":   return Theme.flowColor
        case "orange": return Theme.warningColor
        default:       return fallback
        }
    }

    // True when `name` is a recognised non-default override (unknown values
    // degrade to the widget's natural color, same as resolve()).
    function isOverride(name) {
        switch (name) {
        case "white": case "green": case "red": case "blue": case "orange": return true
        default: return false
        }
    }

    // Swatch color for the picker preview. "default" has no fixed color, so it
    // shows a neutral dot (the label communicates the meaning).
    function swatch(name) {
        return name === "default" ? Theme.textSecondaryColor
                                  : resolve(name, Theme.textSecondaryColor)
    }
}
