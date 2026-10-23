.pragma library

// Pure formatting for the normalized values shown by EquipmentSummary. Hosts
// provide translated label templates and already translated puck-prep labels,
// so this module can be shared without importing native application services.
// Template strings use the same `%1` placeholder as the app translations.
function format(input, labels) {
    input = input || ({})
    labels = labels || ({})

    function text(value) {
        return value === undefined || value === null ? "" : String(value)
    }
    function translatedTemplate(name, fallback) {
        var value = text(labels[name])
        return value.length > 0 ? value : fallback
    }
    function withValue(template, value) {
        return template.replace(/%1/g, text(value))
    }
    function nonEmpty(parts) {
        return parts.map(text).filter(function(part) { return part.length > 0 })
    }

    var combined = nonEmpty([input.grinderBrand, input.grinderModel]).join(" ")
    var grinderName = text(input.grinderName)
    var title = grinderName.length > 0 ? grinderName : combined

    var basket = nonEmpty([input.basketBrand, input.basketModel]).join(" ")
    var basketTemplate = translatedTemplate("basket", "Basket: %1")
    var basketLine = basket.length > 0 ? withValue(basketTemplate, basket) : ""

    var puckPrepLabels = (input.puckPrepLabels || []).map(text)
        .filter(function(label) { return label.length > 0 })
    var puckPrepTemplate = translatedTemplate("puckPrep", "Prep: %1")
    var puckPrepLine = puckPrepLabels.length > 0
        ? withValue(puckPrepTemplate, puckPrepLabels.join(" · ")) : ""

    var dialParts = []
    var grindSetting = text(input.grindSetting)
    if (grindSetting.length > 0)
        dialParts.push(withValue(translatedTemplate("lastGrind", "Grind %1"), grindSetting))
    var rpm = Number(input.rpm)
    if (input.rpmCapable === true && Number.isFinite(rpm) && rpm > 0)
        dialParts.push(withValue(translatedTemplate("lastRpm", "%1 rpm"), rpm))
    var lastDialLine = dialParts.join(" · ")

    var lineageText = ""
    if (input.equipmentState === "older")
        lineageText = text(labels.older) || "Older equipment — a newer version is now in use"
    else if (input.equipmentState === "retired")
        lineageText = text(labels.retired) || "Retired equipment — no longer in inventory"

    var accessibleParts = nonEmpty([title, input.grinderBurrs])
    if (lastDialLine.length > 0) accessibleParts.push(lastDialLine)
    if (basketLine.length > 0) accessibleParts.push(basketLine)
    if (puckPrepLine.length > 0) accessibleParts.push(puckPrepLine)
    if (lineageText.length > 0) accessibleParts.push(lineageText)

    return {
        title: title,
        basketLine: basketLine,
        puckPrepLabels: puckPrepLabels,
        puckPrepTemplate: puckPrepTemplate,
        puckPrepLine: puckPrepLine,
        dialParts: dialParts,
        lastDialLine: lastDialLine,
        lineageText: lineageText,
        accessibleSummary: accessibleParts.join(", ")
    }
}
