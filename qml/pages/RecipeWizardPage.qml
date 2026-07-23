import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Recipe wizard (add-recipe-wizard-tea): ONE page for all recipe creation and
// editing, replacing RecipeComposerPage. Three entry points land here — blank
// (RecipesPage "Add"), prefilled from a shot (promote buttons, via
// `promoteShotId`), and clone (RecipesPage clone action, via `prefill`).
//
// Creation walks steps: drink type → bean → profile → details → summary.
// Picker steps auto-advance on tap (no Next); breadcrumb chips navigate back.
// The SUMMARY IS THE EDIT PAGE: edit/clone/promote land directly on it with
// everything loaded, and tapping a row reopens just that step. Templates (the
// `templates` table below) set defaults per drink type but never restrict —
// the summary offers add/remove for the milk and hot-water blocks, so any
// block combination the recipe model allows stays expressible.
//
// Prefill priority on the details step (never over a user edit): most recent
// shot with the chosen bean+profile pair → (tea) the bag's structured brewing
// data → the profile's own recommended numbers. Tea default temps and the
// profile type-match table live in C++ (src/core/drinktypes.h, exposed via
// ProfileManager) — the single source shared with the ranking helpers.
Page {
    id: wizardPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: mode === "edit" ? TranslationManager.translate("recipes.wizard.editTitle", "Edit Recipe") : TranslationManager.translate("recipes.wizard.createTitle", "New Recipe")

    objectName: "recipeWizardPage"
    background: ThemedPageBackground {}

    // "create" | "edit". Edit loads the row; create starts from `prefill`
    // (possibly empty) or `promoteShotId`. Same contract as the old composer.
    property string mode: "create"
    property int editRecipeId: -1
    property var prefill: ({})
    property real promoteShotId: 0

    // Intercept the Android system back button / Escape key (Main.qml pops
    // the page directly otherwise): step back like the bottom-bar arrow, so
    // leaving the wizard always funnels through the unsaved-changes guard.
    focus: true
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true
            goBackOneStep()
        }
    }

    // --- step machine ----------------------------------------------------
    // "drink" | "bean" | "profile" | "details" | "summary". Creation walks
    // them in order; edit/clone/promote start at "summary". A step opened
    // FROM the summary returns to it on selection instead of advancing.
    property string currentStep: "drink"
    property bool _fromSummary: false
    // True when the wizard OPENED on the summary (edit / promote / clone):
    // back from the summary then exits; in a creation walk it steps back
    // to details instead.
    property bool _enteredAtSummary: false
    // The details step is walked as a sequence of one-facet WINDOWS, the same
    // screens whether creating (walk forward) or editing (a summary card jumps
    // straight to its window). `_detailsPage` names the current window:
    // "equipment" → "numbers" (dose/yield/temp/grind) → "steam"/"water" (only
    // the blocks the drink has). Equipment comes first because the grinder's
    // rpm capability gates the rpm field on the numbers window.
    property string _detailsPage: "equipment"

    // The ordered details windows for the current drink — equipment and the
    // numbers always; steam/water only when the recipe carries that block.
    function detailsPages() {
        var pages = ["equipment", "numbers"]
        if (fHasMilk) pages.push("steam")
        if (fHasWater) pages.push("water")
        return pages
    }
    readonly property bool isLastDetailsPage: {
        var pages = detailsPages()
        return pages.indexOf(_detailsPage) >= pages.length - 1
    }
    // Forward through the details windows (creation walk); past the last one
    // lands on the summary.
    function detailsAdvance() {
        var pages = detailsPages()
        var i = pages.indexOf(_detailsPage)
        if (i < 0 || i >= pages.length - 1) {
            _fromSummary = false
            currentStep = "summary"
        } else {
            _detailsPage = pages[i + 1]
        }
    }
    // "Move on" from a details window: a window opened FROM the summary returns
    // there; in the creation walk it steps to the next window. Backs both the
    // Continue button and the equipment tiles' auto-advance on selection.
    function detailsContinue() {
        if (_fromSummary) {
            _fromSummary = false
            currentStep = "summary"
        } else {
            detailsAdvance()
        }
    }

    function openStep(step) {
        _fromSummary = (currentStep === "summary")
        _enterStep(step)
    }
    function stepDone(nextStep) {
        if (_fromSummary) {
            _fromSummary = false
            _enterStep("summary")
        } else {
            _enterStep(nextStep)
        }
    }
    function _enterStep(step) {
        currentStep = step
        if (step === "bean")
            MainController.bagStorage.requestInventory()
        else if (step === "profile")
            requestProfileRanking()
    }

    // The bottom-bar back arrow steps BACK through the wizard; it exits only
    // from the first step (or from the summary when the wizard opened there —
    // edit/promote/clone). A step opened from the summary returns to it.
    function goBackOneStep() {
        if (_fromSummary) {
            _fromSummary = false
            _enterStep("summary")
            return
        }
        switch (currentStep) {
        case "drink":
            requestExit()
            break
        case "bean":
            _enterStep("drink")
            break
        case "profile":
            _enterStep("bean")
            break
        case "details": {
            // Step back through the details windows; from the first one, back
            // out to the profile step (bean, for hot-water tea with no profile).
            var pages = detailsPages()
            var i = pages.indexOf(_detailsPage)
            if (i > 0) {
                _detailsPage = pages[i - 1]
            } else if (isHotWaterTea) {
                // "Just hot water" was picked ON the profile step — return
                // there with the tea profile list restored (and undo the
                // row's hot-water pre-seed; picking it again re-applies).
                fDrinkType = "tea"
                fHasWater = false
                _enterStep("profile")
            } else {
                _enterStep("profile")
            }
            break
        }
        case "summary":
            if (_enteredAtSummary)
                requestExit()
            else {
                // Back into the creation walk: re-enter at the last details
                // window so Continue-ing returns straight to the summary.
                _detailsPage = detailsPages().slice(-1)[0]
                _enterStep("details")
            }
            break
        default:
            requestExit()
        }
    }

    // --- drink-type templates ---------------------------------------------
    // Per-type wizard configuration. Filter sets follow the profile JSON
    // beverage_type vocabulary (empty/missing = espresso — see
    // ProfileManager::currentProfileBeverageType). Blocks listed under
    // `preseed` are switched on when the type is picked; `fields` gates the
    // details step. Tea default temps come from ProfileManager.defaultTeaTempC.
    readonly property var templates: ({
        espresso:     { beverages: ["espresso", ""], bagKind: "coffee",
                        milk: false, water: false, grind: true, isTea: false },
        filter:       { beverages: ["filter", "pourover"], bagKind: "coffee",
                        milk: false, water: false, grind: true, isTea: false },
        americano:    { beverages: ["espresso", ""], bagKind: "coffee",
                        milk: false, water: true, waterOrder: "after", grind: true, isTea: false },
        long_black:   { beverages: ["espresso", ""], bagKind: "coffee",
                        milk: false, water: true, waterOrder: "before", grind: true, isTea: false },
        latte:        { beverages: ["espresso", ""], bagKind: "coffee",
                        milk: true, water: false, grind: true, isTea: false },
        tea:          { beverages: ["tea_portafilter"], bagKind: "tea",
                        milk: false, water: false, grind: false, isTea: true },
        tea_hotwater: { beverages: [], bagKind: "tea",
                        milk: false, water: true, waterOrder: "after", grind: false, isTea: true }
    })
    readonly property var activeTemplate: templates[fDrinkType] || templates.espresso
    readonly property bool isTeaDrink: activeTemplate.isTea === true
    readonly property bool isHotWaterTea: fDrinkType === "tea_hotwater"

    // Labels/icons live in the DrinkType singleton (shared with the cards,
    // pills, and auto-naming). The wizard's own picker keeps the LONG forms
    // ("Latte / Cappuccino"); everything downstream uses the short ones.
    function drinkTypeLabel(t) { return DrinkType.longLabel(t) }
    function drinkTypeIcon(t) { return DrinkType.icon(t) }

    // --- form state (same vocabulary as the old composer) ------------------
    property string fDrinkType: ""
    property string fProfileTitle: ""
    property string fProfileJson: ""
    property real fProfileTempC: 0
    property real fTempDeltaC: 0
    // The stored offset as loaded, preserved verbatim when the profile's own
    // temperature can't be resolved (uninstalled profile): the tea save path
    // needs the profile temp to convert its absolute UI back to an offset,
    // and without it the loaded offset must ride through untouched.
    property real fLoadedTempOffsetC: 0
    property bool _submitting: false
    // The hard bag link (recipes-bag-links-ui-polish): the SPECIFIC bag this
    // recipe is made with — selection on the bag step links exactly that
    // bag; two open bags of one bean are distinct choices. 0 = no bag.
    property real fBagId: 0
    property string fBeanBaseId: ""
    property string fRoaster: ""
    property string fCoffee: ""
    property string fBagBlob: ""          // selected bag's beanBaseData (tea brewing seeds)
    property real fEquipmentId: 0
    property string fEquipmentName: ""
    // Grinder identity of the recipe's selected package — the grind control's
    // context (grind-value-entry: the recipe's grinder, never the active one).
    property string fEquipmentGrinderBrand: ""
    property string fEquipmentGrinderModel: ""
    // RPM capability: one function, the RECIPE's package as arguments — not the
    // stored pkg.rpmCapable flag (which can drift from the catalog) and not the
    // argument-less active-grinder resolution (which would ignore the package
    // the equipment window just made the user choose).
    readonly property bool fEquipmentRpmCapable:
        Settings.dye.grinderRpmCapable(fEquipmentGrinderBrand, fEquipmentGrinderModel)
    // The recipe's own dial-in (grind always lives on the recipe —
    // fix-recipe-grind-integrity). 0 rpm = unset, uniform across surfaces.
    property string fGrind: ""
    property int fRpmPinned: 0
    // The linked bag's current dial, read once when the bag is selected and
    // offered as the grind/rpm fields' editable DEFAULT — grind always lives
    // on the recipe (fix-recipe-grind-integrity); there is no live follow.
    property string fBagGrindDefault: ""
    property real fBagRpmDefault: 0
    property bool fHasMilk: false
    property real fMilkWeightG: 0
    property string fPitcherName: ""
    property int fPitcherDurationSec: 0
    property int fPitcherFlow: 0
    property real fPitcherTemperatureC: 0
    property bool fHasWater: false
    property string fVesselName: ""
    property int fVesselVolume: 0
    property string fVesselMode: "weight"
    property int fVesselFlowRate: 40
    property real fVesselTemperatureC: 0
    property string fWaterOrder: "after"
    property string errorMessage: ""
    property string _autoName: ""

    // Lowercased names of existing non-archived recipes (excluding the one being
    // edited). TWO consumers: suggestName() uses it to disambiguate an auto-name
    // collision, and `nameInUse` below uses it to GATE SAVE
    // (block-duplicate-active-names) — so this is no longer cosmetic-only. Filled
    // asynchronously by requestInventory() → onInventoryReady; empty until it
    // lands, in which case the collision qualifier degrades to nothing (the plain
    // descriptive name ships, never worse than before) and the save gate reads as
    // "no collision", leaving the storage guard to refuse a genuine duplicate.
    property var _existingRecipeNames: []

    // Yield anchor (add-yield-ratio-anchor): which of {yield, ratio} was last
    // written — "none" | "absolute" | "ratio". The non-anchored field shows
    // the derived value, dimmed, never blank; only the anchor is stored.
    property string fYieldMode: "none"

    // Re-derive the non-anchored yield field through the dose. Called on any
    // dose/yield/ratio edit and after seeding.
    function syncDerivedYieldFields() {
        var d = parseFloat(doseField.text) || 0
        if (fYieldMode === "ratio") {
            var r = parseFloat(ratioField.text) || 0
            yieldField.text = (d > 0 && r > 0) ? (d * r).toFixed(1) : ""
        } else if (fYieldMode === "absolute") {
            var y = parseFloat(yieldField.text) || 0
            ratioField.text = (d > 0 && y > 0) ? (y / d).toFixed(1) : ""
        } else {
            // Unanchored: both empty unless one gets typed into.
        }
    }

    readonly property bool hasBean: fBeanBaseId !== "" || fRoaster !== "" || fCoffee !== ""
    // The name this recipe arrived with (edit mode only; empty when creating).
    property string _originalName: ""
    // Cause of the last refused save, from recipeUpdateFailed.
    property string _saveFailReason: ""

    // Active-name uniqueness (block-duplicate-active-names): the entered name may
    // not match another non-archived recipe. Relies on `_existingRecipeNames`,
    // which onInventoryReady fills with the lowercased non-archived names minus
    // the recipe being edited; it is empty until that request lands, so this reads
    // as "no collision" until then and the storage guard is the real backstop.
    //
    // Re-saving the name a recipe already has is never a collision — otherwise a
    // recipe that already shared a name with another could not be edited at all.
    readonly property bool nameInUse: {
        var n = nameField.text.trim().toLowerCase()
        if (n.length === 0) return false
        if (n === _originalName.trim().toLowerCase()) return false
        return _existingRecipeNames.indexOf(n) >= 0
    }
    readonly property bool canSave: MainController.recipeStorage.isSaveValid(
        nameField.text, fProfileTitle, buildHotWaterJson()) && !nameInUse

    // What the summary hero renders: the wizard state shaped exactly like a
    // stored recipe map, so the shared RecipeDrinkCard shows the card the
    // management page will show after save (WYSIWYG). An object-literal
    // binding — re-evaluates as the fields change.
    readonly property var previewMap: ({
        name: nameField.text,
        drinkType: fDrinkType,
        profileTitle: fProfileTitle,
        bagId: fBagId,
        beanBaseId: fBeanBaseId,
        roasterName: fRoaster,
        coffeeName: fCoffee,
        doseG: parseFloat(doseField.text) || 0,
        yieldValue: fYieldMode === "ratio"
            ? ((parseFloat(ratioField.text) || 0) > 0
               ? Math.max(0.5, Math.min(6.0, parseFloat(ratioField.text))) : 0)
            : (parseFloat(yieldField.text) || 0),
        yieldMode: (fYieldMode === "ratio" && (parseFloat(ratioField.text) || 0) > 0) ? "ratio"
                 : (fYieldMode === "absolute" && (parseFloat(yieldField.text) || 0) > 0) ? "absolute"
                 : "none",
        tempOffsetC: isTeaDrink
            ? (fProfileTempC > 0
                ? (fTeaTempC > 0 && Math.abs(fTeaTempC - fProfileTempC) > 0.05
                    ? fTeaTempC - fProfileTempC : 0)
                : fLoadedTempOffsetC)
            : (Math.abs(fTempDeltaC) > 0.05 ? fTempDeltaC : 0),
        grindPinned: (!activeTemplate.grind) ? "" : fGrind.trim(),
        steamJson: buildSteamJson(),
        hotWaterJson: buildHotWaterJson()
    })

    // Auto-suggested name ("<Bean> <DrinkType> · <Profile>"): applied while the
    // field is empty or still holds the previous suggestion — never over a
    // user edit. SHORT type labels only ("Gran Bar Latte", never "… Latte /
    // Cappuccino"), and the type word is skipped when the bean name already
    // ends with it ("Milk Blend Espresso", not "Milk Blend Espresso Espresso").
    // The profile is included from the first recipe (issue #1548: one bean made
    // many ways needs the profile to be distinguishable AND searchable — users
    // hunt by profile, e.g. "Crem Yir" = Cremina Yirgacheffe). The profile is
    // cleaned first (editor prefix stripped, type-word stutter removed).
    function suggestName() {
        var bean = (fCoffee !== "" ? fCoffee : fRoaster).trim()
        var typeWord = fDrinkType !== "" ? DrinkType.shortLabel(fDrinkType) : ""
        var parts = []
        if (bean !== "") parts.push(bean)
        if (typeWord !== "") {
            var stutter = bean !== ""
                && bean.toLowerCase().endsWith(" " + typeWord.toLowerCase())
            if (bean.toLowerCase() === typeWord.toLowerCase())
                stutter = true
            if (!stutter) parts.push(typeWord)
        }
        var base = parts.join(" ")
        // Append the cleaned profile (a hot-water tea recipe carries none).
        var profile = cleanProfileForName(fProfileTitle, typeWord)
        if (profile !== "")
            base = base === "" ? profile : (base + " · " + profile)

        if (base === "")
            return

        // When the composed name matches an existing recipe's display name
        // (a plain name-string match — we cache names, not their identity),
        // disambiguate by appending the DRAFT's own dial-in value: yield tried
        // first, else dose. Never a bare counter. Best-effort: with the
        // inventory not yet loaded (or no usable value) the plain `base` ships.
        var suggestion = base
        if (nameCollides(base)) {
            var yq = yieldQualifierText()
            var dq = doseQualifierText()
            if (yq !== "" && !nameCollides(base + " " + yq))
                suggestion = base + " " + yq
            else if (dq !== "" && !nameCollides(base + " " + dq))
                suggestion = base + " " + dq
            else if (yq !== "")
                suggestion = base + " " + yq       // best effort — still shown
            else if (dq !== "")
                suggestion = base + " " + dq
        }

        if (suggestion === "" || (nameField.text !== "" && nameField.text !== _autoName))
            return
        nameField.text = suggestion
        _autoName = suggestion
    }

    // Turn a profile title into the token used in a suggested recipe name:
    // strip the D-Flow/ or A-Flow/ editor-membership prefix (see the project
    // note "editor membership = title prefix"), then drop a trailing word that
    // just repeats the drink-type word (the bean stutter rule, for the
    // profile). Returns "" when no profile or nothing survives.
    function cleanProfileForName(title, typeWord) {
        var p = (title || "").trim()
        if (p === "")
            return ""
        var lower = p.toLowerCase()
        if (lower.indexOf("d-flow/") === 0 || lower.indexOf("a-flow/") === 0)
            p = p.substring(p.indexOf("/") + 1).trim()
        if (typeWord && typeWord !== "") {
            var lp = p.toLowerCase(), lt = typeWord.toLowerCase()
            if (lp === lt)
                return ""
            if (lp.endsWith(" " + lt))
                p = p.substring(0, p.length - lt.length - 1).trim()
        }
        return p
    }

    // True when an existing non-archived recipe already carries this exact name
    // (case-insensitive). The set excludes the recipe being edited.
    function nameCollides(candidate) {
        var c = (candidate || "").trim().toLowerCase()
        if (c === "")
            return false
        for (var i = 0; i < _existingRecipeNames.length; ++i)
            if (_existingRecipeNames[i] === c)
                return true
        return false
    }

    // Collision qualifiers from the current dial-in state. Ratio → "1:2.5",
    // absolute yield / dose → "40g" (trailing ".0" trimmed).
    function yieldQualifierText() {
        if (fYieldMode === "ratio") {
            var r = parseFloat(ratioField.text) || 0
            return r > 0 ? "1:" + trimNumForName(r) : ""
        }
        if (fYieldMode === "absolute") {
            var y = parseFloat(yieldField.text) || 0
            return y > 0 ? trimNumForName(y) + "g" : ""
        }
        return ""
    }
    function doseQualifierText() {
        var d = parseFloat(doseField.text) || 0
        return d > 0 ? trimNumForName(d) + "g" : ""
    }
    function trimNumForName(v) {
        return Number(v).toFixed(1).replace(/\.0$/, "")
    }


    Component.onCompleted: {
        if (mode === "edit" && editRecipeId > 0) {
            currentStep = "summary"
            _enteredAtSummary = true
            MainController.recipeStorage.requestRecipe(editRecipeId)
        } else if (promoteShotId > 0) {
            currentStep = "summary"
            _enteredAtSummary = true
            MainController.shotHistory.requestShot(promoteShotId)
        } else if (prefill && Object.keys(prefill).length > 0) {
            currentStep = "summary"
            _enteredAtSummary = true
            applyRecipeMap(prefill)
            nameField.forceActiveFocus()
            nameField.selectAll()
            captureBaseline()
        } else {
            currentStep = "drink"
            captureBaseline()
        }
        // EVERY entry path needs the existing-name set, not just the blank-create
        // walk: besides feeding suggestName()'s auto-name disambiguation, it now
        // backs the duplicate-name gate on Save (block-duplicate-active-names).
        // Requesting it only for the blank walk left `nameInUse` permanently false
        // in edit/promote/clone — i.e. inert in edit mode, which is exactly where a
        // rename collision happens.
        //
        // Issued LAST, after each path's own load above, deliberately: runAsync
        // dispatches to a single FIFO SerialDbWorker, so putting this heavy
        // full-table scan first would delay the recipe/shot the user is waiting to
        // see. The gate reads as "no collision" until the scan lands, and the
        // storage guard is the real backstop either way.
        MainController.recipeStorage.requestInventory()
    }

    // --- ported state <-> JSON helpers (verbatim composer semantics) -------

    function applyRecipeMap(r) {
        nameField.text = r.name || ""
        // Only in EDIT mode does the recipe have a name it already owns. Recording
        // it lets the duplicate gate fire on an actual rename only, so a recipe
        // that already shares a name with another stays editable
        // (block-duplicate-active-names). A clone/promote is a new recipe, so any
        // collision there IS new and must block.
        if (mode === "edit" && editRecipeId > 0)
            _originalName = nameField.text
        fProfileTitle = r.profileTitle || ""
        fProfileJson = r.profileJson || ""
        fBagId = r.bagId || 0
        fBeanBaseId = r.beanBaseId || ""
        fRoaster = r.roasterName || ""
        fCoffee = r.coffeeName || ""
        fEquipmentId = r.equipmentId || 0
        doseField.text = r.doseG > 0 ? Number(r.doseG).toFixed(1) : ""
        // Yield spec: the anchored field gets the stored value; the other
        // derives through the dose (dimmed, never blank). A legacy prefill
        // map still carrying yieldG seeds an absolute.
        fYieldMode = ((r.yieldMode === "ratio" || r.yieldMode === "absolute")
                      && (r.yieldValue || 0) > 0) ? r.yieldMode
                   : ((r.yieldG || 0) > 0 ? "absolute" : "none")
        if (fYieldMode === "ratio") {
            ratioField.text = Number(r.yieldValue).toFixed(1)
            yieldField.text = ""
        } else if (fYieldMode === "absolute") {
            yieldField.text = Number((r.yieldValue || 0) > 0 ? r.yieldValue : r.yieldG).toFixed(1)
            ratioField.text = ""
        } else {
            yieldField.text = ""
            ratioField.text = ""
        }
        syncDerivedYieldFields()
        fLoadedTempOffsetC = r.tempOffsetC || 0
        refreshProfileTemp()
        // The stored offset loads verbatim — no open-time subtraction against
        // the profile temp, so a profile temperature edit can never manufacture
        // a phantom offset here (recipe-relative-temp-offset).
        fTempDeltaC = r.tempOffsetC || 0
        // Tea EDITS its brew temperature ABSOLUTE (fTeaTempC) but stores the
        // same offset — seed the absolute as profileTemp + offset here or an
        // edit/clone/promote (which all load through this function and open on
        // the summary, never through applyDetailsPrefill) would save 0 and
        // silently discard the stored temperature. Unresolvable profile → 0;
        // the save path then preserves fLoadedTempOffsetC instead.
        fTeaTempC = (r.drinkType && String(r.drinkType).indexOf("tea") === 0
                     && fProfileTempC > 0) ? fProfileTempC + (r.tempOffsetC || 0) : 0
        fGrind = r.grindPinned || ""
        fRpmPinned = r.rpmPinned || 0
        applySteamJson(r.steamJson || "")
        applyHotWaterJson(r.hotWaterJson || "")
        // Drink type: stored value, else derive from the loaded blocks — a
        // legacy (pre-migration-28) recipe opens under the right template.
        fDrinkType = r.drinkType || deriveDrinkType()
        // Loading an existing recipe counts as "already filled" — a later async
        // history reply must not overwrite the saved numbers.
        _detailsUserEdited = true
        _numbersSource = "saved"
        if (fEquipmentId > 0)
            MainController.equipmentStorage.requestInventory()
        refreshBagDetails()
    }

    // QML-side mirror of Recipe::deriveDrinkType (blocks + the selected
    // profile's beverage_type, resolvable here because ProfileManager is).
    function deriveDrinkType() {
        var bev = ""
        if (fProfileTitle !== "") {
            var fn = ProfileManager.findProfileByTitle(fProfileTitle)
            if (fn && fn !== "")
                bev = String(ProfileManager.getProfileByFilename(fn).beverage_type || "").toLowerCase()
        }
        if (fProfileTitle === "" && fHasWater) return "tea_hotwater"
        if (bev === "tea_portafilter") return "tea"
        if (fHasMilk) return "latte"
        if (fHasWater) return fWaterOrder === "before" ? "long_black" : "americano"
        if (bev === "filter" || bev === "pourover") return "filter"
        return "espresso"
    }

    function applySteamJson(json) {
        fHasMilk = false; fMilkWeightG = 0
        fPitcherName = ""; fPitcherDurationSec = 0; fPitcherFlow = 0; fPitcherTemperatureC = 0
        if (!json || json === "")
            return
        try {
            var s = JSON.parse(json)
            fHasMilk = !!s.hasMilk
            fMilkWeightG = s.milkWeightG || 0
            fPitcherName = s.pitcherName || ""
            fPitcherDurationSec = s.durationSec || 0
            fPitcherFlow = s.flow || 0
            fPitcherTemperatureC = s.temperatureC || 0
        } catch (e) {
            console.warn("RecipeWizard: bad steam JSON:", e)
        }
    }

    function buildSteamJson() {
        if (!fHasMilk && fMilkWeightG <= 0 && fPitcherName === "")
            return ""
        var s = { hasMilk: fHasMilk }
        if (fMilkWeightG > 0) s.milkWeightG = fMilkWeightG
        if (fPitcherName !== "") {
            s.pitcherName = fPitcherName
            s.durationSec = fPitcherDurationSec
            s.flow = fPitcherFlow
            s.temperatureC = fPitcherTemperatureC
        }
        return JSON.stringify(s)
    }

    function applyHotWaterJson(json) {
        fHasWater = false; fVesselName = ""; fVesselVolume = 0
        fVesselMode = "weight"; fVesselFlowRate = 40; fVesselTemperatureC = 0
        fWaterOrder = "after"
        if (!json || json === "")
            return
        try {
            var w = JSON.parse(json)
            fHasWater = !!w.hasWater
            fVesselName = w.vesselName || ""
            fVesselVolume = w.volume || 0
            fVesselMode = w.mode || "weight"
            fVesselFlowRate = w.flowRate || 40
            fVesselTemperatureC = w.temperatureC || 0
            fWaterOrder = w.order === "before" ? "before" : "after"
        } catch (e) {
            console.warn("RecipeWizard: bad hot water JSON:", e)
        }
    }

    function buildHotWaterJson() {
        if (!fHasWater && fVesselName === "")
            return ""
        var w = { hasWater: fHasWater, order: fWaterOrder }
        if (fVesselName !== "") {
            w.vesselName = fVesselName
            w.volume = fVesselVolume
            w.mode = fVesselMode
            w.flowRate = fVesselFlowRate
            w.temperatureC = fVesselTemperatureC
        }
        return JSON.stringify(w)
    }

    // Build the current steam settings as a snapshot (promote fallback when
    // the shot predates steam snapshots). Mirrors currentSteamSpecJson().
    function currentSteamSnapshot() {
        var s = {}
        var idx = Settings.brew.selectedSteamPitcher
        var presets = Settings.brew.steamPitcherPresets
        if (idx >= 0 && idx < presets.length && !presets[idx].disabled) {
            s.pitcherName = presets[idx].name || ""
            s.durationSec = presets[idx].duration || 0
            s.flow = presets[idx].flow || 0
            s.temperatureC = presets[idx].temperature || 0
        }
        if (Settings.brew.lastSteamMilkG > 0)
            s.milkWeightG = Settings.brew.lastSteamMilkG
        return Object.keys(s).length > 0 ? JSON.stringify(s) : ""
    }

    function prefillFromShot(shot) {
        var beanBaseId = ""
        if (shot.beanBaseJson) {
            try {
                beanBaseId = JSON.parse(shot.beanBaseJson).id || ""
            } catch (e) {
                console.warn("RecipeWizard: bad beanBaseJson on promoted shot:", e)
            }
        }
        // Route through `prefill` so save() picks up the provenance fields.
        prefill = ({
            name: "",
            profileTitle: shot.profileName || "",
            profileJson: shot.profileJson || "",
            // The shot's own bag becomes the recipe's hard bag link (a
            // pre-bag shot carries no link; the bean identity still does).
            bagId: shot.bagId > 0 ? shot.bagId : 0,
            beanBaseId: beanBaseId,
            roasterName: shot.beanBrand || "",
            coffeeName: shot.beanType || "",
            equipmentId: shot.equipmentId || 0,
            doseG: shot.doseWeightG || 0,
            // Promotion copies the shot's recorded ANCHOR (add-yield-ratio-
            // anchor): a 1:2 shot promotes to a 1:2 recipe; a legacy shot
            // (no anchor emitted) promotes its recorded target as absolute —
            // never a ratio reconstructed from target ÷ dose (the dose is
            // post-shot editable). Mirrors RecipePromotion::fieldsFromShotRecord.
            yieldValue: (shot.yieldMode === "ratio" || shot.yieldMode === "absolute")
                ? (shot.yieldAnchorValue || 0) : (shot.targetWeightG || 0),
            yieldMode: (shot.yieldMode === "ratio" || shot.yieldMode === "absolute")
                ? shot.yieldMode : ((shot.targetWeightG || 0) > 0 ? "absolute" : "none"),
            // The shot's temperature override is a frozen ABSOLUTE; converted
            // to the recipe's offset below, once applyRecipeMap has resolved
            // the shot's profile temperature.
            tempOffsetC: 0,
            // The shot's own recorded dial — the exact grind that produced the
            // shot being promoted — is the recipe's default (grind lives on
            // the recipe; there is no inherit-from-bag encoding to fall back
            // to). Editable on the summary before saving.
            grindPinned: shot.grinderSetting || "",
            rpmPinned: shot.rpm || 0,
            steamJson: shot.steamJson && shot.steamJson !== "" ? shot.steamJson
                                                               : currentSteamSnapshot(),
            hotWaterJson: shot.hotWaterJson || "",
            createdFromShotId: promoteShotId
        })
        applyRecipeMap(prefill)
        // Promote-from-shot conversion (recipe-relative-temp-offset): offset =
        // the shot's absolute override − the SHOT's profile snapshot
        // temperature — the profile as it was when the shot was pulled, which
        // is what the override was relative to. Same anchor as the C++
        // promote path (RecipePromotion::fieldsFromShotRecord), so the two
        // surfaces cannot disagree; the installed profile's current
        // temperature is only the fallback for a snapshot-less shot.
        // Unresolvable both ways → no pin.
        if ((shot.temperatureOverrideC || 0) > 0) {
            var promoteAnchor = 0
            if (shot.profileJson && String(shot.profileJson).length > 0) {
                try {
                    promoteAnchor = Number(JSON.parse(shot.profileJson).espresso_temperature) || 0
                } catch (e) {
                    console.warn("RecipeWizard: shot profile snapshot JSON unparsable:", e)
                }
            }
            if (promoteAnchor <= 0)
                promoteAnchor = fProfileTempC
            if (promoteAnchor > 0) {
                var promoteOffset = shot.temperatureOverrideC - promoteAnchor
                if (Math.abs(promoteOffset) < 0.05)
                    promoteOffset = 0
                if (isTeaDrink)
                    fTeaTempC = shot.temperatureOverrideC
                else
                    fTempDeltaC = promoteOffset
            }
        }
        var bean = ((shot.beanBrand || "") + " " + (shot.beanType || "")).trim()
        nameField.text = bean !== "" ? bean : (shot.profileName || "")
        nameField.selectAll()
        captureBaseline()
    }

    // Resolve the selected profile's base temperature (for the offset
    // control) and target yield (for the summary hero's plan line).
    property real fProfileYieldG: 0
    // The selected profile's frame temperatures, for the summary hero's plan
    // line — the card renders ITS profile's temps, never the loaded one
    // (recipe-relative-temp-offset).
    property var fProfileStepTemps: []
    function refreshProfileTemp() {
        fProfileTempC = 0
        fProfileYieldG = 0
        fProfileStepTemps = []
        if (fProfileTitle === "")
            return
        var d = null
        var fn = ProfileManager.findProfileByTitle(fProfileTitle)
        if (fn && fn !== "") {
            d = ProfileManager.getProfileByFilename(fn)
        } else if (fProfileJson !== "") {
            // Embedded fallback for a renamed/uninstalled profile — the same
            // ladder the recipe cards use.
            try { d = JSON.parse(fProfileJson) } catch (e) {
                console.warn("RecipeWizard: embedded profile JSON unparsable:", e)
                d = null
            }
        }
        if (!d)
            return
        fProfileTempC = Number(d.espresso_temperature) || 0
        fProfileYieldG = Number(d.target_weight) || 0
        var temps = []
        var steps = d.steps || []
        for (var i = 0; i < steps.length; ++i) {
            var stTemp = steps[i] ? Number(steps[i].temperature) : 0
            if (stTemp > 0)
                temps.push(stTemp)
        }
        // See RecipesPage.refreshProfileNumbers: a resolved profile whose
        // steps carry no explicit per-step temperature must still yield a
        // non-empty array, or the summary hero falls through to rendering
        // the loaded profile instead of its own.
        fProfileStepTemps = temps.length > 0 ? temps : (fProfileTempC > 0 ? [fProfileTempC] : [])
    }

    // Re-resolve the linked bag's details (grind default, roast level, tea
    // blob) from inventory — used when a recipe is loaded for edit/clone,
    // where the bag map isn't in hand.
    function refreshBagDetails() {
        fBagGrindDefault = ""
        if (hasBean)
            MainController.bagStorage.requestInventory()
    }

    // The exact map save() persists (plus a create-only requestToken) — also
    // the dirty check's comparison basis, so "unsaved changes" means
    // precisely "save() would store something different from what was
    // loaded".
    function buildSaveMap() {
        var map = {
            name: nameField.text.trim(),
            drinkType: fDrinkType !== "" ? fDrinkType : deriveDrinkType(),
            profileTitle: fProfileTitle,
            profileJson: fProfileJson,
            bagId: fBagId,
            beanBaseId: fBeanBaseId,
            roasterName: fRoaster,
            coffeeName: fCoffee,
            equipmentId: fEquipmentId,
            doseG: parseFloat(doseField.text) || 0,
            // Yield spec: only the ANCHOR is stored (one value + a mode);
            // the derived field is display-only and never persisted.
            // Ratio clamps to the single C++ bound (YieldSpec::clampRatio,
            // 0.5-6.0) so the stored spec can never disagree with what
            // activation arms.
            yieldValue: fYieldMode === "ratio"
                ? ((parseFloat(ratioField.text) || 0) > 0
                   ? Math.max(0.5, Math.min(6.0, parseFloat(ratioField.text))) : 0)
                : (parseFloat(yieldField.text) || 0),
            yieldMode: (fYieldMode === "ratio" && (parseFloat(ratioField.text) || 0) > 0) ? "ratio"
                     : (fYieldMode === "absolute" && (parseFloat(yieldField.text) || 0) > 0) ? "absolute"
                     : "none",
            // The offset IS the stored value (recipe-relative-temp-offset):
            // the stepper edits it verbatim, 0 = brew at the profile's own
            // temperature. Tea details edit the ABSOLUTE temp instead
            // (fTeaTempC) and convert here at the boundary; an unresolvable
            // profile preserves the loaded offset verbatim (the absolute UI
            // could never display it, so it must not be able to destroy it).
            tempOffsetC: isTeaDrink
                ? (fProfileTempC > 0
                    ? (fTeaTempC > 0 && Math.abs(fTeaTempC - fProfileTempC) > 0.05
                        ? fTeaTempC - fProfileTempC : 0)
                    : fLoadedTempOffsetC)
                : (Math.abs(fTempDeltaC) > 0.05 ? fTempDeltaC : 0),
            // Grind always lives on the recipe (fix-recipe-grind-integrity):
            // whatever is on the field saves as the recipe's own value. Tea
            // recipes never store grind (nothing to grind).
            grindPinned: (!activeTemplate.grind) ? "" : fGrind.trim(),
            rpmPinned: (activeTemplate.grind
                        && (fEquipmentRpmCapable || fRpmPinned > 0))
                ? fRpmPinned : 0,
            steamJson: buildSteamJson(),
            hotWaterJson: buildHotWaterJson()
        }
        if (prefill && prefill.createdFromShotId)
            map.createdFromShotId = prefill.createdFromShotId
        if (prefill && prefill.clonedFromRecipeId)
            map.clonedFromRecipeId = prefill.clonedFromRecipeId
        return map
    }

    function save() {
        // Re-entry guard: a second tap while the async create/update is in
        // flight would submit twice (create mode would duplicate the recipe —
        // the token guard silently discards the first reply).
        if (_submitting)
            return
        Qt.inputMethod.commit()  // IME: flush the in-progress word first
        errorMessage = ""
        var name = nameField.text.trim()
        if (name === "") {
            errorMessage = TranslationManager.translate("recipes.wizard.errorNoName", "A recipe needs a name")
            return
        }
        if (!MainController.recipeStorage.isSaveValid(name, fProfileTitle, buildHotWaterJson())) {
            errorMessage = TranslationManager.translate("recipes.wizard.errorNoProfile",
                "A recipe needs a profile (unless it is a hot-water drink)")
            return
        }
        var map = buildSaveMap()
        _submitting = true
        _saveFailReason = ""   // never inherit a previous attempt's cause
        if (mode === "edit" && editRecipeId > 0) {
            MainController.recipeStorage.requestUpdateRecipe(editRecipeId, map)
        } else {
            // Correlation token: recipeCreated is a broadcast (MCP/web
            // creates race this one) — the handler below must only pop on
            // OUR create's result.
            _createToken = "wizard-" + Date.now() + "-" + Math.floor(Math.random() * 1e9)
            map.requestToken = _createToken
            MainController.recipeStorage.requestCreateRecipe(map)
        }
    }
    property string _createToken: ""

    // --- unsaved-changes guard ----------------------------------------------
    // Snapshot of buildSaveMap() at load time; "dirty" is a comparison
    // against it, so every field, block toggle, and picker choice is covered
    // without per-control bookkeeping. Captured once the entry state is fully
    // in hand (after applyRecipeMap / prefillFromShot / a blank start).
    property string _baselineJson: ""
    function captureBaseline() {
        _baselineJson = JSON.stringify(buildSaveMap())
    }
    function hasUnsavedChanges() {
        // No baseline yet (an edit/promote load still in flight): anything
        // typed this early would be overwritten by applyRecipeMap when the
        // reply lands anyway — never block the exit.
        if (_baselineJson === "")
            return false
        Qt.inputMethod.commit()  // IME: flush the in-progress word first
        return JSON.stringify(buildSaveMap()) !== _baselineJson
    }

    // Every cancel/back path out of the wizard funnels through here (the
    // save-success pops and the deleted-recipe bail are deliberately not
    // intercepted): with unsaved changes the exit dialog intercepts
    // (Discard / Save); otherwise leave directly. The early creation-walk
    // steps exit silently when nothing saveable exists yet (abandoning a
    // couple of taps must not nag); from the details/summary steps — where
    // typed content lives — any unsaved change prompts, even an unsaveable
    // one (e.g. the name cleared for a retype): Discard plus a disabled
    // Save beats a silent discard.
    function requestExit() {
        var earlyWalk = !_enteredAtSummary
            && currentStep !== "summary" && currentStep !== "details"
        if (hasUnsavedChanges() && (canSave || !earlyWalk))
            exitDialog.open()
        else
            root.goBack()
    }

    // A failed save must be SEEN: the pinned error label exists only on the
    // summary and details steps, but the exit dialog's Save can fire from a
    // walk step (backing out of a create). Land on the summary, where the
    // error, the name field, and Save sit together.
    function showSaveError() {
        if (currentStep !== "summary" && currentStep !== "details") {
            _fromSummary = false
            _enterStep("summary")
        }
    }

    // --- wizard step actions -----------------------------------------------

    function selectDrinkType(type) {
        var was = fDrinkType
        fDrinkType = type
        var t = templates[type]
        // Block pre-seeds. Only when the type actually changed — re-visiting
        // the step from the summary must not clobber tuned blocks.
        if (was !== type) {
            fHasMilk = t.milk === true
            if (t.water === true) {
                fHasWater = true
                fWaterOrder = t.waterOrder || "after"
            } else {
                fHasWater = false
            }
            // Crossing the coffee/tea boundary invalidates the bag choice.
            if (was !== "" && (templates[was] || {}).bagKind !== t.bagKind) {
                fBagId = 0; fBeanBaseId = ""; fRoaster = ""; fCoffee = ""; fBagBlob = ""
                fBagGrindDefault = ""; fBagRpmDefault = 0
            }
            // Tea → hot-water tea keeps no profile; other switches keep it
            // only when it still fits the new filter set (checked lazily by
            // the profile step; clearing here keeps the walk honest).
            if (type === "tea_hotwater")
                { fProfileTitle = ""; fProfileJson = ""; fProfileTempC = 0; fProfileYieldG = 0; fProfileStepTemps = [] }
            // Per-drink-type equipment default (last recipe of this type).
            MainController.recipeStorage.requestLastEquipmentForDrinkType(type)
        }
        suggestName()
        // Hot-water tea has no profile step — details follows the bean.
        stepDone("bean")
    }

    function selectBean(bag) {
        // The previous bag's untouched default may still sit on the field —
        // remember it so a bag swap can re-default, while a user-typed value
        // survives (never overwrite an edit in this wizard session).
        var prevDefault = fBagGrindDefault
        if (!bag) {
            fBagId = 0
            fBeanBaseId = ""; fRoaster = ""; fCoffee = ""; fBagBlob = ""
            fBagGrindDefault = ""; fBagRpmDefault = 0
            _selectedBagRoastLevel = ""; _selectedBagRoastDate = ""
        } else {
            // The tile IS the bag: link exactly this one (two open bags of
            // the same bean are distinct choices — no identity resolution).
            fBagId = bag.id || 0
            fBeanBaseId = bag.beanBaseId || ""
            fRoaster = bag.roasterName || ""
            fCoffee = bag.coffeeName || ""
            fBagBlob = bag.beanBaseData || ""
            fBagGrindDefault = bag.grinderSetting || ""
            fBagRpmDefault = bag.rpm || 0
            _selectedBagRoastLevel = bag.roastLevel || ""
            _selectedBagRoastDate = bag.roastDate || ""
            // One-time editable default (recipe-model): a NEW recipe's grind
            // fields start from the bag's current dial. Only on create —
            // editing an existing recipe never re-offers the default — and
            // only over an empty field or the previous bag's untouched
            // default (a swap re-defaults; a typed value stays).
            if (mode !== "edit" && activeTemplate.grind && fBagGrindDefault !== ""
                && (fGrind.trim() === "" || fGrind.trim() === prevDefault)) {
                fGrind = fBagGrindDefault
                fRpmPinned = (fEquipmentRpmCapable && fBagRpmDefault > 0)
                    ? fBagRpmDefault : 0
            }
            suggestName()
        }
        stepDone(isHotWaterTea ? "details" : "profile")
        if (currentStep === "details")
            applyDetailsPrefill()
    }

    function selectProfile(entry) {
        fProfileTitle = entry.title
        fProfileJson = ""  // installed profile: resolve by title
        // If a tea profile was picked while the type said hot-water (or vice
        // versa), keep the drink type honest.
        if (fDrinkType === "tea_hotwater")
            fDrinkType = "tea"
        var detail = ProfileManager.getProfileByFilename(entry.name)
        if ((detail.recommended_dose > 0 && doseField.text === "")
            || (detail.target_weight > 0 && yieldField.text === ""))
            _numbersSource = "profile"
        if (detail.recommended_dose > 0 && doseField.text === "") {
            doseField.text = Number(detail.recommended_dose).toFixed(1)
            syncDerivedYieldFields()
        }
        if (detail.target_weight > 0 && yieldField.text === "" && fYieldMode === "none") {
            yieldField.text = Number(detail.target_weight).toFixed(1)
            fYieldMode = "absolute"
            syncDerivedYieldFields()
        }
        fProfileTempC = detail.espresso_temperature || 0
        fProfileYieldG = detail.target_weight || 0
        var pickedTemps = []
        var pickedSteps = detail.steps || []
        for (var psi = 0; psi < pickedSteps.length; ++psi) {
            var psTemp = pickedSteps[psi] ? Number(pickedSteps[psi].temperature) : 0
            if (psTemp > 0)
                pickedTemps.push(psTemp)
        }
        // See RecipesPage.refreshProfileNumbers: don't leave this empty
        // when the profile resolved but its steps carry no per-step temp.
        fProfileStepTemps = pickedTemps.length > 0 ? pickedTemps : (fProfileTempC > 0 ? [fProfileTempC] : [])
        // Tea temp is resolved entirely by applyDetailsPrefill (bag vendor
        // temp with the type-match correction, then profile default, then
        // history overwrite) — pre-seeding it here would make that whole
        // chain dead code (its `fTeaTempC <= 0` gate would never open).
        suggestName()
        stepDone("details")
        applyDetailsPrefill()
    }

    function selectJustHotWater() {
        fDrinkType = "tea_hotwater"
        fProfileTitle = ""; fProfileJson = ""; fProfileTempC = 0; fProfileYieldG = 0
        fHasWater = true
        fWaterOrder = "after"
        suggestName()
        stepDone("details")
        applyDetailsPrefill()
    }

    // Link (or clear) the recipe's equipment package, from the equipment
    // window's inline tiles. `pkg` is a flattened package map, or a
    // { isNone: true } sentinel / null to clear.
    function selectEquipment(pkg) {
        if (!pkg || pkg.isNone) {
            fEquipmentId = 0
            fEquipmentName = ""
            fEquipmentGrinderBrand = ""; fEquipmentGrinderModel = ""
            _selectedPackage = ({})
            return
        }
        fEquipmentId = pkg.id
        fEquipmentName = pkg.name
            || ((pkg.grinderBrand || "") + " " + (pkg.grinderModel || "")).trim()
            || ((pkg.basketBrand || "") + " " + (pkg.basketModel || "")).trim()
        fEquipmentGrinderBrand = pkg.grinderBrand || ""
        fEquipmentGrinderModel = pkg.grinderModel || ""
        _selectedPackage = pkg
    }

    // The equipment window's tile model: a "None" tile followed by the
    // in-inventory packages (the currently-linked one is always kept, even if
    // it has since been retired, so the selection stays visible).
    function equipmentTileModel() {
        var arr = [{ isNone: true }]
        for (var i = 0; i < _packages.length; ++i) {
            var p = _packages[i]
            if (p.inInventory !== false || p.id === fEquipmentId)
                arr.push(p)
        }
        return arr
    }

    // Snapshot the chosen steam pitcher preset onto the recipe BY VALUE (never
    // a preset index). Shared by the steam window's tiles.
    function selectPitcher(preset) {
        fPitcherName = preset.name || ""
        fPitcherDurationSec = preset.duration || 0
        fPitcherFlow = preset.flow || 0
        fPitcherTemperatureC = preset.temperature || 0
    }

    // The steam window's tile model: the enabled pitcher presets.
    function pitcherTileModel() {
        var arr = []
        var presets = Settings.brew.steamPitcherPresets
        for (var i = 0; i < presets.length; ++i)
            if (!presets[i].disabled)
                arr.push(presets[i])
        return arr
    }

    // Compact "45s · 62°C" metadata line for a pitcher preset tile.
    function pitcherTileMeta(preset) {
        var parts = []
        if ((preset.duration || 0) > 0)
            parts.push(preset.duration + "s")
        if ((preset.temperature || 0) > 0)
            parts.push(Theme.formatTemperature(preset.temperature, 0))
        return parts.join(" · ")
    }

    // --- details prefill (history → tea bag data → profile defaults) -------

    // Absolute tea temperature (vendor numbers are steeping temps; the tea
    // details step edits the absolute rather than an offset).
    property real fTeaTempC: 0
    property var _teaBrewing: ({})
    // Grind hint under the grind field: the latest grind dialed for this
    // bean (or similar-roast beans), plus a UGS DIRECTION when it was dialed
    // for a different profile — never a computed click count (the KB's own
    // cross-profile rule).
    property string grindHint: ""
    // True once the user has typed in a details field (or an existing recipe
    // was loaded). The profile/bag seeds fill the fields synchronously so the
    // details step is never blank; the async shot-history reply then OVERWRITES
    // them (design D6: history that actually worked wins) — but only while this
    // is false, so a value the user has since typed is never clobbered.
    property bool _detailsUserEdited: false

    // Where the prefilled numbers came from — the wizard TELLS the user, so
    // the details step isn't a wall of unexplained values: "history" (last
    // shot with this bean+profile), "profile" (its recommended numbers),
    // "teabag" (the bag's brewing instructions), "saved" (editing an
    // existing recipe), "" (nothing seeded).
    property string _numbersSource: ""
    readonly property string numbersHint: {
        var origin = ""
        switch (_numbersSource) {
        case "history":
            origin = TranslationManager.translate("recipes.wizard.numbers.fromHistory",
                "Prefilled from your last shot with these beans and this profile — the numbers that worked.")
            break
        case "profile":
            origin = TranslationManager.translate("recipes.wizard.numbers.fromProfile",
                "Prefilled with the profile's own recommended numbers.")
            break
        case "teabag":
            origin = TranslationManager.translate("recipes.wizard.numbers.fromTeaBag",
                "Prefilled from the tea's brewing instructions.")
            break
        case "saved":
            origin = TranslationManager.translate("recipes.wizard.numbers.saved",
                "The recipe's saved numbers.")
            break
        }
        var why = isTeaDrink
            ? TranslationManager.translate("recipes.wizard.numbers.whyTea",
                "Adjust the leaf amount and steeping temperature to taste.")
            : TranslationManager.translate("recipes.wizard.numbers.why",
                "Change them to make this drink your own — the temp offset shifts the "
                + "profile's brew temperature (0° = as the profile was designed).")
        return origin === "" ? why : origin + " " + why
    }

    // --- summary-card value builders (enrich-recipe-editor-cards) -----------
    // Each card reads out the recipe values that EDITING THAT CARD changes,
    // and no value repeats across cards: recipe-overridden numbers (dose,
    // yield/ratio, effective temperature, grind, rpm) live on Details; profile
    // shape on Profile; bag identity on Bean; package identity (minus the
    // recipe-owned grind/rpm) on Equipment. A read establishes a binding
    // dependency, so these re-evaluate as the f* fields change.

    // The recipe's EFFECTIVE brew temperature, unit-aware (the same number the
    // hero's plan line shows), with the signed offset tagged when non-zero.
    // "" when nothing is set. Tea edits an absolute; espresso a profile offset.
    function summaryEffectiveTempStr() {
        if (isHotWaterTea)
            return fVesselTemperatureC > 0 ? Theme.formatTemperature(fVesselTemperatureC, 0) : ""
        if (isTeaDrink)
            return fTeaTempC > 0 ? Theme.formatTemperature(fTeaTempC, 0) : ""
        if (fProfileTempC > 0) {
            var eff = Theme.formatTemperature(fProfileTempC + fTempDeltaC, 0)
            if (Math.abs(fTempDeltaC) > 0.05) {
                var d = Theme.cDeltaToDisplay(fTempDeltaC)
                eff += " (" + (d > 0 ? "+" : "") + d.toFixed(0) + "°)"
            }
            return eff
        }
        // Unresolvable profile temp: show the offset alone rather than nothing.
        if (Math.abs(fTempDeltaC) > 0.05) {
            var dd = Theme.cDeltaToDisplay(fTempDeltaC)
            return (dd > 0 ? "+" : "") + dd.toFixed(0) + "°"
        }
        return ""
    }

    // Details card: every value the details step pins, drink-type-scoped —
    // dose→yield (ratio shown as such), effective temperature, grind (+ rpm
    // only for an RPM-capable grinder). Tea omits grind; hot-water tea shows
    // volume + temperature instead of dose→yield.
    function summaryDetailsValue() {
        var parts = []
        var dose = parseFloat(doseField.text) || 0
        var yieldG = parseFloat(yieldField.text) || 0
        var ratioV = parseFloat(ratioField.text) || 0
        if (isHotWaterTea) {
            if (fVesselVolume > 0)
                parts.push(fVesselVolume + (fVesselMode === "volume" ? "ml" : "g"))
        } else if (fYieldMode === "ratio" && ratioV > 0) {
            parts.push(dose > 0
                ? dose.toFixed(1) + "g → " + (dose * ratioV).toFixed(1) + "g (1:" + ratioV.toFixed(1) + ")"
                : "1:" + ratioV.toFixed(1))
        } else if (dose > 0 && yieldG > 0) {
            parts.push(dose.toFixed(1) + "g → " + yieldG.toFixed(1) + "g")
        } else if (dose > 0) {
            parts.push(dose.toFixed(1) + "g")
        }
        var tempStr = summaryEffectiveTempStr()
        if (tempStr !== "")
            parts.push(tempStr)
        if (activeTemplate.grind) {
            var g = fGrind.trim()
            if (g !== "") {
                var rpm = fRpmPinned
                var grindStr = TranslationManager.translate("recipes.wizard.summary.grind", "grind %1").arg(g)
                if (fEquipmentRpmCapable && rpm > 0)
                    grindStr += " · " + TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(rpm)
                parts.push(grindStr)
            }
        }
        return parts.length > 0 ? parts.join(" · ") : "—"
    }

    // Bean card: coffee name, then roaster · roast level · roast age. The photo
    // is rendered separately by the card's BeanThumbnail.
    function summaryBeanValue() {
        var name = (fCoffee !== "" ? fCoffee : fRoaster)
        var detail = []
        if (fCoffee !== "" && fRoaster !== "")
            detail.push(fRoaster)
        if (_selectedBagRoastLevel !== "")
            detail.push(_selectedBagRoastLevel)
        var age = bagRoastAgeLine()
        if (age !== "")
            detail.push(age)
        return detail.length > 0 ? name + "\n" + detail.join(" · ") : name
    }

    // "<roast date> · <age>d" for the linked bag — mirrors the bean step's
    // per-tile roastAgeLine, but reads the page-level selected-bag state.
    function bagRoastAgeLine() {
        var date = String(_selectedBagRoastDate || "")
        if (date === "")
            return ""
        var parsed = Date.fromLocaleString(Qt.locale("C"), date, "yyyy-MM-dd")
        if (isNaN(parsed.getTime()))
            return date
        var days = Math.floor((Date.now() - parsed.getTime()) / 86400000)
        return days >= 0 ? date + " · " + days + "d" : date
    }

    // The bag's product-page link (bean-image cache key follows the bag), for
    // the summary Bean card's BeanThumbnail.
    function bagImageLink() {
        if (fBagBlob === "")
            return ""
        try { return JSON.parse(fBagBlob).link || "" }
        catch (e) { console.warn("RecipeWizard: bad bag blob JSON:", e); return "" }
    }

    // Profile card: a RICH read-out of what PICKING A PROFILE brings to the
    // recipe — name, editor/type classification, notable beverage type, and a
    // pressure/flow shape summary — EXCLUDING the temp/dose/yield/grind the
    // recipe overrides (those are Details-owned). The (i)/KB buttons live in
    // the card header. "" profile → em dash handled by the caller.
    function summaryProfileValue() {
        if (fProfileTitle === "")
            return "—"
        var info = ProfileManager.profileCatalogInfoForTitle(fProfileTitle)
        var parts = [fProfileTitle]
        var meta = []
        var et = profileEditorLabel(info ? (info.editorType || "") : "")
        if (et !== "")
            meta.push(et)
        var bev = profileBeverageLabel(info ? (info.beverageType || "") : "")
        if (bev !== "")
            meta.push(bev)
        if (meta.length > 0)
            parts.push(meta.join(" · "))
        var shape = summaryProfileShape()
        if (shape !== "")
            parts.push(shape)
        return parts.join("\n")
    }

    // Editor/type classification label. D-Flow/A-Flow are proper nouns (the
    // same names the wizard header shows); the rest are localized.
    function profileEditorLabel(code) {
        switch (code) {
        case "dflow": return "D-Flow"
        case "aflow": return "A-Flow"
        case "pressure": return TranslationManager.translate("recipes.wizard.profileType.pressure", "Pressure profile")
        case "flow": return TranslationManager.translate("recipes.wizard.profileType.flow", "Flow profile")
        case "advanced": return TranslationManager.translate("recipes.wizard.profileType.advanced", "Advanced profile")
        }
        return ""
    }

    // Only NOTABLE beverage types earn a line — espresso is the default and is
    // already conveyed by the drink card, so it (and empty) render nothing.
    function profileBeverageLabel(bev) {
        switch (String(bev).toLowerCase()) {
        case "filter": return TranslationManager.translate("recipes.wizard.profileBeverage.filter", "Filter")
        case "pourover": return TranslationManager.translate("recipes.wizard.profileBeverage.pourover", "Pour-over")
        case "tea_portafilter": return TranslationManager.translate("recipes.wizard.profileBeverage.tea", "Tea")
        }
        return ""
    }

    // Resolve the picked profile's object (installed by title, else the
    // embedded JSON fallback) — the same ladder refreshProfileTemp uses.
    function resolveProfileObj() {
        if (fProfileTitle === "")
            return null
        var fn = ProfileManager.findProfileByTitle(fProfileTitle)
        if (fn && fn !== "")
            return ProfileManager.getProfileByFilename(fn)
        if (fProfileJson !== "") {
            try { return JSON.parse(fProfileJson) }
            catch (e) { console.warn("RecipeWizard: embedded profile JSON unparsable:", e); return null }
        }
        return null
    }

    // A concise pressure/flow SHAPE summary from the profile's frames — frame
    // count plus peak pressure / max flow across steps. Profile-owned detail
    // the recipe never overrides.
    function summaryProfileShape() {
        var d = resolveProfileObj()
        if (!d)
            return ""
        var steps = d.steps || []
        if (steps.length === 0)
            return ""
        var maxP = 0, maxF = 0
        for (var i = 0; i < steps.length; ++i) {
            var p = Number(steps[i].pressure) || 0
            var f = Number(steps[i].flow) || 0
            if (p > maxP) maxP = p
            if (f > maxF) maxF = f
        }
        var bits = [TranslationManager.translate("recipes.wizard.profileShape.frames", "%1 frames").arg(steps.length)]
        if (maxP > 0)
            bits.push(TranslationManager.translate("recipes.wizard.profileShape.peakPressure", "peak %1 bar").arg(maxP.toFixed(1)))
        if (maxF > 0)
            bits.push(TranslationManager.translate("recipes.wizard.profileShape.maxFlow", "max %1 ml/s").arg(maxF.toFixed(1)))
        return bits.join(" · ")
    }

    // Revert the numbers to the profile's own values (the card's "Reset to
    // profile" action): recommended dose, target yield, no temp offset —
    // tea falls back to the per-type default temperature when the profile
    // states none. Marks the fields user-edited so the async history reply
    // cannot overwrite a deliberate reset.
    function resetNumbersToProfile() {
        var dose = 0
        if (fProfileTitle !== "") {
            var fn = ProfileManager.findProfileByTitle(fProfileTitle)
            if (fn && fn !== "") {
                var d = ProfileManager.getProfileByFilename(fn)
                dose = d.recommended_dose || 0
                fProfileTempC = d.espresso_temperature || 0
                fProfileYieldG = d.target_weight || 0
            }
        }
        doseField.text = dose > 0 ? Number(dose).toFixed(1) : ""
        yieldField.text = fProfileYieldG > 0 ? Number(fProfileYieldG).toFixed(1) : ""
        fYieldMode = fProfileYieldG > 0 ? "absolute" : "none"
        ratioField.text = ""
        syncDerivedYieldFields()
        fTempDeltaC = 0
        if (isTeaDrink)
            fTeaTempC = fProfileTempC > 0 ? fProfileTempC
                : ProfileManager.defaultTeaTempC(String(_teaBrewing.teaType || ""))
        _detailsUserEdited = true
        _numbersSource = "profile"
    }

    function applyDetailsPrefill() {
        // The creation walk reaches details here — start at the first window
        // (equipment), then Continue walks numbers → steam/water → summary.
        _detailsPage = "equipment"
        MainController.equipmentStorage.requestInventory()
        // Each window shows its fields expanded and ready (one facet per
        // window now — no collapse-to-summary).
        // Profile/bag seeds run synchronously so the details step is never
        // blank; the shot-history tier lands async via
        // latestShotForBeanProfileReady and OVERWRITES them (history that
        // actually worked is the top priority — design D6), guarded by
        // _detailsUserEdited so a typed value is never clobbered.
        if (isTeaDrink) {
            _teaBrewing = ({})
            if (fBagBlob !== "") {
                try { _teaBrewing = JSON.parse(fBagBlob) } catch (e) { _teaBrewing = ({}); console.warn("RecipeWizard: bad bag blob JSON:", e) }
            }
            var stated = parseFloat(_teaBrewing.brewTempC) || 0
            var typeMatched = fProfileTitle !== ""
                && ProfileManager.teaProfileMatchesType(fProfileTitle, String(_teaBrewing.teaType || ""))
            if (fTeaTempC <= 0 && (stated > 0 || parseFloat(_teaBrewing.leafGramsPer100Ml) > 0))
                _numbersSource = "teabag"
            if (fTeaTempC <= 0) {
                // Vendor temp applies verbatim for hot-water tea; for
                // portafilter tea only when the profile is NOT type-matched
                // (a matched profile's temp already encodes the portafilter
                // adaptation). Fallback: profile temp, then per-type default.
                if (isHotWaterTea && stated > 0)
                    fTeaTempC = stated
                else if (!typeMatched && stated > 0)
                    fTeaTempC = stated
                else if (fProfileTempC > 0)
                    fTeaTempC = fProfileTempC
                else
                    fTeaTempC = ProfileManager.defaultTeaTempC(String(_teaBrewing.teaType || ""))
            }
            // Leaf dose from the bag's ratio × the target volume.
            var ratio = parseFloat(_teaBrewing.leafGramsPer100Ml) || 0
            if (doseField.text === "" && ratio > 0) {
                var volumeMl = isHotWaterTea ? fVesselVolume : (parseFloat(yieldField.text) || 0)
                if (volumeMl > 0)
                    doseField.text = (ratio * volumeMl / 100).toFixed(1)
            }
        }
        if (hasBean && fProfileTitle !== "")
            MainController.shotHistory.requestLatestShotForBeanProfile(fRoaster, fCoffee, fProfileTitle)
        grindHint = ""
        if (activeTemplate.grind && (hasBean || _selectedBagRoastLevel !== ""))
            MainController.shotHistory.requestLatestGrindForBean(fRoaster, fCoffee, _selectedBagRoastLevel)
        // Dose/yield are now seeded — re-run so a collision qualifier (the rare
        // same-bean+type+profile case) reflects real numbers. Guarded by
        // _autoName, so a user-typed name is never touched.
        suggestName()
    }

    // --- profile ranking ----------------------------------------------------

    // Assembled model for the profile step: [{header}] and [{profile row}]
    // entries. Tiers: ① used with this bean, ② similar (type-matched tea
    // profiles first), ③ the rest of the filter set.
    property var profileModel: []
    property var _ranked: ({})
    property string profileFilter: ""

    function requestProfileRanking() {
        _ranked = ({})
        var teaType = ""
        if (isTeaDrink && fBagBlob !== "") {
            try { teaType = String(JSON.parse(fBagBlob).teaType || "") } catch (e) { console.warn("RecipeWizard: bad bag blob JSON:", e) }
        }
        var roastLevel = ""  // the bag list carries roastLevel per bag
        if (!isTeaDrink && _selectedBagRoastLevel !== "")
            roastLevel = _selectedBagRoastLevel
        rebuildProfileModel()
        if (hasBean)
            MainController.shotHistory.requestRankedProfilesForBean(fRoaster, fCoffee, roastLevel, teaType)
    }
    property string _selectedBagRoastLevel: ""
    // The linked bag's roast date, kept alongside the roast level so the
    // summary Bean card can render "<date> · <age>d" (the same line the bean
    // step's tiles show). Resolved from inventory / direct selection.
    property string _selectedBagRoastDate: ""

    function rebuildProfileModel() {
        var filter = profileFilter.trim().toLowerCase()
        var beverages = activeTemplate.beverages
        var all = ProfileManager.allProfilesList
        var inSet = []
        for (var i = 0; i < all.length; ++i) {
            var bev = String(all[i].beverageType || "").trim().toLowerCase()
            if (beverages.indexOf(bev) < 0)
                continue
            if (filter !== "" && String(all[i].title).toLowerCase().indexOf(filter) < 0)
                continue
            inSet.push(all[i])
        }
        var byTitle = {}
        for (i = 0; i < inSet.length; ++i)
            byTitle[String(inSet[i].title).toLowerCase()] = inSet[i]

        var model = []
        var used = {}
        var withBean = (_ranked.withBean || [])
        var similar = (_ranked.similar || [])
        var tier1 = []
        for (i = 0; i < withBean.length; ++i) {
            var p = byTitle[String(withBean[i].profileName).toLowerCase()]
            if (p) { tier1.push({ isHeader: false, tier: 1, title: p.title, name: p.name, reason: "",
                                  tempC: p.espressoTemperature || 0, yieldG: p.targetWeight || 0,
                                  hasKb: p.hasKnowledgeBase === true }); used[p.title] = true }
        }
        if (tier1.length > 0) {
            model.push({ isHeader: true, title: TranslationManager.translate(
                "recipes.wizard.profiles.withBean", "Used with this bean") })
            model = model.concat(tier1)
        }
        // Tier ②: knowledge-driven recommendations (no history needed) first,
        // then similar-bean history. Tea: profiles whose stock title matches
        // the bag's tea type. Coffee: profiles the knowledge base states
        // shine with the bag's roast level (KB roastAffinity — authored from
        // each profile's own dial-in docs).
        var tier2 = []
        var teaType = ""
        if (isTeaDrink && _teaBrewingTypeForRanking() !== "")
            teaType = _teaBrewingTypeForRanking()
        if (teaType !== "") {
            for (i = 0; i < inSet.length; ++i) {
                if (used[inSet[i].title]) continue
                if (ProfileManager.teaProfileMatchesType(inSet[i].title, teaType))
                    tier2.push({ isHeader: false, tier: 2, title: inSet[i].title, name: inSet[i].name,
                                 tempC: inSet[i].espressoTemperature || 0, yieldG: inSet[i].targetWeight || 0,
                                 hasKb: inSet[i].hasKnowledgeBase === true,
                                 reason: TranslationManager.translate(
                                     "recipes.wizard.profiles.matchesType", "matches %1").arg(teaType) })
            }
        }
        if (!isTeaDrink && _selectedBagRoastLevel !== "") {
            for (i = 0; i < inSet.length; ++i) {
                if (used[inSet[i].title]) continue
                if (ProfileManager.kbProfileSuitsRoast(inSet[i].title, _selectedBagRoastLevel))
                    tier2.push({ isHeader: false, tier: 2, title: inSet[i].title, name: inSet[i].name,
                                 tempC: inSet[i].espressoTemperature || 0, yieldG: inSet[i].targetWeight || 0,
                                 hasKb: inSet[i].hasKnowledgeBase === true,
                                 reason: TranslationManager.translate(
                                     "recipes.wizard.profiles.suitsRoast", "suits %1 roasts")
                                     .arg(_selectedBagRoastLevel.toLowerCase()) })
            }
        }
        for (i = 0; i < similar.length; ++i) {
            p = byTitle[String(similar[i].profileName).toLowerCase()]
            if (p && !used[p.title]) {
                var already = false
                for (var t = 0; t < tier2.length; ++t) {
                    if (tier2[t].title === p.title) { already = true; break }
                }
                if (!already)
                    tier2.push({ isHeader: false, tier: 2, title: p.title, name: p.name,
                                 tempC: p.espressoTemperature || 0, yieldG: p.targetWeight || 0,
                                 hasKb: p.hasKnowledgeBase === true,
                                 reason: TranslationManager.translate(
                                     "recipes.wizard.profiles.similarBeans", "used with similar beans") })
            }
        }
        // A HANDFUL of the best, not the whole matching set — candidates
        // beyond the cap fall through to "All profiles" (only the kept rows
        // are marked used).
        tier2 = tier2.slice(0, 5)
        for (i = 0; i < tier2.length; ++i)
            used[tier2[i].title] = true
        if (tier2.length > 0) {
            model.push({ isHeader: true, title: TranslationManager.translate(
                "recipes.wizard.profiles.recommended", "Recommended") })
            model = model.concat(tier2)
        }
        // Tier ③: the rest. Tea with a stated brew temp orders by proximity;
        // otherwise the list keeps allProfilesList's alphabetical order.
        var rest = []
        for (i = 0; i < inSet.length; ++i) {
            if (!used[inSet[i].title])
                rest.push(inSet[i])
        }
        var statedTemp = 0
        if (isTeaDrink && fBagBlob !== "") {
            try { statedTemp = parseFloat(JSON.parse(fBagBlob).brewTempC) || 0 } catch (e) { console.warn("RecipeWizard: bad bag blob JSON:", e) }
        }
        if (statedTemp > 0) {
            var withTemp = rest.map(function(p) {
                var t = p.espressoTemperature || 0
                return { p: p, key: t > 0 ? Math.abs(t - statedTemp) : 999 }
            })
            withTemp.sort(function(a, b) { return a.key - b.key })
            rest = withTemp.map(function(e) { return e.p })
        }
        if (rest.length > 0) {
            if (model.length > 0)
                model.push({ isHeader: true, title: TranslationManager.translate(
                    "recipes.wizard.profiles.all", "All profiles") })
            for (i = 0; i < rest.length; ++i)
                model.push({ isHeader: false, tier: 3, title: rest[i].title, name: rest[i].name, reason: "",
                             tempC: rest[i].espressoTemperature || 0, yieldG: rest[i].targetWeight || 0,
                             hasKb: rest[i].hasKnowledgeBase === true })
        }
        profileModel = model
    }

    function _teaBrewingTypeForRanking() {
        if (fBagBlob === "") return ""
        try { return String(JSON.parse(fBagBlob).teaType || "") } catch (e) { console.warn("RecipeWizard: bad bag blob JSON:", e); return "" }
    }

    // --- connections --------------------------------------------------------

    Connections {
        target: MainController.shotHistory
        enabled: wizardPage.promoteShotId > 0
        function onShotReady(id, shot) {
            if (id === wizardPage.promoteShotId)
                wizardPage.prefillFromShot(shot)
        }
    }
    Connections {
        target: MainController.shotHistory
        function onRankedProfilesForBeanReady(result) {
            // Stale-reply guard: ignore a ranking that answers a bean the user
            // has since switched away from (the query echoes its bean).
            if (String(result.queryBrand || "") !== wizardPage.fRoaster
                || String(result.queryType || "") !== wizardPage.fCoffee)
                return
            wizardPage._ranked = result
            wizardPage.rebuildProfileModel()
        }
        function onLatestGrindForBeanReady(grind) {
            if (!wizardPage.activeTemplate.grind) return
            // Stale-reply guard: the query echoes the bean+roast it answered.
            if (!grind || String(grind.queryBrand || "") !== wizardPage.fRoaster
                || String(grind.queryType || "") !== wizardPage.fCoffee
                || String(grind.queryRoast || "") !== wizardPage._selectedBagRoastLevel) {
                return
            }
            if (!grind.grinderSetting) {
                wizardPage.grindHint = ""
                return
            }
            var g = String(grind.grinderSetting || "")
            var src = String(grind.profileName || "")
            if (g === "") { wizardPage.grindHint = ""; return }
            var parts = []
            if (grind.matchLevel === "bean")
                parts.push(TranslationManager.translate("recipes.wizard.grindHint.bean",
                    "Last grind for this bean: %1 (with %2).").arg(g).arg(src))
            else
                parts.push(TranslationManager.translate("recipes.wizard.grindHint.similar",
                    "Similar beans (%1) last ground at %2 (with %3).")
                    .arg(wizardPage._selectedBagRoastLevel).arg(g).arg(src))
            // Direction only when the grind was dialed for a DIFFERENT
            // profile and the KB knows both profiles' UGS ordering.
            if (src !== "" && wizardPage.fProfileTitle !== "" && src !== wizardPage.fProfileTitle) {
                var dir = ProfileManager.grindDirectionBetween(src, wizardPage.fProfileTitle)
                if (dir === "finer")
                    parts.push(TranslationManager.translate("recipes.wizard.grindHint.finer",
                        "%1 typically grinds finer — start finer than that.").arg(wizardPage.fProfileTitle))
                else if (dir === "coarser")
                    parts.push(TranslationManager.translate("recipes.wizard.grindHint.coarser",
                        "%1 typically grinds coarser — start coarser than that.").arg(wizardPage.fProfileTitle))
            }
            wizardPage.grindHint = parts.join(" ")
        }
        function onLatestShotForBeanProfileReady(shot) {
            if (!shot || Object.keys(shot).length === 0)
                return
            // Stale-reply guard: this async reply may land after the user has
            // switched bean/profile — ignore it unless it still answers the
            // current selection (the returned shot echoes the bean+profile it
            // was queried for).
            if (String(shot.profileName || "") !== wizardPage.fProfileTitle
                || String(shot.beanBrand || "") !== wizardPage.fRoaster
                || String(shot.beanType || "") !== wizardPage.fCoffee)
                return
            // History is the top-priority tier (design D6): overwrite the
            // profile/bag seeds with the numbers that actually worked — but
            // never a value the user has typed (_detailsUserEdited).
            if (wizardPage._detailsUserEdited)
                return
            wizardPage._numbersSource = "history"
            if (shot.doseWeightG > 0)
                doseField.text = Number(shot.doseWeightG).toFixed(1)
            // History prefill carries the shot's ANCHOR: a 1:2 shot seeds a
            // ratio anchor; a legacy/absolute shot seeds grams (add-yield-
            // ratio-anchor).
            if (shot.yieldMode === "ratio" && (shot.yieldAnchorValue || 0) > 0) {
                wizardPage.fYieldMode = "ratio"
                ratioField.text = Number(shot.yieldAnchorValue).toFixed(1)
            } else if (shot.targetWeightG > 0) {
                wizardPage.fYieldMode = "absolute"
                yieldField.text = Number(shot.targetWeightG).toFixed(1)
            }
            wizardPage.syncDerivedYieldFields()
            if (shot.temperatureOverrideC > 0) {
                if (wizardPage.isTeaDrink)
                    wizardPage.fTeaTempC = shot.temperatureOverrideC
                else if (wizardPage.fProfileTempC > 0)
                    wizardPage.fTempDeltaC = shot.temperatureOverrideC - wizardPage.fProfileTempC
            }
            // History is the top prefill tier for grind too: the dial that
            // actually worked with this bean+profile beats the bag's default.
            if (wizardPage.activeTemplate.grind && shot.grinderSetting) {
                wizardPage.fGrind = shot.grinderSetting
                if (wizardPage.fEquipmentRpmCapable && shot.rpm > 0)
                    wizardPage.fRpmPinned = shot.rpm
            }
            // History just overwrote dose/yield — refresh a still-auto name so a
            // collision qualifier reflects the numbers that actually worked.
            wizardPage.suggestName()
        }
    }

    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, recipe) {
            if (wizardPage.mode !== "edit" || recipeId !== wizardPage.editRecipeId)
                return
            if (Object.keys(recipe).length > 0) {
                wizardPage.applyRecipeMap(recipe)
                wizardPage.captureBaseline()
            } else {
                // The recipe was deleted between opening the list and the load
                // landing — don't leave a blank "edit" form the user fills in
                // and only fails to save. Leave the page instead.
                console.warn("RecipeWizard: recipe", wizardPage.editRecipeId,
                             "no longer exists — leaving edit")
                root.goBack()
            }
        }
        function onRecipeCreated(recipeId, recipe) {
            // Only OUR create: a concurrent MCP/web create must neither pop
            // this page nor show a phantom save error.
            if (wizardPage._createToken === ""
                || (recipe.requestToken || "") !== wizardPage._createToken)
                return
            if (wizardPage.mode !== "edit" && wizardPage._submitting) {
                wizardPage._submitting = false
                wizardPage._createToken = ""
                if (recipeId > 0) {
                    pageStack.pop()
                } else {
                    // Name the cause when storage gave one — the generic wording
                    // leaves the user with nothing to act on, and this is the
                    // surface where an actionable message matters most.
                    wizardPage.errorMessage = (recipe && recipe.error === "nameInUse")
                        ? TranslationManager.translate("recipes.dialog.nameInUse",
                              "That name is already in use — choose a different name.")
                        : TranslationManager.translate("recipes.wizard.errorSave", "Could not save the recipe")
                    wizardPage.showSaveError()
                }
            }
        }
        function onRecipeUpdateFailed(recipeId, reason) {
            // Lands just before recipeUpdated(false) and names the cause. Gated on
            // _submitting like the terminal handler below: without it, another
            // surface (MCP/web) failing a rename of THIS recipe while the wizard
            // merely sits open would latch a reason, and a later save of ours that
            // failed for an unrelated cause would report it.
            if (wizardPage.mode === "edit" && wizardPage._submitting
                && recipeId === wizardPage.editRecipeId)
                wizardPage._saveFailReason = reason
        }
        function onRecipeUpdated(recipeId, success) {
            if (wizardPage.mode === "edit" && recipeId === wizardPage.editRecipeId
                && wizardPage._submitting) {
                wizardPage._submitting = false
                if (success) {
                    wizardPage._saveFailReason = ""
                    pageStack.pop()
                } else {
                    // "busy" means the database was locked by another write — the
                    // one cause here where trying again actually works, so say so
                    // rather than showing the generic failure.
                    wizardPage.errorMessage = wizardPage._saveFailReason === "nameInUse"
                        ? TranslationManager.translate("recipes.dialog.nameInUse",
                              "That name is already in use — choose a different name.")
                        : wizardPage._saveFailReason === "busy"
                        ? TranslationManager.translate("recipes.wizard.errorSaveBusy",
                              "The database was busy — tap Save again.")
                        : TranslationManager.translate("recipes.wizard.errorSave", "Could not save the recipe")
                    wizardPage._saveFailReason = ""
                    wizardPage.showSaveError()
                }
            }
        }
        function onInventoryReady(list) {
            // Cache existing recipe names (excluding the one being edited) so
            // suggestName() can disambiguate a collision synchronously.
            var names = []
            for (var i = 0; i < list.length; ++i) {
                var r = list[i]
                if (wizardPage.mode === "edit" && (r.id || 0) === wizardPage.editRecipeId)
                    continue
                var n = ((r && r.name) || "").trim().toLowerCase()
                if (n !== "")
                    names.push(n)
            }
            wizardPage._existingRecipeNames = names
        }
        function onLastEquipmentForDrinkTypeReady(drinkType, equipmentId) {
            // Per-drink-type default: only fills an EMPTY equipment choice.
            // No recipe of this type yet → fall back to the currently active
            // package (design D10), so the row prefills instead of "None".
            if (drinkType !== wizardPage.fDrinkType || wizardPage.fEquipmentId > 0)
                return
            var id = equipmentId > 0 ? equipmentId : Settings.dye.activeEquipmentId
            if (id > 0) {
                wizardPage.fEquipmentId = id
                MainController.equipmentStorage.requestInventory()
            }
        }
    }

    property var _bags: []
    property var _packages: []
    // The full flattened map of the recipe's linked equipment package (from
    // EquipmentPackageView.toVariantMap) — feeds the summary Equipment card's
    // EquipmentSummary. Empty until inventory resolves / a package is picked.
    property var _selectedPackage: ({})
    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            wizardPage._bags = bags
            if (wizardPage.hasBean) {
                for (var i = 0; i < bags.length; ++i) {
                    var b = bags[i]
                    // The hard bag link first; bean identity only as a
                    // fallback for link-less recipes (e.g. stale imports).
                    var match = wizardPage.fBagId > 0
                        ? b.id === wizardPage.fBagId
                        : (wizardPage.fBeanBaseId !== ""
                            ? b.beanBaseId === wizardPage.fBeanBaseId
                            : (String(b.roasterName).toLowerCase() === wizardPage.fRoaster.toLowerCase()
                               && String(b.coffeeName).toLowerCase() === wizardPage.fCoffee.toLowerCase()))
                    if (match) {
                        wizardPage.fBagGrindDefault = b.grinderSetting || ""
                        wizardPage.fBagRpmDefault = b.rpm || 0
                        wizardPage._selectedBagRoastLevel = b.roastLevel || ""
                        wizardPage._selectedBagRoastDate = b.roastDate || ""
                        if (wizardPage.fBagBlob === "")
                            wizardPage.fBagBlob = b.beanBaseData || ""
                        return
                    }
                }
                wizardPage.fBagGrindDefault = ""
                wizardPage.fBagRpmDefault = 0
            }
        }
    }
    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            wizardPage._packages = packages
            if (wizardPage.fEquipmentId > 0) {
                for (var i = 0; i < packages.length; ++i) {
                    if (packages[i].id === wizardPage.fEquipmentId) {
                        wizardPage.fEquipmentName = packages[i].name
                            || ((packages[i].grinderBrand || "") + " " + (packages[i].grinderModel || "")).trim()
                            || ((packages[i].basketBrand || "") + " " + (packages[i].basketModel || "")).trim()
                        wizardPage.fEquipmentGrinderBrand = packages[i].grinderBrand || ""
                        wizardPage.fEquipmentGrinderModel = packages[i].grinderModel || ""
                        wizardPage._selectedPackage = packages[i]
                        return
                    }
                }
                wizardPage.fEquipmentGrinderBrand = ""
                wizardPage.fEquipmentGrinderModel = ""
                wizardPage._selectedPackage = ({})
            }
            // Never start the equipment window empty (creation walk only —
            // edit/clone carry the recipe's own package and a summary-card
            // jump must always show the window): default to the ACTIVE
            // package, and when the inventory holds exactly ONE in-inventory
            // package fill it in and SKIP the window entirely — there is
            // nothing to ask. Back from the numbers window still reaches it.
            if (wizardPage.fEquipmentId <= 0 && !wizardPage._fromSummary
                    && wizardPage.currentStep === "details"
                    && wizardPage._detailsPage === "equipment") {
                var inInv = packages.filter(function(p) { return p.inInventory !== false })
                if (inInv.length === 1) {
                    wizardPage.selectEquipment(inInv[0])
                    wizardPage._detailsPage = "numbers"
                } else if (inInv.length > 1) {
                    var activeId = Settings.dye.activeEquipmentId
                    for (var j = 0; j < inInv.length; j++)
                        if (inInv[j].id === activeId) {
                            wizardPage.selectEquipment(inInv[j])
                            break
                        }
                }
            }
        }
    }

    // Hidden Tr instances for property-bound strings.
    Tr { id: trNone; key: "recipes.composer.none"; fallback: "None"; visible: false }
    Tr { id: trNoBean; key: "recipes.wizard.noBean"; fallback: "No bean"; visible: false }
    Tr { id: trNoTea; key: "recipes.wizard.noTea"; fallback: "No tea"; visible: false }
    Tr { id: trJustHotWater; key: "recipes.wizard.justHotWater"; fallback: "Just hot water — no profile"; visible: false }

    // --- Reusable pieces (composer idiom) -----------------------------------

    component PickerField: ColumnLayout {
        id: pickerField
        property string label: ""
        property string value: ""
        property string placeholder: ""
        signal activated()
        spacing: Theme.scaled(4)
        Label {
            text: pickerField.label
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: Theme.cardBackgroundColor
            border.color: Theme.borderColor
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall
                Label {
                    Layout.fillWidth: true
                    text: pickerField.value !== "" ? pickerField.value : pickerField.placeholder
                    font: Theme.bodyFont
                    color: pickerField.value !== "" ? Theme.textColor : Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Label {
                    text: "→"
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
            }
            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: pickerField.label + ", "
                    + (pickerField.value !== "" ? pickerField.value : pickerField.placeholder)
                accessibleItem: parent
                onAccessibleClicked: pickerField.activated()
            }
        }
    }

    component NumberField: ColumnLayout {
        id: numberField
        property string label: ""
        property alias text: numberInput.text
        property alias input: numberInput
        // Dimmed = a DERIVED value (the non-anchored half of the yield/ratio
        // pair): shown as a consequence, still editable — editing it anchors
        // it (add-yield-ratio-anchor).
        property bool dim: false
        signal edited(string newText)
        spacing: Theme.scaled(4)
        Label {
            text: numberField.label
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        StyledTextField {
            id: numberInput
            Layout.fillWidth: true
            opacity: numberField.dim ? 0.55 : 1.0
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            Accessible.name: numberField.label
            onTextEdited: numberField.edited(text)
        }
    }

    component SectionCard: Rectangle {
        id: sectionCard
        property string title: ""
        // Collapse-to-summary (the "prefilled and optional" treatment): a
        // collapsible card shows only its title + a one-line summary of the
        // current values until tapped — the prefills are ready to save, so
        // the fields stay out of the way unless the user wants them.
        property bool collapsible: false
        property bool expanded: true
        property string summary: ""
        default property alias content: contentColumn.data
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: cardColumn.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.cardRadius
        color: Theme.cardBackgroundColor
        border.color: Theme.borderColor
        border.width: 1
        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Label {
                    visible: sectionCard.title !== ""
                    Layout.fillWidth: true
                    text: sectionCard.title
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                // The single edit glyph, only while collapsed (matching the
                // summary rows) — expanded cards edit in place.
                ColoredIcon {
                    visible: sectionCard.collapsible && !sectionCard.expanded
                    source: "qrc:/icons/edit.svg"
                    iconWidth: Theme.scaled(16)
                    iconHeight: Theme.scaled(16)
                    iconColor: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
            }
            Label {
                visible: sectionCard.collapsible && !sectionCard.expanded
                Layout.fillWidth: true
                text: sectionCard.summary
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }
            ColumnLayout {
                id: contentColumn
                visible: !sectionCard.collapsible || sectionCard.expanded
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
            }
        }
        // Collapsed card: the whole card is the "open it" tap target.
        AccessibleMouseArea {
            anchors.fill: parent
            enabled: sectionCard.collapsible && !sectionCard.expanded
            visible: enabled
            accessibleName: sectionCard.title + ", " + sectionCard.summary + ", "
                + TranslationManager.translate("recipes.wizard.accessible.tapToAdjust", "tap to adjust")
            accessibleItem: sectionCard
            onAccessibleClicked: sectionCard.expanded = true
        }
    }

    // A tap-to-select tile used by the equipment and steam windows: title +
    // optional metadata line, highlighted when selected, emits chosen() on tap.
    component ChoiceTile: Rectangle {
        id: choiceTile
        property string title: ""
        property string meta: ""
        property bool selected: false
        signal chosen()
        width: Math.min(Theme.scaled(240),
            (parent && parent.width > 0) ? parent.width : Theme.scaled(240))
        implicitHeight: choiceTileCol.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.cardRadius
        color: selected ? Qt.alpha(Theme.primaryColor, 0.12) : Theme.cardBackgroundColor
        border.color: selected ? Theme.primaryColor : Theme.borderColor
        border.width: selected ? 2 : 1
        ColumnLayout {
            id: choiceTileCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.scaled(2)
            Label {
                Layout.fillWidth: true
                text: choiceTile.title
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Accessible.ignored: true
            }
            Label {
                visible: choiceTile.meta !== ""
                Layout.fillWidth: true
                text: choiceTile.meta
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }
        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: choiceTile.title
                + (choiceTile.meta !== "" ? ", " + choiceTile.meta : "")
                + (choiceTile.selected
                    ? ", " + TranslationManager.translate("common.selected", "selected") : "")
            accessibleItem: choiceTile
            onAccessibleClicked: choiceTile.chosen()
        }
    }

    // A tappable summary CARD: the same look as a collapsed SectionCard on
    // the details step (title + value summary + ONE pencil glyph), so the
    // summary and details steps share one visual language. Tap anywhere to
    // reopen the owning step.
    component SummaryRow: Rectangle {
        id: summaryRow
        property string label: ""
        property string value: ""
        property string step: ""
        // Which details window this card edits ("equipment"/"numbers"/
        // "steam"/"water"); tapping the card jumps straight to that window.
        property string detailsPage: ""
        // Optional leading photo (Bean card): a shared BeanThumbnail keyed the
        // same way the hero / bean-step tiles are.
        property bool showThumbnail: false
        property string thumbnailKey: ""
        property string thumbnailLink: ""
        property string thumbnailIcon: "qrc:/icons/coffeebeans.svg"
        property string thumbnailFallback: ""
        // Optional extra controls in the header, left of the edit glyph (the
        // Profile card's (i)/KB buttons). They keep their own tap areas, which
        // win over the card-wide select target below (z:-1 — the profile-tile
        // pattern).
        property Component headerActions: null
        // Optional body content that REPLACES the value Label (the Equipment
        // card's EquipmentSummary). When null, `value` renders as text.
        property Component contentComponent: null
        // Announced value for accessibility when contentComponent is used.
        property string accessibleValue: value
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: summaryRowBody.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.cardRadius
        color: Theme.cardBackgroundColor
        border.color: Theme.borderColor
        border.width: 1
        RowLayout {
            id: summaryRowBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium
            BeanThumbnail {
                visible: summaryRow.showThumbnail
                Layout.preferredWidth: Theme.scaled(56)
                Layout.preferredHeight: Theme.scaled(56)
                Layout.alignment: Qt.AlignTop
                imageKey: summaryRow.thumbnailKey
                link: summaryRow.thumbnailLink
                fallbackName: summaryRow.thumbnailFallback
                iconSource: summaryRow.thumbnailIcon
                iconSize: Theme.scaled(24)
                imageSourceSize: Theme.scaled(120)
            }
            ColumnLayout {
                id: summaryRowColumn
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    Label {
                        text: summaryRow.label
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                    Item { Layout.fillWidth: true }
                    Loader {
                        active: summaryRow.headerActions !== null
                        visible: active
                        Layout.alignment: Qt.AlignVCenter
                        sourceComponent: summaryRow.headerActions
                    }
                    // Decorative edit glyph. Must be mouse-transparent: the
                    // card's tap target sits at z:-1 (so header-action buttons
                    // win), so a click-absorbing ColoredIcon here would swallow
                    // taps that land on the pencil and never open the step —
                    // the whole card was tappable EXCEPT the pencil (#1606).
                    // ThemedIcon draws the tinted SVG without grabbing clicks,
                    // so a tap on the pencil falls through to the card handler.
                    ThemedIcon {
                        source: "qrc:/icons/edit.svg"
                        iconSize: Theme.scaled(16)
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    active: summaryRow.contentComponent !== null
                    visible: active
                    sourceComponent: summaryRow.contentComponent
                }
                Label {
                    visible: summaryRow.contentComponent === null
                    Layout.fillWidth: true
                    text: summaryRow.value
                    font: Theme.bodyFont
                    color: Theme.textColor
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }
            }
        }
        // Card-wide select target: sits UNDER the header-action buttons (z:-1)
        // so their taps win, while a tap anywhere else opens the step.
        AccessibleMouseArea {
            anchors.fill: parent
            z: -1
            accessibleName: summaryRow.label + ", " + summaryRow.accessibleValue
            accessibleItem: parent
            onAccessibleClicked: {
                if (summaryRow.step === "details" && summaryRow.detailsPage !== "")
                    wizardPage._detailsPage = summaryRow.detailsPage
                wizardPage.openStep(summaryRow.step)
            }
        }
    }

    // --- Page body -----------------------------------------------------------

    KeyboardAwareContainer {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        textFields: [nameField, doseField.input, yieldField.input,
                     profileSearchField]

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.standardMargin
            anchors.rightMargin: Theme.standardMargin
            spacing: Theme.spacingMedium

            // Breadcrumb chips during the creation walk: selections so far,
            // tappable to reopen the step. Hidden on the summary (which has
            // its own rows).
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                visible: wizardPage.currentStep !== "summary"
                spacing: Theme.spacingSmall

                Repeater {
                    model: [
                        { step: "drink", value: wizardPage.fDrinkType !== ""
                            ? wizardPage.drinkTypeLabel(wizardPage.fDrinkType) : "" },
                        { step: "bean", value: wizardPage.hasBean
                            ? (wizardPage.fCoffee !== "" ? wizardPage.fCoffee : wizardPage.fRoaster) : "" },
                        { step: "profile", value: wizardPage.fProfileTitle }
                    ]
                    delegate: Rectangle {
                        visible: modelData.value !== "" && wizardPage.currentStep !== modelData.step
                        radius: height / 2
                        color: Theme.cardBackgroundColor
                        border.color: Theme.borderColor
                        border.width: 1
                        implicitHeight: Theme.scaled(34)
                        implicitWidth: chipLabel.implicitWidth + 2 * Theme.spacingMedium
                        Label {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: modelData.value
                            font: Theme.captionFont
                            color: Theme.textColor
                            Accessible.ignored: true
                        }
                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate(
                                "recipes.wizard.accessible.chip", "Change %1").arg(modelData.value)
                            accessibleItem: parent
                            onAccessibleClicked: wizardPage.openStep(modelData.step)
                        }
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: ["drink", "bean", "profile", "details", "summary"]
                    .indexOf(wizardPage.currentStep)

                // ===== Step 1: drink type =====
                ColumnLayout {
                    spacing: Theme.spacingMedium
                    Label {
                        Layout.topMargin: Theme.spacingMedium
                        text: TranslationManager.translate("recipes.wizard.chooseDrink", "What are you making?")
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium
                        Repeater {
                            model: ["espresso", "latte", "filter", "americano", "long_black", "tea"]
                            delegate: Rectangle {
                                radius: Theme.cardRadius
                                color: Theme.cardBackgroundColor
                                border.color: wizardPage.fDrinkType === modelData
                                    ? Theme.primaryColor : Theme.borderColor
                                border.width: wizardPage.fDrinkType === modelData ? 2 : 1
                                implicitWidth: Theme.scaled(170)
                                implicitHeight: Theme.scaled(120)
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.spacingSmall
                                    ThemedIcon {
                                        Layout.alignment: Qt.AlignHCenter
                                        source: wizardPage.drinkTypeIcon(modelData)
                                        iconSize: Theme.scaled(44)
                                        color: Theme.textColor
                                        Accessible.ignored: true
                                    }
                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: wizardPage.drinkTypeLabel(modelData)
                                        font: Theme.bodyFont
                                        color: Theme.textColor
                                        Accessible.ignored: true
                                    }
                                }
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: wizardPage.drinkTypeLabel(modelData)
                                    accessibleItem: parent
                                    onAccessibleClicked: wizardPage.selectDrinkType(modelData)
                                }
                            }
                        }
                    }
                }

                // ===== Step 2: bean (kind-filtered) =====
                ColumnLayout {
                    spacing: Theme.spacingSmall
                    // Kind-filtered open bags (tea drinks list tea bags only).
                    readonly property var kindBags: {
                        var kind = wizardPage.activeTemplate.bagKind
                        var out = []
                        for (var i = 0; i < wizardPage._bags.length; ++i) {
                            var b = wizardPage._bags[i]
                            var bKind = String(b.kind || "") === "tea" ? "tea" : "coffee"
                            if (bKind === kind)
                                out.push(b)
                        }
                        return out
                    }
                    Label {
                        Layout.topMargin: Theme.spacingMedium
                        text: wizardPage.isTeaDrink
                            ? TranslationManager.translate("recipes.wizard.chooseTea", "Which tea?")
                            : TranslationManager.translate("recipes.wizard.chooseBean", "Which beans?")
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    // Empty-inventory hint: the skip row alone reads like the
                    // inventory vanished — say WHY the list is empty and where
                    // bags of tea (or coffee) come from.
                    Label {
                        visible: parent.kindBags.length === 0
                        Layout.fillWidth: true
                        text: wizardPage.isTeaDrink
                            ? TranslationManager.translate("recipes.wizard.noTeaBags",
                                  "No bags of tea in your inventory yet — add one below, or continue without one.")
                            : TranslationManager.translate("recipes.wizard.noCoffeeBags",
                                  "No open bags in your inventory yet — add one below, or continue without one.")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                    // Bag TILE grid (recipes-bag-links-ui-polish): one tile per
                    // open bag — photo, roaster caption, coffee name, roast
                    // date/age — so two bags of the same bean are visibly
                    // distinct choices (no dedup; the tile IS the bag). "Add a
                    // new coffee…" and "No bean" render as ghost tiles at the
                    // end of the grid.
                    Flickable {
                        id: bagGridFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: bagGrid.implicitHeight + Theme.scaled(16)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        readonly property var gridModel:
                            parent.kindBags.concat([{ isAddNew: true }, { isNone: true }])

                        // Roast date plus age ("2026-06-12 · 28d"); empty when
                        // the bag states no roast date.
                        function roastAgeLine(bag) {
                            var date = String(bag.roastDate || "")
                            if (date === "") return ""
                            var parsed = Date.fromLocaleString(Qt.locale("C"), date, "yyyy-MM-dd")
                            if (isNaN(parsed.getTime())) return date
                            var days = Math.floor((Date.now() - parsed.getTime()) / 86400000)
                            return days >= 0 ? date + " · " + days + "d" : date
                        }

                        Flow {
                            id: bagGrid
                            width: bagGridFlick.width
                            spacing: Theme.spacingMedium

                            Repeater {
                                model: bagGridFlick.gridModel
                                delegate: Rectangle {
                                    id: bagTile
                                    readonly property bool isGhost: modelData.isAddNew === true || modelData.isNone === true
                                    readonly property bool isSelected: !isGhost && wizardPage.fBagId > 0
                                        && modelData.id === wizardPage.fBagId
                                    readonly property string tileTitle: modelData.isAddNew
                                        ? (wizardPage.isTeaDrink
                                            ? TranslationManager.translate("recipes.wizard.addNewTea", "Add a new tea…")
                                            : TranslationManager.translate("recipes.wizard.addNewCoffee", "Add a new coffee…"))
                                        : modelData.isNone
                                            ? (wizardPage.isTeaDrink ? trNoTea.text : trNoBean.text)
                                            : (modelData.coffeeName || modelData.roasterName || "")
                                    width: Theme.scaled(170)
                                    height: Theme.scaled(190)
                                    radius: Theme.cardRadius
                                    color: isGhost ? "transparent" : Theme.cardBackgroundColor
                                    border.color: isSelected ? Theme.primaryColor : "transparent"
                                    border.width: isSelected ? 2 : 0

                                    // Ghost tiles: dashed border, same size.
                                    Canvas {
                                        anchors.fill: parent
                                        visible: bagTile.isGhost
                                        onPaint: {
                                            var ctx = getContext("2d")
                                            ctx.reset()
                                            ctx.strokeStyle = Theme.borderColor
                                            ctx.lineWidth = 1
                                            ctx.setLineDash([6, 5])
                                            var r = Theme.cardRadius
                                            ctx.beginPath()
                                            ctx.roundedRect(0.5, 0.5, width - 1, height - 1, r, r)
                                            ctx.stroke()
                                        }
                                        Accessible.ignored: true
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        visible: !bagTile.isGhost && !bagTile.isSelected
                                        color: "transparent"
                                        radius: parent.radius
                                        border.color: Theme.borderColor
                                        border.width: 1
                                    }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: Theme.spacingSmall
                                        spacing: Theme.scaled(4)

                                        // Bag photo — the shared BeanThumbnail
                                        // cache widget (canonical id key, else
                                        // the bag's own "bag-<id>" key).
                                        BeanThumbnail {
                                            visible: !bagTile.isGhost
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: Theme.scaled(90)
                                            imageKey: {
                                                if (bagTile.isGhost) return ""
                                                return modelData.beanBaseId && String(modelData.beanBaseId).length > 0
                                                    ? String(modelData.beanBaseId) : "bag-" + modelData.id
                                            }
                                            fallbackName: bagTile.isGhost ? "" : (modelData.coffeeName || "")
                                            link: {
                                                if (bagTile.isGhost || !modelData.beanBaseData
                                                    || String(modelData.beanBaseData).length === 0)
                                                    return ""
                                                try { return JSON.parse(modelData.beanBaseData).link || "" } catch (e) { return "" }
                                            }
                                            iconSource: wizardPage.isTeaDrink
                                                ? "qrc:/icons/tea.svg" : "qrc:/icons/coffeebeans.svg"
                                            iconSize: Theme.scaled(28)
                                            imageSourceSize: Theme.scaled(180)
                                        }
                                        ColoredIcon {
                                            visible: bagTile.isGhost
                                            Layout.alignment: Qt.AlignHCenter
                                            Layout.topMargin: Theme.scaled(30)
                                            source: modelData.isAddNew ? "qrc:/icons/plus.svg"
                                                : (wizardPage.isTeaDrink ? "qrc:/icons/tea.svg" : "qrc:/icons/coffeebeans.svg")
                                            iconWidth: Theme.scaled(32)
                                            iconHeight: Theme.scaled(32)
                                            iconColor: modelData.isAddNew ? Theme.primaryColor : Theme.textSecondaryColor
                                            Accessible.ignored: true
                                        }
                                        Label {
                                            visible: !bagTile.isGhost && (modelData.roasterName || "") !== ""
                                            Layout.fillWidth: true
                                            text: modelData.roasterName || ""
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.alignment: bagTile.isGhost ? Qt.AlignHCenter : Qt.AlignLeft
                                            horizontalAlignment: bagTile.isGhost ? Text.AlignHCenter : Text.AlignLeft
                                            text: bagTile.tileTitle
                                            font: Theme.bodyFont
                                            color: modelData.isAddNew ? Theme.primaryColor : Theme.textColor
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }
                                        Label {
                                            visible: !bagTile.isGhost && bagGridFlick.roastAgeLine(modelData) !== ""
                                            Layout.fillWidth: true
                                            text: bagTile.isGhost ? "" : bagGridFlick.roastAgeLine(modelData)
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                            elide: Text.ElideRight
                                            Accessible.ignored: true
                                        }
                                        Item { Layout.fillHeight: true }
                                    }

                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        accessibleName: bagTile.isGhost ? bagTile.tileTitle
                                            : (((modelData.roasterName || "") + " " + (modelData.coffeeName || "")).trim()
                                               + (bagGridFlick.roastAgeLine(modelData) !== ""
                                                   ? ", " + bagGridFlick.roastAgeLine(modelData) : ""))
                                        accessibleItem: bagTile
                                        onAccessibleClicked: {
                                            if (modelData.isAddNew) {
                                                // Coffee: the search-first flow (the one
                                                // they want may be in Bean Base /
                                                // history). Tea: the tea entry (form-
                                                // first when none exist).
                                                if (wizardPage.isTeaDrink)
                                                    wizardBeansDialog.openTeaEntry(bagGridFlick.gridModel.length > 2)
                                                else {
                                                    wizardBeansDialog.bagKind = "coffee"
                                                    wizardBeansDialog.open()
                                                }
                                                return
                                            }
                                            wizardPage.selectBean(modelData.isNone ? null : modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ===== Step 3: profile (filtered + ranked) =====
                ColumnLayout {
                    spacing: Theme.spacingSmall
                    Label {
                        Layout.topMargin: Theme.spacingMedium
                        text: TranslationManager.translate("recipes.wizard.chooseProfile", "Which profile?")
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    StyledTextField {
                        id: profileSearchField
                        Layout.fillWidth: true
                        placeholder: TranslationManager.translate("profileselector.search", "Search profiles…")
                        Accessible.name: placeholderText
                        onTextChanged: {
                            wizardPage.profileFilter = text
                            wizardPage.rebuildProfileModel()
                        }
                    }
                    // Profile tile GRID (same visual language as the drink
                    // and bag steps): every profile is a tile with its real
                    // temperature and target yield; the ranked tiers carry
                    // the recommendation reason as an on-tile chip. Headers
                    // span the full row. Metadata comes from the catalog
                    // cache (ProfileInfo) — no per-tile file reads.
                    Flickable {
                        id: profileGridFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: profileGrid.implicitHeight + Theme.scaled(16)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        Flow {
                            id: profileGrid
                            width: profileGridFlick.width
                            spacing: Theme.spacingSmall

                            readonly property real tileWidth: {
                                var min = Theme.scaled(230)
                                var columns = Math.max(1, Math.floor(width / min))
                                return (width - (columns - 1) * spacing) / columns
                            }

                            Repeater {
                                model: wizardPage.profileModel
                                delegate: Loader {
                                    sourceComponent: modelData.isHeader ? profileHeader : profileTile
                                    property var row: modelData
                                    Component {
                                        id: profileHeader
                                        Label {
                                            width: profileGrid.width
                                            text: row.title
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                            topPadding: Theme.spacingMedium
                                            bottomPadding: Theme.scaled(2)
                                            Accessible.role: Accessible.Heading
                                            Accessible.name: text
                                        }
                                    }
                                    Component {
                                        id: profileTile
                                        Rectangle {
                                            id: tileRect
                                            width: profileGrid.tileWidth
                                            height: Theme.scaled(124)
                                            radius: Theme.cardRadius
                                            color: Theme.cardBackgroundColor
                                            border.color: wizardPage.fProfileTitle === row.title
                                                ? Theme.primaryColor : Theme.borderColor
                                            border.width: wizardPage.fProfileTitle === row.title ? 2 : 1
                                            readonly property string metaLine: {
                                                var parts = []
                                                if ((row.tempC || 0) > 0)
                                                    parts.push(Theme.formatTemperature(row.tempC, 0))
                                                if ((row.yieldG || 0) > 0)
                                                    parts.push("→ " + Number(row.yieldG).toFixed(0) + "g")
                                                return parts.join(" · ")
                                            }
                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: Theme.spacingSmall
                                                spacing: Theme.scaled(4)
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: row.title
                                                    font: Theme.bodyFont
                                                    color: Theme.textColor
                                                    wrapMode: Text.WordWrap
                                                    maximumLineCount: 2
                                                    elide: Text.ElideRight
                                                    Accessible.ignored: true
                                                }
                                                Label {
                                                    visible: tileRect.metaLine !== ""
                                                    text: tileRect.metaLine
                                                    font: Theme.captionFont
                                                    color: Theme.textSecondaryColor
                                                    Accessible.ignored: true
                                                }
                                                Item { Layout.fillHeight: true }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: Theme.spacingSmall
                                                    // The recommendation reason rides its
                                                    // tile as a chip — never detached text.
                                                    Rectangle {
                                                        visible: row.reason !== ""
                                                        radius: height / 2
                                                        color: Qt.alpha(Theme.primaryColor, 0.15)
                                                        implicitHeight: reasonChip.implicitHeight + Theme.scaled(6)
                                                        implicitWidth: Math.min(
                                                            reasonChip.implicitWidth + Theme.scaled(14),
                                                            tileRect.width - Theme.scaled(90))
                                                        Label {
                                                            id: reasonChip
                                                            anchors.centerIn: parent
                                                            width: Math.min(implicitWidth,
                                                                parent.width - Theme.scaled(10))
                                                            text: row.reason
                                                            font: Theme.captionFont
                                                            color: Theme.primaryColor
                                                            elide: Text.ElideRight
                                                            Accessible.ignored: true
                                                        }
                                                    }
                                                    Item { Layout.fillWidth: true }
                                                    // The same two info affordances the
                                                    // profile page offers: the sparkle KB
                                                    // popup and the Profile Info page.
                                                    ColoredIcon {
                                                        visible: row.hasKb === true
                                                        source: "qrc:/icons/sparkle.svg"
                                                        iconWidth: Theme.scaled(16)
                                                        iconHeight: Theme.scaled(16)
                                                        iconColor: Theme.textSecondaryColor
                                                        Accessible.ignored: true
                                                        AccessibleMouseArea {
                                                            anchors.fill: parent
                                                            anchors.margins: Theme.scaled(-6)
                                                            accessibleName: TranslationManager.translate(
                                                                "profileselector.accessible.view_knowledge",
                                                                "View AI knowledge base")
                                                            accessibleItem: parent
                                                            onAccessibleClicked:
                                                                wizardKnowledgeDialog.openFor(row.title)
                                                        }
                                                    }
                                                    ProfileInfoButton {
                                                        Layout.preferredWidth: Theme.scaled(26)
                                                        Layout.preferredHeight: Theme.scaled(26)
                                                        buttonSize: Theme.scaled(26)
                                                        profileFilename: row.name
                                                        profileName: row.title
                                                        onClicked: pageStack.push(
                                                            Qt.resolvedUrl("ProfileInfoPage.qml"),
                                                            { profileFilename: row.name,
                                                              profileName: row.title })
                                                    }
                                                }
                                            }
                                            // The tile-wide select target sits UNDER the
                                            // info buttons (z: -1) so their own tap areas
                                            // win — same pattern as the recipe cards.
                                            AccessibleMouseArea {
                                                anchors.fill: parent
                                                z: -1
                                                accessibleName: row.title
                                                    + (tileRect.metaLine !== "" ? ", " + tileRect.metaLine : "")
                                                    + (row.reason !== "" ? ", " + row.reason : "")
                                                accessibleItem: tileRect
                                                onAccessibleClicked: wizardPage.selectProfile(row)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // Fixed row (tea only): a profile-less hot-water recipe.
                    ItemDelegate {
                        visible: wizardPage.isTeaDrink
                        Layout.fillWidth: true
                        contentItem: Label {
                            text: trJustHotWater.text
                            font: Theme.bodyFont
                            color: Theme.textColor
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: trJustHotWater.text
                        onClicked: wizardPage.selectJustHotWater()
                    }
                }

                // ===== Step 4: details (drink-type specific) =====
                // A PINNED header row (the "everything is optional" caption
                // + the Continue button) sits above the scrolling cards, so
                // Continue is always visible — expanding a card never pushes
                // it off screen. Section cards flow into two columns on
                // landscape widths; below the threshold they stack. Input
                // controls are sized to their content, never stretched to
                // the page width.
                ColumnLayout {
                    spacing: Theme.spacingSmall

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSmall
                        spacing: Theme.spacingMedium
                        Label {
                            Layout.fillWidth: true
                            // Edit/clone/promote (entered at the summary):
                            // this step commits directly — never send the
                            // user hunting for a second Save press.
                            text: wizardPage._enteredAtSummary
                                ? TranslationManager.translate("recipes.wizard.detailsOptionalEdit",
                                      "Everything here is optional — adjust it and Save.")
                                : TranslationManager.translate("recipes.wizard.detailsWalk",
                                      "Everything here is optional and prefilled. "
                                      + "Adjust it if you like, then continue.")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                        AccessibleButton {
                            visible: wizardPage._enteredAtSummary
                            Layout.alignment: Qt.AlignVCenter
                            text: TranslationManager.translate("common.cancel", "Cancel")
                            accessibleName: TranslationManager.translate("recipes.composer.accessible.cancel", "Cancel recipe editing")
                            onClicked: wizardPage.requestExit()
                        }
                        AccessibleButton {
                            visible: wizardPage._enteredAtSummary
                            Layout.alignment: Qt.AlignVCenter
                            primary: true
                            enabled: wizardPage.canSave
                            text: TranslationManager.translate("common.save", "Save")
                            accessibleName: TranslationManager.translate("recipes.composer.accessible.save", "Save the recipe")
                            onClicked: wizardPage.save()
                        }
                        AccessibleButton {
                            visible: !wizardPage._enteredAtSummary
                            Layout.alignment: Qt.AlignVCenter
                            primary: true
                            // A window opened FROM the summary returns there
                            // ("Done"); in the creation walk, Continue steps to
                            // the next window and the LAST one says "Review" —
                            // the next stop is the named, WYSIWYG summary with
                            // Save, so it never reads like a commit.
                            text: wizardPage._fromSummary
                                ? TranslationManager.translate("common.done", "Done")
                                : (wizardPage.isLastDetailsPage
                                    ? TranslationManager.translate("recipes.wizard.review", "Review")
                                    : TranslationManager.translate("common.continue", "Continue"))
                            accessibleName: text
                            onClicked: wizardPage.detailsContinue()
                        }
                    }

                    // Pinned with the header (matching the summary step) so a
                    // save failure from the details step is never hidden.
                    Label {
                        visible: wizardPage.errorMessage !== ""
                        Layout.fillWidth: true
                        text: wizardPage.errorMessage
                        font: Theme.bodyFont
                        color: Theme.errorColor
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: detailsColumn.implicitHeight + Theme.scaled(24)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    GridLayout {
                        id: detailsColumn
                        width: parent.width
                        // One facet per window now, so a single column keeps each
                        // window's card(s) full-width — no half-empty row from the
                        // old 2-column all-in-one step. The numbers window's two
                        // cards (dose/yield/temp + grind) simply stack.
                        columns: 1
                        columnSpacing: Theme.spacingMedium
                        rowSpacing: Theme.spacingMedium

                        SectionCard {
                            id: numbersCard
                            // The dose/yield/temp/grind window — its own screen,
                            // so the fields show expanded (no collapse chrome).
                            visible: wizardPage._detailsPage === "numbers"
                            collapsible: false
                            // Concrete title: name the fields, not "numbers".
                            title: wizardPage.isTeaDrink
                                ? TranslationManager.translate("recipes.wizard.sectionLeafTemp", "Leaf & temperature")
                                : TranslationManager.translate("recipes.wizard.sectionDoseYield", "Dose, yield & temperature")
                            // The wizard says where the prefills came from and
                            // why the user might change them — never a wall of
                            // unexplained values.
                            Label {
                                Layout.fillWidth: true
                                text: wizardPage.numbersHint
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }
                            // WRAPS rather than squeezing (add-yield-ratio-anchor):
                            // the Ratio control made this a FOUR-control row, and
                            // dose+yield+ratio+temp simply do not fit side by side
                            // in a narrow card — the layout paid for it by crushing
                            // whichever child had the smallest minimum, clipping
                            // "36.0" to "6.0". A grid drops to 2x2 instead, and only
                            // goes single-row where there is genuinely room.
                            // Bound to the CARD's width, never this grid's own, or
                            // the columns binding would feed back into the layout.
                            // Invisible children take no cell, so the tea variants
                            // (no ratio; no yield for hot-water tea) reflow for free.
                            GridLayout {
                                Layout.fillWidth: true
                                columns: numbersCard.width >= Theme.scaled(620) ? 4 : 2
                                columnSpacing: Theme.spacingMedium
                                rowSpacing: Theme.spacingSmall
                                NumberField {
                                    id: doseField
                                    Layout.preferredWidth: Theme.scaled(120)
                                    label: wizardPage.isTeaDrink
                                        ? TranslationManager.translate("recipes.wizard.leafDose", "Leaf (g)")
                                        : TranslationManager.translate("recipes.composer.doseLabel", "Dose (g)")
                                    onEdited: {
                                        wizardPage._detailsUserEdited = true
                                        // A dose edit re-derives the NON-anchored
                                        // yield field; the anchor never moves.
                                        wizardPage.syncDerivedYieldFields()
                                    }
                                }
                                // Yield anchor pair (add-yield-ratio-anchor): a
                                // three-state choice — nothing, a fixed yield, or a
                                // ratio of the dose. Whichever was LAST EDITED is the
                                // anchor (stored); the other shows the derived value,
                                // dimmed, never blank. Clearing the anchored field
                                // clears the yield entirely (mode "none").
                                NumberField {
                                    id: yieldField
                                    visible: !wizardPage.isHotWaterTea
                                    Layout.preferredWidth: Theme.scaled(120)
                                    label: TranslationManager.translate("recipes.composer.yieldLabel", "Yield (g)")
                                    dim: wizardPage.fYieldMode === "ratio"
                                    onEdited: function(newText) {
                                        wizardPage._detailsUserEdited = true
                                        wizardPage.fYieldMode = (parseFloat(newText) || 0) > 0 ? "absolute" : "none"
                                        if (wizardPage.fYieldMode === "none")
                                            ratioField.text = ""
                                        wizardPage.syncDerivedYieldFields()
                                    }
                                }
                                // Ratio: a STYLE choice, picked from the named
                                // chooser (Ristretto / Normale / Lungo — the
                                // user's own configured presets, with the
                                // descriptions that explain them). Designing a
                                // drink is exactly where that vocabulary
                                // belongs. Same framed, edit-icon'd,
                                // tap-to-choose control as Brew Settings' Ratio
                                // row — one idiom in both places; the Yield
                                // field beside it stays the precise-grams
                                // control (the two are one anchor, two views).
                                // Renders "1:2.5" rather than a bare 2.5, and
                                // "Pick…" when unset so the presets are
                                // discoverable.
                                ColumnLayout {
                                    id: ratioField
                                    visible: !wizardPage.isHotWaterTea && !wizardPage.isTeaDrink
                                    // The typed-value API the rest of the page
                                    // uses (syncDerivedYieldFields, save map,
                                    // applyRecipeMap) — kept so the ratio's
                                    // storage path is unchanged by the control swap.
                                    property string text: ""
                                    readonly property double _r: parseFloat(text) || 0
                                    Layout.preferredWidth: Theme.scaled(120)
                                    // A minimum is load-bearing here: the tap
                                    // control below is a bare Rectangle
                                    // (implicitWidth 0), so with this row
                                    // crowded the layout would shrink it to
                                    // nothing while the text-field columns
                                    // held their size — the value elided to
                                    // "…" and the ratio became unreadable.
                                    // Sized to fit "1:2.5" + the edit icon.
                                    Layout.minimumWidth: Theme.scaled(96)
                                    spacing: Theme.scaled(4)
                                    Label {
                                        text: TranslationManager.translate("recipes.composer.ratioLabel", "Ratio")
                                        font: Theme.captionFont
                                        color: Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Theme.scaled(40)
                                        radius: Theme.scaled(8)
                                        color: wizardRatioMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : "transparent"
                                        border.width: 1
                                        border.color: Theme.textSecondaryColor
                                        opacity: wizardPage.fYieldMode === "ratio" ? 1.0 : 0.55
                                        Accessible.role: Accessible.Button
                                        Accessible.name: TranslationManager.translate(
                                            "recipes.composer.chooseNamedRatio", "Choose a named brew ratio")
                                            + (ratioField._r > 0 ? ". 1:" + ratioField._r.toFixed(1) : "")
                                        Accessible.focusable: true
                                        Accessible.onPressAction: wizardRatioPicker.open()
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: Theme.spacingSmall
                                            anchors.rightMargin: Theme.spacingSmall
                                            spacing: Theme.scaled(2)
                                            Text {
                                                Layout.fillWidth: true
                                                text: ratioField._r > 0
                                                    ? "1:" + ratioField._r.toFixed(1)
                                                    : TranslationManager.translate("recipes.composer.pickRatio", "Pick…")
                                                font: Theme.bodyFont
                                                color: ratioField._r > 0 ? Theme.textColor : Theme.textSecondaryColor
                                                horizontalAlignment: Text.AlignHCenter
                                                elide: Text.ElideRight
                                                Accessible.ignored: true
                                            }
                                            ColoredIcon {
                                                source: "qrc:/icons/edit.svg"
                                                iconWidth: Theme.scaled(14)
                                                iconHeight: Theme.scaled(14)
                                                iconColor: Theme.primaryColor
                                                Accessible.ignored: true
                                            }
                                        }
                                        MouseArea {
                                            id: wizardRatioMa
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: wizardRatioPicker.open()
                                        }
                                    }
                                }
                                // Coffee drinks: temperature as an OFFSET on the
                                // profile (shot-plan semantics). Tea: absolute.
                                ColumnLayout {
                                    visible: !wizardPage.isTeaDrink
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(4)
                                    Label {
                                        text: TranslationManager.translate("recipes.composer.tempOffsetLabel", "Temp offset")
                                        font: Theme.captionFont
                                        color: Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                    ValueInput {
                                        // Sized to content — a temp stepper must
                                        // never span the page width.
                                        Layout.preferredWidth: Theme.scaled(190)
                                        enabled: wizardPage.fProfileTempC > 0
                                        readonly property real displayDelta: Theme.cDeltaToDisplay(wizardPage.fTempDeltaC)
                                        value: displayDelta
                                        from: wizardPage.fProfileTempC > 0
                                            ? Theme.cDeltaToDisplay(70 - wizardPage.fProfileTempC) : -10
                                        to: wizardPage.fProfileTempC > 0
                                            ? Theme.cDeltaToDisplay(100 - wizardPage.fProfileTempC) : 10
                                        stepSize: 1
                                        decimals: 0
                                        suffix: "°"
                                        displayText: (displayDelta > 0 ? "+" : "") + displayDelta.toFixed(0) + "°"
                                        valueColor: Math.abs(wizardPage.fTempDeltaC) > 0.1 ? Theme.temperatureColor : Theme.textSecondaryColor
                                        accentColor: Theme.temperatureColor
                                        accessibleName: TranslationManager.translate("recipes.composer.tempOffsetAccessible", "Brew temperature offset")
                                        onValueModified: function(newValue) {
                                            wizardPage.fTempDeltaC = Theme.displayToCDelta(newValue)
                                            wizardPage._detailsUserEdited = true
                                        }
                                    }
                                    // The temperature the offset RESOLVES TO — issue
                                    // #1604: the stepper alone shows "+2°" and a user
                                    // can't dial to a target without knowing the
                                    // profile's default. Reads through the same
                                    // formatter the brew-settings Temp Delta uses, so
                                    // both surfaces render identically (at most two
                                    // temps: "94°C", "80 · 94°C", or "80…96°C").
                                    // temperatureDisplayForSteps — NOT temperatureDisplay
                                    // — because the wizard edits an arbitrarily selected
                                    // profile, not ProfileManager's m_currentProfile;
                                    // fProfileStepTemps carries the picked profile's
                                    // frames. baselineShift == fTempDeltaC shifts every
                                    // frame to the eventual value; the same delta feeds
                                    // the signed tag.
                                    Text {
                                        visible: wizardPage.fProfileTempC > 0
                                        Layout.preferredWidth: Theme.scaled(190)
                                        text: {
                                            // The unit is read in C++ (not a QML-
                                            // capturable dependency) — touch it so the
                                            // readout re-renders on a °C/°F switch.
                                            void(Settings.app.temperatureUnit)
                                            return "→ " + ProfileManager.temperatureDisplayForSteps(
                                                wizardPage.fProfileStepTemps,
                                                wizardPage.fProfileTempC,
                                                Math.abs(wizardPage.fTempDeltaC) > 0.05,
                                                wizardPage.fProfileTempC + wizardPage.fTempDeltaC,
                                                wizardPage.fTempDeltaC)
                                        }
                                        font.family: Theme.bodyFont.family
                                        font.pixelSize: Theme.scaled(13)
                                        font.italic: true
                                        elide: Text.ElideRight
                                        color: Math.abs(wizardPage.fTempDeltaC) > 0.1
                                            ? Theme.temperatureColor : Theme.textSecondaryColor
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }
                                NumberField {
                                    // Portafilter tea only: this edits an ABSOLUTE steep
                                    // temperature that converts to the stored offset against
                                    // the profile. Hot-water tea has no profile — its
                                    // temperature belongs to the water vessel (recipe-model:
                                    // the vessel is the single source of amount/temperature/
                                    // flow; a separate field here stored a value activation
                                    // never used). Disabled when the profile can't be
                                    // resolved: without an anchor the save path preserves the
                                    // stored offset untouched, so the field must not accept
                                    // input it would silently discard.
                                    visible: wizardPage.isTeaDrink && !wizardPage.isHotWaterTea
                                    enabled: wizardPage.fProfileTempC > 0
                                    Layout.preferredWidth: Theme.scaled(120)
                                    label: TranslationManager.translate("recipes.wizard.teaTemp", "Temp (°C)")
                                    text: wizardPage.fTeaTempC > 0 ? String(Math.round(wizardPage.fTeaTempC)) : ""
                                    onEdited: function(newText) {
                                        wizardPage.fTeaTempC = parseFloat(newText) || 0
                                        wizardPage._detailsUserEdited = true
                                    }
                                }
                                // (The RowLayout's trailing fillWidth spacer is
                                // gone: in a grid it would consume a cell and
                                // shove the wrap point one control early.)
                            }
                            // The way back after editing: revert to the
                            // profile's own numbers (no revert exists on the
                            // other cards because they have no canonical
                            // source — grind's revert is the Override toggle).
                            AccessibleButton {
                                visible: wizardPage.fProfileTitle !== ""
                                Layout.alignment: Qt.AlignRight
                                height: Theme.scaled(36)
                                _customFontSize: Theme.captionFont.pixelSize
                                leftPadding: Theme.scaled(10)
                                rightPadding: Theme.scaled(10)
                                text: TranslationManager.translate("recipes.wizard.resetToProfile", "Reset to profile")
                                accessibleName: TranslationManager.translate(
                                    "recipes.wizard.accessible.resetToProfile",
                                    "Reset dose, yield, and temperature to the profile's values")
                                onClicked: wizardPage.resetNumbersToProfile()
                            }
                        }

                        // Grind: coffee drinks only (tea has nothing to grind).
                        // Equipment BEFORE grind: the rpm field's visibility
                        // depends on the chosen grinder's rpm capability, so
                        // the user sees (and can correct) the equipment choice
                        // before reaching the field it gates
                        // (fix-recipe-grind-integrity). Prefilled from the
                        // per-drink-type default; a row, not a step (it rarely
                        // changes once set).
                        SectionCard {
                            // The equipment window — first in the walk.
                            visible: wizardPage._detailsPage === "equipment"
                            title: TranslationManager.translate("recipes.composer.sectionEquipment", "Equipment")
                            Label {
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipes.wizard.equipmentHint",
                                      "Prefilled from the gear you last used for this drink — pick a "
                                      + "different package only if this recipe uses another grinder or basket.")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }
                            // The in-inventory packages as inline, tap-to-select
                            // tiles (no dialog): the current one is highlighted,
                            // "None" clears the link, and picking one moves on.
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                Repeater {
                                    model: wizardPage.equipmentTileModel()
                                    delegate: ChoiceTile {
                                        readonly property bool isNone: modelData.isNone === true
                                        readonly property string grinder:
                                            ((modelData.grinderBrand || "") + " " + (modelData.grinderModel || "")).trim()
                                        readonly property string basket:
                                            ((modelData.basketBrand || "") + " " + (modelData.basketModel || "")).trim()
                                        title: isNone ? trNone.text
                                            : (modelData.name || grinder || basket)
                                        meta: {
                                            if (isNone) return ""
                                            var parts = []
                                            if (grinder !== "" && grinder !== title) parts.push(grinder)
                                            if (basket !== "") parts.push(basket)
                                            return parts.join(" · ")
                                        }
                                        selected: isNone
                                            ? wizardPage.fEquipmentId <= 0
                                            : modelData.id === wizardPage.fEquipmentId
                                        onChosen: {
                                            wizardPage.selectEquipment(isNone ? null : modelData)
                                            // Picking a tile is the choice — move
                                            // on without a second Continue tap.
                                            wizardPage.detailsContinue()
                                        }
                                    }
                                }
                            }
                        }

                        SectionCard {
                            id: grindCard
                            // Grind rides with the numbers window — after
                            // equipment, so the rpm field's grinder gate is set.
                            // Shown expanded (its own screen, no collapse).
                            visible: wizardPage.activeTemplate.grind && wizardPage._detailsPage === "numbers"
                            collapsible: false
                            title: TranslationManager.translate("recipes.composer.grindLabel", "Grind")
                            Label {
                                visible: wizardPage.hasBean
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipes.wizard.grindOwnHelp",
                                      "This recipe's own grind. It starts from the bag's current dial; "
                                      + "adjusting the grind while this recipe is selected keeps it up "
                                      + "to date.")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }
                            // The knowledge-base grind hint as an anchored
                            // callout (icon + tinted background), not muted
                            // caption text.
                            Rectangle {
                                visible: wizardPage.grindHint !== ""
                                Layout.fillWidth: true
                                implicitHeight: grindHintRow.implicitHeight + 2 * Theme.spacingSmall
                                radius: Theme.scaled(8)
                                color: Qt.alpha(Theme.primaryColor, 0.10)
                                RowLayout {
                                    id: grindHintRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: Theme.spacingSmall
                                    anchors.rightMargin: Theme.spacingSmall
                                    spacing: Theme.spacingSmall
                                    ColoredIcon {
                                        Layout.alignment: Qt.AlignTop
                                        source: "qrc:/icons/info.svg"
                                        iconWidth: Theme.scaled(18)
                                        iconHeight: Theme.scaled(18)
                                        iconColor: Theme.primaryColor
                                        Accessible.ignored: true
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: wizardPage.grindHint
                                        font: Theme.captionFont
                                        color: Theme.textColor
                                        wrapMode: Text.WordWrap
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }
                                }
                            }
                            // One tap-to-open control for both dial-in halves
                            // ("grind · rpm"); typing lives in the picker's
                            // text mode. Context is the RECIPE's selected
                            // package, never the active grinder. Clearing the
                            // grind RE-ARMS blank-adopts-bag rather than
                            // refilling immediately: the next bag (re)selection
                            // on create fills the bag's default back in.
                            GrindField {
                                Layout.fillWidth: true
                                presentation: "field"
                                grinderBrand: wizardPage.fEquipmentGrinderBrand
                                grinderModel: wizardPage.fEquipmentGrinderModel
                                grindSetting: wizardPage.fGrind
                                rpmValue: wizardPage.fRpmPinned
                                accessibleName: TranslationManager.translate("recipes.composer.grindLabel", "Grind")
                                onGrindCommitted: function(v) {
                                    wizardPage.fGrind = v
                                    wizardPage._detailsUserEdited = true
                                }
                                onRpmCommitted: function(rpm) {
                                    wizardPage.fRpmPinned = rpm
                                    wizardPage._detailsUserEdited = true
                                }
                            }
                        }

                        // Milk block (latte template, or added on the summary).
                        // No milk-weight capture here: milk is weighed each
                        // time you steam — the recipe only needs the pitcher
                        // (whose preset carries steam time/flow/temperature)
                        // and the milk INTENT, which drives the heater hold.
                        SectionCard {
                            // The steam window (milk drinks).
                            visible: wizardPage.fHasMilk && wizardPage._detailsPage === "steam"
                            title: TranslationManager.translate("recipes.composer.sectionSteam", "Steam")
                            Label {
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipes.composer.steamHint",
                                      "Pick the pitcher you steam this drink with — its preset sets the "
                                      + "steam time and flow. You'll weigh the milk when you steam; a milk "
                                      + "drink also keeps the steam heater warm while the recipe is active "
                                      + "(5–9 min warm-up).")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }
                            // The pitcher presets as inline, tap-to-select
                            // tiles (like equipment): the chosen one is
                            // highlighted, and picking one moves on.
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                Repeater {
                                    model: wizardPage.pitcherTileModel()
                                    delegate: ChoiceTile {
                                        title: modelData.name || ""
                                        meta: wizardPage.pitcherTileMeta(modelData)
                                        selected: (modelData.name || "") === wizardPage.fPitcherName
                                        onChosen: {
                                            wizardPage.selectPitcher(modelData)
                                            wizardPage.detailsContinue()
                                        }
                                    }
                                }
                            }
                        }

                        // Hot-water block (americano/long black/hot-water tea
                        // templates, or added on the summary).
                        SectionCard {
                            // The hot-water window.
                            visible: wizardPage.fHasWater && wizardPage._detailsPage === "water"
                            title: TranslationManager.translate("recipes.composer.sectionHotWater", "Hot water")
                            PickerField {
                                Layout.fillWidth: true
                                label: TranslationManager.translate("recipes.composer.waterVessel", "Water vessel")
                                value: wizardPage.fVesselName
                                placeholder: TranslationManager.translate("recipes.composer.chooseVessel", "Choose vessel…")
                                onActivated: vesselPicker.open()
                            }
                            ColumnLayout {
                                visible: !wizardPage.isHotWaterTea
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall
                                Label {
                                    text: TranslationManager.translate("recipes.composer.waterOrder", "When to add the water")
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    Accessible.ignored: true
                                }
                                StyledComboBox {
                                    Layout.fillWidth: true
                                    accessibleLabel: TranslationManager.translate("recipes.composer.waterOrder", "When to add the water")
                                    model: [
                                        TranslationManager.translate("recipes.composer.waterAfter", "After espresso (Americano)"),
                                        TranslationManager.translate("recipes.composer.waterBefore", "Before espresso (long black)")
                                    ]
                                    currentIndex: wizardPage.fWaterOrder === "before" ? 1 : 0
                                    onActivated: function(index) {
                                        wizardPage.fWaterOrder = index === 1 ? "before" : "after"
                                    }
                                }
                            }
                        }

                    }
                    }
                }

                // ===== Step 5: summary — the edit page =====
                // Pinned header (name + Cancel/Save + errors) above the
                // scrolling body, matching the details step; the component
                // cards flow in the same responsive grid.
                ColumnLayout {
                    spacing: Theme.spacingSmall

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSmall
                        spacing: Theme.spacingMedium
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)
                            Label {
                                text: TranslationManager.translate("recipes.composer.nameLabel", "Recipe name") + " *"
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }
                            StyledTextField {
                                id: nameField
                                Layout.fillWidth: true
                                placeholder: TranslationManager.translate("recipes.composer.namePlaceholder", "e.g. Morning cappuccino")
                                accessibleName: TranslationManager.translate("recipes.composer.nameLabel", "Recipe name")
                            }
                            // Blocks Save when the name matches another active recipe
                            // (block-duplicate-active-names).
                            Label {
                                visible: wizardPage.nameInUse
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipes.dialog.nameInUse",
                                          "That name is already in use — choose a different name.")
                                font: Theme.captionFont
                                color: Theme.warningColor
                                wrapMode: Text.WordWrap
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }
                        }
                        AccessibleButton {
                            Layout.alignment: Qt.AlignBottom
                            text: TranslationManager.translate("common.cancel", "Cancel")
                            accessibleName: TranslationManager.translate("recipes.composer.accessible.cancel", "Cancel recipe editing")
                            onClicked: wizardPage.requestExit()
                        }
                        AccessibleButton {
                            Layout.alignment: Qt.AlignBottom
                            primary: true
                            enabled: wizardPage.canSave
                            text: TranslationManager.translate("common.save", "Save")
                            accessibleName: TranslationManager.translate("recipes.composer.accessible.save", "Save the recipe")
                            onClicked: wizardPage.save()
                        }
                    }

                    // Pinned with the header so a save failure is never
                    // hidden below the scroll.
                    Label {
                        visible: wizardPage.errorMessage !== ""
                        Layout.fillWidth: true
                        text: wizardPage.errorMessage
                        font: Theme.bodyFont
                        color: Theme.errorColor
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: summaryColumn.implicitHeight + Theme.scaled(24)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: summaryColumn
                        width: parent.width
                        spacing: Theme.spacingMedium

                        // WYSIWYG hero: the SAME card component the Recipes
                        // management page renders — what you build here is
                        // what the list will show (design D4).
                        RecipeDrinkCard {
                            Layout.fillWidth: true
                            recipe: wizardPage.previewMap
                            active: false
                            profileTempC: wizardPage.fProfileTempC
                            profileYieldG: wizardPage.fProfileYieldG
                            profileStepTemps: wizardPage.fProfileStepTemps
                            imageKey: wizardPage.fBeanBaseId !== ""
                                ? wizardPage.fBeanBaseId
                                : (wizardPage.fBagId > 0 ? "bag-" + wizardPage.fBagId : "")
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: wizardPage.width >= Theme.scaled(720) ? 2 : 1
                            columnSpacing: Theme.spacingMedium
                            rowSpacing: Theme.spacingMedium

                        SummaryRow {
                            label: TranslationManager.translate("recipes.wizard.rowDrink", "Drink")
                            value: wizardPage.fDrinkType !== ""
                                ? wizardPage.drinkTypeLabel(wizardPage.fDrinkType) : "—"
                            step: "drink"
                        }
                        SummaryRow {
                            label: wizardPage.isTeaDrink
                                ? TranslationManager.translate("recipes.wizard.rowTea", "Tea")
                                : TranslationManager.translate("recipes.wizard.rowBean", "Bean")
                            showThumbnail: wizardPage.hasBean
                            thumbnailKey: wizardPage.fBeanBaseId !== ""
                                ? wizardPage.fBeanBaseId
                                : (wizardPage.fBagId > 0 ? "bag-" + wizardPage.fBagId : "")
                            thumbnailLink: wizardPage.bagImageLink()
                            thumbnailFallback: wizardPage.fCoffee
                            thumbnailIcon: wizardPage.isTeaDrink
                                ? "qrc:/icons/tea.svg" : "qrc:/icons/coffeebeans.svg"
                            value: wizardPage.hasBean
                                ? wizardPage.summaryBeanValue()
                                : (wizardPage.isTeaDrink ? trNoTea.text : trNoBean.text)
                            step: "bean"
                        }
                        SummaryRow {
                            id: profileSummaryRow
                            visible: !wizardPage.isHotWaterTea
                            label: TranslationManager.translate("recipes.wizard.rowProfile", "Profile")
                            value: wizardPage.summaryProfileValue()
                            accessibleValue: wizardPage.fProfileTitle !== "" ? wizardPage.fProfileTitle : "—"
                            step: "profile"
                            // The two info affordances the profile-step tiles
                            // offer — the sparkle KB popup and the Profile Info
                            // page — reachable without leaving the summary.
                            headerActions: RowLayout {
                                spacing: Theme.spacingSmall
                                readonly property string profileFilename:
                                    wizardPage.fProfileTitle !== ""
                                        ? ProfileManager.findProfileByTitle(wizardPage.fProfileTitle) : ""
                                readonly property bool hasKb: {
                                    if (wizardPage.fProfileTitle === "") return false
                                    var info = ProfileManager.profileCatalogInfoForTitle(wizardPage.fProfileTitle)
                                    return info && info.hasKnowledgeBase === true
                                }
                                ColoredIcon {
                                    visible: parent.hasKb
                                    source: "qrc:/icons/sparkle.svg"
                                    iconWidth: Theme.scaled(18)
                                    iconHeight: Theme.scaled(18)
                                    iconColor: Theme.textSecondaryColor
                                    Accessible.ignored: true
                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        anchors.margins: Theme.scaled(-6)
                                        accessibleName: TranslationManager.translate(
                                            "profileselector.accessible.view_knowledge",
                                            "View AI knowledge base")
                                        accessibleItem: parent
                                        onAccessibleClicked:
                                            wizardKnowledgeDialog.openFor(wizardPage.fProfileTitle)
                                    }
                                }
                                ProfileInfoButton {
                                    visible: parent.profileFilename !== ""
                                    Layout.preferredWidth: Theme.scaled(26)
                                    Layout.preferredHeight: Theme.scaled(26)
                                    buttonSize: Theme.scaled(26)
                                    profileFilename: parent.profileFilename
                                    profileName: wizardPage.fProfileTitle
                                    onClicked: pageStack.push(
                                        Qt.resolvedUrl("ProfileInfoPage.qml"),
                                        { profileFilename: parent.profileFilename,
                                          profileName: wizardPage.fProfileTitle })
                                }
                            }
                        }
                        // Equipment sits ABOVE dose/yield/temp: the grinder must
                        // be chosen before grind/rpm (the rpm field is gated by
                        // the grinder's capability), matching the walk order.
                        SummaryRow {
                            id: equipmentSummaryRow
                            label: TranslationManager.translate("recipes.composer.sectionEquipment", "Equipment")
                            // No package, or one not yet resolved from inventory:
                            // fall back to the plain name / "None".
                            value: wizardPage.fEquipmentId > 0 ? wizardPage.fEquipmentName : trNone.text
                            accessibleValue: value
                            step: "details"
                            detailsPage: "equipment"
                            // A resolved package renders through the shared
                            // EquipmentSummary — grinder, basket, puck prep —
                            // but NOT its dial memory (grind/rpm): those are
                            // recipe-owned and shown on the Dose/yield/temp card.
                            contentComponent: (wizardPage.fEquipmentId > 0
                                && wizardPage._selectedPackage
                                && wizardPage._selectedPackage.id === wizardPage.fEquipmentId)
                                ? equipmentSummaryComponent : null
                        }
                        SummaryRow {
                            // Coffee drinks pin a grind; tea does not — keep the
                            // title honest for each.
                            label: wizardPage.activeTemplate.grind
                                ? TranslationManager.translate("recipes.wizard.rowDoseYieldTempGrind", "Dose, yield, temp & grind")
                                : TranslationManager.translate("recipes.wizard.rowDoseYieldTemp", "Dose, yield & temp")
                            value: wizardPage.summaryDetailsValue()
                            step: "details"
                            detailsPage: "numbers"
                        }
                        // Every stored block gets a visible row: a latte's
                        // milk shows ON the summary, not only behind an edit.
                        SummaryRow {
                            visible: wizardPage.fHasMilk
                            label: TranslationManager.translate("recipes.wizard.rowSteam", "Steam / milk")
                            value: {
                                var parts = []
                                if (wizardPage.fPitcherName !== "")
                                    parts.push(wizardPage.fPitcherName)
                                if (wizardPage.fMilkWeightG > 0)
                                    parts.push(TranslationManager.translate(
                                        "recipes.list.milkWeight", "%1g milk").arg(wizardPage.fMilkWeightG))
                                return parts.length > 0 ? parts.join(" · ") : "—"
                            }
                            step: "details"
                            detailsPage: "steam"
                        }
                        SummaryRow {
                            visible: wizardPage.fHasWater
                            label: TranslationManager.translate("recipes.wizard.rowHotWater", "Hot water")
                            value: {
                                var parts = []
                                if (wizardPage.fVesselName !== "")
                                    parts.push(wizardPage.fVesselName)
                                if (wizardPage.fVesselVolume > 0)
                                    parts.push(wizardPage.fVesselVolume
                                        + (wizardPage.fVesselMode === "volume" ? "ml" : "g"))
                                if (!wizardPage.isHotWaterTea)
                                    parts.push(wizardPage.fWaterOrder === "before"
                                        ? TranslationManager.translate("recipes.wizard.waterBeforeShort", "before the espresso")
                                        : TranslationManager.translate("recipes.wizard.waterAfterShort", "after the espresso"))
                                return parts.length > 0 ? parts.join(" · ") : "—"
                            }
                            step: "details"
                            detailsPage: "water"
                        }
                        }

                        // Template escape hatches: any block combination the
                        // model allows stays expressible.
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                            spacing: Theme.spacingMedium
                            AccessibleButton {
                                text: wizardPage.fHasMilk
                                    ? TranslationManager.translate("recipes.wizard.removeMilk", "Remove milk")
                                    : TranslationManager.translate("recipes.wizard.addMilk", "Add milk")
                                accessibleName: text
                                onClicked: {
                                    wizardPage.fHasMilk = !wizardPage.fHasMilk
                                    if (wizardPage.fHasMilk) {
                                        wizardPage._detailsPage = "steam"
                                        wizardPage.openStep("details")
                                    }
                                }
                            }
                            AccessibleButton {
                                text: wizardPage.fHasWater
                                    ? TranslationManager.translate("recipes.wizard.removeWater", "Remove hot water")
                                    : TranslationManager.translate("recipes.wizard.addWater", "Add hot water")
                                accessibleName: text
                                onClicked: {
                                    wizardPage.fHasWater = !wizardPage.fHasWater
                                    if (wizardPage.fHasWater) {
                                        wizardPage._detailsPage = "water"
                                        wizardPage.openStep("details")
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                    }
                }
            }
        }
    }

    // Create-a-bag entry point ON the bean step (add-recipe-wizard-tea):
    // the full Change Beans dialog in the right kind mode — coffee gets the
    // search-first flow, tea the tea form. The created bag becomes the
    // active bag (inventory-context semantics, same as the Beans page) and
    // is immediately selected as this recipe's bean, advancing the wizard.
    ChangeBeansDialog {
        id: wizardBeansDialog
        context: "inventory"
        onBagSelected: function(bagId, bag) {
            if (wizardPage.currentStep === "bean")
                wizardPage.selectBean(bag)
        }
    }

    // Shared KB popup (qml/components/ProfileKnowledgeDialog.qml), opened
    // from the sparkle on a profile tile.
    ProfileKnowledgeDialog {
        id: wizardKnowledgeDialog
    }

    // The summary Equipment card's body — the shared EquipmentSummary fed the
    // resolved package's identity. Grind setting and rpm are DELIBERATELY not
    // passed (grindSetting/rpm/rpmCapable left at defaults), so its dial line
    // stays empty: those are recipe-owned and appear on the Details card.
    Component {
        id: equipmentSummaryComponent
        EquipmentSummary {
            grinderName: wizardPage._selectedPackage.name || ""
            grinderBrand: wizardPage._selectedPackage.grinderBrand || ""
            grinderModel: wizardPage._selectedPackage.grinderModel || ""
            grinderBurrs: wizardPage._selectedPackage.grinderBurrs || ""
            basketBrand: wizardPage._selectedPackage.basketBrand || ""
            basketModel: wizardPage._selectedPackage.basketModel || ""
            puckPrepCanonical: wizardPage._selectedPackage.puckPrepCanonical || ""
        }
    }

    // --- Pickers (selection-only dialogs, composer idiom) -------------------

    component PickerDialog: Dialog {
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(520), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(620), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
    }



    PickerDialog {
        id: vesselPicker
        height: Math.min(Theme.scaled(420), parent.height - Theme.scaled(80))
        contentItem: ListView {
            clip: true
            model: Settings.brew.waterVesselPresets
            delegate: ItemDelegate {
                width: ListView.view.width
                // Preset metadata on the row: amount (per its mode) and
                // temperature — "220ml · 96°C", never name-only.
                readonly property string rowMeta: {
                    var parts = []
                    if ((modelData.volume || 0) > 0)
                        parts.push(modelData.volume + (modelData.mode === "volume" ? "ml" : "g"))
                    if ((modelData.temperature || 0) > 0)
                        parts.push(Theme.formatTemperature(modelData.temperature, 0))
                    return parts.join(" · ")
                }
                contentItem: ColumnLayout {
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: modelData.name || ""
                        font: Theme.bodyFont
                        color: Theme.textColor
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: rowMeta !== ""
                        Layout.fillWidth: true
                        text: rowMeta
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        elide: Text.ElideRight
                    }
                }
                Accessible.role: Accessible.Button
                Accessible.name: (modelData.name || "") + (rowMeta !== "" ? ", " + rowMeta : "")
                onClicked: {
                    // Snapshot BY VALUE (never a preset index).
                    wizardPage.fVesselName = modelData.name || ""
                    wizardPage.fVesselVolume = modelData.volume || 0
                    wizardPage.fVesselMode = modelData.mode || "weight"
                    wizardPage.fVesselFlowRate = modelData.flowRate || 40
                    wizardPage.fVesselTemperatureC = modelData.temperature || 0
                    vesselPicker.close()
                }
            }
        }
    }

    // Unsaved-changes intercept (requestExit): leaving with edits offers
    // Save / Discard — closing the dialog (Esc / tap outside) keeps editing.
    UnsavedChangesDialog {
        id: exitDialog
        itemType: "recipe"
        showSaveAs: false
        canSave: wizardPage.canSave
        onDiscardClicked: root.goBack()
        onSaveClicked: wizardPage.save()
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: wizardPage.goBackOneStep()
    }

    // The named-ratio chooser (Ristretto / Normale / Lungo), opened from the
    // Ratio field's 1:X.X readout. PICK-ONLY: it fills the wizard's field and
    // anchors the ratio, never touching the live session — this page designs a
    // recipe, it does not arm the next brew.
    RatioPresetDialog {
        id: wizardRatioPicker
        pickOnly: true
        compareRatio: parseFloat(ratioField.text) || 0
        onRatioPicked: function(r) {
            ratioField.text = Number(r).toFixed(1)
            wizardPage.fYieldMode = "ratio"
            wizardPage._detailsUserEdited = true
            wizardPage.syncDerivedYieldFields()
        }
    }
}
