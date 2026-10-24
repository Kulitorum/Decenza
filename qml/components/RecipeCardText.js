.pragma library

// Pure card-row formatting shared by native and browser host adapters.
// Translation and drink-type policy remain caller supplied so this module has
// no dependency on Decenza singletons.
function drinkLine(recipe, drinkType, steam, hotWater, isHotWaterOnly,
                   shortLabelFor, hotWaterLabel, formatMilkWeight) {
    recipe = recipe || ({})
    steam = steam || ({})
    hotWater = hotWater || ({})
    var parts = []
    if (isHotWaterOnly)
        parts.push(shortLabelFor("tea") + " · " + hotWaterLabel)
    else
        parts.push(shortLabelFor(drinkType || recipe.drinkType || "espresso"))
    if (recipe.profileTitle && String(recipe.profileTitle).trim() !== "")
        parts.push(String(recipe.profileTitle))
    if (steam.hasMilk && (steam.milkWeightG || 0) > 0)
        parts.push(formatMilkWeight(steam.milkWeightG))
    return parts.join(" · ")
}

function beanLine(recipe, shotsWord) {
    recipe = recipe || ({})
    var parts = []
    var bean = ((recipe.roasterName || "") + " " + (recipe.coffeeName || "")).trim()
    if (bean !== "")
        parts.push(bean)
    if ((recipe.shotCount || 0) > 0)
        parts.push(recipe.shotCount + " " + shotsWord)
    return parts.join(" · ")
}

function vesselLine(hotWater) {
    hotWater = hotWater || ({})
    var parts = []
    if ((hotWater.volume || 0) > 0)
        parts.push(hotWater.volume + (hotWater.mode === "volume" ? "ml" : "g"))
    if ((hotWater.temperatureC || 0) > 0)
        parts.push(Math.round(hotWater.temperatureC) + "°C")
    if (hotWater.vesselName)
        parts.unshift(hotWater.vesselName)
    return parts.join(" · ")
}
