.pragma library

// Shared label keys, fallbacks and icon mapping; hosts supply translation.
    function shortLabel(t, translate) {
        switch (t) {
        case "espresso": return translate("recipes.type.short.espresso", "Espresso")
        case "filter": return translate("recipes.type.short.filter", "Filter")
        case "americano": return translate("recipes.type.short.americano", "Americano")
        case "long_black": return translate("recipes.type.short.longBlack", "Long black")
        case "latte": return translate("recipes.type.short.latte", "Latte")
        case "latte_hotwater": return translate("recipes.type.short.latteHotWater", "Latte + Water")
        case "tea": return translate("recipes.type.short.tea", "Tea")
        case "tea_hotwater": return translate("recipes.type.short.tea", "Tea")
        }
        return t
    }

    function longLabel(t, translate) {
        switch (t) {
        case "espresso": return translate("recipes.wizard.type.espresso", "Espresso")
        case "filter": return translate("recipes.wizard.type.filter", "Filter")
        case "americano": return translate("recipes.wizard.type.americano", "Americano")
        case "long_black": return translate("recipes.wizard.type.longBlack", "Long black")
        case "latte": return translate("recipes.wizard.type.latte", "Latte / Cappuccino")
        case "latte_hotwater": return translate("recipes.wizard.type.latteHotWater", "Latte + Hot Water")
        case "tea": return translate("recipes.wizard.type.tea", "Tea")
        case "tea_hotwater": return translate("recipes.wizard.type.teaHotWater", "Tea (hot water)")
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

