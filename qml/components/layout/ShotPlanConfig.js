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
// derivation (which would resurrect the defaults). Only when the key is
// absent entirely, derive from the legacy shotPlanShow* booleans in canonical
// order, honoring each legacy default. The compound legacy "Profile &
// temperature" boolean expands to the now-independent `profile` +
// `temperature` items. The six legacy item booleans are read here, never
// written (shotPlanShowSteamPlan is a live key, not one of them).
function itemsFor(props) {
    if (!props) props = {}
    var items = props.shotPlanItems
    if (items !== undefined && items !== null) {
        // Arrives from C++ as a QVariantList, not a JS array; normalize to strings.
        var out = []
        for (var i = 0; i < items.length; i++) out.push(String(items[i]))
        return out
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
