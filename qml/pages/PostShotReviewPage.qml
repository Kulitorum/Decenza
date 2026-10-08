pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../components/DateUtils.js" as DateUtils
import "../components/layout/ShotPlanConfig.js" as ShotPlanConfig

T.Page {
    id: postShotReviewPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("postshotreview.title", "Shot Review")

    objectName: "postShotReviewPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    Component.onCompleted: {
        if (Settings.graph.advancedMode)
            _refreshBaristaHistory()
        if (editShotId > 0) {
            loadShotForEditing()
            // Edits here autosave field by field; they reach the upload
            // destinations once, when the page closes.
            _heldShotId = editShotId
            MainController.shotUploads.holdUpdates(editShotId)
        }
    }
    property int _heldShotId: 0

    // Barista names from history, read only when the advanced-only field is shown: at page
    // open in advanced mode, else when advanced mode is switched on. The query cost ~32 ms of
    // this page's open on a Galaxy Tab A9+ (#1976), and the page opens after every shot.
    // Refreshed on a write once read, never per keystroke: the suggestions binding below reads
    // `editBarista`, which onTextEdited rewrites on every character, and calling the getter
    // there ran a SELECT DISTINCT each time (1.1 ms median, 35 ms worst on a real database).
    property var _baristaHistory: []
    property bool _baristaHistoryLoaded: false
    function _refreshBaristaHistory() {
        _baristaHistoryLoaded = true
        _baristaHistory = MainController.shotHistory
            ? MainController.shotHistory.getDistinctBaristas().slice() : []
    }
    StackView.onActivated: {
        // Hunt for the refractometer while this page is open: activation kicks
        // an immediate scan, and BLEManager keeps scans back-to-back until the
        // refractometer connects (C++ guards handle not-configured/connected).
        // After the page's first frame, not before it: starting the scan held up
        // the page's open by ~27 ms on a Galaxy Tab A9+ (#1976).
        if (Settings.savedRefractometerAddress !== "")
            _huntAfterFrame = true
    }
    property bool _huntAfterFrame: false
    Connections {
        target: postShotReviewPage._huntAfterFrame ? postShotReviewPage.Window.window : null
        function onFrameSwapped() {
            postShotReviewPage._huntAfterFrame = false
            BLEManager.setRefractometerHunt(true)
        }
    }

    // Disconnect refractometer when the page is torn down. NOTE: we do NOT
    // autosave() here — calling Keyboard.commit() / a DB write / singleton
    // writes during QML object destruction is unsafe (events delivered to a
    // being-destroyed TextEdit, nulled context). StackView.onDeactivating below
    // already flushes on every normal navigation exit, and handleBack() flushes
    // on the explicit back path, so the destruction flush was redundant.
    Component.onDestruction: {
        // Safety net: onDeactivating already ends the hunt on every normal
        // navigation exit, but a page destroyed without deactivating (app
        // teardown) must not leave continuous scanning armed.
        BLEManager.setRefractometerHunt(false)
        if (Refractometer && Refractometer.connected) {
            Refractometer.disconnectFromDevice()
        }
        // Only queues network requests: no DB write or Keyboard.commit(), which are
        // unsafe during destruction. A failure is logged by the uploader.
        if (_heldShotId > 0) MainController.shotUploads.releaseUpdates(_heldShotId)
    }

    // Flush whenever the page loses the foreground within the stack (back, a
    // child page pushed on top) so a deferred/in-progress edit is persisted.
    // Note this fires only on stack transitions — NOT on app backgrounding,
    // where the suspended event loop is what stops scan activity.
    // Also end the refractometer hunt — continuous scanning is scoped to this
    // page being the active page.
    StackView.onDeactivating: {
        // The R2 is only used to capture TDS/EY on this page. Leaving it ends the
        // hunt AND disconnects, so it isn't holding a BLE link (contending with
        // the DE1/scale) while we're off the page. The hunt reconnects on return.
        _huntAfterFrame = false
        BLEManager.setRefractometerHunt(false)
        if (Refractometer && Refractometer.connected) {
            Refractometer.disconnectFromDevice()
        }
        milkWeighButton.cancel()
        autosave()
    }

    function handleBack() {
        // Every committed edit is already persisted; just flush a possible
        // in-progress text field and leave — no confirmation prompt needed.
        autosave()
        AppShell.backRequested()
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
    readonly property bool hasCurves: !!(editShotData.pressure && editShotData.pressure.length > 0)

    // How the shot went and what changed since the previous shot on its profile:
    // ShotHistoryStorage::requestShotOutcome, re-read after each saved edit.
    property var shotOutcome: ({})

    // The list the page was opened from, newest first; empty when opened for one shot.
    property var shotIds: []
    readonly property int currentIndex: shotIds.indexOf(editShotId)
    readonly property bool canGoNewer: currentIndex > 0
    readonly property bool canGoOlder: currentIndex >= 0 && currentIndex < shotIds.length - 1
    readonly property string positionText: TranslationManager.translate("shotdetail.accessible.position", "Shot %1 of %2")
        .arg(currentIndex + 1).arg(shotIds.length)
    property int _swipeDirection: 0
    property bool _stepping: false

    // Step to another shot in the list: the edit in hand is saved to the shot it was
    // made on, and undo, the upload hold and auto-close end with it.
    function stepTo(index) {
        if (index < 0 || index >= shotIds.length || index === currentIndex) return
        autosave()
        reviewGraph.dismissInspect()
        milkWeighButton.cancel()
        autoClose = false
        _swipeDirection = index > currentIndex ? 1 : -1
        stepAnimation.targetId = shotIds[index]
        stepAnimation.restart()
    }
    function _loadSteppedShot(shotId) {
        if (_heldShotId > 0) MainController.shotUploads.releaseUpdates(_heldShotId)
        _undoStack = []
        _undoDepth = 0
        _lastEditKey = ""
        _committedState = ({})
        _editLoaded = false
        _saveFailed = false
        r2CommitPending = false
        pendingVisualizerUpdate = false
        pendingDecentUpdate = false
        _decentUploaded = false
        _decentState = ({})
        _visualizerId = ""
        // onShotReady keeps a non-zero TDS as a live reading for the shot it loads.
        editDrinkTds = 0
        editDrinkEy = 0
        shotOutcome = ({})
        _stepping = true
        editShotId = shotId
        _heldShotId = shotId
        MainController.shotUploads.holdUpdates(shotId)
        loadShotForEditing()
    }

    SequentialAnimation {
        id: stepAnimation
        property int targetId: 0
        ParallelAnimation {
            NumberAnimation { target: pageColumn; property: "opacity"; to: 0; duration: 140; easing.type: Easing.InQuad }
            NumberAnimation { target: contentSlide; property: "x"; to: postShotReviewPage._swipeDirection * -Theme.scaled(50); duration: 140; easing.type: Easing.InQuad }
        }
        ScriptAction {
            script: {
                contentSlide.x = postShotReviewPage._swipeDirection * Theme.scaled(50)
                postShotReviewPage._loadSteppedShot(stepAnimation.targetId)
            }
        }
    }
    ParallelAnimation {
        id: enterAnimation
        NumberAnimation { target: pageColumn; property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutQuad }
        NumberAnimation { target: contentSlide; property: "x"; to: 0; duration: 180; easing.type: Easing.OutQuad }
    }

    function inspectGraphAt(x, y) {
        if (x > reviewGraph.plotArea.x + reviewGraph.plotArea.width) reviewGraph.toggleRightAxis()
        else reviewGraph.inspectAtPosition(x, y)
    }

    // Field selection + order for the snapshot line, taken from the user's first
    // idle-page Shot Plan widget so this line shows the fields they configured.
    // Reactive on layout edits. Only the item list is used — the snapshot is
    // always a plain fragment line (sentence/stacked toggles are ignored).
    readonly property var _shotPlanItemOrder:
        ShotPlanConfig.itemOrderFromLayoutJson(Settings.network.layoutConfiguration)

    // The shot-time profile's defaults (from the frozen profileJson snapshot),
    // for the override highlights — "was the recorded value a deviation from
    // the profile it ran?" 0 when the snapshot lacks the field, which disables
    // the highlight (a frozen shot must never borrow the live dial's override
    // state). The temperature comparison is essential, not cosmetic: the save
    // path records temperatureOverrideC for EVERY shot (user override or
    // profile default), so t > 0 alone does not mean "overridden".
    readonly property var _shotProfileDefaults: {
        if (!editShotData.profileJson) return ({ yield: 0, temp: 0 })
        try {
            let p = JSON.parse(editShotData.profileJson)
            return { yield: p.target_weight || 0, temp: p.espresso_temperature || 0 }
        } catch (e) { return ({ yield: 0, temp: 0 }) }
    }
    readonly property real _shotProfileYield: _shotProfileDefaults.yield
    readonly property bool _shotTempOverridden:
        (editShotData.temperatureOverrideC || 0) > 0 && _shotProfileDefaults.temp > 0
        && Math.abs(editShotData.temperatureOverrideC - _shotProfileDefaults.temp) > 0.1

    // Recipe identity for the recipe card, live-resolved by editShotData.recipeId
    // (follows renames). Grind/rpm on that card comes from this page's live edit
    // state, never this map's pin.
    RecipeResolver {
        id: recipeResolver
        sourceRecipeId: postShotReviewPage.editShotData.recipeId || -1
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
        // The dial-in card states the PLAN, the shot's recorded target; the
        // achieved weight has its own editable field above. Falls back to the
        // achieved weight only when no target was recorded (volume/timer profiles).
        var dose = editDoseWeight || 0
        var yieldG = (editShotData.targetWeightG || 0) > 0 ? editShotData.targetWeightG
                                                           : (editDrinkWeight || 0)
        if (dose > 0 && yieldG > 0) return dose.toFixed(1) + "g → " + yieldG.toFixed(1) + "g"
        if (dose > 0) return dose.toFixed(1) + "g"
        return ""
    }
    // Read-only dial-in for the recipe card: dose → yield · grind · rpm (rpm
    // only for rpm-capable grinders). Grind itself is edited in the Dial-in row above.
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
            let s = JSON.parse(editShotData.steamJson)
            if (!s.hasMilk) return ""
            let parts = []
            if (s.pitcherName) parts.push(s.pitcherName)
            if ((s.milkWeightG || 0) > 0)
                parts.push(TranslationManager.translate("recipes.list.milkWeight", "%1g milk").arg(Math.round(s.milkWeightG)))
            return parts.join(" · ")
        } catch (e) { WebDebugLogger.warn("Steam", "PostShotReviewPage", ["bad steamJson on shot", editShotData.id, e].map(String).join(" ")); return "" }
    }
    function recipeWaterText() {
        if (!editShotData.hotWaterJson) return ""
        try {
            let w = JSON.parse(editShotData.hotWaterJson)
            if (!w.hasWater) return ""
            let parts = []
            if (w.vesselName) parts.push(w.vesselName)
            if ((w.volume || 0) > 0) parts.push(w.volume + (w.mode === "volume" ? "ml" : "g"))
            if ((w.temperatureC || 0) > 0) parts.push(Math.round(Theme.cToDisplay(w.temperatureC)) + Theme.tempUnitSuffix())
            return parts.join(" · ")
        } catch (e) { WebDebugLogger.warn("Shot", "PostShotReviewPage", ["bad hotWaterJson on shot", editShotData.id, e].map(String).join(" ")); return "" }
    }

    // RecipeField (labeled component row) is a shared component in
    // qml/components/RecipeField.qml.

    Tr { id: trRowProfile; key: "recipes.wizard.rowProfile"; fallback: "Profile"; visible: false }
    Tr { id: trRowBeans; key: "shotdetail.beaninfo"; fallback: "Beans"; visible: false }
    Tr { id: trRowDialIn; key: "shotdetail.recipe.dialIn"; fallback: "Dial-in"; visible: false }
    Tr { id: trRowSteam; key: "recipes.wizard.rowSteam"; fallback: "Steam / milk"; visible: false }
    Tr { id: trRowWater; key: "recipes.wizard.rowHotWater"; fallback: "Hot water"; visible: false }
    Tr { id: trRowEquipment; key: "shotdetail.equipment"; fallback: "Equipment"; visible: false }

    // Multi-reading refractometer runs. avgTotal > 0 means a run is in flight; both
    // reset when it finishes so the button returns to its resting label.
    //
    // These only ever populate for a run the DEVICE decided to make multi-reading:
    // a loop test on an unsettled prism, or an averaged run if the R2's own test
    // count was raised outside Decenza. Nothing here requests one.
    property int avgDone: 0
    property int avgTotal: 0
    // A reading has arrived that has not been committed yet. The driver delivers a
    // value per reading during a settling or averaged run, so committing on arrival
    // wrote the shot record once per reading — each one superseded by the next.
    property bool r2CommitPending: false

    Connections {
        // Deliberately NOT gated on BLEManager.refractometerConnected, unlike the
        // tdsChanged block below. That block must re-attach when the R2 connects after
        // the page opens. This one must keep firing while it DISconnects: the driver
        // emits connectedChanged before measuringChanged, and connectedChanged drives
        // refractometerConnectedChanged synchronously — so the extra term would
        // re-evaluate this target to null and the measuringChanged that ends the run
        // would be delivered to nothing, stranding the progress label and the pending
        // commit. The device object itself stays non-null across a disconnect.
        target: Refractometer

        function onAverageProgress(completed, total) {
            postShotReviewPage.avgDone = completed
            postShotReviewPage.avgTotal = total
        }
        // The end of a run is the commit point. measurementComplete fires exactly once
        // per run — the driver separates delivering a value from declaring the run
        // over — so this is where a reading becomes a saved reading.
        function onMeasurementComplete() {
            postShotReviewPage.avgDone = 0
            postShotReviewPage.avgTotal = 0
            postShotReviewPage.commitPendingR2Reading()
        }
        function onMeasuringChanged() {
            if (Refractometer && !Refractometer.measuring) {
                postShotReviewPage.avgDone = 0
                postShotReviewPage.avgTotal = 0
                // Covers a run that ends without a terminal status — the watchdog
                // clears the measuring state but emits no measurementComplete, and a
                // reading that arrived is still the user's reading.
                postShotReviewPage.commitPendingR2Reading()
            }
        }
    }

    function commitPendingR2Reading() {
        if (!r2CommitPending) return
        r2CommitPending = false
        autosave("r2", true)
    }

    property bool autoClose: true  // false when user opens manually (no auto-dismiss)
    property string uploadError: ""
    // Reason a policy-based skip rejected the upload (maintenance profile,
    // too-short shot). Surfaced as informational text — not red error styling —
    // because the system intentionally chose not to upload.
    property string uploadSkipReason: ""
    property bool pendingVisualizerUpdate: false  // set when a metadata edit has been saved locally but not yet PATCHed to visualizer
    property bool pendingDecentUpdate: false      // the same, for the Decent account
    // Whether this shot is in the Decent account (decentUploadStateReady), and the
    // whole state for the uploads card.
    property bool _decentUploaded: false
    property var _decentState: ({})
    // visualizerId from DB, captured before any Object.assign strips Q_GADGET fields. Captured in onShotReady
    // and refreshed in onUploadSucceededForShot (a fresh upload completed for THIS shot — from
    // this page or from the shot-completion background uploader) so the "Re-Upload" button label
    // and the Upload button's in-sync state see a stable value after saveEditedShot replaces
    // editShotData with a plain JS object.
    property string _visualizerId: ""

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
            AppShell.backRequested()
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
        onTapped: postShotReviewPage.resetAutoCloseTimer()
    }
    // One plot height for every way the page opens; the review page's old key
    // seeds it once.
    property real graphHeight: Settings.value("shotPage/graphHeight",
                                              Settings.value("postShotReview/graphHeight", Theme.scaled(220)))

    // Load shot data for editing (async)
    function loadShotForEditing() {
        if (editShotId <= 0) return
        MainController.shotHistory.requestShot(editShotId)
    }

    // Handle async shot data
    Connections {
        target: MainController.shotHistory
        function onDecentUploadStateReady(shotId, state) {
            if (shotId !== postShotReviewPage.editShotId) return
            postShotReviewPage._decentUploaded = !!state.uploaded
            postShotReviewPage._decentState = state
            // Decent kept an earlier copy (NotReplaced): the edit still has to reach it.
            if (state.replacePending) postShotReviewPage.pendingDecentUpdate = true
        }
        function onDecentUploadStateUpdated(shotId, success) {
            if (shotId === postShotReviewPage.editShotId) MainController.shotHistory.requestDecentUploadState(shotId)
        }
        function onShotReady(shotId, shot) {
            if (shotId !== postShotReviewPage.editShotId) return
            // Ignore a RE-delivery of the shot we already hold. requestShot is a shared
            // async API and this page is not its only caller — the last-shot background
            // re-reads the newest shot whenever one is saved, which is this shot, at the
            // moment this page opens. Re-running the block below would repopulate every
            // edit field from the database and reset the upload status, which is the same
            // clobber-an-in-progress-edit hazard onVisualizerInfoUpdated documents below.
            if (postShotReviewPage.editShotData && postShotReviewPage.editShotData.id === shotId)
                return
            postShotReviewPage.editShotData = shot
            postShotReviewPage._visualizerId = postShotReviewPage.editShotData.visualizerId || ""
            MainController.shotHistory.requestDecentUploadState(shotId)
            MainController.shotHistory.requestShotOutcome(shotId)
            if (postShotReviewPage._stepping) {
                postShotReviewPage._stepping = false
                Qt.callLater(function() {
                    flickable.returnToBounds()
                    enterAnimation.start()
                })
            }
            // Reset upload status text when loading a new shot so stale
            // error/skip messages from a previous shot don't carry over.
            postShotReviewPage.uploadError = ""
            postShotReviewPage.uploadSkipReason = ""
            if (postShotReviewPage.editShotData.id) {
                // Populate editing fields
                postShotReviewPage.editBeanBrand = postShotReviewPage.editShotData.beanBrand || ""
                postShotReviewPage.editBeanType = postShotReviewPage.editShotData.beanType || ""
                postShotReviewPage.editRoastDate = DateUtils.normalizeDateString(postShotReviewPage.editShotData.roastDate || "")
                postShotReviewPage.editRoastLevel = postShotReviewPage.editShotData.roastLevel || ""
                postShotReviewPage.editGrinderBrand = postShotReviewPage.editShotData.grinderBrand || ""
                postShotReviewPage.editGrinderModel = postShotReviewPage.editShotData.grinderModel || ""
                postShotReviewPage.editGrinderBurrs = postShotReviewPage.editShotData.grinderBurrs || ""
                postShotReviewPage.editEquipmentId = postShotReviewPage.editShotData.equipmentId || -1
                postShotReviewPage.editEquipmentName = postShotReviewPage.editShotData.equipmentName || ""
                // Basket + puck prep are display-only here (owned by the package,
                // re-pointed via the picker) but shown in the equipment card.
                postShotReviewPage.editBasketBrand = postShotReviewPage.editShotData.basketBrand || ""
                postShotReviewPage.editBasketModel = postShotReviewPage.editShotData.basketModel || ""
                postShotReviewPage.editPuckPrep = postShotReviewPage.editShotData.puckPrep || ""
                postShotReviewPage.editGrinderSetting = postShotReviewPage.editShotData.grinderSetting || ""
                postShotReviewPage.editRpm = postShotReviewPage.editShotData.rpm || 0
                postShotReviewPage.editBarista = postShotReviewPage.editShotData.barista || ""
                // Fall back to last-used DYE dose when the shot has no stored dose,
                // so EY can be computed immediately when TDS arrives.
                postShotReviewPage.editDoseWeight = (postShotReviewPage.editShotData.doseWeightG > 0) ? postShotReviewPage.editShotData.doseWeightG : Settings.dye.dyeBeanWeight
                postShotReviewPage.editDrinkWeight = postShotReviewPage.editShotData.finalWeightG ?? 0
                // Preserve any live R2 reading that arrived before the async DB load;
                // only take the DB value when no measurement has been received yet.
                if (postShotReviewPage.editDrinkTds === 0) {
                    postShotReviewPage.editDrinkTds = postShotReviewPage.editShotData.drinkTdsPct ?? 0
                    postShotReviewPage.editDrinkEy = postShotReviewPage.editShotData.drinkEyPct ?? 0
                }
                postShotReviewPage.editEnjoyment = postShotReviewPage.editShotData.enjoyment0to100 ?? 0
                postShotReviewPage.editTasteBalance = postShotReviewPage.editShotData.tasteBalance || ""
                postShotReviewPage.editTasteBody = postShotReviewPage.editShotData.tasteBody || ""
                postShotReviewPage.editNotes = postShotReviewPage.editShotData.espressoNotes || ""
                postShotReviewPage.editBeverageType = postShotReviewPage.editShotData.beverageType || "espresso"
                postShotReviewPage.editBeanBaseJson = postShotReviewPage.editShotData.beanBaseJson || ""
                // A canonical pick persists identity immediately; if the page
                // closed (or the network blipped) before the attribute payload
                // arrived, the shot is stuck with a bare {id, roaster, name}
                // blob — fields don't lock, the advisor sees nothing. Complete
                // it: re-issue the best-effort fetch; the onCanonicalDetails
                // merge below finishes the job.
                if (postShotReviewPage.beanBaseLinked && postShotReviewPage.activeBeanBase.source === "visualizer"
                    && postShotReviewPage.activeBeanBase.origin === undefined
                    && postShotReviewPage.activeBeanBase.degree === undefined)
                    MainController.beanbase.fetchCanonicalDetails(postShotReviewPage.activeBeanBase)
                // Recompute EY now that dose/weight are loaded (covers the case where TDS
                // arrived via R2 before the shot data was ready, or where the DB already
                // has a non-zero TDS from a previous session).
                postShotReviewPage.calculateEy()
                // Establish the autosave baseline now that every edit field
                // mirrors the loaded record. _editLoaded gates autosave so a
                // pre-load flush can't write empty metadata over the shot
                // (hasUnsavedChanges is transiently true before this point —
                // empty baseline vs. dose defaulted from Settings).
                postShotReviewPage._committedState = postShotReviewPage.captureEditState()
                postShotReviewPage._editLoaded = true
                // A control the user touched on the previous shot still shows that
                // shot's value until its binding is restored.
                postShotReviewPage.rebindInputs()
                // Show what Visualizer holds now, not as of the last sync pass.
                if (postShotReviewPage._visualizerId !== "")
                    MainController.visualizerSync.refreshShot(shotId)
                // Quality badges already arrived recomputed in `shot` via
                // loadShotRecordStatic, which also persists drift to the DB
                // and emits shotBadgesUpdated when it does. onShotBadgesUpdated
                // below catches the persist event.
            }
        }
        function onShotBadgesUpdated(shotId, channeling, grindIssue, skipFirstFrame, pourTruncated) {
            if (shotId !== postShotReviewPage.editShotId) return
            var updated = postShotReviewPage.clonePersistedShot(postShotReviewPage.editShotData)
            updated.channelingDetected = channeling
            updated.grindIssueDetected = grindIssue
            updated.skipFirstFrameDetected = skipFirstFrame
            updated.pourTruncatedDetected = pourTruncated
            postShotReviewPage.editShotData = updated
        }
        function onShotMetadataUpdated(shotId, success) {
            if (shotId !== postShotReviewPage.editShotId) return
            // Success needs no reload: saveEditedShot() already advanced the
            // in-memory baseline optimistically, and a reload would clobber an
            // in-progress edit.
            if (success) {
                postShotReviewPage._saveFailed = false
                MainController.shotHistory.requestShotOutcome(shotId)
            } else {
                WebDebugLogger.warn("Shot", "PostShotReviewPage", ["Failed to save metadata for shot", shotId].map(String).join(" "))
                postShotReviewPage._saveFailed = true
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(TranslationManager.translate(
                        "postshotreview.saveFailed", "Saving shot changes failed — will retry"))
            }
        }
        function onShotOutcomeReady(shotId, outcome) {
            if (shotId === postShotReviewPage.editShotId) postShotReviewPage.shotOutcome = outcome
        }
        function onShotDeleted(shotId) {
            if (shotId !== postShotReviewPage.editShotId) return
            // Nothing left to save to.
            postShotReviewPage._editLoaded = false
            AppShell.backRequested()
        }
        function onShotPulledFromVisualizer(shotId, previous, written) {
            if (shotId === postShotReviewPage.editShotId && postShotReviewPage._editLoaded)
                postShotReviewPage.mergePulledChanges(previous, written)
        }
        function onHistoryDataChanged() {
            if (postShotReviewPage._baristaHistoryLoaded)
                postShotReviewPage._refreshBaristaHistory()
        }
        function onVisualizerInfoUpdated(shotId, success) {
            if (shotId !== postShotReviewPage.editShotId) return
            // No reload: a full loadShotForEditing() here would re-run
            // onShotReady, clobber an in-progress edit, and orphan the undo
            // stack (same race the metadata path avoids). The visualizer id is
            // refreshed in place by onUploadSucceededForShot below.
            if (!success)
                WebDebugLogger.warn("Shot", "PostShotReviewPage", ["Failed to save visualizer info for shot", shotId].map(String).join(" "))
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
    // Structured taste axes (add-ai-taste-intake): "" = unset.
    property string editTasteBalance: ""
    property string editTasteBody: ""

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
        target: BLEManager.refractometerConnected ? Refractometer : null
        enabled: postShotReviewPage.visible
        function onTdsChanged(tds) {
            if (!postShotReviewPage.isEditMode) return
            if (tds < postShotReviewPage.kMinimumPlausibleTds) {
                WebDebugLogger.debug("Refractometer", "PostShotReviewPage", ["R2 tds", tds.toFixed(2),
                    "dropped: below threshold", postShotReviewPage.kMinimumPlausibleTds,
                    "shotId=", postShotReviewPage.editShotId,
                    "wasMeasuring=", Refractometer.measuring].map(String).join(" "))
                return
            }
            if (tds > postShotReviewPage.kMaximumPlausibleTds) {
                WebDebugLogger.debug("Refractometer", "PostShotReviewPage", ["R2 tds", tds.toFixed(2),
                    "dropped: above threshold", postShotReviewPage.kMaximumPlausibleTds,
                    "shotId=", postShotReviewPage.editShotId,
                    "wasMeasuring=", Refractometer.measuring].map(String).join(" "))
                return
            }
            postShotReviewPage.editDrinkTds = tds
            postShotReviewPage.calculateEy()
            // An R2 measurement is a committed value just like a user-entered one, and
            // without persisting it the value relied on a later manual Save and was
            // frequently lost on navigate-away. But it is committed when the RUN ends,
            // not when a value arrives: a settling or averaged run delivers a reading
            // every few seconds, and committing each one wrote the shot record five
            // times in a measured 16-second loop, every write but the last superseded.
            // Deferred unconditionally. An earlier version committed immediately when
            // `measuring` was false — but `measuring` is a REQUEST-side flag, set only
            // by requestMeasurement()/requestAveragedMeasurement(), so it is false
            // throughout a device-initiated run. Auto Test and the physical button are
            // exactly that, and an Auto Test loop is where the five-writes-in-16s
            // measurement came from, so the guard exempted the case it was written for.
            //
            // Nothing is lost by always deferring: finishMeasurement() emits
            // measurementComplete and measuringChanged on every terminal path
            // regardless of who started the run.
            postShotReviewPage.r2CommitPending = true
        }
    }

    // Auto-calculate EY from TDS, dose weight, and beverage weight
    // Formula: EY(%) = (beverageWeight × TDS%) / doseWeight
    function calculateEy() {
        if (editDoseWeight > 0 && editDrinkWeight > 0 && editDrinkTds > 0) {
            let ey = (editDrinkWeight * editDrinkTds) / editDoseWeight
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
        editTasteBalance !== (editShotData.tasteBalance || "") ||
        editTasteBody !== (editShotData.tasteBody || "") ||
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
            tasteBalance: editTasteBalance, tasteBody: editTasteBody,
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
        editTasteBalance = s.tasteBalance !== undefined ? s.tasteBalance : ""
        editTasteBody = s.tasteBody !== undefined ? s.tasteBody : ""
        editNotes = s.notes; editBeverageType = s.beverageType
        editBeanBaseJson = s.beanBaseJson !== undefined ? s.beanBaseJson : ""
        rebindInputs()
    }

    // RatingInput (internal `root.value = …`), the dose/out ValueInputs (handlers do
    // `xInput.value = …`) and the TastePicker chips imperatively assign their own
    // value during interaction, which severs the `value: editX` binding. Re-establish
    // it (not a bare assignment, which would sever it permanently) after Undo and
    // after a step to another shot, so the controls show editX again and keep
    // tracking it. The TDS/EY handlers do not self-assign, so their bindings stay
    // live and must NOT be touched here: re-asserting them would sever the binding
    // and break later R2 / calculateEy() updates.
    function rebindInputs() {
        ratingInput.value = Qt.binding(function() { return editEnjoyment })
        doseInput.value = Qt.binding(function() { return editDoseWeight })
        outInput.value = Qt.binding(function() { return editDrinkWeight })
        tastePicker.tasteBalance = Qt.binding(function() { return editTasteBalance })
        tastePicker.tasteBody = Qt.binding(function() { return editTasteBody })
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
        Keyboard.commit()
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
        Keyboard.commit()
        applyEditState(_undoStack.pop())
        _undoDepth = _undoStack.length
        // Force the next edit (even to the same control) to open a fresh
        // undo frame relative to this restored state.
        _lastEditKey = ""
        saveEditedShot()
        _committedState = captureEditState()
    }

    // A plain-JS copy of editShotData that carries every field the page still
    // reads after a save.
    //
    // QML cannot make one itself: `Object.assign({}, shot)` and spread copy own
    // properties, and a Q_GADGET's Q_PROPERTYs are accessors on the PROTOTYPE,
    // so enumeration silently drops durationSec, the sample arrays, dateTime,
    // profileName, debugLog, phases, badges and every other read-only field.
    // That broke the AI Advice / Discuss / Re-Upload button visibility
    // (predicate `editShotData.durationSec > 0`), the graph, the badges row,
    // the phase summary and the bottom-bar labels the moment the user made any
    // edit. (The `_visualizerId` cache in this file was added
    // in #1241 as targeted band-aids for the same root cause.)
    //
    // This used to be a hand-written whitelist naming ~60 fields — a second
    // declaration of ShotProjection::toVariantMap's body, in another language,
    // with nothing to keep the two in step. It fell behind twice: `recipeId`
    // (the first autosave emptied it, the recipe card vanished and the "no
    // recipe" prompts took its place) and then the entire equipment package,
    // which left every conversation opened from this page keyed to the
    // unpackaged pool while the same shot opened from Shot History keyed to its
    // real basket. Ask C++ for the field list instead.
    //
    // `src` is the gadget wrapper on the first call and the plain clone from
    // the previous call after that — plain objects hold their fields as own
    // properties, so there a shallow copy is the whole job.
    function clonePersistedShot(src) {
        return src.toVariantMap ? src.toVariantMap() : Object.assign({}, src)
    }

    // Save edited shot back to history
    // Sync sticky metadata back to Settings (bean/grinder info) for the
    // next shot — but ONLY when editing the most recent shot. The sticky
    // settings are "prep for the next pull"; editing a HISTORIC shot
    // (opened from Shot History) must not touch the bean
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
        // A re-point here becomes the active equipment (and the active bag's).
        // The dial is written only when it belongs to the active package, or the
        // shot has none. (editShotData still holds the previous equipment — it
        // is advanced after this call.)
        if (editEquipmentId > 0 && editEquipmentId !== (editShotData.equipmentId || -1))
            Settings.dye.activeEquipmentId = editEquipmentId
        if (editEquipmentId <= 0 || Settings.dye.activeEquipmentId === editEquipmentId) {
            Settings.dye.dyeGrinderSetting = editGrinderSetting
            if (editRpm > 0) Settings.dye.dyeGrinderRpm = editRpm
        }
        Settings.dye.dyeBarista = editBarista
        if (editDoseWeight > 0) Settings.dye.dyeBeanWeight = editDoseWeight
        if (editDrinkWeight > 0) Settings.dye.dyeDrinkWeight = editDrinkWeight
        // The link is sticky like the bean fields above: fixing the bean
        // on the shot you just pulled should carry to the next shot too.
        Settings.dye.dyeBeanBaseId = beanBaseLinked ? String(activeBeanBase.id) : ""
        Settings.dye.dyeBeanBaseData = beanBaseLinked ? editBeanBaseJson : ""
    }

    // The fields this page saves: the metadata key, the captureEditState()
    // key, and the shot-record property it mirrors. `pulled` marks the ones a
    // Visualizer pull can change underneath an open page.
    readonly property var _fieldSpecs: [
        { meta: "beanBrand",      state: "beanBrand",      shot: "beanBrand",       def: "" },
        { meta: "beanType",       state: "beanType",       shot: "beanType",        def: "" },
        { meta: "roastDate",      state: "roastDate",      shot: "roastDate",       def: "",  date: true },
        { meta: "roastLevel",     state: "roastLevel",     shot: "roastLevel",      def: "" },
        { meta: "grinderSetting", state: "grinderSetting", shot: "grinderSetting",  def: "",  pulled: true },
        { meta: "rpm",            state: "rpm",            shot: "rpm",             def: 0,   pulled: true },
        { meta: "equipmentId",    state: "equipmentId",    shot: "equipmentId",     def: -1,  orDef: true },
        { meta: "barista",        state: "barista",        shot: "barista",         def: "",  pulled: true },
        { meta: "doseWeight",     state: "doseWeight",     shot: "doseWeightG",     def: 0,   pulled: true },
        { meta: "finalWeight",    state: "drinkWeight",    shot: "finalWeightG",    def: 0,   pulled: true },
        { meta: "drinkTds",       state: "drinkTds",       shot: "drinkTdsPct",     def: 0,   pulled: true },
        { meta: "drinkEy",        state: "drinkEy",        shot: "drinkEyPct",      def: 0,   pulled: true },
        { meta: "espressoNotes",  state: "notes",          shot: "espressoNotes",   def: "",  pulled: true },
        { meta: "beverageType",   state: "beverageType",   shot: "beverageType",    def: "espresso" },
        { meta: "beanBaseJson",   state: "beanBaseJson",   shot: "beanBaseJson",    def: "" },
        { meta: "enjoyment",      state: "enjoyment",      shot: "enjoyment0to100", def: 0,   pulled: true },
        { meta: "tasteBalance",   state: "tasteBalance",   shot: "tasteBalance",    def: "",  pulled: true },
        { meta: "tasteBody",      state: "tasteBody",      shot: "tasteBody",       def: "",  pulled: true }
    ]

    // A stored value in the form a spec's edit field holds.
    function _asField(spec, v) {
        v = (typeof spec.def === "string" || spec.orDef) ? (v || spec.def) : (v ?? spec.def)
        return spec.date ? DateUtils.normalizeDateString(v) : v
    }
    function _stored(spec, shot) { return _asField(spec, shot[spec.shot]) }

    // A Visualizer pull rewrote stored fields (`previous` -> `written`, keyed by
    // metadata key). Each field still showing its old value takes the new one;
    // a field edited here keeps the edit, which the next save sends. Undo frames
    // holding the old value move too, so Undo cannot write it back.
    function mergePulledChanges(previous, written) {
        var state = captureEditState()
        var nb = clonePersistedShot(editShotData)
        var shownChanged = false
        for (var i = 0; i < _fieldSpecs.length; ++i) {
            var spec = _fieldSpecs[i]
            if (!spec.pulled || written[spec.meta] === undefined) continue
            var before = _asField(spec, previous[spec.meta])
            var after = _asField(spec, written[spec.meta])
            var baseline = _stored(spec, editShotData)
            // The dose field shows the DYE dose when the shot has none.
            var shown = (spec.meta === "doseWeight" && !(baseline > 0)) ? Settings.dye.dyeBeanWeight : baseline
            nb[spec.shot] = written[spec.meta]
            if (state[spec.state] === shown) {
                state[spec.state] = after
                shownChanged = true
            }
            var frames = _undoStack.concat([_committedState])
            for (var f = 0; f < frames.length; ++f)
                if (frames[f][spec.state] === before) frames[f][spec.state] = after
        }
        editShotData = nb
        if (!shownChanged) return
        applyEditState(state)
        _committedState = captureEditState()
    }

    function saveEditedShot() {
        Keyboard.commit()
        if (editShotId <= 0) return
        // Only the fields that differ from the stored record, so a value pulled
        // from Visualizer while this page was open is not written back over by
        // the page's older copy. A retry after a failed write cannot tell what
        // that write carried (the baseline already advanced), so it sends all.
        var state = captureEditState()
        var metadata = {}
        for (var i = 0; i < _fieldSpecs.length; ++i) {
            var spec = _fieldSpecs[i]
            if (_saveFailed || state[spec.state] !== _stored(spec, editShotData))
                metadata[spec.meta] = state[spec.state]
        }
        if (Object.keys(metadata).length === 0) return
        // Keep the indexed canonical id in lockstep with the blob — the
        // backend does NOT derive beanbase_id from beanbase_json on a
        // metadata update (updateShotMetadataStatic), so a link made here
        // (e.g. the lightweight LinkBeanBaseDialog path) would otherwise
        // leave beanbase_id stale and break the history search lane.
        if (metadata.beanBaseJson !== undefined)
            metadata["beanBaseId"] = beanBaseLinked ? String(activeBeanBase.id) : ""
        pendingVisualizerUpdate = true
        pendingDecentUpdate = true
        // Held while the page is open: this save reaches the destinations on close.
        if (_heldShotId === editShotId) MainController.shotUploads.expectHeldEdit(editShotId)
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
        nb.tasteBalance = editTasteBalance
        nb.tasteBody = editTasteBody
        nb.espressoNotes = editNotes
        nb.beverageType = editBeverageType
        editShotData = nb
    }

    // The Upload button: every active destination, through ShotUploads. Flushes
    // the edit first; the upload reads the saved row after that write.
    function uploadNow() {
        autosave()
        if (Settings.visualizer.visualizerActive) {
            // Cleared before dispatching: an edit made after this goes on close.
            pendingVisualizerUpdate = false
            uploadError = ""
            uploadSkipReason = ""
        }
        if (Settings.decent.active) pendingDecentUpdate = false
        MainController.shotUploads.uploadNow(editShotId)
    }

    // Anything short of a stored upload leaves the edit pending, so Upload stays lit.
    // Never cleared here: Upload clears it on tap, and an edit saved while the
    // upload ran did not go with it.
    Connections {
        target: MainController.decentUploader
        function onUploadFinished(shotId, result) {
            if (shotId !== postShotReviewPage.editShotId) return
            if (result !== DecentShotUploader.Result.Uploaded) postShotReviewPage.pendingDecentUpdate = true
            // Success is not shown, but is spoken, as Visualizer's is.
            const spoken = result === DecentShotUploader.Result.Uploaded
                ? TranslationManager.translate("decent.upload.done", "Uploaded to your Decent account")
                : decentUploadStatus.text
            if (AccessibilityManager.enabled && spoken.length > 0)
                AccessibilityManager.announce(spoken, true)
        }
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
            postShotReviewPage.uploadError = ""
            postShotReviewPage.uploadSkipReason = ""
            if (url) {
                // clonePersistedShot (not Object.assign) so a first-time upload
                // on an unedited shot — where editShotData is still the raw
                // Q_GADGET wrapper from onShotReady — doesn't strip durationSec,
                // the frame arrays, dateTime, etc. See the helper's docstring.
                let nb = postShotReviewPage.clonePersistedShot(postShotReviewPage.editShotData)
                nb.visualizerId = visualizerId
                nb.visualizerUrl = url
                nb.hasVisualizerUpload = true
                postShotReviewPage.editShotData = nb
                postShotReviewPage._visualizerId = visualizerId
            }
        }
        // This shot's upload or update finished, from this page or any other trigger.
        function onSavedShotFinished(shotId, error, skipReason) {
            if (shotId !== postShotReviewPage.editShotId) return
            postShotReviewPage.uploadError = error
            postShotReviewPage.uploadSkipReason = skipReason
        }
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        targetFlickable: flickable
        textFields: [
            baristaField.textField,
            notesExpandable.textField
        ]

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentHeight: pageColumn.implicitHeight + Theme.spacingMedium
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onMovementStarted: postShotReviewPage.resetAutoCloseTimer()
        onContentYChanged: postShotReviewPage.resetAutoCloseTimer()

        // One column at every width, graph full width under the header. Side by side
        // was tried here, as on the comparison page, and left the plot too small to read.
        // What the user records right after a pull (rating, taste, notes, measurements)
        // comes straight after the graph; the outcome and comparison follow it.
        ColumnLayout {
            id: pageColumn
            width: flickable.width
            spacing: Theme.scaled(6)
            transform: Translate { id: contentSlide; x: 0 }

            // Header: Profile (Temp) + date + quality badges + sparkle + Read TDS + Basic/Advanced toggle
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                visible: postShotReviewPage.hasCurves

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            // StyledText so elide works (Qt ignores elide on RichText); still
                            // renders the <font> highlight and emoji <img> tags.
                            textFormat: Text.StyledText
                            text: {
                                var name = Theme.escapeHtml(postShotReviewPage.editShotData.profileName || "")
                                var t = postShotReviewPage.editShotData.temperatureOverrideC
                                var result
                                if (t !== undefined && t !== null && t > 0) {
                                    // The recorded temp is the effective brew temperature (present
                                    // on every shot); highlight it only when it deviated from the
                                    // shot-time profile's own default.
                                    let tempStr = "(" + Math.round(Theme.cToDisplay(t)) + Theme.tempUnitSuffix() + ")"
                                    if (postShotReviewPage._shotTempOverridden)
                                        tempStr = "<font color=\"" + Theme.colorToHex(Theme.highlightColor) + "\">" + tempStr + "</font>"
                                    result = name + " " + tempStr
                                } else {
                                    result = name
                                }
                                // allowMarkup: `name` is escaped above and tempStr is a
                                // <font> span we build ourselves — escaping here would
                                // render the highlight as raw tags.
                                return Theme.replaceEmojiWithImg(result, Theme.titleFont.pixelSize, true)
                            }
                            font: Theme.titleFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: postShotReviewPage.editShotData.dateTime || ""
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                            Layout.maximumWidth: pageColumn.width * 0.35
                        }

                        QualityBadges {
                            // No `visible` gate. The row used to be hidden unless a flag
                            // fired OR the shot carried a profileKbId — which in practice
                            // gated only the CLEAN shot, and with it the Shot Summary chip,
                            // the one affordance that opens the analysis. The dialog's lines
                            // come from analyzeShot() over this shot's own curves; the KB
                            // contributes suppressions, not content, so an unresolved profile
                            // has MORE to report, not less. The id was the wrong proxy by
                            // then anyway — it is the persisted column, while the pipeline
                            // re-resolves on every load, and an ambiguous shape resolves to a
                            // candidate set that persists nothing. Chip conditions live
                            // inside QualityBadges and are untouched.
                            Layout.fillWidth: false
                            Layout.maximumWidth: pageColumn.width * 0.5
                            channelingDetected: postShotReviewPage.editShotData.channelingDetected ?? false
                            grindIssueDetected: postShotReviewPage.editShotData.grindIssueDetected ?? false
                            skipFirstFrameDetected: postShotReviewPage.editShotData.skipFirstFrameDetected ?? false
                            pourTruncatedDetected: postShotReviewPage.editShotData.pourTruncatedDetected ?? false
                            verdictCategory: (postShotReviewPage.editShotData && postShotReviewPage.editShotData.detectorResults)
                                ? (postShotReviewPage.editShotData.detectorResults.verdictCategory ?? "") : ""
                            onSummaryRequested: reviewAnalysisDialog.open()
                        }

                        // The SHOT's own derivation, recorded when its analysis
                        // ran — not ProfileManager's, which answers for whatever
                        // profile currently bears this title and diverges the
                        // moment the user edits or deletes it. The badges beside
                        // this line were computed under the entry named here.
                        KbDerivedFromLabel {
                            derivedFrom: postShotReviewPage.editShotData.profileKbDerivedFrom || ""
                            Layout.maximumWidth: pageColumn.width * 0.3
                        }

                        ShotAnalysisDialog {
                            id: reviewAnalysisDialog
                            shotData: postShotReviewPage.editShotData
                        }
                    }
                }

                // KB sparkle button — opens the profile knowledge base
                Image {
                    id: headerSparkle
                    visible: ProfileManager.profileHasKnowledge(
                                 postShotReviewPage.editShotData.profileName || "")
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
                            // openForShot(), not openFor(): the dial-in difference
                            // block must compare against the profile this shot was
                            // PULLED with. Editing the catalog profile afterwards
                            // must not rewrite what an old shot appears to have
                            // been brewed with. Everything else the dialog needs
                            // still comes from the one function that knows every
                            // field — hand-setting properties leaves them stale.
                            (knowledgeDialogLoader.ensure() as ProfileKnowledgeDialog)?.openForShot(
                                postShotReviewPage.editShotData.profileName || "",
                                postShotReviewPage.editShotData.profileJson || "")
                        }
                    }
                }

                // Promote this shot to a recipe. Hidden when it came from one that still
                // resolves: a recipe's shot never offers to become a recipe.
                HeaderPillButton {
                    visible: !((postShotReviewPage.editShotData.recipeId || -1) > 0 && (recipeResolver.recipe.name || "") !== "")
                    text: TranslationManager.translate("shotpage.saveAsRecipe", "Save as recipe")
                    accessibleName: TranslationManager.translate("shotdetail.button.recipe", "Create recipe from this shot")
                    onClicked: AppShell.recipeWizardRequested("create", { promoteShotId: postShotReviewPage.editShotId })
                }

                // Milk weigh button: captures the milk for the next steam from this page,
                // where a group-head steam is often started. Tap weighs, long-press
                // changes pitcher. Only on the shot just pulled: an old shot opened from
                // history is not followed by a steam.
                HeaderPillButton {
                    id: milkWeighButton

                    // Usable = a real, enabled preset with a saved empty weight; net milk
                    // needs that weight. steamPitcherPresets is read for the dependency:
                    // the count and lookup are invokables, which record none.
                    readonly property var usablePitchers: {
                        void(Settings.brew.steamPitcherPresets)
                        var out = []
                        for (var i = 0; i < Settings.brew.steamPitcherCount(); ++i) {
                            var p = Settings.brew.getSteamPitcherPreset(i)
                            if (p && !p.disabled && (p.pitcherWeightG ?? 0) > 0)
                                out.push(i)
                        }
                        return out
                    }
                    readonly property int selected: Settings.brew.selectedSteamPitcher
                    readonly property bool selectedUsable: usablePitchers.indexOf(selected) >= 0
                    readonly property var selectedPreset: {
                        void(Settings.brew.steamPitcherPresets)
                        return Settings.brew.getSteamPitcherPreset(selected)
                    }
                    readonly property string pitcherName: selectedUsable ? SteamLabels.pitcherName(selectedPreset) : ""
                    readonly property bool realScale: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
                    readonly property real sessionMilk: AppShell.sessionMeasuredMilkG

                    // Tap starts the same attempt as selecting Steam on the idle page.
                    property bool armed: false

                    function arm() {
                        milkCapture.startAttempt()
                        armed = true
                    }
                    // Tap again, leaving the page or losing the button: give back the milk
                    // the attempt set aside, since nothing replaced it.
                    function cancel() {
                        if (!armed)
                            return
                        armed = false
                        milkCapture.cancelAttempt()
                    }
                    function selectNextPitcher() {
                        armed = false   // the pitcher change itself clears the session milk
                        if (usablePitchers.length === 0)
                            return
                        var pos = usablePitchers.indexOf(selected)
                        MainController.selectSteamPitcher(usablePitchers[(pos + 1) % usablePitchers.length], 0)
                        if (AccessibilityManager.enabled)
                            AccessibilityManager.announce(milkWeighButton.pitcherName)
                    }

                    readonly property string labelText: {
                        if (!selectedUsable)
                            return TranslationManager.translate("postshotreview.milk.choosePitcher", "Choose pitcher")
                        if (armed) {
                            if (!milkCapture.loadPresent)
                                return TranslationManager.translate("postshotreview.milk.placePitcher", "Place pitcher")
                            const hint = SteamLabels.captureHint(milkCapture)
                            return hint !== "" ? hint
                                : TranslationManager.translate("postshotreview.milk.weighing", "Weighing…")
                        }
                        if (sessionMilk > 0)
                            return TranslationManager.translate("postshotreview.milk.captured", "%1 · %2 g")
                                .arg(pitcherName).arg(sessionMilk.toFixed(0))
                        return pitcherName
                    }
                    // While armed the label is an instruction, so a screen-reader user hears each
                    // step; the placement step gets the idle page's full prompt.
                    onLabelTextChanged: {
                        if (!armed || !AccessibilityManager.enabled)
                            return
                        AccessibilityManager.announce(milkCapture.loadPresent ? labelText
                            : TranslationManager.translate("idle.label.placeOrReplacePitcher", "Place (or lift and replace) the milk pitcher on the scale"))
                    }

                    visible: Settings.brew.milkAutoCaptureEnabled && usablePitchers.length > 0 && realScale
                             && postShotReviewPage.editShotId === MainController.lastSavedShotId
                    onVisibleChanged: if (!visible) cancel()
                    text: labelText
                    highlighted: armed
                    supportLongPress: true
                    accessibleName: {
                        var name = TranslationManager.translate("postshotreview.milk.weighAccessible", "Weigh milk")
                        return name + (selectedUsable ? ", " : ". ") + labelText
                    }
                    onClicked: {
                        if (!selectedUsable)
                            selectNextPitcher()
                        else if (armed)
                            cancel()
                        else
                            arm()
                    }
                    onLongPressed: selectNextPitcher()
                    onIncreaseRequested: selectNextPitcher()

                    MilkCapture {
                        id: milkCapture
                        active: milkWeighButton.armed && milkWeighButton.visible
                                && postShotReviewPage.StackView.status === StackView.Active
                        onMilkCaptured: function(milk, t) {
                            milkWeighButton.armed = false
                            if (AccessibilityManager.enabled) {
                                AccessibilityManager.announce(t > 0
                                    ? TranslationManager.translate("idle.steamCaptured", "Steam time: %1s for %2g milk").arg(t).arg(milk.toFixed(0))
                                    : TranslationManager.translate("postshotreview.milk.capturedAccessible", "%1 grams of milk").arg(milk.toFixed(0)))
                            }
                        }
                    }
                    // A new pitcher changes the weight being subtracted.
                    Connections {
                        target: Settings.brew
                        function onSelectedSteamPitcherChanged() { milkWeighButton.armed = false }
                    }
                }

                // Read TDS button (DiFluid R1 / R2 refractometer)
                HeaderPillButton {
                    id: readTdsButton
                    property bool refConnected: BLEManager.refractometerConnected
                    property bool refMeasuring: refConnected && Refractometer.measuring
                    // R1 advertises with names starting "DFT_TDJ_*" (see DiFluidR1::isR1Device).
                    property bool isR1: (Settings.savedRefractometerName || "").toLowerCase().indexOf("dft_tdj") === 0
                    property real tdsValue: postShotReviewPage.editDrinkTds
                    // Only a plausible reading is worth showing here, against the
                    // same two constants that gate an incoming one — one definition
                    // of plausible, not a second. Those constants postdate readings
                    // already on disk: the R2's 655.09% error sentinel was autosaved
                    // onto a shot before they existed (see kMaximumPlausibleTds), and
                    // this button would otherwise be the most prominent place that
                    // value ever appeared — the only one in basic mode, where the TDS
                    // input is hidden and cannot correct it. A deliberately typed
                    // out-of-range value stays visible in that input.
                    property bool tdsPlausible: tdsValue >= postShotReviewPage.kMinimumPlausibleTds
                        && tdsValue <= postShotReviewPage.kMaximumPlausibleTds
                    visible: Settings.savedRefractometerAddress !== ""
                    enabled: !refMeasuring
                    opacity: refMeasuring ? 0.5 : 1.0
                    text: {
                        // A ×3 run takes ~22s on hardware, so "..." for that long
                        // reads as hung — show which test of how many instead.
                        if (postShotReviewPage.avgTotal > 0)
                            return postShotReviewPage.avgDone + "/" + postShotReviewPage.avgTotal
                        if (readTdsButton.refMeasuring) return TranslationManager.translate("postshotreview.refractometer.measuring", "...")
                        // Once this shot has a TDS — read here with this
                        // button, sent by the device's own Start button, or
                        // loaded from the saved shot — show the value rather
                        // than the invitation or the off state, so a reading
                        // stays readable even if the refractometer link drops
                        // afterwards. In basic mode the TDS input is hidden,
                        // so this is the only place the reading is visible.
                        // Tapping still re-reads (or reconnects); the
                        // connection state stays in the accessible name.
                        if (readTdsButton.tdsPlausible)
                            return readTdsButton.tdsValue.toFixed(2) + "%"
                        if (!readTdsButton.refConnected) {
                            return readTdsButton.isR1
                                ? TranslationManager.translate("postshotreview.refractometer.r1off", "R1 Off")
                                : TranslationManager.translate("postshotreview.refractometer.r2off", "R2 Off")
                        }
                        return TranslationManager.translate("postshotreview.refractometer.readTds", "Read TDS")
                    }
                    accessibleName: {
                        var action = readTdsButton.refConnected
                            ? TranslationManager.translate("postshotreview.readTdsFromRefractometer", "Read TDS from refractometer")
                            : TranslationManager.translate("postshotreview.reconnectRefractometer", "Reconnect refractometer")
                        // The label text is Accessible.ignored, so a shown
                        // reading has to be spoken here or it is inaudible.
                        if (readTdsButton.tdsPlausible)
                            return TranslationManager.translate("postshotreview.label.tds", "TDS") + " "
                                + readTdsButton.tdsValue.toFixed(2) + " "
                                + TranslationManager.translate("postshotreview.unit.percent", "percent") + ". " + action
                        return action
                    }
                    onClicked: {
                        if (!readTdsButton.refConnected) {
                            BLEManager.scanForDevices()
                            return
                        }
                        postShotReviewPage.avgDone = 0
                        postShotReviewPage.avgTotal = 0
                        // A single test, deliberately — a judgement about magnitude,
                        // not about whether averaging works. Three runs on hardware
                        // (7.82/7.83/7.85, 8.04/8.05/8.05, 8.10/8.08/8.08) show
                        // genuine random scatter, sigma about 0.011% TDS, which
                        // averaging over three does reduce — to about 0.007%.
                        //
                        // But that 0.005% improvement is smaller than the 0.01% step
                        // the device reports in, so it cannot even be represented in
                        // the answer, and it is an order of magnitude under
                        // sample-prep variance. The cost is 12-22s against ~3.5s.
                        //
                        // Averaging is not used anywhere: setDeviceTestCount() exists
                        // as protocol coverage only and nothing calls it, so the
                        // device's own count stays at 1 and an Auto Test reading is a
                        // single reading too. See BLE_PROTOCOL.md, "Averaging is
                        // driver-level only".
                        Refractometer.requestMeasurement()
                    }
                }

                // Graph display options (advanced curves, flow scale).
                GraphOptionsButton {}
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
                // Fields + order come from the user's Shot Plan widget config;
                // profile + temperature are filtered out (already in the title),
                // so no temperature bindings are needed here.
                itemOrder: postShotReviewPage._shotPlanItemOrder
                profileName: postShotReviewPage.editShotData.profileName || ""
                dose: postShotReviewPage.editDoseWeight || 0
                // targetWeightG is the planned target (0 for volume/timer
                // profiles) — fall back to the edited output so a yield still shows.
                // Override state comes from THIS shot's frozen snapshot (recorded
                // target vs the profile snapshot's default), never the live dial.
                profileYield: postShotReviewPage._shotProfileYield
                targetWeight: (postShotReviewPage.editShotData.targetWeightG || 0) > 0
                    ? postShotReviewPage.editShotData.targetWeightG : (postShotReviewPage.editDrinkWeight || 0)
                // The shot is poured, so lead the yield segment with what came
                // out and keep the target behind it ("36.4g (target 36.0g)"). Bound to
                // the LIVE edit state like the rest of this line, so correcting
                // the out weight moves it. Collapses to one number on target,
                // and when targetWeightG is 0 the target above already IS this.
                actualYield: postShotReviewPage.editDrinkWeight || 0
                yieldOverridden: (postShotReviewPage.editShotData.targetWeightG || 0) > 0
                    && postShotReviewPage._shotProfileYield > 0
                    && Math.abs(postShotReviewPage.editShotData.targetWeightG - postShotReviewPage._shotProfileYield) > 0.1
                // THIS shot's recorded anchor, not the live dial's. These
                // default to Settings.brew reads, so leaving them unbound
                // would re-render the just-pulled shot against whatever the
                // user dials next while the review page is still open.
                yieldAnchorMode: postShotReviewPage.editShotData.yieldMode || "none"
                yieldAnchorRatio: postShotReviewPage.editShotData.yieldMode === "ratio"
                    ? (postShotReviewPage.editShotData.yieldAnchorValue || 0) : 0
                // Temperature is filtered out of the line (it lives in the title,
                // highlighted there when it deviated from the profile default);
                // pin the flag off the live dial regardless.
                tempOverridden: false
                yieldTargetOnly: true
                roasterBrand: postShotReviewPage.editBeanBrand
                coffeeName: postShotReviewPage.editBeanType
                roastDate: postShotReviewPage.editRoastDate
                // THIS shot's frozen recipe (resolved from editShotData.recipeId),
                // never the live active recipe — same resolver the recipe card
                // uses. Empty when the shot had no recipe.
                recipeName: recipeResolver.recipe.name || ""
                grindSize: postShotReviewPage.editGrinderSetting
                grindRpm: postShotReviewPage.editRpm
                // Only show RPM for grinders that actually report it (a Niche
                // Zero does not); a stale/spurious recorded RPM must not surface.
                rpmCapable: postShotReviewPage.editRpmCapable
                beverageType: postShotReviewPage.editBeverageType || "espresso"
                isCleaning: false
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.focusable: true
            }

            // Resizable graph, with the crosshair values under the plot so a tap never
            // covers a curve or moves the page.
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), postShotReviewPage.graphHeight))
                    + graphReadout.implicitHeight + resizeHandle.height + Theme.spacingSmall
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                visible: postShotReviewPage.hasCurves
                Accessible.role: Accessible.Graphic
                Accessible.name: TranslationManager.translate("shot.graph.accessible.name", "Shot graph. Tap to inspect values")
                Accessible.focusable: true
                Accessible.onPressAction: reviewGraph.inspectAtPosition(reviewGraph.plotArea.x + reviewGraph.plotArea.width / 2, 0)

                HistoryShotGraph {
                    id: reviewGraph
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: graphReadout.top
                    anchors.margins: Theme.spacingSmall
                    showPhaseLabels: Settings.graph.advancedMode
                    pressureData: postShotReviewPage.editShotData.pressure || []
                    flowData: postShotReviewPage.editShotData.flow || []
                    temperatureData: postShotReviewPage.editShotData.temperature || []
                    weightData: postShotReviewPage.editShotData.weight || []
                    weightFlowRateData: postShotReviewPage.editShotData.weightFlowRate || []
                    resistanceData: postShotReviewPage.editShotData.resistance || []
                    conductanceData: postShotReviewPage.editShotData.conductance || []
                    darcyResistanceData: postShotReviewPage.editShotData.darcyResistance || []
                    conductanceDerivativeData: postShotReviewPage.editShotData.conductanceDerivative || []
                    temperatureMixData: postShotReviewPage.editShotData.temperatureMix || []
                    portalSamples: postShotReviewPage.editShotData.portalSamples || []
                    pressureGoalData: postShotReviewPage.editShotData.pressureGoal || []
                    flowGoalData: postShotReviewPage.editShotData.flowGoal || []
                    temperatureGoalData: postShotReviewPage.editShotData.temperatureGoal || []
                    temperatureMixGoalData: postShotReviewPage.editShotData.temperatureMixGoal || []
                    phaseMarkers: postShotReviewPage.editShotData.phases || []
                    maxTime: postShotReviewPage.editShotData.durationSec || 60
                }

                GraphReadout {
                    id: graphReadout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: resizeHandle.top
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    graph: reviewGraph
                }

                // Tap or drag to inspect; from a list, a horizontal swipe steps to the
                // newer or older shot instead.
                SwipeableArea {
                    anchors.fill: reviewGraph
                    enabled: postShotReviewPage.shotIds.length > 1
                    canSwipeLeft: postShotReviewPage.canGoOlder
                    canSwipeRight: postShotReviewPage.canGoNewer
                    onSwipedLeft: postShotReviewPage.stepTo(postShotReviewPage.currentIndex + 1)
                    onSwipedRight: postShotReviewPage.stepTo(postShotReviewPage.currentIndex - 1)
                    onTapped: function(x, y) { postShotReviewPage.inspectGraphAt(x, y) }
                    onMoved: function(x, y) { reviewGraph.inspectAtPosition(x, y) }
                }
                MouseArea {
                    anchors.fill: reviewGraph
                    enabled: postShotReviewPage.shotIds.length <= 1
                    onClicked: function(mouse) { postShotReviewPage.inspectGraphAt(mouse.x, mouse.y) }
                    onPositionChanged: function(mouse) {
                        if (pressed) reviewGraph.inspectAtPosition(mouse.x, mouse.y)
                    }
                }

                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.ignored: true

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
                            startHeight = Math.min(Theme.scaled(400), postShotReviewPage.graphHeight)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                let currentY = mouse.y + resizeHandle.mapToItem(postShotReviewPage, 0, 0).y
                                postShotReviewPage.graphHeight = Math.max(Theme.scaled(100),
                                    Math.min(Theme.scaled(400), startHeight + currentY - startY))
                            }
                        }
                        onReleased: {
                            Settings.setValue("shotPage/graphHeight", postShotReviewPage.graphHeight)
                            flickable.returnToBounds()
                        }
                    }
                }
            }

            GraphChipRow {
                Layout.fillWidth: true
                visible: postShotReviewPage.hasCurves
                portalAvailable: (postShotReviewPage.editShotData.portalSamples || []).length > 0
                phaseEntries: reviewGraph.phaseEntries
                hiddenPhaseLabels: reviewGraph.hiddenPhaseLabels
                onPhaseToggled: label => reviewGraph.togglePhaseLabel(label)
            }

            // Newer / older in the list the page was opened from, newest first.
            RowLayout {
                visible: postShotReviewPage.shotIds.length > 1
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.newershot", "Newer Shot")
                    accessibleName: TranslationManager.translate("shotdetail.accessible.newershot", "Newer shot")
                        + ", " + postShotReviewPage.positionText
                    Layout.fillWidth: true
                    Layout.preferredWidth: 10
                    enabled: postShotReviewPage.canGoNewer
                    onClicked: postShotReviewPage.stepTo(postShotReviewPage.currentIndex - 1)
                }
                Text {
                    text: (postShotReviewPage.currentIndex + 1) + " / " + postShotReviewPage.shotIds.length
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.oldershot", "Older Shot")
                    accessibleName: TranslationManager.translate("shotdetail.accessible.oldershot", "Older shot")
                        + ", " + postShotReviewPage.positionText
                    Layout.fillWidth: true
                    Layout.preferredWidth: 10
                    enabled: postShotReviewPage.canGoOlder
                    onClicked: postShotReviewPage.stepTo(postShotReviewPage.currentIndex + 1)
                }
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
                    // not `parent.width`: the RowLayout's width depends on this child's
                    // preferred size, and that cycle tripped Qt Quick Layouts'
                    // "recursive rearrange" guard in production. The flickable's width
                    // does not; the cap is ~45% of the page.
                    Layout.maximumWidth: flickable.width * 0.45
                    Accessible.ignored: true
                }

                Rectangle {
                    id: ratingBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    radius: Theme.scaled(12)
                    color: Theme.cardBackgroundColor
                    border.width: 1
                    border.color: Theme.textSecondaryColor

                    RatingInput {
                        id: ratingInput
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(4)
                        value: postShotReviewPage.editEnjoyment
                        accessibleName: TranslationManager.translate("rating.quick.prompt", "How was this shot?")
                        onValueModified: function(newValue) {
                            postShotReviewPage.editEnjoyment = newValue
                            postShotReviewPage.autosave("rating")
                        }
                        onActiveFocusChanged: if (!activeFocus) postShotReviewPage.finalizeEdit()
                    }
                }
            }

            // Structured taste axes (add-ai-taste-intake). Overall is hidden here
            // because the rating slider above already owns it — one rating widget,
            // no parallel UI. Same TastePicker component as the AI intake dialog,
            // writing the same shot columns.
            TastePicker {
                id: tastePicker
                Layout.fillWidth: true
                showOverall: false
                tasteBalance: postShotReviewPage.editTasteBalance
                tasteBody: postShotReviewPage.editTasteBody
                onTasteBalanceModified: function(value) {
                    postShotReviewPage.editTasteBalance = value
                    postShotReviewPage.autosave("tasteBalance")
                }
                onTasteBodyModified: function(value) {
                    postShotReviewPage.editTasteBody = value
                    postShotReviewPage.autosave("tasteBody")
                }
            }

            // Notes
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
                    text: postShotReviewPage.editNotes
                    accessibleName: TranslationManager.translate("postshotreview.label.notes", "Notes")
                    textFont: Theme.bodyFont
                    onTextChanged: postShotReviewPage.editNotes = text
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
                            value: postShotReviewPage.editDoseWeight
                            accessibleName: TranslationManager.translate("postshotreview.label.dose", "Dose") + " " + value + " " + TranslationManager.translate("postshotreview.unit.grams", "grams")
                            onValueModified: function(newValue) {
                                doseInput.value = newValue
                                postShotReviewPage.editDoseWeight = newValue
                                postShotReviewPage.calculateEy()
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
                                if (activeFocus) { Keyboard.commit(); Keyboard.hide() }
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
                            value: postShotReviewPage.editDrinkWeight
                            accessibleName: TranslationManager.translate("postshotreview.accessible.output", "Output") + " " + value + " " + TranslationManager.translate("postshotreview.unit.grams", "grams")
                            onValueModified: function(newValue) {
                                outInput.value = newValue
                                postShotReviewPage.editDrinkWeight = newValue
                                postShotReviewPage.calculateEy()
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
                                if (activeFocus) { Keyboard.commit(); Keyboard.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                        }
                    }

                    // Grind + RPM (moved from the field grid — the most-adjusted
                    // dial-in, now beside Dose/Out). One tap-to-open control for
                    // both halves ("grind · rpm"): tapping opens the grind picker
                    // (wheels + keyboard entry). Grinder context is the SHOT's
                    // grinder (editGrinderBrand/Model, seeded from editShotData)
                    // — step, candidates and notation follow the grinder this
                    // shot was pulled on, not the currently active one. Commits
                    // autosave immediately: Done is the commit event (a
                    // tap-to-open control has no blur).
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
                        GrindField {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            presentation: "field"
                            fieldColor: Theme.cardBackgroundColor   // match the Dose/Out steppers
                            grinderBrand: postShotReviewPage.editGrinderBrand
                            grinderModel: postShotReviewPage.editGrinderModel
                            grindSetting: postShotReviewPage.editGrinderSetting
                            rpmValue: postShotReviewPage.editRpm
                            accessibleName: TranslationManager.translate("shotdetail.grind", "Grind")
                            onGrindCommitted: function(v) {
                                postShotReviewPage.editGrinderSetting = v
                                postShotReviewPage.autosave("grinderSetting", true)
                            }
                            onRpmCommitted: function(rpm) {
                                postShotReviewPage.editRpm = rpm
                                postShotReviewPage.autosave("rpm", true)
                            }
                        }
                    }

                    // TDS (advanced mode only)
                    ColumnLayout {
                        visible: Settings.graph.advancedMode
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.scaled(2)
                        RowLayout {
                            spacing: Theme.scaled(4)
                            Tr {
                                key: "postshotreview.label.tds"
                                fallback: "TDS"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }
                            // Refractometer status dot (only when configured)
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(6)
                                Layout.preferredHeight: Theme.scaled(6)
                                radius: Theme.scaled(3)
                                visible: Settings.savedRefractometerAddress !== ""
                                color: {
                                    if (!BLEManager.refractometerConnected) return Theme.textSecondaryColor
                                    if (Refractometer.tds > 0) return Theme.successColor
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
                                value: postShotReviewPage.editDrinkTds
                                accessibleName: TranslationManager.translate("postshotreview.label.tds", "TDS") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                                onValueModified: function(newValue) {
                                    postShotReviewPage.editDrinkTds = newValue
                                    postShotReviewPage.calculateEy()
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
                                if (activeFocus) { Keyboard.commit(); Keyboard.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                            }
                        }
                    }

                    // EY (advanced mode only)
                    ColumnLayout {
                        visible: Settings.graph.advancedMode
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
                            value: postShotReviewPage.editDrinkEy
                            accessibleName: TranslationManager.translate("postshotreview.accessible.extractionyield", "Extraction yield") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                            onValueModified: function(newValue) {
                                postShotReviewPage.editDrinkEy = newValue
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
                                if (activeFocus) { Keyboard.commit(); Keyboard.hide() }
                                else postShotReviewPage.finalizeEdit()
                            }
                        }
                    }
                }
            }

            ShotResultsCard {
                Layout.fillWidth: true
                comparison: postShotReviewPage.shotOutcome.comparison || ({})
                previousWhen: postShotReviewPage.shotOutcome.previousDateTime || ""
                onCompareRequested: {
                    MainController.shotComparison.clearAll()
                    MainController.shotComparison.addShots([postShotReviewPage.shotOutcome.previousShotId,
                                                            postShotReviewPage.editShotId])
                    AppShell.shotComparisonRequested()
                }
            }

            // Phase summary panel (advanced mode only)
            PhaseSummaryPanel {
                Layout.fillWidth: true
                phaseSummaries: postShotReviewPage.editShotData.phaseSummaries || []
                visible: Settings.graph.advancedMode && (postShotReviewPage.editShotData.phaseSummaries || []).length > 0
            }



            // Standalone bean summary (+ Change Beans) — shown ONLY when the
            // shot used no recipe. With a recipe these fold into the recipe card
            // above, so it reads as one cohesive recipe. Bean dialog + equipment
            // picker live at page scope (shared with the recipe card).
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                visible: (postShotReviewPage.editShotData.recipeId || -1) <= 0

                BeanSummary {
                    id: reviewBeanSummary
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    useShotData: true
                    roasterName: postShotReviewPage.editBeanBrand
                    coffeeName: postShotReviewPage.editBeanType
                    roastDate: postShotReviewPage.editRoastDate
                    roastLevel: postShotReviewPage.editRoastLevel
                    beanBaseData: postShotReviewPage.editBeanBaseJson
                    linkable: true
                    onLinkRequested: postShotReviewPage.requestBeanLink()
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(44)
                    Layout.alignment: Qt.AlignVCenter
                    text: reviewBeanSummary.hasBeans
                        ? TranslationManager.translate("beans.button.change", "Change Beans")
                        : TranslationManager.translate("beans.button.select", "Select Beans")
                    accessibleName: TranslationManager.translate("beans.button.accessible.change", "Change the selected beans")
                    onClicked: (changeBeansLoader.ensure() as ChangeBeansDialog)?.open()
                }
            }

            BeanBaseDetailsRow {
                Layout.fillWidth: true
                // Recipe-gate AND the bean-linked gate. This override replaces the
                // component's own `visible: hasData`, so without beanBaseLinked an
                // unlinked no-recipe shot forces the row visible: it then paints its
                // "Linked to Bean Base / Tap for bean details" fallback into a
                // zero-height box (implicitHeight is 0 when !hasData), overlapping
                // the Barista field below with a dead tap target.
                visible: (postShotReviewPage.editShotData.recipeId || -1) <= 0 && postShotReviewPage.beanBaseLinked
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
                        WebDebugLogger.warn("Shot", "PostShotReviewPage", ["enrichment merge skipped — unparseable blob"].map(String).join(" "))
                        return
                    }
                    for (let k in attrs) merged[k] = attrs[k]
                    postShotReviewPage.editBeanBaseJson = JSON.stringify(merged)
                    if (attrs.degree) postShotReviewPage.editRoastLevel = attrs.degree
                    postShotReviewPage.autosave("beanBase", true)
                }
            }

            // Barista — advanced-only (most users are the sole barista).
            SuggestionField {
                id: baristaField
                visible: Settings.graph.advancedMode
                Layout.fillWidth: true
                label: TranslationManager.translate("postshotreview.label.barista", "Barista")
                text: postShotReviewPage.editBarista
                suggestions: {
                    var list = postShotReviewPage._baristaHistory.slice()
                    if (postShotReviewPage.editBarista.length > 0 && list.indexOf(postShotReviewPage.editBarista) === -1) list = [postShotReviewPage.editBarista].concat(list)
                    return list
                }
                onVisibleChanged: if (visible && !postShotReviewPage._baristaHistoryLoaded) postShotReviewPage._refreshBaristaHistory()
                onTextEdited: function(t) { postShotReviewPage.editBarista = t }
                onInputBlurred: postShotReviewPage.autosave("barista", true)
            }

            // Recipe card (recipeId > 0): the recipe AND its components in
            // one cohesive card, modelled on the recipe editor's summary.
            // Beans and equipment are edited right here; profile, dial-in
            // and steam/water are read-only echoes (grind/RPM are edited in
            // the Dial-in row at the top). Every value is this page's live
            // edit state. When a recipe is used this replaces the standalone
            // bean and equipment controls (which gate to the no-recipe case).
            Rectangle {
                id: recipeCard
                Layout.fillWidth: true
                Layout.preferredHeight: recipeColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                border.width: 1
                border.color: Theme.borderColor
                visible: (postShotReviewPage.editShotData.recipeId || -1) > 0

                readonly property string recipeName: recipeResolver.recipe.name || ""
                readonly property string recipeDrinkLabel:
                    DrinkType.shortLabel(DrinkType.fromRecipeMap(recipeResolver.recipe))

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
                        textFormat: Text.StyledText
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
                            source: DrinkType.icon(DrinkType.fromRecipeMap(recipeResolver.recipe))
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
                        Layout.preferredHeight: Theme.scaled(1)
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
                                roasterName: postShotReviewPage.editBeanBrand
                                coffeeName: postShotReviewPage.editBeanType
                                roastDate: postShotReviewPage.editRoastDate
                                roastLevel: postShotReviewPage.editRoastLevel
                                beanBaseData: postShotReviewPage.editBeanBaseJson
                                linkable: true
                                onLinkRequested: postShotReviewPage.requestBeanLink()
                            }
                            AccessibleButton {
                                Layout.preferredHeight: Theme.scaled(44)
                                Layout.alignment: Qt.AlignVCenter
                                text: reviewRecipeBeanSummary.hasBeans
                                    ? TranslationManager.translate("beans.button.change", "Change Beans")
                                    : TranslationManager.translate("beans.button.select", "Select Beans")
                                accessibleName: TranslationManager.translate("beans.button.accessible.change", "Change the selected beans")
                                onClicked: (changeBeansLoader.ensure() as ChangeBeansDialog)?.open()
                            }
                        }
                        BeanBaseDetailsRow {
                            Layout.fillWidth: true
                            beanBaseJson: postShotReviewPage.editBeanBaseJson
                        }
                    }

                    // Dial-in (read-only) — grind/RPM are edited in the
                    // Dial-in row above; echoed here as part of the overview.
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
                            grinderName: postShotReviewPage.editEquipmentName || ""
                            grinderBrand: postShotReviewPage.editGrinderBrand
                            grinderModel: postShotReviewPage.editGrinderModel
                            grinderBurrs: postShotReviewPage.editGrinderBurrs
                            basketBrand: postShotReviewPage.editBasketBrand
                            basketModel: postShotReviewPage.editBasketModel
                            puckPrepCanonical: postShotReviewPage.editPuckPrep
                        }
                        AccessibleButton {
                            Layout.preferredHeight: Theme.scaled(36)
                            _customFontSize: Theme.captionFont.pixelSize
                            leftPadding: Theme.scaled(10)
                            rightPadding: Theme.scaled(10)
                            text: (postShotReviewPage.editEquipmentName.length > 0 || postShotReviewPage.editGrinderBrand.length > 0 || postShotReviewPage.editGrinderModel.length > 0)
                                  ? TranslationManager.translate("postshotreview.changeEquipment", "Change Equipment")
                                  : TranslationManager.translate("postshotreview.addEquipment", "Add Equipment")
                            accessibleName: text
                            onClicked: (equipmentDialogLoader.ensure() as SwitchEquipmentDialog)?.openPicker()
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
                Layout.fillWidth: true
                Layout.preferredHeight: equipmentCardColumn.implicitHeight + Theme.scaled(24)
                // With a recipe, equipment folds into the recipe card above.
                visible: (postShotReviewPage.editShotData.recipeId || -1) <= 0
                readonly property bool hasEquipment: postShotReviewPage.editEquipmentName.length > 0
                                                     || postShotReviewPage.editGrinderBrand.length > 0 || postShotReviewPage.editGrinderModel.length > 0
                color: Theme.cardBackgroundColor
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
                        grinderName: postShotReviewPage.editEquipmentName || ""
                        grinderBrand: postShotReviewPage.editGrinderBrand
                        grinderModel: postShotReviewPage.editGrinderModel
                        grinderBurrs: postShotReviewPage.editGrinderBurrs
                        basketBrand: postShotReviewPage.editBasketBrand
                        basketModel: postShotReviewPage.editBasketModel
                        puckPrepCanonical: postShotReviewPage.editPuckPrep
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
                        onClicked: (equipmentDialogLoader.ensure() as SwitchEquipmentDialog)?.openPicker()
                    }
                }
            }


            // Where this shot has been uploaded. Uploading itself is the bottom bar's.
            Rectangle {
                id: uploadsCard
                readonly property bool onVisualizer: postShotReviewPage._visualizerId !== ""
                readonly property bool decentUploaded: !!postShotReviewPage._decentState.uploaded
                readonly property bool decentRejected: !!postShotReviewPage._decentState.rejected
                readonly property string decentUrl: decentUploaded
                    ? MainController.decentUploader.shotViewUrl(postShotReviewPage._decentState.serial,
                                                                postShotReviewPage._decentState.serverShotId)
                    : ""
                Layout.fillWidth: true
                Layout.preferredHeight: uploadsColumn.implicitHeight + Theme.spacingMedium * 2
                visible: onVisualizer || decentUploaded || decentRejected
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: uploadsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    RowLayout {
                        visible: uploadsCard.onVisualizer
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)
                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("shotdetail.uploadedtovisualizer", "Uploaded to Visualizer")
                        ThemedIcon {
                            source: "qrc:/icons/CloudUpload.svg"
                            iconSize: Theme.labelFont.pixelSize
                            color: Theme.successColor
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("shotdetail.uploadedtovisualizer", "Uploaded to Visualizer")
                            font: Theme.labelFont
                            color: Theme.successColor
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }

                    RowLayout {
                        visible: uploadsCard.decentUploaded || uploadsCard.decentRejected
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)
                        ThemedIcon {
                            source: "qrc:/icons/CloudUpload.svg"
                            iconSize: Theme.labelFont.pixelSize
                            color: uploadsCard.decentUploaded ? Theme.successColor : Theme.errorColor
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: uploadsCard.decentUploaded
                                  ? TranslationManager.translate("shotdetail.uploadedToDecent", "Uploaded to Decent")
                                  : TranslationManager.translate("shotdetail.rejectedByDecent", "Not accepted by Decent (HTTP %1)")
                                        .arg(postShotReviewPage._decentState.rejectedStatus || 0)
                            font: Theme.labelFont
                            color: uploadsCard.decentUploaded ? Theme.successColor : Theme.errorColor
                            elide: Text.ElideRight
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                        Text {
                            id: decentViewLink
                            visible: uploadsCard.decentUrl.length > 0
                            text: TranslationManager.translate("shotdetail.viewOnDecent", "View on decentespresso.com")
                            font: Theme.captionFont
                            color: Theme.primaryColor
                            Accessible.ignored: true
                            AccessibleMouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(6)
                                accessibleName: TranslationManager.translate("shotdetail.viewOnDecentAccessible",
                                                                             "View this shot on decentespresso.com. Opens web browser")
                                accessibleItem: decentViewLink
                                onAccessibleClicked: Qt.openUrlExternally(uploadsCard.decentUrl)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                AccessibleButton {
                    visible: Settings.graph.advancedMode
                    text: TranslationManager.translate("shotdetail.viewdebuglog", "View Debug Log")
                    accessibleName: TranslationManager.translate("shotDetail.viewDebugLog", "View debug log for this shot")
                    Layout.fillWidth: true
                    onClicked: (debugLogLoader.ensure() as DecenzaDialog)?.open()
                }
                AccessibleButton {
                    text: TranslationManager.translate("shotdetail.deleteshot", "Delete Shot")
                    accessibleName: TranslationManager.translate("shotDetail.deleteShotPermanently", "Permanently delete this shot from history")
                    destructive: true
                    Layout.fillWidth: true
                    onClicked: (deleteDialogLoader.ensure() as DecenzaDialog)?.open()
                }
            }

        }
    }

    } // KeyboardAwareContainer

    // "Link to Bean Base" nudge action. A historical shot (anything but the
    // just-pulled one) links lightweight — attach the canonical record to THIS
    // shot only, no bag created. The most-recent shot keeps the full Change
    // Beans path, where linking the active bag is the intended "wrong bag" fix.
    function requestBeanLink() {
        if (editShotId === MainController.lastSavedShotId) {
            (changeBeansLoader.ensure() as ChangeBeansDialog)?.open()
        } else {
            (linkBeanBaseLoader.ensure() as LinkBeanBaseDialog)?.openWith(
                [editBeanBrand, editBeanType].filter(function(s) { return s && s.length > 0 }).join(" "))
        }
    }

    // Apply a canonical Bean Base pick to this shot's snapshot only. Mirrors
    // the fields ChangeBeansDialog.onBagSelected writes (so the linked shot
    // looks identical) MINUS any bag: no inventory bag, activeBagId untouched.
    function applyCanonicalLinkToShot(entry) {
        editBeanBaseJson = JSON.stringify(entry)
        if (entry.roasterName) editBeanBrand = String(entry.roasterName)
        if (entry.roastName) editBeanType = String(entry.roastName)
        if (entry.degree) editRoastLevel = String(entry.degree)
        // autosave() -> saveEditedShot() persists the snapshot and already sets
        // pendingVisualizerUpdate, so the enriched bean info pushes to Visualizer.
        autosave("beanBase", true)
        // Best-effort attribute enrichment (origin/variety/process/...): the
        // onCanonicalDetails handler above merges it into editBeanBaseJson and
        // re-saves when it arrives.
        MainController.beanbase.fetchCanonicalDetails(entry)
    }

    // Link, Change Beans and Change Equipment dialogs — page-scoped so both the recipe card and
    // the standalone bean/equipment rows share one instance regardless of which
    // is visible. Built on first open, not with the page: eagerly they were ~14k
    // objects (ChangeBeansDialog alone carries date pickers, Bean Base details and an
    // equipment picker) created after every shot and destroyed again on Back (#1976).
    OnDemandLoader {
        id: linkBeanBaseLoader
        sourceComponent: Component {
            LinkBeanBaseDialog {
                onEntryPicked: function(entry) { postShotReviewPage.applyCanonicalLinkToShot(entry) }
            }
        }
    }

    OnDemandLoader {
        id: changeBeansLoader
        sourceComponent: Component {
            ChangeBeansDialog {
                // Only the most recent shot is the "post-shot" fix path (sets
                // activeBagId too); older shots opened through this page are historical
                // — retag the shot only.
                context: postShotReviewPage.editShotId === MainController.lastSavedShotId ? "postShot" : "historicalShot"
                shotId: postShotReviewPage.editShotId
                onBagSelected: function(bagId, bag) {
                    // The dialog already wrote the snapshot to the DB — mirror it into
                    // the edit fields and advance the autosave baseline so a later
                    // autosave doesn't clobber the new bag with stale values.
                    postShotReviewPage.editBeanBrand = bag.roasterName || ""
                    postShotReviewPage.editBeanType = bag.coffeeName || ""
                    postShotReviewPage.editRoastDate = bag.roastDate || ""
                    postShotReviewPage.editRoastLevel = bag.roastLevel || ""
                    postShotReviewPage.editBeanBaseJson = bag.beanBaseData || ""
                    var nb = postShotReviewPage.clonePersistedShot(postShotReviewPage.editShotData)
                    nb.beanBrand = postShotReviewPage.editBeanBrand
                    nb.beanType = postShotReviewPage.editBeanType
                    nb.roastDate = postShotReviewPage.editRoastDate
                    nb.roastLevel = postShotReviewPage.editRoastLevel
                    nb.beanBaseJson = postShotReviewPage.editBeanBaseJson
                    postShotReviewPage.editShotData = nb
                    postShotReviewPage._committedState = postShotReviewPage.captureEditState()
                    postShotReviewPage.pendingVisualizerUpdate = true
                    postShotReviewPage.pendingDecentUpdate = true
                }
            }
        }
    }
    // Re-point this shot's grinder to a different/new package. The picker
    // doesn't touch the active bag (applyToActiveBag:false); we resolve the
    // chosen package and persist equipmentId here. On the most recent shot the
    // save's runStickySync then makes it the active (and active bag's) package.
    OnDemandLoader {
        id: equipmentDialogLoader
        sourceComponent: Component {
            SwitchEquipmentDialog {
                applyToActiveBag: false
                onPackageSaved: function(packageId) {
                    postShotReviewPage._pendingEquipmentId = packageId
                    MainController.equipmentStorage.requestPackage(packageId)
                }
            }
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

    // Built on first open, like the dialogs above.
    OnDemandLoader {
        id: debugLogLoader
        sourceComponent: Component {
            DecenzaDialog {
                id: debugLogDialog
                parent: Overlay.overlay
                anchors.centerIn: parent
                width: parent.width * 0.9
                height: parent.height * 0.8
                modal: true
                padding: 0
                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 1
                    border.color: Theme.borderColor
                }
                contentItem: ColumnLayout {
                    spacing: 0
                    Text {
                        text: TranslationManager.translate("shotdetail.debuglog", "Debug Log")
                        font: Theme.titleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(20)
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(10)
                        contentWidth: availableWidth
                        TextArea {
                            text: postShotReviewPage.editShotData.debugLog
                                  || TranslationManager.translate("shotdetail.nodebuglog", "No debug log available")
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.scaled(12)
                            color: Theme.textColor
                            readOnly: true
                            selectByMouse: true
                            wrapMode: Text.Wrap
                            background: Rectangle { color: "transparent" }
                            Accessible.role: Accessible.EditableText
                            Accessible.name: TranslationManager.translate("shotdetail.debuglog", "Debug Log")
                            Accessible.description: text.substring(0, 200)
                        }
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("shotdetail.close", "Close")
                        accessibleName: TranslationManager.translate("shotdetail.closeDebugLog", "Close debug log")
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(20)
                        onClicked: debugLogDialog.close()
                    }
                }
            }
        }
    }

    OnDemandLoader {
        id: deleteDialogLoader
        sourceComponent: Component {
            DecenzaDialog {
                id: deleteConfirmDialog
                parent: Overlay.overlay
                anchors.centerIn: parent
                width: Theme.scaled(360)
                modal: true
                padding: 0
                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 1
                    border.color: Theme.borderColor
                }
                contentItem: ColumnLayout {
                    spacing: 0
                    Text {
                        text: TranslationManager.translate("shotdetail.deleteconfirmtitle", "Delete Shot?")
                        font: Theme.titleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(20)
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                    }
                    Text {
                        text: TranslationManager.translate("shotdetail.deleteconfirmmessage", "This will permanently delete this shot from history.")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        wrapMode: Text.Wrap
                        Accessible.ignored: true
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(10)
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(20)
                    }
                    RowLayout {
                        spacing: Theme.scaled(10)
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(20)
                        AccessibleButton {
                            text: TranslationManager.translate("shotdetail.cancel", "Cancel")
                            accessibleName: TranslationManager.translate("shotdetail.cancelDelete", "Cancel delete")
                            Layout.fillWidth: true
                            onClicked: deleteConfirmDialog.close()
                        }
                        AccessibleButton {
                            text: TranslationManager.translate("shotdetail.delete", "Delete")
                            accessibleName: TranslationManager.translate("shotdetail.confirmDelete", "Confirm delete shot")
                            destructive: true
                            Layout.fillWidth: true
                            onClicked: {
                                deleteConfirmDialog.close()
                                // No flush after this: the row is going away.
                                postShotReviewPage._editLoaded = false
                                MainController.shotHistory.requestDeleteShot(postShotReviewPage.editShotId)
                            }
                        }
                    }
                }
            }
        }
    }

    // Bottom bar (stays visible under keyboard)
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("postshotreview.title", "Shot Review")
        onBackClicked: postShotReviewPage.handleBack()

        leftContent: BottomBarSubtitle {
            bar: bottomBar
            page: postShotReviewPage
            primaryText: postShotReviewPage.editShotData.profileName || ""
            secondaryText: postShotReviewPage.editShotData.dateTime || ""
        }

        // Undo button — edits autosave on every commit point; this reverts the
        // most recent committed change (repeatable). Visible only when there is
        // something on the undo stack.
        AccessibleButton {
            id: undoButton
            visible: postShotReviewPage._undoDepth > 0
            icon.source: "qrc:/icons/history.svg"
            tintIcon: true
            text: TranslationManager.translate("postshotreview.button.undo", "Undo")
            accessibleName: TranslationManager.translate("postshotreview.accessible.undo", "Undo last change")
            onClicked: postShotReviewPage.undoLastChange()
        }

        // The one Upload button: sends the shot to every destination switched on
        // and connected (Visualizer, the Decent account, or both).
        AccessibleButton {
            id: uploadButton
            readonly property bool toVisualizer: Settings.visualizer.visualizerActive
            readonly property bool toDecent: Settings.decent.active
            visible: postShotReviewPage.editShotData.durationSec > 0 && (uploadButton.toVisualizer || uploadButton.toDecent)
                     && !MainController.visualizer.uploading && !MainController.decentUploader.uploading

            // Every active destination already holds this shot as edited: nothing to
            // push. Otherwise a tap sends something, which the warning fill signals;
            // colour alone can't carry that, so the description flags any active
            // destination not yet holding the current version.
            readonly property bool uploadedSomewhere:
                (uploadButton.toVisualizer && !!postShotReviewPage._visualizerId) || (uploadButton.toDecent && postShotReviewPage._decentUploaded)
            readonly property bool inSync:
                (!uploadButton.toVisualizer || (!!postShotReviewPage._visualizerId && !postShotReviewPage.pendingVisualizerUpdate))
                && (!uploadButton.toDecent || (postShotReviewPage._decentUploaded && !postShotReviewPage.pendingDecentUpdate))
            primary: uploadButton.inSync
            warning: !uploadButton.inSync

            icon.source: "qrc:/icons/CloudUpload.svg"
            tintIcon: true
            text: TranslationManager.translate("postshotreview.button.uploadShot", "Upload")

            accessibleName: uploadButton.uploadedSomewhere
                ? TranslationManager.translate("postshotreview.button.reuploadShot", "Re-Upload shot")
                : TranslationManager.translate("postshotreview.button.uploadShotAccessible", "Upload shot")
            accessibleDescription: (uploadButton.uploadedSomewhere && !uploadButton.inSync)
                ? TranslationManager.translate("postshotreview.accessible.changespending", "Changes pending upload")
                : ""

            onClicked: postShotReviewPage.uploadNow()
        }

        // Uploading/Updating indicator
        Text {
            visible: MainController.visualizer.uploading
            // One translate() per branch: a Tr with a switched key passes through a
            // mismatched key/fallback pair, which rewrote the string registry every upload.
            text: postShotReviewPage._visualizerId
                  ? TranslationManager.translate("postshotreview.status.updating", "Updating...")
                  : TranslationManager.translate("postshotreview.status.uploading", "Uploading...")
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }

        Text {
            visible: postShotReviewPage.uploadError.length > 0 && !MainController.visualizer.uploading
            text: TranslationManager.translate("postshotreview.upload.failed", "Upload failed") + ": " + postShotReviewPage.uploadError
            color: Theme.errorColor
            font: Theme.labelFont
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // Capped so a long server message doesn't inflate contentRow.implicitWidth
            // and starve BottomBar.leftContentMaxWidth: a fillWidth child is Preferred
            // policy, so the layout shrinks it, but its UNCAPPED implicit width is what
            // the row reports as preferred (qquicklayout.cpp:1279 clamps preferred to
            // maximum, which is what makes this cap register).
            Layout.maximumWidth: postShotReviewPage.width * 0.25
        }

        Text {
            visible: postShotReviewPage.uploadSkipReason.length > 0 && !MainController.visualizer.uploading
            text: TranslationManager.translate("postshotreview.upload.skipped", "Upload skipped") + ": " + postShotReviewPage.uploadSkipReason
            color: Theme.textSecondaryColor
            font: Theme.labelFont
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // Capped so a long server message doesn't inflate contentRow.implicitWidth
            // and starve BottomBar.leftContentMaxWidth: a fillWidth child is Preferred
            // policy, so the layout shrinks it, but its UNCAPPED implicit width is what
            // the row reports as preferred (qquicklayout.cpp:1279 clamps preferred to
            // maximum, which is what makes this cap register).
            Layout.maximumWidth: postShotReviewPage.width * 0.25
        }

        DecentUploadStatus {
            id: decentUploadStatus
            shotId: postShotReviewPage.editShotId
            Layout.fillWidth: true
            // Same cap as the Visualizer status lines above, for the same reason.
            Layout.maximumWidth: postShotReviewPage.width * 0.25
        }

        // AI Advice button - visible when AI is configured and we have shot data
        AccessibleButton {
            id: aiAdviceButton
            visible: MainController.aiManager && MainController.aiManager.isConfigured && postShotReviewPage.editShotData.durationSec > 0
            enabled: MainController.aiManager && MainController.aiManager.isConfigured && !MainController.aiManager.isAnalyzing
            primary: true
            icon.source: "qrc:/icons/sparkle.svg"
            tintIcon: true
            text: MainController.aiManager && MainController.aiManager.isAnalyzing
                  ? TranslationManager.translate("postshotreview.button.analyzing", "Analyzing...")
                  : TranslationManager.translate("postshotreview.button.aiadvice", "AI Advice")
            accessibleName: TranslationManager.translate("postshotreview.accessible.getaiadvice", "Get AI Advice")
            onClicked: {
                // editShotData is the DB-load snapshot and is NOT updated as
                // the user rates the shot on this page (or in the advisor's
                // own intake). Hand the advisor the LIVE taste so its per-shot
                // intake gate sees feedback the user already gave and doesn't
                // re-ask "how did this shot taste?".
                var shotForAdvisor = postShotReviewPage.clonePersistedShot(postShotReviewPage.editShotData)
                shotForAdvisor.tasteBalance = postShotReviewPage.editTasteBalance
                shotForAdvisor.tasteBody = postShotReviewPage.editTasteBody
                shotForAdvisor.enjoyment0to100 = postShotReviewPage.editEnjoyment
                // A local, not a bare `(…)` line: see scripts/check_qml_asi_hazards.py.
                const overlay = conversationOverlayLoader.ensure() as ConversationOverlay
                overlay?.openWithShot(shotForAdvisor, postShotReviewPage.editBeanBrand, postShotReviewPage.editBeanType, postShotReviewPage.editShotData.profileName, postShotReviewPage.editShotId)
            }
        }

        // Discuss button - opens external AI app
        AccessibleButton {
            id: discussButton
            readonly property bool isClaudeDesktopReady:
                Settings.network.discussShotApp !== Settings.network.discussAppClaudeDesktop
                || Settings.network.claudeRcSessionUrl.length > 0
            visible: postShotReviewPage.editShotData.durationSec > 0 && Settings.network.discussShotApp !== Settings.network.discussAppNone
            enabled: isClaudeDesktopReady
            primary: true
            icon.source: "qrc:/icons/sparkle.svg"
            tintIcon: true
            text: TranslationManager.translate("postshotreview.button.discuss", "Discuss")
            accessibleName: TranslationManager.translate("postshotreview.accessible.discuss", "Discuss shot with external AI app")
            onClicked: {
                // Copy shot summary to clipboard if MCP is not connected
                if (!Settings.mcp.mcpEnabled && MainController.aiManager) {
                    // Prose, not the JSON envelope: the user is pasting this into an
                    // external AI tool, where prose reads better and does not
                    // double-ship the structured fields (#1042).
                    let summary = MainController.aiManager.buildShotAnalysisProseForShot(postShotReviewPage.editShotData)
                    if (summary.length > 0) MainController.copyToClipboard(summary)
                }
                // Open configured AI app
                var url = Settings.network.discussShotUrl()
                if (url.length > 0) Settings.network.openDiscussUrl(url)
            }
        }

        // Email Prompt button - fallback for users without API keys
        AccessibleButton {
            id: emailPromptButton
            visible: MainController.aiManager && !MainController.aiManager.isConfigured && postShotReviewPage.editShotData.durationSec > 0
            icon.source: "qrc:/icons/sparkle.svg"
            tintIcon: true
            text: TranslationManager.translate("postshotreview.button.emailprompt", "Email Prompt")
            accessibleName: TranslationManager.translate("postshotreview.accessible.emailprompt", "Email AI prompt to yourself")
            onClicked: {
                // Prose, not the JSON envelope — the email body lands in the
                // user's mail client; the JSON shape double-shipped structured
                // fields (#1042).
                var prompt = MainController.aiManager.buildShotAnalysisProseForShot(postShotReviewPage.editShotData)
                // Open mailto: with prompt in body
                Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent("Espresso Shot Analysis") +
                                    "&body=" + encodeURIComponent(prompt))
            }
        }

    }

    // Profile AI knowledge base dialog
    // Shared KB popup (qml/components/ProfileKnowledgeDialog.qml).
    OnDemandLoader {
        id: knowledgeDialogLoader
        anchors.fill: parent  // the dialog centres on and sizes from its parent
        sourceComponent: Component {
            ProfileKnowledgeDialog {
            }
        }
    }

    // Built on first use: the advisor overlay was created with every open of this page
    // whether or not the advisor was opened (#1976).
    OnDemandLoader {
        id: conversationOverlayLoader
        anchors.fill: parent
        z: 200  // the overlay's own z only orders it inside this Loader
        sourceComponent: Component {
            ConversationOverlay {
                anchors.fill: parent
                overlayTitle: TranslationManager.translate("postshotreview.conversation.title", "Dialing Conversation")

                // Taste tapped in the advisor's intake flows back to this page at once so
                // the rating slider + taste chips reflect it. The overlay already
                // persisted the taps to the DB (requestUpdateShotMetadata, which the
                // upload destinations follow), so mirror them in without re-saving — the
                // same external-flow pattern as ChangeBeansDialog.onBagSelected. That
                // means advancing BOTH baselines: editShotData (what hasUnsavedChanges
                // compares against) as well as _committedState (the undo baseline). If we
                // only advanced _committedState, hasUnsavedChanges would stay stuck true
                // and the next lifecycle flush (backing out) would redundantly re-save,
                // re-send it to the upload destinations, and push a phantom undo frame.
                // Empty axes are left untouched.
                onTasteIntakeSubmitted: function(tasteBalance, tasteBody, overall) {
                    var s = postShotReviewPage.captureEditState()
                    if (tasteBalance.length > 0) s.tasteBalance = tasteBalance
                    if (tasteBody.length > 0) s.tasteBody = tasteBody
                    if (overall > 0) s.enjoyment = overall
                    postShotReviewPage.applyEditState(s)
                    var nb = postShotReviewPage.clonePersistedShot(postShotReviewPage.editShotData)
                    nb.tasteBalance = postShotReviewPage.editTasteBalance
                    nb.tasteBody = postShotReviewPage.editTasteBody
                    nb.enjoyment0to100 = postShotReviewPage.editEnjoyment
                    postShotReviewPage.editShotData = nb
                    postShotReviewPage._committedState = postShotReviewPage.captureEditState()
                }
            }
        }
    }

}
