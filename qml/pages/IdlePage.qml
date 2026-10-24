// Loader/sourceComponent blocks below are nested components, so `idlePage` and the other ids
// in this file are not statically resolvable inside them without this pragma. There is no
// Repeater or delegate in this file, so no `required property` is needed.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../components/layout/PillFit.js" as PillFit

T.Page {
    id: idlePage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("idle.pageTitle", "Idle")

    objectName: "idlePage"
    StatusToast { id: homeOperationToast; error: true; durationMs: 5000 }

    function showHomeOperationError(message) {
        var text = message || TranslationManager.translate(
            "idle.operationFailed", "The selection could not be changed. Please try again.")
        homeOperationToast.show(text)
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null
                && AccessibilityManager.enabled)
            AccessibilityManager.announce(text, true)
    }
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
        if (AppShell.pendingBrewDialog) {
            AppShell.pendingBrewDialog = false
            AppShell.brewSettingsRequested()
        }
    }

    // Developer shortcut: hold the top-right corner for 5 seconds to simulate a completed shot.
    // Simulation Mode only: it saves into the real shot history and clears the
    // current shot notes, and an 80x80 corner is easy to hold by accident on a release tablet.
    Item {
        // DE1Device.simulationMode is the live state; Settings.app.simulationMode is the
        // choice for the next launch, and Ctrl+D changes only the device's.
        visible: DE1Device.simulationMode
        anchors.top: parent.top
        anchors.right: parent.right
        width: Theme.scaled(80)
        height: Theme.scaled(80)
        z: 100

        Timer {
            id: fakeShortHoldTimer
            interval: 5000
            onTriggered: {
                WebDebugLogger.debug("Shot", "IdlePage", ["DEV: Simulating completed shot"].map(String).join(" "))
                HomePresetActions.generateFakeShot()
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

    // Parse the same host-backed layout snapshot used by StatusBar and the layout editors.
    property var layoutConfig: {
        var raw = LayoutService.layoutConfiguration
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

    // Use the canonical LayoutSnapshot defaults shared with LayoutPreview. Reading
    // layoutConfig also makes these method-backed bindings refresh with the snapshot.
    property int centerStatusYOffset: { var d = layoutConfig; return LayoutService.getZoneYOffset("centerStatus") }
    property int centerTopYOffset: { var d = layoutConfig; return LayoutService.getZoneYOffset("centerTop") }
    property int centerMiddleYOffset: { var d = layoutConfig; return LayoutService.getZoneYOffset("centerMiddle") }

    // Center zone scales (user-configurable sizing)
    property real centerStatusScale: { var d = layoutConfig; return LayoutService.getZoneScale("centerStatus") }
    property real centerTopScale: { var d = layoutConfig; return LayoutService.getZoneScale("centerTop") }
    property real centerMiddleScale: { var d = layoutConfig; return LayoutService.getZoneScale("centerMiddle") }

    // Per-zone options map ({} when a zone has none). Every zone rendered here reads its
    // distribution / alignment / style / itemSize through this one accessor, so the idle
    // screen honors the same options LayoutPreview shows. Zones must be wired explicitly:
    // an unpassed option silently falls back to the zone component's default, which is how
    // the top/bottom bars ignored distribution/alignment/style before.
    function zoneOpts(zone: string): var {
        return (layoutConfig.zoneOptions && layoutConfig.zoneOptions[zone]) || ({})
    }

    // Per-zone item size ("compact" | "large"); bars grow to fit large items.
    function zoneItemSize(zone: string): string {
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
    // feed back into the offset it produces). The center column's top is its own y.
    //
    // lowerMidBarBottom is SOLVED, not a constant: it is the band's rest position
    // (bottomBar.y displaced by the user's signed zone Y-offset) except when the
    // centre column crowds the band, where it descends toward the bar. So this
    // extent moves with crowding, by up to the whole offset — reading bottomBar.y
    // flat would mis-measure in whichever direction that offset points, and reading
    // a fixed rest position would mis-measure whenever the band has slid.
    //
    // The squeeze itself still does not affect it: shrinking moves the band's TOP
    // edge, and this is its bottom.
    //
    // This is the band's BOX bottom, not the bottom of what it paints — the very
    // distinction the squeeze turns on elsewhere. Deliberate here: over-requesting
    // clearance for a popup is safe, under-requesting is not.
    readonly property real _idleContentBottom: {
        var colBottom = centerContent.y + centerContent.height
        var bandBottom = lowerMidBarVisible ? idlePage.lowerMidBarBottom : 0
        return Math.max(colBottom, bandBottom)
    }
    readonly property real _idleContentTop: centerContent.y

    // Called by a picker as its popup opens, with the popup's top edge (in
    // idlePage coords) and height. Slides content only by the overlap (0 when the
    // content doesn't reach the popup), bounded, in the direction set by which
    // half of the page the popup sits in.
    //
    // topPanelClearance has a second effect: it slides the centre column DOWN while
    // the band's own Translate does not carry that term, so lowerMidBarColumnBottom
    // counts it and an upper-half picker makes the band yield too — descending first,
    // then shrinking, exactly as a carousel does.
    function requestPanelClearance(panelTop: real, panelHeight: real) {
        var panelBottom = panelTop + panelHeight
        if ((panelTop + panelBottom) / 2 >= idlePage.height / 2) {
            let up = idlePage._idleContentBottom - panelTop + Theme.spacingSmall
            idlePage.bottomPanelClearance = Math.max(0, Math.min(up, idlePage._maxPanelClearance))
            idlePage.topPanelClearance = 0
        } else {
            let down = panelBottom - idlePage._idleContentTop + Theme.spacingSmall
            idlePage.topPanelClearance = Math.max(0, Math.min(down, idlePage._maxPanelClearance))
            idlePage.bottomPanelClearance = 0
        }
    }
    function releasePanelClearance() {
        idlePage.bottomPanelClearance = 0
        idlePage.topPanelClearance = 0
    }

    // Center-zone inline carousel: an open carousel grows the vertically-centred
    // centre column down towards the bottom-anchored lower-mid band, and the band
    // used to simply fade out when they met — taking the widgets in it (Grind,
    // Weather, …) off the screen for as long as the carousel stayed open.
    //
    // The BAND yields instead, cheapest first: it descends into the empty gap under
    // it (lowerMidBarBottom), then shrinks to the tallest height at which its content
    // still clears the column (lowerMidBarClearHeight), then fades. The SQUEEZE moves nothing
    // above the band: the band is bottom-anchored, so shrinking moves only its own
    // top edge, and the
    // centre column reads nothing from it. A layout whose column never reaches the
    // band is untouched in both states. (Sizing centre-zone readouts to their
    // content — LayoutCenterZone's Layout.preferredHeight — does re-flow the column,
    // once and at rest. That landed in PR Kulitorum/Decenza#1848, in LayoutCenterZone.qml.)
    //
    // Last resort, once the band has spent both cheaper responses. It is shoved down
    // FIRST (lowerMidBarBottom reclaims the user's upward zone Y-offset, which is
    // empty space) and shrunk SECOND; only when the descent has reached the bottom
    // bar and the height has reached its floor is there nothing left to give, and
    // the band is then HIDDEN rather than left overlapping the column.
    //
    // Asks whether the TALLEST clearing height (lowerMidBarClearHeight) has fallen
    // below the smallest the band may render at, rather than comparing against the
    // band's rendered top — and that is not a stylistic choice. lowerMidBar.height
    // animates (Behavior, below) and so does the preset row that drives the column
    // (Layout.preferredHeight, likewise), with independent lag. A predicate reading
    // the rendered lowerMidBar.y therefore goes transiently true mid-animation while
    // the band's top edge is still on its way down, which starts the very fade the
    // squeeze exists to avoid — a flash on every carousel open. The band's
    // anchors.bottomMargin animates too now, which only widens that trap. Comparing VALUES
    // removes the lag on the band's side. The column's own growth still animates, but
    // on an OPEN it runs in one direction only, so the predicate crosses once. A
    // SWITCH straight from one preset row to a shorter one (LayoutActions assigns
    // activePresetFunction directly, never via "") moves it the other way, which
    // un-squeezes the band and is the intended response rather than a flicker. And it
    // says what is actually meant: fade only once the band cannot give up any more
    // height.
    //
    // Still gated on an open carousel, which leaves one asymmetry worth knowing:
    // lowerMidBarClearHeight is not so gated, so a centre column tall enough to crowd
    // the band with no preset row open squeezes it to the minimum and then simply
    // overlaps, with no fade. Extending the fade there would change the resting idle
    // screen, which this change deliberately does not touch.
    readonly property bool carouselOverlapsBand: {
        if (idlePage.activePresetFunction === "" || !idlePage.lowerMidBarVisible)
            return false
        return idlePage.lowerMidBarClearHeight < idlePage.lowerMidBarMinHeight
    }

    Component.onCompleted: {
        BagService.requestInventory()
        EquipmentService.requestInventory()
        RecipeService.requestInventory()
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
    // MIRRORS PresetPillRow.ringOutset — the row reserves this much on each side
    // for the outward selection/focus rings, so pack against the same width or a
    // page that fits here would need a third row there.
    readonly property real _pillRingOutset: Theme.scaled(3) + Theme.focusMargin
    function _pillPagesFor(widths: var, availWidth: real): var {
        availWidth = Math.max(0, availWidth - 2 * _pillRingOutset)
        var sizes = PillFit.packPageSizes(widths, Theme.scaled(12), availWidth, 2)
        if (sizes.length <= 1)
            return sizes
        // Paginating → arrows appear → repack against the width minus the
        // symmetric arrow gutters (matches PresetPillRow.pillsAvailableWidth).
        return PillFit.packPageSizes(widths, Theme.scaled(12),
                                     Math.max(0, availWidth - 2 * Theme.scaled(48)), 2)
    }
    function _pillPageStart(sizes: var, pageIndex: int): int {
        var idx = Math.max(0, Math.min(pageIndex, sizes.length - 1))
        var start = 0
        for (let p = 0; p < idx; ++p)
            start += sizes[p]
        return start
    }
    function _pillPageSlice(list: var, sizes: var, pageIndex: int): var {
        if (!list || list.length === 0)
            return []
        var idx = Math.max(0, Math.min(pageIndex, sizes.length - 1))
        var start = _pillPageStart(sizes, pageIndex)
        return list.slice(start, start + (sizes[idx] || 0))
    }

    // ---- Pill-order freeze, shared by the three MRU-ordered rows (#1673) ----
    // Recipes, beans and equipment are all listed last_used DESC, and activating
    // a pill touches last_used. The next inventory therefore re-sorts the list
    // and the row REPACKS, moving pills out from under the user's finger. While
    // a row is open we hold the order it is showing (PillFit.keepOrder); the
    // freeze lifts on close, where a re-request adopts the true MRU order.
    //
    // The StackView check matters: activePresetFunction is NOT cleared by
    // navigation, so without it the freeze would still be engaged on the way
    // back from e.g. the Recipes page and anything created there would be held
    // at the tail of the list instead of appearing MRU-first.
    function _orderFrozen(row: var): bool {
        return activePresetFunction === row && StackView.status === StackView.Active
    }
    // Which MRU row is open, so the close edge can be detected in one place.
    property string _openMruRow: ""
    // Set while a close-time re-request is in flight: that inventory is the one
    // that lifts the freeze, so it must be taken in true MRU order even if the
    // user has already reopened the row (a fast close→open beats the reply).
    property bool _bagOrderLiftPending: false
    property bool _equipmentOrderLiftPending: false
    property bool _recipeOrderLiftPending: false
    function _liftOrderFreeze(row: var) {
        if (row === "recipes") {
            _recipeOrderLiftPending = true
            RecipeService.requestInventory()
        } else if (row === "beans") {
            _bagOrderLiftPending = true
            BagService.requestInventory()
        } else if (row === "equipment") {
            _equipmentOrderLiftPending = true
            EquipmentService.requestInventory()
        }
    }

    // Inventory bags for the beans pill row (bean-bag-inventory: pills are
    // bags, selection is activeBagId, no dirty state — edits write through).
    // The full MRU inventory (inventoryReady is MRU-ordered) is kept and paged;
    // the full inventory also lives on the Beans page.
    property var inventoryBags: []
    property int beanPageIndex: 0
    readonly property var _beanPageSizes: {
        var w = []
        for (let i = 0; i < inventoryBags.length; ++i)
            w.push(idlePillMetrics.advanceWidth(bagLabel(inventoryBags[i])) + Theme.scaled(40))
        return _pillPagesFor(w, beanPresetLoader.width)
    }
    readonly property int beanPageCount: Math.max(1, _beanPageSizes.length)
    readonly property var visibleBags: _pillPageSlice(inventoryBags, _beanPageSizes, beanPageIndex)

    function bagLabel(bag: var): string {
        if (!bag) return ""
        var coffee = bag.coffeeName || ""
        return coffee.length > 0 ? coffee : (bag.roasterName || "")
    }

    function startCurrentEspresso() { HomePresetActions.startEspresso() }

    Connections {
        target: BagService
        function onInventoryReady(bags) {
            const freeze = idlePage._orderFrozen("beans") && !idlePage._bagOrderLiftPending
            idlePage._bagOrderLiftPending = false
            idlePage.inventoryBags = freeze
                ? PillFit.keepOrder(idlePage.inventoryBags, bags, "id")
                : bags
            // Keep the page valid if bags were added/removed/reordered.
            idlePage.beanPageIndex = Math.max(0, Math.min(idlePage.beanPageIndex, idlePage.beanPageCount - 1))
        }
        function onBagsChanged() {
            BagService.requestInventory()
        }
        function onInventoryFailed() {
            idlePage.showHomeOperationError(TranslationManager.translate(
                "idle.bagInventoryFailed", "Could not load coffee bags. Reconnect and try again."))
        }
        function onBagActivated(bagId, success) {
            if (bagId !== idlePage._pendingBagActivationId) return
            idlePage._pendingBagActivationId = -1
            if (!success) idlePage.showHomeOperationError()
        }
    }
    property double _pendingBagActivationId: -1



    readonly property var brewUiPreferences: RecipeContext.preferences

    // Equipment packages for the equipment pill row (add-basket-equipment): pills
    // are packages, selection is activeEquipmentId. The full MRU inventory is
    // kept and paged into two-row pages (descriptive-recipe-names — previously
    // capped to 5 with no paging); the full inventory also lives on the
    // Equipment page.
    property var inventoryEquipment: []
    property int equipmentPageIndex: 0
    readonly property var _equipmentPageSizes: {
        var w = []
        for (let i = 0; i < inventoryEquipment.length; ++i)
            w.push(idlePillMetrics.advanceWidth(equipmentLabel(inventoryEquipment[i])) + Theme.scaled(40))
        return _pillPagesFor(w, equipmentPresetLoader.width)
    }
    readonly property int equipmentPageCount: Math.max(1, _equipmentPageSizes.length)
    readonly property var visibleEquipment: _pillPageSlice(inventoryEquipment, _equipmentPageSizes, equipmentPageIndex)

    function equipmentLabel(pkg: var): string {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""]
                .filter(function(s) { return s.length > 0 }).join(" ")
    }

    Connections {
        target: EquipmentService
        function onInventoryReady(packages) {
            const freeze = idlePage._orderFrozen("equipment") && !idlePage._equipmentOrderLiftPending
            idlePage._equipmentOrderLiftPending = false
            idlePage.inventoryEquipment = freeze
                ? PillFit.keepOrder(idlePage.inventoryEquipment, packages, "id")
                : packages
            idlePage.equipmentPageIndex = Math.max(0, Math.min(idlePage.equipmentPageIndex, idlePage.equipmentPageCount - 1))
        }
        function onPackagesChanged() {
            EquipmentService.requestInventory()
        }
        function onInventoryFailed(reason) {
            idlePage.showHomeOperationError(TranslationManager.translate(
                "idle.equipmentInventoryFailed", "Could not load equipment. Reconnect and try again."))
        }
        function onOperationCompleted(requestId, success, resultId, reason) {
            if (requestId !== idlePage._pendingEquipmentSelectionRequestId) return
            idlePage._pendingEquipmentSelectionRequestId = ""
            if (!success) idlePage.showHomeOperationError()
        }
    }
    property string _pendingEquipmentSelectionRequestId: ""

    // Recipes for the recipe pill row (add-recipes): pills are recipes,
    // selection is activeRecipeId, activation runs through MainController's
    // single path. The full MRU list (inventoryReady is MRU-ordered) is kept
    // and paged; the full list also lives on the Recipes page.
    property var inventoryRecipes: []
    property int recipePageIndex: 0
    readonly property var _recipePageSizes: {
        var w = []
        for (let i = 0; i < inventoryRecipes.length; ++i)
            // Recipe pills always carry a drink-type icon → add its width.
            w.push(idlePillMetrics.advanceWidth(inventoryRecipes[i].name || "")
                   + Theme.scaled(20) + Theme.scaled(6) + Theme.scaled(40))
        return _pillPagesFor(w, recipePresetLoader.width)
    }
    readonly property int recipePageCount: Math.max(1, _recipePageSizes.length)
    readonly property var visibleRecipes: _pillPageSlice(inventoryRecipes, _recipePageSizes, recipePageIndex)

    Connections {
        target: RecipeService
        function onInventoryReady(recipes) {
            const freeze = idlePage._orderFrozen("recipes") && !idlePage._recipeOrderLiftPending
            idlePage._recipeOrderLiftPending = false
            idlePage.inventoryRecipes = freeze
                ? PillFit.keepOrder(idlePage.inventoryRecipes, recipes, "id")
                : recipes
            // Keep the page valid if recipes were added/removed/reordered.
            idlePage.recipePageIndex = Math.max(0, Math.min(idlePage.recipePageIndex, idlePage.recipePageCount - 1))
        }
        function onRecipesChanged() {
            RecipeService.requestInventory()
        }
        function onInventoryFailed(reason) {
            idlePage.showHomeOperationError(TranslationManager.translate(
                "idle.recipeInventoryFailed", "Could not load recipes. Reconnect and try again."))
        }
    }

    // Favorite-profile pills (the espresso row) page the same way (descriptive-
    // recipe-names). selectedFavoriteProfile is an ABSOLUTE index into the full
    // favorites, so taps/selection map through _profilePageStart. The selected
    // pill may carry a modified marker that widens it — its width includes that.
    property int profilePageIndex: 0
    readonly property var _profilePageSizes: {
        var _m = ProfileService.profileModified  // re-measure when the marker toggles
        var favs = ProfileService.favoriteProfiles
        var currentFilename = ProfileService.currentProfileSummary.filename || ""
        var sel = idlePage._favoriteIndexForFilename(currentFilename)
        var w = []
        for (let i = 0; i < favs.length; ++i) {
            let name = (favs[i] && favs[i].name) || ""
            if (_m && i === sel)
                name = ProfileService.isCurrentProfileReadOnly
                    ? name + " " + TranslationManager.translate("presets.modified", "(modified)")
                    : "*" + name
            w.push(idlePillMetrics.advanceWidth(name) + Theme.scaled(40))
        }
        return _pillPagesFor(w, espressoColumnLoader.width)
    }
    readonly property int profilePageCount: Math.max(1, _profilePageSizes.length)
    readonly property int _profilePageStart: _pillPageStart(_profilePageSizes, profilePageIndex)
    readonly property var visibleProfiles: _pillPageSlice(ProfileService.favoriteProfiles, _profilePageSizes, profilePageIndex)

    function _favoriteIndexForFilename(filename: string): int {
        if (!filename) return -1
        var favorites = ProfileService.favoriteProfiles
        for (let i = 0; i < favorites.length; ++i) {
            if (favorites[i] && favorites[i].filename === filename) return i
        }
        return -1
    }

    // Flush and hot-water pill rows page the same way (descriptive-recipe-names).
    // Both use an ABSOLUTE selected index (Settings.brew), so taps map through
    // the page start. No icon on these pills.
    property int flushPageIndex: 0
    readonly property var _flushPageSizes: {
        var favs = idlePage.brewUiPreferences.flushPresets || []
        var w = []
        for (let i = 0; i < favs.length; ++i)
            w.push(idlePillMetrics.advanceWidth((favs[i] && favs[i].name) || "") + Theme.scaled(40))
        return _pillPagesFor(w, flushPresetLoader.width)
    }
    readonly property int flushPageCount: Math.max(1, _flushPageSizes.length)
    readonly property int _flushPageStart: _pillPageStart(_flushPageSizes, flushPageIndex)
    readonly property var visibleFlush: _pillPageSlice(idlePage.brewUiPreferences.flushPresets || [], _flushPageSizes, flushPageIndex)

    property int hotWaterPageIndex: 0
    readonly property var _hotWaterPageSizes: {
        var favs = idlePage.brewUiPreferences.waterVesselPresets || []
        var w = []
        for (let i = 0; i < favs.length; ++i)
            w.push(idlePillMetrics.advanceWidth((favs[i] && favs[i].name) || "") + Theme.scaled(40))
        return _pillPagesFor(w, hotWaterPresetLoader.width)
    }
    readonly property int hotWaterPageCount: Math.max(1, _hotWaterPageSizes.length)
    readonly property int _hotWaterPageStart: _pillPageStart(_hotWaterPageSizes, hotWaterPageIndex)
    readonly property var visibleWaterVessels: _pillPageSlice(idlePage.brewUiPreferences.waterVesselPresets || [], _hotWaterPageSizes, hotWaterPageIndex)

    // Recipe selection is host-owned and shared with the compact RecipesItem.
    // See tryStartRecipe() below for the two-tap select-then-start handler.
    function tryStartRecipe(recipe: var) { HomePresetActions.tapRecipe(recipe.id) }

    // Track which function's presets are showing (used by center-zone action items)
    property string activePresetFunction: ""  // "", "steam", "espresso", "hotwater", "flush", "beans", "equipment", "recipes"
    property bool _preparationRowSyncPending: false
    property string _pendingPreparationRowMode: ""

    function isPreparationMode(mode: string): bool {
        return PreparationService.state.isPreparationMode(mode)
    }

    // The row is local presentation state. Selecting a preparation row requests
    // the host mode and opens only after the host confirms it; closing the row
    // leaves host mode intact.
    function togglePresetFunction(mode: string) {
        if (activePresetFunction === mode) {
            if (_pendingPreparationRowMode === mode)
                _pendingPreparationRowMode = ""
            activePresetFunction = ""
            return
        }
        if (isPreparationMode(mode)) {
            if (!PreparationService.state.available)
                return
            _pendingPreparationRowMode = mode
            if (!PreparationService.state.selectMode(mode)) {
                if (_pendingPreparationRowMode === mode)
                    _pendingPreparationRowMode = ""
                return
            }
            if (PreparationService.state.mode === mode) {
                _pendingPreparationRowMode = ""
                activePresetFunction = mode
                return
            }
            // Wait for the host's modeChanged signal before opening the row.
            return
        }
        activePresetFunction = mode
    }

    function syncPreparationModeToRow() {
        if (StackView.status !== StackView.Active || !PreparationService.state.available)
            return
        if (_pendingPreparationRowMode !== "") {
            if (PreparationService.state.mode === _pendingPreparationRowMode)
                activePresetFunction = PreparationService.state.mode
            _pendingPreparationRowMode = ""
            return
        }
        // Do not close a local inventory/editor row when the host mode changes.
        if (activePresetFunction === "" || isPreparationMode(activePresetFunction))
            activePresetFunction = PreparationService.state.mode
    }

    function preparationStateChanged() {
        if (!PreparationService.state.available)
            _pendingPreparationRowMode = ""
        if (StackView.status === StackView.Active) {
            syncPreparationModeToRow()
            _preparationRowSyncPending = false
        } else {
            _preparationRowSyncPending = true
        }
        _publishOperationMode()
    }

    // The host owns bean-weight capture and dose updates. This page only presents
    // the capture state and a transient confirmation while it is active.
    property bool beanCaptureShown: false
    property string beanCaptureText: ""
    Timer { id: idleBeanCaptureTimer; interval: 3500; onTriggered: idlePage.beanCaptureShown = false }
    readonly property WeightCaptureState beanCapture: PreparationService.state.bean
    Connections {
        target: PreparationService.state
        function onBeanCaptured(grams) {
            if (idlePage.StackView.status !== StackView.Active
                    || PreparationService.state.mode !== "espresso")
                return
            idlePage.beanCaptureText = TranslationManager.translate("idle.doseCaptured", "Dose set: %1g").arg(grams.toFixed(1))
            idlePage.beanCaptureShown = true
            idleBeanCaptureTimer.restart()
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null
                    && AccessibilityManager.enabled)
                AccessibilityManager.announce(idlePage.beanCaptureText)
        }
    }

    // Publish the live dose-weighing state on Theme for the Beans layout widget
    // (DoseWeightItem) — the widget can live outside IdlePage (persistent status
    // bar), so it can't reach beanCapture directly. Live only while this page is
    // showing and an uncaptured dose sits on the scale with a saved cup tare —
    // stricter than the panel readout below, which keeps ticking for an already-
    // captured load; -1 = not weighing. The engine's re-arm (net change >
    // rearmDelta after capture) drops isCaptured, so adding more beans after
    // capture goes live again automatically.
    Binding {
        target: Theme
        property: "doseLiveNetG"
        value: (idlePage.visible && beanCapture.active && beanCapture.loadPresent && !beanCapture.isCaptured
                && idlePage.brewUiPreferences.doseCupTareWeight > 0)
               ? Math.max(0, beanCapture.netWeight) : -1
    }
    Binding {
        target: Theme
        property: "doseCaptureFlash"
        value: idlePage.visible && idlePage.beanCaptureShown
    }

    // The host owns milk capture and steam-time learning. This page only presents
    // a transient confirmation for a capture while IdlePage is active.
    property bool milkCaptureShown: false
    property string milkCaptureText: ""
    Timer { id: idleMilkCaptureTimer; interval: 3500; onTriggered: idlePage.milkCaptureShown = false }
    MilkCapture {
        id: idleMilkCapture
        // Host-owned milk auto-capture is only presented while
        // the steam flow is showing AND this page is the active StackView page — so a
        // stray weight never silently changes the steam stop time, and the capture
        // can't double-fire alongside SteamPage's own copy when SteamPage is pushed
        // on top (long-press) while activePresetFunction is still "steam".
        onMilkCaptured: function(milk, t) {
            if (idlePage.StackView.status !== StackView.Active
                    || PreparationService.state.mode !== "steam")
                return
            if (t <= 0) return
            idlePage.milkCaptureText = TranslationManager.translate("idle.steamCaptured", "Steam time: %1s for %2g milk").arg(t).arg(milk.toFixed(0))
            idlePage.milkCaptureShown = true
            idleMilkCaptureTimer.restart()
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(idlePage.milkCaptureText)
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
                                              && idlePage.brewUiPreferences.doseCupTareWeight > 0
                                              && beanCapture.loadPresent
                                              && beanCapture.netWeight >= beanCapture.minNet
                                              && beanCapture.netWeight <= beanCapture.maxNet
        readonly property bool milkSettling: idleMilkCapture.active && !idleMilkCapture.isCaptured
                                            && idleMilkCapture.cupWeight > 0
                                            && idleMilkCapture.loadPresent
                                            && idleMilkCapture.netWeight >= idleMilkCapture.minNet
                                            && idleMilkCapture.netWeight <= idleMilkCapture.maxNet
        readonly property string milkHint: SteamLabels.captureHint(idleMilkCapture)
        visible: beansSettling || milkSettling || milkHint !== ""
        text: milkHint !== "" ? milkHint
            : TranslationManager.translate("scale.waitForBell", "Wait for the bell before you take it off the scale")
        color: Theme.warningColor
        font: Theme.labelFont
        SequentialAnimation on opacity {
            running: waitForBellHint.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 450 }
            NumberAnimation { to: 1.0; duration: 450 }
        }
    }

    // Publish the selected operation (espresso/steam/…) to the Theme singleton so the
    // persistent status-bar widgets (e.g. the page-aware Plan widget) can tell what the
    // user has selected on the idle screen — they load as separate components and can't
    // read this property by scope. Cleared when this page isn't the active one.
    function _publishOperationMode() {
        Theme.currentOperationMode =
            (StackView.status === StackView.Active && PreparationService.state.available)
                ? PreparationService.state.mode : ""
    }
    StackView.onStatusChanged: {
        if (StackView.status === StackView.Active && _preparationRowSyncPending) {
            syncPreparationModeToRow()
            _preparationRowSyncPending = false
        }
        _publishOperationMode()
        // Coming back with a row still open: _orderFrozen() was false the whole
        // time we were away, but anything created on the page we just left (a new
        // recipe, a new bag) may have arrived while it was true again mid-pop. Ask
        // once for a fresh list and take it unfrozen, so a new item shows up
        // MRU-first instead of being appended at the tail of the frozen order.
        if (StackView.status === StackView.Active && _openMruRow !== "")
            _liftOrderFreeze(_openMruRow)
    }

    Connections {
        target: PreparationService.state
        function onModeChanged() { idlePage.preparationStateChanged() }
        function onAvailableChanged() { idlePage.preparationStateChanged() }
        function onActionFailed(action, message) {
            if (idlePage._pendingPreparationRowMode === action)
                idlePage._pendingPreparationRowMode = ""
        }
    }

    // Keep the local row presentation and accessibility announcements in sync.
    onActivePresetFunctionChanged: {
        // A local close or inventory selection cancels any unacknowledged mode
        // request so a late host echo cannot reopen a row over the user's choice.
        if (_pendingPreparationRowMode !== "" && activePresetFunction !== _pendingPreparationRowMode)
            _pendingPreparationRowMode = ""
        _publishOperationMode()
        // An MRU row just closed → lift its order freeze and adopt the real MRU
        // order for the next open (see _liftOrderFreeze / _orderFrozen).
        var nowOpen = (activePresetFunction === "recipes" || activePresetFunction === "beans"
                       || activePresetFunction === "equipment") ? activePresetFunction : ""
        if (_openMruRow !== "" && _openMruRow !== nowOpen)
            _liftOrderFreeze(_openMruRow)
        _openMruRow = nowOpen

        // Paged pill rows always (re)open on the first page — the most-recent items.
        if (activePresetFunction === "recipes") recipePageIndex = 0
        else if (activePresetFunction === "beans") beanPageIndex = 0
        else if (activePresetFunction === "espresso") profilePageIndex = 0
        else if (activePresetFunction === "equipment") equipmentPageIndex = 0
        else if (activePresetFunction === "flush") flushPageIndex = 0
        else if (activePresetFunction === "hotwater") hotWaterPageIndex = 0
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled && activePresetFunction !== "") {
            let presets = []
            let selectedName = ""
            switch (activePresetFunction) {
                case "espresso": {
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleProfiles
                    let selAbs = idlePage._favoriteIndexForFilename(
                        ProfileService.currentProfileSummary.filename || "")
                    let selRel = selAbs - idlePage._profilePageStart
                    if (selRel >= 0 && selRel < presets.length) {
                        selectedName = presets[selRel].name
                    }
                    break
                }
                case "steam":
                    // Resolve through the helper, not by position: the built-in
                    // "Heater off" pitcher is stored as a sentinel, so a `>= 0`
                    // index test announced an empty name for a perfectly valid
                    // selection.
                    selectedName = SteamLabels.pitcherName(
                        SteamService.getSteamPitcherPreset(SteamService.state.selectedSteamPitcher))
                    break
                case "hotwater": {
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleWaterVessels
                    let selWv = idlePage.brewUiPreferences.selectedWaterVessel - idlePage._hotWaterPageStart
                    if (selWv >= 0 && selWv < presets.length) {
                        selectedName = presets[selWv].name
                    }
                    break
                }
                case "flush": {
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleFlush
                    let selFl = idlePage.brewUiPreferences.selectedFlushPreset - idlePage._flushPageStart
                    if (selFl >= 0 && selFl < presets.length) {
                        selectedName = presets[selFl].name
                    }
                    break
                }
                case "beans":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleBags.map(function(b) { return { name: idlePage.bagLabel(b) } })
                    for (let bi = 0; bi < idlePage.visibleBags.length; ++bi) {
                        if (idlePage.visibleBags[bi].id === BagService.activeBagId) {
                            selectedName = idlePage.bagLabel(idlePage.visibleBags[bi])
                            break
                        }
                    }
                    break
                case "equipment":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleEquipment.map(function(p) { return { name: idlePage.equipmentLabel(p) } })
                    for (let ei = 0; ei < idlePage.visibleEquipment.length; ++ei) {
                        if (idlePage.visibleEquipment[ei].id === EquipmentService.activePackageId) {
                            selectedName = idlePage.equipmentLabel(idlePage.visibleEquipment[ei])
                            break
                        }
                    }
                    break
                case "recipes":
                    // Announce the visible page (the row just reset to page 1).
                    presets = idlePage.visibleRecipes.map(function(r) { return { name: r.name } })
                    for (let ri = 0; ri < idlePage.visibleRecipes.length; ++ri) {
                        // Match the pill highlight (selectedIndex) — the synchronous
                        // The shared machine-control selection, not the lagging storage active id.
                        if (idlePage.visibleRecipes[ri].id === MachineControlService.selectedRecipeId) {
                            selectedName = idlePage.visibleRecipes[ri].name
                            break
                        }
                    }
                    break
            }

            if (presets.length > 0) {
                let names = []
                for (let i = 0; i < presets.length; i++) {
                    names.push(presets[i].name)
                }
                let announcement = presets.length + " " + TranslationManager.translate("idle.accessible.presets", "presets") + ": " + names.join(", ")
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
        enabled: idlePage.activePresetFunction !== "" &&
                 !(typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled)
        onClicked: idlePage.activePresetFunction = ""
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
            // qmllint disable Quick.layout-positioning
            // False positive, verified: this `y` belongs to the Translate transform, not to the
            // layout-managed item. A transform is precisely how you offset an item inside a layout
            // WITHOUT fighting the layout — the alternative qmllint suggests (Layout.topMargin)
            // would make the layout re-measure on every animation frame. The linter attributes the
            // `y` to the enclosing item and cannot see the transform boundary.
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
            // qmllint enable Quick.layout-positioning
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
            Layout.preferredHeight: idlePage.activePresetFunction !== "" ? activePresetRow.implicitHeight : 0
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.scaled(900)
            Layout.leftMargin: Theme.standardMargin
            Layout.rightMargin: Theme.standardMargin
            clip: true

            property var activePresetRow: {
                switch (idlePage.activePresetFunction) {
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
                active: idlePage.activePresetFunction === "steam"
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
                Connections {
                    target: PreparationService.state
                    function onChanged() {
                        if (steamPresetLoader.active) steamPresetLoader.steamPillSuffixVersion++
                    }
                }
                Connections {
                    target: SteamService
                    function onStateChanged() {
                        if (steamPresetLoader.active) steamPresetLoader.steamPillSuffixVersion++
                    }
                }

                sourceComponent: Column {
                    width: parent ? parent.width : 0
                    spacing: Theme.scaled(8)

                    PresetPillRow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        maxWidth: steamPresetLoader.width
                        presets: SteamService.state.steamPitcherPresets
                        selectedIndex: SteamService.state.selectedSteamPitcherDisplayIndex
                        supportLongPress: true
                        pillSuffixMaxWidth: Theme.scaled(60)  // Reserve ~"(1234g)" worth of width
                        pillSuffixVersion: steamPresetLoader.steamPillSuffixVersion

                        // Live milk weigh: scale reading minus the saved empty-pitcher
                        // weight, updating as milk is poured. Assumes the scale's zero is
                        // the empty scale; a manual tare with the pitcher on makes it read
                        // low. Display only — steam-time scaling uses netMilkForPitcher(),
                        // whose 50–1500 g window is sized for time scaling and would zero small amounts
                        // here. The rule itself is SteamLabels' — this only wires it in,
                        // so the SteamItem popup cannot drift from it.
                        pillSuffixFn: function(preset) { return SteamLabels.pitcherPillSuffix(preset) }

                        // "<name> Pitcher" (e.g. "Small Pitcher"), except where the
                        // name already says pitcher and for the built-in entry.
                        pillLabelFn: function(preset, name) { return SteamLabels.pitcherPillLabel(preset, name) }

                        onPresetSelected: function(index) { HomePresetActions.tapSteamPitcher(index) }

                        // Long-press a pitcher to open the steam page settings, where you set
                        // the duration and either tap "Weigh" next to Reference milk (with milk
                        // on the scale) or "Use as baseline" to teach this pitcher its
                        // milk-weight -> steam-time reference.
                        onPresetLongPressed: function(index) {
                            HomePresetActions.selectSteamPitcher(index)
                            AppShell.steamRequested()
                        }
                    }

                    // "Place the milk pitcher on the scale" — same position as the bean prompt (below
                    // the pills). Shown while the host milk detector is active and its
                    // seeded baseline sees no pitcher load yet. Blinks three times.
                    // "or lift and replace": the detectors take their first settled reading as the empty
                    // scale, so a pitcher ALREADY sitting there won't register until it's lifted and set
                    // back — without the hedge the prompt would assert something false.
                    // The hint promises a beep ONLY when the capture sound will actually play — the
                    // ding is separately gated on doseCaptureSoundEnabled (default off).
                    Text {
                        id: steamPlacePrompt
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        visible: idleMilkCapture.active && idleMilkCapture.seeded
                                 && idleMilkCapture.rawWeight - idleMilkCapture.virtualZero < 50
                        text: TranslationManager.translate("idle.label.placeOrReplacePitcher", "Place (or lift and replace) the milk pitcher on the scale") + "\n"
                            + (idlePage.brewUiPreferences.doseCaptureSoundEnabled
                                ? TranslationManager.translate("idle.label.placePitcherHint", "(and wait for the beep before removing)")
                                : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                        PromptPulse on opacity {
                            running: steamPlacePrompt.visible
                        }
                    }
                }
            }

            Loader {
                id: espressoColumnLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: idlePage.activePresetFunction === "espresso"
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
                            var sel = idlePage._favoriteIndexForFilename(
                                ProfileService.currentProfileSummary.filename || "")
                            if (sel < 0) return -1
                            var rel = sel - idlePage._profilePageStart
                            return (rel >= 0 && rel < idlePage.visibleProfiles.length) ? rel : -1
                        }
                        supportLongPress: true
                        modified: ProfileService.profileModified
                        modifiedIsReadOnly: ProfileService.isCurrentProfileReadOnly

                        pageCount: idlePage.profilePageCount
                        pageIndex: idlePage.profilePageIndex
                        prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousProfiles", "Previous profiles")
                        nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextProfiles", "Next profiles")
                        onPageChangeRequested: function(delta) {
                            idlePage.profilePageIndex = Math.max(0, Math.min(idlePage.profilePageIndex + delta, idlePage.profilePageCount - 1))
                        }

                        onPresetSelected: function(index) {
                            const preset = ProfileService.favoriteProfiles[idlePage._profilePageStart + index]
                            if (preset && preset.filename) HomePresetActions.tapProfile(preset.filename)
                        }

                        onPresetLongPressed: function(index) {
                            var absIndex = idlePage._profilePageStart + index
                            var preset = ProfileService.favoriteProfiles[absIndex]
                            if (preset && preset.filename) {
                                if (preset.filename !== ProfileService.currentProfileSummary.filename) {
                                    HomePresetActions.loadProfile(preset.filename)
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
                        visible: idlePage._favoriteIndexForFilename(
                                     ProfileService.currentProfileSummary.filename || "") === -1
                                 && idlePage.canStartOperations
                        spacing: Theme.scaled(8)

                        Rectangle {
                            id: nonFavoriteProfilePill
                            width: nonFavoriteProfileText.implicitWidth + Theme.scaled(40)
                            height: Theme.scaled(50)
                            radius: Theme.scaled(10)
                            color: Theme.successColor

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: (ProfileService.currentProfileSummary.name || "") + " " + TranslationManager.translate("idle.accessible.startespresso", "Start espresso")
                            Accessible.focusable: true
                            Accessible.onPressAction: idleNonFavMouseArea.clicked(null)
                            Keys.onReturnPressed: function(event) { idleNonFavMouseArea.clicked(null); event.accepted = true }
                            Keys.onSpacePressed: function(event) { idleNonFavMouseArea.clicked(null); event.accepted = true }

                            Text {
                                id: nonFavoriteProfileText
                                anchors.centerIn: parent
                                text: ProfileService.currentProfileSummary.name || ""
                                color: Theme.primaryContrastColor
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: idleNonFavMouseArea
                                anchors.fill: parent
                                onClicked: idlePage.startCurrentEspresso()
                            }
                        }

                        ProfileInfoButton {
                            anchors.verticalCenter: parent.verticalCenter
                            profileFilename: ProfileService.currentProfileSummary.filename || ""
                            profileName: ProfileService.currentProfileSummary.name || ""

                            onClicked: {
                                AppShell.profileInfoRequested(ProfileService.currentProfileSummary.filename || "",
                                                              ProfileService.currentProfileSummary.name || "")
                            }
                        }
                    }

                    // Bean weight: live net-weight readout from the host capture state.
                    // Only shown when a dose-cup tare is saved.
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
                                 && idlePage.brewUiPreferences.doseCupTareWeight > 0
                        spacing: Theme.scaled(8)

                        // Live net beans briefly flash in the accent color after capture.
                        Text {
                            id: weighBeansText
                            horizontalAlignment: Text.AlignHCenter
                            // True while prompting the user to place beans (no load on
                            // the scale yet) — this state pulses (PromptPulse).
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
                                     + (idlePage.brewUiPreferences.doseCaptureSoundEnabled
                                         ? TranslationManager.translate("idle.label.placeBeansHint", "(and wait for the beep before removing)")
                                         : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
                            }
                            color: idlePage.beanCaptureShown ? Theme.primaryColor : Theme.textSecondaryColor
                            font: Theme.labelFont
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                            onShowingPlacePromptChanged: if (!showingPlacePrompt) opacity = 1.0
                            PromptPulse on opacity {
                                running: weighBeansText.showingPlacePrompt
                            }
                        }
                    }
                }
            }

            Loader {
                id: hotWaterPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: idlePage.activePresetFunction === "hotwater"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: hotWaterPresetLoader.width
                    // Windowed to the current two-row page; selection/taps map to
                    // the absolute selectedWaterVessel via _hotWaterPageStart.
                    presets: idlePage.visibleWaterVessels
                    selectedIndex: {
                        var rel = idlePage.brewUiPreferences.selectedWaterVessel - idlePage._hotWaterPageStart
                        return (rel >= 0 && rel < idlePage.visibleWaterVessels.length) ? rel : -1
                    }

                    pageCount: idlePage.hotWaterPageCount
                    pageIndex: idlePage.hotWaterPageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousHotWater", "Previous vessels")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextHotWater", "Next vessels")
                    onPageChangeRequested: function(delta) {
                        idlePage.hotWaterPageIndex = Math.max(0, Math.min(idlePage.hotWaterPageIndex + delta, idlePage.hotWaterPageCount - 1))
                    }

                    onPresetSelected: function(index) { HomePresetActions.tapWaterVessel(idlePage._hotWaterPageStart + index) }
                }
            }

            Loader {
                id: flushPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: idlePage.activePresetFunction === "flush"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: flushPresetLoader.width
                    // Windowed to the current two-row page; selection/taps map to
                    // the absolute selectedFlushPreset via _flushPageStart.
                    presets: idlePage.visibleFlush
                    selectedIndex: {
                        var rel = idlePage.brewUiPreferences.selectedFlushPreset - idlePage._flushPageStart
                        return (rel >= 0 && rel < idlePage.visibleFlush.length) ? rel : -1
                    }

                    pageCount: idlePage.flushPageCount
                    pageIndex: idlePage.flushPageIndex
                    prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousFlush", "Previous flushes")
                    nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextFlush", "Next flushes")
                    onPageChangeRequested: function(delta) {
                        idlePage.flushPageIndex = Math.max(0, Math.min(idlePage.flushPageIndex + delta, idlePage.flushPageCount - 1))
                    }

                    onPresetSelected: function(index) { HomePresetActions.tapFlushPreset(idlePage._flushPageStart + index) }
                }
            }

            Loader {
                id: beanPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: idlePage.activePresetFunction === "beans"
                visible: active
                sourceComponent: PresetPillRow {
                    id: inlineBeanPresetRow
                    maxWidth: beanPresetLoader.width
                    presets: idlePage.visibleBags.map(function(b) { return { name: idlePage.bagLabel(b) } })
                    selectedIndex: {
                        var list = idlePage.visibleBags
                        for (let i = 0; i < list.length; ++i) {
                            if (list[i].id === BagService.activeBagId) return i
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
                        idlePage._pendingBagActivationId = bag.id
                        BagService.requestActivateBag(bag.id)
                    }
                }
            }

            Loader {
                id: equipmentPresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: idlePage.activePresetFunction === "equipment"
                visible: active
                sourceComponent: PresetPillRow {
                    maxWidth: equipmentPresetLoader.width
                    presets: idlePage.visibleEquipment.map(function(p) { return { name: idlePage.equipmentLabel(p) } })
                    selectedIndex: {
                        var list = idlePage.visibleEquipment
                        for (let i = 0; i < list.length; ++i) {
                            if (list[i].id === EquipmentService.activePackageId) return i
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
                        var requestId = EquipmentService.newRequestId()
                        idlePage._pendingEquipmentSelectionRequestId = requestId
                        EquipmentService.activatePackage(pkg.id, true, requestId)
                    }
                }
            }

            Loader {
                id: recipePresetLoader
                width: parent.width
                anchors.horizontalCenter: parent.horizontalCenter
                active: idlePage.activePresetFunction === "recipes"
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
                        for (let i = 0; i < list.length; ++i) {
                            if (list[i].id === MachineControlService.selectedRecipeId) return i
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
    readonly property int lowerMidBarYOffset: { var d = layoutConfig; return LayoutService.getZoneYOffset("lowerMidBar") }
    readonly property real lowerMidBarScale: { var d = layoutConfig; return LayoutService.getZoneScale("lowerMidBar") }
    readonly property bool lowerMidBarHasItems: idlePage.lowerMidBarItems.length > 0
    // Auto-grow: the band fits its content (large item-size makes it taller). This
    // is the band's size when nothing is crowding it, and the baseline every figure
    // below measures against — NOT a floor on what it renders at, which is
    // lowerMidBarMinHeight. The scaled(82) is this zone's own minimum and is not
    // Theme.bottomBarHeight, which is scaled(70) (Theme.qml:1034) and is separately
    // applied to lmbZone.implicitHeight by LayoutBarZone.qml:51.
    // Declared once: the guard below, the Rectangle's fill and the zone's own style
    // all read these rather than re-spelling the default. The squeeze switches on
    // whether the band paints, so the guard and the fill agreeing is load-bearing,
    // and three copies of `|| "standard"` is three chances for them to stop.
    readonly property string lowerMidBarStyle: idlePage.lowerMidBarOptions.style || "standard"
    readonly property color lowerMidBarFill: Theme.zoneBackgroundColor(idlePage.lowerMidBarStyle)

    readonly property real lowerMidBarRestHeight: Math.max(Theme.scaled(82), lmbZone.implicitHeight)
    // Floor on the squeeze: the smallest the band may render. Past it the band stops
    // giving up height; with a carousel open carouselOverlapsBand then fades it, and
    // with none open it simply overlaps (see that property for why the two differ).
    //
    // It does TWO jobs, and saying so matters because an earlier version of this
    // comment claimed one "rather than" the other: it floors the rendered height
    // (lowerMidBarCurrentHeight) AND it is the fade threshold (carouselOverlapsBand).
    // With no carousel open the fade is gated off, so there it does only the first.
    //
    // What it does NOT do is keep the widgets tappable, though it is easy to write
    // that — and this comment previously did. The
    // squeeze is a render scale on the zone, and QQuickItem::scale transforms hit
    // testing along with painting, so a widget's tap area shrinks with
    // lowerMidBarContentScale = currentHeight / restHeight. Flooring currentHeight
    // bounds the BOX; the tap targets inside still scale, and the taller the band's
    // rest height the smaller they get at the floor.
    //
    // Flat rather than proportional, and the cost is real: at restHeight scaled(82)
    // the worst content scale is ~0.54, but a tall band (itemSize "large", or a user
    // zone scale) reaches ~0.31, where a compact widget's tap area is well under
    // Theme.touchTargetMin. The previous max(touchTargetMin, restHeight * 0.6) bought
    // a 0.6 worst case — and bought it by hiding the band instead, which is the worse
    // failure. A band too small to tap can still be read, and its widgets remain
    // reachable from their own pages; a band that is not there tells the user
    // nothing. Small beats absent.
    //
    // NOT justified by the SM-X210 Steam-row case that prompted it: the slide below
    // resolves that one on its own (clear 69.07 against even the old 65.4 floor).
    // This stands or falls on the argument above.
    readonly property real lowerMidBarMinHeight: Theme.touchTargetMin

    // Where the band would sit if nothing were crowding it: bottomBar.top displaced
    // by the user's signed zone Y-offset, so the offset adds to the bottom. The
    // Rectangle's anchors.bottomMargin is this offset's negation AT REST only — once
    // the band slides the margin follows the solved edge instead.
    readonly property real lowerMidBarRestBottom:
        bottomBar.y + idlePage.lowerMidBarYOffset
    // The lowest it may sit. Below the rest position only for a NEGATIVE offset — a
    // lift, with genuinely empty space underneath it — and there the limit is the
    // bottom bar. A positive offset already pushes the band down over the bar, so
    // maxBottom collapses to restBottom and there is no slide to be had.
    readonly property real lowerMidBarMaxBottom:
        Math.max(idlePage.lowerMidBarRestBottom, bottomBar.y)

    // The band's bottom edge, solved rather than fixed — the FIRST thing it gives up
    // when crowded, and the cheapest.
    //
    // A user's upward offset is a decorative gap: reclaiming it costs a slide into
    // space that was empty anyway, and buys real height at 1/k per unit. Shrinking,
    // by contrast, costs legibility, and fading costs the widgets entirely. So the
    // order is slide, then shrink, then fade, and this is the first step: descend
    // only as far as full height requires, never past the bar, never above the
    // user's chosen position. With nothing crowding the band the solve returns the
    // rest position exactly, so a layout that never needs the room never moves.
    //
    // On the measured SM-X210 case, with the Steam preset row open: the offset is
    // -40, worth 40/k = 43 units of height. Clearing the fade threshold needed 33 of
    // them (clear was 25.9 against a floor of 59), so the slide saves the band — and
    // full height would have needed 83, so it does NOT restore it to full size. The
    // band ends at 69 of 109, scale 0.64. Both halves of that matter: the slide is
    // what makes it visible, and the shrink is what it still costs.
    readonly property real lowerMidBarBottom: {
        var wantFullHeight = idlePage.lowerMidBarColumnBottom
            + idlePage.lowerMidBarRestHeight * idlePage.lowerMidBarContentTopFactor
        return Math.max(idlePage.lowerMidBarRestBottom,
                        Math.min(idlePage.lowerMidBarMaxBottom, wantFullHeight))
    }

    // The height at which the band's CONTENT clears the centre column, solved
    // directly rather than by subtracting an intrusion.
    //
    // One discrepancy, worth seeing from two angles — the whole of it is the factor
    // 1/k below, and k < 1 IS the padding. Neither angle alone is the bug: correcting
    // only the first would leave the band too TALL (R(1-k) + D, or 90 against the
    // correct 85.7 in the example). Together they hid it over visible empty space.
    //
    // The band paints its items centred, and lowerMidBarRestHeight carries a floor
    // (scaled(82) here, Theme.bottomBarHeight inside LayoutBarZone) that a short row
    // does not fill. Measuring the column against the band's top EDGE therefore
    // counts padding nothing draws in — the same mistake the centre zones used to
    // make on their own side, from the other direction. contentImplicitHeight is the
    // unfloored painted height, so the comparison is content against content — for
    // the transparent default style. A band that PAINTS a background pins the factor
    // to 1 (see the guard below), which compares box against box and so reduces
    // exactly to the subtraction described next; that is correct there, because a
    // painted slab has no spare padding to give away.
    //
    // And a squeeze sized to the intrusion OVER-corrects, which is the opposite of
    // how it looks. Differentiate the formula below: giving up S of height moves the
    // content's top edge down by S·k, and k ≥ 1/2 — MORE than S/2, and more still
    // once the content scales with the height. Subtracting the intrusion from the
    // rest height therefore leaves the band as short as HALF the height it actually
    // needed (h_old = k·h_new, and k ≥ 1/2), bottoming it out at its floor while its
    // content still had room, and tripping the fade. With bottom 1000, rest 100 and content 40 (k = 0.7), a column at 940
    // needs a height of 85.7; the subtraction gave 60, clearing the column by an
    // extra 18 it never had to spend.
    //
    // Solving both at once: with height h, scale h/restHeight and content height
    // contentH0*h/restHeight, the content's top edge sits at
    //     bottom - h*(1/2 + contentH0/(2*restHeight))
    // so requiring that to clear the column gives h directly. k is that bracket, at
    // least 1/2 and at most 1, so the division is always well conditioned.
    readonly property real lowerMidBarContentTopFactor: {
        // A band that PAINTS a background has no spare padding to give away: the
        // slab is drawn edge to edge, so the column has to clear the whole rect and
        // the factor is 1. Only the default "standard" style is transparent
        // (Theme.qml:901-905) — "surface" and "accentBar" both fill. Without this
        // the carousel would sit on a visible coloured block, which is the same
        // defect in miniature.
        if (idlePage.lowerMidBarFill.a > 0)
            return 1.0
        var rest = idlePage.lowerMidBarRestHeight
        if (rest <= 0) return 1.0
        // The clamp is defensive: restHeight is floored at implicitHeight, which is
        // floored at contentImplicitHeight, so content cannot exceed rest outside a
        // transient evaluation order. It is what pins k to [1/2, 1].
        return 0.5 + Math.min(rest, lmbZone.contentImplicitHeight) / (2 * rest)
    }

    // How far the centre column reaches, as the band sees it.
    //
    // Includes topPanelClearance because the two Translates do NOT fully cancel.
    // The column carries -bottomPanelClearance + topPanelClearance and the band
    // carries -bottomPanelClearance, so the bottom term cancels (deliberately: both
    // slide together) and the top term does not — an upper-half picker slides the
    // column DOWN while the band stays put. Without it the column is under-measured
    // by exactly that much while such a picker is open (and the band's height by that
    // over k, so up to twice it).
    readonly property real lowerMidBarColumnBottom:
        centerContent.y + centerContent.height + idlePage.topPanelClearance
        + Theme.spacingMedium

    // The TALLEST the band may be while its content still clears the column — a
    // ceiling, not a requirement. Larger than the rest height whenever the column is
    // nowhere near, which is the normal case and why it is then clamped away.
    //
    // Loop-free by construction, and the constraint is tighter than it looks. Every
    // input is independent of the band's RENDERED size:
    //   - the centre column never reads the band, and bottomBar is anchored to the
    //     page, so lowerMidBarColumnBottom and lowerMidBarBottom are both free of it;
    //   - lowerMidBarRestHeight comes from lmbZone.implicitHeight and the factor from
    //     lmbZone.contentImplicitHeight, and both are content-driven — the latter is
    //     itemsRow.implicitHeight times the user's zoneScale (LayoutBarZone.qml:48)
    //     and the former is that under a constant floor (:51), which adds no
    //     dependency. itemsRow anchors to its parent's verticalCenter/left/right
    //     (LayoutBarZone.qml:56-58), so it measures the parent's WIDTH but never its
    //     height;
    //   - topPanelClearance is the dangerous-looking one, and the danger is REAL now
    //     in a way it was not before this property was solved. lowerMidBarBottom used
    //     to be bottomBar.y plus the offset and carried no clearance term; it now
    //     solves from bottomBar.y, the zone offset, lowerMidBarColumnBottom (which
    //     ADDS topPanelClearance), lowerMidBarRestHeight and
    //     lowerMidBarContentTopFactor. So the path lowerMidBarBottom →
    //     _idleContentBottom → requestPanelClearance → topPanelClearance →
    //     lowerMidBarColumnBottom → lowerMidBarBottom now closes.
    //
    //     Two things break it, and both are worth knowing before touching
    //     requestPanelClearance. It is an imperative function writing a plain
    //     property, not a binding, so it cannot form a binding loop at all. And the
    //     branch that reads _idleContentBottom writes only bottomPanelClearance,
    //     which does not appear in lowerMidBarColumnBottom (it cancels against the
    //     band's own Translate); the branch that writes topPanelClearance reads
    //     _idleContentTop, which never touches the band. None of the five inputs
    //     carries a term in the band's RENDERED height.
    //
    // That last point is why the squeeze may NOT run through the zone's zoneScale,
    // however natural that looks — zoneScale is the other factor in both of those
    // expressions, so scaling the content there would put the band's size on both
    // sides of its own binding. It is applied as a render transform on the zone
    // instead: QQuickItem::setScale only marks BasicTransform dirty and emits no
    // geometry change (qtdeclarative/src/quick/items/qquickitem.cpp:6448-6460), so
    // anchors, width and height never see it.
    readonly property real lowerMidBarClearHeight: {
        if (!idlePage.lowerMidBarVisible) return idlePage.lowerMidBarRestHeight
        return (idlePage.lowerMidBarBottom - idlePage.lowerMidBarColumnBottom)
            / idlePage.lowerMidBarContentTopFactor
    }

    // NOT named "full height": LayoutPreview.qml carries a lowerMidFullHeight that
    // means the UN-squeezed height, i.e. this file's lowerMidBarRestHeight. Two
    // mirrored surfaces using one name for opposite quantities is how they drift.
    readonly property real lowerMidBarCurrentHeight: Math.max(
        idlePage.lowerMidBarMinHeight,
        Math.min(idlePage.lowerMidBarRestHeight, idlePage.lowerMidBarClearHeight))
    // Render scale for the band's contents, so a squeezed band shrinks what it holds
    // instead of clipping it. 1.0 whenever nothing is crowding the band.
    readonly property real lowerMidBarContentScale: idlePage.lowerMidBarRestHeight > 0
        ? idlePage.lowerMidBarCurrentHeight / idlePage.lowerMidBarRestHeight
        : 1.0
    // Deliberately the REST height, not the squeezed one, and that is a hard
    // requirement rather than a preference: lowerMidBarClearHeight reads
    // lowerMidBarVisible, so gating this on the squeezed height closes the cycle
    // fits → visible → clearHeight → currentHeight → fits and QML reports a binding
    // loop with the values latching non-deterministically.
    //
    // It is also the right semantics. Whether the band exists at all is a property
    // of the viewport, not of what the user happens to have open.
    readonly property bool lowerMidBarFits:
        (idlePage.height - Theme.statusBarHeight - Theme.bottomBarHeight - lowerMidBarRestHeight) >= Theme.scaled(220)
    readonly property bool lowerMidBarVisible: lowerMidBarHasItems && lowerMidBarFits

    Rectangle {
        id: lowerMidBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        // Negative offset (the editor's "up") lifts the band off the bottom bar.
        // Follows the SOLVED bottom edge (lowerMidBarBottom), which equals
        // bottomBar.y + yOffset at rest and descends toward the bar only when the
        // band would otherwise have to shrink or hide.
        anchors.bottomMargin: bottomBar.y - idlePage.lowerMidBarBottom
        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
        }
        visible: idlePage.lowerMidBarVisible
        height: visible ? idlePage.lowerMidBarCurrentHeight : 0
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        color: idlePage.lowerMidBarFill
        // Fade out (rather than overlap the bottom action bar) when the center-zone
        // carousel expands down into the band; `enabled:false` also stops the hidden
        // band from swallowing taps meant for the bottom bar underneath.
        opacity: idlePage.carouselOverlapsBand ? 0 : 1
        enabled: !idlePage.carouselOverlapsBand
        // A fully transparent item IS still traversable by TalkBack/VoiceOver, so
        // the faded-out band must be neutralised for screen readers too.
        // `enabled: false` above is what actually does that: enabled propagates
        // down, so every widget in the band reports the disabled state.
        //
        // The Accessible.ignored below does NOT hide the band's contents, despite
        // how it reads. Qt's bridge FLATTENS an ignored node rather than pruning
        // its subtree — unignoredChildren() in qaccessiblequickitem.cpp recurses
        // through it and promotes any descendant carrying its own role (every
        // AccessibleTapHandler/AccessibleButton in the band does) up past it. It
        // only drops this Rectangle's own node, which, having no role or name, is
        // barely a node to begin with. Kept because it is harmless and states the
        // intent; do not rely on it alone to hide anything.
        //
        // `visible: false` is the only construct that genuinely removes a subtree
        // from the accessibility tree. It is not used here because it would collapse
        // the band's painted area outright, where the fade keeps the band's place
        // while it is unreachable. Note it would NOT disturb the rest of the page:
        // nothing references this Rectangle by id at all — the squeeze and the fade
        // are both computed from idlePage properties — and the band's height already
        // varies by design (lowerMidBarCurrentHeight) with nothing above it moving.
        Accessible.ignored: idlePage.carouselOverlapsBand
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        // Slides UP with the center content to clear a bottom-zone picker popup.
        transform: Translate {
            y: -idlePage.bottomPanelClearance
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        }

        // Laid out at the band's REST height and scaled down to whatever height the
        // band currently has, so scale alone carries the squeeze.
        //
        // Deliberately not anchors.fill, and the reason is double application rather
        // than any feedback: filling would already size the zone to the squeezed
        // height, itemsRow would centre its natural-size content in that, and the
        // scale below would then shrink it a second time. (It would be harmless to
        // implicitHeight, which never measures this item — see lowerMidBarClearHeight.)
        LayoutBarZone {
            id: lmbZone
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            height: idlePage.lowerMidBarRestHeight
            scale: idlePage.lowerMidBarContentScale
            // Shrinks towards the band's own bottom edge, so within a squeeze the top
            // edge is what moves. That edge is itself solved and descends during a
            // slide, so the contents stay put relative to the BAND, not to the bar.
            //
            // A uniform scale shrinks x as well as y, so the origin is pinned to the
            // zone's ALIGNMENT edge — the same rule, and the same reason, as
            // LayoutBarZone.qml:63-65 applies to its own zoneScale. With plain
            // Item.Bottom a left- or right-aligned band's contents would slide inward
            // by (1 - scale) * width / 2 as it squeezed.
            transformOrigin: {
                var a = idlePage.lowerMidBarOptions.alignment || "center"
                if (a === "left") return Item.BottomLeft
                if (a === "right") return Item.BottomRight
                return Item.Bottom
            }
            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
            zoneName: "lowerMidBar"
            items: idlePage.lowerMidBarItems
            distribution: idlePage.lowerMidBarOptions.distribution || "packed"
            alignment: idlePage.lowerMidBarOptions.alignment || "center"
            zoneStyle: idlePage.lowerMidBarStyle
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
        // When the glass chrome is on, use the same neutral surface
        // scrim as StatusBar and the shared BottomBar so every bar reads
        // consistently and the wallpaper shows through; otherwise keep the
        // standard bottom-bar hue.
        color: Theme.glassChrome
               ? Theme.chromeFill(Theme.surfaceColor)
               : Theme.bottomBarColor
        // opacity < 1 forces the scrim through the alpha pass; without it this
        // bar renders opaque and the wallpaper can't show through. See
        // docs/CLAUDE_MD/QML_GOTCHAS.md "Translucent element renders opaque".
        opacity: Theme.glassChrome ? 0.99 : 1.0

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
