.pragma library

// Shot Plan widget item-list config, shared by the widget (ShotPlanItem.qml)
// and the in-app editor (ScreensaverEditorPopup.qml) so the legacy-derivation
// rule cannot drift between them. The web layout editor (shotserver_layout.cpp,
// spItemsFromProps) mirrors this rule in its embedded JS — keep the two
// implementations in sync.

// All item keys in canonical (default) order — the pre-configurable fragment
// order. roastDate is the only item that defaults OFF, matching the legacy
// shotPlanShowRoastDate default.
var allKeys = ["doseYield", "profile", "temperature", "roaster", "coffee", "grind", "roastDate"]

// Resolve an instance's ordered display-item list: prefer the stored
// `shotPlanItems` array — its PRESENCE is what matters, an empty array is a
// valid "show nothing" configuration and must not fall through to legacy
// derivation (which would resurrect the defaults). Derive from the legacy
// shotPlanShow* booleans in canonical order, honoring each legacy default,
// only when the key is absent, null (what the pre-fix bug stored — this is
// its recovery path), or malformed. The compound legacy "Profile &
// temperature" boolean expands to the now-independent `profile` +
// `temperature` items. The six legacy item booleans are read here, never
// written (shotPlanShowSteamPlan is a live key, not one of them).
function itemsFor(props) {
    if (!props) props = {}
    var items = props.shotPlanItems
    // Accept only real array-likes. Array.isArray is deliberately NOT used —
    // a QVariantList surfaced from C++ can be a sequence, not a JS Array —
    // but a stored string must not char-split into bogus one-letter keys and
    // a stored object must not read as an empty list: both would blank the
    // widget silently, and the web editor (Array.isArray) would disagree by
    // showing the legacy derivation instead. Malformed → warn + same
    // legacy-derivation branch as the web side.
    if (items !== undefined && items !== null) {
        if (typeof items !== "string" && typeof items.length === "number") {
            // May be a C++-backed list rather than a plain JS array; normalize
            // elements to strings.
            var out = []
            for (var i = 0; i < items.length; i++) out.push(String(items[i]))
            return out
        }
        console.warn("ShotPlanConfig: malformed shotPlanItems (" + typeof items + "), using legacy derivation")
    }
    var order = []
    if (props.shotPlanShowDoseYield !== false) order.push("doseYield")
    if (props.shotPlanShowProfile !== false) { order.push("profile"); order.push("temperature") }
    if (props.shotPlanShowRoaster !== false) order.push("roaster")
    if (props.shotPlanShowCoffee !== false) order.push("coffee")
    if (props.shotPlanShowGrind !== false) order.push("grind")
    if (props.shotPlanShowRoastDate === true) order.push("roastDate")
    return order
}

// The first Shot Plan widget object in a parsed layout, scanning every zone's
// item list in object order, or null when the layout has none. Lets surfaces
// outside the idle screen (the shot-page snapshot line) mirror the widget the
// user actually placed. Normally there is at most one; "first found" is the
// tie-break when a layout has several.
function firstShotPlanItem(layoutObj) {
    if (!layoutObj || !layoutObj.zones) return null
    var zones = layoutObj.zones
    for (var z in zones) {
        var arr = zones[z]
        if (!arr || typeof arr.length !== "number") continue
        for (var i = 0; i < arr.length; i++) {
            if (arr[i] && arr[i].type === "shotPlan") return arr[i]
        }
    }
    return null
}

// The ordered display-item list from the user's first Shot Plan widget, falling
// back to the same canonical defaults the widget itself uses when the layout has
// none. Only the field selection + order is taken; the sentence/stacked toggles
// are intentionally ignored (the snapshot line is always a plain fragment list).
// `profile` and `temperature` are dropped: the shot-page snapshot line sits
// directly under a title that already shows the profile name and temperature
// (e.g. "Default (90°C)"), so repeating them there is redundant.
function itemOrderFromLayout(layoutObj) {
    return itemsFor(firstShotPlanItem(layoutObj))
        .filter(function(k) { return k !== "profile" && k !== "temperature" })
}

// Same as itemOrderFromLayout, but parses the raw layout JSON string first
// (the form the pages hold it in) — a malformed/empty string falls back to the
// canonical defaults. Keeps the parse+guard next to the resolver it feeds.
function itemOrderFromLayoutJson(jsonStr) {
    var layout
    try { layout = JSON.parse(jsonStr) } catch (e) { layout = null }
    return itemOrderFromLayout(layout)
}
