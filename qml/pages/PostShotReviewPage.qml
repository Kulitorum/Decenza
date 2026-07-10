import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../components"
import "../components/DateUtils.js" as DateUtils
import "../components/layout/ShotPlanConfig.js" as ShotPlanConfig

Page {
    id: postShotReviewPage
    objectName: "postShotReviewPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("postshotreview.title", "Shot Review")
        if (editShotId > 0) {
            loadShotForEditing()
        }
    }
    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("postshotreview.title", "Shot Review")
        // Reconnect refractometer when entering/returning to this page
        if (Settings.savedRefractometerAddress !== "" && !BLEManager.refractometerConnected) {
            BLEManager.tryDirectConnectToRefractometer()
        }
    }

    // Disconnect refractometer when the page is torn down. NOTE: we do NOT
    // autosave() here — calling Qt.inputMethod.commit() / a DB write / singleton
    // writes during QML object destruction is unsafe (events delivered to a
    // being-destroyed TextEdit, nulled context). StackView.onDeactivating below
    // already flushes on every normal navigation exit, and handleBack() flushes
    // on the explicit back path, so the destruction flush was redundant.
    Component.onDestruction: {
        if (Refractometer && Refractometer.connected) {
            Refractometer.disconnectFromDevice()
        }
        // If a pending edit has not yet been synced to visualizer, fire the PATCH
        // now. maybeAutoUpdateVisualizer() requires four conditions: pendingVisualizerUpdate
        // set, Settings.visualizer.visualizerAutoUpdate on, MainController.visualizer
        // present, and a captured _visualizerId (i.e. the shot was previously uploaded).
        // Safe here because maybeAutoUpdateVisualizer() only dispatches a network call —
        // no DB writes or Qt.inputMethod.commit(), which are the operations flagged as
        // unsafe during destruction. Note: any failure response arrives after this page
        // is fully destroyed, so errors are logged by the C++ uploader but cannot be
        // surfaced to the user from here.
        maybeAutoUpdateVisualizer()
    }

    // Flush whenever the page loses the foreground (back, a child page pushed
    // on top, app backgrounded) so a deferred/in-progress edit is persisted.
    StackView.onDeactivating: autosave()

    function handleBack() {
        // Every committed edit is already persisted; just flush a possible
        // in-progress text field and leave — no confirmation prompt needed.
        autosave()
        root.goBack()
    }

    // Intercept Android system back button / Escape key; reset auto-close on any key
    focus: true
    Keys.onPressed: function(event) { resetAutoCloseTimer() }
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true
            handleBack()
        }
    }

    property int editShotId: 0  // Shot ID to edit (always use edit mode now)
    property var editShotData: ({})  // Loaded shot data when editing
    property bool isEditMode: editShotId > 0

    // Field selection + order for the snapshot line, taken from the user's first
    // idle-page Shot Plan widget so this line shows the fields they configured.
    // Reactive on layout edits. Only the item list is used — the snapshot is
    // always a plain fragment line (sentence/stacked toggles are ignored).
    readonly property var _shotPlanItemOrder: {
        var layout
        try { layout = JSON.parse(Settings.network.layoutConfiguration) } catch (e) { layout = null }
        return ShotPlanConfig.itemOrderFromLayout(layout)
    }

    // Recipe identity for the recipe card, live-resolved by editShotData.recipeId
    // (a shot-linked recipe can only be archived, never deleted, so the row
    // always resolves and follows renames). Grind/rpm on that card comes from
    // this page's live edit state, never this map's pin. _resolvedRecipeId
    // guards against re-requesting on the frequent editShotData reassigns.
    property var resolvedRecipe: ({})
    property int _resolvedRecipeId: -1
    onEditShotDataChanged: {
        var rid = editShotData.recipeId || -1
        if (rid > 0 && rid !== _resolvedRecipeId) {
            _resolvedRecipeId = rid
            resolvedRecipe = ({})
            MainController.recipeStorage.requestRecipe(rid)
        } else if (rid <= 0) {
            _resolvedRecipeId = -1
            resolvedRecipe = ({})
        }
    }
    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, recipe) {
            if (recipeId === postShotReviewPage._resolvedRecipeId)
                postShotReviewPage.resolvedRecipe = recipe
        }
    }

    // --- Read-only recipe-component row text (from the page's live edit state) ---
    function recipeProfileText() {
        var parts = []
        if (editShotData.profileName) parts.push(editShotData.profileName)
        var t = editShotData.temperatureOverrideC || 0
        if (t > 0) parts.push(Math.round(Theme.cToDisplay(t)) + Theme.tempUnitSuffix())
        return parts.join(" · ")
    }
    function recipeDoseYieldText() {
        var dose = editDoseWeight || 0
        var yieldG = editDrinkWeight || 0
        if (dose > 0 && yieldG > 0) return dose.toFixed(1) + "g → " + yieldG.toFixed(1) + "g"
        if (dose > 0) return dose.toFixed(1) + "g"
        return ""
    }
    // Read-only dial-in for the recipe card: dose → yield · grind · rpm (rpm
    // only for rpm-capable grinders). Grind itself is edited in the field grid.
    function recipeDialInText() {
        var _ = TranslationManager.translationVersion
        var parts = []
        var dy = recipeDoseYieldText()
        if (dy !== "") parts.push(dy)
        if (editGrinderSetting.length > 0)
            parts.push(TranslationManager.translate("equipment.card.lastGrind", "Grind %1").arg(editGrinderSetting))
        if (editRpm > 0 && editRpmCapable)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(editRpm))
        return parts.join(" · ")
    }
    function recipeSteamText() {
        if (!editShotData.steamJson) return ""
        try {
            var s = JSON.parse(editShotData.steamJson)
            if (!s.hasMilk) return ""
            var parts = []
            if (s.pitcherName) parts.push(s.pitcherName)
            if ((s.milkWeightG || 0) > 0)
                parts.push(TranslationManager.translate("recipes.list.milkWeight", "%1g milk").arg(s.milkWeightG))
            return parts.join(" · ")
        } catch (e) { return "" }
    }
    function recipeWaterText() {
        if (!editShotData.hotWaterJson) return ""
        try {
            var w = JSON.parse(editShotData.hotWaterJson)
            if (!w.hasWater) return ""
            var parts = []
            if (w.vesselName) parts.push(w.vesselName)
            if ((w.volume || 0) > 0) parts.push(w.volume + (w.mode === "volume" ? "ml" : "g"))
            if ((w.temperatureC || 0) > 0) parts.push(Math.round(Theme.cToDisplay(w.temperatureC)) + Theme.tempUnitSuffix())
            return parts.join(" · ")
        } catch (e) { return "" }
    }

    // One labeled read-only component row inside the recipe card (caption over
    // value); hides when empty.
    component RecipeField: ColumnLayout {
        id: rf
        property string fieldLabel: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 0
        visible: rf.value !== ""
        Text {
            text: rf.fieldLabel
            font: Theme.captionFont
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            text: rf.value
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    Tr { id: trRowProfile; key: "recipes.wizard.rowProfile"; fallback: "Profile"; visible: false }
    Tr { id: trRowBeans; key: "shotdetail.beaninfo"; fallback: "Beans"; visible: false }
    Tr { id: trRowDialIn; key: "shotdetail.recipe.dialIn"; fallback: "Dial-in"; visible: false }
    Tr { id: trRowSteam; key: "recipes.wizard.rowSteam"; fallback: "Steam / milk"; visible: false }
    Tr { id: trRowWater; key: "recipes.wizard.rowHotWater"; fallback: "Hot water"; visible: false }
    Tr { id: trRowEquipment; key: "shotdetail.equipment"; fallback: "Equipment"; visible: false }

    property bool autoClose: true  // false when user opens manually (no auto-dismiss)
    property bool advancedMode: Settings.boolValue("shotReview/advancedMode", false)
    property string uploadError: ""
    // Reason a policy-based skip rejected the upload (maintenance profile,
    // too-short shot). Surfaced as informational text — not red error styling —
    // because the system intentionally chose not to upload.
    property string uploadSkipReason: ""
    property bool pendingVisualizerUpdate: false  // set when a metadata edit has been saved locally but not yet PATCHed to visualizer
    // profileName from DB — captured once in onShotReady before any Object.assign strips Q_GADGET
    // fields; held for the entire page lifetime so buildVisualizerOverrides() and manual upload
    // can always include it without risk of it becoming empty after a save cycle.
    property string _profileName: ""
    // visualizerId from DB — same Q_GADGET-strip hazard as _profileName. Captured in onShotReady
    // and refreshed in onUploadSucceededForShot (a fresh upload completed for THIS shot — from
    // this page or from the shot-completion background uploader) so the "Re-Upload" button label,
    // the auto-update PATCH gate, and the manual upload button all see a stable value after
    // saveEditedShot replaces editShotData with a plain JS object.
    property string _visualizerId: ""
    // Track requests THIS page initiated so the shared VisualizerUploader signals
    // (updateSuccess, uploadFailed) can be filtered. Without these guards an unrelated request
    // (e.g. an MCP-triggered PATCH on the same visualizer record from a different session) would
    // clear our uploadError and reset the in-flight flags for a foreign request, leaving the page
    // in a spuriously clean state. updateSuccess carries a visualizerId string but no caller
    // identity (the same cloud shot can be PATCHed concurrently from any session), and
    // uploadFailed carries no identifier at all — the flags are the only reliable discriminator.
    property bool _firstUploadInFlight: false
    property bool _patchInFlight: false

    // Pick up toggle changes made on any other page sharing this setting
    // (Shot Detail, Shot Comparison, Espresso view selector).
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (key === "shotReview/advancedMode")
                postShotReviewPage.advancedMode = Settings.boolValue("shotReview/advancedMode", false)
        }
    }

    // Auto-close timer: return to idle after configured timeout
    // 0 = instant (handled in main.qml, never reaches this page)
    // 1-30 = minutes, 31 = never
    property int autoCloseTimeout: Settings.value("postShotReviewTimeout", 31)

    Timer {
        id: autoCloseTimer
        interval: postShotReviewPage.autoCloseTimeout * 60000
        running: postShotReviewPage.autoClose
                 && postShotReviewPage.autoCloseTimeout > 0
                 && postShotReviewPage.autoCloseTimeout < 31
                 && postShotReviewPage.StackView.status === StackView.Active
        onTriggered: {
            // Auto-close: flush any pending edit and exit
            postShotReviewPage.autosave()
            root.goBack()
        }
    }

    // Reset timer on user interaction
    function resetAutoCloseTimer() {
        if (autoCloseTimer.running) {
            autoCloseTimer.restart()
        }
    }

    // Detect taps anywhere on the page
    TapHandler {
        onTapped: resetAutoCloseTimer()
    }
    // Incremented when async distinct cache refreshes; referenced in suggestion bindings
    // to force QML re-evaluation (the >= 0 condition is always true by design)
    property int _distinctCacheVersion: 0
    // Persisted graph height (like ShotComparisonPage)
    property real graphHeight: Settings.value("postShotReview/graphHeight", Theme.scaled(200))

    // Load shot data for editing (async)
    function loadShotForEditing() {
        if (editShotId <= 0) return
        MainController.shotHistory.requestShot(editShotId)
    }

    // Handle async shot data
    Connections {
        target: MainController.shotHistory
        function onShotReady(shotId, shot) {
            if (shotId !== postShotReviewPage.editShotId) return
            editShotData = shot
            _profileName = editShotData.profileName || ""
            _visualizerId = editShotData.visualizerId || ""
            // Reset upload status text when loading a new shot so stale
            // error/skip messages from a previous shot don't carry over.
            uploadError = ""
            uploadSkipReason = ""
            if (editShotData.id) {
                // Populate editing fields
                editBeanBrand = editShotData.beanBrand || ""
                editBeanType = editShotData.beanType || ""
                editRoastDate = DateUtils.normalizeDateString(editShotData.roastDate || "")
                editRoastLevel = editShotData.roastLevel || ""
                editGrinderBrand = editShotData.grinderBrand || ""
                editGrinderModel = editShotData.grinderModel || ""
                editGrinderBurrs = editShotData.grinderBurrs || ""
                editEquipmentId = editShotData.equipmentId || -1
                editEquipmentName = editShotData.equipmentName || ""
                // Basket + puck prep are display-only here (owned by the package,
                // re-pointed via the picker) but shown in the equipment card.
                editBasketBrand = editShotData.basketBrand || ""
                editBasketModel = editShotData.basketModel || ""
                editPuckPrep = editShotData.puckPrep || ""
                editGrinderSetting = editShotData.grinderSetting || ""
                editRpm = editShotData.rpm || 0
                editBarista = editShotData.barista || ""
                // Fall back to last-used DYE dose when the shot has no stored dose,
                // so EY can be computed immediately when TDS arrives.
                editDoseWeight = (editShotData.doseWeightG > 0) ? editShotData.doseWeightG : Settings.dye.dyeBeanWeight
                editDrinkWeight = editShotData.finalWeightG ?? 0
                // Preserve any live R2 reading that arrived before the async DB load;
                // only take the DB value when no measurement has been received yet.
                if (editDrinkTds === 0) {
                    editDrinkTds = editShotData.drinkTdsPct ?? 0
                    editDrinkEy = editShotData.drinkEyPct ?? 0
                }
                editEnjoyment = editShotData.enjoyment0to100 ?? 0
                editNotes = editShotData.espressoNotes || ""
                editBeverageType = editShotData.beverageType || "espresso"
                editBeanBaseJson = editShotData.beanBaseJson || ""
                // A canonical pick persists identity immediately; if the page
                // closed (or the network blipped) before the attribute payload
                // arrived, the shot is stuck with a bare {id, roaster, name}
                // blob — fields don't lock, the advisor sees nothing. Complete
                // it: re-issue the best-effort fetch; the onCanonicalDetails
                // merge below finishes the job.
                if (beanBaseLinked && activeBeanBase.source === "visualizer"
                    && activeBeanBase.origin === undefined
                    && activeBeanBase.degree === undefined)
                    MainController.beanbase.fetchCanonicalDetails(activeBeanBase)
                // Recompute EY now that dose/weight are loaded (covers the case where TDS
                // arrived via R2 before the shot data was ready, or where the DB already
                // has a non-zero TDS from a previous session).
                calculateEy()
                // Establish the autosave baseline now that every edit field
                // mirrors the loaded record. _editLoaded gates autosave so a
                // pre-load flush can't write empty metadata over the shot
                // (hasUnsavedChanges is transiently true before this point —
                // empty baseline vs. dose defaulted from Settings).
                _committedState = captureEditState()
                _editLoaded = true
                // Quality badges already arrived recomputed in `shot` via
                // loadShotRecordStatic, which also persists drift to the DB
                // and emits shotBadgesUpdated when it does. onShotBadgesUpdated
                // below catches the persist event.
            }
        }
        function onShotBadgesUpdated(shotId, channeling, grindIssue, skipFirstFrame, pourTruncated) {
            if (shotId !== postShotReviewPage.editShotId) return
            var updated = clonePersistedShot(editShotData)
            updated.channelingDetected = channeling
            updated.grindIssueDetected = grindIssue
            updated.skipFirstFrameDetected = skipFirstFrame
            updated.pourTruncatedDetected = pourTruncated
            editShotData = updated
        }
        function onShotMetadataUpdated(shotId, success) {
            if (shotId !== postShotReviewPage.editShotId) return
            // Success needs no reload: saveEditedShot() already advanced the
            // in-memory baseline optimistically. Reloading here would race an
            // autosave from another field and clobber an in-progress edit.
            if (success) {
                postShotReviewPage._saveFailed = false
            } else {
                console.warn("PostShotReviewPage: Failed to save metadata for shot", shotId)
                postShotReviewPage._saveFailed = true
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(TranslationManager.translate(
                        "postshotreview.saveFailed", "Saving shot changes failed — will retry"))
            }
        }
        function onVisualizerInfoUpdated(shotId, success) {
            if (shotId !== postShotReviewPage.editShotId) return
            // No reload: a full loadShotForEditing() here would re-run
            // onShotReady, clobber an in-progress edit, and orphan the undo
            // stack (same race the metadata path avoids). The visualizer id is
            // refreshed in place by onUploadSucceededForShot / onUpdateSuccess below.
            if (!success)
                console.warn("PostShotReviewPage: Failed to save visualizer info for shot", shotId)
        }
        function onDistinctCacheReady() {
            _distinctCacheVersion++
        }
    }

    // Editing fields (separate from Settings.dye* to avoid polluting current session)
    property string editBeanBrand: ""
    property string editBeanType: ""
    property string editRoastDate: ""
    property string editRoastLevel: ""
    // Grinder brand/model/burrs are READ-ONLY display, resolved from the shot's
    // equipment package; editEquipmentId is the re-point target the picker sets.
    property string editGrinderBrand: ""
    property string editGrinderModel: ""
    property string editGrinderBurrs: ""
    property string editEquipmentName: ""  // package display name (read-only label)
    property int editEquipmentId: -1
    property int _pendingEquipmentId: -1   // package id awaiting requestPackage resolution
    // Basket + puck prep: read-only display, resolved from the package like the
    // grinder identity; shown in the equipment card, never edited as free text.
    property string editBasketBrand: ""
    property string editBasketModel: ""
    property string editPuckPrep: ""       // canonical puck-prep flag string
    property string editGrinderSetting: ""
    property int editRpm: 0                // grinder rpm dial-in (shown when rpmCapable)
    readonly property bool editRpmCapable: Settings.dye.grinderRpmCapable(editGrinderBrand, editGrinderModel)
    property string editBarista: ""
    property double editDoseWeight: 0
    property double editDrinkWeight: 0
    property double editDrinkTds: 0
    property double editDrinkEy: 0
    property int editEnjoyment: 0  // 0 = unrated

    property string editNotes: ""
    property string editBeverageType: "espresso"
    // Bean Base snapshot stored with this shot. Searchable/correctable right
    // here (same flow as BeanInfoPage): picking a result rewrites THIS
    // shot's snapshot and the bean fields, autosaved like any other edit —
    // and undoable, since the blob rides the undo state.
    property string editBeanBaseJson: ""

    readonly property var activeBeanBase: {
        if (!editBeanBaseJson || editBeanBaseJson.length === 0) return ({})
        try { return JSON.parse(editBeanBaseJson) } catch (e) { return ({}) }
    }
    readonly property bool beanBaseLinked: activeBeanBase.id !== undefined && activeBeanBase.id !== ""

    // Real espresso TDS is 5–22%; below 3.0% is a calibration or empty cuvette.
    readonly property real kMinimumPlausibleTds: 3.0

    // Above the R2's physical measurement range it's a device error sentinel,
    // not a reading (the R2 emitted raw 0xFFE5 → 655.09% during a failed
    // measurement and it was autosaved onto a shot). Symmetric with
    // kMinimumPlausibleTds so the physical R2 Start button and the "Read TDS"
    // button — both of which arrive via onTdsChanged — are gated identically.
    readonly property real kMaximumPlausibleTds: 35.0

    // Gate by visibility so device-initiated R2 readings between shots don't
    // land on whichever shot happens to be loaded.
    //
    // The `BLEManager.refractometerConnected` reference is load-bearing — it
    // gives the target binding a signal to re-evaluate on. `Refractometer` is
    // a context property whose value gets swapped (null ↔ live pointer) over
    // the app lifetime, but `setContextProperty` doesn't emit a notify signal,
    // so a binding that captured `null` at page-load stays stuck. Adding the
    // BLEManager Q_PROPERTY (which DOES emit refractometerConnectedChanged)
    // forces the binding to re-evaluate when the R2 connects after the review
    // page has already opened. Without this, R2 readings that arrive after
    // the page opens are silently dropped.
    Connections {
        target: BLEManager.refractometerConnected
            && (typeof Refractometer !== "undefined") && Refractometer
            ? Refractometer : null
        enabled: postShotReviewPage.visible
        function onTdsChanged(tds) {
            if (!isEditMode) return
            if (tds < postShotReviewPage.kMinimumPlausibleTds) {
                console.debug("[PostShotReview] R2 tds", tds.toFixed(2),
                    "dropped: below threshold", postShotReviewPage.kMinimumPlausibleTds,
                    "shotId=", editShotId,
                    "wasMeasuring=", (typeof Refractometer !== "undefined" && Refractometer) ? Refractometer.measuring : false)
                return
            }
            if (tds > postShotReviewPage.kMaximumPlausibleTds) {
                console.debug("[PostShotReview] R2 tds", tds.toFixed(2),
                    "dropped: above threshold", postShotReviewPage.kMaximumPlausibleTds,
                    "shotId=", editShotId,
                    "wasMeasuring=", (typeof Refractometer !== "undefined" && Refractometer) ? Refractometer.measuring : false)
                return
            }
            editDrinkTds = tds
            calculateEy()
            // An R2 measurement is a committed value just like a user-entered
            // one — persist it the moment it lands (finalize: a discrete async
            // commit, not a coalesced gesture). Without this it relied on a
            // later manual Save and was frequently lost on navigate-away.
            postShotReviewPage.autosave("r2", true)
        }
    }

    // Auto-calculate EY from TDS, dose weight, and beverage weight
    // Formula: EY(%) = (beverageWeight × TDS%) / doseWeight
    function calculateEy() {
        if (editDoseWeight > 0 && editDrinkWeight > 0 && editDrinkTds > 0) {
            var ey = (editDrinkWeight * editDrinkTds) / editDoseWeight
            ey = Math.round(ey * 10) / 10  // Round to 1 decimal
            editDrinkEy = ey
        }
    }

    // Track if any edits were made
    property bool hasUnsavedChanges: isEditMode && (
        editBeanBrand !== (editShotData.beanBrand || "") ||
        editBeanType !== (editShotData.beanType || "") ||
        editRoastDate !== DateUtils.normalizeDateString(editShotData.roastDate || "") ||
        editRoastLevel !== (editShotData.roastLevel || "") ||
        editGrinderBrand !== (editShotData.grinderBrand || "") ||
        editGrinderModel !== (editShotData.grinderModel || "") ||
        editGrinderBurrs !== (editShotData.grinderBurrs || "") ||
        editGrinderSetting !== (editShotData.grinderSetting || "") ||
        editRpm !== (editShotData.rpm || 0) ||
        editEquipmentId !== (editShotData.equipmentId || -1) ||
        editBarista !== (editShotData.barista || "") ||
        editDoseWeight !== ((editShotData.doseWeightG > 0) ? editShotData.doseWeightG : Settings.dye.dyeBeanWeight) ||
        editDrinkWeight !== (editShotData.finalWeightG ?? 0) ||
        editDrinkTds !== (editShotData.drinkTdsPct ?? 0) ||
        editDrinkEy !== (editShotData.drinkEyPct ?? 0) ||
        editEnjoyment !== (editShotData.enjoyment0to100 ?? 0) ||
        editNotes !== (editShotData.espressoNotes || "") ||
        editBeverageType !== (editShotData.beverageType || "espresso") ||
        editBeanBaseJson !== (editShotData.beanBaseJson || "") ||
        _saveFailed
    )

    // A failed metadata write must not be silently dropped: saveEditedShot()
    // advances the baseline optimistically, so on failure this flag forces
    // hasUnsavedChanges back on — the next commit point or lifecycle flush
    // retries the write. Cleared on the next successful save.
    property bool _saveFailed: false

    // ---- Autosave + undo ---------------------------------------------------
    // There is no manual Save button: every committed edit is persisted right
    // away and the prior committed state is pushed onto an undo stack, so the
    // last change (repeatable) can be reverted. The baseline (editShotData) is
    // advanced optimistically inside saveEditedShot() so hasUnsavedChanges
    // clears without a DB round-trip — reloading on save would clobber an edit
    // the user has already started in another field.
    readonly property int kMaxUndoDepth: 50
    property var _undoStack: []
    // INVARIANT: _undoDepth must be reassigned to _undoStack.length after every
    // push/pop/splice — in-place array mutation does not emit a QML change
    // signal, so this integer mirror is the only thing undoButton.visible can
    // bind to. Never mutate _undoStack without updating _undoDepth.
    property int _undoDepth: 0
    property var _committedState: ({})  // last persisted edit-field values
    property bool _editLoaded: false    // true once onShotReady has populated fields
    // Undo coalescing: a continuous interaction with one control (slider drag,
    // a burst of +/- stepper clicks, typing into one field) is ONE undoable
    // change. A new undo frame opens only when the edit key differs from the
    // previous one; coalescing ends (via finalizeEdit) when the control loses
    // focus or a discrete/terminal commit fires, so dragging the rating slider
    // 75→85 is a single Undo back to 75, while editing dose, leaving it, and
    // editing it again are two separate Undo frames.
    property string _lastEditKey: ""

    function captureEditState() {
        return {
            beanBrand: editBeanBrand, beanType: editBeanType,
            roastDate: editRoastDate, roastLevel: editRoastLevel,
            grinderBrand: editGrinderBrand, grinderModel: editGrinderModel,
            grinderBurrs: editGrinderBurrs, grinderSetting: editGrinderSetting,
            equipmentId: editEquipmentId, equipmentName: editEquipmentName, rpm: editRpm,
            basketBrand: editBasketBrand, basketModel: editBasketModel, puckPrep: editPuckPrep,
            barista: editBarista, doseWeight: editDoseWeight,
            drinkWeight: editDrinkWeight, drinkTds: editDrinkTds,
            drinkEy: editDrinkEy, enjoyment: editEnjoyment,
            notes: editNotes, beverageType: editBeverageType,
            beanBaseJson: editBeanBaseJson
        }
    }

    function applyEditState(s) {
        editBeanBrand = s.beanBrand; editBeanType = s.beanType
        editRoastDate = s.roastDate; editRoastLevel = s.roastLevel
        editGrinderBrand = s.grinderBrand; editGrinderModel = s.grinderModel
        editGrinderBurrs = s.grinderBurrs; editGrinderSetting = s.grinderSetting
        editEquipmentId = s.equipmentId !== undefined ? s.equipmentId : -1
        editEquipmentName = s.equipmentName !== undefined ? s.equipmentName : ""
        editBasketBrand = s.basketBrand !== undefined ? s.basketBrand : ""
        editBasketModel = s.basketModel !== undefined ? s.basketModel : ""
        editPuckPrep = s.puckPrep !== undefined ? s.puckPrep : ""
        editRpm = s.rpm !== undefined ? s.rpm : 0
        editBarista = s.barista; editDoseWeight = s.doseWeight
        editDrinkWeight = s.drinkWeight; editDrinkTds = s.drinkTds
        editDrinkEy = s.drinkEy; editEnjoyment = s.enjoyment
        editNotes = s.notes; editBeverageType = s.beverageType
        editBeanBaseJson = s.beanBaseJson !== undefined ? s.beanBaseJson : ""
        // RatingInput (internal `root.value = …`) and the dose/out ValueInputs
        // (handlers do `xInput.value = …`) imperatively assign their own
        // `value` during interaction, which severs the `value: editX` binding.
        // Re-establish the binding (not a bare assignment, which would sever it
        // permanently) so Undo restores the UI and future edits keep tracking
        // editX. The TDS/EY onValueModified handlers in THIS file (unlike
        // dose/out) do not self-assign tdsInput.value/eyInput.value, so their
        // `value: editDrinkTds`/`editDrinkEy` bindings stay live and must NOT
        // be touched here — re-asserting them would sever the binding and
        // break later R2 / calculateEy() updates. (If a future edit adds a
        // self-assign to those handlers, re-bind them here too.)
        ratingInput.value = Qt.binding(function() { return editEnjoyment })
        doseInput.value = Qt.binding(function() { return editDoseWeight })
        outInput.value = Qt.binding(function() { return editDrinkWeight })
    }

    // Persist current edits if dirty.
    //
    // `key`      — identifies the control being edited. A new undo frame opens
    //              when it differs from `_lastEditKey` (coalescing). Absent/""
    //              means a lifecycle flush (handleBack / deactivate / upload).
    // `finalize` — true for terminal/discrete/async commits (blur, suggestion
    //              pick, combo/date change, R2 reading) and focus-loss; ends
    //              coalescing so the next edit (even same control) is a new
    //              frame.
    //
    // DB-write coalescing: a same-control continuous tick (slider drag, held
    // stepper) neither opens a frame nor writes to the DB — it defers. The
    // value is persisted when the gesture boundary is reached (frame open on
    // first tick, finalize on focus-loss, or a lifecycle flush). This keeps
    // one drag to ~2 DB writes instead of one per emission.
    function autosave(key, finalize) {
        Qt.inputMethod.commit()
        var lifecycle = (key === undefined || key === "")
        if (!_editLoaded || !hasUnsavedChanges) {
            if (finalize || lifecycle) _lastEditKey = ""
            return
        }
        // Open a frame on a control change, or on a lifecycle flush that is
        // NOT mid-gesture (a gesture already opened its frame on first tick).
        var newFrame = (!lifecycle && key !== _lastEditKey)
                       || (lifecycle && _lastEditKey === "")
        if (newFrame) {
            _undoStack.push(_committedState)
            // Cap depth but never drop index 0 — that is the loaded-record
            // baseline that makes Undo "repeatable down to the loaded record".
            if (_undoStack.length > kMaxUndoDepth) _undoStack.splice(1, 1)
            _undoDepth = _undoStack.length
            if (!lifecycle) _lastEditKey = key
        }
        // Persist only on a boundary: a freshly opened frame, an explicit
        // finalize, or a lifecycle flush. Coalesced continuous ticks defer.
        if (newFrame || finalize || lifecycle) {
            saveEditedShot()
            _committedState = captureEditState()
        }
        if (finalize || lifecycle) _lastEditKey = ""
    }

    // End an in-progress coalesced gesture: persist the deferred final value
    // and reset coalescing. Wired to focus-loss of the slider/steppers.
    function finalizeEdit() {
        autosave(_lastEditKey, true)
    }

    // Revert the most recent committed change. Repeatable down to the loaded
    // record; the reverted state is itself persisted.
    function undoLastChange() {
        if (_undoStack.length === 0) return
        Qt.inputMethod.commit()
        applyEditState(_undoStack.pop())
        _undoDepth = _undoStack.length
        // Force the next edit (even to the same control) to open a fresh
        // undo frame relative to this restored state.
        _lastEditKey = ""
        saveEditedShot()
        _committedState = captureEditState()
    }

    // Build a plain-JS clone of editShotData that carries every field the
    // page still reads after a save. Object.assign({}, editShotData) on the
    // Q_GADGET wrapper returned by shotReady() only copies own properties —
    // but Q_PROPERTYs on the wrapper are exposed as accessors on the
    // prototype, not as own properties of the instance, so Object.assign
    // silently drops durationSec, pressure/flow/weight/... arrays, dateTime,
    // profileName, debugLog, phases, badges, and every other read-only
    // field. That broke the AI Advice / Discuss / Re-Upload button visibility (predicate
    // `editShotData.durationSec > 0`), the graph, the badges row, the
    // phase summary, and the bottom-bar context labels the moment the user
    // made any edit. (The `_profileName`/`_visualizerId` caches in this file
    // were added in #1241 as targeted band-aids for the same root cause.)
    // Listing every field by name here works because direct dot access on a
    // Q_GADGET wrapper is fine — it's only the implicit enumeration in
    // Object.assign / spread that strips. Subsequent saves see `src` as the
    // plain-JS clone produced by the previous call, which still has every
    // key, so the chain holds.
    function clonePersistedShot(src) {
        return {
            id: src.id, uuid: src.uuid, timestamp: src.timestamp,
            timestampIso: src.timestampIso, dateTime: src.dateTime,
            profileName: src.profileName, profileKbId: src.profileKbId,
            profileJson: src.profileJson, profileNotes: src.profileNotes,
            beanNotes: src.beanNotes,
            temperatureOverrideC: src.temperatureOverrideC,
            targetWeightG: src.targetWeightG,
            durationSec: src.durationSec, debugLog: src.debugLog,
            stoppedBy: src.stoppedBy,
            visualizerId: src.visualizerId, visualizerUrl: src.visualizerUrl,
            hasVisualizerUpload: src.hasVisualizerUpload,
            channelingDetected: src.channelingDetected,
            grindIssueDetected: src.grindIssueDetected,
            skipFirstFrameDetected: src.skipFirstFrameDetected,
            pourTruncatedDetected: src.pourTruncatedDetected,
            detectorResults: src.detectorResults,
            summaryLines: src.summaryLines,
            phases: src.phases, phaseSummaries: src.phaseSummaries,
            pressure: src.pressure, flow: src.flow,
            temperature: src.temperature, temperatureMix: src.temperatureMix,
            resistance: src.resistance, conductance: src.conductance,
            darcyResistance: src.darcyResistance,
            conductanceDerivative: src.conductanceDerivative,
            waterDispensed: src.waterDispensed,
            pressureGoal: src.pressureGoal, flowGoal: src.flowGoal,
            temperatureGoal: src.temperatureGoal,
            weight: src.weight, weightFlowRate: src.weightFlowRate,
            // Editable fields — included so a clone that isn't followed by a
            // saveEditedShot field-override (e.g. onShotBadgesUpdated) still
            // carries the current persisted values.
            beanBrand: src.beanBrand, beanType: src.beanType,
            roastDate: src.roastDate, roastLevel: src.roastLevel,
            grinderBrand: src.grinderBrand, grinderModel: src.grinderModel,
            grinderBurrs: src.grinderBurrs, grinderSetting: src.grinderSetting,
            rpm: src.rpm,
            barista: src.barista, doseWeightG: src.doseWeightG,
            finalWeightG: src.finalWeightG, drinkTdsPct: src.drinkTdsPct,
            drinkEyPct: src.drinkEyPct, enjoyment0to100: src.enjoyment0to100,
            espressoNotes: src.espressoNotes, beverageType: src.beverageType,
            beanBaseJson: src.beanBaseJson
        }
    }

    // Save edited shot back to history
    // Sync sticky metadata back to Settings (bean/grinder info) for the
    // next shot — but ONLY when editing the most recent shot. The sticky
    // settings are "prep for the next pull"; editing a HISTORIC shot
    // (opened from Shot History / Shot Detail) must not touch the bean
    // dialog, dose/yield, or the live bean link. lastSavedShotId is
    // seeded from the DB at startup, so this holds across app restarts
    // too: the newest shot syncs forward, every older shot does not.
    // Per-shot fields (enjoyment, notes, TDS, EY) are NOT synced — otherwise
    // they would leak into the next shot's metadata, since MainController
    // builds shot metadata from these Settings values at shot end.
    //
    // Called SYNCHRONOUSLY from saveEditedShot — deliberately not deferred
    // to write confirmation: the exit-flush save (back button / auto-close)
    // outlives the page only as a background DB write, so a success-callback
    // sync would silently never run on the most common flow. If the write
    // fails, _saveFailed forces a retry which re-syncs; the brief divergence
    // on the rare failed-write-then-immediate-exit path is the lesser evil.
    function runStickySync() {
        var isMostRecentShot = editShotId > 0 && editShotId === MainController.lastSavedShotId
        if (!isMostRecentShot) return
        Settings.dye.dyeBeanBrand = editBeanBrand
        Settings.dye.dyeBeanType = editBeanType
        Settings.dye.dyeRoastDate = editRoastDate
        Settings.dye.dyeRoastLevel = editRoastLevel
        Settings.dye.dyeGrinderBrand = editGrinderBrand
        Settings.dye.dyeGrinderModel = editGrinderModel
        Settings.dye.dyeGrinderBurrs = editGrinderBurrs
        Settings.dye.dyeGrinderSetting = editGrinderSetting
        Settings.dye.dyeBarista = editBarista
        if (editDoseWeight > 0) Settings.dye.dyeBeanWeight = editDoseWeight
        if (editDrinkWeight > 0) Settings.dye.dyeDrinkWeight = editDrinkWeight
        // The link is sticky like the bean fields above: fixing the bean
        // on the shot you just pulled should carry to the next shot too.
        Settings.dye.dyeBeanBaseId = beanBaseLinked ? String(activeBeanBase.id) : ""
        Settings.dye.dyeBeanBaseData = beanBaseLinked ? editBeanBaseJson : ""
    }

    function saveEditedShot() {
        Qt.inputMethod.commit()
        if (editShotId <= 0) return
        pendingVisualizerUpdate = true
        var metadata = {
            "beanBrand": editBeanBrand,
            "beanType": editBeanType,
            "roastDate": editRoastDate,
            "roastLevel": editRoastLevel,
            // Grinder identity (brand/model/burrs) is ignored by the backend now
            // — it resolves via equipmentId. Kept here only so the Visualizer PATCH
            // (buildVisualizerOverrides) still sends the resolved grinder strings.
            "grinderBrand": editGrinderBrand,
            "grinderModel": editGrinderModel,
            "grinderBurrs": editGrinderBurrs,
            "grinderSetting": editGrinderSetting,
            "rpm": editRpm,
            "equipmentId": editEquipmentId,
            "barista": editBarista,
            "doseWeight": editDoseWeight,
            "finalWeight": editDrinkWeight,
            "drinkTds": editDrinkTds,
            "drinkEy": editDrinkEy,
            "espressoNotes": editNotes,
            "beverageType": editBeverageType,
            "beanBaseJson": editBeanBaseJson
        }
        metadata["enjoyment"] = editEnjoyment
        MainController.shotHistory.requestUpdateShotMetadata(editShotId, metadata)

        runStickySync()

        // Advance the in-memory baseline so hasUnsavedChanges clears at once.
        // We deliberately do NOT reload from the DB on save success — an async
        // reload would overwrite an edit the user has already started in
        // another field (autosave fires on every commit point).
        // clonePersistedShot (instead of Object.assign) preserves every
        // non-edited Q_GADGET field — see the helper's docstring for why.
        var nb = clonePersistedShot(editShotData)
        nb.beanBrand = editBeanBrand
        nb.beanType = editBeanType
        nb.roastDate = editRoastDate
        nb.roastLevel = editRoastLevel
        nb.grinderBrand = editGrinderBrand
        nb.grinderModel = editGrinderModel
        nb.grinderBurrs = editGrinderBurrs
        nb.equipmentId = editEquipmentId
        nb.equipmentName = editEquipmentName
        // Basket + puck prep are display-only but kept in sync so editShotData
        // stays a faithful mirror after a re-point (resolved from equipmentId on
        // the next load; copied here for the in-memory clone's consistency).
        nb.basketBrand = editBasketBrand
        nb.basketModel = editBasketModel
        nb.puckPrep = editPuckPrep
        nb.grinderSetting = editGrinderSetting
        nb.rpm = editRpm
        nb.barista = editBarista
        nb.doseWeightG = editDoseWeight
        nb.finalWeightG = editDrinkWeight
        nb.drinkTdsPct = editDrinkTds
        nb.drinkEyPct = editDrinkEy
        nb.beanBaseJson = editBeanBaseJson
        nb.enjoyment0to100 = editEnjoyment
        nb.espressoNotes = editNotes
        nb.beverageType = editBeverageType
        editShotData = nb
    }

    function buildVisualizerOverrides() {
        // grinderBurrs and beverageType are intentionally omitted — the Visualizer
        // PATCH body has no fields for them (only combined grinder_model for the
        // grinder; beverage_type is not part of the PATCH schema). Both values still
        // persist locally; they just don't propagate to visualizer.coffee.
        var overrides = {
            "beanBrand": editBeanBrand,
            "beanType": editBeanType,
            "roastDate": editRoastDate,
            "roastLevel": editRoastLevel,
            "grinderBrand": editGrinderBrand,
            "grinderModel": editGrinderModel,
            "grinderSetting": editGrinderSetting,
            "barista": editBarista,
            "doseWeightG": editDoseWeight,
            "finalWeightG": editDrinkWeight,
            "drinkTdsPct": editDrinkTds,
            "drinkEyPct": editDrinkEy,
            "espressoNotes": editNotes,
            "enjoyment0to100": editEnjoyment
        }
        // Only include profileName when non-empty; an empty string would cause
        // setStr to send null, clearing profile_title on visualizer.coffee.
        if (_profileName)
            overrides["profileName"] = _profileName
        return overrides
    }

    function maybeAutoUpdateVisualizer() {
        if (!pendingVisualizerUpdate) return
        if (!Settings.visualizer.visualizerAutoUpdate) return
        if (!MainController.visualizer) return
        // Only PATCH already-uploaded shots. Initial uploads are owned by the
        // shot-completion auto-upload flow and the manual button. editShotData may
        // be a plain-JS clone (clonePersistedShot, after badges/save) or the raw
        // gadget (untouched since onShotReady); the C++ method takes QVariant and
        // runs ShotProjection::coerce(), which accepts both — passing a
        // const ShotProjection& used to throw on the clone and silently drop the
        // PATCH.
        if (!_visualizerId) return
        pendingVisualizerUpdate = false
        _patchInFlight = true
        console.log("PostShotReview: auto-updating visualizer shot", _visualizerId, "for shot id", editShotId)
        MainController.visualizer.updateShotOnVisualizerWithOverrides(
            _visualizerId, editShotData, buildVisualizerOverrides())
    }

    // Handle upload status changes
    Connections {
        target: MainController.visualizer
        function onUploadingChanged() {
            if (AccessibilityManager.enabled) {
                if (MainController.visualizer.uploading) {
                    AccessibilityManager.announce(TranslationManager.translate("postshotreview.accessible.uploadingtovisualizer", "Uploading to Visualizer"), true)
                }
            }
        }
        function onLastUploadStatusChanged() {
            if (AccessibilityManager.enabled && MainController.visualizer.lastUploadStatus.length > 0) {
                AccessibilityManager.announce(MainController.visualizer.lastUploadStatus, true)
            }
        }
        function onUploadSucceededForShot(dbShotId, visualizerId, url) {
            // Filter by local DB shot id so this page reacts only to uploads for the
            // shot it is currently editing. This covers both uploads dispatched from
            // this page (manual button) AND the shot-completion auto-upload that may
            // finish while the user is already on this page — without this handler
            // the new visualizer id would not be visible until the page reopens.
            if (dbShotId !== postShotReviewPage.editShotId) return
            if (_firstUploadInFlight)
                _firstUploadInFlight = false
            uploadError = ""
            uploadSkipReason = ""
            if (url) {
                // clonePersistedShot (not Object.assign) so a first-time upload
                // on an unedited shot — where editShotData is still the raw
                // Q_GADGET wrapper from onShotReady — doesn't strip durationSec,
                // the frame arrays, dateTime, etc. See the helper's docstring.
                var nb = clonePersistedShot(editShotData)
                nb.visualizerId = visualizerId
                nb.visualizerUrl = url
                nb.hasVisualizerUpload = true
                editShotData = nb
                _visualizerId = visualizerId
            }
        }
        function onUpdateSuccess(visualizerId) {
            // updateSuccess carries no shot id. Filter on the in-flight flag we set
            // before dispatching the PATCH; ignore PATCHes initiated elsewhere (MCP),
            // which would otherwise clear uploadError and reset _patchInFlight for a
            // request we did not dispatch — leaving the page spuriously "clean".
            if (!_patchInFlight) return
            _patchInFlight = false
            uploadError = ""
            uploadSkipReason = ""
        }
        function onUploadFailed(error) {
            // Only surface and react when the failure belongs to a request we
            // dispatched. Without this guard, an unrelated background upload failure
            // would set uploadError on this page and leave our in-flight flag stuck.
            if (!_firstUploadInFlight && !_patchInFlight) return
            _firstUploadInFlight = false
            _patchInFlight = false
            uploadError = error
            // pendingVisualizerUpdate is already cleared by every dispatch site
            // (manual upload button onClicked, maybeAutoUpdateVisualizer above) before
            // the network request goes out, so there is nothing to roll back here.
        }
        function onUploadSkipped(reason) {
            // Policy rejection (maintenance profile, too-short shot). Clear the
            // in-flight flag the same way onUploadFailed does, but populate the
            // informational uploadSkipReason instead of uploadError so the page
            // doesn't surface a red "Upload failed" string for a deliberate skip.
            if (!_firstUploadInFlight && !_patchInFlight) return
            _firstUploadInFlight = false
            _patchInFlight = false
            uploadSkipReason = reason
        }
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        targetFlickable: flickable
        textFields: [
            settingField.textField, rpmField.textField, baristaField.textField,
            notesExpandable.textField
        ]

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentHeight: mainColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onMovementStarted: resetAutoCloseTimer()
        onContentYChanged: resetAutoCloseTimer()

        ColumnLayout {
            id: mainColumn
            width: parent.width
            spacing: Theme.scaled(6)

            // Header: Profile (Temp) + date + quality badges + sparkle + Read TDS + Basic/Advanced toggle
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: !!(editShotData.pressure && editShotData.pressure.length > 0)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            textFormat: Text.RichText
                            text: {
                                var name = editShotData.profileName || ""
                                var t = editShotData.temperatureOverrideC
                                var result
                                if (t !== undefined && t !== null && t > 0) {
                                    result = name + " (" + Math.round(Theme.cToDisplay(t)) + Theme.tempUnitSuffix() + ")"
                                } else {
                                    result = name
                                }
                                return Theme.replaceEmojiWithImg(result, Theme.titleFont.pixelSize)
                            }
                            font: Theme.titleFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: editShotData.dateTime || ""
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                            Layout.maximumWidth: postShotReviewPage.width * 0.35
                        }

                        QualityBadges {
                            visible: !!(editShotData.profileKbId
                                        || editShotData.channelingDetected
                                        || editShotData.grindIssueDetected
                                        || editShotData.skipFirstFrameDetected
                                        || editShotData.pourTruncatedDetected)
                            Layout.fillWidth: false
                            Layout.maximumWidth: postShotReviewPage.width * 0.5
                            channelingDetected: editShotData.channelingDetected ?? false
                            grindIssueDetected: editShotData.grindIssueDetected ?? false
                            skipFirstFrameDetected: editShotData.skipFirstFrameDetected ?? false
                            pourTruncatedDetected: editShotData.pourTruncatedDetected ?? false
                            verdictCategory: (editShotData && editShotData.detectorResults)
                                ? (editShotData.detectorResults.verdictCategory ?? "") : ""
                            onSummaryRequested: reviewAnalysisDialog.open()
                        }

                        ShotAnalysisDialog {
                            id: reviewAnalysisDialog
                            shotData: editShotData
                        }
                    }
                }

                // KB sparkle button — opens the profile knowledge base
                Image {
                    id: headerSparkle
                    visible: !!(editShotData.profileKbId)
                    source: "qrc:/icons/sparkle.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                    Layout.alignment: Qt.AlignVCenter
                    opacity: headerSparkleArea.containsMouse ? 1.0 : 0.6
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }

                    AccessibleMouseArea {
                        id: headerSparkleArea
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(-8)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        accessibleName: TranslationManager.translate("profileselector.accessible.view_knowledge", "View AI knowledge base")
                        accessibleItem: headerSparkle
                        onAccessibleClicked: {
                            shotKnowledgeDialog.profileTitle = editShotData.profileName || ""
                            shotKnowledgeDialog.content = ProfileManager.profileKnowledgeContent(editShotData.profileName)
                            shotKnowledgeDialog.open()
                        }
                    }
                }

                // Read TDS button (DiFluid R1 / R2 refractometer)
                Rectangle {
                    id: readTdsButton
                    property bool refConnected: BLEManager.refractometerConnected
                    property bool refMeasuring: refConnected && typeof Refractometer !== "undefined" && Refractometer && Refractometer.measuring
                    // R1 advertises with names starting "DFT_TDJ_*" (see DiFluidR1::isR1Device).
                    property bool isR1: (Settings.savedRefractometerName || "").toLowerCase().indexOf("dft_tdj") === 0
                    visible: Settings.savedRefractometerAddress !== ""
                    Layout.preferredWidth: Theme.scaled(80)
                    Layout.preferredHeight: Theme.scaled(36)
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.scaled(12)
                    color: Theme.surfaceColor
                    border.width: 1
                    border.color: Theme.textSecondaryColor
                    opacity: refMeasuring ? 0.5 : 1.0
                    Accessible.ignored: true

                    Text {
                        anchors.centerIn: parent
                        text: {
                            if (!readTdsButton.refConnected) {
                                return readTdsButton.isR1
                                    ? TranslationManager.translate("postshotreview.refractometer.r1off", "R1 Off")
                                    : TranslationManager.translate("postshotreview.refractometer.r2off", "R2 Off")
                            }
                            if (readTdsButton.refMeasuring) return TranslationManager.translate("postshotreview.refractometer.measuring", "...")
                            return TranslationManager.translate("postshotreview.refractometer.readTds", "Read TDS")
                        }
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                        Accessible.ignored: true
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: readTdsButton.refConnected
                            ? TranslationManager.translate("postshotreview.readTdsFromRefractometer", "Read TDS from refractometer")
                            : TranslationManager.translate("postshotreview.reconnectRefractometer", "Reconnect refractometer")
                        accessibleItem: readTdsButton
                        enabled: !readTdsButton.refMeasuring
                        onAccessibleClicked: {
                            if (readTdsButton.refConnected) {
                                if (typeof Refractometer !== "undefined" && Refractometer)
                                    Refractometer.requestMeasurement()
                            } else {
                                BLEManager.scanForDevices()
                            }
                        }
                    }
                }

                // Basic/Advanced mode toggle (matches espresso page view selector)
                Rectangle {
                    Layout.preferredWidth: Theme.scaled(36)
                    Layout.preferredHeight: Theme.scaled(36)
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.scaled(18)
                    color: postShotReviewPage.advancedMode ? Theme.accentColor : Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: Theme.scaled(1)

                    Accessible.ignored: true

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/settings.svg"
                        sourceSize.width: Theme.scaled(18)
                        sourceSize.height: Theme.scaled(18)

                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: postShotReviewPage.advancedMode ? Theme.primaryContrastColor : Theme.textColor
                        }
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: postShotReviewPage.advancedMode
                            ? TranslationManager.translate("shotReview.mode.switchBasic", "Switch to basic view")
                            : TranslationManager.translate("shotReview.mode.switchAdvanced", "Switch to advanced view")
                        accessibleItem: parent
                        accessibleRole: Accessible.CheckBox
                        accessibleChecked: postShotReviewPage.advancedMode
                        onAccessibleClicked: {
                            postShotReviewPage.advancedMode = !postShotReviewPage.advancedMode
                            Settings.setValue("shotReview/advancedMode", postShotReviewPage.advancedMode)
                        }
                    }
                }
            }

            // Shot Plan snapshot line — this shot's dial-in rendered as a
            // glanceable sentence beneath the title. Bound to the page's LIVE
            // edit state (the source of truth here), so it updates as the user
            // edits dose/grind/beans. Reuses the home-screen ShotPlanText
            // renderer so the format can't drift. Non-interactive.
            ShotPlanText {
                id: shotPlanSnapshot
                Layout.fillWidth: true
                visible: text !== ""
                sentence: false
                maxLines: 2
                // Fields + order come from the user's Shot Plan widget config.
                itemOrder: postShotReviewPage._shotPlanItemOrder
                singleTemp: true
                profileName: editShotData.profileName || ""
                dose: editDoseWeight || 0
                // targetWeightG is the planned target (0 for volume/timer
                // profiles) — fall back to the edited output so a yield still shows.
                profileYield: editShotData.targetWeightG || 0
                targetWeight: (editShotData.targetWeightG || 0) > 0
                    ? editShotData.targetWeightG : (editDrinkWeight || 0)
                yieldTargetOnly: true
                // temperatureOverrideC always carries the effective brew temp
                // (user override OR profile default); 0 only for legacy volume shots.
                profileTemp: editShotData.temperatureOverrideC || 0
                overrideTemp: editShotData.temperatureOverrideC || 0
                tempOverridden: false
                roasterBrand: editBeanBrand
                coffeeName: editBeanType
                roastDate: editRoastDate
                grindSize: editGrinderSetting
                grindRpm: editRpm
                // Only show RPM for grinders that actually report it (a Niche
                // Zero does not); a stale/spurious recorded RPM must not surface.
                rpmCapable: editRpmCapable
                beverageType: editBeverageType || "espresso"
                isCleaning: false
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.focusable: true
            }

            GraphInspectBar { graph: reviewGraph }

            // Resizable Graph (visible when we have shot data)
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), postShotReviewPage.graphHeight))
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: !!(editShotData.pressure && editShotData.pressure.length > 0)
                Accessible.role: Accessible.Graphic
                Accessible.name: TranslationManager.translate("shot.graph.accessible.name", "Shot graph. Tap to inspect values")
                Accessible.focusable: true
                Accessible.onPressAction: reviewGraphMouseArea.clicked(null)

                HistoryShotGraph {
                    id: reviewGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    advancedMode: postShotReviewPage.advancedMode
                    showPhaseLabels: postShotReviewPage.advancedMode
                    pressureData: editShotData.pressure || []
                    flowData: editShotData.flow || []
                    temperatureData: editShotData.temperature || []
                    weightData: editShotData.weight || []
                    weightFlowRateData: editShotData.weightFlowRate || []
                    resistanceData: editShotData.resistance || []
                    conductanceData: editShotData.conductance || []
                    darcyResistanceData: editShotData.darcyResistance || []
                    conductanceDerivativeData: editShotData.conductanceDerivative || []
                    temperatureMixData: editShotData.temperatureMix || []
                    pressureGoalData: editShotData.pressureGoal || []
                    flowGoalData: editShotData.flowGoal || []
                    temperatureGoalData: editShotData.temperatureGoal || []
                    phaseMarkers: editShotData.phases || []
                    maxTime: editShotData.durationSec || 60
                }

                // Tap/drag-to-inspect overlay (shows crosshair, values shown above graph)
                MouseArea {
                    id: reviewGraphMouseArea
                    anchors.fill: reviewGraph
                    onClicked: function(mouse) {
                        if (mouse.x > reviewGraph.plotArea.x + reviewGraph.plotArea.width) {
                            reviewGraph.toggleRightAxis()
                        } else {
                            reviewGraph.inspectAtPosition(mouse.x, mouse.y)
                        }
                    }
                    onPositionChanged: function(mouse) {
                        if (pressed) {
                            reviewGraph.inspectAtPosition(mouse.x, mouse.y)
                        }
                    }
                }

                // Resize handle at bottom
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.ignored: true

                    // Visual indicator (three lines)
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Repeater {
                            model: 3
                            Rectangle {
                                width: Theme.scaled(30)
                                height: 1
                                color: Theme.textSecondaryColor
                                opacity: resizeMouseArea.containsMouse || resizeMouseArea.pressed ? 0.8 : 0.4
                            }
                        }
                    }

                    MouseArea {
                        id: resizeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeVerCursor
                        preventStealing: true

                        property real startY: 0
                        property real startHeight: 0

                        onPressed: function(mouse) {
                            startY = mouse.y + resizeHandle.mapToItem(postShotReviewPage, 0, 0).y
                            startHeight = graphCard.Layout.preferredHeight
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(postShotReviewPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                // Clamp between min and max
                                newHeight = Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), newHeight))
                                postShotReviewPage.graphHeight = newHeight
                            }
                        }

                        onReleased: {
                            Settings.setValue("postShotReview/graphHeight", postShotReviewPage.graphHeight)
                            flickable.returnToBounds()
                        }
                    }
                }

            }

            GraphLegend {
                graph: reviewGraph
                advancedMode: postShotReviewPage.advancedMode
                visible: !!(editShotData.pressure && editShotData.pressure.length > 0)
            }

            // Phase summary panel (advanced mode only)
            PhaseSummaryPanel {
                Layout.fillWidth: true
                phaseSummaries: editShotData.phaseSummaries || []
                visible: postShotReviewPage.advancedMode && (editShotData.phaseSummaries || []).length > 0
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Tr {
                    id: ratingLabel
                    key: "rating.quick.prompt"
                    fallback: "How was this shot?"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    // Cap on an ancestor whose width does not depend on this label,
                    // instead of `parent.width`. `parent` is the RowLayout, whose width
                    // depends on this child's preferred size — that mutual dependency
                    // tripped Qt Quick Layouts' "recursive rearrange" guard in
                    // production. Binding to postShotReviewPage.width breaks the cycle;
                    // the label still sizes to its implicitWidth, capped to ~45% of
                    // the page.
                    Layout.maximumWidth: postShotReviewPage.width * 0.45
                    Accessible.ignored: true
                }

                Rectangle {
                    id: ratingBox
                    Layout.fillWidth: true
                    height: Theme.scaled(44)
                    radius: Theme.scaled(12)
                    color: Theme.surfaceColor
                    border.width: 1
                    border.color: Theme.textSecondaryColor

                    RatingInput {
                        id: ratingInput
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(4)
                        value: editEnjoyment
                        accessibleName: TranslationManager.translate("rating.quick.prompt", "How was this shot?")
                        onValueModified: function(newValue) {
                            editEnjoyment = newValue
                            postShotReviewPage.autosave("rating")
                        }
                        onActiveFocusChanged: if (!activeFocus) postShotReviewPage.finalizeEdit()
                    }
                }
            }

            // Notes (moved to top, right after rating)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Tr {
                    id: notesLabel
                    key: "postshotreview.label.notes"
                    fallback: "Notes"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(11)
                    Accessible.ignored: true
                }

                ExpandableTextArea {
                    id: notesExpandable
                    Layout.fillWidth: true
                    inlineHeight: Theme.scaled(100)
                    text: editNotes
                    accessibleName: TranslationManager.translate("postshotreview.label.notes", "Notes")
                    textFont: Theme.bodyFont
                    onTextChanged: editNotes = text
                    onEditingFinished: postShotReviewPage.autosave("notes", true)
                }
            }

            // === Measurements (Dose, Out, TDS, EY) ===
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: measurementsLabel.height + measurementsRow.height + 4

                Tr {
                    id: measurementsLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    key: "postshotreview.section.measurements"
                    fallback: "Measurements"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(11)
                    Accessible.ignored: true
                }

                RowLayout {
                    id: measurementsRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: measurementsLabel.bottom
                    anchors.topMargin: Theme.scaled(2)
                    spacing: Theme.scaled(6)

                    // Dose (bean weight)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1   // equal share of the row
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "postshotreview.label.dose"
                            fallback: "Dose"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            Accessible.ignored: true
                        }
                        ValueInput {
                            id: doseInput
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            from: 0
                            to: 40
                            stepSize: 0.1
                            decimals: 1
                            suffix: "g"
                            valueColor: Theme.dyeDoseColor
                            value: editDoseWeight
                            accessibleName: TranslationManager.translate("postshotreview.label.dose", "Dose") + " " + value + " " + TranslationManager.translate("postshotreview.unit.grams", "grams")
                            onValueModified: function(newValue) {
                                doseInput.value = newValue
                                editDoseWeight = newValue
                                calculateEy()
                                postShotReviewPage.autosave("dose")
                            }
                            // valueCommitted is ValueInput's real end-of-
                            // interaction signal (drag release / +/- release /
                            // typed commit) — touch interactions never change
                            // active focus, so this is what flushes the
                            // deferred coalesced value. The focus-loss branch
                            // covers the keyboard/tab path.
                            onValueCommitted: postShotReviewPage.finalizeEdit()
                            onActiveFocusChanged: {
                                if (activeFocus) { Qt.inputMethod.commit(); Qt.inputMethod.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                        }
                    }

                    // Out (drink weight)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "postshotreview.label.out"
                            fallback: "Out"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            Accessible.ignored: true
                        }
                        ValueInput {
                            id: outInput
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            from: 0
                            to: 500
                            stepSize: 0.1
                            decimals: 1
                            suffix: "g"
                            valueColor: Theme.dyeOutputColor
                            value: editDrinkWeight
                            accessibleName: TranslationManager.translate("postshotreview.accessible.output", "Output") + " " + value + " " + TranslationManager.translate("postshotreview.unit.grams", "grams")
                            onValueModified: function(newValue) {
                                outInput.value = newValue
                                editDrinkWeight = newValue
                                calculateEy()
                                postShotReviewPage.autosave("out")
                            }
                            // valueCommitted is ValueInput's real end-of-
                            // interaction signal (drag release / +/- release /
                            // typed commit) — touch interactions never change
                            // active focus, so this is what flushes the
                            // deferred coalesced value. The focus-loss branch
                            // covers the keyboard/tab path.
                            onValueCommitted: postShotReviewPage.finalizeEdit()
                            onActiveFocusChanged: {
                                if (activeFocus) { Qt.inputMethod.commit(); Qt.inputMethod.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                        }
                    }

                    // Grind (moved from the field grid — the most-adjusted
                    // dial-in, now beside Dose/Out).
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "shotdetail.grind"
                            fallback: "Grind"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            Accessible.ignored: true
                        }
                        SuggestionField {
                            id: settingField
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            fieldColor: Theme.surfaceColor   // match the Dose/Out steppers
                            label: ""
                            text: editGrinderSetting
                            suggestions: {
                                var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(editGrinderModel) : []
                                if (editGrinderSetting.length > 0 && list.indexOf(editGrinderSetting) === -1) list = [editGrinderSetting].concat(list)
                                return list
                            }
                            onTextEdited: function(t) { editGrinderSetting = t }
                            onInputBlurred: postShotReviewPage.autosave("grinderSetting", true)
                        }
                    }

                    // RPM (only when the grinder is rpm-adjustable)
                    ColumnLayout {
                        visible: postShotReviewPage.editRpmCapable
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "postshotreview.label.rpm"
                            fallback: "RPM"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            Accessible.ignored: true
                        }
                        SuggestionField {
                            id: rpmField
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            fieldColor: Theme.surfaceColor   // match the Dose/Out steppers
                            label: ""
                            text: editRpm > 0 ? String(editRpm) : ""
                            suggestions: []
                            onTextEdited: function(t) { editRpm = parseInt(t) || 0 }
                            onInputBlurred: postShotReviewPage.autosave("rpm", true)
                        }
                    }

                    // TDS (advanced mode only)
                    ColumnLayout {
                        visible: postShotReviewPage.advancedMode
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.scaled(2)
                        RowLayout {
                            spacing: Theme.scaled(4)
                            Tr {
                                key: "postshotreview.label.tds"
                                fallback: "TDS%"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }
                            // Refractometer status dot (only when configured)
                            Rectangle {
                                width: Theme.scaled(6)
                                height: Theme.scaled(6)
                                radius: Theme.scaled(3)
                                visible: Settings.savedRefractometerAddress !== ""
                                color: {
                                    if (!BLEManager.refractometerConnected) return Theme.textSecondaryColor
                                    if (typeof Refractometer !== "undefined" && Refractometer && Refractometer.tds > 0) return Theme.successColor
                                    return Theme.accentColor
                                }
                                Accessible.ignored: true
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            ValueInput {
                                id: tdsInput
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(36)
                                from: 0
                                to: 20
                                stepSize: 0.01
                                decimals: 2
                                suffix: ""
                                valueColor: Theme.dyeTdsColor
                                value: editDrinkTds
                                accessibleName: TranslationManager.translate("postshotreview.label.tds", "TDS") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                                onValueModified: function(newValue) {
                                    editDrinkTds = newValue
                                    calculateEy()
                                    postShotReviewPage.autosave("tds")
                                }
                                // valueCommitted is ValueInput's real end-of-
                            // interaction signal (drag release / +/- release /
                            // typed commit) — touch interactions never change
                            // active focus, so this is what flushes the
                            // deferred coalesced value. The focus-loss branch
                            // covers the keyboard/tab path.
                            onValueCommitted: postShotReviewPage.finalizeEdit()
                            onActiveFocusChanged: {
                                if (activeFocus) { Qt.inputMethod.commit(); Qt.inputMethod.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                            }
                        }
                    }

                    // EY (advanced mode only)
                    ColumnLayout {
                        visible: postShotReviewPage.advancedMode
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "postshotreview.label.ey"
                            fallback: "EY%"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            Accessible.ignored: true
                        }
                        ValueInput {
                            id: eyInput
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            from: 0
                            to: 30
                            stepSize: 0.1
                            decimals: 1
                            suffix: ""
                            valueColor: Theme.dyeEyColor
                            value: editDrinkEy
                            accessibleName: TranslationManager.translate("postshotreview.accessible.extractionyield", "Extraction yield") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                            onValueModified: function(newValue) {
                                editDrinkEy = newValue
                                postShotReviewPage.autosave("ey")
                            }
                            // valueCommitted is ValueInput's real end-of-
                            // interaction signal (drag release / +/- release /
                            // typed commit) — touch interactions never change
                            // active focus, so this is what flushes the
                            // deferred coalesced value. The focus-loss branch
                            // covers the keyboard/tab path.
                            onValueCommitted: postShotReviewPage.finalizeEdit()
                            onActiveFocusChanged: {
                                if (activeFocus) { Qt.inputMethod.commit(); Qt.inputMethod.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                        }
                    }
                }
            }

            // Standalone bean summary (+ Change Beans) — shown ONLY when the
            // shot used no recipe. With a recipe these fold into the recipe card
            // above, so it reads as one cohesive recipe. Bean dialog + equipment
            // picker live at page scope (shared with the recipe card).
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: (editShotData.recipeId || -1) <= 0

                BeanSummary {
                    id: reviewBeanSummary
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    useShotData: true
                    roasterName: editBeanBrand
                    coffeeName: editBeanType
                    roastDate: editRoastDate
                    roastLevel: editRoastLevel
                    beanBaseData: editBeanBaseJson
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(44)
                    Layout.alignment: Qt.AlignVCenter
                    text: reviewBeanSummary.hasBeans
                        ? TranslationManager.translate("beans.button.change", "Change Beans")
                        : TranslationManager.translate("beans.button.select", "Select Beans")
                    accessibleName: TranslationManager.translate("beans.button.accessible.change", "Change the selected beans")
                    onClicked: reviewChangeBeansDialog.open()
                }
            }

            BeanBaseDetailsRow {
                Layout.fillWidth: true
                visible: (editShotData.recipeId || -1) <= 0
                beanBaseJson: postShotReviewPage.editBeanBaseJson
            }

            // Best-effort enrichment merge after a canonical pick (same
            // contract as BeanInfoPage, but into the SHOT's snapshot).
            Connections {
                target: MainController.beanbase
                function onCanonicalDetails(canonicalId, attrs) {
                    if (!postShotReviewPage.beanBaseLinked
                        || postShotReviewPage.activeBeanBase.id !== canonicalId) return
                    var merged
                    try { merged = JSON.parse(postShotReviewPage.editBeanBaseJson) } catch (e) {
                        console.warn("PostShotReviewPage: enrichment merge skipped — unparseable blob")
                        return
                    }
                    for (var k in attrs) merged[k] = attrs[k]
                    postShotReviewPage.editBeanBaseJson = JSON.stringify(merged)
                    if (attrs.degree) postShotReviewPage.editRoastLevel = attrs.degree
                    postShotReviewPage.autosave("beanBase", true)
                }
            }

            // 3-column grid for all fields
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 6

                // (Bean identity fields removed — the read-only BeanSummary +
                // Change Beans dialog above replace them.)
                //
                // Grinder identity (brand/model/burrs) is owned by the equipment
                // PACKAGE now (add-equipment-packages), so it is READ-ONLY here and
                // changed by re-pointing the shot to a different package via the
                // picker — not edited as free text (those edits were silently
                // discarded).
                // (Equipment identity card moved to the END of this grid — per-shot
                // dial-in and shot metadata first, hardware context last.)

                // Grind + RPM moved up into the Dial-in row (with Dose/Out).

                // Beverage type is captured from the profile at shot time and is
                // not editable — we trust the profile, and the recipe now
                // preserves the shot's context. (editBeverageType still carries
                // the shot's captured value through save unchanged.)

                // Barista — advanced-only (most users are the sole barista).
                SuggestionField {
                    id: baristaField
                    visible: postShotReviewPage.advancedMode
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.barista", "Barista")
                    text: editBarista
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctBaristas() : []
                        if (editBarista.length > 0 && list.indexOf(editBarista) === -1) list = [editBarista].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { editBarista = t }
                    onInputBlurred: postShotReviewPage.autosave("barista", true)
                }

                // Preset (profile) and Shot date were removed here — both were
                // read-only and already shown in the title, the Shot Plan
                // snapshot line, and the recipe card, so they only added clutter.

                // Recipe card (recipeId > 0): the recipe AND its editable
                // components in one cohesive card, modelled on the recipe
                // editor's summary. Beans, dial-in (grind/RPM) and equipment are
                // edited right here (the edit controls the user asked to keep in
                // the card); profile/steam/water are read-only. Every value is
                // this page's live edit state. When a recipe is used this
                // replaces the standalone bean/grind/equipment controls (which
                // gate to the no-recipe case).
                Rectangle {
                    id: recipeCard
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredHeight: recipeColumn.implicitHeight + Theme.scaled(24)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 1
                    border.color: Theme.borderColor
                    visible: (editShotData.recipeId || -1) > 0

                    readonly property string recipeName: postShotReviewPage.resolvedRecipe.name || ""
                    readonly property string recipeDrinkLabel:
                        DrinkType.shortLabel(DrinkType.fromRecipeMap(postShotReviewPage.resolvedRecipe))

                    Accessible.role: Accessible.Grouping
                    Accessible.name: {
                        var parts = [TranslationManager.translate("shotdetail.recipe", "Recipe")]
                        if (recipeName !== "") parts.push(recipeName)
                        if (recipeDrinkLabel !== "") parts.push(recipeDrinkLabel)
                        var p = postShotReviewPage.recipeProfileText(); if (p !== "") parts.push(p)
                        return parts.join(", ")
                    }

                    ColumnLayout {
                        id: recipeColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.spacingSmall

                        // --- Hero: eyebrow + recipe name + drink type ---
                        Tr {
                            key: "shotdetail.recipe"
                            fallback: "Recipe"
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: recipeCard.recipeName !== ""
                            textFormat: Text.RichText
                            text: Theme.replaceEmojiWithImg(recipeCard.recipeName, Theme.titleFont.pixelSize)
                            font: Theme.titleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(6)
                            visible: recipeCard.recipeDrinkLabel !== ""
                            ColoredIcon {
                                Layout.alignment: Qt.AlignVCenter
                                source: DrinkType.icon(DrinkType.fromRecipeMap(postShotReviewPage.resolvedRecipe))
                                iconWidth: Theme.scaled(16)
                                iconHeight: Theme.scaled(16)
                                iconColor: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: recipeCard.recipeDrinkLabel
                                font: Theme.bodyFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.scaled(2)
                            Layout.bottomMargin: Theme.scaled(2)
                            height: Theme.scaled(1)
                            color: Theme.borderColor
                            Accessible.ignored: true
                        }

                        // Profile (read-only)
                        RecipeField {
                            fieldLabel: trRowProfile.text
                            value: postShotReviewPage.recipeProfileText()
                        }

                        // Beans (editable — Change Beans opens the shared dialog)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Text {
                                text: trRowBeans.text
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)
                                BeanSummary {
                                    id: reviewRecipeBeanSummary
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    useShotData: true
                                    roasterName: editBeanBrand
                                    coffeeName: editBeanType
                                    roastDate: editRoastDate
                                    roastLevel: editRoastLevel
                                    beanBaseData: editBeanBaseJson
                                }
                                AccessibleButton {
                                    Layout.preferredHeight: Theme.scaled(44)
                                    Layout.alignment: Qt.AlignVCenter
                                    text: reviewRecipeBeanSummary.hasBeans
                                        ? TranslationManager.translate("beans.button.change", "Change Beans")
                                        : TranslationManager.translate("beans.button.select", "Select Beans")
                                    accessibleName: TranslationManager.translate("beans.button.accessible.change", "Change the selected beans")
                                    onClicked: reviewChangeBeansDialog.open()
                                }
                            }
                            BeanBaseDetailsRow {
                                Layout.fillWidth: true
                                beanBaseJson: postShotReviewPage.editBeanBaseJson
                            }
                        }

                        // Dial-in (read-only) — grind is edited in the field grid
                        // below, echoed here as part of the recipe overview.
                        RecipeField {
                            fieldLabel: trRowDialIn.text
                            value: postShotReviewPage.recipeDialInText()
                        }

                        // Steam / Hot water (read-only)
                        RecipeField {
                            fieldLabel: trRowSteam.text
                            value: postShotReviewPage.recipeSteamText()
                        }
                        RecipeField {
                            fieldLabel: trRowWater.text
                            value: postShotReviewPage.recipeWaterText()
                        }

                        // Equipment (editable — Change Equipment opens the picker)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Text {
                                text: trRowEquipment.text
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }
                            EquipmentSummary {
                                id: reviewRecipeEquipment
                                Layout.fillWidth: true
                                visible: reviewRecipeEquipment.accessibleSummary !== ""
                                grinderName: editEquipmentName || ""
                                grinderBrand: editGrinderBrand
                                grinderModel: editGrinderModel
                                grinderBurrs: editGrinderBurrs
                                basketBrand: editBasketBrand
                                basketModel: editBasketModel
                                puckPrepCanonical: editPuckPrep
                            }
                            AccessibleButton {
                                Layout.preferredHeight: Theme.scaled(36)
                                _customFontSize: Theme.captionFont.pixelSize
                                leftPadding: Theme.scaled(10)
                                rightPadding: Theme.scaled(10)
                                text: (editEquipmentName.length > 0 || editGrinderBrand.length > 0 || editGrinderModel.length > 0)
                                      ? TranslationManager.translate("postshotreview.changeEquipment", "Change Equipment")
                                      : TranslationManager.translate("postshotreview.addEquipment", "Add Equipment")
                                accessibleName: text
                                onClicked: shotEquipmentDialog.openPicker()
                            }
                        }
                    }
                }

                // Equipment identity card (grinder + basket + puck prep), styled
                // like the inventory EquipmentCard and sharing its EquipmentSummary
                // renderer. Deliberately LAST in the grid: the editable per-shot
                // dial-in and shot metadata above come first; the card is trailing
                // hardware context. Grind setting + RPM are omitted here — they are
                // the per-shot dial-in edited in the fields above, so echoing them
                // read-only would only duplicate. Re-point via the Change Equipment
                // button (occupying the same action-button row the inventory card
                // uses); all details live on the card, so there is no separate info
                // button.
                Rectangle {
                    id: equipmentCard
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredHeight: equipmentCardColumn.implicitHeight + Theme.scaled(24)
                    // With a recipe, equipment folds into the recipe card above.
                    visible: (editShotData.recipeId || -1) <= 0
                    readonly property bool hasEquipment: editEquipmentName.length > 0
                                                         || editGrinderBrand.length > 0 || editGrinderModel.length > 0
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 1
                    border.color: Theme.borderColor
                    Accessible.role: Accessible.Grouping
                    Accessible.name: TranslationManager.translate("postshotreview.label.equipment", "Equipment:")
                        + " " + (hasEquipment ? equipmentSummary.accessibleSummary
                                              : TranslationManager.translate("postshotreview.equipmentNotSet", "Not set"))

                    ColumnLayout {
                        id: equipmentCardColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(6)

                        EquipmentSummary {
                            id: equipmentSummary
                            Layout.fillWidth: true
                            visible: equipmentCard.hasEquipment
                            grinderName: editEquipmentName || ""
                            grinderBrand: editGrinderBrand
                            grinderModel: editGrinderModel
                            grinderBurrs: editGrinderBurrs
                            basketBrand: editBasketBrand
                            basketModel: editBasketModel
                            puckPrepCanonical: editPuckPrep
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !equipmentCard.hasEquipment
                            elide: Text.ElideRight
                            text: TranslationManager.translate("postshotreview.equipmentNotSet", "Not set")
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.subtitleFont.pixelSize
                            font.bold: true
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }
                        AccessibleButton {
                            Layout.preferredHeight: Theme.scaled(36)
                            _customFontSize: Theme.captionFont.pixelSize
                            leftPadding: Theme.scaled(10)
                            rightPadding: Theme.scaled(10)
                            text: equipmentCard.hasEquipment
                                  ? TranslationManager.translate("postshotreview.changeEquipment", "Change Equipment")
                                  : TranslationManager.translate("postshotreview.addEquipment", "Add Equipment")
                            accessibleName: text
                            onClicked: shotEquipmentDialog.openPicker()
                        }
                    }
                }

            }

            Item { Layout.preferredHeight: 10 }
        }
    }

    } // KeyboardAwareContainer

    // Change Beans + Change Equipment — page-scoped so both the recipe card and
    // the standalone bean/equipment rows share one instance regardless of which
    // is visible.
    ChangeBeansDialog {
        id: reviewChangeBeansDialog
        // Only the most recent shot is the "post-shot" fix path (sets
        // activeBagId too); older shots opened through this page are historical
        // — retag the shot only.
        context: editShotId === MainController.lastSavedShotId ? "postShot" : "historicalShot"
        shotId: postShotReviewPage.editShotId
        onBagSelected: function(bagId, bag) {
            // The dialog already wrote the snapshot to the DB — mirror it into
            // the edit fields and advance the autosave baseline so a later
            // autosave doesn't clobber the new bag with stale values.
            editBeanBrand = bag.roasterName || ""
            editBeanType = bag.coffeeName || ""
            editRoastDate = bag.roastDate || ""
            editRoastLevel = bag.roastLevel || ""
            editBeanBaseJson = bag.beanBaseData || ""
            var nb = clonePersistedShot(editShotData)
            nb.beanBrand = editBeanBrand
            nb.beanType = editBeanType
            nb.roastDate = editRoastDate
            nb.roastLevel = editRoastLevel
            nb.beanBaseJson = editBeanBaseJson
            editShotData = nb
            _committedState = captureEditState()
            pendingVisualizerUpdate = true
        }
    }
    // Re-point this shot's grinder to a different/new package. The picker
    // doesn't touch the active bag (applyToActiveBag:false); we resolve the
    // chosen package and persist equipmentId here.
    SwitchEquipmentDialog {
        id: shotEquipmentDialog
        applyToActiveBag: false
        onPackageSaved: function(packageId) {
            postShotReviewPage._pendingEquipmentId = packageId
            MainController.equipmentStorage.requestPackage(packageId)
        }
    }
    Connections {
        target: MainController.equipmentStorage
        function onPackageReady(packageId, pkg) {
            if (packageId !== postShotReviewPage._pendingEquipmentId) return
            postShotReviewPage._pendingEquipmentId = -1
            postShotReviewPage.editEquipmentId = packageId
            postShotReviewPage.editGrinderBrand = pkg.grinderBrand || ""
            postShotReviewPage.editGrinderModel = pkg.grinderModel || ""
            postShotReviewPage.editGrinderBurrs = pkg.grinderBurrs || ""
            postShotReviewPage.editBasketBrand = pkg.basketBrand || ""
            postShotReviewPage.editBasketModel = pkg.basketModel || ""
            postShotReviewPage.editPuckPrep = pkg.puckPrepCanonical || ""
            postShotReviewPage.editEquipmentName =
                (pkg.name && String(pkg.name).length > 0) ? String(pkg.name) : ""
            postShotReviewPage.autosave("equipment", true)
        }
    }

    // Bottom bar (stays visible under keyboard)
    BottomBar {
        title: TranslationManager.translate("postshotreview.title", "Shot Review")
        onBackClicked: handleBack()

        // Profile name + date remain visible while the user scrolls,
        // providing context when the header is off-screen.
        ColumnLayout {
            visible: !!(editShotData.profileName)
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Accessible.role: Accessible.StaticText
            Accessible.name: (editShotData.profileName || "") + (editShotData.dateTime ? ", " + editShotData.dateTime : "")
            Accessible.focusable: true

            Text {
                text: editShotData.profileName || ""
                font: Theme.labelFont
                color: Theme.textColor
                elide: Text.ElideRight
                Layout.maximumWidth: postShotReviewPage.width * 0.3
                Accessible.ignored: true
            }
            Text {
                text: editShotData.dateTime || ""
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                elide: Text.ElideRight
                Layout.maximumWidth: postShotReviewPage.width * 0.3
                Accessible.ignored: true
            }
        }

        // Undo button — edits autosave on every commit point; this reverts the
        // most recent committed change (repeatable). Visible only when there is
        // something on the undo stack.
        AccessibleButton {
            id: undoButton
            visible: postShotReviewPage._undoDepth > 0
            Layout.preferredWidth: undoButtonContent.implicitWidth + Theme.scaled(40)
            Layout.preferredHeight: Theme.scaled(44)
            text: TranslationManager.translate("postshotreview.button.undo", "Undo")
            accessibleName: TranslationManager.translate("postshotreview.accessible.undo", "Undo last change")
            onClicked: postShotReviewPage.undoLastChange()

            contentItem: Row {
                id: undoButtonContent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/history.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }

                Tr {
                    key: "postshotreview.button.undo"
                    fallback: "Undo"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }
        }

        // Upload / Re-Upload to Visualizer button
        Rectangle {
            id: uploadButton
            visible: editShotData.durationSec > 0 && !MainController.visualizer.uploading
            Layout.preferredWidth: uploadButtonContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: uploadArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: _visualizerId
                ? TranslationManager.translate("postshotreview.button.reupload", "Re-Upload to Visualizer")
                : TranslationManager.translate("postshotreview.button.upload", "Upload to Visualizer")
            Accessible.focusable: true
            Accessible.onPressAction: uploadArea.clicked(null)

            Row {
                id: uploadButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/emoji/2601.svg"  // Cloud icon
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }

                Tr {
                    key: _visualizerId
                         ? "postshotreview.button.reupload"
                         : "postshotreview.button.upload"
                    fallback: _visualizerId
                              ? "Re-Upload to Visualizer"
                              : "Upload to Visualizer"
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: uploadArea
                anchors.fill: parent
                onClicked: {
                    // Flush any pending edit before uploading
                    autosave()
                    // Clear the pending flag before dispatching — auto-update on destruction
                    // must not fire a second request while this one is in flight. On failure
                    // pendingVisualizerUpdate remains false (it was cleared here), so
                    // auto-update on close will not retry; the user must tap the button again.
                    pendingVisualizerUpdate = false

                    uploadError = ""
                    uploadSkipReason = ""
                    if (_visualizerId) {
                        // Re-upload: PATCH metadata from current edit fields. Reuse
                        // buildVisualizerOverrides() so the manual and auto-update paths
                        // stay in sync as fields evolve.
                        var patchOverrides = buildVisualizerOverrides()
                        _patchInFlight = true
                        // editShotData may be a plain-JS clone (badges/save) or the
                        // raw gadget; the C++ method takes QVariant and coerces it,
                        // so id/duration/frame arrays survive either way. Edited
                        // fields ride in patchOverrides.
                        MainController.visualizer.updateShotOnVisualizerWithOverrides(
                            _visualizerId, editShotData, patchOverrides)
                    } else {
                        // First upload: pass editShotData (a clone after badges/save,
                        // or the raw gadget if untouched) plus current edit-field
                        // overrides. The C++ method takes QVariant and coerces via
                        // ShotProjection::coerce(), so id, durationSec, and frame
                        // arrays survive isValid().
                        var uploadOverrides = buildVisualizerOverrides()
                        _firstUploadInFlight = true
                        MainController.visualizer.uploadShotFromHistoryWithOverrides(
                            editShotData, uploadOverrides)
                    }
                }
            }
        }

        // Uploading/Updating indicator
        Tr {
            visible: MainController.visualizer.uploading
            key: _visualizerId
                 ? "postshotreview.status.updating"
                 : "postshotreview.status.uploading"
            fallback: _visualizerId ? "Updating..." : "Uploading..."
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }

        Text {
            visible: uploadError.length > 0 && !MainController.visualizer.uploading
            text: TranslationManager.translate("postshotreview.upload.failed", "Upload failed") + ": " + uploadError
            color: Theme.errorColor
            font: Theme.labelFont
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text {
            visible: uploadSkipReason.length > 0 && !MainController.visualizer.uploading
            text: TranslationManager.translate("postshotreview.upload.skipped", "Upload skipped") + ": " + uploadSkipReason
            color: Theme.textSecondaryColor
            font: Theme.labelFont
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // AI Advice button - visible when AI is configured and we have shot data
        Rectangle {
            id: aiAdviceButton
            visible: MainController.aiManager && MainController.aiManager.isConfigured && editShotData.durationSec > 0
            Layout.preferredWidth: aiAdviceContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: MainController.aiManager && MainController.aiManager.isConfigured
                   ? Theme.primaryColor : Theme.surfaceColor
            opacity: MainController.aiManager && MainController.aiManager.isAnalyzing ? 0.6 : 1.0

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("postshotreview.accessible.getaiadvice", "Get AI Advice")
            Accessible.focusable: true
            Accessible.onPressAction: aiAdviceArea.clicked(null)

            Row {
                id: aiAdviceContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textColor
                    }
                }

                Tr {
                    key: MainController.aiManager && MainController.aiManager.isAnalyzing
                          ? "postshotreview.button.analyzing" : "postshotreview.button.aiadvice"
                    fallback: MainController.aiManager && MainController.aiManager.isAnalyzing
                          ? "Analyzing..." : "AI Advice"
                    color: MainController.aiManager && MainController.aiManager.isConfigured
                           ? Theme.primaryContrastColor : Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

                MouseArea {
                id: aiAdviceArea
                anchors.fill: parent
                enabled: MainController.aiManager && MainController.aiManager.isConfigured && !MainController.aiManager.isAnalyzing
                onClicked: {
                    conversationOverlay.openWithShot(editShotData, editBeanBrand, editBeanType, editShotData.profileName, editShotId)
                }
            }
        }

        // Discuss button - opens external AI app
        Rectangle {
            id: discussButton
            readonly property bool isClaudeDesktopReady:
                Settings.network.discussShotApp !== Settings.network.discussAppClaudeDesktop
                || Settings.network.claudeRcSessionUrl.length > 0
            visible: editShotData.durationSec > 0 && Settings.network.discussShotApp !== Settings.network.discussAppNone
            enabled: isClaudeDesktopReady
            opacity: enabled ? 1.0 : 0.5
            Layout.preferredWidth: discussContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: discussArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("postshotreview.accessible.discuss", "Discuss shot with external AI app")
            Accessible.focusable: true
            Accessible.onPressAction: discussArea.clicked(null)

            Row {
                id: discussContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.primaryContrastColor
                    }
                }

                Tr {
                    key: "postshotreview.button.discuss"
                    fallback: "Discuss"
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: discussArea
                anchors.fill: parent
                enabled: discussButton.isClaudeDesktopReady
                onClicked: {
                    // Copy shot summary to clipboard if MCP is not connected
                    if (!Settings.mcp.mcpEnabled && MainController.aiManager) {
                        // Prose, not the JSON envelope — the user is pasting this into
                        // an external AI tool. See #1042 / ShotDetailPage clipboard
                        // path for rationale.
                        var summary = MainController.aiManager.buildShotAnalysisProseForShot(editShotData)
                        if (summary.length > 0) MainController.copyToClipboard(summary)
                    }
                    // Open configured AI app
                    var url = Settings.network.discussShotUrl()
                    if (url.length > 0) Settings.network.openDiscussUrl(url)
                }
            }
        }

        // Email Prompt button - fallback for users without API keys
        Rectangle {
            id: emailPromptButton
            visible: MainController.aiManager && !MainController.aiManager.isConfigured && editShotData.durationSec > 0
            Layout.preferredWidth: emailPromptContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: Theme.surfaceColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("postshotreview.accessible.emailprompt", "Email AI prompt to yourself")
            Accessible.focusable: true
            Accessible.onPressAction: emailPromptArea.clicked(null)

            Row {
                id: emailPromptContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                    opacity: 0.6
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }

                Tr {
                    key: "postshotreview.button.emailprompt"
                    fallback: "Email Prompt"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: emailPromptArea
                anchors.fill: parent
                onClicked: {
                    // Prose, not the JSON envelope — the email body lands in the
                    // user's mail client; the JSON shape double-shipped structured
                    // fields (#1042).
                    var prompt = MainController.aiManager.buildShotAnalysisProseForShot(editShotData)
                    // Open mailto: with prompt in body
                    Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent("Espresso Shot Analysis") +
                                        "&body=" + encodeURIComponent(prompt))
                }
            }
        }

    }

    // === Inline Components ===

    component LabeledComboBox: Item {
        property string label: ""
        property var model: []
        property string currentValue: ""
        signal valueChanged(string value)

        implicitHeight: comboLabel.height + 48 + 2

        Text {
            id: comboLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(11)
            Accessible.ignored: true
        }

        StyledComboBox {
            id: combo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: comboLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            height: Theme.scaled(48)
            model: parent.model
            currentIndex: Math.max(0, model.indexOf(parent.currentValue))
            font.pixelSize: Theme.scaled(14)
            accessibleLabel: parent.label
            emptyItemText: TranslationManager.translate("postshotreview.option.none", "(None)")

            Accessible.description: currentIndex > 0 ? currentText : TranslationManager.translate("postshotreview.accessible.notset", "Not set")

            onActiveFocusChanged: {
                if (activeFocus && AccessibilityManager.enabled) {
                    let value = currentIndex > 0 ? currentText : TranslationManager.translate("postshotreview.accessible.notset", "Not set")
                    AccessibilityManager.announce(parent.label + ". " + value)
                }
            }

            background: Rectangle {
                color: Theme.backgroundColor
                radius: Theme.scaled(4)
                border.color: combo.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                border.width: 1
            }

            contentItem: Text {
                text: combo.currentIndex === 0 && combo.model[0] === "" ? parent.parent.label : combo.displayText
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.scaled(12)
            }

            indicator: Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(12)
                anchors.verticalCenter: parent.verticalCenter
                text: "▼"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(10)
            }

            onActivated: function(index) { parent.valueChanged(currentText) }
        }
    }

    // Profile AI knowledge base dialog
    // Shared KB popup (qml/components/ProfileKnowledgeDialog.qml).
    ProfileKnowledgeDialog {
        id: shotKnowledgeDialog
    }

    ConversationOverlay {
        id: conversationOverlay
        anchors.fill: parent
        overlayTitle: TranslationManager.translate("postshotreview.conversation.title", "Dialing Conversation")
    }

}
