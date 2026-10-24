.pragma library

// Shared Shot Plan text assembly. Hosts provide raw values, already-resolved
// temperature display text, localized templates, and theme-specific rich-text
// helpers. Dose/yield/grind and sentence/fragment policy live here once.

function _argSafe(value) {
    return String(value).replace(/%(\d)/g, "%\u200B$1")
}

// QString.arg-style sequential placeholder substitution, including reordered
// numbered placeholders in translations. A protected user token (% + ZWSP)
// is intentionally not matched.
function _arg(template, values) {
    var result = String(template)
    for (var i = 0; i < values.length; ++i) {
        var match
        var expression = /%(\d+)/g
        var lowest = Number.MAX_VALUE
        while ((match = expression.exec(result)) !== null)
            lowest = Math.min(lowest, Number(match[1]))
        if (lowest === Number.MAX_VALUE)
            break
        var token = new RegExp("%" + lowest + "(?!\\d)", "g")
        var replacement = String(values[i])
        // A replacement string would interpret `$&`, `$'`, and `$`` as
        // String.replace substitutions. QString.arg inserts its argument
        // literally, so use a callback to preserve user-supplied text.
        result = result.replace(token, function() { return replacement })
    }
    return result
}

function _tr(translate, key, fallback, values) {
    return _arg(translate(key, fallback), values || [])
}

function _has(input, key) {
    return input.itemOrder && input.itemOrder.indexOf(key) !== -1
}

function _yieldText(input, translate) {
    if (!_has(input, "doseYield"))
        return ""
    if (input.yieldAnchorMode === "ratio" && input.yieldAnchorRatio > 0 && input.dose <= 0)
        return "1:" + input.yieldAnchorRatio.toFixed(1)
    if (input.targetWeight <= 0)
        return ""

    var ratio = input.yieldAnchorMode === "ratio" && input.yieldAnchorRatio > 0
        ? "1:" + input.yieldAnchorRatio.toFixed(1) : ""
    var mark = ratio !== "" ? " (" + ratio + ")" : ""
    var baseline = input.baselineYieldG > 0 ? input.baselineYieldG : input.profileYield
    var yieldOverride = input.yieldOverridden && baseline > 0
        && Math.abs(input.targetWeight - baseline) > 0.1
    if (!input.yieldTargetOnly && yieldOverride)
        return baseline.toFixed(1) + " → " + input.targetWeight.toFixed(1) + "g" + mark

    var actual = input.actualYield.toFixed(1)
    var target = input.targetWeight.toFixed(1)
    if (input.actualYield > 0 && actual !== target)
        return actual + "g " + _tr(translate, "shotplan.targetYield", "(target %1g)", [target]) + mark
    return target + "g" + mark
}

function _build(input, translate, formatValue, sep, blockSep) {
    if (input.isCleaning) {
        return input.profileName
            ? _tr(translate, "shotplan.sentenceCleaning",
                 "Cleaning run with %1 — no coffee in the portafilter!",
                 [formatValue(input.profileName, true)])
            : translate("shotplan.cleaningNoProfile", "Cleaning run — no coffee in the portafilter!")
    }

    var yieldText = _yieldText(input, translate)
    var doseText = _has(input, "doseYield") && input.dose > 0
        ? input.dose.toFixed(1) + "g" : ""
    var profileText = _has(input, "profile") && input.profileName ? input.profileName : ""
    var recipeText = _has(input, "recipe") && input.recipeName.length > 0 ? input.recipeName : ""
    var anchorText = profileText !== "" ? profileText : recipeText
    var tempOverride = input.tempOverridden
        && Math.abs(input.overrideTemp - input.tempHlBaseline) > 0.1
    var yieldBaseline = input.baselineYieldG > 0 ? input.baselineYieldG : input.profileYield
    var yieldOverride = input.yieldOverridden && yieldBaseline > 0
        && Math.abs(input.targetWeight - yieldBaseline) > 0.1
    var temp = input.temperatureText !== ""
        ? formatValue(input.temperatureText, true, tempOverride) : ""
    var roasterText = _has(input, "roaster") && input.roasterBrand ? input.roasterBrand : ""
    var coffeeText = _has(input, "coffee") && input.coffeeName ? input.coffeeName : ""
    var grindText = ""
    if (_has(input, "grind")) {
        var grindParts = []
        if (input.grindSize.length > 0)
            grindParts.push(input.grindSize)
        if (input.grindRpm > 0 && input.rpmCapable)
            grindParts.push(_tr(translate, "equipment.card.lastRpm", "%1 rpm", [input.grindRpm]))
        if (grindParts.length > 0) {
            grindText = grindParts.join(" · ")
            if (input.grindSize.length > 0)
                grindText = _tr(translate, "shotplan.grind", "grind %1", [formatValue(grindText, true)])
            else
                grindText = formatValue(grindText, true)
        }
    }
    var roastDateText = _has(input, "roastDate") && input.roastDate.length > 0
        ? input.roastDate : ""
    var roastedText = roastDateText !== ""
        ? _tr(translate, "shotplan.roasted", "roasted %1", [formatValue(roastDateText, true)]) : ""
    var doseInText = doseText !== ""
        ? _tr(translate, "shotplan.doseIn", "%1 in", [formatValue(doseText, true)]) : ""
    var order = input.itemOrder || []

    if (input.sentence && anchorText !== "") {
        var sentence
        if (yieldText !== "" && input.temperatureText !== "")
            sentence = _tr(translate, "shotplan.sentence", "Brew %1 of %2, using %3 at %4",
                [formatValue(yieldText, true, yieldOverride), formatValue(input.beverageText, false),
                 formatValue(anchorText, true), temp])
        else if (yieldText === "" && input.temperatureText !== "")
            sentence = _tr(translate, "shotplan.sentenceNoYield", "Brew %1, using %2 at %3",
                [formatValue(input.beverageText, false), formatValue(anchorText, true), temp])
        else if (yieldText !== "")
            sentence = _tr(translate, "shotplan.sentenceNoTemp", "Brew %1 of %2, using %3",
                [formatValue(yieldText, true, yieldOverride), formatValue(input.beverageText, false),
                 formatValue(anchorText, true)])
        else
            sentence = _tr(translate, "shotplan.sentenceNoYieldNoTemp", "Brew %1, using %2",
                [formatValue(input.beverageText, false), formatValue(anchorText, true)])

        var tail = []
        for (var i = 0; i < order.length; ++i) {
            switch (order[i]) {
            case "doseYield": if (doseInText !== "") tail.push(doseInText); break
            case "roaster": if (roasterText !== "") tail.push(formatValue(roasterText, true)); break
            case "coffee": if (coffeeText !== "") tail.push(formatValue(coffeeText, true)); break
            case "grind": if (grindText !== "") tail.push(grindText); break
            case "roastDate": if (roastedText !== "") tail.push(roastedText); break
            case "recipe":
                if (recipeText !== "" && profileText !== "") tail.push(formatValue(recipeText, true))
                break
            }
        }
        return tail.length > 0 ? sentence + blockSep + tail.join(sep) : sentence
    }

    if (input.sentence) {
        var beans = ""
        if (roasterText !== "" && coffeeText !== "")
            beans = formatValue(roasterText, true) + " " + formatValue(coffeeText, true)
        else if (roasterText !== "")
            beans = formatValue(roasterText, true)
        else if (coffeeText !== "")
            beans = formatValue(coffeeText, true)

        var result = yieldText !== ""
            ? _tr(translate, "shotplan.recipe.head", "Brew %1 of %2",
                  [formatValue(yieldText, true, yieldOverride), formatValue(input.beverageText, false)])
            : _tr(translate, "shotplan.recipe.headNoYield", "Brew %1", [formatValue(input.beverageText, false)])
        if (input.temperatureText !== "")
            result = _tr(translate, "shotplan.recipe.atTemp", "%1 at %2", [result, temp])
        if (doseText !== "" && beans !== "")
            result = _tr(translate, "shotplan.recipe.fromDoseBeans", "%1 from %2 of %3",
                         [result, formatValue(doseText, true), beans])
        else if (doseText !== "")
            result = _tr(translate, "shotplan.recipe.fromDose", "%1 from %2",
                         [result, formatValue(doseText, true)])
        else if (beans !== "")
            result = _tr(translate, "shotplan.recipe.fromBeans", "%1 from %2", [result, beans])

        var recipeTail = []
        for (var j = 0; j < order.length; ++j) {
            switch (order[j]) {
            case "grind": if (grindText !== "") recipeTail.push(grindText); break
            case "roastDate": if (roastedText !== "") recipeTail.push(roastedText); break
            }
        }
        return recipeTail.length > 0 ? result + blockSep + recipeTail.join(sep) : result
    }

    var parts = []
    for (var k = 0; k < order.length; ++k) {
        switch (order[k]) {
        case "doseYield":
            if (doseInText !== "") parts.push(doseInText)
            if (yieldText !== "") parts.push(formatValue(yieldText, true, yieldOverride))
            break
        case "profile": if (profileText !== "") parts.push(formatValue(profileText, true)); break
        case "temperature": if (temp !== "") parts.push(temp); break
        case "roaster": if (roasterText !== "") parts.push(formatValue(roasterText, true)); break
        case "coffee": if (coffeeText !== "") parts.push(formatValue(coffeeText, true)); break
        case "grind": if (grindText !== "") parts.push(grindText); break
        case "roastDate": if (roastedText !== "") parts.push(roastedText); break
        case "recipe": if (recipeText !== "") parts.push(formatValue(recipeText, true)); break
        }
    }
    return parts.join(sep)
}

function format(input, translate, options) {
    input = input || ({})
    options = options || ({})
    var plain = _build(input, translate, function(value) { return _argSafe(value) },
                       options.plainSeparator || "  ·  ",
                       options.plainSeparator || "  ·  ")

    function richValue(value, live, overridden) {
        var escaped = options.escapeHtml(String(value).replace(/%(\d)/g, "%\u200B$1"))
        var bold = live ? "<b>" + escaped + "</b>" : escaped
        return overridden ? "<font color=\"" + options.highlightColor + "\">" + bold + "</font>" : bold
    }

    var rich = _build(input, translate, richValue,
                      options.richSeparator || " <font size=\"+1\"><b>·</b></font> ",
                      input.stacked
                          ? "<br>"
                          : (options.richSeparator || " <font size=\"+1\"><b>·</b></font> "))
    if (input.isCleaning && rich !== "")
        rich = "<b>" + rich + "</b>"
    if (options.replaceEmoji)
        rich = options.replaceEmoji(rich)
    return { text: plain, rich: rich }
}
