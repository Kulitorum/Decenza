import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "DateUtils.js" as DateUtils

// Unified "Change Beans" dialog (bean-bag-inventory): one ranked search across
// inventory bags (Tier 0), Bean Base canonical, and the local shot history,
// followed by a Bag Details form for non-inventory picks. Selection semantics
// depend on `context`:
//   "brew" / "inventory" / "idle"  ->  set Settings.dye.activeBagId
//   "postShot"                     ->  set activeBagId AND rewrite the just-saved
//                                      shot's snapshot (the "wrong bag" fix path)
//   "historicalShot"               ->  rewrite only that shot's snapshot;
//                                      activeBagId untouched
//
// Extra entry point (bag inventory page): openForEdit(bag) updates the same
// bag row in place.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(560), parent ? parent.width * 0.95 : Theme.scaled(560))
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    property string context: "brew"   // "brew" | "inventory" | "idle" | "postShot" | "historicalShot"
    property var shotId: 0            // shot to retag (postShot / historicalShot)

    // Emitted after the context's selection semantics ran. `bag` is the
    // selected/created bag's map (CoffeeBag-shaped keys).
    signal bagSelected(int bagId, var bag)

    // "search" -> ranked result list; "form" -> bag details form
    property string mode: "search"
    // "create" -> requestCreateBag on confirm; "edit" -> requestUpdateBag
    property string formMode: "create"
    // Bag kind (add-recipe-wizard-tea): stamped by the entry point ("Add
    // Coffee" / "Add Tea" / edit's bag row) and immutable after creation.
    // Tea mode is SUBTRACTION over the coffee form: the Visualizer canonical
    // lane is suppressed (coffee-only database), identity labels read
    // Brand/Tea, and roast level + grind/rpm + canonical-link affordances
    // are hidden. URL + Get info + photo + weight fields stay.
    property string bagKind: "coffee"
    readonly property bool isTea: bagKind === "tea"
    property int editBagId: -1
    property bool _awaitingCreate: false
    property bool _armedForm: false   // openForEdit pre-armed the form
    property string errorMessage: ""

    // Form state (editable controls write back via onTextEdited)
    property string fRoaster: ""
    property string fCoffee: ""
    property string fRoastDate: ""
    property string fRoastLevel: ""
    property string fBeanBaseId: ""
    property string fBeanBaseData: ""
    // True when the user linked/unlinked Bean Base in this edit session —
    // Save then propagates the bag's link to all its shots.
    property bool fLinkDirty: false
    // Grinder identity (brand/model/burrs) is owned by the equipment package, not
    // the bag (add-equipment-packages); only the grind-setting dial-in stays here.
    // The bag points at a package via fEquipmentId; brand/model/burrs are the
    // resolved read-only display (refreshed via EquipmentStorage.packageReady).
    property string fGrinderSetting: ""
    property string fRpm: ""           // grinder rpm dial-in (string form; "" = unset)
    property int fEquipmentId: -1
    property string fEquipmentName: ""  // package display name (resolved via packageReady)
    property string fEquipmentBrand: ""
    property string fEquipmentModel: ""
    property string fEquipmentBurrs: ""
    readonly property bool fEquipmentRpmCapable:
        Settings.dye.grinderRpmCapable(fEquipmentBrand, fEquipmentModel)
    // Display the package name (defaults to "{brand} {model}").
    readonly property string fEquipmentLabel: fEquipmentName.length > 0
        ? fEquipmentName
        : [fEquipmentBrand, fEquipmentModel].filter(function(s){ return s && s.length > 0 }).join(" ")
    property string fDose: ""         // text form; "" = unset
    property string fYield: ""
    // Yield anchor (add-yield-ratio-anchor): the bag's own yield is a spec —
    // a fixed weight OR a ratio of the dose, never both. Whichever field was
    // last edited is the anchor; the other shows derived and dimmed.
    property string fYieldRatio: ""   // text form; "" = unset
    property string fYieldAnchor: "none"   // "none" | "absolute" | "ratio"
    function syncDerivedBagYield() {
        var d = parseFloat(fDose) || 0
        if (fYieldAnchor === "ratio") {
            var r = parseFloat(fYieldRatio) || 0
            fYield = (d > 0 && r > 0) ? (d * r).toFixed(1) : ""
        } else if (fYieldAnchor === "absolute") {
            var y = parseFloat(fYield) || 0
            fYieldRatio = (d > 0 && y > 0) ? (y / d).toFixed(1) : ""
        }
    }
    property string fNotes: ""
    property bool fFreeze: false
    property string fFrozenDate: ""
    property string fDefrostDate: ""
    // Out-of-freezer storage lifecycle. storageHint is the PLAN for how beans
    // are kept when not in the freezer ("" = unset); openedDate marks the
    // current portion leaving airtight storage — a different event from
    // defrostDate (leaving the freezer), on a different axis. NEITHER is
    // freeze-gated: a frozen bag carries a plan for when it is thawed, and a
    // thawed portion can then be opened. frozenDate alone decides frozen-ness.
    property string fStorageHint: ""
    property string fOpenedDate: ""

    // Bean details (add-bag-detail-editing): the descriptive working keys of
    // the beanBaseData blob, editable for linked and manual bags alike. A
    // canonical link prefills them and shows a badge — it never locks a field.
    property string fOrigin: ""
    property string fRegion: ""
    property string fFarm: ""
    property string fProducer: ""
    property string fVariety: ""
    property string fElevation: ""
    property string fProcess: ""
    property string fHarvest: ""
    property string fQualityScore: ""
    property string fPlaceOfPurchase: ""
    property string fTastingNotes: ""
    property string fLink: ""
    // Tea detail keys (add-recipe-wizard-tea): the tea vocabulary of the same
    // blob — shown instead of the coffee-only fields when isTea. brewTempC /
    // leafGramsPer100Ml seed the recipe wizard, so they must land in the blob
    // (string form; extraction normalizes to Celsius / per-100ml numbers).
    property string fTeaType: ""
    property string fGarden: ""
    property string fCultivar: ""
    property string fFlush: ""
    property string fBrewTempC: ""
    property string fLeafRatio: ""
    property string fSteepTime: ""
    property bool detailsExpanded: false
    // Product URL when the form opened; a URL changed to a NON-EMPTY value on
    // save re-resolves the bag image (the cached og:image pixels describe the
    // old page). Clearing the URL keeps the cached image — there is nothing
    // to re-resolve from.
    property string _openedLink: ""
    // "Get info from page": fetch the product page's text and have the
    // configured AI extract bean details, filling only EMPTY fields. Gated on
    // an AI provider being configured (the button hides otherwise).
    property bool fetchingInfo: false
    property string infoStatus: ""
    // Stage-2 product photo URL for a bag being CREATED — the cache key is
    // the row id, which doesn't exist until onBagCreated.
    property string _extractedImageUrl: ""
    // URL captured at click time — completion signals are gated on THIS, not
    // the live field: editing the URL mid-fetch must neither wedge the busy
    // flag nor let a stale extraction (an LLM call is slow) fill a form it
    // wasn't requested for.
    property string _fetchUrl: ""

    // Stable failure codes from the C++ layers -> translated messages; other
    // strings (Qt transport errors, provider errors) pass through verbatim.
    function infoErrorText(error) {
        switch (error) {
        case "urlFetchUnsupported":
            return TranslationManager.translate("changebeans.form.getInfo.urlFetchUnsupported",
                "This page needs the AI to fetch it, which the configured provider can't do")
        case "invalidUrl":
            return TranslationManager.translate("changebeans.form.getInfo.invalidUrl", "Not a valid web address")
        case "notAWebPage":
            return TranslationManager.translate("changebeans.form.getInfo.notAWebPage", "That address is not a web page")
        case "emptyPage":
            return TranslationManager.translate("changebeans.form.getInfo.emptyPage", "The page returned no readable text")
        case "busy":
            return TranslationManager.translate("changebeans.form.getInfo.busy", "The AI is busy — try again in a moment")
        case "notConfigured":
            return TranslationManager.translate("changebeans.form.getInfo.notConfigured", "No AI provider configured")
        case "unreadable":
            return TranslationManager.translate("changebeans.form.getInfo.unreadable", "Could not read the AI's response")
        }
        return error
    }

    readonly property var formBeanBase: {
        if (!fBeanBaseData || fBeanBaseData.length === 0) return ({})
        try { return JSON.parse(fBeanBaseData) } catch (e) {
            // The C++ merge refuses to touch a corrupt blob, so the stored
            // data survives — but the form renders blank detail fields.
            console.warn("ChangeBeansDialog: corrupt beanBaseData for bag", editBagId, e)
            return ({})
        }
    }
    readonly property var _formAttrParts: {
        var parts = []
        if (fOrigin) parts.push(fOrigin)
        if (fVariety) parts.push(fVariety)
        if (fProcess) parts.push(fProcess)
        return parts
    }
    // Plain join for the accessibility string; joinWithBullet (styled bold dot,
    // HTML-escaped) for the displayed line.
    readonly property string formAttrLine: _formAttrParts.join("  ·  ")
    readonly property string formAttrLineRich: Theme.joinWithBullet(_formAttrParts)

    // Pull the editable detail fields out of the stored form blob,
    // fBeanBaseData (canonical pick, enrichment arrival, unlink, revert —
    // every fBeanBaseData mutation). NOT the live `stagedBlob` — that one is
    // derived FROM these fields.
    function syncDetailFieldsFromBlob() {
        var bb = formBeanBase
        function s(key) { return bb[key] !== undefined && bb[key] !== null ? String(bb[key]) : "" }
        fOrigin = s("origin"); fRegion = s("region"); fFarm = s("farm")
        fProducer = s("producer"); fVariety = s("variety"); fElevation = s("elevation")
        fProcess = s("process"); fHarvest = s("harvest"); fQualityScore = s("qualityScore")
        fPlaceOfPurchase = s("placeOfPurchase"); fTastingNotes = s("tastingNotes"); fLink = s("link")
        fTeaType = s("teaType"); fGarden = s("garden"); fCultivar = s("cultivar")
        fFlush = s("flush"); fBrewTempC = s("brewTempC"); fLeafRatio = s("leafGramsPer100Ml")
        fSteepTime = s("steepTime")
    }

    // The edits map handed to BeanBaseBlob::mergeBeanDetails. All detail keys
    // are always present (the form is the full truth for them: an emptied
    // field removes its key). Identity working keys ride along only when a
    // blob exists or details were entered — a plain rename of a detail-less
    // manual bag must not conjure a blob.
    function detailEdits() {
        var edits = {
            "origin": fOrigin, "region": fRegion, "farm": fFarm,
            "producer": fProducer, "variety": fVariety, "elevation": fElevation,
            "process": fProcess, "harvest": fHarvest, "qualityScore": fQualityScore,
            "placeOfPurchase": fPlaceOfPurchase, "tastingNotes": fTastingNotes, "link": fLink,
            "teaType": fTeaType, "garden": fGarden, "cultivar": fCultivar,
            "flush": fFlush, "brewTempC": fBrewTempC, "leafGramsPer100Ml": fLeafRatio,
            "steepTime": fSteepTime
        }
        var anyDetail = false
        for (var k in edits) {
            if (String(edits[k]).trim().length > 0) { anyDetail = true; break }
        }
        if (fBeanBaseData.length > 0 || anyDetail) {
            edits["roasterName"] = fRoaster.trim()
            edits["roastName"] = fCoffee.trim()
            edits["degree"] = fRoastLevel
        }
        return edits
    }

    // Live-staged blob (current form values merged over the stored one).
    // Recomputed on any field edit; feeds the Save path and the Revert gate.
    readonly property string stagedBlob: MainController.beanbase.mergeBeanDetails(fBeanBaseData, detailEdits())
    readonly property bool canRevert: fBeanBaseId.length > 0
        && MainController.beanbase.blobDiffersFromCanonical(stagedBlob)

    // Revert to Bean Base data: restore every canonical-supplied value over
    // the working keys, drop user additions the canonical entry lacked, and
    // SAVE — a revert persists like any edit (and pushes to Visualizer).
    function performRevert() {
        var reverted = MainController.beanbase.revertToCanonical(stagedBlob)
        fBeanBaseData = reverted
        syncDetailFieldsFromBlob()
        var bb = formBeanBase
        if (bb.roasterName) fRoaster = String(bb.roasterName)
        if (bb.roastName) fCoffee = String(bb.roastName)
        fRoastLevel = bb.degree ? String(bb.degree) : ""
        confirmForm()
    }

    // --- Manual-entry autosuggest (history + Bean Base canonical) ---
    // Canonical entries for the current form query; refreshed as the user
    // types in the roaster/coffee fields (C++ debounces + caches).
    property var formCanonicalEntries: []
    property string _formCanonicalQuery: ""
    // Bumped when the shot-history distinct cache refreshes (suggestions
    // re-evaluate, mirroring BrewDialog's pattern).
    property int _distinctVersion: 0

    function requestFormCanonical(q) {
        // Tea mode: no canonical autosuggest either (coffee-only database).
        if (isTea) {
            formCanonicalEntries = []
            _formCanonicalQuery = ""
            return
        }
        q = q.trim()
        if (q.length < 2) {
            formCanonicalEntries = []
            _formCanonicalQuery = ""
            return
        }
        _formCanonicalQuery = q.toLowerCase()
        MainController.beanbase.search(q)
    }

    function roasterSuggestions() {
        var _ = _distinctVersion
        var out = MainController.shotHistory ? MainController.shotHistory.getDistinctBeanBrands().slice() : []
        for (var i = 0; i < formCanonicalEntries.length; i++) {
            var name = formCanonicalEntries[i].roasterName
            if (name && out.indexOf(name) === -1) out.push(name)
        }
        return out
    }

    function coffeeSuggestions() {
        var _ = _distinctVersion
        var out = MainController.shotHistory ? MainController.shotHistory.getDistinctBeanTypesForBrand(fRoaster).slice() : []
        for (var i = 0; i < formCanonicalEntries.length; i++) {
            var entry = formCanonicalEntries[i]
            if (fRoaster.length > 0 && entry.roasterName
                && entry.roasterName.toLowerCase() !== fRoaster.toLowerCase())
                continue
            if (entry.roastName && out.indexOf(entry.roastName) === -1) out.push(entry.roastName)
        }
        return out
    }

    // A picked coffee suggestion that came from Bean Base carries the
    // canonical link — apply it like a search-bar pick (enriched async).
    function adoptCanonicalByName(coffeeName) {
        for (var i = 0; i < formCanonicalEntries.length; i++) {
            var entry = formCanonicalEntries[i]
            if (entry.roastName !== coffeeName) continue
            if (fRoaster.length > 0 && entry.roasterName
                && entry.roasterName.toLowerCase() !== fRoaster.toLowerCase())
                continue
            fBeanBaseId = String(entry.id || "")
            fBeanBaseData = JSON.stringify(entry)
            syncDetailFieldsFromBlob()
            if (entry.roasterName) fRoaster = entry.roasterName
            fLinkDirty = true
            MainController.beanbase.fetchCanonicalDetails(entry)
            return
        }
    }

    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() { root._distinctVersion++ }
    }

    Connections {
        target: MainController.beanbase
        function onSearchResults(query, entries) {
            if (root.mode !== "form") return
            if (query.toLowerCase() !== root._formCanonicalQuery) return
            root.formCanonicalEntries = entries
        }
    }

    function todayIso() {
        var now = new Date()
        return now.getFullYear() + "-"
            + String(now.getMonth() + 1).padStart(2, "0") + "-"
            + String(now.getDate()).padStart(2, "0")
    }

    function sourceLabel(sources, tier) {
        if (tier === 0)
            return TranslationManager.translate("changebeans.source.inventory", "In inventory")
        switch (sources) {
        case "beanbase":
            return TranslationManager.translate("changebeans.source.beanbase", "Bean Base")
        case "history":
            return TranslationManager.translate("changebeans.source.history", "History")
        case "beanbase+history":
            return TranslationManager.translate("changebeans.source.beanbase", "Bean Base")
                + "  ·  " + TranslationManager.translate("changebeans.source.history", "History")
        }
        return ""
    }

    function resetForm() {
        fRoaster = ""; fCoffee = ""; fRoastDate = ""; fRoastLevel = ""
        fBeanBaseId = ""; fBeanBaseData = ""
        fLinkDirty = false
        fGrinderSetting = ""
        fEquipmentId = -1; fEquipmentName = ""; fEquipmentBrand = ""; fEquipmentModel = ""; fEquipmentBurrs = ""
        fRpm = ""
        fDose = ""; fYield = ""; fYieldRatio = ""; fYieldAnchor = "none"; fNotes = ""
        fFreeze = false; fFrozenDate = ""; fDefrostDate = ""
        fStorageHint = ""; fOpenedDate = ""
        syncDetailFieldsFromBlob()   // blob is empty: clears every detail field
        detailsExpanded = false
        _openedLink = ""
        fetchingInfo = false
        infoStatus = ""
        _fetchUrl = ""
        _extractedImageUrl = ""
        errorMessage = ""
    }

    function prefillFromBag(bag) {
        fRoaster = bag.roasterName || ""
        fCoffee = bag.coffeeName || ""
        fRoastLevel = bag.roastLevel || ""
        fBeanBaseId = bag.beanBaseId ? String(bag.beanBaseId) : ""
        fBeanBaseData = bag.beanBaseData || ""
        syncDetailFieldsFromBlob()
        _openedLink = fLink
        fGrinderSetting = bag.grinderSetting || ""
        fEquipmentId = bag.equipmentId || -1
        fRpm = (bag.rpm ?? 0) > 0 ? String(bag.rpm) : ""
        // Resolve the package's name + grinder identity for the read-only label
        // (packageReady fills fEquipmentName/Brand/Model/Burrs below).
        fEquipmentName = ""; fEquipmentBrand = ""; fEquipmentModel = ""; fEquipmentBurrs = ""
        if (fEquipmentId > 0 && MainController.equipmentStorage)
            MainController.equipmentStorage.requestPackage(fEquipmentId)
        // toFixed(1) (not String()) so a non-exact double like 37.8 prefills as
        // "37.8", not "37.800000000000004" — matching the brew-settings format.
        fDose = (bag.doseWeightG ?? 0) > 0 ? Number(bag.doseWeightG).toFixed(1) : ""
        // Yield spec: the anchored field gets the stored value; the other
        // derives through the dose. Search-model history rows carry the same
        // yieldValue/yieldMode keys as stored bags.
        var yMode = bag.yieldMode || "none"
        var yVal = bag.yieldValue ?? 0
        if ((yMode === "ratio" || yMode === "absolute") && yVal > 0) {
            fYieldAnchor = yMode
            if (yMode === "ratio") { fYieldRatio = Number(yVal).toFixed(1); fYield = "" }
            else { fYield = Number(yVal).toFixed(1); fYieldRatio = "" }
        } else {
            fYieldAnchor = "none"; fYield = ""; fYieldRatio = ""
        }
        syncDerivedBagYield()
    }

    // Tier 1-4 search result -> creation form. Roast date is ALWAYS blank and
    // never inferred — a new bag is a new roast date. Identity is prefilled
    // but stays editable, linked or not (add-bag-detail-editing): the link is
    // a badge, never a lock.
    function openFormFromResult(row) {
        resetForm()
        formMode = "create"
        editBagId = -1
        prefillFromBag(row)
        fRoastDate = ""
        mode = "form"
        if (fBeanBaseId.length === 0)
            editLinkBar.prefill([fRoaster, fCoffee].filter(function(x) { return x.length > 0 }).join(" "))
    }

    function openManualEntry() {
        resetForm()
        formMode = "create"
        editBagId = -1
        mode = "form"
    }

    // "Add Tea" entry point (add-recipe-wizard-tea): tea mode, opening on the
    // past-tea-bags search when any exist (the re-buy flow) or straight on
    // the form when none do. The caller decides via hasTeaBags — it has the
    // inventory list; the dialog would only know it async.
    function openTeaEntry(hasTeaBags) {
        bagKind = "tea"
        if (hasTeaBags) {
            open()
            return
        }
        openManualEntry()
        _armedForm = true
        open()
    }

    // Edit mode: update the existing bag row in place (activeBagId untouched).
    // Pre-filled INCLUDING the roast date. The form follows the bag's kind.
    function openForEdit(bag) {
        resetForm()
        bagKind = String(bag.kind || "") === "tea" ? "tea" : "coffee"
        formMode = "edit"
        editBagId = bag.id
        prefillFromBag(bag)
        fRoastDate = bag.roastDate || ""
        fNotes = bag.notes || ""
        fFrozenDate = bag.frozenDate || ""
        fDefrostDate = bag.defrostDate || ""
        fFreeze = fFrozenDate.length > 0
        fStorageHint = bag.storageHint || ""
        fOpenedDate = bag.openedDate || ""
        mode = "form"
        _armedForm = true
        open()
        if (fBeanBaseId.length === 0)
            editLinkBar.prefill([fRoaster, fCoffee].filter(function(x) { return x.length > 0 }).join(" "))
    }

    // "Find in Bean Base" on the bag card: edit mode with the link search
    // pre-run, so the canonical results pop up immediately.
    function openForEditAndLink(bag) {
        openForEdit(bag)
        if (fBeanBaseId.length === 0)
            editLinkBar.prefillAndSearch([fRoaster, fCoffee].filter(function(x) { return x.length > 0 }).join(" "))
    }

    // Context-dependent selection semantics. `bag` must carry the bag-shaped keys.
    function applySelection(bagId, bag) {
        if (root.context === "historicalShot") {
            updateShotSnapshot(bagId, bag)
        } else {
            Settings.dye.activeBagId = bagId
            if (root.context === "postShot")
                updateShotSnapshot(bagId, bag)
        }
        root.bagSelected(bagId, bag)
    }

    function updateShotSnapshot(bagId, bag) {
        var sid = root.shotId ?? 0
        if (sid <= 0 || !MainController.shotHistory) return
        MainController.shotHistory.requestUpdateShotMetadata(sid, {
            "beanBrand": bag.roasterName || "",
            "beanType": bag.coffeeName || "",
            "roastDate": bag.roastDate || "",
            "roastLevel": bag.roastLevel || "",
            "beanBaseJson": bag.beanBaseData || "",
            "beanBaseId": bag.beanBaseId ? String(bag.beanBaseId) : "",
            "bagId": bagId,
            "frozenDate": bag.frozenDate || "",
            "defrostDate": bag.defrostDate || "",
            "storageHint": bag.storageHint || "",
            "openedDate": bag.openedDate || ""
        })
    }

    // Inventory rows carry the bag id under "id" (CoffeeBag::toVariantMap);
    // the model's bagId role/key is only set on non-inventory rows (-1).
    // Accept either so a future C++ normalization doesn't break this.
    function resolveBagId(row) {
        if (row.bagId !== undefined && row.bagId > 0) return row.bagId
        if (row.id !== undefined && row.id > 0) return row.id
        return -1
    }

    function selectResult(row) {
        var bagId = resolveBagId(row)
        if (row.tier === 0 && bagId > 0) {
            if (root.context === "inventory") {
                // "Add New Bag" → picking an existing inventory bag means
                // "another bag of the same coffee" (e.g. a fresh purchase):
                // open the creation form pre-filled from it with the bean
                // identity prefilled (and editable) — same as picking a History
                // result — and the roast date blank. A separate bag is created;
                // two bags of one coffee, with their own dates/freeze, is
                // expected and fine.
                openFormFromResult(row)
            } else {
                // Switching contexts (brew / idle / post-shot / historical):
                // pick the existing bag, no new row.
                applySelection(bagId, row)
                root.close()
            }
        } else {
            openFormFromResult(row)
        }
    }

    function parseWeight(text) {
        var v = parseFloat(String(text).replace(",", "."))
        return (isNaN(v) || v < 0) ? 0 : v
    }

    function confirmForm() {
        Qt.inputMethod.commit()
        errorMessage = ""
        if (fRoaster.trim().length === 0 && fCoffee.trim().length === 0) {
            errorMessage = TranslationManager.translate(
                "changebeans.form.identityRequired", "Enter a roaster or coffee name")
            return
        }
        // Merge the edited detail fields into the blob (canonical snapshot
        // captured on the first edit of a linked bag; cleared fields removed).
        var mergedBlob = stagedBlob
        // A URL changed to a non-empty value re-resolves the bag image — the
        // cached og:image pixels describe the old page. Linked bags key the
        // cache by canonical id; manual bags by their row id (create mode
        // handles the manual case in onBagCreated, once the id exists).
        var imageKey = fBeanBaseId.length > 0 ? fBeanBaseId
                     : (formMode === "edit" && editBagId > 0 ? "bag-" + editBagId : "")
        if (imageKey.length > 0 && fLink.trim() !== _openedLink && fLink.trim().length > 0)
            MainController.beanbase.refreshBagImage(imageKey, fCoffee.trim(), fLink.trim())
        var fields = {
            "roasterName": fRoaster.trim(),
            "coffeeName": fCoffee.trim(),
            "roastDate": fRoastDate.length === 10 ? fRoastDate : "",
            // Tea bags carry no roast level (teaType lives in the blob).
            "roastLevel": isTea ? "" : fRoastLevel,
            "beanBaseId": fBeanBaseId,
            "beanBaseData": mergedBlob,
            "grinderSetting": isTea ? "" : fGrinderSetting.trim(),
            "rpm": isTea ? 0 : (parseInt(fRpm) || 0),
            "doseWeightG": parseWeight(fDose),
            // Yield spec: only the anchor is stored (one value + a mode).
            "yieldValue": fYieldAnchor === "ratio"
                ? ((parseFloat(fYieldRatio) || 0) > 0
                   ? Math.max(0.5, Math.min(6.0, parseFloat(fYieldRatio))) : 0)
                : parseWeight(fYield),
            "yieldMode": (fYieldAnchor === "ratio" && (parseFloat(fYieldRatio) || 0) > 0) ? "ratio"
                       : (fYieldAnchor === "absolute" && parseWeight(fYield) > 0) ? "absolute"
                       : "none",
            "notes": fNotes,
            "frozenDate": fFreeze ? (fFrozenDate.length === 10 ? fFrozenDate : todayIso()) : "",
            // Out-of-freezer storage plan: how the beans are kept when NOT in
            // the freezer. Orthogonal to the freeze axis, so it is written in
            // every freeze state — a plan is most useful precisely while the
            // bag is frozen ("when this is thawed, it goes in a vacuum jar").
            "storageHint": fStorageHint
        }
        // kind is stamped at creation only (immutable identity; the edit
        // path never writes it).
        if (formMode !== "edit")
            fields["kind"] = bagKind
        if (formMode === "edit") {
            // defrostDate follows the freeze toggle, unlike storageHint/openedDate
            // below — and that asymmetry is deliberate, not the bug this change
            // fixed. frozenDate and defrostDate are the SAME axis (the freezer):
            // turning the toggle off says this bag is not stored frozen, which
            // retires that axis as a unit, and a thaw date without a freeze date
            // would be a thaw from a freezer the bag was never in. The bug was
            // freezing clearing OTHER axes. Cross-axis clearing is the error;
            // within-axis is coherent.
            fields["defrostDate"] = fFreeze ? (fDefrostDate.length === 10 ? fDefrostDate : "") : ""
            // openedDate marks the current portion leaving airtight storage —
            // the sibling of defrostDate (leaving the freezer), not its
            // non-frozen substitute. Edit-mode only (the "Mark Opened" quick
            // action on the bag card is the everyday path). Independent of the
            // freeze axis: a bag frozen, later thawed, then moved to a counter
            // jar carries both.
            fields["openedDate"] = fOpenedDate.length === 10 ? fOpenedDate : ""
            // Re-point the bag's equipment package (<=0 -> NULL via the column hook).
            fields["equipmentId"] = fEquipmentId
            // A link change fixes the whole bag: propagate the (new or
            // cleared) canonical link onto every shot referencing it.
            MainController.bagStorage.requestUpdateBag(editBagId, fields, fLinkDirty)
            // If this is the active bag, sync the active equipment selection so
            // Brew Settings reflects the change.
            if (editBagId === Settings.dye.activeBagId)
                Settings.dye.activeEquipmentId = fEquipmentId > 0 ? fEquipmentId : -1
            root.close()
        } else {
            fields["defrostDate"] = ""
            fields["inInventory"] = true
            // Persist the equipment package picked in the create form too (the
            // picker row is shown in both modes); <=0 -> NULL via the column hook.
            fields["equipmentId"] = fEquipmentId
            _awaitingCreate = true
            MainController.bagStorage.requestCreateBag(fields)
        }
    }

    // Revert confirmation: local edits (including a user-added URL the
    // canonical entry lacked) are discarded and the bag saved immediately.
    Dialog {
        id: revertConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(420), parent ? parent.width * 0.9 : Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape
        padding: Theme.scaled(20)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(16)

            Accessible.role: Accessible.Dialog
            Accessible.name: trRevertConfirm.text

            Tr {
                id: trRevertConfirm
                Layout.fillWidth: true
                key: "changebeans.form.revert.confirm"
                fallback: "Restore the original Bean Base data? Your edits to the bean details are discarded and the bag is saved."
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.scaled(10)

                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: revertConfirmDialog.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("changebeans.form.revert.confirmButton", "Revert & save")
                    accessibleName: TranslationManager.translate("changebeans.form.revert.accessible", "Revert edited values to the original Bean Base data and save")
                    onClicked: {
                        revertConfirmDialog.close()
                        root.performRevert()
                    }
                }
            }
        }
    }

    Connections {
        target: MainController.bagStorage
        function onBagCreated(bagId, bag) {
            if (!root._awaitingCreate) return
            root._awaitingCreate = false
            if (bagId <= 0) {
                root.errorMessage = TranslationManager.translate(
                    "changebeans.form.createFailed", "Could not save the bag — please try again")
                return
            }
            // A manual bag created with a product URL: warm its image now
            // that the row id (= its cache key) exists. Linked bags were
            // warmed at entry pick under their canonical id. A stage-2
            // extraction photo URL (SPA page, no og:image) wins directly.
            if (root._extractedImageUrl.length > 0)
                MainController.beanbase.cacheBagImageFromUrl("bag-" + bagId, root._extractedImageUrl)
            else if (root.fBeanBaseId.length === 0 && root.fLink.trim().length > 0)
                MainController.beanbase.ensureBagImage(
                    "bag-" + bagId, root.fCoffee.trim(), root.fLink.trim())
            root.applySelection(bagId, bag)
            root.close()
        }
    }

    // Re-point THIS bag's equipment package. The picker doesn't switch the
    // active bag (applyToActiveBag:false); we record the chosen id and resolve
    // its grinder identity for the label via packageReady below. Persisted on
    // Save (fields.equipmentId).
    SwitchEquipmentDialog {
        id: bagEquipmentDialog
        applyToActiveBag: false
        onPackageSaved: function(packageId) {
            root.fEquipmentId = packageId
            if (MainController.equipmentStorage)
                MainController.equipmentStorage.requestPackage(packageId)
        }
    }
    EquipmentInfoDialog {
        id: bagEquipmentInfoDialog
    }
    // Shared details viewer for the search results' info button — one instance,
    // re-pointed at whichever canonical row was tapped. imageKey defaults to the
    // blob's canonical id, so the popup resolves and shows the extracted product
    // photo on open (same viewer as BagCard / BeanInfoPage).
    BeanBaseDetailsPopup {
        id: canonicalPreviewPopup
    }
    Connections {
        target: MainController.equipmentStorage
        // Fills the read-only label for whichever package this bag now points at
        // (both the edit-open prefill and a fresh pick funnel through fEquipmentId).
        function onPackageReady(packageId, pkg) {
            if (packageId !== root.fEquipmentId) return
            root.fEquipmentBrand = pkg.grinderBrand || ""
            root.fEquipmentModel = pkg.grinderModel || ""
            root.fEquipmentBurrs = pkg.grinderBurrs || ""
            root.fEquipmentName = (pkg.name && String(pkg.name).length > 0) ? String(pkg.name) : ""
        }
    }

    onAboutToShow: {
        // Scope the unified search to this entry's bag kind: tea mode drops
        // the Visualizer canonical lane and keeps only known tea bags.
        MainController.beanSearch.bagKind = isTea ? "tea" : ""
        if (!_armedForm) {
            mode = "search"
            errorMessage = ""
            searchField.text = ""
            MainController.beanSearch.query = ""
            MainController.beanSearch.refresh()
        }
        _armedForm = false
    }

    onOpened: {
        if (mode === "search")
            searchField.forceActiveFocus()
        else if (fRoaster.length === 0 && fCoffee.length === 0)
            roasterInput.forceActiveFocus()
        else
            roastDateField.textField.forceActiveFocus()

        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(mode === "search"
                ? TranslationManager.translate("changebeans.accessible.searchOpened", "Change Beans dialog. Search beans")
                : formTitleText.text)
        }
    }

    onClosed: {
        _awaitingCreate = false
        // Abandoning the dialog cancels a Get info in flight: a late page
        // result must not spend an AI call for a form nobody is looking at.
        fetchingInfo = false
        infoStatus = ""
        _fetchUrl = ""
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: KeyboardAwareContainer {
        id: keyboardContainer
        inOverlay: true
        implicitWidth: root.width
        implicitHeight: Math.min(mainColumn.implicitHeight,
                                 root.parent ? root.parent.height * 0.9 : mainColumn.implicitHeight)
        textFields: [searchField, roasterInput.textField, coffeeInput.textField, roastDateField.textField,
                     grindSettingInput, doseInput, yieldInput, yieldRatioInput,
                     notesInput, frozenDateField.textField, defrostDateField.textField,
                     openedDateField.textField,
                     originField.textField, regionField.textField, farmField.textField,
                     producerField.textField, varietyField.textField, elevationField.textField,
                     processField.textField, harvestField.textField, qualityScoreField.textField,
                     placeOfPurchaseField.textField, tastingNotesField.textField, urlField.textField]
        targetFlickable: formFlickable

        ColumnLayout {
            id: mainColumn
            anchors.fill: parent
            spacing: 0

            // ===== Header =====
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    id: formTitleText
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        var _ = TranslationManager.translationVersion
                        if (root.mode === "search")
                            return root.context === "inventory"
                                ? TranslationManager.translate("changebeans.title.addBag", "Add Bag")
                                : TranslationManager.translate("changebeans.title", "Change Beans")
                        return root.formMode === "edit"
                            ? TranslationManager.translate("changebeans.title.editBag", "Edit Bag")
                            : TranslationManager.translate("changebeans.title.newBag", "New Bag")
                    }
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.ignored: true  // announced on open
                }

                // Form actions live in the header so they're reachable
                // without scrolling past the fields.
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(8)

                    HideKeyboardButton {
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    AccessibleButton {
                        visible: root.mode === "form"
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.scaled(38)
                        leftPadding: Theme.scaled(14)
                        rightPadding: Theme.scaled(14)
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("changebeans.form.accessible.cancel", "Cancel bag details")
                        onClicked: root.close()
                    }

                    AccessibleButton {
                        visible: root.mode === "form"
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.scaled(38)
                        leftPadding: Theme.scaled(14)
                        rightPadding: Theme.scaled(14)
                        primary: true
                        enabled: !root._awaitingCreate
                        text: root.formMode === "edit"
                            ? TranslationManager.translate("common.save", "Save")
                            : TranslationManager.translate("changebeans.form.create", "Add Bag")
                        accessibleName: root.formMode === "edit"
                            ? TranslationManager.translate("changebeans.form.accessible.save", "Save bag changes")
                            : TranslationManager.translate("changebeans.form.accessible.create", "Create bag")
                        onClicked: root.confirmForm()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // ===== Search page =====
            ColumnLayout {
                visible: root.mode === "search"
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(16)
                spacing: Theme.scaled(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    StyledTextField {
                        id: searchField
                        Layout.fillWidth: true
                        placeholder: TranslationManager.translate("changebeans.search.placeholder", "Search roaster or coffee")
                        accessibleName: TranslationManager.translate("changebeans.search.accessible", "Search beans")
                        onTextEdited: MainController.beanSearch.query = text
                    }

                    BusyIndicator {
                        running: MainController.beanSearch.searching
                        visible: running
                        implicitWidth: Theme.scaled(28)
                        implicitHeight: Theme.scaled(28)
                        Accessible.ignored: true
                    }
                }

                ListView {
                    id: resultsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(340)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: MainController.beanSearch
                    spacing: Theme.scaled(4)

                    delegate: Rectangle {
                        id: resultRow
                        width: resultsList.width
                        height: Theme.scaled(56)
                        radius: Theme.scaled(8)
                        color: Theme.backgroundColor
                        border.width: 1
                        border.color: resultRow.isActiveBag ? Theme.primaryColor : Theme.borderColor
                        Accessible.ignored: true

                        // Delegates are recreated on every model reset, so the
                        // one-shot get() read stays in sync with the row data.
                        readonly property var rowData: MainController.beanSearch.get(index)
                        readonly property int rowBagId: root.resolveBagId(resultRow.rowData)
                        readonly property bool isActiveBag: model.tier === 0 && rowBagId > 0
                            && rowBagId === Settings.dye.activeBagId

                        // Canonical-sourced rows (Bean Base autocomplete, alone
                        // or merged with history) carry the full descriptive
                        // blob — offer the details preview, including the
                        // extracted product photo, so same-name near-duplicates
                        // can be inspected before picking. Other tiers have
                        // nothing beyond what the row already shows.
                        readonly property bool canPreview:
                            (model.sources === "beanbase" || model.sources === "beanbase+history")
                            && ((resultRow.rowData && resultRow.rowData.beanBaseData) || "").length > 0

                        readonly property string primaryText: {
                            var coffee = model.coffeeName || ""
                            var roaster = model.roasterName || ""
                            if (coffee.length > 0 && roaster.length > 0) return roaster + " " + coffee
                            return coffee.length > 0 ? coffee : roaster
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            spacing: Theme.scaled(8)
                            // Sit above the row-selection overlay so the info
                            // button gets its own taps; the non-interactive
                            // Texts and chip don't grab events, so a tap on them
                            // still falls through to the overlay and selects the
                            // row.
                            z: 1

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(2)

                                Text {
                                    Layout.fillWidth: true
                                    text: resultRow.primaryText
                                    // Bean Base free text — never let AutoText parse it as markup
                                    textFormat: Text.PlainText
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.bodyFont.pixelSize
                                    font.bold: true
                                    color: Theme.textColor
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    // Bean Base rows have no roast date; their detail line
                                    // (roast level · origin · notes) is what tells the
                                    // canonical DB's same-name near-duplicates apart.
                                    text: model.roastDate || model.detail || ""
                                    // Bean Base free text — never let AutoText parse it as markup
                                    textFormat: Text.PlainText
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }
                            }

                            // Details preview — opens the full canonical entry
                            // (all attributes + the extracted product photo, so
                            // the user can see at a glance whether one exists).
                            StyledIconButton {
                                Layout.alignment: Qt.AlignVCenter
                                visible: resultRow.canPreview
                                implicitWidth: Theme.scaled(36)
                                implicitHeight: Theme.scaled(36)
                                icon.source: "qrc:/icons/info.svg"
                                accessibleName: TranslationManager.translate("changebeans.result.details", "Show all bean details")
                                onClicked: {
                                    canonicalPreviewPopup.beanBaseJson =
                                        (resultRow.rowData && resultRow.rowData.beanBaseData) || ""
                                    canonicalPreviewPopup.open()
                                }
                            }

                            // Source chip
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: chipText.implicitWidth + Theme.scaled(16)
                                implicitHeight: chipText.implicitHeight + Theme.scaled(8)
                                radius: height / 2
                                color: model.tier === 0 ? Theme.primaryColor : Theme.backgroundColor
                                border.width: model.tier === 0 ? 0 : 1
                                border.color: Theme.textSecondaryColor

                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: root.sourceLabel(model.sources, model.tier)
                                    font: Theme.captionFont
                                    color: model.tier === 0 ? Theme.primaryContrastColor : Theme.textSecondaryColor
                                    Accessible.ignored: true
                                }
                            }
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: resultRow.primaryText
                                + (model.detail ? ", " + model.detail : "")
                                + ", " + root.sourceLabel(model.sources, model.tier)
                                + (resultRow.isActiveBag
                                    ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                            accessibleItem: resultRow
                            onAccessibleClicked: {
                                Qt.inputMethod.commit()
                                root.selectResult(MainController.beanSearch.get(index))
                            }
                        }
                    }

                    // Tier 5: manual entry — static row, not in the model.
                    // Top of the list while the search is empty (a fresh bag
                    // is one tap away); last row once a query narrows things.
                    header: MainController.beanSearch.query.length === 0 ? manualEntryComponent : null
                    footer: MainController.beanSearch.query.length > 0 ? manualEntryComponent : null

                    Component {
                        id: manualEntryComponent
                        Item {
                        width: resultsList.width
                        height: Theme.scaled(60)

                        Rectangle {
                            id: manualRow
                            anchors.fill: parent
                            anchors.topMargin: Theme.scaled(4)
                            anchors.bottomMargin: Theme.scaled(4)
                            radius: Theme.scaled(8)
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.textSecondaryColor
                            Accessible.ignored: true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.scaled(12)
                                spacing: Theme.scaled(8)

                                ColoredIcon {
                                    source: "qrc:/icons/plus.svg"
                                    iconWidth: Theme.scaled(16)
                                    iconHeight: Theme.scaled(16)
                                    iconColor: Theme.textColor
                                    Accessible.ignored: true
                                }

                                Tr {
                                    Layout.fillWidth: true
                                    key: "changebeans.enterManually"
                                    fallback: "Enter manually"
                                    font: Theme.bodyFont
                                    color: Theme.textColor
                                    Accessible.ignored: true
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("changebeans.accessible.enterManually", "Enter bean details manually")
                                accessibleItem: manualRow
                                onAccessibleClicked: {
                                    Qt.inputMethod.commit()
                                    root.openManualEntry()
                                }
                            }
                        }
                        }
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("changebeans.accessible.cancel", "Cancel changing beans")
                    onClicked: root.close()
                }
            }

            // ===== Bag details form =====
            Flickable {
                id: formFlickable
                visible: root.mode === "form"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(formColumn.implicitHeight + keyboardContainer.estimatedKeyboardHeight,
                                                 root.parent ? root.parent.height * 0.9 - Theme.scaled(60) : formColumn.implicitHeight)
                contentHeight: formColumn.implicitHeight + keyboardContainer.estimatedKeyboardHeight
                contentWidth: width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                ColumnLayout {
                    id: formColumn
                    width: formFlickable.width
                    spacing: Theme.scaled(10)

                    Item { Layout.preferredHeight: Theme.scaled(6) }

                    // --- Bean Base link: upgrade a free-text bag to its
                    // canonical record (edit mode: saving propagates the link
                    // to every shot pulled with this bag). Visible in CREATE
                    // mode too — a bag built from a history pick must be
                    // linkable in the same form, not save-then-"Find in Bean
                    // Base" from the card.
                    Item {
                        // Tea bags never link canonically (Visualizer's
                        // database is coffee-only) — the whole bar is hidden.
                        visible: !root.isTea
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        implicitHeight: editLinkBar.implicitHeight

                        BeanBaseSearchBar {
                            id: editLinkBar
                            anchors.left: parent.left
                            anchors.right: parent.right
                            linked: root.fBeanBaseId.length > 0
                            linkedLabel: {
                                var bb = root.formBeanBase
                                var name = [bb.roasterName || root.fRoaster, bb.roastName || root.fCoffee]
                                    .filter(function(x) { return x && x.length > 0 }).join(" ")
                                return name
                            }

                            onEntrySelected: function(entry) {
                                root.fBeanBaseId = String(entry.id || "")
                                root.fBeanBaseData = JSON.stringify(entry)
                                root.syncDetailFieldsFromBlob()
                                if (entry.roasterName) root.fRoaster = entry.roasterName
                                if (entry.roastName) root.fCoffee = entry.roastName
                                root.fLinkDirty = true
                                // Best-effort attribute enrichment (origin,
                                // variety, process, ...) — merged below when
                                // canonicalDetails arrives.
                                MainController.beanbase.fetchCanonicalDetails(entry)
                                // Warm the bag-photo file cache so the
                                // inventory card shows the image right away.
                                MainController.beanbase.ensureBagImage(
                                    String(entry.id || ""), entry.roastName || "", entry.link || "")
                            }
                            onUnlinkRequested: {
                                root.fBeanBaseId = ""
                                root.fBeanBaseData = ""
                                // The whole blob clears with the link — the
                                // canonical attribute cache AND any unsaved
                                // detail edits typed this session (bean-base-
                                // search: unlink preserves only the identity
                                // fields).
                                root.syncDetailFieldsFromBlob()
                                root.fLinkDirty = true
                            }
                        }
                    }

                    Connections {
                        target: MainController.beanbase
                        function onPageTextReady(url, text) {
                            if (!root.fetchingInfo || url !== root._fetchUrl) return
                            root.infoStatus = TranslationManager.translate(
                                "changebeans.form.getInfo.extracting", "Extracting details…")
                            MainController.aiManager.extractCoffeeBagDetails(url, text, root.bagKind)
                        }
                        function onPageTextFailed(url, error) {
                            if (!root.fetchingInfo || url !== root._fetchUrl) return
                            // Stage 2 (add-recipe-wizard-tea): an empty page is
                            // the JS-rendered-shop signature — let the provider
                            // fetch the URL itself via its web-fetch tool. Other
                            // failures (bad URL, site down) would fail there too.
                            if (error === "emptyPage" && MainController.aiManager.supportsUrlExtraction()) {
                                root.infoStatus = TranslationManager.translate(
                                    "changebeans.form.getInfo.stage2", "Page is script-rendered — asking the AI to fetch it…")
                                MainController.aiManager.extractCoffeeBagDetailsFromUrl(url, url, root.bagKind)
                                return
                            }
                            root.fetchingInfo = false
                            root.infoStatus = TranslationManager.translate(
                                "changebeans.form.getInfo.pageFailed", "Couldn't read the page: %1")
                                .arg(root.infoErrorText(error))
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                AccessibilityManager.announce(root.infoStatus)
                        }
                    }

                    Connections {
                        target: MainController.aiManager
                        // Fill ONLY empty fields — the page never overrides
                        // something the user (or the canonical entry) set.
                        function onBagDetailsExtracted(requestToken, fields) {
                            if (!root.fetchingInfo || requestToken !== root._fetchUrl) return
                            root.fetchingInfo = false
                            var applied = 0
                            function take(key, current, apply) {
                                if (fields[key] && String(current).trim().length === 0) {
                                    apply(String(fields[key]))
                                    applied++
                                }
                            }
                            take("origin", root.fOrigin, function(v) { root.fOrigin = v })
                            take("region", root.fRegion, function(v) { root.fRegion = v })
                            take("farm", root.fFarm, function(v) { root.fFarm = v })
                            take("producer", root.fProducer, function(v) { root.fProducer = v })
                            take("variety", root.fVariety, function(v) { root.fVariety = v })
                            take("elevation", root.fElevation, function(v) { root.fElevation = v })
                            take("process", root.fProcess, function(v) { root.fProcess = v })
                            take("harvest", root.fHarvest, function(v) { root.fHarvest = v })
                            take("tastingNotes", root.fTastingNotes, function(v) { root.fTastingNotes = v })
                            take("roastLevel", root.fRoastLevel, function(v) { root.fRoastLevel = v })
                            // Tea vocabulary (add-recipe-wizard-tea) — the tea
                            // prompt's structured keys, incl. the brewing data
                            // that seeds the recipe wizard.
                            take("teaType", root.fTeaType, function(v) { root.fTeaType = v })
                            take("garden", root.fGarden, function(v) { root.fGarden = v })
                            take("cultivar", root.fCultivar, function(v) { root.fCultivar = v })
                            take("flush", root.fFlush, function(v) { root.fFlush = v })
                            take("brewTempC", root.fBrewTempC, function(v) { root.fBrewTempC = v })
                            take("leafGramsPer100Ml", root.fLeafRatio, function(v) { root.fLeafRatio = v })
                            take("steepTime", root.fSteepTime, function(v) { root.fSteepTime = v })
                            // Stage-2 photo: the model returned the product
                            // image URL directly (no og:image on SPA pages).
                            // Edit mode caches it now; create mode stashes it
                            // until the row id (= cache key) exists.
                            if (fields["imageUrl"]) {
                                var imgKey = root.fBeanBaseId.length > 0 ? root.fBeanBaseId
                                    : (root.formMode === "edit" && root.editBagId > 0
                                        ? "bag-" + root.editBagId : "")
                                if (imgKey.length > 0)
                                    MainController.beanbase.cacheBagImageFromUrl(imgKey, String(fields["imageUrl"]))
                                else
                                    root._extractedImageUrl = String(fields["imageUrl"])
                            }
                            root.detailsExpanded = true
                            root.infoStatus = applied > 0
                                ? TranslationManager.translate("changebeans.form.getInfo.applied",
                                      "%1 field(s) filled from the page").arg(applied)
                                : TranslationManager.translate("changebeans.form.getInfo.nothing",
                                      "Nothing new found on the page")
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                AccessibilityManager.announce(root.infoStatus)
                        }
                        function onBagDetailsExtractionFailed(requestToken, error) {
                            if (!root.fetchingInfo || requestToken !== root._fetchUrl) return
                            root.fetchingInfo = false
                            root.infoStatus = root.infoErrorText(error)
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                AccessibilityManager.announce(root.infoStatus)
                        }
                    }

                    Connections {
                        target: MainController.beanbase
                        function onCanonicalDetails(canonicalId, attrs) {
                            if (root.mode !== "form" || root.fBeanBaseId !== canonicalId)
                                return
                            try {
                                var blob = JSON.parse(root.fBeanBaseData)
                                for (var key in attrs)
                                    blob[key] = attrs[key]
                                root.fBeanBaseData = JSON.stringify(blob)
                                root.syncDetailFieldsFromBlob()
                            } catch (e) {
                                // Keep the minimal entry; without the log the
                                // detail fields just never populate, which is
                                // indistinguishable from an empty API result.
                                console.warn("ChangeBeansDialog: dropping enrichment, corrupt staged blob:", e)
                            }
                        }
                    }

                    // --- Identity: always editable, linked or not.
                    // Tea mode relabels the same columns: Brand / Tea. ---
                    FieldRow {
                        labelKey: root.isTea ? "changebeans.form.brand" : "changebeans.form.roaster"
                        labelFallback: root.isTea ? "Brand:" : "Roaster:"

                        SuggestionField {
                            id: roasterInput
                            Layout.fillWidth: true
                            text: root.fRoaster
                            suggestions: root.roasterSuggestions()
                            accessibleName: TranslationManager.translate("changebeans.form.roaster.accessible", "Roaster")
                            onTextEdited: function(t) {
                                root.fRoaster = t
                                root.requestFormCanonical(t)
                            }
                        }
                    }

                    FieldRow {
                        labelKey: root.isTea ? "changebeans.form.tea" : "changebeans.form.coffee"
                        labelFallback: root.isTea ? "Tea:" : "Coffee:"

                        SuggestionField {
                            id: coffeeInput
                            Layout.fillWidth: true
                            text: root.fCoffee
                            suggestions: root.coffeeSuggestions()
                            accessibleName: TranslationManager.translate("changebeans.form.coffee.accessible", "Coffee name")
                            onTextEdited: function(t) {
                                root.fCoffee = t
                                root.requestFormCanonical(root.fRoaster.length > 0 ? root.fRoaster + " " + t : t)
                            }
                            // A Bean Base suggestion pick links the bag too.
                            onSuggestionSelected: function(t) { root.adoptCanonicalByName(t) }
                        }
                    }

                    // --- Roast date: ALWAYS blank in create modes, optional ---
                    BeanDateField {
                        id: roastDateField
                        labelKey: "changebeans.form.roastDate"
                        labelFallback: "Roasted:"
                        value: root.fRoastDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.roastDate.accessible", "Roast date, optional.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.roastDate.openCalendar", "Open calendar to pick roast date")
                        onValueEdited: function(dateString) { root.fRoastDate = dateString }
                    }

                    // --- Roast level: always editable. A canonical degree that
                    // is not one of the combo levels (e.g. "Light To
                    // Medium-light") is shown as the display text until the
                    // user actively picks a level.
                    FieldRow {
                        visible: !root.isTea
                        labelKey: "changebeans.form.roastLevel"
                        labelFallback: "Roast level:"

                        StyledComboBox {
                            id: roastLevelCombo
                            Layout.fillWidth: true
                            accessibleLabel: TranslationManager.translate("changebeans.form.roastLevel.accessible", "Roast level")
                            model: ["",
                                TranslationManager.translate("shotmetadata.roastlevel.light", "Light"),
                                TranslationManager.translate("shotmetadata.roastlevel.mediumlight", "Medium-Light"),
                                TranslationManager.translate("shotmetadata.roastlevel.medium", "Medium"),
                                TranslationManager.translate("shotmetadata.roastlevel.mediumdark", "Medium-Dark"),
                                TranslationManager.translate("shotmetadata.roastlevel.dark", "Dark")]
                            currentIndex: Math.max(0, model.indexOf(root.fRoastLevel))
                            displayText: root.fRoastLevel.length > 0 && currentIndex <= 0
                                ? root.fRoastLevel : currentText
                            onActivated: root.fRoastLevel = currentIndex > 0 ? currentText : ""
                        }
                    }

                    // --- Bean details (add-bag-detail-editing): the descriptive
                    // working keys of the beanBaseData blob. Collapsed by
                    // default with the origin·variety·process summary in the
                    // header; every field editable, linked or not. ---
                    // Header styled like the input controls around it (same
                    // fill + radius as StyledComboBox) so it reads as tappable,
                    // with a down/up chevron — a bare label + right-arrow read
                    // as static text with a navigation affordance.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        // Match StyledComboBox (the input controls around it): 36 / r8.
                        Layout.preferredHeight: Theme.scaled(36)
                        radius: Theme.scaled(8)
                        color: Qt.rgba(255, 255, 255, 0.1)

                        Accessible.role: Accessible.Button
                        Accessible.name: trBeanDetails.text
                            + (root.formAttrLine.length > 0 ? ", " + root.formAttrLine : "")
                            + ", " + (root.detailsExpanded
                                ? TranslationManager.translate("common.expanded", "expanded")
                                : TranslationManager.translate("common.collapsed", "collapsed"))
                        Accessible.focusable: true
                        Accessible.onPressAction: detailsHeaderArea.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            spacing: Theme.scaled(6)

                            Tr {
                                id: trBeanDetails
                                key: "changebeans.form.beanDetails"
                                fallback: "Bean details"
                                font: Theme.bodyFont
                                color: Theme.textColor
                                Accessible.ignored: true
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: !root.detailsExpanded && root.formAttrLine.length > 0
                                text: root.formAttrLineRich
                                textFormat: Text.StyledText
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }

                            Item { visible: root.detailsExpanded || root.formAttrLine.length === 0; Layout.fillWidth: true }

                            ColoredIcon {
                                source: "qrc:/icons/ArrowLeft.svg"
                                iconWidth: Theme.scaled(12)
                                iconHeight: Theme.scaled(12)
                                iconColor: Theme.textSecondaryColor
                                // ArrowLeft points west: -90 = down (expand
                                // opens below), +90 = up (collapse).
                                rotation: root.detailsExpanded ? 90 : -90
                                Accessible.ignored: true
                            }
                        }

                        MouseArea {
                            id: detailsHeaderArea
                            anchors.fill: parent
                            onClicked: root.detailsExpanded = !root.detailsExpanded
                        }
                    }

                    ColumnLayout {
                        visible: root.detailsExpanded
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        component DetailField: FieldRow {
                            id: detailRow
                            property string accessibleText: ""
                            property string value: ""
                            property alias textField: detailInput
                            signal edited(string text)
                            StyledTextField {
                                id: detailInput
                                Layout.fillWidth: true
                                text: detailRow.value
                                accessibleName: detailRow.accessibleText
                                onTextEdited: detailRow.edited(text)
                            }
                        }

                        DetailField {
                            id: urlField
                            labelKey: "changebeans.form.url"; labelFallback: "URL:"
                            accessibleText: TranslationManager.translate("changebeans.form.url.accessible", "Roaster product page URL")
                            value: root.fLink
                            onEdited: function(t) { root.fLink = t }
                        }

                        // "Get info from page": Visualizer-style extraction —
                        // fetch the page text, let the configured AI pull out
                        // the details, fill only fields still empty. Hidden
                        // without a URL or a configured AI provider.
                        RowLayout {
                            Layout.leftMargin: Theme.scaled(20)
                            Layout.rightMargin: Theme.scaled(20)
                            spacing: Theme.scaled(10)
                            visible: root.fLink.trim().length > 0
                                && MainController.aiManager && MainController.aiManager.isConfigured

                            AccessibleButton {
                                enabled: !root.fetchingInfo
                                text: TranslationManager.translate("changebeans.form.getInfo", "Get info from page")
                                accessibleName: TranslationManager.translate("changebeans.form.getInfo.accessible",
                                    "Fetch the product page and fill empty bean detail fields using AI")
                                onClicked: {
                                    root.fetchingInfo = true
                                    root._fetchUrl = root.fLink.trim()
                                    root.infoStatus = TranslationManager.translate(
                                        "changebeans.form.getInfo.fetching", "Reading page…")
                                    // The bag kind selects the extraction
                                    // vocabulary (tea: teaType + brewing data).
                                    MainController.beanbase.fetchPageText(root._fetchUrl)
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: root.infoStatus.length > 0
                                text: root.infoStatus
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.Wrap
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }
                        }

                        DetailField {
                            id: originField
                            labelKey: "beanbase.details.origin"; labelFallback: "Origin:"
                            accessibleText: TranslationManager.translate("beanbase.details.origin", "Origin")
                            value: root.fOrigin
                            onEdited: function(t) { root.fOrigin = t }
                        }
                        DetailField {
                            id: regionField
                            labelKey: "beanbase.details.region"; labelFallback: "Region:"
                            accessibleText: TranslationManager.translate("beanbase.details.region", "Region")
                            value: root.fRegion
                            onEdited: function(t) { root.fRegion = t }
                        }
                        // Tea details (add-recipe-wizard-tea): shown instead
                        // of the coffee-only farm/producer/variety/process/
                        // harvest rows. The brewing values seed the recipe
                        // wizard (temp/dose), so they live here, not in notes.
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.teaType"; labelFallback: "Type:"
                            accessibleText: TranslationManager.translate("changebeans.details.teaType.accessible", "Tea type, e.g. black, green, oolong")
                            value: root.fTeaType
                            onEdited: function(t) { root.fTeaType = t }
                        }
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.garden"; labelFallback: "Garden:"
                            accessibleText: TranslationManager.translate("changebeans.details.garden.accessible", "Estate or garden")
                            value: root.fGarden
                            onEdited: function(t) { root.fGarden = t }
                        }
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.cultivar"; labelFallback: "Cultivar:"
                            accessibleText: TranslationManager.translate("changebeans.details.cultivar.accessible", "Cultivar")
                            value: root.fCultivar
                            onEdited: function(t) { root.fCultivar = t }
                        }
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.flush"; labelFallback: "Flush:"
                            accessibleText: TranslationManager.translate("changebeans.details.flush.accessible", "Harvest or flush")
                            value: root.fFlush
                            onEdited: function(t) { root.fFlush = t }
                        }
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.brewTemp"; labelFallback: "Brew temp (°C):"
                            accessibleText: TranslationManager.translate("changebeans.details.brewTemp.accessible", "Vendor brew temperature in Celsius")
                            value: root.fBrewTempC
                            onEdited: function(t) { root.fBrewTempC = t }
                        }
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.leafRatio"; labelFallback: "Leaf (g/100ml):"
                            accessibleText: TranslationManager.translate("changebeans.details.leafRatio.accessible", "Leaf grams per 100 milliliters of water")
                            value: root.fLeafRatio
                            onEdited: function(t) { root.fLeafRatio = t }
                        }
                        DetailField {
                            visible: root.isTea
                            labelKey: "changebeans.details.steepTime"; labelFallback: "Steep time:"
                            accessibleText: TranslationManager.translate("changebeans.details.steepTime.accessible", "Steep time, e.g. 3 to 5 minutes")
                            value: root.fSteepTime
                            onEdited: function(t) { root.fSteepTime = t }
                        }

                        DetailField {
                            id: farmField
                            visible: !root.isTea
                            labelKey: "beanbase.details.farm"; labelFallback: "Farm:"
                            accessibleText: TranslationManager.translate("beanbase.details.farm", "Farm")
                            value: root.fFarm
                            onEdited: function(t) { root.fFarm = t }
                        }
                        DetailField {
                            id: producerField
                            visible: !root.isTea
                            labelKey: "beanbase.details.producer"; labelFallback: "Producer:"
                            accessibleText: TranslationManager.translate("beanbase.details.producer", "Producer")
                            value: root.fProducer
                            onEdited: function(t) { root.fProducer = t }
                        }
                        DetailField {
                            id: varietyField
                            visible: !root.isTea
                            labelKey: "beanbase.details.variety"; labelFallback: "Variety:"
                            accessibleText: TranslationManager.translate("beanbase.details.variety", "Variety")
                            value: root.fVariety
                            onEdited: function(t) { root.fVariety = t }
                        }
                        DetailField {
                            id: elevationField
                            labelKey: "beanbase.details.elevation"; labelFallback: "Elevation:"
                            accessibleText: TranslationManager.translate("beanbase.details.elevation", "Elevation")
                            value: root.fElevation
                            onEdited: function(t) { root.fElevation = t }
                        }
                        DetailField {
                            id: processField
                            visible: !root.isTea
                            labelKey: "beanbase.details.process"; labelFallback: "Process:"
                            accessibleText: TranslationManager.translate("beanbase.details.process", "Process")
                            value: root.fProcess
                            onEdited: function(t) { root.fProcess = t }
                        }
                        DetailField {
                            id: harvestField
                            visible: !root.isTea
                            labelKey: "beanbase.details.harvest"; labelFallback: "Harvest:"
                            accessibleText: TranslationManager.translate("beanbase.details.harvest", "Harvest")
                            value: root.fHarvest
                            onEdited: function(t) { root.fHarvest = t }
                        }
                        DetailField {
                            id: qualityScoreField
                            labelKey: "beanbase.details.qualityScore"; labelFallback: "Quality score:"
                            accessibleText: TranslationManager.translate("beanbase.details.qualityScore", "Quality score")
                            value: root.fQualityScore
                            onEdited: function(t) { root.fQualityScore = t }
                        }
                        DetailField {
                            id: placeOfPurchaseField
                            labelKey: "beanbase.details.placeOfPurchase"; labelFallback: "Purchased at:"
                            accessibleText: TranslationManager.translate("beanbase.details.placeOfPurchase", "Purchased at")
                            value: root.fPlaceOfPurchase
                            onEdited: function(t) { root.fPlaceOfPurchase = t }
                        }
                        DetailField {
                            id: tastingNotesField
                            labelKey: "beanbase.details.tastingNotes"; labelFallback: "Tasting notes:"
                            accessibleText: TranslationManager.translate("beanbase.details.tastingNotes", "Tasting notes")
                            value: root.fTastingNotes
                            onEdited: function(t) { root.fTastingNotes = t }
                        }
                        // Revert to the pristine canonical values (shown only
                        // when linked AND something differs from the snapshot).
                        // A revert saves immediately — it is an edit like any
                        // other, and it pushes to Visualizer the same way.
                        AccessibleButton {
                            Layout.leftMargin: Theme.scaled(20)
                            visible: root.canRevert
                            text: TranslationManager.translate("changebeans.form.revert", "Revert to Bean Base data")
                            accessibleName: TranslationManager.translate("changebeans.form.revert.accessible", "Revert edited values to the original Bean Base data and save")
                            onClicked: revertConfirmDialog.open()
                        }
                    }

                    // --- Freeze tracking (under roast level, above grind) ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "changebeans.form.freeze"
                            fallback: "Frozen bag"
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        StyledSwitch {
                            id: freezeSwitch
                            checked: root.fFreeze
                            accessibleName: TranslationManager.translate("changebeans.form.freeze.accessible", "Track this bag as frozen")
                            onToggled: {
                                root.fFreeze = checked
                                if (checked && root.fFrozenDate.length !== 10)
                                    root.fFrozenDate = root.todayIso()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    BeanDateField {
                        id: frozenDateField
                        visible: root.fFreeze
                        labelKey: "changebeans.form.frozenDate"
                        labelFallback: "Frozen:"
                        value: root.fFrozenDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.frozenDate.accessible", "Frozen date.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.frozenDate.openCalendar", "Open calendar to pick frozen date")
                        onValueEdited: function(dateString) { root.fFrozenDate = dateString }
                    }

                    // Defrost date is only directly editable in edit mode
                    // ("Thaw" on the bag card is the everyday path)
                    BeanDateField {
                        id: defrostDateField
                        visible: root.fFreeze && root.formMode === "edit"
                        labelKey: "changebeans.form.defrostDate"
                        labelFallback: "Defrosted:"
                        value: root.fDefrostDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.defrostDate.accessible", "Defrost date, optional.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.defrostDate.openCalendar", "Open calendar to pick defrost date")
                        onValueEdited: function(dateString) { root.fDefrostDate = dateString }
                    }

                    // --- Out-of-freezer storage: how the beans are kept when
                    // NOT in the freezer. This is a plan, not present state, so
                    // it is orthogonal to the freeze axis and always shown — on
                    // a frozen bag it records where the beans go once thawed.
                    // The enum has no "frozen" value; frozenDate alone decides
                    // frozen-ness, so the two can never disagree. ---
                    FieldRow {
                        labelKey: "changebeans.form.storageHint"
                        labelFallback: "Out of freezer:"

                        StyledComboBox {
                            id: storageHintCombo
                            Layout.fillWidth: true
                            // Canonical enum values (index-aligned with model);
                            // index 0 = unset ("").
                            readonly property var hintValues: ["", "counter", "airtight", "vacuum-sealed", "fridge"]
                            accessibleLabel: TranslationManager.translate("changebeans.form.storageHint.accessible", "Storage type when out of the freezer")
                            model: [
                                TranslationManager.translate("changebeans.form.storageHint.unset", "Not specified"),
                                TranslationManager.translate("changebeans.form.storageHint.counter", "Counter"),
                                TranslationManager.translate("changebeans.form.storageHint.airtight", "Airtight container"),
                                TranslationManager.translate("changebeans.form.storageHint.vacuum", "Vacuum-sealed"),
                                TranslationManager.translate("changebeans.form.storageHint.fridge", "Fridge")]
                            currentIndex: Math.max(0, hintValues.indexOf(root.fStorageHint))
                            onActivated: root.fStorageHint = currentIndex > 0 ? hintValues[currentIndex] : ""
                        }
                    }

                    // Opened date is only directly editable in edit mode
                    // ("Mark Opened" on the bag card is the everyday path),
                    // mirroring the defrost field above. Not freeze-gated: a
                    // bag frozen, later thawed, then moved to a counter jar
                    // legitimately carries both a defrost and an opened date.
                    BeanDateField {
                        id: openedDateField
                        visible: root.formMode === "edit"
                        labelKey: "changebeans.form.openedDate"
                        labelFallback: "Opened:"
                        value: root.fOpenedDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.openedDate.accessible", "Opened date, optional.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.openedDate.openCalendar", "Open calendar to pick opened date")
                        onValueEdited: function(dateString) { root.fOpenedDate = dateString }
                    }

                    // --- Grinder setting + dose (the per-bag dial-in fields).
                    // Tea has nothing to grind: grind + rpm rows hidden. ---
                    FieldRow {
                        visible: !root.isTea
                        labelKey: "changebeans.form.grindSetting"
                        labelFallback: "Grind:"

                        StyledTextField {
                            id: grindSettingInput
                            Layout.fillWidth: true
                            text: root.fGrinderSetting
                            accessibleName: TranslationManager.translate("changebeans.form.grindSetting.accessible", "Grinder setting")
                            onTextEdited: root.fGrinderSetting = text
                        }
                    }

                    // RPM dial-in — only when the bag's grinder is rpm-adjustable.
                    FieldRow {
                        labelKey: "changebeans.form.rpm"
                        labelFallback: "RPM:"
                        visible: root.fEquipmentRpmCapable && !root.isTea

                        StyledTextField {
                            Layout.fillWidth: true
                            text: root.fRpm
                            inputMethodHints: Qt.ImhDigitsOnly
                            accessibleName: TranslationManager.translate("changebeans.form.rpm.accessible", "Grinder rpm")
                            onTextEdited: root.fRpm = text
                        }
                    }

                    // Equipment package (read-only NAME + info + re-point button).
                    // Grinder identity is owned by the package, not the bag; tap
                    // Switch/Add to point this bag at a different package, or the
                    // info button to see the package's contents.
                    FieldRow {
                        labelKey: "changebeans.form.equipment"
                        labelFallback: "Equipment:"

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: root.fEquipmentLabel.length > 0
                                  ? root.fEquipmentLabel
                                  : TranslationManager.translate("changebeans.form.equipmentNotSet", "Not set")
                            font: Theme.bodyFont
                            color: root.fEquipmentLabel.length > 0 ? Theme.textColor : Theme.textSecondaryColor
                            Accessible.role: Accessible.StaticText
                            Accessible.name: TranslationManager.translate("changebeans.form.equipment", "Equipment:") + " " + text
                        }
                        AccessibleButton {
                            visible: root.fEquipmentId > 0
                            icon.source: "qrc:/icons/info.svg"
                            accessibleName: TranslationManager.translate("equipment.info.button", "Equipment details")
                            onClicked: bagEquipmentInfoDialog.openFor(root.fEquipmentId)
                        }
                        AccessibleButton {
                            text: root.fEquipmentLabel.length > 0
                                  ? TranslationManager.translate("changebeans.form.switchEquipment", "Switch")
                                  : TranslationManager.translate("changebeans.form.addEquipment", "Add")
                            accessibleName: TranslationManager.translate("changebeans.form.switchEquipmentAccessible", "Switch equipment package")
                            onClicked: bagEquipmentDialog.openPicker()
                        }
                    }

                    FieldRow {
                        labelKey: "changebeans.form.dose"
                        labelFallback: "Dose:"

                        StyledTextField {
                            id: doseInput
                            Layout.fillWidth: true
                            text: root.fDose
                            placeholder: TranslationManager.translate("changebeans.form.grams", "g")
                            accessibleName: TranslationManager.translate("changebeans.form.dose.accessible", "Dose weight in grams")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: {
                                root.fDose = text
                                // A dose edit re-derives the non-anchored
                                // yield field; the anchor never moves.
                                root.syncDerivedBagYield()
                            }
                        }

                        Tr {
                            key: "changebeans.form.yield"
                            fallback: "Yield:"
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        // The bag's own yield anchor (add-yield-ratio-anchor):
                        // a fixed weight OR a ratio of the dose — last edited
                        // wins, the other shows derived and dimmed. Blank =
                        // no yield of its own (the profile answers).
                        StyledTextField {
                            id: yieldInput
                            Layout.fillWidth: true
                            text: root.fYield
                            opacity: root.fYieldAnchor === "ratio" ? 0.55 : 1.0
                            placeholder: TranslationManager.translate("changebeans.form.yieldOverride.placeholder", "Profile default")
                            accessibleName: TranslationManager.translate("changebeans.form.yieldOverride.accessible", "Yield in grams, blank to follow the profile default")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: {
                                root.fYield = text
                                root.fYieldAnchor = (parseFloat(text) || 0) > 0 ? "absolute" : "none"
                                if (root.fYieldAnchor === "none")
                                    root.fYieldRatio = ""
                                root.syncDerivedBagYield()
                            }
                        }

                        Tr {
                            key: "changebeans.form.ratio"
                            fallback: "Ratio 1:"
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        StyledTextField {
                            id: yieldRatioInput
                            Layout.fillWidth: true
                            text: root.fYieldRatio
                            opacity: root.fYieldAnchor === "ratio" ? 1.0 : 0.55
                            placeholder: TranslationManager.translate("changebeans.form.ratioMultiplier", "x")
                            accessibleName: TranslationManager.translate("changebeans.form.yieldRatio.accessible", "Yield as a ratio of the dose, blank to follow the profile default")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: {
                                root.fYieldRatio = text
                                root.fYieldAnchor = (parseFloat(text) || 0) > 0 ? "ratio" : "none"
                                if (root.fYieldAnchor === "none")
                                    root.fYield = ""
                                root.syncDerivedBagYield()
                            }
                        }
                    }

                    // Notes — always visible. Grinder IDENTITY is no longer edited
                    // here: it's owned by the Equipment package (add-equipment-
                    // packages), set via Switch Equipment in Brew Settings. The bag
                    // keeps only its grind-setting dial-in (above).
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        FieldRow {
                            labelKey: "changebeans.form.notes"
                            labelFallback: "Notes:"

                            StyledTextField {
                                id: notesInput
                                Layout.fillWidth: true
                                text: root.fNotes
                                accessibleName: TranslationManager.translate("changebeans.form.notes.accessible", "Bag notes")
                                onTextEdited: root.fNotes = text
                            }
                        }
                    }

                    // Error message (create failure / validation)
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        visible: root.errorMessage.length > 0
                        text: root.errorMessage
                        font: Theme.bodyFont
                        color: Theme.warningColor
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.AlertMessage
                        Accessible.name: text
                        Accessible.focusable: true
                    }

                    // (Cancel / Save live in the dialog header — reachable
                    // without scrolling past the form fields.)
                    Item { Layout.preferredHeight: Theme.scaled(8) }
                }
            }
        }
    }

    // Labelled form row: shared-width label + caller-supplied controls
    component FieldRow: RowLayout {
        property string labelKey: ""
        property string labelFallback: ""

        Layout.fillWidth: true
        Layout.leftMargin: Theme.scaled(20)
        Layout.rightMargin: Theme.scaled(20)
        spacing: Theme.scaled(6)

        Tr {
            key: labelKey
            fallback: labelFallback
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            Layout.alignment: Qt.AlignVCenter
            // "Equipment:" is the widest label used through this row (see
            // BrewDialog.qml's equivalent row), so let it size to its content
            // (min = the shared 85px column) rather than a fixed 85px that
            // clips it and lets the value butt right against it — this is
            // what happens on Windows, where Segoe UI renders it wider than
            // Roboto/San Francisco do at the same pixel size.
            Layout.minimumWidth: Theme.scaled(85)
            Accessible.ignored: true
        }
    }

    // Bean date entry row (roast / frozen / defrost): the labelled FieldRow plus a
    // locale-aware date text field and a calendar-picker button. Single-sources the
    // accessibility contract for bean dates:
    //   - No inputMask. A masked field pre-fills a "____-__-__" skeleton that Qt
    //     exposes via displayText() to the accessibility tree, so TalkBack/VoiceOver
    //     announce a skeleton of blanks. This field starts genuinely empty instead.
    //   - Entry order + separator follow the host locale (US month-first, most of
    //     the world day-first, ISO year-first); the value is always STORED as ISO
    //     yyyy-mm-dd — only the displayed order is localized.
    //   - Digits-only keypad, separators inserted progressively as the user types.
    // The caller binds `value` (ISO) and writes it back in onValueEdited (a signal,
    // so the field never touches caller state).
    component BeanDateField: FieldRow {
        id: dateField

        property string value: ""                  // stored ISO yyyy-mm-dd (or "")
        property string fieldAccessibleName: ""
        property string calendarAccessibleName: ""
        // Exposes the text input so the dialog's KeyboardAwareContainer can track
        // it and onOpened can focus it — same convention as roasterInput.textField.
        property alias textField: dateInput
        signal valueEdited(string dateString)      // emits ISO yyyy-mm-dd (or "")

        // Locale-derived entry order/separator (pure helpers in DateUtils).
        readonly property string _dateFormat: Qt.locale().dateFormat(Locale.ShortFormat)
        readonly property var _order: DateUtils.dateOrderFromFormat(dateField._dateFormat)
        readonly property string _sep: DateUtils.dateSeparatorFromFormat(dateField._dateFormat)
        readonly property string _placeholder: dateField._order.map(function(k) {
            return k === "y" ? "yyyy" : (k === "M" ? "mm" : "dd")
        }).join(dateField._sep)
        // Spoken order hint, fully translated: each segment word and the joiner are
        // looked up here (QML can reach TranslationManager; DateUtils cannot), so a
        // non-English screen reader hears the order in its own language.
        readonly property string _orderHint:
            TranslationManager.translate("dateentry.orderHint", "Enter %1.").replace("%1",
                DateUtils.orderWords(dateField._order,
                    ({ y: TranslationManager.translate("dateentry.segment.year", "year"),
                       M: TranslationManager.translate("dateentry.segment.month", "month"),
                       d: TranslationManager.translate("dateentry.segment.day", "day") }),
                    TranslationManager.translate("dateentry.orderConnector", ", then ")))

        StyledTextField {
            id: dateInput
            Layout.fillWidth: true
            placeholder: dateField._placeholder
            // Persistent label + spoken order + calendar hint, assembled via a
            // translatable template so translators control ordering and spacing.
            accessibleName: TranslationManager.translate("dateentry.fieldAccessible", "%1 %2 %3")
                .replace("%1", dateField.fieldAccessibleName)
                .replace("%2", dateField._orderHint)
                .replace("%3", TranslationManager.translate("dateentry.calendarHint", "Or use the calendar button next to this field."))
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhPreferNumbers

            // value (ISO) -> displayed localized text. Set imperatively, not via a
            // `text:` binding, because user editing breaks a text binding; this
            // Connections re-syncs the display whenever the stored value changes
            // (calendar selection, form load/reset) and the user isn't mid-edit.
            Component.onCompleted:
                text = DateUtils.isoToLocalized(dateField.value, dateField._order, dateField._sep)
            Connections {
                target: dateField
                function onValueChanged() {
                    if (!dateInput.activeFocus)
                        dateInput.text = DateUtils.isoToLocalized(dateField.value, dateField._order, dateField._sep)
                }
            }

            // Progressive formatting: separators appear as segments fill. Reassigning
            // `text` would send the caret to the end, so preserve it by the count of
            // digits before the caret (stable across separator insertion) and restore
            // it after reformatting — lets the user edit an earlier segment in place.
            onTextEdited: {
                var digitsBeforeCaret = text.substring(0, cursorPosition).replace(/\D/g, "").length
                var formatted = DateUtils.formatAsTyped(text, dateField._order, dateField._sep)
                if (formatted !== text)
                    text = formatted
                cursorPosition = DateUtils.caretForDigits(text, digitsBeforeCaret)
            }

            // Commit: parse localized -> ISO, store it, and reconcile the display so
            // shown and stored can't diverge. Empty stores ""; a complete valid date
            // stores its ISO; an incomplete/invalid entry is reverted to the stored
            // value (never left diverging from what a subsequent Save would persist).
            onEditingFinished: {
                if (text.replace(/\D/g, "").length === 0) {
                    dateField.valueEdited("")
                    text = ""
                    return
                }
                var iso = DateUtils.localizedToIso(text, dateField._order)
                if (iso.length > 0) {
                    dateField.valueEdited(iso)
                    text = DateUtils.isoToLocalized(iso, dateField._order, dateField._sep)
                } else {
                    // Incomplete/invalid: revert to the stored value so the shown text
                    // matches what Save would persist (no silent divergence).
                    text = DateUtils.isoToLocalized(dateField.value, dateField._order, dateField._sep)
                }
            }
        }

        AccessibleButton {
            Layout.preferredWidth: Theme.scaled(44)
            Layout.preferredHeight: Theme.scaled(44)
            accessibleName: dateField.calendarAccessibleName
            leftPadding: Theme.scaled(8)
            rightPadding: Theme.scaled(8)
            icon.source: "qrc:/emoji/1f4c5.svg"
            icon.width: Theme.scaled(20)
            icon.height: Theme.scaled(20)
            text: ""
            onClicked: datePicker.openWithDate(dateField.value)
        }

        DatePickerDialog {
            id: datePicker
            onDateSelected: function(dateString) { dateField.valueEdited(dateString) }
        }
    }
}
