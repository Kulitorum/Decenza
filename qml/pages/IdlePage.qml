import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../components"
import "../components/layout"
import "../components/layout/PillFit.js" as PillFit

Page {
    id: idlePage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("idle.pageTitle", "Idle")

    objectName: "idlePage"
    // Exposed so the global Brew Settings dialog (main.qml) can source the live
    // empty-scale virtual zero while this is the current page.
    readonly property real scaleVirtualZero: beanCapture.virtualZero
    background: ThemedPageBackground {}

    // True when the app is allowed to start machine operations on-screen.
    // The hardware Group Head Controller (GHC), when present and active, takes
    // exclusive control of starting shots/steam/etc., so on-screen start calls
    // are only valid in headless (no/inactive GHC) or simulation mode.
    readonly property bool canStartOperations: DE1Device.isHeadless || DE1Device.simulationMode

    StackView.onActivated: {
        // Safety net: if a picker popup was destroyed while open (e.g. a layout
        // rebuild) its onClosed never fired, so clear any leftover slide offset.
        idlePage.releasePanelClearance()
        if (root.pendingBrewDialog) {
            root.pendingBrewDialog = false
            root.openBrewSettings()
        }
    }

    // Secret developer mode: hold top-right corner for 5 seconds to simulate a completed shot
    Item {
        anchors.top: parent.top
        anchors.right: parent.right
        width: Theme.scaled(80)
        height: Theme.scaled(80)
        z: 100

        Timer {
            id: fakeShortHoldTimer
            interval: 5000
            onTriggered: {
                console.log("DEV: Simulating completed shot")
                MainController.generateFakeShotData()
                pageStack.push(Qt.resolvedUrl("EspressoPage.qml"))
                fakeShowMetadataTimer.start()
            }
        }

        Timer {
            id: fakeShowMetadataTimer
            interval: 300
            onTriggered: {
                var shotId = MainController.lastSavedShotId
                console.log("DEV: Opening PostShotReviewPage with shotId:", shotId)
                pageStack.push(Qt.resolvedUrl("PostShotReviewPage.qml"), { editShotId: shotId })
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: fakeShortHoldTimer.start()
            onReleased: fakeShortHoldTimer.stop()
            onCanceled: fakeShortHoldTimer.stop()
        }
    }

    // ============================================================
    // Layout configuration
    // ============================================================

    // Parse layout and extract zone items
    property var layoutConfig: {
        var raw = Settings.network.layoutConfiguration
        try {
            return JSON.parse(raw)
        } catch(e) {
            return { zones: {} }
        }
    }

    property var topLeftItems: layoutConfig.zones ? (layoutConfig.zones.topLeft || []) : []
    property var topRightItems: layoutConfig.zones ? (layoutConfig.zones.topRight || []) : []
    property var centerStatusItems: layoutConfig.zones ? (layoutConfig.zones.centerStatus || []) : []
    property var centerTopItems: layoutConfig.zones ? (layoutConfig.zones.centerTop || []) : []
    property var centerMiddleItems: layoutConfig.zones ? (layoutConfig.zones.centerMiddle || []) : []
    property var bottomLeftItems: layoutConfig.zones ? (layoutConfig.zones.bottomLeft || []) : []
    property var bottomRightItems: layoutConfig.zones ? (layoutConfig.zones.bottomRight || []) : []
    // Lower-mid bar: optional full-width band above the bottom action bar.
    property var lowerMidBarItems: layoutConfig.zones ? (layoutConfig.zones.lowerMidBar || []) : []

    // Center zone Y-offsets (user-configurable positioning)
    property int centerStatusYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerStatus || 0) : 0
    property int centerTopYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerTop || 0) : 0
    property int centerMiddleYOffset: layoutConfig.offsets ? (layoutConfig.offsets.centerMiddle || 0) : 0

    // Center zone scales (user-configurable sizing)
    property real centerStatusScale: layoutConfig.scales ? (layoutConfig.scales.centerStatus || 1.0) : 1.0
    property real centerTopScale: layoutConfig.scales ? (layoutConfig.scales.centerTop || 1.0) : 1.0
    property real centerMiddleScale: layoutConfig.scales ? (layoutConfig.scales.centerMiddle || 1.0) : 1.0

    // Per-zone options map ({} when a zone has none). Every zone rendered here reads its
    // distribution / alignment / style / itemSize through this one accessor, so the idle
    // screen honors the same options LayoutPreview shows. Zones must be wired explicitly:
    // an unpassed option silently falls back to the zone component's default, which is how
    // the top/bottom bars ignored distribution/alignment/style before.
    function zoneOpts(zone) {
        return (layoutConfig.zoneOptions && layoutConfig.zoneOptions[zone]) || ({})
    }

    // Per-zone item size ("compact" | "large"); bars grow to fit large items.
    function zoneItemSize(zone) {
        return zoneOpts(zone).itemSize || "compact"
    }

    // ============================================================
    // Transient panel clearance (idle-page-panel-clearance)
    // A floating quick-picker popup makes room by sliding the OTHER idle content
    // out of its way — the popup itself never moves. Direction follows the popup's
    // position, so a picker works in ANY bar zone: a popup in the lower half lifts
    // the content above it UP; one in the upper half pushes the content below it
    // DOWN. Restores on close. A transient view offset only — it never touches
    // saved zone config.
    // ============================================================
    property real bottomPanelClearance: 0   // content above slides up (lower-half popup)
    property real topPanelClearance: 0      // content below slides down (upper-half popup)

    // Upper bound so a slide never pushes content off-screen under a bar.
    readonly property real _maxPanelClearance:
        Math.max(0, idlePage.height - Theme.statusBarHeight - Theme.bottomBarHeight - Theme.scaled(120))

    // Un-offset extents of the movable idle content (read raw so the test can't
    // feed back into the offset it produces). lowerMidBar (when visible) reaches
    // bottomBar.top; the center column's top is its own y.
    readonly property real _idleContentBottom: {
        var colBottom = centerContent.y + centerContent.height
        var bandBottom = lowerMidBarVisible ? bottomBar.y : 0
        return Math.max(colBottom, bandBottom)
    }
    readonly property real _idleContentTop: centerContent.y

    // Called by a picker as its popup opens, with the popup's top edge (in
    // idlePage coords) and height. Slides content only by the overlap (0 when the
    // content doesn't reach the popup), bounded, in the direction set by which
    // half of the page the popup sits in.
    function requestPanelClearance(panelTop, panelHeight) {
        var panelBottom = panelTop + panelHeight
        if ((panelTop + panelBottom) / 2 >= idlePage.height / 2) {
            var up = idlePage._idleContentBottom - panelTop + Theme.spacingSmall
            idlePage.bottomPanelClearance = Math.max(0, Math.min(up, idlePage._maxPanelClearance))
            idlePage.topPanelClearance = 0
        } else {
            var down = panelBottom - idlePage._idleContentTop + Theme.spacingSmall
            idlePage.topPanelClearance = Math.max(0, Math.min(down, idlePage._maxPanelClearance))
            idlePage.bottomPanelClearance = 0
        }
    }
    function releasePanelClearance() {
        idlePage.bottomPanelClearance = 0
        idlePage.topPanelClearance = 0
    }

    // Center-zone inline carousel: when the expanded center column would reach
    // the bottom-anchored lower-mid band, the band slides DOWN out of the way
    // (content below yields down), bounded to at most fully tucking away.
    // One-way: reads the column's un-offset bottom, drives the band offset.
    readonly property real carouselBandPush: {
        if (idlePage.activePresetFunction === "" || !idlePage.lowerMidBarVisible)
            return 0
        var overlap = (centerContent.y + centerContent.height + Theme.spacingMedium) - lowerMidBar.y
        return Math.max(0, Math.min(overlap, idlePage.lowerMidBarFullHeight + Theme.scaled(20)))
    }

    Component.onCompleted: {
        MainController.bagStorage.requestInventory()
        MainController.equipmentStorage.requestInventory()
        MainController.recipeStorage.requestInventory()
        _publishOperationMode()
    }

    // Idle pill rows pack their full MRU inventory into pages of AT MOST TWO
    // ROWS at each row's available width (descriptive-recipe-names) — the longer
    // bean+type+profile recipe names made a fixed "5 per page" spill past two
    // rows. Each row keeps its complete MRU list and hands PresetPillRow a
    // windowed slice; the per-page count varies with name length and from page
    // to page, and the arrows appear only once there is more than one page.
    // Pill-width measurement MIRRORS PresetPillRow's pill metrics (font 16 bold,
    // padding 40, spacing 12, icon 20+6) — keep in sync (see PillFit.js). The
    // available width is the pill row's Loader width (its parent), used directly
    // because the pill-row id lives inside the Loader's Component scope.
    // FontMetrics.advanceWidth() (not a mutated TextMetrics.text/.width) so
    // measuring inside a reactive page-size binding doesn't self-trigger a
    // binding loop. Font MIRRORS PresetPillRow's pill font (16 bold).
    FontMetrics { id: idlePillMetrics; font.pixelSize: Theme.scaled(16); font.bold: true }
    function _pillPagesFor(widths, availWidth) {
        var sizes = PillFit.packPageSizes(widths, Theme.scaled(12), availWidth, 2)
        if (sizes.length <= 1)
            return sizes
        // Paginating → arrows appear → repack against the width minus the
        // symmetric arrow gutters (matches PresetPillRow.pillsAvailableWidth).
        return PillFit.packPageSizes(widths, Theme.scaled(12),
                                     Math.max(0, availWidth - 2 * Theme.scaled(48)), 2)
    }
    function _pillPageStart(sizes, pageIndex) {
        var idx = Math.max(0, Math.min(pageIndex, sizes.length - 1))
        var start = 0
        for (var p = 0; p < idx; ++p)
            start += sizes[p]
        return start
    }
    function _pillPageSlice(list, sizes, pageIndex) {
        if (!list || list.length === 0)
            return []
        var idx = Math.max(0, Math.min(pageIndex, sizes.length - 1))
        var start = _pillPageStart(sizes, pageIndex)
        return list.slice(start, start + (sizes[idx] || 0))
    }

    // Inventory bags for the beans pill row (bean-bag-inventory: pills are
    // bags, selection is activeBagId, no dirty state — edits write through).
    // The full MRU inventory (inventoryReady is MRU-ordered) is kept and paged;
    // the full inventory also lives on the Beans page.
    property var inventoryBags: []
    property int beanPageIndex: 0
    readonly property var _beanPageSizes: {
        var w = []
        for (var i = 0; i < inventoryBags.length; ++i)
            w.push(idlePillMetrics.advanceWidth(bagLabel(inventoryBags[i])) + Theme.scaled(40))
        return _pillPagesFor(w, beanPresetLoader.width)
    }
    readonly property int beanPageCount: Math.max(1, _beanPageSizes.length)
    readonly property var visibleBags: _pillPageSlice(inventoryBags, _beanPageSizes, beanPageIndex)

    function bagLabel(bag) {
        if (!bag) return ""
        var coffee = bag.coffeeName || ""
        return coffee.length > 0 ? coffee : (bag.roasterName || "")
    }

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            idlePage.inventoryBags = bags
            // Keep the page valid if bags were added/removed/reordered.
            idlePage.beanPageIndex = Math.max(0, Math.min(idlePage.beanPageIndex, idlePage.beanPageCount - 1))
        }
        function onBagsChanged() {
            MainController.bagStorage.requestInventory()
        }
    }

    // Equipment packages for the equipment pill row (add-basket-equipment): pills
    // are packages, selection is activeEquipmentId. The full MRU inventory is
    // kept and paged into two-row pages (descriptive-recipe-names — previously
    // capped to 5 with no paging); the full inventory also lives on the
    // Equipment page.
    property var inventoryEquipment: []
    property int equipmentPageIndex: 0
    readonly property var _equipmentPageSizes: {
        var w = []
        for (var i = 0; i < inventoryEquipment.length; ++i)
            w.push(idlePillMetrics.advanceWidth(equipmentLabel(inventoryEquipment[i])) + Theme.scaled(40))
        return _pillPagesFor(w, equipmentPresetLoader.width)
    }
    readonly property int equipmentPageCount: Math.max(1, _equipmentPageSizes.length)
    readonly property var visibleEquipment: _pillPageSlice(inventoryEquipment, _equipmentPageSizes, equipmentPageIndex)

    function equipmentLabel(pkg) {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""]
                .filter(function(s) { return s.length > 0 }).join(" ")
    }

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            idlePage.inventoryEquipment = packages
            idlePage.equipmentPageIndex = Math.max(0, Math.min(idlePage.equipmentPageIndex, idlePage.equipmentPageCount - 1))
        }
        function onPackagesChanged() {
            MainController.equipmentStorage.requestInventory()
        }
    }

    // Recipes for the recipe pill row (add-recipes): pills are recipes,
    // selection is activeRecipeId, activation runs through MainController's
    // single path. The full MRU list (inventoryReady is MRU-ordered) is kept
    // and paged; the full list also lives on the Recipes page.
    property var inventoryRecipes: []
    property int recipePageIndex: 0
    readonly property var _recipePageSizes: {
        var w = []
        for (var i = 0; i < inventoryRecipes.length; ++i)
            // Recipe pills always carry a drink-type icon → add its width.
            w.push(idlePillMetrics.advanceWidth(inventoryRecipes[i].name || "")
                   + Theme.scaled(20) + Theme.scaled(6) + Theme.scaled(40))
        return _pillPagesFor(w, recipePresetLoader.width)
    }
    readonly property int recipePageCount: Math.max(1, _recipePageSizes.length)
    readonly property var visibleRecipes: _pillPageSlice(inventoryRecipes, _recipePageSizes, recipePageIndex)

    Connections {
        target: MainController.recipeStorage
        function onInventoryReady(recipes) {
            idlePage.inventoryRecipes = recipes
            // Keep the page valid if recipes were added/removed/reordered.
            idlePage.recipePageIndex = Math.max(0, Math.min(idlePage.recipePageIndex, idlePage.recipePageCount - 1))
        }
        function onRecipesChanged() {
            MainController.recipeStorage.requestInventory()
        }
    }

    // Favorite-profile pills (the espresso row) page the same way (descriptive-
    // recipe-names). selectedFavoriteProfile is an ABSOLUTE index into the full
    // favorites, so taps/selection map through _profilePageStart. The selected
    // pill may carry a modified marker that widens it — its width includes that.
    property int profilePageIndex: 0
    readonly property var _profilePageSizes: {
        var _m = ProfileManager.profileModified  // re-measure when the marker toggles
        var favs = Settings.app.favoriteProfiles
        var sel = Settings.app.selectedFavoriteProfile
        var w = []
        for (var i = 0; i < favs.length; ++i) {
            var name = (favs[i] && favs[i].name) || ""
            if (_m && i === sel)
                name = ProfileManager.isCurrentProfileReadOnly
                    ? name + " " + TranslationManager.translate("presets.modified", "(modified)")
                    : "*" + name
            w.push(idlePillMetrics.advanceWidth(name) + Theme.scaled(40))
        }
        return _pillPagesFor(w, espressoColumnLoader.width)
    }
    readonly property int profilePageCount: Math.max(1, _profilePageSizes.length)
    readonly property int _profilePageStart: _pillPageStart(_profilePageSizes, profilePageIndex)
    readonly property var visibleProfiles: _pillPageSlice(Settings.app.favoriteProfiles, _profilePageSizes, profilePageIndex)

    // Flush and hot-water pill rows page the same way (descriptive-recipe-names).
    // Both use an ABSOLUTE selected index (Settings.brew), so taps map through
    // the page start. No icon on these pills.
    property int flushPageIndex: 0
    readonly property var _flushPageSizes: {
        var favs = Settings.brew.flushPresets
        var w = []
        for (var i = 0; i < favs.length; ++i)
            w.push(idlePillMetrics.advanceWidth((favs[i] && favs[i].name) || "") + Theme.scaled(40))
        return _pillPagesFor(w, flushPresetLoader.width)
    }
    readonly property int flushPageCount: Math.max(1, _flushPageSizes.length)
    readonly property int _flushPageStart: _pillPageStart(_flushPageSizes, flushPageIndex)
    readonly property var visibleFlush: _pillPageSlice(Settings.brew.flushPresets, _flushPageSizes, flushPageIndex)

    property int hotWaterPageIndex: 0
    readonly property var _hotWaterPageSizes: {
        var favs = Settings.brew.waterVesselPresets
        var w = []
        for (var i = 0; i < favs.length; ++i)
            w.push(idlePillMetrics.advanceWidth((favs[i] && favs[i].name) || "") + Theme.scaled(40))
        return _pillPagesFor(w, hotWaterPresetLoader.width)
    }
    readonly property int hotWaterPageCount: Math.max(1, _hotWaterPageSizes.length)
    readonly property int _hotWaterPageStart: _pillPageStart(_hotWaterPageSizes, hotWaterPageIndex)
    readonly property var visibleWaterVessels: _pillPageSlice(Settings.brew.waterVesselPresets, _hotWaterPageSizes, hotWaterPageIndex)

    // Recipe pill selection is the synchronous MainController.selectedRecipeId
    // (shared with the compact RecipesItem so both layouts behave identically —
    // the recipe analogue of the profile pills' Settings.app.selectedFavoriteProfile).
    // See tryStartRecipe() below for the two-tap select-then-start handler.
    function tryStartRecipe(recipe) {
        var alreadySelected = (recipe.id === MainController.selectedRecipeId)
        if (!alreadySelected) {
            MainController.activateRecipe(recipe.id)  // sets selectedRecipeId synchronously
            return
        }
        // Second tap on the selected recipe → start. Log which gate blocks it
        // (the two gates fail for very different reasons) so a debug log tells
        // us exactly why a shot did not start.
        if (!idlePage.canStartOperations) {
            console.log("[recipe pill] start blocked: app cannot start operations (active GHC?) — recipe=" + recipe.id
                        + " isHeadless=" + DE1Device.isHeadless + " simulationMode=" + DE1Device.simulationMode)
        } else if (!MachineState.isReady) {
            console.log("[recipe pill] start blocked: machine not ready — recipe=" + recipe.id
                        + " phase=" + MachineState.phase)
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
        } else {
            // Deferred in MainController until the recipe's profile is applied,
            // so a fast second tap can't pull a shot on the previous profile.
            console.log("[recipe pill] requesting start — recipe=" + recipe.id + " phase=" + MachineState.phase)
            MainController.startSelectedRecipeShotWhenApplied()
        }
    }

    // Track which function's presets are showing (used by center-zone action items)
    property string activePresetFunction: ""  // "", "steam", "espresso", "hotwater", "flush", "beans", "equipment", "recipes"

    // Idle bean auto-capture: tracks a virtual zero off the empty scale, then when
    // the dose cup (with beans) rests stable it sets the dose (dyeBeanWeight),
    // optionally dings (if doseCaptureSoundEnabled), and confirms on the readout.
    // The scale owns the DOSE and never the yield anchor (add-yield-ratio-anchor):
    // under a ratio anchor the target re-derives in C++ from the anchor's own
    // ratio; an absolute anchor stays put (it used to be stomped with
    // dose x lastUsedRatio here on every capture). Net dose =
    // (load - virtualZero) - cupWeight, so it is robust to
    // an un-zeroed/drifting scale. The baseline tracks even with no cup saved (so the
    // "Weigh" button can reuse it); all user-visible behaviour and the actual capture
    // stay gated on a saved cup (doseCupTareWeight > 0). Stays armed whether or not the
    // brew dialog is open — one persistent latch means an already-weighed cup is not
    // re-captured on open/close. Only on home/espresso mode (NOT steam/hot-water/flush,
    // where the scale is for milk/water).
    property bool beanCaptureShown: false
    property string beanCaptureText: ""
    Timer { id: idleBeanCaptureTimer; interval: 3500; onTriggered: idlePage.beanCaptureShown = false }
    StableWeightCapture {
        id: beanCapture
        rawWeight: (ScaleDevice && ScaleDevice.connected) ? MachineState.scaleWeight : 0
        cupWeight: Settings.brew.doseCupTareWeight
        active: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
                && idlePage.activePresetFunction !== "steam"
                && idlePage.activePresetFunction !== "hotwater"
                && idlePage.activePresetFunction !== "flush"
        minNet: 5
        maxNet: 45
        tolerance: 0.5
        stableMs: 2500
        onStableCaptured: function(net) {
            // net is always >= minNet (5 g) here — no extra floor needed.
            // Write the canonical dose ONLY. A capture never computes a yield
            // (the old `brewYieldOverride = net * lastUsedRatio` was
            // anchor-blind and used the global preset); the active anchor's
            // own ratio re-derives the target in ProfileManager. The shared
            // Brew Settings dialog reflects the dose via its dyeBeanWeight
            // watcher while it is open.
            Settings.dye.dyeBeanWeight = net
            idlePage.beanCaptureText = TranslationManager.translate("idle.doseCaptured", "Dose set: %1g").arg(net.toFixed(1))
            idlePage.beanCaptureShown = true
            idleBeanCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                if (Settings.brew.doseCaptureSoundEnabled)
                    AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(idlePage.beanCaptureText)
            }
        }
    }

    // Publish the live dose-weighing state on the window root for the Beans layout
    // widget (DoseWeightItem) — the widget can live outside IdlePage (persistent
    // status bar), so it can't reach beanCapture directly. Live only while this
    // page is showing and an uncaptured dose sits on the scale with a saved cup
    // tare — stricter than the panel readout below, which keeps ticking for an
    // already-captured load; -1 = not weighing. The engine's re-arm (net change
    // > rearmDelta after capture) drops isCaptured, so adding more beans after
    // capture goes live again automatically.
    Binding {
        target: idlePage.Window.window
        property: "doseLiveNetG"
        value: (idlePage.visible && beanCapture.loadPresent && !beanCapture.isCaptured
                && Settings.brew.doseCupTareWeight > 0)
               ? Math.max(0, beanCapture.netWeight) : -1
    }
    Binding {
        target: idlePage.Window.window
        property: "doseCaptureFlash"
        value: idlePage.visible && idlePage.beanCaptureShown
    }

    // When the scale is zeroed/tared, the old virtual zero is stale (it would
    // double-count the offset that was just removed) — re-establish the baseline.
    Connections {
        target: MachineState
        function onTareCompleted() { beanCapture.reset() }
    }

    // Idle milk auto-capture: while the steam presets are showing on the home
    // screen and the selected pitcher has a saved empty-pitcher weight (cupWeight),
    // rest the milk pitcher on the scale to record the milk weight. If the pitcher
    // is ALSO calibrated (has a reference milk weight), the steam time is locked
    // proportionally with a ding + confirmation. This is the steam equivalent of
    // the bean auto-capture above; the dedicated Steam page has its own copy.
    property bool milkCaptureShown: false
    property string milkCaptureText: ""
    // Last milk weight measured this session (for the bottom status row). 0 = none yet.
    property real measuredMilkG: 0
    // The captured milk is net of the SELECTED pitcher's saved weight, so it's wrong
    // for any other pitcher — drop it on selection change, mirroring main.qml's reset
    // of sessionMeasuredMilkG. Without this the pill tap's fallback could scale the
    // new pitcher's steam by the previous pitcher's milk.
    Connections {
        target: Settings.brew
        function onSelectedSteamPitcherChanged() { idlePage.measuredMilkG = 0 }
    }
    Timer { id: idleMilkCaptureTimer; interval: 3500; onTriggered: idlePage.milkCaptureShown = false }
    StableWeightCapture {
        id: idleMilkCapture
        // Virtual-zero model (same as the bean capture): raw scale reading minus the
        // empty-pitcher weight (cupWeight) gives net milk, robust to an un-zeroed
        // scale. Auto-capture requires a saved pitcher weight (cupWeight > 0),
        // symmetric with beans requiring a saved dose-cup tare.
        rawWeight: (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) ? MachineState.scaleWeight : 0
        cupWeight: {
            var p = Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher)
            return (p && !p.disabled) ? (p.pitcherWeightG ?? 0) : 0
        }
        // Opt-in (Settings.brew.milkAutoCaptureEnabled, default OFF — calibrating a
        // pitcher turns it on) and only while
        // the steam flow is showing AND this page is the active StackView page — so a
        // stray weight never silently changes the steam stop time, and the capture
        // can't double-fire alongside SteamPage's own copy when SteamPage is pushed
        // on top (long-press) while activePresetFunction is still "steam".
        active: Settings.brew.milkAutoCaptureEnabled
                && idlePage.activePresetFunction === "steam"
                && idlePage.StackView.status === StackView.Active
                && ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
        minNet: 50   // nobody steams < 50 g milk; floor also keeps a bean cup from tripping milk capture
        maxNet: 1500
        tolerance: 1.5
        stableMs: 2500
        onStableCaptured: function(milk) {
            idlePage.measuredMilkG = milk  // record measured (net) milk for the status row
            // Latch the measured milk for the baseline pair (committed atomically with
            // the duration at session end, in main.qml). Recorded even for an
            // uncalibrated preset so the very first calibration-bootstrap steam can be
            // adopted — and never as a half-pair, since the time half is written there.
            if (Window.window) Window.window.sessionMeasuredMilkG = milk
            // Single source of truth (SettingsBrew): 0 when off/uncalibrated → nothing to lock.
            var t = Settings.brew.scaledSteamTime(Settings.brew.selectedSteamPitcher, milk)
            if (t <= 0) return
            Settings.brew.steamTimeout = t
            // Push to the DE1 now (same as the steam-preset selection) so a GHC/auto
            // steam actually uses the scaled time, not the machine's last-sent value.
            MainController.applySteamSettings()
            idlePage.milkCaptureText = TranslationManager.translate("idle.steamCaptured", "Steam time: %1s for %2g milk").arg(t).arg(milk.toFixed(0))
            idlePage.milkCaptureShown = true
            idleMilkCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined") {
                if (Settings.brew.doseCaptureSoundEnabled)
                    AccessibilityManager.playCaptureDing()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(idlePage.milkCaptureText)
            }
        }
    }
    // Re-zero the milk capture when the scale is tared (same reason as the bean one).
    Connections {
        target: MachineState
        function onTareCompleted() { idleMilkCapture.reset() }
    }

    // Clear the last measured milk when a steam session ends, so the next steam isn't
    // scaled from a stale measurement (e.g. the home-screen preset path's fallback).
    // Phase leaves Steaming only at the true session end (Puffing/Ending stay in the
    // Steaming phase by machinestate.cpp's load-bearing invariant).
    Connections {
        target: MachineState
        property bool wasSteaming: false
        function onPhaseChanged() {
            if (MachineState.phase === MachineStateType.Phase.Steaming)
                wasSteaming = true
            else if (wasSteaming) {
                idlePage.measuredMilkG = 0
                wasSteaming = false
            }
        }
    }

    // Transient confirmation banner for the milk-weight capture (auto-dismiss).
    Rectangle {
        visible: idlePage.milkCaptureShown
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(12)
        z: 2000
        width: milkBannerLabel.implicitWidth + Theme.scaled(32)
        height: milkBannerLabel.implicitHeight + Theme.scaled(20)
        radius: Theme.cardRadius
        color: Theme.primaryColor
        Text {
            id: milkBannerLabel
            anchors.centerIn: parent
            text: idlePage.milkCaptureText
            color: Theme.primaryContrastColor
            font: Theme.bodyFont
        }
    }

    // Small flashing reminder shown while a dose cup of beans is settling on the
    // scale (a load is present but stable-capture hasn't fired yet). Disappears the
    // instant capture completes; a ding plays then only if doseCaptureSoundEnabled.
    Text {
        id: waitForBellHint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(70)
        z: 1500
        horizontalAlignment: Text.AlignHCenter
        // Only while a load sits in the capture window with a cup saved. Outside
        // [minNet, maxNet] no capture will ever fire, so don't tell the user to wait
        // for a bell that can't ring (a too-heavy cup or the wrong vessel).
        readonly property bool beansSettling: beanCapture.active && !beanCapture.isCaptured
                                              && Settings.brew.doseCupTareWeight > 0
                                              && beanCapture.loadPresent
                                              && beanCapture.netWeight >= beanCapture.minNet
                                              && beanCapture.netWeight <= beanCapture.maxNet
        readonly property bool milkSettling: idleMilkCapture.active && !idleMilkCapture.isCaptured
                                            && idleMilkCapture.cupWeight > 0
                                            && idleMilkCapture.loadPresent
                                            && idleMilkCapture.netWeight >= idleMilkCapture.minNet
                                            && idleMilkCapture.netWeight <= idleMilkCapture.maxNet
        visible: beansSettling || milkSettling
        text: TranslationManager.translate("scale.waitForBell", "Wait for the bell before you take it off the scale")
        color: Theme.warningColor
        font: Theme.labelFont
        SequentialAnimation on opacity {
            running: waitForBellHint.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 450 }
            NumberAnimation { to: 1.0; duration: 450 }
        }
    }

    // Scale-load detector for the "place the pitcher" prompt below. Gated on
    // milkAutoCaptureEnabled (same as idleMilkCapture): with weight-timed steaming OFF, placing the
    // pitcher captures nothing and no beep follows, so the prompt must not tell the user to do it.
    // It never captures; we only read loadPresent.
    StableWeightCapture {
        id: idlePitcherDetect
        rawWeight: (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale) ? MachineState.scaleWeight : 0
        active: Settings.brew.milkAutoCaptureEnabled
                && idlePage.activePresetFunction === "steam"
                && idlePage.StackView.status === StackView.Active
                && ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
        minNet: 999999   // never auto-captures; only provides loadPresent
        loadThreshold: 50  // an empty pitcher already weighs well above this
    }

    // Publish the selected operation (espresso/steam/…) to the Theme singleton so the
    // persistent status-bar widgets (e.g. the page-aware Plan widget) can tell what the
    // user has selected on the idle screen — they load as separate components and can't
    // read this property by scope. Cleared when this page isn't the active one.
    function _publishOperationMode() {
        Theme.currentOperationMode =
            (StackView.status === StackView.Active) ? activePresetFunction : ""
    }
    StackView.onStatusChanged: _publishOperationMode()

    // Auto-tare scale and announce presets when activePresetFunction changes
    onActivePresetFunctionChanged: {
        _publishOperationMode()
        // Paged pill rows always (re)open on the first page — the most-recent items.
        if (activePresetFunction === "recipes") recipePageIndex = 0
        else if (activePresetFunction === "beans") beanPageIndex = 0
        else if (activePresetFunction === "espresso") profilePageIndex = 0
        else if (activePresetFunction === "equipment") equipmentPageIndex = 0
        else if (activePresetFunction === "flush") flushPageIndex = 0
        else if (activePresetFunction === "hotwater") hotWaterPageIndex = 0
        // Auto-tare when steam pills appear so the scale starts at 0
        // before the user places the pitcher
        if (activePresetFunction === "steam" && typeof MachineState !== "undefined") {
            MachineState.tareScale()
            // Fresh steam attempt: drop any milk captured but not consumed by a prior
            // (abandoned) attempt, so it can't scale this one.
            if (Window.window) Window.window.sessionMeasuredMilkG = 0
        }

        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled && activePresetFunction !== "") {
            var presets = []
            var selectedName = ""
            switch (activePresetFunction) {
                case "espresso":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleProfiles
                    var selAbs = Settings.app.selectedFavoriteProfile
                    var selRel = selAbs - idlePage._profilePageStart
                    if (selRel >= 0 && selRel < presets.length) {
                        selectedName = presets[selRel].name
                    }
                    break
                case "steam":
                    presets = Settings.brew.steamPitcherPresets
                    if (Settings.brew.selectedSteamPitcher >= 0 && Settings.brew.selectedSteamPitcher < presets.length) {
                        selectedName = presets[Settings.brew.selectedSteamPitcher].name
                    }
                    break
                case "hotwater":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleWaterVessels
                    var selWv = Settings.brew.selectedWaterVessel - idlePage._hotWaterPageStart
                    if (selWv >= 0 && selWv < presets.length) {
                        selectedName = presets[selWv].name
                    }
                    break
                case "flush":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleFlush
                    var selFl = Settings.brew.selectedFlushPreset - idlePage._flushPageStart
                    if (selFl >= 0 && selFl < presets.length) {
                        selectedName = presets[selFl].name
                    }
                    break
                case "beans":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleBags.map(function(b) { return { name: idlePage.bagLabel(b) } })
                    for (var bi = 0; bi < idlePage.visibleBags.length; ++bi) {
                        if (idlePage.visibleBags[bi].id === Settings.dye.activeBagId) {
                            selectedName = idlePage.bagLabel(idlePage.visibleBags[bi])
                            break
                        }
                    }
                    break
                case "equipment":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleEquipment.map(function(p) { return { name: idlePage.equipmentLabel(p) } })
                    for (var ei = 0; ei < idlePage.visibleEquipment.length; ++ei) {
                        if (idlePage.visibleEquipment[ei].id === Settings.dye.activeEquipmentId) {
                            selectedName = idlePage.equipmentLabel(idlePage.visibleEquipment[ei])
                            break
                        }
                    }
                    break
                case "recipes":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleRecipes.map(function(r) { return { name: r.name } })
                    for (var ri = 0; ri < idlePage.visibleRecipes.length; ++ri) {
                        // Match the pill highlight (selectedIndex) — the synchronous
                        // MainController.selectedRecipeId, not the lagging activeRecipeId.
                        if (idlePage.visibleRecipes[ri].id === MainController.selectedRecipeId) {
                            selectedName = idlePage.visibleRecipes[ri].name
                            break
                        }
                    }
                    break
            }

            if (presets.length > 0) {
                var names = []
                for (var i = 0; i < presets.length; i++) {
                    names.push(presets[i].name)
                }
                var announcement = presets.length + " " + TranslationManager.translate("idle.accessible.presets", "presets") + ": " + names.join(", ")
                if (selectedName !== "") {
                    announcement += ". " + selectedName + " " + TranslationManager.translate("idle.accessible.isSelected", "is selected")
                }
                AccessibilityManager.announce(announcement)
            }
        }
    }

    // Click away to hide presets (disabled in accessibility mode to prevent mis-clicks)
    MouseArea {
        anchors.fill: parent
        z: -1
        enabled: activePresetFunction !== "" &&
                 !(typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
        onClicked: activePresetFunction = ""
    }

    // ============================================================
    // Top info section (from layout topLeft/topRight zones)
    // ============================================================
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.scaled(20)

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.scaled(50)

            LayoutBarZone {
                zoneName: "topLeft"
                items: idlePage.topLeftItems
                distribution: idlePage.zoneOpts("topLeft").distribution || "packed"
                alignment: idlePage.zoneOpts("topLeft").alignment || "center"
                zoneStyle: idlePage.zoneOpts("topLeft").style || "standard"
                itemSize: idlePage.zoneItemSize("topLeft")
            }

            Item { Layout.fillWidth: true }

            LayoutBarZone {
                zoneName: "topRight"
                items: idlePage.topRightItems
                distribution: idlePage.zoneOpts("topRight").distribution || "packed"
                alignment: idlePage.zoneOpts("topRight").alignment || "center"
                zoneStyle: idlePage.zoneOpts("topRight").style || "standard"
                itemSize: idlePage.zoneItemSize("topRight")
            }
        }
    }

    // ============================================================
    // Center content (from layout centerTop/centerMiddle zones)
    // ============================================================
    ColumnLayout {
        id: centerContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: Theme.scaled(50)
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        spacing: Theme.scaled(20)
        // Transient slide to clear a picker popup: up for a lower-half popup,
        // down for an upper-half one (restores to 0 on close).
        transform: Translate {
            y: -idlePage.bottomPanelClearance + idlePage.topPanelClearance
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        }

        // Status readouts (temp, water level, connection)
        LayoutCenterZone {
            Layout.fillWidth: true
            Layout.topMargin: idlePage.centerStatusYOffset
            zoneName: "centerStatus"
            items: idlePage.centerStatusItems
            visible: idlePage.centerStatusItems.length > 0
            zoneScale: idlePage.centerStatusScale
            alignment: idlePage.zoneOpts("centerStatus").alignment || "center"
            zoneStyle: idlePage.zoneOpts("centerStatus").style || "standard"
        }

        // Main action buttons from centerTop zone
        LayoutCenterZone {
            id: centerTopZone
            Layout.fillWidth: true
            Layout.topMargin: idlePage.centerTopYOffset
            zoneName: "centerTop"
            items: idlePage.centerTopItems
            zoneScale: idlePage.centerTopScale
            alignment: idlePage.zoneOpts("centerTop").alignment || "center"
            zoneStyle: idlePage.zoneOpts("centerTop").style || "standard"
        }

        // Inline preset rows (for center-zone action buttons)
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: activePresetFunction !== "" ? activePresetRow.implicitHeight : 0
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.scaled(900)
            Layout.leftMargin: Theme.standardMargin
            Layout.rightMargin: Theme.standardMargin
            clip: true

            property var activePresetRow: {
                switch (activePresetFunction) {
                    case "steam": return steamPresetLoader
                    case "espresso": return espressoColumnLoader
                    case "hotwater": return hotWaterPresetLoader
                    case "flush": return flushPresetLoader
                    case "beans": return beanPresetLoader
                    case "equipment": return equipmentPresetLoader
                    case "recipes": return recipePresetLoader
                    default: return steamPresetLoader
                }
            }

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }

            Loader {
                id: steamPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "steam"
                visible: active

                // Track scale weight changes and bump version to refresh the live
                // net-milk pill suffix (see pillSuffixFn below)
                property int steamPillSuffixVersion: 0
                Connections {
                    target: MachineState
                    function onScaleWeightChanged() {
                        if (steamPresetLoader.active) steamPresetLoader.steamPillSuffixVersion++
                    }
                }

                sourceComponent: Column {
                    width: parent ? parent.width : 0
                    spacing: Theme.scaled(8)

                    PresetPillRow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        maxWidth: steamPresetLoader.width
                        presets: Settings.brew.steamPitcherPresets
                        selectedIndex: Settings.brew.selectedSteamPitcher
                        supportLongPress: true
                        pillSuffixMaxWidth: Theme.scaled(60)  // Reserve ~"(1234g)" worth of width
                        pillSuffixVersion: steamPresetLoader.steamPillSuffixVersion

                        // Live milk weigh: scale reading minus the saved empty-pitcher
                        // weight, updating as milk is poured. Assumes an un-tared gross
                        // reading — a pitcher zeroed by the steam auto-tare reads (0g)
                        // until lifted and replaced (see steamPlacePrompt). Display only —
                        // the capture path (idleMilkCapture) and steam-time scaling never
                        // read this. Deliberately NOT netMilkForPitcher(): its 50–1500 g
                        // window is sized for time scaling and would zero small amounts
                        // here. Twin of the SteamItem popup's pillSuffixFn — keep in sync.
                        pillSuffixFn: function(index) {
                            if (!ScaleDevice.connected || ScaleDevice.isFlowScale) return ""
                            var preset = Settings.brew.steamPitcherPresets[index]
                            if (!preset || preset.disabled) return ""
                            var pitcherWeight = preset.pitcherWeightG ?? 0
                            if (pitcherWeight <= 0) return ""
                            var milkWeight = Math.max(0, MachineState.scaleWeight - pitcherWeight)
                            return " (" + Math.round(milkWeight) + "g)"
                        }

                        // Show pitcher presets as "<name> Pitcher" (e.g. "Small Pitcher"),
                        // skipping the disabled "Off" preset and any name that already
                        // mentions a pitcher.
                        pillLabelFn: function(index, name) {
                            var preset = Settings.brew.steamPitcherPresets[index]
                            if (preset && preset.disabled) return name
                            if (!name || name.toLowerCase().indexOf("pitcher") >= 0) return name
                            return name + " " + TranslationManager.translate("idle.label.pitcherSuffix", "Pitcher")
                        }

                        onPresetSelected: function(index) {
                            var wasAlreadySelected = (index === Settings.brew.selectedSteamPitcher)
                            Settings.brew.selectedSteamPitcher = index
                            var preset = Settings.brew.getSteamPitcherPreset(index)
                            if (preset && preset.disabled) {
                                // "Off" preset — disable the steam heater; don't touch
                                // steamTimeout/steamFlow (preset.duration/flow are undefined
                                // for disabled presets and writing undefined to these int
                                // properties errors), and don't start steam on re-tap.
                                MainController.turnOffSteamHeater()
                                return
                            }
                            if (preset) {
                                // Weight-scaled steaming (DSx2-style), via the single source of
                                // truth in SettingsBrew. Net milk on the scale now, or the last
                                // measured weight if the pitcher was lifted to the wand.
                                var idx = Settings.brew.selectedSteamPitcher
                                var milk = (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale)
                                           ? Settings.brew.netMilkForPitcher(idx, MachineState.scaleWeight) : 0
                                if (milk <= 0)
                                    milk = idlePage.measuredMilkG
                                Settings.brew.steamTimeout = Settings.brew.effectiveSteamDurationSec(idx, milk)
                                Settings.brew.steamFlow = preset.flow !== undefined ? preset.flow : 150
                                Settings.brew.steamTemperature = (preset.temperature !== undefined) ? preset.temperature : Settings.brew.steamTemperature
                            }
                            MainController.applySteamSettings()

                            if (wasAlreadySelected) {
                                if (MachineState.isReady && idlePage.canStartOperations) {
                                    DE1Device.startSteam()
                                } else {
                                    console.log("Cannot start steam - machine not ready, phase:", MachineState.phase)
                                    if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                        AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                }
                            }
                        }

                        // Long-press a pitcher to open the steam page settings, where you set
                        // the duration and either tap "Weigh" next to Reference milk (with milk
                        // on the scale) or "Use as baseline" to teach this pitcher its
                        // milk-weight -> steam-time reference.
                        onPresetLongPressed: function(index) {
                            Settings.brew.selectedSteamPitcher = index
                            pageStack.push(Qt.resolvedUrl("SteamPage.qml"))
                        }
                    }

                    // "Place the milk pitcher on the scale" — same position as the bean prompt (below
                    // the pills). Shown only while idlePitcherDetect is active (weight-timed steaming on,
                    // steam selected, scale connected) and nothing is on the scale yet. Gently blinks.
                    // "or lift and replace": selecting steam auto-tares the scale, so a pitcher that was
                    // ALREADY sitting there reads as 0 and won't register until it's lifted and set back
                    // — without the hedge the prompt would assert something false.
                    // The hint promises a beep ONLY when the capture sound will actually play — the
                    // ding is separately gated on doseCaptureSoundEnabled (default off).
                    Text {
                        id: steamPlacePrompt
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        visible: idlePitcherDetect.active && !idlePitcherDetect.loadPresent
                        text: TranslationManager.translate("idle.label.placeOrReplacePitcher", "Place (or lift and replace) the milk pitcher on the scale") + "\n"
                            + (Settings.brew.doseCaptureSoundEnabled
                                ? TranslationManager.translate("idle.label.placePitcherHint", "(and wait for the beep before removing)")
                                : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                        SequentialAnimation on opacity {
                            running: steamPlacePrompt.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.45; duration: 800 }
                            NumberAnimation { to: 1.0; duration: 800 }
                        }
                    }
                }
            }

            Loader {
                id: espressoColumnLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "espresso"
                visible: active
                sourceComponent: Column {
                    width: parent ? parent.width : 0
                    spacing: Theme.scaled(8)

                    PresetPillRow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        maxWidth: espressoColumnLoader.width

                        // Windowed to the current two-row page; selection and taps
                        // map back to absolute favorite indices via _profilePageStart.
                        presets: idlePage.visibleProfiles
                        selectedIndex: {
                            var sel = Settings.app.selectedFavoriteProfile
                            if (sel < 0) return -1
                            var rel = sel - idlePage._profilePageStart
                            return (rel >= 0 && rel < idlePage.visibleProfiles.length) ? rel : -1
                        }
                        supportLongPress: true
                        modified: ProfileManager.profileModified
                        modifiedIsReadOnly: ProfileManager.isCurrentProfileReadOnly

                        pageCount: idlePage.profilePageCount
                        pageIndex: idlePage.profilePageIndex
                        prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousProfiles", "Previous profiles")
                        nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextProfiles", "Next profiles")
                        onPageChangeRequested: function(delta) {
                            idlePage.profilePageIndex = Math.max(0, Math.min(idlePage.profilePageIndex + delta, idlePage.profilePageCount - 1))
                        }

                        onPresetSelected: function(index) {
                            var absIndex = idlePage._profilePageStart + index
                            var wasAlreadySelected = (absIndex === Settings.app.selectedFavoriteProfile)
                            Settings.app.selectedFavoriteProfile = absIndex
                            var preset = Settings.app.getFavoriteProfile(absIndex)

                            if (wasAlreadySelected) {
                                if (MachineState.isReady && idlePage.canStartOperations) {
                                    DE1Device.startEspresso()
                                } else {
                                    console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                                    if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                        AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                }
                            } else {
                                if (preset && preset.filename) {
                                    ProfileManager.loadProfile(preset.filename)
                                }
                            }
                        }

                        onPresetLongPressed: function(index) {
                            var absIndex = idlePage._profilePageStart + index
                            var preset = Settings.app.getFavoriteProfile(absIndex)
                            if (preset && preset.filename) {
                                if (absIndex !== Settings.app.selectedFavoriteProfile) {
                                    Settings.app.selectedFavoriteProfile = absIndex
                                    ProfileManager.loadProfile(preset.filename)
                                }
                                profilePreviewPopup.profileFilename = preset.filename
                                profilePreviewPopup.profileName = preset.name || ""
                                profilePreviewPopup.open()
                            }
                        }
                    }

                    // Green pill showing non-favorite profile name
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: Settings.app.selectedFavoriteProfile === -1 && idlePage.canStartOperations
                        spacing: Theme.scaled(8)

                        Rectangle {
                            id: nonFavoriteProfilePill
                            width: nonFavoriteProfileText.implicitWidth + Theme.scaled(40)
                            height: Theme.scaled(50)
                            radius: Theme.scaled(10)
                            color: Theme.successColor

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: (ProfileManager.currentProfileName || "") + " " + TranslationManager.translate("idle.accessible.startespresso", "Start espresso")
                            Accessible.focusable: true
                            Accessible.onPressAction: idleNonFavMouseArea.clicked(null)
                            Keys.onReturnPressed: { idleNonFavMouseArea.clicked(null); event.accepted = true }
                            Keys.onSpacePressed:  { idleNonFavMouseArea.clicked(null); event.accepted = true }

                            Text {
                                id: nonFavoriteProfileText
                                anchors.centerIn: parent
                                text: ProfileManager.currentProfileName || ""
                                color: Theme.primaryContrastColor
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: idleNonFavMouseArea
                                anchors.fill: parent
                                onClicked: {
                                    if (MachineState.isReady && idlePage.canStartOperations) {
                                        DE1Device.startEspresso()
                                    } else {
                                        console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                            AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                                    }
                                }
                            }
                        }

                        ProfileInfoButton {
                            anchors.verticalCenter: parent.verticalCenter
                            profileFilename: Settings.app.currentProfile
                            profileName: ProfileManager.currentProfileName

                            onClicked: {
                                pageStack.push(Qt.resolvedUrl("ProfileInfoPage.qml"), {
                                    profileFilename: Settings.app.currentProfile,
                                    profileName: ProfileManager.currentProfileName
                                })
                            }
                        }
                    }

                    // Bean weight: live net-weight readout for the auto-capture above
                    // (net = load minus the virtual zero and the saved cup). Only shown
                    // once a dose-cup tare is saved (doseCupTareWeight > 0); with no cup
                    // saved the auto-capture is off, so the readout/prompt stays hidden.
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
                                 && Settings.brew.doseCupTareWeight > 0
                        spacing: Theme.scaled(8)

                        // Small, unobtrusive live net-weight readout. Beans now
                        // auto-capture when stable, so this is a readout (not a
                        // button) — it shows the live net beans and briefly flashes
                        // the captured dose in the accent color.
                        Text {
                            id: weighBeansText
                            horizontalAlignment: Text.AlignHCenter
                            // True while prompting the user to place beans (no load on
                            // the scale yet) — this state gently blinks.
                            readonly property bool showingPlacePrompt: !idlePage.beanCaptureShown
                                && !beanCapture.loadPresent
                            text: {
                                if (idlePage.beanCaptureShown)
                                    return idlePage.beanCaptureText
                                if (beanCapture.loadPresent)
                                    return beanCapture.netWeight.toFixed(1) + " g " + TranslationManager.translate("idle.label.onScale", "on scale")
                                // The beep hint only when the capture sound will actually play
                                // (doseCaptureSoundEnabled, default off) — same rule as the pitcher prompt.
                                return TranslationManager.translate("idle.label.placeBeansOnScale", "Place Beans on Scale") + "\n"
                                     + (Settings.brew.doseCaptureSoundEnabled
                                         ? TranslationManager.translate("idle.label.placeBeansHint", "(and wait for the beep before removing)")
                                         : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
                            }
                            color: idlePage.beanCaptureShown ? Theme.primaryColor : Theme.textSecondaryColor
                            font: Theme.labelFont
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                            onShowingPlacePromptChanged: if (!showingPlacePrompt) opacity = 1.0
                            SequentialAnimation on opacity {
                                running: weighBeansText.showingPlacePrompt
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.45; duration: 800 }
                                NumberAnimation { to: 1.0; duration: 800 }
                            }
                        }
                    }
                }
            }

            Loader {
                id: hotWaterPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "hotwater"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: hotWaterPresetLoader.width
                    // Windowed to the current two-row page; selection/taps map to
                    // the absolute selectedWaterVessel via _hotWaterPageStart.
                    presets: idlePage.visibleWaterVessels
                    selectedIndex: {
                        var rel = Settings.brew.selectedWaterVessel - idlePage._hotWaterPageStart
                        return (rel >= 0 && rel < idlePage.visibleWaterVessels.length) ? rel : -1
                    }

                    pageCount: idlePage.hotWaterPageCount
                    pageIndex: idlePage.hotWaterPageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousHotWater", "Previous vessels")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextHotWater", "Next vessels")
                    onPageChangeRequested: function(delta) {
                        idlePage.hotWaterPageIndex = Math.max(0, Math.min(idlePage.hotWaterPageIndex + delta, idlePage.hotWaterPageCount - 1))
                    }

                    onPresetSelected: function(index) {
                        var absIndex = idlePage._hotWaterPageStart + index
                        var wasAlreadySelected = (absIndex === Settings.brew.selectedWaterVessel)
                        Settings.brew.selectedWaterVessel = absIndex
                        var preset = Settings.brew.getWaterVesselPreset(absIndex)
                        if (preset) {
                            Settings.brew.waterVolume = preset.volume
                        }
                        MainController.applyHotWaterSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady && idlePage.canStartOperations) {
                                DE1Device.startHotWater()
                            } else {
                                console.log("Cannot start hot water - machine not ready, phase:", MachineState.phase)
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                    AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                            }
                        }
                    }
                }
            }

            Loader {
                id: flushPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "flush"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: flushPresetLoader.width
                    // Windowed to the current two-row page; selection/taps map to
                    // the absolute selectedFlushPreset via _flushPageStart.
                    presets: idlePage.visibleFlush
                    selectedIndex: {
                        var rel = Settings.brew.selectedFlushPreset - idlePage._flushPageStart
                        return (rel >= 0 && rel < idlePage.visibleFlush.length) ? rel : -1
                    }

                    pageCount: idlePage.flushPageCount
                    pageIndex: idlePage.flushPageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousFlush", "Previous flushes")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextFlush", "Next flushes")
                    onPageChangeRequested: function(delta) {
                        idlePage.flushPageIndex = Math.max(0, Math.min(idlePage.flushPageIndex + delta, idlePage.flushPageCount - 1))
                    }

                    onPresetSelected: function(index) {
                        var absIndex = idlePage._flushPageStart + index
                        var wasAlreadySelected = (absIndex === Settings.brew.selectedFlushPreset)
                        Settings.brew.selectedFlushPreset = absIndex
                        var preset = Settings.brew.getFlushPreset(absIndex)
                        if (preset) {
                            Settings.brew.flushFlow = preset.flow
                            Settings.brew.flushSeconds = preset.seconds
                        }
                        MainController.applyFlushSettings()

                        if (wasAlreadySelected) {
                            if (MachineState.isReady && idlePage.canStartOperations) {
                                DE1Device.startFlush()
                            } else {
                                console.log("Cannot start flush - machine not ready, phase:", MachineState.phase)
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                    AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                            }
                        }
                    }
                }
            }

            Loader {
                id: beanPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "beans"
                visible: active
                sourceComponent: PresetPillRow {
                    id: inlineBeanPresetRow
                    maxWidth: beanPresetLoader.width
                    presets: idlePage.visibleBags.map(function(b) { return { name: idlePage.bagLabel(b) } })
                    selectedIndex: {
                        var list = idlePage.visibleBags
                        for (var i = 0; i < list.length; ++i) {
                            if (list[i].id === Settings.dye.activeBagId) return i
                        }
                        return -1
                    }

                    pageCount: idlePage.beanPageCount
                    pageIndex: idlePage.beanPageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousBeans", "Previous beans")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextBeans", "Next beans")
                    onPageChangeRequested: function(delta) {
                        idlePage.beanPageIndex = Math.max(0, Math.min(idlePage.beanPageIndex + delta, idlePage.beanPageCount - 1))
                    }

                    onPresetSelected: function(index) {
                        var bag = idlePage.visibleBags[index]
                        if (!bag) return
                        Settings.dye.activeBagId = bag.id
                    }
                }
            }

            Loader {
                id: equipmentPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "equipment"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: equipmentPresetLoader.width
                    presets: idlePage.visibleEquipment.map(function(p) { return { name: idlePage.equipmentLabel(p) } })
                    selectedIndex: {
                        var list = idlePage.visibleEquipment
                        for (var i = 0; i < list.length; ++i) {
                            if (list[i].id === Settings.dye.activeEquipmentId) return i
                        }
                        return -1
                    }

                    pageCount: idlePage.equipmentPageCount
                    pageIndex: idlePage.equipmentPageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousEquipment", "Previous equipment")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextEquipment", "Next equipment")
                    onPageChangeRequested: function(delta) {
                        idlePage.equipmentPageIndex = Math.max(0, Math.min(idlePage.equipmentPageIndex + delta, idlePage.equipmentPageCount - 1))
                    }

                    onPresetSelected: function(index) {
                        var pkg = idlePage.visibleEquipment[index]
                        if (!pkg) return
                        Settings.dye.switchToEquipment(pkg)
                    }
                }
            }

            Loader {
                id: recipePresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: activePresetFunction === "recipes"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: recipePresetLoader.width
                    // Drink-type icon per pill; a stale recipe (linked bag
                    // finished) dims but still activates.
                    presets: idlePage.visibleRecipes.map(function(r) {
                        return { name: r.name,
                                 icon: DrinkType.icon(DrinkType.fromRecipeMap(r)),
                                 dimmed: r.stale === true,
                                 stateHint: r.stale === true ? TranslationManager.translate(
                                     "recipes.pill.bagFinished", "bag finished") : "" }
                    })
                    selectedIndex: {
                        var list = idlePage.visibleRecipes
                        for (var i = 0; i < list.length; ++i) {
                            if (list[i].id === MainController.selectedRecipeId) return i
                        }
                        return -1
                    }

                    pageCount: idlePage.recipePageCount
                    pageIndex: idlePage.recipePageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousRecipes", "Previous recipes")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextRecipes", "Next recipes")
                    onPageChangeRequested: function(delta) {
                        idlePage.recipePageIndex = Math.max(0, Math.min(idlePage.recipePageIndex + delta, idlePage.recipePageCount - 1))
                    }

                    onPresetSelected: function(index) {
                        var recipe = idlePage.visibleRecipes[index]
                        if (!recipe) return
                        // Match the profile/espresso pills: first tap selects the
                        // recipe (kicks off activation); tapping the selected
                        // recipe again starts the shot (when ready).
                        idlePage.tryStartRecipe(recipe)
                    }
                }
            }
        }

        // Center middle zone (shot plan, etc.)
        LayoutCenterZone {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: idlePage.centerMiddleYOffset
            zoneName: "centerMiddle"
            items: idlePage.centerMiddleItems
            zoneScale: idlePage.centerMiddleScale
            alignment: idlePage.zoneOpts("centerMiddle").alignment || "center"
            zoneStyle: idlePage.zoneOpts("centerMiddle").style || "standard"
        }
    }

    // ============================================================
    // Lower-mid bar (optional, from layout lowerMidBar zone)
    // Full-width band above the bottom action bar. Empty -> zero height.
    // Height-gated: hidden unless the viewport has room for the bar plus a
    // minimum center height (runtime-adaptive, no device-class check). This is a
    // heuristic on total free height, not a hard guarantee against the
    // vertically-centered center content reaching under the bar on mid-height
    // viewports; raise the threshold if overlap is seen.
    // ============================================================
    readonly property var lowerMidBarOptions: zoneOpts("lowerMidBar")
    // Lower-mid bar position (offset) + scale, matching the center-zone controls.
    readonly property int lowerMidBarYOffset: layoutConfig.offsets ? (layoutConfig.offsets.lowerMidBar || 0) : 0
    readonly property real lowerMidBarScale: layoutConfig.scales ? (layoutConfig.scales.lowerMidBar || 1.0) : 1.0
    readonly property bool lowerMidBarHasItems: idlePage.lowerMidBarItems.length > 0
    // Auto-grow: the band fits its content (large item-size makes it taller),
    // never smaller than the standard bar height.
    readonly property real lowerMidBarFullHeight: Math.max(Theme.scaled(82), lmbZone.implicitHeight)
    readonly property bool lowerMidBarFits:
        (idlePage.height - Theme.statusBarHeight - Theme.bottomBarHeight - lowerMidBarFullHeight) >= Theme.scaled(220)
    readonly property bool lowerMidBarVisible: lowerMidBarHasItems && lowerMidBarFits

    Rectangle {
        id: lowerMidBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        // Negative offset (the editor's "up") lifts the band off the bottom bar.
        anchors.bottomMargin: -idlePage.lowerMidBarYOffset
        visible: idlePage.lowerMidBarVisible
        height: visible ? idlePage.lowerMidBarFullHeight : 0
        color: Theme.zoneBackgroundColor(idlePage.lowerMidBarOptions.style)
        // Slides UP with the center content to clear a bottom-zone picker popup,
        // and DOWN out of the way when the center-zone carousel expands into it.
        transform: Translate {
            y: -idlePage.bottomPanelClearance + idlePage.carouselBandPush
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        }

        LayoutBarZone {
            id: lmbZone
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            zoneName: "lowerMidBar"
            items: idlePage.lowerMidBarItems
            distribution: idlePage.lowerMidBarOptions.distribution || "packed"
            alignment: idlePage.lowerMidBarOptions.alignment || "center"
            zoneStyle: idlePage.lowerMidBarOptions.style || "standard"
            itemSize: idlePage.lowerMidBarOptions.itemSize || "compact"
            zoneScale: idlePage.lowerMidBarScale
        }
    }

    // ============================================================
    // Bottom bar (from layout bottomLeft/bottomRight zones)
    // ============================================================
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // Auto-grow to fit large item-size; standard bar height otherwise.
        height: Math.max(Theme.bottomBarHeight, blZone.implicitHeight, brZone.implicitHeight)
        // When a custom background image is active, use the same neutral surface
        // scrim as StatusBar and the shared BottomBar so every bar reads
        // consistently and the wallpaper shows through; otherwise keep the
        // standard bottom-bar hue.
        color: Settings.theme.backgroundImagePath.length > 0
               ? Theme.scrimColor(Theme.surfaceColor)
               : Theme.bottomBarColor
        // opacity < 1 forces the scrim through the alpha pass; without it this
        // bar renders opaque and the wallpaper can't show through. See
        // docs/CLAUDE_MD/QML_GOTCHAS.md "Translucent element renders opaque".
        opacity: Settings.theme.backgroundImagePath.length > 0 ? 0.99 : 1.0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            LayoutBarZone {
                id: blZone
                zoneName: "bottomLeft"
                items: idlePage.bottomLeftItems
                distribution: idlePage.zoneOpts("bottomLeft").distribution || "packed"
                alignment: idlePage.zoneOpts("bottomLeft").alignment || "center"
                zoneStyle: idlePage.zoneOpts("bottomLeft").style || "standard"
                itemSize: idlePage.zoneItemSize("bottomLeft")
                Layout.fillHeight: true
            }

            Item { Layout.fillWidth: true }

            LayoutBarZone {
                id: brZone
                zoneName: "bottomRight"
                items: idlePage.bottomRightItems
                distribution: idlePage.zoneOpts("bottomRight").distribution || "packed"
                alignment: idlePage.zoneOpts("bottomRight").alignment || "center"
                zoneStyle: idlePage.zoneOpts("bottomRight").style || "standard"
                itemSize: idlePage.zoneItemSize("bottomRight")
                Layout.fillHeight: true
            }
        }
    }

    // Profile preview popup for long-press on espresso pills
    ProfilePreviewPopup {
        id: profilePreviewPopup
    }
}
