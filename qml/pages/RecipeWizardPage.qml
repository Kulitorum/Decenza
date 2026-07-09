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
    objectName: "recipeWizardPage"
    background: Rectangle { color: Theme.backgroundColor }

    // "create" | "edit". Edit loads the row; create starts from `prefill`
    // (possibly empty) or `promoteShotId`. Same contract as the old composer.
    property string mode: "create"
    property int editRecipeId: -1
    property var prefill: ({})
    property real promoteShotId: 0

    // --- step machine ----------------------------------------------------
    // "drink" | "bean" | "profile" | "details" | "summary". Creation walks
    // them in order; edit/clone/promote start at "summary". A step opened
    // FROM the summary returns to it on selection instead of advancing.
    property string currentStep: "drink"
    property bool _fromSummary: false

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

    function drinkTypeLabel(t) {
        switch (t) {
        case "espresso": return TranslationManager.translate("recipes.wizard.type.espresso", "Espresso")
        case "filter": return TranslationManager.translate("recipes.wizard.type.filter", "Filter")
        case "americano": return TranslationManager.translate("recipes.wizard.type.americano", "Americano")
        case "long_black": return TranslationManager.translate("recipes.wizard.type.longBlack", "Long black")
        case "latte": return TranslationManager.translate("recipes.wizard.type.latte", "Latte / Cappuccino")
        case "tea": return TranslationManager.translate("recipes.wizard.type.tea", "Tea")
        case "tea_hotwater": return TranslationManager.translate("recipes.wizard.type.teaHotWater", "Tea (hot water)")
        }
        return t
    }
    function drinkTypeIcon(t) {
        switch (t) {
        case "filter": return "qrc:/icons/filter.svg"
        case "americano": case "long_black": return "qrc:/icons/water.svg"
        case "latte": return "qrc:/icons/steam.svg"
        case "tea": case "tea_hotwater": return "qrc:/icons/tea.svg"
        }
        return "qrc:/icons/espresso 8mm.svg"
    }

    // --- form state (same vocabulary as the old composer) ------------------
    property string fDrinkType: ""
    property string fProfileTitle: ""
    property string fProfileJson: ""
    property real fProfileTempC: 0
    property real fTempDeltaC: 0
    property real fLoadedTempOverrideC: 0
    property bool _submitting: false
    property string fBeanBaseId: ""
    property string fRoaster: ""
    property string fCoffee: ""
    property string fBagBlob: ""          // selected bag's beanBaseData (tea brewing seeds)
    property real fEquipmentId: 0
    property string fEquipmentName: ""
    property bool fEquipmentRpmCapable: false
    property bool fGrindOverride: false
    property string fInheritedGrind: ""
    property real fInheritedRpm: 0
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
    property string bagSwapHint: ""
    property string _autoName: ""

    readonly property bool hasBean: fBeanBaseId !== "" || fRoaster !== "" || fCoffee !== ""
    readonly property bool canSave: MainController.recipeStorage.isSaveValid(
        nameField.text, fProfileTitle, buildHotWaterJson())

    // Auto-suggested name ("<Bean> <DrinkType>"): applied while the field is
    // empty or still holds the previous suggestion — never over a user edit.
    function suggestName() {
        var bean = (fCoffee !== "" ? fCoffee : fRoaster).trim()
        var parts = []
        if (bean !== "") parts.push(bean)
        if (fDrinkType !== "") parts.push(drinkTypeLabel(fDrinkType))
        var suggestion = parts.join(" ")
        if (suggestion === "" || (nameField.text !== "" && nameField.text !== _autoName))
            return
        nameField.text = suggestion
        _autoName = suggestion
    }

    StackView.onActivated: root.currentPageTitle = mode === "edit"
        ? TranslationManager.translate("recipes.wizard.editTitle", "Edit Recipe")
        : TranslationManager.translate("recipes.wizard.createTitle", "New Recipe")

    Component.onCompleted: {
        root.currentPageTitle = mode === "edit"
            ? TranslationManager.translate("recipes.wizard.editTitle", "Edit Recipe")
            : TranslationManager.translate("recipes.wizard.createTitle", "New Recipe")
        if (mode === "edit" && editRecipeId > 0) {
            currentStep = "summary"
            MainController.recipeStorage.requestRecipe(editRecipeId)
        } else if (promoteShotId > 0) {
            currentStep = "summary"
            MainController.shotHistory.requestShot(promoteShotId)
        } else if (prefill && Object.keys(prefill).length > 0) {
            currentStep = "summary"
            applyRecipeMap(prefill)
            nameField.forceActiveFocus()
            nameField.selectAll()
        } else {
            currentStep = "drink"
        }
    }

    // --- ported state <-> JSON helpers (verbatim composer semantics) -------

    function applyRecipeMap(r) {
        nameField.text = r.name || ""
        fProfileTitle = r.profileTitle || ""
        fProfileJson = r.profileJson || ""
        fBeanBaseId = r.beanBaseId || ""
        fRoaster = r.roasterName || ""
        fCoffee = r.coffeeName || ""
        fEquipmentId = r.equipmentId || 0
        doseField.text = r.doseG > 0 ? Number(r.doseG).toFixed(1) : ""
        yieldField.text = r.yieldG > 0 ? Number(r.yieldG).toFixed(1) : ""
        fLoadedTempOverrideC = r.tempOverrideC || 0
        refreshProfileTemp()
        fTempDeltaC = (r.tempOverrideC > 0 && fProfileTempC > 0)
            ? r.tempOverrideC - fProfileTempC : 0
        fGrindOverride = (r.grindPinned || "") !== "" || (r.rpmPinned || 0) > 0
        grindField.text = r.grindPinned || ""
        rpmField.text = (r.rpmPinned || 0) > 0 ? String(r.rpmPinned) : ""
        applySteamJson(r.steamJson || "")
        applyHotWaterJson(r.hotWaterJson || "")
        // Drink type: stored value, else derive from the loaded blocks — a
        // legacy (pre-migration-28) recipe opens under the right template.
        fDrinkType = r.drinkType || deriveDrinkType()
        if (fEquipmentId > 0)
            MainController.equipmentStorage.requestInventory()
        refreshInheritedGrind()
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
        var hasBeanData = beanBaseId !== "" || shot.beanBrand || shot.beanType
        // Route through `prefill` so save() picks up the provenance fields.
        prefill = ({
            name: "",
            profileTitle: shot.profileName || "",
            profileJson: shot.profileJson || "",
            beanBaseId: beanBaseId,
            roasterName: shot.beanBrand || "",
            coffeeName: shot.beanType || "",
            equipmentId: shot.equipmentId || 0,
            doseG: shot.doseWeightG || 0,
            yieldG: shot.targetWeightG || 0,
            tempOverrideC: shot.temperatureOverrideC || 0,
            grindPinned: hasBeanData ? "" : (shot.grinderSetting || ""),
            rpmPinned: hasBeanData ? 0 : (shot.rpm || 0),
            steamJson: shot.steamJson && shot.steamJson !== "" ? shot.steamJson
                                                               : currentSteamSnapshot(),
            hotWaterJson: shot.hotWaterJson || "",
            createdFromShotId: promoteShotId
        })
        applyRecipeMap(prefill)
        var bean = ((shot.beanBrand || "") + " " + (shot.beanType || "")).trim()
        nameField.text = bean !== "" ? bean : (shot.profileName || "")
        nameField.selectAll()
    }

    // Resolve the selected profile's base temperature (for the offset control).
    function refreshProfileTemp() {
        fProfileTempC = 0
        if (fProfileTitle === "")
            return
        var fn = ProfileManager.findProfileByTitle(fProfileTitle)
        if (fn && fn !== "") {
            var d = ProfileManager.getProfileByFilename(fn)
            fProfileTempC = d.espresso_temperature || 0
        }
    }

    function refreshInheritedGrind() {
        fInheritedGrind = ""
        if (hasBean)
            MainController.bagStorage.requestInventory()
    }

    function save() {
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
        var map = {
            name: name,
            drinkType: fDrinkType !== "" ? fDrinkType : deriveDrinkType(),
            profileTitle: fProfileTitle,
            profileJson: fProfileJson,
            beanBaseId: fBeanBaseId,
            roasterName: fRoaster,
            coffeeName: fCoffee,
            equipmentId: fEquipmentId,
            doseG: parseFloat(doseField.text) || 0,
            yieldG: parseFloat(yieldField.text) || 0,
            // Offset semantics (like the shot plan): 0° = no override. When the
            // profile's base temp is unknown (profile not installed) we can't
            // recompute the absolute — preserve the loaded override verbatim.
            // Tea details edit the ABSOLUTE temp instead (fTeaTempC below).
            tempOverrideC: isTeaDrink
                ? (fTeaTempC > 0 && Math.abs(fTeaTempC - fProfileTempC) > 0.05 ? fTeaTempC : 0)
                : (fProfileTempC > 0
                    ? (Math.abs(fTempDeltaC) > 0.05 ? fProfileTempC + fTempDeltaC : 0)
                    : fLoadedTempOverrideC),
            // Override OFF (with a bean) = inherit: store nothing. Bean-less
            // recipes always keep their grind/rpm on the recipe. Tea recipes
            // never store grind (nothing to grind).
            grindPinned: (!activeTemplate.grind) ? ""
                : ((!hasBean || fGrindOverride) ? grindField.text.trim() : ""),
            rpmPinned: (activeTemplate.grind && (!hasBean || fGrindOverride)
                        && (fEquipmentRpmCapable || (parseInt(rpmField.text) || 0) > 0))
                ? (parseInt(rpmField.text) || 0) : 0,
            steamJson: buildSteamJson(),
            hotWaterJson: buildHotWaterJson()
        }
        if (prefill && prefill.createdFromShotId)
            map.createdFromShotId = prefill.createdFromShotId
        if (prefill && prefill.clonedFromRecipeId)
            map.clonedFromRecipeId = prefill.clonedFromRecipeId
        _submitting = true
        if (mode === "edit" && editRecipeId > 0)
            MainController.recipeStorage.requestUpdateRecipe(editRecipeId, map)
        else
            MainController.recipeStorage.requestCreateRecipe(map)
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
            // Crossing the coffee/tea boundary invalidates the bean choice.
            if (was !== "" && (templates[was] || {}).bagKind !== t.bagKind) {
                fBeanBaseId = ""; fRoaster = ""; fCoffee = ""; fBagBlob = ""
                fInheritedGrind = ""; fInheritedRpm = 0
            }
            // Tea → hot-water tea keeps no profile; other switches keep it
            // only when it still fits the new filter set (checked lazily by
            // the profile step; clearing here keeps the walk honest).
            if (type === "tea_hotwater")
                { fProfileTitle = ""; fProfileJson = ""; fProfileTempC = 0 }
            // Per-drink-type equipment default (last recipe of this type).
            MainController.recipeStorage.requestLastEquipmentForDrinkType(type)
        }
        suggestName()
        // Hot-water tea has no profile step — details follows the bean.
        stepDone("bean")
    }

    function selectBean(bag) {
        var hadBean = hasBean
        if (!bag) {
            // No bean to inherit from: carry the current dial onto the recipe.
            if (activeTemplate.grind && !fGrindOverride && grindField.text === "") {
                grindField.text = fInheritedGrind
                rpmField.text = fInheritedRpm > 0 ? String(fInheritedRpm) : ""
            }
            fBeanBaseId = ""; fRoaster = ""; fCoffee = ""; fBagBlob = ""
            fInheritedGrind = ""; fInheritedRpm = 0
        } else {
            fBeanBaseId = bag.beanBaseId || ""
            fRoaster = bag.roasterName || ""
            fCoffee = bag.coffeeName || ""
            fBagBlob = bag.beanBaseData || ""
            fInheritedGrind = bag.grinderSetting || ""
            fInheritedRpm = bag.rpm || 0
            if (hadBean && !fGrindOverride)
                bagSwapHint = TranslationManager.translate(
                    "recipes.wizard.grindFollowsHint", "Grind now follows %1: %2")
                    .arg(((bag.roasterName || "") + " " + (bag.coffeeName || "")).trim())
                    .arg(fInheritedGrind || "—")
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
        if (detail.recommended_dose > 0 && doseField.text === "")
            doseField.text = Number(detail.recommended_dose).toFixed(1)
        if (detail.target_weight > 0 && yieldField.text === "")
            yieldField.text = Number(detail.target_weight).toFixed(1)
        fProfileTempC = detail.espresso_temperature || 0
        if (isTeaDrink && fTeaTempC <= 0)
            fTeaTempC = fProfileTempC
        suggestName()
        stepDone("details")
        applyDetailsPrefill()
    }

    function selectJustHotWater() {
        fDrinkType = "tea_hotwater"
        fProfileTitle = ""; fProfileJson = ""; fProfileTempC = 0
        fHasWater = true
        fWaterOrder = "after"
        suggestName()
        stepDone("details")
        applyDetailsPrefill()
    }

    // --- details prefill (history → tea bag data → profile defaults) -------

    // Absolute tea temperature (vendor numbers are steeping temps; the tea
    // details step edits the absolute rather than an offset).
    property real fTeaTempC: 0
    property var _teaBrewing: ({})

    function applyDetailsPrefill() {
        // Tier 2/3 seeds first (synchronous); the shot-history tier lands
        // async via latestShotForBeanProfileReady and wins by overwriting
        // still-empty fields only — user edits are never clobbered.
        if (isTeaDrink) {
            _teaBrewing = ({})
            if (fBagBlob !== "") {
                try { _teaBrewing = JSON.parse(fBagBlob) } catch (e) { _teaBrewing = ({}) }
            }
            var stated = parseFloat(_teaBrewing.brewTempC) || 0
            var typeMatched = fProfileTitle !== ""
                && ProfileManager.teaProfileMatchesType(fProfileTitle, String(_teaBrewing.teaType || ""))
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
            try { teaType = String(JSON.parse(fBagBlob).teaType || "") } catch (e) {}
        }
        var roastLevel = ""  // the bag list carries roastLevel per bag
        if (!isTeaDrink && _selectedBagRoastLevel !== "")
            roastLevel = _selectedBagRoastLevel
        rebuildProfileModel()
        if (hasBean)
            MainController.shotHistory.requestRankedProfilesForBean(fRoaster, fCoffee, roastLevel, teaType)
    }
    property string _selectedBagRoastLevel: ""

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
            if (p) { tier1.push({ isHeader: false, title: p.title, name: p.name, reason: "" }); used[p.title] = true }
        }
        if (tier1.length > 0) {
            model.push({ isHeader: true, title: TranslationManager.translate(
                "recipes.wizard.profiles.withBean", "Used with this bean") })
            model = model.concat(tier1)
        }
        // Tier ②: type-matched tea profiles (no history needed) first, then
        // similar-bean history.
        var tier2 = []
        var teaType = ""
        if (isTeaDrink && _teaBrewingTypeForRanking() !== "")
            teaType = _teaBrewingTypeForRanking()
        if (teaType !== "") {
            for (i = 0; i < inSet.length; ++i) {
                if (used[inSet[i].title]) continue
                if (ProfileManager.teaProfileMatchesType(inSet[i].title, teaType)) {
                    tier2.push({ isHeader: false, title: inSet[i].title, name: inSet[i].name,
                                 reason: TranslationManager.translate(
                                     "recipes.wizard.profiles.matchesType", "matches %1").arg(teaType) })
                    used[inSet[i].title] = true
                }
            }
        }
        for (i = 0; i < similar.length; ++i) {
            p = byTitle[String(similar[i].profileName).toLowerCase()]
            if (p && !used[p.title]) {
                tier2.push({ isHeader: false, title: p.title, name: p.name,
                             reason: TranslationManager.translate(
                                 "recipes.wizard.profiles.similarBeans", "used with similar beans") })
                used[p.title] = true
            }
        }
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
            try { statedTemp = parseFloat(JSON.parse(fBagBlob).brewTempC) || 0 } catch (e) {}
        }
        if (statedTemp > 0) {
            var withTemp = rest.map(function(p) {
                var d = ProfileManager.getProfileByFilename(p.name)
                var t = d.espresso_temperature || 0
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
                model.push({ isHeader: false, title: rest[i].title, name: rest[i].name, reason: "" })
        }
        profileModel = model
    }

    function _teaBrewingTypeForRanking() {
        if (fBagBlob === "") return ""
        try { return String(JSON.parse(fBagBlob).teaType || "") } catch (e) { return "" }
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
            wizardPage._ranked = result
            wizardPage.rebuildProfileModel()
        }
        function onLatestShotForBeanProfileReady(shot) {
            if (!shot || Object.keys(shot).length === 0)
                return
            // History wins the prefill — but only into still-empty fields.
            if (doseField.text === "" && shot.doseWeightG > 0)
                doseField.text = Number(shot.doseWeightG).toFixed(1)
            if (yieldField.text === "" && shot.targetWeightG > 0)
                yieldField.text = Number(shot.targetWeightG).toFixed(1)
            if (shot.temperatureOverrideC > 0) {
                if (wizardPage.isTeaDrink) {
                    if (wizardPage.fTeaTempC <= 0)
                        wizardPage.fTeaTempC = shot.temperatureOverrideC
                } else if (Math.abs(wizardPage.fTempDeltaC) < 0.05 && wizardPage.fProfileTempC > 0) {
                    wizardPage.fTempDeltaC = shot.temperatureOverrideC - wizardPage.fProfileTempC
                }
            }
            if (wizardPage.activeTemplate.grind && !wizardPage.hasBean
                && grindField.text === "" && shot.grinderSetting)
                grindField.text = shot.grinderSetting
        }
    }

    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, recipe) {
            if (wizardPage.mode === "edit" && recipeId === wizardPage.editRecipeId
                && Object.keys(recipe).length > 0)
                wizardPage.applyRecipeMap(recipe)
        }
        function onRecipeCreated(recipeId, recipe) {
            if (wizardPage.mode !== "edit" && wizardPage._submitting) {
                wizardPage._submitting = false
                if (recipeId > 0)
                    pageStack.pop()
                else
                    wizardPage.errorMessage =
                        TranslationManager.translate("recipes.wizard.errorSave", "Could not save the recipe")
            }
        }
        function onRecipeUpdated(recipeId, success) {
            if (wizardPage.mode === "edit" && recipeId === wizardPage.editRecipeId
                && wizardPage._submitting) {
                wizardPage._submitting = false
                if (success)
                    pageStack.pop()
                else
                    wizardPage.errorMessage =
                        TranslationManager.translate("recipes.wizard.errorSave", "Could not save the recipe")
            }
        }
        function onLastEquipmentForDrinkTypeReady(drinkType, equipmentId) {
            // Per-drink-type default: only fills an EMPTY equipment choice.
            if (drinkType === wizardPage.fDrinkType && wizardPage.fEquipmentId <= 0
                && equipmentId > 0) {
                wizardPage.fEquipmentId = equipmentId
                MainController.equipmentStorage.requestInventory()
            }
        }
    }

    property var _bags: []
    property var _packages: []
    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            wizardPage._bags = bags
            if (wizardPage.hasBean) {
                for (var i = 0; i < bags.length; ++i) {
                    var b = bags[i]
                    var match = wizardPage.fBeanBaseId !== ""
                        ? b.beanBaseId === wizardPage.fBeanBaseId
                        : (String(b.roasterName).toLowerCase() === wizardPage.fRoaster.toLowerCase()
                           && String(b.coffeeName).toLowerCase() === wizardPage.fCoffee.toLowerCase())
                    if (match) {
                        wizardPage.fInheritedGrind = b.grinderSetting || ""
                        wizardPage.fInheritedRpm = b.rpm || 0
                        wizardPage._selectedBagRoastLevel = b.roastLevel || ""
                        if (wizardPage.fBagBlob === "")
                            wizardPage.fBagBlob = b.beanBaseData || ""
                        return
                    }
                }
                wizardPage.fInheritedGrind = ""
                wizardPage.fInheritedRpm = 0
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
                        wizardPage.fEquipmentRpmCapable = !!packages[i].rpmCapable
                        return
                    }
                }
                wizardPage.fEquipmentRpmCapable = false
            }
        }
    }

    // Hidden Tr instances for property-bound strings.
    Tr { id: trNone; key: "recipes.composer.none"; fallback: "None"; visible: false }
    Tr { id: trInherited; key: "recipes.composer.grindInherited"; fallback: "Follows the bag"; visible: false }
    Tr { id: trNoBean; key: "recipes.wizard.noBean"; fallback: "No bean"; visible: false }
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
            color: Theme.surfaceColor
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
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            Accessible.name: numberField.label
            onTextEdited: numberField.edited(text)
        }
    }

    component SectionCard: Rectangle {
        id: sectionCard
        property string title: ""
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: cardColumn.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.cardRadius
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1
        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall
            Label {
                visible: sectionCard.title !== ""
                text: sectionCard.title
                font: Theme.subtitleFont
                color: Theme.textColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
    }

    // A tappable summary row: label, value, chevron → reopens a step.
    component SummaryRow: Rectangle {
        id: summaryRow
        property string label: ""
        property string value: ""
        property string step: ""
        Layout.fillWidth: true
        implicitHeight: Theme.scaled(52)
        radius: Theme.scaled(8)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall
            Label {
                text: summaryRow.label
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                Accessible.ignored: true
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: summaryRow.value
                font: Theme.bodyFont
                color: Theme.textColor
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
            accessibleName: summaryRow.label + ", " + summaryRow.value
            accessibleItem: parent
            onAccessibleClicked: wizardPage.openStep(summaryRow.step)
        }
    }

    // --- Page body -----------------------------------------------------------

    KeyboardAwareContainer {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        textFields: [nameField, doseField.input, yieldField.input, grindField, rpmField,
                     milkField.input, profileSearchField]

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
                        color: Theme.surfaceColor
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
                            model: ["espresso", "filter", "americano", "long_black", "latte", "tea"]
                            delegate: Rectangle {
                                radius: Theme.cardRadius
                                color: Theme.surfaceColor
                                border.color: wizardPage.fDrinkType === modelData
                                    ? Theme.primaryColor : Theme.borderColor
                                border.width: wizardPage.fDrinkType === modelData ? 2 : 1
                                implicitWidth: Theme.scaled(170)
                                implicitHeight: Theme.scaled(120)
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.spacingSmall
                                    Image {
                                        Layout.alignment: Qt.AlignHCenter
                                        source: wizardPage.drinkTypeIcon(modelData)
                                        sourceSize.width: Theme.scaled(44)
                                        sourceSize.height: Theme.scaled(44)
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
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: {
                            var kind = wizardPage.activeTemplate.bagKind
                            var out = []
                            for (var i = 0; i < wizardPage._bags.length; ++i) {
                                var b = wizardPage._bags[i]
                                var bKind = String(b.kind || "") === "tea" ? "tea" : "coffee"
                                if (bKind === kind)
                                    out.push(b)
                            }
                            out.push({ isNone: true })
                            return out
                        }
                        delegate: ItemDelegate {
                            width: ListView.view.width
                            contentItem: Label {
                                text: modelData.isNone ? trNoBean.text
                                    : ((modelData.roasterName || "") + " " + (modelData.coffeeName || "")).trim()
                                font: Theme.bodyFont
                                color: Theme.textColor
                                elide: Text.ElideRight
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: contentItem.text
                            onClicked: wizardPage.selectBean(modelData.isNone ? null : modelData)
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
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: wizardPage.profileModel
                        delegate: Loader {
                            width: ListView.view.width
                            sourceComponent: modelData.isHeader ? headerRow : profileRow
                            property var row: modelData
                            Component {
                                id: headerRow
                                Label {
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
                                id: profileRow
                                ItemDelegate {
                                    width: parent ? parent.width : 0
                                    contentItem: RowLayout {
                                        spacing: Theme.spacingSmall
                                        Label {
                                            Layout.fillWidth: true
                                            text: row.title
                                            font: Theme.bodyFont
                                            color: Theme.textColor
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            visible: row.reason !== ""
                                            text: row.reason
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                        }
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: row.title + (row.reason !== "" ? ", " + row.reason : "")
                                    onClicked: wizardPage.selectProfile(row)
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
                Flickable {
                    contentHeight: detailsColumn.implicitHeight + Theme.scaled(24)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: detailsColumn
                        width: parent.width
                        spacing: Theme.spacingMedium

                        SectionCard {
                            title: TranslationManager.translate("recipes.wizard.sectionNumbers", "The numbers")
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                NumberField {
                                    id: doseField
                                    Layout.fillWidth: true
                                    label: wizardPage.isTeaDrink
                                        ? TranslationManager.translate("recipes.wizard.leafDose", "Leaf (g)")
                                        : TranslationManager.translate("recipes.composer.doseLabel", "Dose (g)")
                                }
                                NumberField {
                                    id: yieldField
                                    visible: !wizardPage.isHotWaterTea
                                    Layout.fillWidth: true
                                    label: TranslationManager.translate("recipes.composer.yieldLabel", "Yield (g)")
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
                                        Layout.fillWidth: true
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
                                        }
                                    }
                                }
                                NumberField {
                                    visible: wizardPage.isTeaDrink
                                    Layout.fillWidth: true
                                    label: TranslationManager.translate("recipes.wizard.teaTemp", "Temp (°C)")
                                    text: wizardPage.fTeaTempC > 0 ? String(Math.round(wizardPage.fTeaTempC)) : ""
                                    onEdited: function(newText) { wizardPage.fTeaTempC = parseFloat(newText) || 0 }
                                }
                            }
                        }

                        // Grind: coffee drinks only (tea has nothing to grind).
                        SectionCard {
                            visible: wizardPage.activeTemplate.grind
                            title: TranslationManager.translate("recipes.composer.grindLabel", "Grind")
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(4)
                                    Label {
                                        visible: wizardPage.hasBean && !wizardPage.fGrindOverride
                                        Layout.fillWidth: true
                                        text: {
                                            var inherited = trInherited.text
                                            if (wizardPage.fInheritedGrind !== "") {
                                                inherited += ": " + wizardPage.fInheritedGrind
                                                if (wizardPage.fEquipmentRpmCapable && wizardPage.fInheritedRpm > 0)
                                                    inherited += " · " + wizardPage.fInheritedRpm + " rpm"
                                            }
                                            return inherited
                                        }
                                        font: Theme.bodyFont
                                        color: Theme.textColor
                                        wrapMode: Text.WordWrap
                                    }
                                    RowLayout {
                                        visible: !wizardPage.hasBean || wizardPage.fGrindOverride
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingMedium
                                        StyledTextField {
                                            id: grindField
                                            Layout.fillWidth: true
                                            placeholder: TranslationManager.translate("recipes.composer.grindPlaceholder", "e.g. 2.4")
                                            Accessible.name: TranslationManager.translate("recipes.composer.grindLabel", "Grind")
                                        }
                                        StyledTextField {
                                            id: rpmField
                                            visible: wizardPage.fEquipmentRpmCapable
                                            Layout.preferredWidth: Theme.scaled(110)
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            placeholder: TranslationManager.translate("recipes.composer.rpmLabel", "RPM")
                                            Accessible.name: TranslationManager.translate("recipes.composer.rpmLabel", "RPM")
                                        }
                                    }
                                }
                                ColumnLayout {
                                    visible: wizardPage.hasBean
                                    Layout.alignment: Qt.AlignTop
                                    spacing: Theme.scaled(2)
                                    Label {
                                        text: TranslationManager.translate("recipes.composer.grindOverrideShort", "Override")
                                        font: Theme.captionFont
                                        color: Theme.textSecondaryColor
                                        Layout.alignment: Qt.AlignHCenter
                                        Accessible.ignored: true
                                    }
                                    StyledSwitch {
                                        checked: wizardPage.fGrindOverride
                                        Accessible.name: TranslationManager.translate("recipes.composer.grindOverride", "Override grind for this recipe")
                                        onToggled: {
                                            wizardPage.fGrindOverride = checked
                                            if (checked && grindField.text === "") {
                                                grindField.text = wizardPage.fInheritedGrind
                                                rpmField.text = wizardPage.fEquipmentRpmCapable
                                                        && wizardPage.fInheritedRpm > 0
                                                    ? String(wizardPage.fInheritedRpm) : ""
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Milk block (latte template, or added on the summary).
                        SectionCard {
                            visible: wizardPage.fHasMilk
                            title: TranslationManager.translate("recipes.composer.sectionSteam", "Steam")
                            Label {
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipes.composer.milkHint",
                                      "Keeps the steam heater warm while this recipe is active (5–9 min warm-up).")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                PickerField {
                                    Layout.fillWidth: true
                                    label: TranslationManager.translate("recipes.composer.pitcher", "Pitcher")
                                    value: wizardPage.fPitcherName
                                    placeholder: TranslationManager.translate("recipes.composer.choosePitcher", "Choose pitcher…")
                                    onActivated: pitcherPicker.open()
                                }
                                NumberField {
                                    id: milkField
                                    Layout.fillWidth: true
                                    label: TranslationManager.translate("recipes.composer.milkWeight", "Milk (g)")
                                    text: wizardPage.fMilkWeightG > 0 ? String(wizardPage.fMilkWeightG) : ""
                                    onEdited: function(newText) { wizardPage.fMilkWeightG = parseFloat(newText) || 0 }
                                }
                            }
                        }

                        // Hot-water block (americano/long black/hot-water tea
                        // templates, or added on the summary).
                        SectionCard {
                            visible: wizardPage.fHasWater
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

                        // Equipment: prefilled from the per-drink-type default;
                        // a row, not a step (it rarely changes once set).
                        SectionCard {
                            title: TranslationManager.translate("recipes.composer.sectionEquipment", "Equipment")
                            PickerField {
                                Layout.fillWidth: true
                                label: TranslationManager.translate("recipes.composer.equipmentLabel", "Grinder / basket package")
                                value: wizardPage.fEquipmentId > 0 ? wizardPage.fEquipmentName : ""
                                placeholder: trNone.text
                                onActivated: { MainController.equipmentStorage.requestInventory(); equipmentPicker.open() }
                            }
                        }

                        Label {
                            visible: wizardPage.bagSwapHint !== ""
                            Layout.fillWidth: true
                            text: wizardPage.bagSwapHint
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }

                        AccessibleButton {
                            Layout.alignment: Qt.AlignRight
                            primary: true
                            text: TranslationManager.translate("recipes.wizard.continue", "Continue")
                            accessibleName: TranslationManager.translate("recipes.wizard.accessible.continue", "Continue to the summary")
                            onClicked: { wizardPage._fromSummary = false; wizardPage.currentStep = "summary" }
                        }
                    }
                }

                // ===== Step 5: summary — the edit page =====
                Flickable {
                    contentHeight: summaryColumn.implicitHeight + Theme.scaled(24)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: summaryColumn
                        width: parent.width
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
                            }
                            AccessibleButton {
                                Layout.alignment: Qt.AlignBottom
                                text: TranslationManager.translate("common.cancel", "Cancel")
                                accessibleName: TranslationManager.translate("recipes.composer.accessible.cancel", "Cancel recipe editing")
                                onClicked: pageStack.pop()
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
                            value: wizardPage.hasBean
                                ? (wizardPage.fRoaster + " " + wizardPage.fCoffee).trim() : trNoBean.text
                            step: "bean"
                        }
                        SummaryRow {
                            visible: !wizardPage.isHotWaterTea
                            label: TranslationManager.translate("recipes.wizard.rowProfile", "Profile")
                            value: wizardPage.fProfileTitle !== "" ? wizardPage.fProfileTitle : "—"
                            step: "profile"
                        }
                        SummaryRow {
                            label: TranslationManager.translate("recipes.wizard.rowDetails", "Details")
                            value: {
                                var parts = []
                                var dose = parseFloat(doseField.text) || 0
                                var yieldG = parseFloat(yieldField.text) || 0
                                if (dose > 0 && yieldG > 0 && !wizardPage.isHotWaterTea)
                                    parts.push(dose.toFixed(1) + "g → " + yieldG.toFixed(1) + "g")
                                else if (dose > 0)
                                    parts.push(dose.toFixed(1) + "g")
                                if (wizardPage.isTeaDrink && wizardPage.fTeaTempC > 0)
                                    parts.push(Math.round(wizardPage.fTeaTempC) + "°C")
                                else if (Math.abs(wizardPage.fTempDeltaC) > 0.05)
                                    parts.push((wizardPage.fTempDeltaC > 0 ? "+" : "")
                                               + Theme.cDeltaToDisplay(wizardPage.fTempDeltaC).toFixed(0) + "°")
                                if (wizardPage.fHasMilk && wizardPage.fMilkWeightG > 0)
                                    parts.push(wizardPage.fMilkWeightG + "g "
                                        + TranslationManager.translate("recipes.list.milk", "milk"))
                                if (wizardPage.fHasWater && wizardPage.fVesselName !== "")
                                    parts.push(wizardPage.fVesselName)
                                return parts.length > 0 ? parts.join(" · ") : "—"
                            }
                            step: "details"
                        }
                        SummaryRow {
                            label: TranslationManager.translate("recipes.composer.sectionEquipment", "Equipment")
                            value: wizardPage.fEquipmentId > 0 ? wizardPage.fEquipmentName : trNone.text
                            step: "details"
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
                                    if (wizardPage.fHasMilk)
                                        wizardPage.openStep("details")
                                }
                            }
                            AccessibleButton {
                                text: wizardPage.fHasWater
                                    ? TranslationManager.translate("recipes.wizard.removeWater", "Remove hot water")
                                    : TranslationManager.translate("recipes.wizard.addWater", "Add hot water")
                                accessibleName: text
                                onClicked: {
                                    wizardPage.fHasWater = !wizardPage.fHasWater
                                    if (wizardPage.fHasWater)
                                        wizardPage.openStep("details")
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Label {
                            visible: wizardPage.errorMessage !== ""
                            Layout.fillWidth: true
                            text: wizardPage.errorMessage
                            font: Theme.bodyFont
                            color: Theme.errorColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
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
        id: equipmentPicker
        contentItem: ListView {
            clip: true
            model: [{ isNone: true }].concat(wizardPage._packages)
            delegate: ItemDelegate {
                width: ListView.view.width
                contentItem: Label {
                    text: modelData.isNone ? trNone.text
                        : (modelData.name
                           || ((modelData.grinderBrand || "") + " " + (modelData.grinderModel || "")).trim()
                           || ((modelData.basketBrand || "") + " " + (modelData.basketModel || "")).trim())
                    font: Theme.bodyFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                }
                Accessible.role: Accessible.Button
                Accessible.name: contentItem.text
                onClicked: {
                    if (modelData.isNone) {
                        wizardPage.fEquipmentId = 0
                        wizardPage.fEquipmentName = ""
                        wizardPage.fEquipmentRpmCapable = false
                    } else {
                        wizardPage.fEquipmentId = modelData.id
                        wizardPage.fEquipmentName = contentItem.text
                        wizardPage.fEquipmentRpmCapable = !!modelData.rpmCapable
                    }
                    equipmentPicker.close()
                }
            }
        }
    }

    PickerDialog {
        id: pitcherPicker
        height: Math.min(Theme.scaled(420), parent.height - Theme.scaled(80))
        contentItem: ListView {
            clip: true
            model: Settings.brew.steamPitcherPresets
            delegate: ItemDelegate {
                width: ListView.view.width
                visible: !modelData.disabled
                height: modelData.disabled ? 0 : implicitHeight
                contentItem: Label {
                    text: modelData.name || ""
                    font: Theme.bodyFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                }
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name || ""
                onClicked: {
                    // Snapshot BY VALUE (never a preset index).
                    wizardPage.fPitcherName = modelData.name || ""
                    wizardPage.fPitcherDurationSec = modelData.duration || 0
                    wizardPage.fPitcherFlow = modelData.flow || 0
                    wizardPage.fPitcherTemperatureC = modelData.temperature || 0
                    pitcherPicker.close()
                }
            }
        }
    }

    PickerDialog {
        id: vesselPicker
        height: Math.min(Theme.scaled(420), parent.height - Theme.scaled(80))
        contentItem: ListView {
            clip: true
            model: Settings.brew.waterVesselPresets
            delegate: ItemDelegate {
                width: ListView.view.width
                contentItem: Label {
                    text: modelData.name || ""
                    font: Theme.bodyFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                }
                Accessible.role: Accessible.Button
                Accessible.name: modelData.name || ""
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

    BottomBar {
        barColor: "transparent"
        onBackClicked: root.goBack()
    }
}
