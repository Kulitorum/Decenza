pragma Singleton
import QtQuick
import Decenza

// Drink-type presentation helpers (recipes-bag-links-ui-polish): the single
// QML source for drink-type labels and icons.
//
// Long labels are the wizard drink-type picker's descriptive forms ("Latte /
// Cappuccino", "Tea (hot water)"). Short labels are for every OTHER surface —
// cards, pills, the wizard summary hero, and auto-suggested recipe names —
// where a slash or parenthetical reads as noise, and where americano / long
// black need text beside their shared water icon to stay distinguishable.
QtObject {

    function shortLabel(t) {
        switch (t) {
        case "espresso": return TranslationManager.translate("recipes.type.short.espresso", "Espresso")
        case "filter": return TranslationManager.translate("recipes.type.short.filter", "Filter")
        case "americano": return TranslationManager.translate("recipes.type.short.americano", "Americano")
        case "long_black": return TranslationManager.translate("recipes.type.short.longBlack", "Long black")
        case "latte": return TranslationManager.translate("recipes.type.short.latte", "Latte")
        case "latte_hotwater": return TranslationManager.translate("recipes.type.short.latteHotWater", "Latte + Water")
        case "tea": return TranslationManager.translate("recipes.type.short.tea", "Tea")
        case "tea_hotwater": return TranslationManager.translate("recipes.type.short.tea", "Tea")
        }
        return t
    }

    function longLabel(t) {
        switch (t) {
        case "espresso": return TranslationManager.translate("recipes.wizard.type.espresso", "Espresso")
        case "filter": return TranslationManager.translate("recipes.wizard.type.filter", "Filter")
        case "americano": return TranslationManager.translate("recipes.wizard.type.americano", "Americano")
        case "long_black": return TranslationManager.translate("recipes.wizard.type.longBlack", "Long black")
        case "latte": return TranslationManager.translate("recipes.wizard.type.latte", "Latte / Cappuccino")
        case "latte_hotwater": return TranslationManager.translate("recipes.wizard.type.latteHotWater", "Latte + Hot Water")
        case "tea": return TranslationManager.translate("recipes.wizard.type.tea", "Tea")
        case "tea_hotwater": return TranslationManager.translate("recipes.wizard.type.teaHotWater", "Tea (hot water)")
        }
        return t
    }

    function icon(t) {
        switch (t) {
        case "filter": return "qrc:/icons/filter.svg"
        case "americano": case "long_black": return "qrc:/icons/water.svg"
        case "latte": case "latte_hotwater": return "qrc:/icons/steam.svg"
        case "tea": case "tea_hotwater": return "qrc:/icons/tea.svg"
        }
        return "qrc:/icons/espresso.svg"
    }

    // Icon set for surfaces that can show more than one glyph (the wizard's
    // drink-type picker). Latte + Water is the milk drink WITH added hot water,
    // so it carries both the steam and water glyphs; every other type is a
    // single-element list matching icon().
    function icons(t) {
        if (t === "latte_hotwater")
            return ["qrc:/icons/steam.svg", "qrc:/icons/water.svg"]
        return [icon(t)]
    }

    // Cheap block-derived fallback for legacy rows without a stored
    // drinkType (no profile lookup — milk/water are the available signals;
    // the storage layer derives the full precedence on writes).
    function fromRecipeMap(r) {
        var t = (r && r.drinkType) || ""
        if (t !== "")
            return t
        var milk = false, water = false, order = ""
        // Parse failures are WARNED: the derived type feeds behavior (the
        // stale card's re-point picker chooses tea vs coffee bags on it),
        // not just display.
        try {
            if (r && r.steamJson) milk = !!JSON.parse(r.steamJson).hasMilk
        } catch (e) { console.warn("DrinkType: bad steam JSON on recipe", (r && r.name) || "?", e) }
        try {
            if (r && r.hotWaterJson) {
                var w = JSON.parse(r.hotWaterJson)
                water = !!w.hasWater
                order = w.order || ""
            }
        } catch (e) { console.warn("DrinkType: bad hot-water JSON on recipe", (r && r.name) || "?", e) }
        if ((!r || !r.profileTitle || String(r.profileTitle).trim() === "") && water)
            return "tea_hotwater"
        if (milk && water)
            return "latte_hotwater"
        if (milk)
            return "latte"
        if (water)
            return order === "before" ? "long_black" : "americano"
        return "espresso"
    }
}
