import QtQuick
import Decenza

Item {
    id: root

    required property var modelData
    required property string zoneName

    // Zone style propagation (composable-brew-bar): a styled zone (e.g. accentBar)
    // passes down the contrast text color and value emphasis; widgets that support
    // it bind these, otherwise they keep their own theme colors.
    property color zoneTextColor: Theme.textColor
    // Fill for the chips and tiles a widget draws. Set only by LayoutPreview, when it is
    // previewing a background colour that has not been applied and Theme's own values
    // still describe the CURRENT background. Transparent = unset, use Theme.
    property color zoneFillOverride: "transparent"
    property bool zoneValueBold: false
    // The zone's style preset ("standard" | "surface" | "accentBar"). Widgets that
    // render a filled chip (Ratio/Grind pills) key off this: the light-capsule +
    // accent-text look only reads on an accentBar, so elsewhere they use a themed
    // surface chip instead of a bare zoneTextColor fill (white in dark mode).
    property string zoneStyle: "standard"

    readonly property string itemType: modelData.type || ""
    readonly property string itemId: modelData.id || ""

    // Per-zone item-size override ("auto" | "compact" | "large"). "auto" keeps
    // the zone's natural style (bar zones compact, center zones large); the
    // others force the widget's compact or large rendering regardless of zone.
    property string itemSize: "auto"

    // Is this rendered compact (bar style) vs large (center style)?
    readonly property bool isCompact: {
        if (root.itemSize === "large") return false
        if (root.itemSize === "compact") return true
        return zoneName.startsWith("top") || zoneName.startsWith("bottom") || zoneName === "statusBar" || zoneName === "lowerMidBar"
    }

    // Action button types that get compiled to CustomItem in center zones
    readonly property bool isCompiledType: {
        switch (itemType) {
            case "espresso":
            case "steam":
            case "hotwater":
            case "flush":
            case "beans":
            case "recipes":
            case "equipment":
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
    // The fill compiled action tiles are built with. Normally the theme's, but LayoutPreview
    // overrides it while previewing a background colour that has not been applied — without
    // this the compiled tiles kept the CURRENT background's fill while everything around
    // them followed the candidate.
    readonly property color _tileFill: zoneFillOverride.a > 0 ? zoneFillOverride
                                                              : Theme.actionTileColor

    function compileToCustom(type) {
        // Reference for translation reactivity
        var _ = typeof TranslationManager !== "undefined" && TranslationManager !== null ? TranslationManager.translationVersion : 0

        switch (type) {
            case "espresso": return {
                emoji: "qrc:/icons/profile.svg",
                content: TranslationManager.translate("idle.button.profiles", "Profiles"),
                action: "togglePreset:espresso",
                backgroundColor: Settings.app.selectedFavoriteProfile === -1 ? Theme.highlightColor : root._tileFill
            }
            case "steam": return {
                emoji: "qrc:/icons/steam.svg",
                content: TranslationManager.translate("idle.button.steam", "Steam"),
                action: "togglePreset:steam",
                backgroundColor: root._tileFill
            }
            case "hotwater": return {
                emoji: "qrc:/icons/water.svg",
                content: TranslationManager.translate("idle.button.hotwater", "Hot Water"),
                action: "togglePreset:hotwater",
                backgroundColor: root._tileFill
            }
            case "flush": return {
                emoji: "qrc:/icons/flush.svg",
                content: TranslationManager.translate("idle.button.flush", "Flush"),
                action: "togglePreset:flush",
                backgroundColor: root._tileFill
            }
            case "beans": return {
                emoji: "qrc:/icons/coffeebeans.svg",
                content: TranslationManager.translate("idle.button.beaninfo", "Beans"),
                action: "togglePreset:beans",
                backgroundColor: Settings.dye.activeBagId <= 0 ? Theme.highlightColor : root._tileFill
            }
            case "recipes": return {
                emoji: "qrc:/icons/espresso.svg",
                content: TranslationManager.translate("idle.button.recipes", "Recipes"),
                action: "togglePreset:recipes",
                backgroundColor: root._tileFill
            }
            case "equipment": return {
                emoji: "qrc:/icons/grind.svg",
                content: TranslationManager.translate("idle.button.equipment", "Equipment"),
                action: "togglePreset:equipment",
                backgroundColor: root._tileFill
            }
            case "history": return {
                emoji: "qrc:/icons/history.svg",
                content: TranslationManager.translate("idle.button.history", "History"),
                action: "navigate:history",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: root._tileFill
            }
            case "settings": return {
                emoji: "qrc:/icons/settings.svg",
                content: TranslationManager.translate("idle.button.settings", "Settings"),
                action: "navigate:settings",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: root._tileFill
            }
            case "autofavorites": return {
                emoji: "qrc:/icons/star.svg",
                content: TranslationManager.translate("idle.button.autofavorites", "Favorites"),
                action: "navigate:autofavorites",
                longPressAction: "",
                doubleclickAction: "",
                backgroundColor: root._tileFill
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

    // Accessibility: delegate is invisible to screen readers — child items
    // handle their own Accessible.role/name/focusable. Without this, TalkBack
    // sees both the Pane wrapper AND the child as separate focus targets,
    // requiring a double-swipe to reach each widget.
    Accessible.ignored: true

    // Track loaded item's implicit size so the parent RowLayout allocates
    // the correct width (Loader alone doesn't re-propagate after property
    // bindings are set up in onLoaded)
    // `loader.item` is a QObject; every widget source under items/ roots at
    // LayoutWidgetItem, so the cast is what makes these reads checkable rather than assumed.
    readonly property LayoutWidgetItem widget: loader.item as LayoutWidgetItem

    implicitWidth: root.widget ? root.widget.implicitWidth : 0
    implicitHeight: root.widget ? root.widget.implicitHeight : 0

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
                case "recipes":          src = "items/RecipesItem.qml"; break
                case "equipment":        src = "items/EquipmentItem.qml"; break
                case "history":          src = "items/HistoryItem.qml"; break
                case "autofavorites":    src = "items/AutoFavoritesItem.qml"; break
                case "sleep":            src = "items/SleepItem.qml"; break
                case "settings":         src = "items/SettingsItem.qml"; break
                case "temperature":      src = "items/TemperatureItem.qml"; break
                case "waterLevel":       src = "items/WaterLevelItem.qml"; break
                case "clock":            src = "items/ClockItem.qml"; break
                // connectionStatus merged into machineStatus (alias): the machine
                // status shows the phase and "Disconnected" when offline, replacing
                // the old binary Online/Offline widget.
                case "connectionStatus":
                case "machineStatus":    src = "items/MachineStatusItem.qml"; break
                case "scaleWeight":      src = "items/ScaleWeightItem.qml"; break
                case "profileName":      src = "items/ProfileNameItem.qml"; break
                case "doseWeight":       src = "items/DoseWeightItem.qml"; break
                case "milkWeight":       src = "items/MilkWeightItem.qml"; break
                case "ratioQuickSelect": src = "items/RatioQuickSelectItem.qml"; break
                case "grindQuickSelect": src = "items/GrindQuickSelectItem.qml"; break
                case "shotPlan":         src = "items/ShotPlanItem.qml"; break
                case "spacer":           src = "items/SpacerItem.qml"; break
                case "custom":           src = "items/CustomItem.qml"; break
                case "weather":          src = "items/WeatherItem.qml"; break
                case "pageTitle":        src = "items/PageTitleItem.qml"; break
                case "steamTemperature": src = "items/SteamTemperatureItem.qml"; break
                case "separator":        src = "items/SeparatorItem.qml"; break
                case "quit":             src = "items/QuitItem.qml"; break
                case "lastShot":         src = "items/LastShotItem.qml"; break
                case "batteryLevel":     src = "items/BatteryLevelItem.qml"; break
                case "scaleBattery":     src = "items/ScaleBatteryItem.qml"; break
                case "ghcSimulator":     src = "items/MiniGHCItem.qml"; break
                case "discuss":          src = "items/DiscussItem.qml"; break
                case "screensaverFlipClock":
                case "screensaverPipes":
                case "screensaverAttractor":
                case "screensaverShotMap":   src = "items/ScreensaverItem.qml"; break
                default:
                    // Empty source means the Loader never loads, so neither onLoaded nor the
                    // Loader.Error branch of onStatusChanged below ever fires — an unrecognised
                    // type would vanish from the layout with no diagnostic at all.
                    console.warn("LayoutItemDelegate: unknown widget type '" + root.itemType
                                 + "' (id '" + root.itemId + "') — not rendered")
                    src = ""
                    break
            }
            return src ? Qt.resolvedUrl(src) : ""
        }

        onLoaded: {
            // Cast the handler's own `item`, not root.widget — the binding that feeds
            // root.widget has no guaranteed ordering against this handler.
            var widget = item as LayoutWidgetItem
            if (!widget) {
                // Not just unstyled: the early return also skips the anchors.fill below, and
                // root.widget stays null so implicitWidth/implicitHeight are 0 — the widget
                // takes no space and paints nothing.
                console.warn("LayoutItemDelegate: widget type '" + root.itemType + "' (id '"
                             + root.itemId + "') does not root at LayoutWidgetItem — "
                             + "it will not render")
                return
            }

            widget.isCompact = Qt.binding(function() { return root.isCompact })

            // Zone style propagation (composable-brew-bar). Every widget inherits these
            // from LayoutWidgetItem now, so the bindings are unconditional; the
            // `typeof item.zoneX !== "undefined"` probes this replaced were each an
            // unchecked read on a QObject, and silently skipped a widget whose property
            // had been renamed rather than reporting it.
            widget.zoneTextColor = Qt.binding(function() { return root.zoneTextColor })
            widget.zoneFillOverride = Qt.binding(function() { return root.zoneFillOverride })
            widget.zoneValueBold = Qt.binding(function() { return root.zoneValueBold })
            widget.zoneStyle = Qt.binding(function() { return root.zoneStyle })

            if (root.isCompiled) {
                // Compiled items: reactive binding merges original modelData with compiled properties
                widget.modelData = Qt.binding(function() {
                    var compiled = root.compileToCustom(root.itemType)
                    if (!compiled) return root.modelData
                    var merged = { id: root.modelData.id, type: root.modelData.type }
                    for (var key in compiled) {
                        if (compiled.hasOwnProperty(key))
                            merged[key] = compiled[key]
                    }
                    // Gestures (layout-widget-gesture-overrides): the user's stored
                    // override if there is one, otherwise the type's RESERVED
                    // destination — read from the C++ table, which is the only place
                    // that destination is declared. The compiled entries above used to
                    // spell it out a second time, and the dedicated items a third.
                    //
                    // Deliberately ONLY these two keys: the merge above rebuilds the
                    // widget from its type on purpose, so a layout saved long ago
                    // cannot resurrect a stale `content`, `emoji` or `backgroundColor`
                    // that the compiled defaults have since changed. Widening this to
                    // every stored key would silently change how existing widgets look.
                    var reserved = Settings.network.gestureReservedActionForType(root.itemType)
                    var gestureKeys = ["longPressAction", "doubleclickAction"]
                    for (var g = 0; g < gestureKeys.length; ++g) {
                        var gk = gestureKeys[g]
                        var stored = root.modelData[gk]
                        if (stored !== undefined && stored !== "")
                            merged[gk] = stored
                        else if (reserved)
                            merged[gk] = reserved
                    }
                    return merged
                })
            } else {
                widget.modelData = Qt.binding(function() { return root.modelData })
            }
            // Bind loaded item to fill the Loader so it gets the correct size
            // from the parent Layout (implicit size flows up, actual size flows down)
            widget.anchors.fill = loader
        }

        onStatusChanged: {
            if (status === Loader.Error)
                console.warn("LayoutItemDelegate: failed to load widget type '" + root.itemType
                             + "' (id '" + root.itemId + "') from " + source)
        }
    }
}
