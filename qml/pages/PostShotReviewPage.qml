import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../components"
import "../components/DateUtils.js" as DateUtils

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
                editGrinderSetting = editShotData.grinderSetting || ""
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
            if (!success)
                console.warn("PostShotReviewPage: Failed to save metadata for shot", shotId)
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
            if (_pendingBeanAutoFill.length > 0) {
                var types = MainController.shotHistory.getDistinctBeanTypesForBrand(_pendingBeanAutoFill)
                if (types.length > 0) {
                    _pendingBeanAutoFill = ""
                    if (types.length === 1) {
                        editBeanType = types[0]
                        // Async auto-fill (cache resolved after the roaster was
                        // picked) — persist like any other committed value.
                        postShotReviewPage.autosave("beanType", true)
                    }
                }
            }
        }
    }

    property string _pendingBeanAutoFill: ""

    // Editing fields (separate from Settings.dye* to avoid polluting current session)
    property string editBeanBrand: ""
    property string editBeanType: ""
    property string editRoastDate: ""
    property string editRoastLevel: ""
    property string editGrinderBrand: ""
    property string editGrinderModel: ""
    property string editGrinderBurrs: ""
    property string editGrinderSetting: ""
    property string editBarista: ""
    property double editDoseWeight: 0
    property double editDrinkWeight: 0
    property double editDrinkTds: 0
    property double editDrinkEy: 0
    property int editEnjoyment: 0  // 0 = unrated

    property string editNotes: ""
    property string editBeverageType: "espresso"
    // Bean Base snapshot stored with this shot (read-only on this page —
    // re-linking happens via the Beans page in edit mode).
    property string editBeanBaseJson: ""

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
        editBarista !== (editShotData.barista || "") ||
        editDoseWeight !== ((editShotData.doseWeightG > 0) ? editShotData.doseWeightG : Settings.dye.dyeBeanWeight) ||
        editDrinkWeight !== (editShotData.finalWeightG ?? 0) ||
        editDrinkTds !== (editShotData.drinkTdsPct ?? 0) ||
        editDrinkEy !== (editShotData.drinkEyPct ?? 0) ||
        editEnjoyment !== (editShotData.enjoyment0to100 ?? 0) ||
        editNotes !== (editShotData.espressoNotes || "") ||
        editBeverageType !== (editShotData.beverageType || "espresso")
    )

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
            barista: editBarista, doseWeight: editDoseWeight,
            drinkWeight: editDrinkWeight, drinkTds: editDrinkTds,
            drinkEy: editDrinkEy, enjoyment: editEnjoyment,
            notes: editNotes, beverageType: editBeverageType
        }
    }

    function applyEditState(s) {
        editBeanBrand = s.beanBrand; editBeanType = s.beanType
        editRoastDate = s.roastDate; editRoastLevel = s.roastLevel
        editGrinderBrand = s.grinderBrand; editGrinderModel = s.grinderModel
        editGrinderBurrs = s.grinderBurrs; editGrinderSetting = s.grinderSetting
        editBarista = s.barista; editDoseWeight = s.doseWeight
        editDrinkWeight = s.drinkWeight; editDrinkTds = s.drinkTds
        editDrinkEy = s.drinkEy; editEnjoyment = s.enjoyment
        editNotes = s.notes; editBeverageType = s.beverageType
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
            barista: src.barista, doseWeightG: src.doseWeightG,
            finalWeightG: src.finalWeightG, drinkTdsPct: src.drinkTdsPct,
            drinkEyPct: src.drinkEyPct, enjoyment0to100: src.enjoyment0to100,
            espressoNotes: src.espressoNotes, beverageType: src.beverageType
        }
    }

    // Save edited shot back to history
    function saveEditedShot() {
        Qt.inputMethod.commit()
        if (editShotId <= 0) return
        pendingVisualizerUpdate = true
        var metadata = {
            "beanBrand": editBeanBrand,
            "beanType": editBeanType,
            "roastDate": editRoastDate,
            "roastLevel": editRoastLevel,
            "grinderBrand": editGrinderBrand,
            "grinderModel": editGrinderModel,
            "grinderBurrs": editGrinderBurrs,
            "grinderSetting": editGrinderSetting,
            "barista": editBarista,
            "doseWeight": editDoseWeight,
            "finalWeight": editDrinkWeight,
            "drinkTds": editDrinkTds,
            "drinkEy": editDrinkEy,
            "espressoNotes": editNotes,
            "beverageType": editBeverageType
        }
        metadata["enjoyment"] = editEnjoyment
        MainController.shotHistory.requestUpdateShotMetadata(editShotId, metadata)

        // Sync sticky metadata back to Settings (bean/grinder info) for the next shot.
        // Per-shot fields (enjoyment, notes, TDS, EY) are NOT synced — otherwise they
        // would leak into the next shot's metadata, since MainController builds shot
        // metadata from these Settings values at shot end.
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
        nb.grinderSetting = editGrinderSetting
        nb.barista = editBarista
        nb.doseWeightG = editDoseWeight
        nb.finalWeightG = editDrinkWeight
        nb.drinkTdsPct = editDrinkTds
        nb.drinkEyPct = editDrinkEy
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
            roasterField.textField, coffeeField.textField, roastDateField.textField,
            grinderBrandField.textField, grinderModelField.textField, grinderBurrsField.textField,
            settingField.textField, baristaField.textField,
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
                                    result = name + " (" + Math.round(t) + "\u00B0C)"
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
                            height: Theme.scaled(40)
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
                            height: Theme.scaled(40)
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

                    // TDS (advanced mode only)
                    ColumnLayout {
                        visible: postShotReviewPage.advancedMode
                        Layout.fillWidth: true
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
                                height: Theme.scaled(28)
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
                            height: Theme.scaled(28)
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

            // Bean Base details for this shot's snapshot — bag photo +
            // origin · variety · process, tap for the full popup. Zero
            // footprint for unlinked/legacy shots.
            BeanBaseDetailsRow {
                Layout.fillWidth: true
                beanBaseJson: postShotReviewPage.editBeanBaseJson
            }

            // 3-column grid for all fields
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 6

                // === ROW 1: Bean info ===
                SuggestionField {
                    id: roasterField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.roaster", "Roaster")
                    text: editBeanBrand
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctBeanBrands() : []
                        if (editBeanBrand.length > 0 && list.indexOf(editBeanBrand) === -1) list = [editBeanBrand].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { editBeanBrand = t }
                    onInputBlurred: postShotReviewPage.autosave("beanBrand", true)
                    onSuggestionSelected: function(t) {
                        editBeanType = ""
                        editRoastDate = ""
                        var types = MainController.shotHistory.getDistinctBeanTypesForBrand(t)
                        if (types.length === 1) editBeanType = types[0]
                        else if (types.length === 0) _pendingBeanAutoFill = t
                        postShotReviewPage.autosave("beanBrand", true)
                    }
                }

                SuggestionField {
                    id: coffeeField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.coffee", "Coffee")
                    text: editBeanType
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctBeanTypesForBrand(editBeanBrand) : []
                        if (editBeanType.length > 0 && list.indexOf(editBeanType) === -1) list = [editBeanType].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { editBeanType = t }
                    onInputBlurred: postShotReviewPage.autosave("beanType", true)
                    onSuggestionSelected: function(t) { editRoastDate = ""; postShotReviewPage.autosave("beanType", true) }
                }

                Item {
                    Layout.fillWidth: true
                    implicitHeight: roastDateField.implicitHeight

                    LabeledField {
                        id: roastDateField
                        anchors.left: parent.left
                        anchors.right: reviewCalendarBtn.left
                        anchors.rightMargin: Theme.scaled(4)
                        label: TranslationManager.translate("postshotreview.label.roastdate", "Roast date (yyyy-mm-dd)")
                        text: editRoastDate
                        inputHints: Qt.ImhDate
                        inputMask: "9999-99-99"
                        onTextEdited: function(t) { editRoastDate = t }
                        onEditingFinished: postShotReviewPage.autosave("roastDate", true)
                    }

                    AccessibleButton {
                        id: reviewCalendarBtn
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: Theme.scaled(44)
                        height: Theme.scaled(44)
                        accessibleName: TranslationManager.translate("datepicker.openCalendar", "Open calendar")
                        leftPadding: Theme.scaled(8)
                        rightPadding: Theme.scaled(8)
                        icon.source: "qrc:/emoji/1f4c5.svg"
                        icon.width: Theme.scaled(20)
                        icon.height: Theme.scaled(20)
                        text: ""
                        onClicked: reviewDatePicker.openWithDate(editRoastDate)
                    }

                    DatePickerDialog {
                        id: reviewDatePicker
                        onDateSelected: function(dateString) { editRoastDate = dateString; postShotReviewPage.autosave("roastDate", true) }
                    }
                }

                // === ROW 2: Roast level, Grinder ===
                LabeledComboBox {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.roastlevel", "Roast level")
                    model: ["",
                        TranslationManager.translate("postshotreview.roastlevel.light", "Light"),
                        TranslationManager.translate("postshotreview.roastlevel.mediumlight", "Medium-Light"),
                        TranslationManager.translate("postshotreview.roastlevel.medium", "Medium"),
                        TranslationManager.translate("postshotreview.roastlevel.mediumdark", "Medium-Dark"),
                        TranslationManager.translate("postshotreview.roastlevel.dark", "Dark")]
                    currentValue: editRoastLevel
                    onValueChanged: function(v) { editRoastLevel = v; postShotReviewPage.autosave("roastLevel", true) }
                }

                SuggestionField {
                    id: grinderBrandField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.grinderbrand", "Grinder brand")
                    text: editGrinderBrand
                    suggestions: {
                        var history = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderBrands() : []
                        var known = Settings.dye.knownGrinderBrands()
                        var merged = history.slice()
                        for (var i = 0; i < known.length; i++) {
                            if (merged.indexOf(known[i]) < 0) merged.push(known[i])
                        }
                        return merged
                    }
                    onTextEdited: function(t) { editGrinderBrand = t }
                    onInputBlurred: postShotReviewPage.autosave("grinderBrand", true)
                    onSuggestionSelected: function(t) {
                        editGrinderModel = ""
                        editGrinderBurrs = ""
                        var models = Settings.dye.knownGrinderModels(t)
                        if (models.length === 1) {
                            editGrinderModel = models[0]
                            var burrs = Settings.dye.suggestedBurrs(t, models[0])
                            if (burrs.length === 1) editGrinderBurrs = burrs[0]
                        }
                        postShotReviewPage.autosave("grinderBrand", true)
                    }
                }

                SuggestionField {
                    id: grinderModelField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.grindermodel", "Model")
                    text: editGrinderModel
                    suggestions: {
                        var history = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderModelsForBrand(editGrinderBrand) : []
                        var known = Settings.dye.knownGrinderModels(editGrinderBrand)
                        var merged = history.slice()
                        for (var i = 0; i < known.length; i++) {
                            if (merged.indexOf(known[i]) < 0) merged.push(known[i])
                        }
                        return merged
                    }
                    onTextEdited: function(t) { editGrinderModel = t }
                    onInputBlurred: postShotReviewPage.autosave("grinderModel", true)
                    onSuggestionSelected: function(t) {
                        var burrs = Settings.dye.suggestedBurrs(editGrinderBrand, t)
                        if (burrs.length === 1) editGrinderBurrs = burrs[0]
                        postShotReviewPage.autosave("grinderModel", true)
                    }
                }

                // === ROW 3: Burrs, Setting, Beverage type ===
                SuggestionField {
                    id: grinderBurrsField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.grinderburrs", "Burrs")
                    text: editGrinderBurrs
                    suggestions: {
                        var history = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderBurrsForModel(editGrinderBrand, editGrinderModel) : []
                        var known = Settings.dye.suggestedBurrs(editGrinderBrand, editGrinderModel)
                        var merged = history.slice()
                        for (var i = 0; i < known.length; i++) {
                            if (merged.indexOf(known[i]) < 0) merged.push(known[i])
                        }
                        return merged
                    }
                    onTextEdited: function(t) { editGrinderBurrs = t }
                    onInputBlurred: postShotReviewPage.autosave("grinderBurrs", true)
                }

                SuggestionField {
                    id: settingField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.setting", "Setting")
                    text: editGrinderSetting
                    suggestions: {
                        var list = _distinctCacheVersion >= 0 ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(editGrinderModel) : []
                        if (editGrinderSetting.length > 0 && list.indexOf(editGrinderSetting) === -1) list = [editGrinderSetting].concat(list)
                        return list
                    }
                    onTextEdited: function(t) { editGrinderSetting = t }
                    onInputBlurred: postShotReviewPage.autosave("grinderSetting", true)
                }

                // === ROW 4: Beverage type, Barista, Preset, Shot Date ===
                LabeledComboBox {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.beveragetype", "Beverage type")
                    model: ["espresso", "filter", "pourover", "tea_portafilter", "tea", "calibrate", "cleaning", "descale", "manual"]
                    currentValue: editBeverageType
                    onValueChanged: function(v) { editBeverageType = v; postShotReviewPage.autosave("beverageType", true) }
                }

                SuggestionField {
                    id: baristaField
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

                // Preset (read-only display)
                Item {
                    Layout.fillWidth: true
                    implicitHeight: presetLabel.height + presetValue.height + 2

                    Text {
                        id: presetLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: TranslationManager.translate("postshotreview.label.preset", "Preset")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: presetValue
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: presetLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        height: Theme.scaled(48)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: Theme.textSecondaryColor
                        border.width: 1

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            textFormat: Text.RichText
                            text: Theme.replaceEmojiWithImg(editShotData.profileName || "", Theme.scaled(14))
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("postshotreview.label.preset", "Preset") + ": " + (editShotData.profileName || "")
                    }
                }

                // Shot date/time (read-only display)
                Item {
                    Layout.fillWidth: true
                    implicitHeight: shotDateLabel.height + shotDateValue.height + 2

                    Text {
                        id: shotDateLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: TranslationManager.translate("postshotreview.label.shotdate", "Shot date")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: shotDateValue
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: shotDateLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        height: Theme.scaled(48)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: Theme.textSecondaryColor
                        border.width: 1

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            text: editShotData.dateTime || ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("postshotreview.label.shotdate", "Shot date") + ": " + (editShotData.dateTime || "")
                    }
                }

            }

            Item { Layout.preferredHeight: 10 }
        }
    }

    } // KeyboardAwareContainer

    // Bottom bar (stays visible under keyboard)
    BottomBar {
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

    component LabeledField: Item {
        property string label: ""
        property string text: ""
        property int inputHints: Qt.ImhNone
        property string inputMask: ""
        property alias textField: fieldInput  // Expose for KeyboardAwareContainer registration
        signal textEdited(string text)
        signal editingFinished()

        implicitHeight: fieldLabel.height + fieldInput.height + 2

        Text {
            id: fieldLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(11)
            Accessible.ignored: true
        }

        StyledTextField {
            id: fieldInput
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: fieldLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            text: parent.text
            inputMethodHints: parent.inputHints
            inputMask: parent.inputMask
            EnterKey.type: Qt.EnterKeyNext
            Keys.onReturnPressed: nextItemInFocusChain().forceActiveFocus()
            onTextChanged: parent.textEdited(text)
            onActiveFocusChanged: {
                if (activeFocus) {
                    if (AccessibilityManager.enabled) {
                        let announcement = parent.label + ". " + (text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty"))
                        AccessibilityManager.announce(announcement)
                    }
                } else {
                    parent.editingFinished()
                }
            }

            Accessible.role: Accessible.EditableText
            Accessible.name: parent.label
            Accessible.description: text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty")
        }
    }

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
    Dialog {
        id: shotKnowledgeDialog
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(shotKbContent.implicitHeight + Theme.scaled(120), parent.height - Theme.scaled(80))
        padding: 0
        modal: true

        property string profileTitle: ""
        property string content: ""

        function formatContent(raw) {
            var lines = raw.split('\n')
            var parts = []
            for (var i = 0; i < lines.length; i++) {
                var line = lines[i]
                if (!line.trim()) continue
                if (line.startsWith('Also matches:') || line.startsWith('AnalysisFlags:')) continue
                var colonIdx = line.indexOf(': ')
                if (colonIdx > 0 && colonIdx <= 35 && !line.startsWith('DO NOT') && !line.startsWith('-')) {
                    var label = line.substring(0, colonIdx).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                    var value = line.substring(colonIdx + 2).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                    parts.push('<b>' + label + ':</b> ' + value)
                } else if (line.startsWith('DO NOT')) {
                    parts.push('<i>' + line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') + '</i>')
                } else {
                    parts.push(line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'))
                }
            }
            return parts.join('<br>')
        }

        header: Item {
            implicitHeight: Theme.scaled(50)

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scaled(8)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.primaryColor
                    }
                }

                Text {
                    text: shotKnowledgeDialog.profileTitle
                    font: Theme.titleFont
                    color: Theme.textColor
                    anchors.verticalCenter: parent.verticalCenter
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

        contentItem: Flickable {
            clip: true
            contentHeight: shotKbContent.implicitHeight + Theme.scaled(30)
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            Text {
                id: shotKbContent
                width: parent.width - Theme.scaled(40)
                x: Theme.scaled(20)
                y: Theme.scaled(15)
                text: shotKnowledgeDialog.formatContent(shotKnowledgeDialog.content)
                textFormat: Text.RichText
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
        }

        footer: Item {
            implicitHeight: Theme.scaled(55)

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }

            AccessibleButton {
                anchors.centerIn: parent
                width: Theme.scaled(100)
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                onClicked: shotKnowledgeDialog.close()
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }
    }

    ConversationOverlay {
        id: conversationOverlay
        anchors.fill: parent
        overlayTitle: TranslationManager.translate("postshotreview.conversation.title", "Dialing Conversation")
    }

}
