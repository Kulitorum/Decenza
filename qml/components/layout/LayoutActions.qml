// Dispatch for every layout-widget action string, and the context-filtered Shot History
// filters that some of them build.
//
// This lives in a singleton rather than in CustomItem because it now has TWO kinds of
// caller: the Custom widget, and the ten built-in action widgets whose gestures a user can
// override (layout-widget-gesture-overrides). Those built-ins render two ways — compiled to
// CustomItem in the center/action zones, and as their own items/<Type>Item.qml elsewhere —
// and the second kind cannot reach a function that lives on CustomItem. Copying the
// dispatch into them was the alternative, and a copied 170-line switch is the drift shape
// this codebase keeps paying for.
//
// `ctx` carries what the dispatch cannot look up for itself: `idlePage`, needed by
// togglePreset to find the preset row. Callers that have no IdlePage ancestor pass nothing
// and togglePreset warns, exactly as before.
pragma Singleton

import QtQuick
import Decenza

QtObject {
    id: layoutActions

    // ONE prefix for every diagnostic from this dispatch. It was written by hand
    // at eleven sites in two families — "CustomItem: " and "[CustomItem] " — so
    // no single grep returned the widget's story from a submitted log, which is
    // how these are actually read (docs/CLAUDE_MD/LOGGING.md). Now that built-in
    // widgets dispatch through here too, the prefix names the SUBSYSTEM rather
    // than one widget.
    function _warn(msg) {
        console.warn("LayoutActions: " + msg)
    }

    // --- Context-filtered Shot History filters (custom-widget-history-actions).
    // Each returns the props object AppShell.shotHistoryRequested expects: an
    // `initialFilter` map, or `null` when there is nothing active to filter on.
    //
    // `null`, NOT `{}`, and the difference is load-bearing. An empty props object
    // reaching the already-showing path reads as "show everything" and CLEARS the
    // filter and the search text the user typed — so a button meant to narrow
    // would wipe their work. `null` means "no context"; goToShotHistory() leaves a
    // visible list alone and only opens unfiltered History on the push path, where
    // there is no state to lose.
    //
    // Recipe and bag match on an ID, so a rename cannot orphan the filter and
    // two records sharing a name stay distinct; their names ride along under
    // recipeName/bagLabel, which ShotHistoryPage uses for the banner label ONLY
    // and never as a query term. Bean has no such id on a shot row (shots store
    // bean_brand/bean_type as text), so it matches those two strings — which is
    // also what makes it the wider of the two coffee filters: the same coffee
    // across every bag of it, where the bag filter is one bag.
    //
    // Each warns when it finds no context. Every other unresolvable case in
    // executeActionString() warns; these were the only silent arms, and "the
    // button did nothing" is exactly the report that needs a log line behind it.
    function _recipeHistoryFilter() {
        var id = Settings.dye.activeRecipeId
        if (!(id > 0)) {
            _warn("historyRecipe with no active recipe")
            return null
        }
        // activeRecipe can briefly LEAD activeRecipeId during activation
        // (maincontroller.h documents it as self-healing), so the name may not
        // have landed yet. The query is unaffected — it matches the id — but a
        // blank label would render a bare "Filtered:" with nothing after it.
        var name = MainController.activeRecipe.name || ""
        return { initialFilter: { recipeId: id, recipeName: name } }
    }

    function _beanHistoryFilter() {
        var f = {}
        // Omit rather than send "" — ShotHistoryPage drops empty fields anyway,
        // and omitting keeps a brand-only bean filtering on the brand.
        if (Settings.dye.dyeBeanBrand) f.beanBrand = Settings.dye.dyeBeanBrand
        if (Settings.dye.dyeBeanType) f.beanType = Settings.dye.dyeBeanType
        if (Object.keys(f).length === 0) {
            _warn("historyBean with no bean selected")
            return null
        }
        return { initialFilter: f }
    }

    function _bagHistoryFilter() {
        var id = Settings.dye.activeBagId
        if (!(id > 0)) {
            _warn("historyBag with no active bag")
            return null
        }
        // The bag's label, read from the DYE fields the bag writes through on
        // activation — SettingsDye::applyActiveBag() (settings_dye.cpp:776-777)
        // maps roasterName → dyeBeanBrand and coffeeName → dyeBeanType. That is
        // the path EVERY direct bag selection takes; maincontroller.cpp's copy is
        // only the recipe-linked branch. Coffee name first, roaster as fallback —
        // the rule BeansItem.bagLabel() uses.
        //
        // The read here is synchronous, but the write-through behind it is not:
        // between selecting a bag and its row arriving, activeBagId is already the
        // new bag while these fields still hold the previous one's. That can
        // mislabel the banner for an instant; it cannot mis-scope the query, which
        // matches on bagId.
        var label = Settings.dye.dyeBeanType || Settings.dye.dyeBeanBrand || ""
        return { initialFilter: { bagId: id, bagLabel: label } }
    }

    function _profileHistoryFilter() {
        // currentProfileTitle, NOT currentProfileName: the latter is a DISPLAY
        // string that becomes "*Londinium" (or "Londinium (modified)" for a
        // read-only profile) as soon as the profile is edited, while shots store
        // the bare title. Filtering on the decorated form matched nothing and
        // showed an empty list under a "Filtered:" banner — indistinguishable
        // from "you have never pulled this profile".
        var title = ProfileManager.currentProfileTitle || ""
        if (title === "") {
            _warn("historyProfile with no profile loaded")
            return null
        }
        return { initialFilter: { profileName: title } }
    }

    // The ONE place a built-in action widget asks "has the user overridden this
    // gesture?" (layout-widget-gesture-overrides). Returns true when an override ran,
    // so the caller's default is skipped:
    //
    //     onAccessibleLongPressed: if (!LayoutActions.runGesture(modelData, "longPressAction", ctx)) root.goToBeanInfo()
    //
    // Ten widgets need this, in two render formats, and a hand-written
    // "check override, else default" in each is exactly the copy that drifts. An
    // absent or empty override returns false, which is what keeps every untouched
    // widget behaving exactly as it did.
    // The action list BOTH editors offer, filtered to a page context. It lived in
    // CustomEditorPopup; the built-in widgets' options popup needs exactly the same
    // list, and a second copy of "read the catalog, filter by context, translate" is
    // the drift this catalog was centralized to end.
    // `excludeSubmenu` drops actions that need a second selection step to complete
    // (`command:loadProfile:<file>`). A surface without that sub-picker must not offer
    // them: it would store the bare id, which the dispatch then rejects — the same
    // exclusion the web editor makes, for the same reason.
    function pickerActions(pageContext, excludeSubmenu) {
        var ctx = pageContext || "idle"
        var out = []
        var catalog = Settings.network.layoutActionCatalog()
        // An empty catalog means the C++ table did not reach QML at all. The picker
        // would render as a lone "None" row, which reads like a context restriction
        // rather than a fault — say so instead of showing nothing.
        if (!catalog || catalog.length === 0) {
            _warn("layoutActionCatalog() returned nothing; the action picker will be empty")
            return out
        }
        for (var i = 0; i < catalog.length; ++i) {
            var a = catalog[i]
            if (a.contexts.indexOf(ctx) < 0 && a.contexts.indexOf("all") < 0)
                continue
            if (excludeSubmenu && a.expandsToSubmenu)
                continue
            // Reading TranslationManager.translate (a Q_PROPERTY holding a callable)
            // establishes the dependency, so these labels re-resolve on a language change.
            var label = TranslationManager.translate(a.labelKey, a.label)
            // Marked in the catalog, not matched by id here: an id comparison on the far
            // side of the C++/QML boundary goes stale in silence if the id is renamed,
            // taking with it the only cue that this row opens a second list.
            if (a.expandsToSubmenu)
                label += "..."
            out.push({ id: a.id, label: label, contexts: a.contexts })
        }
        return out
    }

    // Same list with the leading "None" entry the pickers show. "None" is the absence
    // of an action, which is why it is not in the catalog.
    // `defaultLabel`, when non-empty, adds a restore-the-default row ABOVE "None"
    // and NAMES what it restores ("Default (opens Bean Info)") — "Default" alone
    // does not tell the user what they are going back to, and going back is the
    // thing they most need to be able to do confidently.
    //
    // Only passed where unset and "nothing" actually differ: a widget that
    // reserves a destination opens a page when unset. Everywhere else unset
    // already means nothing happens, and two rows with one effect is worse than
    // one honest row.
    function pickerItems(pageContext, excludeSubmenu, defaultLabel) {
        var items = []
        if (defaultLabel)
            items.push({ id: "", label: defaultLabel })
        items.push({ id: defaultLabel ? layoutActions.kNoAction : "",
                     label: TranslationManager.translate("customeditor.action.none", "None") })
        var filtered = pickerActions(pageContext, excludeSubmenu)
        for (var i = 0; i < filtered.length; i++)
            items.push(filtered[i])
        return items
    }

    // What TalkBack/VoiceOver says after the widget's name when a gesture carries an
    // override. Action labels stay generic: a stored action string ("navigate:settings")
    // has no human-readable label at this point.
    //
    // Lives here rather than on CustomItem for the reason the dispatch does — every
    // built-in action widget needs the same sentence, and the dedicated items cannot
    // reach a property on CustomItem. Three of them announced nothing at all, which
    // matters most to the users who cannot see the widget: AccessibleTapHandler
    // disables double-tap detection in accessibility mode, so long press is the ONLY
    // secondary gesture a screen-reader user has.
    // A stored kNoAction is an explicit silence, so it announces nothing: promising a
    // secondary action that deliberately does nothing is worse than promising none.
    // CustomItem's own hint used to count it as an action; this is that fix too.
    function gestureHint(modelData) {
        var lp = modelData ? modelData.longPressAction : ""
        var dc = modelData ? modelData.doubleclickAction : ""
        var hasLP = !!lp && lp !== layoutActions.kNoAction
        var hasDC = !!dc && dc !== layoutActions.kNoAction
        if (hasLP && hasDC)
            return TranslationManager.translate("customitem.accessible.hint.both", "Long-press or double-tap for additional actions.")
        if (hasLP)
            return TranslationManager.translate("customitem.accessible.hint.longpress", "Long-press for additional action.")
        if (hasDC)
            return TranslationManager.translate("customitem.accessible.hint.doubletap", "Double-tap for additional action.")
        return ""
    }

    // Explicitly "do nothing on this gesture" — DISTINCT from unset. Unset ("")
    // means "whatever this widget does by default", which on most action widgets
    // is opening its page. A user who wants the gesture silenced needs a way to
    // say so that is not the same value as "I haven't chosen".
    readonly property string kNoAction: "none"

    function runGesture(modelData, gestureKey, ctx) {
        if (!modelData) return false
        var stored = modelData[gestureKey]
        if (!stored) return false
        // Consumed, not dispatched: returning true is what stops
        // runGestureOrReserved falling through to the reserved destination.
        if (stored === layoutActions.kNoAction) return true
        execute(stored, ctx)
        return true
    }

    // What a gesture does, all in: the user's override if they set one, otherwise the
    // widget type's RESERVED destination — the action that keeps its page reachable.
    //
    // The reserved destination is read from the C++ table and nowhere else. It used to be
    // stated three times for every widget — in compileToCustom's longPressAction, in the
    // dedicated item's goToX(), and in gestureReservedDestination() — three copies free to
    // drift, with nothing failing if they did. Now the C++ table is the only declaration
    // and both render formats resolve through here.
    function runGestureOrReserved(modelData, gestureKey, widgetType, ctx) {
        if (runGesture(modelData, gestureKey, ctx))
            return
        var reserved = Settings.network.gestureReservedActionForType(widgetType)
        if (reserved)
            execute(reserved, ctx)
    }

    function execute(actionStr, ctx) {
        if (!actionStr) return
        var parts = actionStr.split(":")
        if (parts.length < 2) {
            _warn("malformed action '" + actionStr + "' (expected 'category:target')")
            return
        }
        var category = parts[0]
        var target = parts.slice(1).join(":")

        if (category === "togglePreset") {
            var p = ctx && ctx.idlePage ? ctx.idlePage : null
            if (p && typeof p.activePresetFunction !== "undefined") {
                p.activePresetFunction = (p.activePresetFunction === target) ? "" : target
            } else {
                _warn("togglePreset couldn't find IdlePage ancestor; preset '" + target + "' not toggled")
            }
        } else if (category === "navigate") {
            // `target` is USER CONFIGURATION — the widget editor stores whichever destination the
            // user picked — so a string key is inherent here, unlike the call sites that had one
            // only because nobody had declared a name. It is dispatched to a named AppShell signal
            // rather than mapped to a page FILENAME: a bad key now warns below instead of
            // resolving to a 404 URL, and the shell decides push-vs-replace.
            //
            // The operation pages used to `replace(null, ...)` here, copying main.qml's phase
            // handler. That copied the line and not the reason: the phase handler replaces because
            // the MACHINE drove the change and there is no meaningful back, whereas this is the
            // user tapping a widget. It also left pageStack.depth at 1, which makes goBack()'s
            // `depth > 1` test fail and the back control silently dead. They push now, like the
            // dedicated Steam/HotWater/Flush widgets always did.
            switch (target) {
            case "settings":        AppShell.settingsRequested(""); break
            case "history":         AppShell.shotHistoryRequested({}); break
            // Context-filtered History (custom-widget-history-actions). Each
            // resolves its filter HERE, from live state, so the widget stores
            // only the action id and follows whatever is active now — a layout
            // exported to another device filters by that device's beans. A
            // helper returns null when there is no context; see the note on
            // them for why that is not the same as {}.
            case "historyRecipe":   AppShell.shotHistoryRequested(_recipeHistoryFilter()); break
            case "historyBean":     AppShell.shotHistoryRequested(_beanHistoryFilter()); break
            case "historyBag":      AppShell.shotHistoryRequested(_bagHistoryFilter()); break
            case "historyProfile":  AppShell.shotHistoryRequested(_profileHistoryFilter()); break
            case "profiles":        AppShell.profileSelectorRequested(); break
            case "profileEditor":   AppShell.profileEditorRequested(); break
            case "recipes":         AppShell.recipeEditorRequested(); break
            case "recipeList":      AppShell.recipesRequested(); break
            case "descaling":       AppShell.descalingRequested(); break
            case "ai":              AppShell.aiSettingsRequested(); break
            case "visualizer":      AppShell.visualizerBrowserRequested(); break
            case "autofavorites":   AppShell.autoFavoritesRequested(); break
            case "steam":           AppShell.steamRequested(); break
            case "hotwater":        AppShell.hotWaterRequested(); break
            case "flush":           AppShell.flushRequested(); break
            case "beaninfo":        AppShell.beanInfoRequested(); break
            case "equipment":       AppShell.equipmentRequested(); break
            case "espresso":        AppShell.espressoRequested(); break
            case "community":       AppShell.communityBrowserRequested(); break
            case "flowCalibration": AppShell.flowCalibrationRequested(); break
            case "profileImport":   AppShell.profileImportRequested(); break
            case "shotReview":
                var shotId = MainController.lastSavedShotId
                if (shotId > 0)
                    AppShell.postShotReviewRequested(shotId, false)
                break
            default:
                _warn("unknown navigate target '" + target + "'")
            }
        } else if (category === "command") {
            // The hardware Group Head Controller (GHC), when present and active, takes
            // exclusive control of starting shots/steam/etc., so on-screen start calls
            // are only valid in headless (no/inactive GHC) or simulation mode.
            var canStart = typeof DE1Device !== "undefined" && DE1Device !== null
                    && DE1Device.guiEnabled
                    && (DE1Device.isHeadless || DE1Device.simulationMode)
            switch (target) {
                case "sleep":
                    if (ScaleDevice.connected)
                        ScaleDevice.disableLcd()
                    if (typeof DE1Device !== "undefined" && DE1Device !== null)
                        DE1Device.goToSleep()
                    AppShell.screensaverRequested()
                    break
                case "startEspresso":
                    if (canStart)
                        DE1Device.startEspresso()
                    break
                case "startSteam":
                    if (canStart)
                        DE1Device.startSteam()
                    break
                case "startHotWater":
                    if (canStart)
                        DE1Device.startHotWater()
                    break
                case "startFlush":
                    if (canStart)
                        DE1Device.startFlush()
                    break
                case "idle":
                    if (typeof DE1Device !== "undefined" && DE1Device !== null)
                        DE1Device.requestIdle()
                    break
                case "tare":
                    if (typeof MachineState !== "undefined" && MachineState !== null)
                        MachineState.tareScale()
                    break
                case "scanDE1":
                    if (typeof BLEManager !== "undefined" && BLEManager !== null)
                        BLEManager.scanForDevices()
                    break
                case "scanScale":
                    if (typeof BLEManager !== "undefined" && BLEManager !== null)
                        BLEManager.scanForDevices()
                    break
                case "brewSettings":
                    AppShell.brewSettingsRequested()
                    break
                case "toggleCharging":
                    if (typeof BatteryManager !== "undefined" && BatteryManager !== null)
                        BatteryManager.chargingMode = (BatteryManager.chargingMode + 1) % 3
                    break
                case "tempToggleSteam":
                    if (typeof Settings !== "undefined" && Settings !== null && typeof MainController !== "undefined" && MainController !== null) {
                        // RESOLVED state, not the transient steamDisabled flag.
                        // steamDisabled is only one of several reasons the heater
                        // can be cold — a "Heater off" pitcher, or simply no
                        // permission — so keying on it made this toggle
                        // one-directional: with Keep warm when idle off and
                        // nothing granting permission, the heater is off while the
                        // flag is clear, so every tap took the else branch and
                        // turned off an already-cold boiler. The same swap was made
                        // in SteamPage, DescalingPage and CustomItem; this
                        // dispatcher was the one the sweep missed — which is why
                        // the direction now lives in MainController, not here.
                        MainController.toggleSteamHeater("custom-widget-toggle")
                    }
                    break
                case "uploadVisualizer":
                    var lastId = MainController.lastSavedShotId
                    if (lastId > 0) {
                        var handler = function(shotId, data) {
                            if (shotId !== lastId) return
                            MainController.shotHistory.shotReady.disconnect(handler)
                            MainController.visualizer.uploadShotFromHistory(data)
                        }
                        MainController.shotHistory.shotReady.connect(handler)
                        MainController.shotHistory.requestShot(lastId)
                    } else {
                        _warn("uploadVisualizer — no saved shot this session, nothing to upload")
                    }
                    break
                case "disconnectDE1":
                    if (typeof DE1Device !== "undefined" && DE1Device !== null)
                        DE1Device.disconnect()
                    break
                case "previousProfile":
                    var prevName = ProfileManager.previousProfileName()
                    if (prevName)
                        ProfileManager.loadProfile(prevName)
                    break
                case "quit":
                    Qt.quit()
                    break
                default:
                    // Handle parameterized commands like loadProfile:<name>
                    if (target.indexOf("loadProfile:") === 0) {
                        var profileName = target.substring("loadProfile:".length)
                        if (profileName)
                            ProfileManager.loadProfile(profileName)
                        else
                            _warn("loadProfile command with empty profile name")
                    } else {
                        _warn("unknown command '" + target + "'")
                    }
                    break
            }
        } else {
            _warn("unknown action category '" + category + "' in '" + actionStr + "'")
        }
    }
}
