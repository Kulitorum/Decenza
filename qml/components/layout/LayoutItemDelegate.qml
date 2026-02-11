import QtQuick
import DecenzaDE1

Item {
    id: root

    required property var modelData
    required property string zoneName

    readonly property string itemType: modelData.type || ""
    readonly property string itemId: modelData.id || ""

    // Is this a bar zone (compact rendering)?
    readonly property bool isCompact: zoneName.startsWith("top") || zoneName.startsWith("bottom") || zoneName === "statusBar"

    // Action button types that get compiled to CustomItem in center zones
    readonly property bool isCompiledType: {
        switch (itemType) {
            case "espresso":
            case "steam":
            case "hotwater":
            case "flush":
            case "beans":
            case "history":
            case "settings":
            case "autofavorites":
            case "sleep":
            case "quit":
                return true
            default:
                return false
        }
    }

    // Whether this item is rendered as a compiled CustomItem
    readonly property bool isCompiled: !isCompact && isCompiledType

    // Compile action button type to CustomItem-compatible modelData
    // Returns object with emoji, content, action, etc.
    function compileToCustom(type) {
        // Reference for translation reactivity
        var _ = typeof TranslationManager !== "undefined" ? TranslationManager.translationVersion : 0

        switch (type) {
            case "espresso": return {
                emoji: "qrc:/icons/espresso.svg",
                content: TranslationManager.translate("idle.button.espresso", "Espresso"),
                action: "togglePreset:espresso",
                longPressAction: "navigate:profiles",
                doubleclickAction: "navigate:profiles",
                backgroundColor: Settings.selectedFavoriteProfile === -1 ? Theme.highlightColor : Theme.primaryColor
            }
            case "steam": return {
                emoji: "qrc:/icons/steam.svg",
                content: TranslationManager.translate("idle.button.steam", "Steam"),
                action: "togglePreset:steam",
                longPressAction: "navigate:steam",
                doubleclickAction: "navigate:steam",
                backgroundColor: Theme.primaryColor
            }
            case "hotwater": return {
                emoji: "qrc:/icons/water.svg",
                content: TranslationManager.translate("idle.button.hotwater", "Hot Water"),
                action: "togglePreset:hotwater",
                longPressAction: "navigate:hotwater",
                doubleclickAction: "navigate:hotwater",
                backgroundColor: Theme.primaryColor
            }
            case "flush": return {
                emoji: "qrc:/icons/flush.svg",
                content: TranslationManager.translate("idle.button.flush", "Flush"),
                action: "togglePreset:flush",
                longPressAction: "navigate:flush",
                doubleclickAction: "navigate:flush",
                backgroundColor: Theme.primaryColor
            }
            case "beans": return {
                emoji: "qrc:/icons/coffeebeans.svg",
                content: TranslationManager.translate("idle.button.beaninfo", "Beans"),
                action: "togglePreset:beans",
                longPressAction: "navigate:beaninfo",
                doubleclickAction: "navigate:beaninfo",
                backgroundColor: Settings.selectedBeanPreset === -1 ? Theme.highlightColor : Theme.primaryColor
            }
            case "history": return {
                emoji: "qrc:/icons/history.svg",
                content: TranslationManager.translate("idle.button.history", "History"),
                action: "navigate:history",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: Theme.primaryColor
            }
            case "settings": return {
                emoji: "qrc:/icons/settings.svg",
                content: TranslationManager.translate("idle.button.settings", "Settings"),
                action: "navigate:settings",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: Theme.primaryColor
            }
            case "autofavorites": return {
                emoji: "qrc:/icons/star.svg",
                content: TranslationManager.translate("idle.button.autofavorites", "Favorites"),
                action: "navigate:autofavorites",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: Theme.primaryColor
            }
            case "sleep": return {
                emoji: "qrc:/icons/sleep.svg",
                content: TranslationManager.translate("idle.button.sleep", "Sleep"),
                action: "command:sleep",
                longPressAction: "command:quit",
                doubleclickAction: "",
                backgroundColor: "#555555"
            }
            case "quit": return {
                emoji: "qrc:/icons/quit.svg",
                content: TranslationManager.translate("idle.button.quit", "Quit"),
                action: "command:quit",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: "#555555"
            }
            default: return null
        }
    }

    // Accessibility: expose delegate to screen readers so dynamically added items are discoverable
    Accessible.role: Accessible.Pane
    Accessible.name: {
        switch (root.itemType) {
            case "espresso":         return "Espresso"
            case "steam":            return "Steam"
            case "hotwater":         return "Hot Water"
            case "flush":            return "Flush"
            case "beans":            return "Beans"
            case "history":          return "History"
            case "settings":         return "Settings"
            case "autofavorites":    return "Favorites"
            case "sleep":            return "Sleep"
            case "quit":             return "Quit"
            case "temperature":      return "Temperature"
            case "waterLevel":       return "Water Level"
            case "connectionStatus": return "Connection Status"
            case "scaleWeight":      return "Scale Weight"
            case "shotPlan":         return "Shot Plan"
            case "weather":          return "Weather"
            case "pageTitle":        return "Page Title"
            case "steamTemperature": return "Steam Temperature"
            case "custom":           return "Custom Widget"
            case "lastShot":         return "Last Shot"
            default:                 return root.itemType
        }
    }
    Accessible.focusable: true

    // Track loaded item's implicit size so the parent RowLayout allocates
    // the correct width (Loader alone doesn't re-propagate after property
    // bindings are set up in onLoaded)
    implicitWidth: loader.item ? loader.item.implicitWidth : 0
    implicitHeight: loader.item ? loader.item.implicitHeight : 0

    Loader {
        id: loader
        anchors.fill: parent

        source: {
            // In center zones, compile action buttons to CustomItem
            if (root.isCompiled)
                return Qt.resolvedUrl("items/CustomItem.qml")

            // Original routing for compact mode and non-action types
            var src = ""
            switch (root.itemType) {
                case "espresso":         src = "items/EspressoItem.qml"; break
                case "steam":            src = "items/SteamItem.qml"; break
                case "hotwater":         src = "items/HotWaterItem.qml"; break
                case "flush":            src = "items/FlushItem.qml"; break
                case "beans":            src = "items/BeansItem.qml"; break
                case "history":          src = "items/HistoryItem.qml"; break
                case "autofavorites":    src = "items/AutoFavoritesItem.qml"; break
                case "sleep":            src = "items/SleepItem.qml"; break
                case "settings":         src = "items/SettingsItem.qml"; break
                case "temperature":      src = "items/TemperatureItem.qml"; break
                case "waterLevel":       src = "items/WaterLevelItem.qml"; break
                case "connectionStatus": src = "items/ConnectionStatusItem.qml"; break
                case "scaleWeight":      src = "items/ScaleWeightItem.qml"; break
                case "shotPlan":         src = "items/ShotPlanItem.qml"; break
                case "spacer":           src = "items/SpacerItem.qml"; break
                case "custom":           src = "items/CustomItem.qml"; break
                case "weather":          src = "items/WeatherItem.qml"; break
                case "pageTitle":        src = "items/PageTitleItem.qml"; break
                case "steamTemperature": src = "items/SteamTemperatureItem.qml"; break
                case "separator":        src = "items/SeparatorItem.qml"; break
                case "quit":             src = "items/QuitItem.qml"; break
                case "lastShot":         src = "items/LastShotItem.qml"; break
                case "screensaverFlipClock":
                case "screensaverPipes":
                case "screensaverAttractor":
                case "screensaverShotMap":   src = "items/ScreensaverItem.qml"; break
                default:                 src = ""; break
            }
            return src ? Qt.resolvedUrl(src) : ""
        }

        onLoaded: {
            item.isCompact = Qt.binding(function() { return root.isCompact })
            item.itemId = root.itemId

            if (root.isCompiled) {
                // Compiled items: reactive binding merges original modelData with compiled properties
                item.modelData = Qt.binding(function() {
                    var compiled = root.compileToCustom(root.itemType)
                    if (!compiled) return root.modelData
                    var merged = { id: root.modelData.id, type: root.modelData.type }
                    for (var key in compiled) {
                        if (compiled.hasOwnProperty(key))
                            merged[key] = compiled[key]
                    }
                    return merged
                })
            } else {
                if (typeof item.modelData !== "undefined") {
                    item.modelData = Qt.binding(function() { return root.modelData })
                }
            }
            // Bind loaded item to fill the Loader so it gets the correct size
            // from the parent Layout (implicit size flows up, actual size flows down)
            item.anchors.fill = loader
        }

        onStatusChanged: {
            if (status === Loader.Error) {
            } else if (status === Loader.Null) {
            } else if (status === Loader.Loading) {
            }
        }
    }
}
