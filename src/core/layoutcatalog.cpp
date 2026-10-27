#include "layoutcatalog.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QVector>
#include <cstring>

// Capability schema: the single source of truth for which per-instance option
// keys each widget type supports. Readout types list the sections the unified
// readout options editor shows; types with a dedicated editor (custom, sleep,
// shot plan, last shot, screensavers) are configurable but carry no readout
// keys. The web editor receives this same table as JSON (see
// readoutCapabilitiesJson), so adding a type or key here is the only
// editor-side registration step — the widget's QML item must still read and
// render the keys it declares (modelData.displayMode / modelData.color).
namespace {
const QHash<QString, QStringList>& readoutOptionSchema() {
    static const QHash<QString, QStringList> schema = {
        { QStringLiteral("machineStatus"),    { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("temperature"),      { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("steamTemperature"), { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("waterLevel"),       { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("clock"),            { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("scaleWeight"),      { QStringLiteral("dataMode"), QStringLiteral("displayMode"),
                                                QStringLiteral("showRatio"), QStringLiteral("color") } },
        { QStringLiteral("batteryLevel"),     { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("scaleBattery"),     { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("doseWeight"),       { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("milkWeight"),       { QStringLiteral("displayMode"), QStringLiteral("color") } },
        { QStringLiteral("profileName"),      { QStringLiteral("color") } },
        // Built-in ACTION widgets (layout-widget-gesture-overrides). They carry no
        // readout keys; what makes them configurable is that a user may override a
        // gesture. Listing them here is what turns on the has-options indicator and
        // the instance editor for them, on both surfaces.
        { QStringLiteral("recipes"),          { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("beans"),            { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("steam"),            { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("hotwater"),         { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("flush"),            { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("espresso"),         { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("equipment"),        { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("history"),          { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("autofavorites"),    { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
        { QStringLiteral("settings"),         { QStringLiteral("longPressAction"), QStringLiteral("doubleclickAction") } },
    };
    return schema;
}

// Which gesture a widget type must KEEP for reaching its own page, expressed as the
// navigate action that gesture performs.
//
// These seven widgets spend their tap on an operation (togglePreset:<mode>), so their page
// is reachable ONLY by long-press and double-click — and today BOTH open it. That
// redundancy is the budget a gesture override spends: override one, and the other stays
// bound to the page. Whichever gesture the user leaves empty is the reserved one, so the
// choice of which carries the action is theirs.
//
// A type ABSENT from this table has no reservation: history, autofavorites and settings
// open their page on TAP, so both gestures are genuinely free.
//
// Declared here so both editors DERIVE the rule — which slot locks, and the label for the
// destination it opens (resolved through layoutActionLabels()). The alternative is two
// hand-written lists that must agree about ten widgets, which is the drift the action
// catalog was just centralized to end.
const QHash<QString, QString>& gestureReservedDestination() {
    static const QHash<QString, QString> kReserved = {
        { QStringLiteral("recipes"),   QStringLiteral("navigate:recipeList") },
        { QStringLiteral("beans"),     QStringLiteral("navigate:beaninfo") },
        { QStringLiteral("steam"),     QStringLiteral("navigate:steam") },
        { QStringLiteral("hotwater"),  QStringLiteral("navigate:hotwater") },
        { QStringLiteral("flush"),     QStringLiteral("navigate:flush") },
        { QStringLiteral("espresso"),  QStringLiteral("navigate:profiles") },
        { QStringLiteral("equipment"), QStringLiteral("navigate:equipment") },
    };
    return kReserved;
}

// Types with a dedicated editor popup (no readout option keys). Screensavers
// are covered by prefix in typeHasBespokeEditor, not listed here.
const QSet<QString>& bespokeEditorTypes() {
    static const QSet<QString> kBespoke = {
        QStringLiteral("custom"),
        QStringLiteral("sleep"),
        QStringLiteral("shotPlan"),
        QStringLiteral("lastShot"),
    };
    return kBespoke;
}

bool typeHasBespokeEditor(const QString& type) {
    // Every screensaver opens the screensaver editor (some show only "no
    // settings", but they still route to an editor — keep parity with the
    // open behaviour so the indicator and the gesture agree).
    if (type.startsWith(QLatin1String("screensaver")))
        return true;
    return bespokeEditorTypes().contains(type);
}

// Per-type default display mode for readouts whose "today's rendering" is not
// value-only. An absent stored displayMode always means this default. Consumed
// by ReadoutOptionsPopup, the battery item components, and the web editor
// (injected as WIDGET_DISPLAY_DEFAULTS). NOTE: the other item components
// hard-code the "text" default — giving a type a non-text default here also
// requires its component to call defaultDisplayModeForType (as the battery
// items do), or the editors and the widget will disagree.
const QHash<QString, QString>& displayModeDefaults() {
    static const QHash<QString, QString> kDefaults = {
        { QStringLiteral("batteryLevel"), QStringLiteral("icon") },
        { QStringLiteral("scaleBattery"), QStringLiteral("icon") },
    };
    return kDefaults;
}

// Widget catalog: the single declaration of every placeable widget type — its
// palette category, palette label, and short chip label (both as translation
// key + English fallback), plus the special/screensaver flag the web editor
// uses to color chips and add-menu items. Consumed by the in-app palette +
// chip names (LayoutEditorZone.qml), the library card (LibraryItemCard.qml),
// and the web editor (injected as WIDGET_CATALOG). Table order is not display
// order — both pickers group by category and sort by label at runtime.
// `inPalette=false` marks legacy aliases that need a display name but are not
// offered in the add-widget picker.
struct WidgetCatalogEntry {
    const char* type;
    int cat;              // indexes the category-name list below
    const char* labelKey; // palette label translation key
    const char* label;    // palette label English fallback
    const char* chipKey;  // chip/short name translation key
    const char* chip;     // chip/short name English fallback
    const char* flag;     // "" | "special" | "screensaver" (web chip + add-menu coloring)
    bool inPalette;
};

const QVector<WidgetCatalogEntry>& widgetCatalogTable() {
    static const QVector<WidgetCatalogEntry> kCatalog = {
        // Actions (0)
        { "espresso",      0, "layoutEditor.widgetProfiles",  "Profiles",  "layoutEditor.chipProfiles",  "Profiles",  "", true },
        { "steam",         0, "layoutEditor.widgetSteam",     "Steam",     "layoutEditor.chipSteam",     "Steam",     "", true },
        { "hotwater",      0, "layoutEditor.widgetHotWater",  "Hot Water", "layoutEditor.chipHotWater",  "Hot Water", "", true },
        { "flush",         0, "layoutEditor.widgetFlush",     "Flush",     "layoutEditor.chipFlush",     "Flush",     "", true },
        { "sleep",         0, "layoutEditor.widgetSleep",     "Sleep",     "layoutEditor.chipSleep",     "Sleep",     "", true },
        { "settings",      0, "layoutEditor.widgetSettings",  "Settings",  "layoutEditor.chipSettings",  "Settings",  "", true },
        { "quit",          0, "layoutEditor.widgetQuit",      "Quit",      "layoutEditor.chipQuit",      "Quit",      "special", true },
        { "history",       0, "layoutEditor.widgetHistory",   "History",   "layoutEditor.chipHistory",   "History",   "", true },
        { "beans",         0, "layoutEditor.widgetBeans",     "Beans",     "layoutEditor.chipBeans",     "Beans",     "", true },
        { "recipes",       0, "layoutEditor.widgetRecipes",   "Recipes",   "layoutEditor.chipRecipes",   "Recipes",   "", true },
        { "equipment",     0, "layoutEditor.widgetEquipment", "Equipment", "layoutEditor.chipEquipment", "Equipment", "", true },
        { "autofavorites", 0, "layoutEditor.widgetFavorites", "Favorites", "layoutEditor.chipFavorites", "Favorites", "", true },
        { "discuss",       0, "layoutEditor.widgetDiscuss",   "Discuss",   "layoutEditor.chipDiscuss",   "Discuss",   "", true },
        { "ghcSimulator",  0, "layoutEditor.widgetGHCSimulator", "Mini GHC", "layoutEditor.chipGHCSim",  "Mini GHC",  "", true },
        // Readouts (1)
        { "machineStatus",    1, "layoutEditor.widgetMachineStatus", "Machine Status", "layoutEditor.chipMachine",    "Machine",    "", true },
        { "scaleWeight",      1, "layoutEditor.widgetScaleWeight",   "Scale Weight",   "layoutEditor.chipScale",      "Scale",      "", true },
        { "temperature",      1, "layoutEditor.widgetTemperature",   "Temperature",    "layoutEditor.chipTemp",       "Temp",       "", true },
        { "steamTemperature", 1, "layoutEditor.widgetSteamTemp",     "Steam Temp",     "layoutEditor.chipSteamTemp",  "Steam Temp", "", true },
        { "batteryLevel",     1, "layoutEditor.widgetBatteryLevel",  "Battery Level",  "layoutEditor.chipBattery",    "Battery",    "", true },
        { "scaleBattery",     1, "layoutEditor.widgetScaleBattery",  "Scale Battery",  "layoutEditor.chipScaleBat",   "Scale Bat",  "", true },
        { "waterLevel",       1, "layoutEditor.widgetWaterLevel",    "Water Level",    "layoutEditor.chipWater",      "Water",      "", true },
        { "profileName",      1, "layoutEditor.widgetProfileName",   "Profile Name",   "layoutEditor.chipProfileName","Profile",    "", true },
        { "doseWeight",       1, "layoutEditor.widgetDoseWeight",    "Dose Weight",    "layoutEditor.chipDoseWeight", "Dose",       "", true },
        { "milkWeight",       1, "layoutEditor.widgetMilkWeight",    "Milk Weight",    "layoutEditor.chipMilkWeight", "Milk",       "", true },
        { "ratioQuickSelect", 1, "layoutEditor.widgetRatioQuickSelect", "Ratio Quick-Select", "layoutEditor.chipRatioQuick", "Ratio", "", true },
        { "grindQuickSelect", 1, "layoutEditor.widgetGrindQuickSelect", "Grind Quick-Select", "layoutEditor.chipGrindQuick", "Grind", "", true },
        { "temperatureQuickSelect", 1, "layoutEditor.widgetTempQuickSelect", "Temp Quick-Select", "layoutEditor.chipTempQuick", "Temp Quick", "", true },
        { "shotPlan",         1, "layoutEditor.widgetShotPlan",      "Shot Plan",      "layoutEditor.chipShotPlan",   "Shot Plan",  "", true },
        // The clock palette label reuses the chip key — pre-existing (the widget
        // was renamed to "Time" and the chip key kept for translations).
        { "clock",            1, "layoutEditor.chipTime",            "Time",           "layoutEditor.chipTime",       "Time",       "", true },
        // Legacy alias: merged into machineStatus. The active-layout migration
        // rewrites it on load, but saved library items/layouts keep the old
        // type, so it needs a chip name while staying out of the palette.
        { "connectionStatus", 1, "layoutEditor.chipMachine",         "Machine",        "layoutEditor.chipMachine",    "Machine",    "", false },
        // Utility (2)
        { "custom",    2, "layoutEditor.widgetCustom",    "Custom",     "layoutEditor.chipCustom",    "Custom",     "special", true },
        { "pageTitle", 2, "layoutEditor.widgetPageTitle", "Page Title", "layoutEditor.chipPageTitle", "Page Title", "special", true },
        { "separator", 2, "layoutEditor.widgetSeparator", "Separator",  "layoutEditor.chipSep",       "Sep",        "special", true },
        { "spacer",    2, "layoutEditor.widgetSpacer",    "Spacer",     "layoutEditor.chipSpacer",    "Spacer",     "special", true },
        { "weather",   2, "layoutEditor.widgetWeather",   "Weather",    "layoutEditor.chipWeather",   "Weather",    "special", true },
        // Screensavers (3)
        { "screensaverPipes",     3, "layoutEditor.widget3DPipes",    "3D Pipes",   "layoutEditor.chipPipes",     "Pipes",     "screensaver", true },
        { "screensaverAttractor", 3, "layoutEditor.widgetAttractors", "Attractors", "layoutEditor.chipAttractor", "Attractor", "screensaver", true },
        { "screensaverFlipClock", 3, "layoutEditor.widgetFlipClock",  "Flip Clock", "layoutEditor.chipClock",     "Clock",     "screensaver", true },
        { "lastShot",             3, "layoutEditor.widgetLastShot",   "Last Shot",  "layoutEditor.chipLastShot",  "Last Shot", "screensaver", true },
        { "screensaverShotMap",   3, "layoutEditor.widgetShotMap",    "Shot Map",   "layoutEditor.chipMap",       "Map",       "screensaver", true },
    };
    return kCatalog;
}

struct WidgetCategoryName {
    const char* key;
    const char* fallback;
};

const QVector<WidgetCategoryName>& widgetCategoryTable() {
    static const QVector<WidgetCategoryName> kCategories = {
        { "layoutEditor.catActions",      "Actions" },
        { "layoutEditor.catReadouts",     "Readouts" },
        { "layoutEditor.catUtility",      "Utility" },
        { "layoutEditor.catScreensavers", "Screensavers" },
    };
    return kCategories;
}

// Action catalog: the single declaration of every action a Custom widget can be
// assigned to a gesture. Consumed by the in-app action picker
// (CustomEditorPopup.qml) and the web layout editor (injected as ACTIONS).
//
// This replaces two hand-written copies that had already drifted: the web list
// was sixteen entries behind the in-app one and labelled `command:scanScale`
// differently in each. Nothing failed when they diverged, which is exactly why
// they did.
//
// Two flags, each stating a FACT about the action. Nothing here names a
// consequence — the surfaces derive their own rules, so an action does not have
// to be mislabelled to get the behaviour it needs.
//
// `inPicker=false` — a legacy alias a stored layout may still carry. It still
// needs a LABEL: `command:scanDE1` dispatches identically to
// `command:scanScale` (both call BLEManager::scanForDevices()), so offering both
// would be two rows that do the same thing, but a widget saved with it must not
// render as its raw id. Same role as `inPalette` in the widget catalog above,
// and note how that one delivers on the label half — widgetChipNames() and
// widgetCatalogJson()'s chipNames iterate the WHOLE table before the inPalette
// skip. layoutActionLabels() below is the analogue.
//
// `expandsToSubmenu=true` — selecting it opens a second list rather than
// committing, because the stored form carries an argument
// (`command:loadProfile:<filename>`). Two things DERIVE from that one fact: the
// in-app picker appends "..." to the label, and the web editor omits the action
// entirely, because it has no submenu and its pickAction() would store the bare
// id, which CustomItem then rejects. Centralizing the catalog is what put such
// an action in reach of that surface; deriving the exclusion is what keeps the
// fix from creating a dead button. An earlier draft stored this inverted, as
// `webCapable`, and published `!webCapable` under this name — one fact, two
// names, negated between them, so an action that was web-incapable for any
// OTHER reason would have silently grown an ellipsis.
//
// The picker's "None" entry is NOT here: it is an absence of an action, not an
// action, and each surface renders it its own way.
//
// Adding an entry here does NOT make it work — CustomItem.executeActionString()
// still needs the matching dispatch arm (tst_customwidgethtml asserts that).
struct LayoutActionEntry {
    const char* id;
    const char* labelKey;  // translation key, resolved in-app
    const char* label;     // English fallback; what the web editor shows
    const char* contexts;  // space-separated page contexts; "all" means every page
    bool inPicker;
    bool expandsToSubmenu;
};

const QVector<LayoutActionEntry>& layoutActionTable() {
    static const QVector<LayoutActionEntry> kActions = {
        //                            id                             translation key                          English fallback               contexts     inPicker expandsToSubmenu
        // Navigate
        { "navigate:settings",        "customaction.navigate.settings",        "Go to Settings",            "idle all", true,  false },
        { "navigate:history",         "customaction.navigate.history",         "Go to History",             "idle all", true,  false },
        { "navigate:historyRecipe",   "customaction.navigate.historyRecipe",   "Go to History (this recipe)",  "idle all", true, false },
        { "navigate:historyBean",     "customaction.navigate.historyBean",     "Go to History (this bean)",    "idle all", true, false },
        { "navigate:historyBag",      "customaction.navigate.historyBag",      "Go to History (this bag)",     "idle all", true, false },
        { "navigate:historyProfile",  "customaction.navigate.historyProfile",  "Go to History (this profile)", "idle all", true, false },
        { "navigate:profiles",        "customaction.navigate.profiles",        "Go to Profiles",            "idle all", true,  false },
        { "navigate:profileEditor",   "customaction.navigate.profileEditor",   "Go to Profile Editor",      "idle all", true,  false },
        { "navigate:dflowEditor",     "customaction.navigate.dflowEditor",     "Go to D-Flow Editor",       "idle all", true,  false },
        { "navigate:recipeList",      "customaction.navigate.recipeList",      "Go to Recipes",             "idle all", true,  false },
        { "navigate:equipment",       "customaction.navigate.equipment",       "Go to Equipment",           "idle all", true,  false },
        { "navigate:descaling",       "customaction.navigate.descaling",       "Go to Descaling",           "idle all", true,  false },
        { "navigate:ai",              "customaction.navigate.ai",              "Go to AI Settings",         "idle all", true,  false },
        { "navigate:visualizer",      "customaction.navigate.visualizer",      "Go to Visualizer",          "idle all", true,  false },
        { "navigate:autofavorites",   "customaction.navigate.autofavorites",   "Go to Favorites",           "idle all", true,  false },
        { "navigate:steam",           "customaction.navigate.steam",           "Go to Steam",               "idle",     true,  false },
        { "navigate:hotwater",        "customaction.navigate.hotwater",        "Go to Hot Water",           "idle",     true,  false },
        { "navigate:flush",           "customaction.navigate.flush",           "Go to Flush",               "idle",     true,  false },
        { "navigate:beaninfo",        "customaction.navigate.beaninfo",        "Go to Bean Info",           "idle all", true,  false },
        { "navigate:espresso",        "customaction.navigate.espresso",        "Go to Espresso",            "idle",     true,  false },
        { "navigate:flowCalibration", "customaction.navigate.flowCalibration", "Go to Flow Calibration",    "idle all", true,  false },
        { "navigate:profileImport",   "customaction.navigate.profileImport",   "Go to Profile Import",      "idle all", true,  false },
        { "navigate:shotReview",      "customaction.navigate.shotReview",      "Go to Shot Review",         "idle",     true,  false },
        // Command
        { "command:sleep",            "customaction.command.sleep",            "Sleep",                     "idle",     true,  false },
        { "command:startEspresso",    "customaction.command.startEspresso",    "Start Espresso",            "idle",     true,  false },
        { "command:startSteam",       "customaction.command.startSteam",       "Start Steam",               "idle",     true,  false },
        { "command:startHotWater",    "customaction.command.startHotWater",    "Start Hot Water",           "idle",     true,  false },
        { "command:startFlush",       "customaction.command.startFlush",       "Start Flush",               "idle",     true,  false },
        { "command:idle",             "customaction.command.idle",             "Stop (Idle)",               "idle espresso steam hotwater flush", true, false },
        { "command:tare",             "customaction.command.tare",             "Tare Scale",                "idle espresso all", true, false },
        { "command:scanScale",        "customaction.command.scanScale",        "Scan for Devices",          "idle all", true,  false },
        { "command:brewSettings",     "customaction.command.brewSettings",     "Open Brew Settings",        "idle",     true,  false },
        { "command:tempToggleSteam",  "customaction.command.tempToggleSteam",  "Toggle Steam (temporary)",  "idle",     true,  false },
        { "command:toggleCharging",   "customaction.command.toggleCharging",   "Toggle Charging Mode",      "idle all", true,  false },
        { "command:uploadVisualizer", "customaction.command.uploadShot",       "Upload last shot",          "idle",     true,  false },
        { "command:disconnectDE1",    "customaction.command.disconnectDE1",    "Disconnect DE1",            "idle",     true,  false },
        // The one parameterized action — see the expandsToSubmenu note above.
        { "command:loadProfile",      "customaction.command.loadProfile",      "Load Profile",              "idle",     true,  true },
        { "command:previousProfile",  "customaction.command.previousProfile",  "Previous Profile",          "idle",     true,  false },
        { "command:quit",             "customaction.command.quit",             "Quit App",                  "idle",     true,  false },
        // Legacy alias, see inPicker note above.
        { "command:scanDE1",          "customaction.command.scanDE1",          "Scan for DE1",              "idle all", false, false },
    };
    return kActions;
}
} // namespace

bool LayoutCatalog::typeHasOptions(const QString& type) {
    return typeHasBespokeEditor(type) || readoutOptionSchema().contains(type);
}

QStringList LayoutCatalog::optionKeysForType(const QString& type) {
    return readoutOptionSchema().value(type);
}

QJsonObject LayoutCatalog::readoutCapabilitiesJson() {
    QJsonObject caps;
    const auto& schema = readoutOptionSchema();
    for (auto it = schema.constBegin(); it != schema.constEnd(); ++it)
        caps[it.key()] = QJsonArray::fromStringList(it.value());
    // Bespoke-editor types are configurable but carry no readout keys; the web
    // editor only needs their presence for its has-options behaviour.
    // (Screensavers stay a prefix rule on both sides.)
    for (const QString& type : bespokeEditorTypes())
        caps[type] = QJsonArray();
    return caps;
}

QString LayoutCatalog::defaultDisplayModeForType(const QString& type) {
    return displayModeDefaults().value(type, QStringLiteral("text"));
}

QStringList LayoutCatalog::zoneOptionValues(const QString& key) {
    static const QHash<QString, QStringList> values{
        { QStringLiteral("distribution"), { QStringLiteral("packed"), QStringLiteral("equalWidth"), QStringLiteral("spaced") } },
        { QStringLiteral("alignment"), { QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right") } },
        { QStringLiteral("style"), { QStringLiteral("standard"), QStringLiteral("surface"), QStringLiteral("accentBar") } },
        { QStringLiteral("itemSize"), { QStringLiteral("compact"), QStringLiteral("large") } },
    };
    return values.value(key);
}

QJsonObject LayoutCatalog::displayModeDefaultsJson() {
    QJsonObject defaults;
    const auto& map = displayModeDefaults();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        defaults[it.key()] = it.value();
    return defaults;
}

QVariantMap LayoutCatalog::sleepOptionDefaults() {
    // allowQuit stays on: the default layout (defaultLayoutJson) has no Quit widget, so
    // Sleep's long-press is its only in-app exit.
    return {
        {QStringLiteral("allowQuit"), true},
        {QStringLiteral("confirmQuit"), false},
        {QStringLiteral("showIcon"), true},
    };
}

QJsonObject LayoutCatalog::sleepOptionDefaultsJson() {
    return QJsonObject::fromVariantMap(sleepOptionDefaults());
}

QVariantList LayoutCatalog::widgetCatalog() {
    QVariantList list;
    for (const auto& e : widgetCatalogTable()) {
        if (!e.inPalette)
            continue;
        list.append(QVariantMap{
            { QStringLiteral("type"), QString::fromLatin1(e.type) },
            { QStringLiteral("cat"), e.cat },
            { QStringLiteral("labelKey"), QString::fromLatin1(e.labelKey) },
            { QStringLiteral("label"), QString::fromLatin1(e.label) },
            { QStringLiteral("flag"), QString::fromLatin1(e.flag) },
        });
    }
    return list;
}

QVariantMap LayoutCatalog::widgetChipNames() {
    QVariantMap map;
    for (const auto& e : widgetCatalogTable()) {
        map.insert(QString::fromLatin1(e.type), QVariantMap{
            { QStringLiteral("key"), QString::fromLatin1(e.chipKey) },
            { QStringLiteral("fallback"), QString::fromLatin1(e.chip) },
        });
    }
    return map;
}

QVariantList LayoutCatalog::widgetCategoryNames() {
    QVariantList list;
    for (const auto& c : widgetCategoryTable()) {
        list.append(QVariantMap{
            { QStringLiteral("key"), QString::fromLatin1(c.key) },
            { QStringLiteral("fallback"), QString::fromLatin1(c.fallback) },
        });
    }
    return list;
}

QJsonObject LayoutCatalog::widgetCatalogJson() {
    // Shape consumed by the web layout editor: `types` mirrors the old
    // WIDGET_TYPES entries verbatim (English labels, special/screensaver
    // flags); `chipNames` replaces the old DISPLAY_NAMES with the in-app chip
    // labels (which renamed a few drifted web short names); `catNames` the
    // old CAT_NAMES verbatim.
    QJsonArray types;
    QJsonObject chipNames;
    for (const auto& e : widgetCatalogTable()) {
        chipNames[QString::fromLatin1(e.type)] = QString::fromLatin1(e.chip);
        if (!e.inPalette)
            continue;
        QJsonObject t{
            { QStringLiteral("type"), QString::fromLatin1(e.type) },
            { QStringLiteral("cat"), e.cat },
            { QStringLiteral("label"), QString::fromLatin1(e.label) },
        };
        if (qstrcmp(e.flag, "special") == 0)
            t[QStringLiteral("special")] = true;
        else if (qstrcmp(e.flag, "screensaver") == 0)
            t[QStringLiteral("screensaver")] = true;
        types.append(t);
    }
    QJsonArray catNames;
    for (const auto& c : widgetCategoryTable())
        catNames.append(QString::fromLatin1(c.fallback));
    return QJsonObject{
        { QStringLiteral("types"), types },
        { QStringLiteral("chipNames"), chipNames },
        { QStringLiteral("catNames"), catNames },
    };
}

namespace {
// "idle all" → ["idle", "all"]. Stored as one string in the table so an entry
// stays one readable line; split once per call, which happens when a picker
// opens.
QStringList splitActionContexts(const char* contexts) {
    return QString::fromLatin1(contexts).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}
} // namespace

QVariantList LayoutCatalog::layoutActionCatalog() {
    QVariantList list;
    for (const auto& e : layoutActionTable()) {
        if (!e.inPicker)
            continue;
        list.append(QVariantMap{
            { QStringLiteral("id"), QString::fromLatin1(e.id) },
            { QStringLiteral("labelKey"), QString::fromLatin1(e.labelKey) },
            { QStringLiteral("label"), QString::fromLatin1(e.label) },
            { QStringLiteral("contexts"), splitActionContexts(e.contexts) },
            { QStringLiteral("expandsToSubmenu"), e.expandsToSubmenu },
        });
    }
    return list;
}

QVariantMap LayoutCatalog::layoutActionLabels() {
    // EVERY entry, including !inPicker — the analogue of widgetChipNames().
    // A layout can carry an action that is no longer offered, and both editors
    // fall back to printing the raw id when a lookup misses, so this is what
    // keeps `command:scanDE1` reading "Scan for DE1" rather than
    // "command:scanDE1".
    QVariantMap map;
    for (const auto& e : layoutActionTable()) {
        map.insert(QString::fromLatin1(e.id), QVariantMap{
            { QStringLiteral("key"), QString::fromLatin1(e.labelKey) },
            { QStringLiteral("fallback"), QString::fromLatin1(e.label) },
        });
    }
    return map;
}

QString LayoutCatalog::gestureReservedActionForType(const QString& type) {
    return gestureReservedDestination().value(type);
}

bool LayoutCatalog::typeSupportsGestureOverrides(const QString& type) {
    return readoutOptionSchema().value(type).contains(QStringLiteral("longPressAction"));
}

QJsonObject LayoutCatalog::layoutActionCatalogJson() {
    // Shape consumed by the web layout editor: English labels only, since that
    // editor has no translation layer. Two channels, for the same reason the
    // widget catalog has `types` and `chipNames`:
    //   actions — what this editor may OFFER (inPicker, minus parameterized)
    //   labels  — what any stored id RESOLVES to (every entry)
    // The "None" entry the picker prepends is not an action and stays on that
    // side.
    QJsonArray actions;
    QJsonObject labels;
    for (const auto& e : layoutActionTable()) {
        labels[QString::fromLatin1(e.id)] = QString::fromLatin1(e.label);
        // A parameterized action is omitted here rather than flagged: this editor
        // has no submenu to expand it with.
        if (!e.inPicker || e.expandsToSubmenu)
            continue;
        actions.append(QJsonObject{
            { QStringLiteral("id"), QString::fromLatin1(e.id) },
            { QStringLiteral("label"), QString::fromLatin1(e.label) },
            { QStringLiteral("contexts"),
              QJsonArray::fromStringList(splitActionContexts(e.contexts)) },
        });
    }
    // Gesture-override rule per widget type, so the web editor derives the reserved
    // slot exactly as the in-app one does: { type: reservedActionId }, with an empty
    // string meaning "both gestures free".
    QJsonObject gestures;
    for (auto it = readoutOptionSchema().constBegin(); it != readoutOptionSchema().constEnd(); ++it) {
        if (!it.value().contains(QStringLiteral("longPressAction")))
            continue;
        gestures[it.key()] = gestureReservedDestination().value(it.key());
    }
    return QJsonObject{
        { QStringLiteral("actions"), actions },
        { QStringLiteral("labels"), labels },
        { QStringLiteral("gestureTypes"), gestures },
    };
}


QVector<QPair<QString, QString>> LayoutCatalog::translationStrings() {
    QVector<QPair<QString, QString>> out;
    for (const auto& e : layoutActionTable())
        out.append({ QString::fromLatin1(e.labelKey), QString::fromLatin1(e.label) });
    for (const auto& e : widgetCatalogTable()) {
        out.append({ QString::fromLatin1(e.labelKey), QString::fromLatin1(e.label) });
        out.append({ QString::fromLatin1(e.chipKey), QString::fromLatin1(e.chip) });
    }
    for (const auto& c : widgetCategoryTable())
        out.append({ QString::fromLatin1(c.key), QString::fromLatin1(c.fallback) });
    return out;
}
