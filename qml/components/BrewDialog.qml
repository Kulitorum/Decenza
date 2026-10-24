pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

DecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    // 520 on tablets; caps to the viewport so it can't overflow a narrow phone
    // (the content column and the height cap both track this width).
    readonly property real _frameWidth: Math.min(Theme.scaled(520),
                                                  (root.parent ? root.parent.width : Theme.scaled(520)) * 0.95)
    width: _frameWidth
    modal: true
    closePolicy: hostWritePending
                 ? Popup.NoAutoClose : Dialog.CloseOnEscape
    browserBackDismissible: !hostWritePending
    browserUnloadDirty: visible && (brewSnapshotLoading || hostWritePending || staleBrewSettings
        || JSON.stringify(draftSnapshot()) !== JSON.stringify(_openedDraft)
        || (_lastDialogLoadedProfileFilename.length > 0
            && _lastDialogLoadedProfileFilename !== _originalProfileFilename))
    padding: 0

    // Accessibility: Dialog is announced via onAboutToShow below
    // Note: Don't set 'title' property - it creates a built-in header frame
    // Note: Don't set Accessible properties on Dialog - it doesn't derive from Item

    // Temperature override
    property double temperatureValue: 0
    property double profileTemperature: 0

    // Empty-scale virtual zero, fed from IdlePage's bean capture. Lets "Weigh" store
    // the cup as a delta (reading - virtualZero) so a non-zeroed scale doesn't bake an
    // offset into the saved cup weight.
    property real scaleVirtualZero: 0

    // Dose value (editable, default 18g)
    property double doseValue: 18.0
    property double ratio: Number(RecipeContext.preferences.lastUsedRatio || 2.0)

    // Target (yield) value and the YIELD ANCHOR (add-yield-ratio-anchor):
    // which of {ratio, stop-at} was last written. "ratio" = the ratio is the
    // anchor and the stop-at derives (dose x ratio); "absolute" = the stop-at
    // is the anchor and the ratio derives; "none" = neither is anchored (the
    // profile answers) until the user edits a row. This is the permanent home
    // of what targetManuallySet used to track per-dialog-session. Seeded from
    // the persisted session anchor (RecipeContext.preferences.brewYieldMode); mutated
    // ONLY by the user editing a row — never by a dose capture.
    property double targetValue: doseValue * ratio
    property double profileTargetWeight: 0
    property string anchorMode: "none"

    // The host snapshot is the commit baseline for this dialog. User edits stay
    // local until OK, and a stale revision keeps the draft visible for review.
    property var brewSnapshot: ({})
    property var brewDialogSnapshot: ({})
    property string brewRevision: ""
    property bool brewSnapshotLoading: false
    property string _snapshotRequestId: ""
    property string _rebaseOnSnapshot: ""
    property var _rebaseExpectation: ({})
    property string _applyRequestId: ""
    property string _profileTemperatureRequestId: ""
    property double _profileTemperatureTarget: 0
    property string _yieldRecipeRequestId: ""
    property string _yieldBagRequestId: ""
    property string _lastDialogLoadedProfileFilename: ""
    property string _originalProfileFilename: ""
    property var _profileLoadRequests: ({})
    property var _preferenceRequests: ({})
    property var _recipeUpdateRequests: ({})
    property var _bagUpdateRequests: ({})
    property var _queuedPreferences: ({})
    property string _preferenceWriteId: ""
    property string _preferenceWriteName: ""
    property var _pendingProfileSeed: ({})
    property string _pendingRecipeSwitchId: ""
    property var _pendingRecipeSwitches: ({})
    property var _recipeEditableBaseline: ({})
    property var _openedDraft: ({})
    property var _recipeEditableBaselineId: -1
    property var _openedBagId: -1
    property var _openedBagYield: ({})
    property double _observedBeanWeight: 0
    property string _profileErrorText: ""
    property bool _applyPending: false
    property bool staleBrewSettings: false
    readonly property bool hostWritePending: _applyPending || _profileTemperatureRequestId.length > 0
        || _pendingRecipeSwitchId.length > 0 || Object.keys(_profileLoadRequests).length > 0
        || Object.keys(_recipeUpdateRequests).length > 0 || Object.keys(_bagUpdateRequests).length > 0
    readonly property bool canWriteBrew: MachineControlService.ready && !hostWritePending
        && !brewSnapshotLoading && !staleBrewSettings && brewRevision.length === 64

    // Equipment (read-only, resolved from the active package via SettingsDye) +
    // the dial-in fields that live in Brew Settings (grind setting + rpm).
    readonly property var preferences: RecipeContext.preferences
    readonly property var currentProfileSummary: ProfileService.currentProfileSummary
    readonly property var currentProfile: currentProfileSummary
    readonly property string equipmentBrand: preferences.dyeGrinderBrand || ""
    readonly property string equipmentModel: preferences.dyeGrinderModel || ""
    readonly property bool equipmentRpmCapable: preferences.grinderRpmCapable === true
    // Show the package's display name (defaults to "{brand} {model}"), not the
    // raw grinder identity (add-equipment-packages).
    readonly property string equipmentLabel: preferences.dyeEquipmentName || ""
    property string grindSetting: ""
    property int grindRpm: 0

    // Profile
    property string selectedProfileTitle: ""
    property string originalProfileFilename: ""

    // Recipe mode: when a recipe is active it owns the profile/beans/equipment,
    // so those rows hide and a Recipe quick-switch row takes the Profile row's
    // place. activeRecipeId is a NOTIFYing property, so this re-evaluates live
    // (e.g. deactivation from another surface while the dialog is open).
    readonly property bool recipeActive: RecipeService.activeRecipeId >= 0
    readonly property var activeRecipe: recipeForId(RecipeService.activeRecipeId)
    // Baselines for Temp Delta and Stop-at: the active recipe, else bag, else profile
    // (MainController / core/brewbaseline.h). The highlight, the Temp Delta zero-point
    // and Clear measure against them.
    readonly property double recipeTempBaseline: Number(brewDialogSnapshot.activeBaselineTemperatureC || 0)
    // Yield baseline as a TYPE-AWARE ANCHOR PAIR (add-yield-ratio-anchor):
    // the active store's own {value, mode} resolved through the ladder
    // (recipe -> bag -> profile; MainController folds that), with the OTHER
    // row's baseline derived through the dialog's dose. Because the derived
    // baseline moves with the dose exactly as the derived value does,
    // neither row spuriously highlights on a dose change — in either mode.
    // A ratio-anchored store never falls back to the profile's gram target
    // (that fallthrough is the #1485 spurious-override bug).
    readonly property double baselineYieldValue: Number(brewDialogSnapshot.activeBaselineYieldValue || 0)
    readonly property string baselineYieldMode: brewDialogSnapshot.activeBaselineYieldMode || "absolute"
    // True when a recipe or bag actually designs a yield; false = the ladder
    // bottomed out at the profile (baselineYieldMode reads "absolute" there,
    // but Clear must restore anchor mode "none", not arm an absolute).
    readonly property bool baselineIsStoreAnchor: brewDialogSnapshot.activeBaselineYieldSource !== "profile"
    readonly property double baselineStopAt: baselineYieldMode === "ratio"
        ? (doseValue > 0 ? baselineYieldValue * doseValue : 0)
        : baselineYieldValue
    readonly property double baselineRatio: baselineYieldMode === "ratio"
        ? baselineYieldValue
        : ((doseValue > 0 && baselineYieldValue > 0) ? baselineYieldValue / doseValue : 0)
    // Non-archived MRU recipe inventory (same source as the pill row), for the
    // quick-switch suggestions and the name→id resolution.
    property var recipeChoices: []
    property string selectedRecipeName: ""
    // Recipe id of an in-flight "Update Recipe" write, matched against the
    // recipeUpdated(recipeId, success) result (RecipesPage's pending-id
    // pattern) so a failure never looks like the tap was ignored.
    // Failure banner for the fire-and-forget recipe operations (update /
    // switch). Auto-dismisses — the allowed toast use of a timer.
    property string recipeErrorText: ""
    Timer {
        id: recipeErrorTimer
        interval: 4000
        onTriggered: {
            if (!root.staleBrewSettings)
                root.recipeErrorText = ""
        }
    }
    function showRecipeError(msg) {
        recipeErrorText = msg
        recipeErrorTimer.restart()
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled)
            AccessibilityManager.announce(msg, true)
    }

    function clearRecipeError() {
        recipeErrorTimer.stop()
        recipeErrorText = ""
    }

    function getRecipeSuggestions() {
        var names = []
        for (let i = 0; i < recipeChoices.length; i++)
            names.push(recipeChoices[i].name)
        return names
    }

    // Resolve a chosen name to a recipe id. Names can collide; prefer a match
    // whose id differs from the active one (so picking a duplicate name always
    // switches). Returns -1 when nothing matches.
    function recipeForId(id) {
        for (let i = 0; i < recipeChoices.length; i++) {
            if (Number(recipeChoices[i].id) === Number(id))
                return recipeChoices[i]
        }
        return brewDialogSnapshot.activeRecipe || ({})
    }

    function resolveRecipeId(name) {
        var fallback = -1
        for (let i = 0; i < recipeChoices.length; i++) {
            let r = recipeChoices[i]
            if (r.name === name) {
                if (r.id !== RecipeService.activeRecipeId)
                    return r.id
                if (fallback < 0)
                    fallback = r.id
            }
        }
        return fallback
    }

    // --- The single yield/ratio Update button (add-yield-ratio-anchor) ---
    // One persist button for the yield/ratio pair. It sits on whichever row
    // is anchored (its location IS the anchor indicator) and its destination
    // follows the resolution ladder: the active recipe when it designs a
    // yield (or has nothing to fall through to), else the active bag, else
    // nothing (hidden — the session anchor still applies to the brew). A
    // profile is never a destination: target_weight is absolute and profiles
    // are shared/exported ("Update Profile" for yield lives in the profile
    // editors now).
    readonly property string yieldPersistTarget: brewDialogSnapshot.yieldPersistTarget || ""
    readonly property string yieldPersistLabel: yieldPersistTarget === "recipe"
        ? TranslationManager.translate("brewDialog.updateRecipe", "Update Recipe")
        : TranslationManager.translate("brewDialog.updateBag", "Update Bag")
    // The DESTINATION store's own stored spec (not the ladder baseline) —
    // the button gates on the shown anchor differing from what that store
    // already holds, comparing like with like. A mode change alone enables
    // it: persisting it genuinely changes behaviour on the next dose change
    // even when the gram value is identical.
    readonly property double storedYieldValue: yieldPersistTarget === "recipe"
        ? Number(activeRecipe.yieldValue || 0)
        : Number(RecipeContext.preferences.activeBagYieldValue || 0)
    readonly property string storedYieldMode: yieldPersistTarget === "recipe"
        ? (activeRecipe.yieldMode || "none")
        : (RecipeContext.preferences.activeBagYieldMode || "none")
    readonly property bool yieldPersistEnabled: {
        if (yieldPersistTarget === "" || anchorMode === "none")
            return false
        if (anchorMode !== storedYieldMode)
            return true
        var shown = anchorMode === "ratio" ? ratio : targetValue
        if (anchorMode === "ratio") {
            // One tolerance unit, converted through the dose (0.1 g).
            let d = doseValue > 0 ? doseValue : 18
            return Math.abs(shown - storedYieldValue) * d > 0.1
        }
        return Math.abs(shown - storedYieldValue) > 0.1
    }
    function persistYieldAnchor() {
        if (!canWriteBrew)
            return
        var value = anchorMode === "ratio" ? ratio : targetValue
        if (yieldPersistTarget === "recipe") {
            const id = Number(brewSnapshot.activeRecipeId || -1)
            if (id < 0 || id !== RecipeService.activeRecipeId
                    || id !== _recipeEditableBaselineId || Object.keys(_recipeEditableBaseline).length === 0) {
                staleBrewSettings = true
                showRecipeError(TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again."))
                return
            }
            const requestId = RecipeService.requestUpdateRecipe(
                id, {"yieldValue": value, "yieldMode": anchorMode}, _recipeEditableBaseline)
            if (requestId) {
                const requests = Object.assign({}, _recipeUpdateRequests)
                requests[requestId] = {
                    kind: "recipe-yield",
                    patch: {yieldValue: value, yieldMode: anchorMode}
                }
                _recipeUpdateRequests = requests
            }
        } else if (yieldPersistTarget === "bag") {
            const bagId = _openedBagId
            if (bagId < 0 || Number(preferences.activeBagId || -1) !== bagId) {
                staleBrewSettings = true
                showRecipeError(TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again."))
                return
            }
            if (Object.keys(_openedBagYield).length !== 2) {
                showRecipeError(TranslationManager.translate("brewDialog.bagUpdateFailed", "Couldn't save to the beans"))
                return
            }
            const requestId = BagService.requestUpdateBag(
                bagId, {"yieldValue": value, "yieldMode": anchorMode}, false,
                _openedBagYield, bagId)
            if (requestId) {
                const requests = Object.assign({}, _bagUpdateRequests)
                requests[requestId] = {
                    yieldValue: value,
                    yieldMode: anchorMode
                }
                _bagUpdateRequests = requests
            }
        }
    }

    // Seed every dial-in field from the current DYE/profile/override state.
    // Called on open and after a recipe switch settles. Writes ONLY local
    // root.* values — never Settings — so re-seeding can't trigger a
    // dose/grind stamp on a just-activated recipe.
    function seedFromSnapshot(snapshot) {
        if (!snapshot || !snapshot.revision)
            return
        profileTemperature = Number(snapshot.profileTemperatureC || 0)
        profileTargetWeight = Number(snapshot.profileTargetWeightG || 0)
        temperatureValue = snapshot.hasTemperatureOverride
            ? Number(snapshot.temperatureC || 0) : profileTemperature
        doseValue = Number(snapshot.doseG) > 0 ? Number(snapshot.doseG) : 18.0
        grindSetting = snapshot.grindSetting || ""
        grindRpm = Number(snapshot.grindRpm || 0)
        selectedProfileTitle = currentProfile.title || ""
        selectedRecipeName = recipeActive ? (activeRecipe.name || "") : ""
        staleBrewSettings = false
        _openedBagId = Number(snapshot.activeBagId || -1)
        _openedBagYield = {
            yieldValue: Number(snapshot.activeBagYieldValue || 0),
            yieldMode: snapshot.activeBagYieldMode || "none"
        }
        captureRecipeEditableBaseline()
        _observedBeanWeight = Number(preferences.dyeBeanWeight || 0)
        seedYieldFromSnapshot(snapshot)
        _openedDraft = draftSnapshot()
    }

    function draftSnapshot() {
        return {
            doseValue: doseValue,
            ratio: ratio,
            targetValue: targetValue,
            anchorMode: anchorMode,
            temperatureValue: temperatureValue,
            grindSetting: grindSetting,
            grindRpm: grindRpm
        }
    }

    function captureRecipeEditableBaseline() {
        const id = Number(brewSnapshot.activeRecipeId || -1)
        _recipeEditableBaselineId = -1
        _recipeEditableBaseline = ({})
        if (id < 0) {
            _recipeEditableBaselineId = -1
            _recipeEditableBaseline = ({})
            return
        }
        const row = recipeForId(id)
        if (Number(row.id) === id) {
            _recipeEditableBaselineId = id
            _recipeEditableBaseline = RecipeService.editableSnapshot(row)
        }
    }

    // The one place the stored anchor mode enters the dialog: a ratio-anchored
    // session opens ratio-first; "none" shows the profile's target, unanchored.
    function seedYieldFromSnapshot(snapshot) {
        anchorMode = snapshot.yieldMode || "none"
        if (anchorMode === "ratio") {
            ratio = Number(snapshot.yieldValue || snapshot.lastUsedRatio || 2.0)
            targetValue = doseValue > 0 ? doseValue * ratio : profileTargetWeight
        } else if (anchorMode === "absolute") {
            targetValue = Number(snapshot.yieldValue || 0)
            ratio = doseValue > 0 ? targetValue / doseValue : Number(snapshot.lastUsedRatio || 2.0)
        } else {
            targetValue = profileTargetWeight
            ratio = doseValue > 0 && targetValue > 0 ? targetValue / doseValue
                                                     : Number(snapshot.lastUsedRatio || 2.0)
        }
    }

    function getProfileSuggestions() {
        var profiles = ProfileService.availableProfiles
        var titles = []
        for (let i = 0; i < profiles.length; i++)
            titles.push(profiles[i].title)
        return titles
    }

    function loadProfileByTitle(title) {
        var filename = ProfileService.findProfileByTitle(title)
        if (filename.length > 0) {
            const groupBefore = currentProfile.beverageGroup || ""
            const requestId = ProfileService.loadProfile(filename)
            if (requestId) {
                const requests = Object.assign({}, _profileLoadRequests)
                requests[requestId] = {filename: filename, groupBefore: groupBefore}
                _profileLoadRequests = requests
                _lastDialogLoadedProfileFilename = filename
            }
        }
    }

    function requestBrewSnapshot(seed, rebaseKind, rebaseExpectation) {
        if (!MachineControlService.ready) {
            brewSnapshotLoading = false
            showRecipeError(TranslationManager.translate("brewDialog.serviceUnavailable", "Brew settings are unavailable. Reconnect and try again."))
            return
        }
        brewSnapshotLoading = true
        const id = MachineControlService.requestBrewSettings()
        if (!id) {
            brewSnapshotLoading = false
            showRecipeError(TranslationManager.translate("brewDialog.serviceUnavailable", "Brew settings are unavailable. Reconnect and try again."))
            return
        }
        _snapshotRequestId = id
        _seedOnSnapshot = seed
        _rebaseOnSnapshot = rebaseKind || ""
        _rebaseExpectation = rebaseExpectation || ({})
    }

    property bool _seedOnSnapshot: false

    function updateFromBrewSnapshot(snapshot, seed, rebaseKind, rebaseExpectation) {
        if (!snapshot || !snapshot.revision)
            return
        const previousRecipeId = Number(brewSnapshot.activeRecipeId || -1)
        const previousBagId = _openedBagId
        rebaseKind = rebaseKind || ""
        if (!seed && rebaseKind.length > 0) {
            if (!canRebaseSnapshot(brewSnapshot, snapshot, rebaseKind,
                                   rebaseExpectation || ({}))) {
                staleBrewSettings = true
                showRecipeError(TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again."))
                return
            }
            brewSnapshot = snapshot
            brewDialogSnapshot = snapshot.brewDialog || ({})
            brewRevision = snapshot.revision
            staleBrewSettings = false
            return
        }
        brewSnapshot = snapshot
        brewDialogSnapshot = snapshot.brewDialog || ({})
        brewRevision = snapshot.revision
        if (seed) {
            staleBrewSettings = false
            recipeErrorText = ""
            seedFromSnapshot(snapshot)
        } else {
            const target = yieldPersistTarget
            if ((target === "recipe" && Number(snapshot.activeRecipeId) !== previousRecipeId)
                    || (target === "bag" && Number(snapshot.activeBagId) !== previousBagId))
                staleBrewSettings = true
        }
    }

    function canRebaseSnapshot(previous, next, kind, expected) {
        if (!previous || !previous.revision || !next || !next.revision)
            return false
        const allowed = {
            "recipe-yield": ["brewDialog.activeBaselineYieldValue", "brewDialog.activeBaselineYieldMode",
                             "brewDialog.activeRecipe.yieldValue", "brewDialog.activeRecipe.yieldMode",
                             "yieldValue", "yieldMode"],
            "bag-yield": ["activeBagYieldValue", "activeBagYieldMode",
                           "brewDialog.activeBaselineYieldValue", "brewDialog.activeBaselineYieldMode"],
            "recipe-temperature": ["brewDialog.activeBaselineTemperatureC",
                                   "yieldValue", "yieldMode",
                                   "hasTemperatureOverride", "temperatureC"],
            "profile-temperature": ["profileTemperatureC", "profileContentSha256",
                                     "hasTemperatureOverride", "temperatureC",
                                     "profileLoadGeneration", "brewDialog.activeBaselineTemperatureC"]
        }[kind]
        if (!allowed)
            return false
        const left = JSON.parse(JSON.stringify(previous))
        const right = JSON.parse(JSON.stringify(next))
        delete left.revision
        delete right.revision
        for (let i = 0; i < allowed.length; i++) {
            const path = allowed[i].split(".")
            let lhs = left
            let rhs = right
            for (let j = 0; j < path.length - 1; j++) {
                lhs = lhs && lhs[path[j]]
                rhs = rhs && rhs[path[j]]
            }
            if (lhs)
                delete lhs[path[path.length - 1]]
            if (rhs)
                delete rhs[path[path.length - 1]]
        }
        if (JSON.stringify(left) !== JSON.stringify(right))
            return false
        const dialog = next.brewDialog || ({})
        const activeRecipe = dialog.activeRecipe || ({})
        const recipeYieldMode = activeRecipe.yieldMode || "none"
        const recipeYieldValue = Number(activeRecipe.yieldValue || 0)
        const profileTarget = Number(next.profileTargetWeightG || 0)
        var appliedYieldMode = "none"
        var appliedYieldValue = 0
        if (recipeYieldMode === "ratio" && recipeYieldValue > 0) {
            appliedYieldMode = "ratio"
            appliedYieldValue = recipeYieldValue
        } else if (recipeYieldMode === "absolute" && recipeYieldValue > 0
                   && Math.abs(recipeYieldValue - profileTarget) > 0.1) {
            appliedYieldMode = "absolute"
            appliedYieldValue = recipeYieldValue
        }
        if (kind === "recipe-yield")
            return Number(activeRecipe.yieldValue) === Number(expected.yieldValue)
                && activeRecipe.yieldMode === expected.yieldMode
                && Number(dialog.activeBaselineYieldValue) === Number(expected.yieldValue)
                && dialog.activeBaselineYieldMode === expected.yieldMode
                && next.yieldMode === appliedYieldMode
                && Math.abs(Number(next.yieldValue) - appliedYieldValue) < 0.001
        if (kind === "bag-yield")
            return Number(next.activeBagYieldValue) === Number(expected.yieldValue)
                && next.activeBagYieldMode === expected.yieldMode
                && Number(dialog.activeBaselineYieldValue) === Number(expected.yieldValue)
                && dialog.activeBaselineYieldMode === expected.yieldMode
        if (kind === "recipe-temperature") {
            var profileTemperature = Number(next.profileTemperatureC)
            var tempOffset = Number(expected.tempOffsetC)
            const shouldOverrideTemperature = profileTemperature > 0 && Math.abs(tempOffset) > 0.05
            const expectedTemperature = shouldOverrideTemperature
                ? profileTemperature + tempOffset : 0
            return next.yieldMode === appliedYieldMode
                && Math.abs(Number(next.yieldValue) - appliedYieldValue) < 0.001
                && next.hasTemperatureOverride === shouldOverrideTemperature
                && Math.abs(Number(next.temperatureC) - expectedTemperature) < 0.001
                && Math.abs(Number(dialog.activeBaselineTemperatureC)
                            - (profileTemperature > 0 ? profileTemperature + tempOffset : profileTemperature)) < 0.001
        }
        if (kind === "profile-temperature")
            return Math.abs(Number(next.profileTemperatureC) - Number(expected.valueC)) < 0.001
                && next.hasTemperatureOverride === false
                && Math.abs(Number(dialog.activeBaselineTemperatureC) - Number(expected.valueC)) < 0.001
        return false
    }

    function advanceRecipeBaseline(patch) {
        if (_recipeEditableBaselineId < 0 || Object.keys(_recipeEditableBaseline).length === 0)
            return
        const updated = Object.assign({}, _recipeEditableBaseline)
        Object.keys(patch).forEach(function(key) { updated[key] = patch[key] })
        _recipeEditableBaseline = updated
    }

    function queuePreferenceWrite(name, value) {
        const queued = Object.assign({}, _queuedPreferences)
        queued[name] = value
        _queuedPreferences = queued
        flushPreferenceWrites()
    }

    function flushPreferenceWrites() {
        if (_preferenceWriteId || !RecipeContext.ready)
            return
        const names = Object.keys(_queuedPreferences)
        if (!names.length)
            return
        const name = names[0]
        const id = RecipeContext.newRequestId()
        const queued = Object.assign({}, _queuedPreferences)
        const value = queued[name]
        _preferenceWriteId = id
        _preferenceWriteName = name
        _preferenceRequests[id] = {name: name, value: value}
        RecipeContext.setPreference(id, name, value, RecipeContext.revision)
    }

    function saveApply() {
        if (!MachineControlService.ready || brewSnapshotLoading || hostWritePending
                || staleBrewSettings || !brewRevision)
            return
        Keyboard.commit()
        const id = MachineControlService.applyBrewSettings(brewRevision, {
            doseG: doseValue,
            grindSetting: grindSetting,
            grindRpm: grindRpm,
            yieldMode: anchorMode,
            yieldValue: anchorMode === "ratio" ? ratio : targetValue,
            temperatureC: temperatureValue
        }, SteamService.state.controlContextId)
        if (!id) {
            showRecipeError(TranslationManager.translate("brewDialog.applyFailed", "Couldn't apply brew settings"))
            return
        }
        _applyRequestId = id
        _applyPending = true
    }

    // Low dose warning - shown when dose is low OR when scale read failed
    property bool showScaleWarning: false
    property bool lowDoseWarning: doseValue < 3 || showScaleWarning

    // Bean auto-capture is owned by the host; reflect a new measured dose into
    // this dialog without changing the selected yield anchor.
    Connections {
        target: RecipeContext
        function onStateChanged() { root.flushPreferenceWrites() }
        function onPreferencesChanged() {
            if (!root.visible)
                return
            const beanWeight = Number(RecipeContext.preferences.dyeBeanWeight || 0)
            if (beanWeight !== root._observedBeanWeight) {
                root._observedBeanWeight = beanWeight
                if (beanWeight > 0)
                    root.doseValue = beanWeight
            }
        }
        function onPreferenceCompleted(requestId, name, success, currentPreferences, reason) {
            const pending = root._preferenceRequests[requestId]
            if (!pending)
                return
            const requests = Object.assign({}, root._preferenceRequests)
            delete requests[requestId]
            root._preferenceRequests = requests
            const queued = Object.assign({}, root._queuedPreferences)
            // Never replay a failed CAS. Keep only a newer value the user
            // explicitly entered while this request was pending.
            if (queued[name] === pending.value)
                delete queued[name]
            root._queuedPreferences = queued
            if (root._preferenceWriteId === requestId) {
                root._preferenceWriteId = ""
                root._preferenceWriteName = ""
            }
            if (!success && root.visible)
                root.showRecipeError(TranslationManager.translate("brewDialog.preferenceWriteFailed", "Couldn't save this brew setting. Review it and try again."))
            root.flushPreferenceWrites()
        }
    }

    // A dose change re-derives the NON-anchored quantity: under a ratio
    // anchor the stop-at follows (dose x ratio); under an absolute anchor the
    // stop-at holds and the displayed ratio drifts; unanchored, the stop-at
    // is the profile's and the ratio display derives from it.
    onDoseValueChanged: {
        if (anchorMode === "ratio") {
            targetValue = doseValue * ratio
        } else if (anchorMode === "absolute") {
            if (doseValue > 0)
                ratio = targetValue / doseValue
        } else if (doseValue > 0 && targetValue > 0) {
            ratio = targetValue / doseValue
        }
    }

    onRejected: {
        // Restore the original profile if the user changed it via the profile
        // picker. Inert in recipe mode: the recipe owns the profile and the
        // picker is hidden, so there is nothing of the user's to undo.
        const currentFilename = ProfileService.currentProfileSummary.filename || ""
        if (!recipeActive && originalProfileFilename.length > 0
                && _lastDialogLoadedProfileFilename.length > 0
                && currentFilename === _lastDialogLoadedProfileFilename
                && currentFilename !== originalProfileFilename) {
            ProfileService.loadProfile(originalProfileFilename)
        }
        _lastDialogLoadedProfileFilename = ""
    }

    onAboutToShow: {
        scaleVirtualZero = PreparationService.state.bean.virtualZero
        // Announce dialog for accessibility
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
            let announcement = TranslationManager.translate("brewDialog.dialogAnnouncement", "Brew Settings dialog. Profile: ") + (currentProfile.title || "")
            if ((preferences.dyeBeanBrand || "").length > 0)
                announcement += ". " + TranslationManager.translate("brewDialog.roasterAnnouncementLabel", "Roaster: ") + preferences.dyeBeanBrand
            if ((preferences.dyeBeanType || "").length > 0)
                announcement += ". " + TranslationManager.translate("brewDialog.coffeeAnnouncementLabel", "Coffee: ") + preferences.dyeBeanType
            AccessibilityManager.announce(announcement)
        }

        originalProfileFilename = currentProfile.filename || ""
        _lastDialogLoadedProfileFilename = ""
        showScaleWarning = false
        recipeErrorText = ""
        _applyPending = false
        _applyRequestId = ""
        _pendingRecipeSwitchId = ""
        _rebaseOnSnapshot = ""
        _rebaseExpectation = ({})
        brewRevision = ""
        _recipeEditableBaselineId = -1
        _recipeEditableBaseline = ({})
        if (RecipeService.ready)
            RecipeService.requestInventory()
        requestBrewSnapshot(true)
    }

    onClosed: {
        // Late replies from a previous opening must not seed or dismiss a new draft.
        _snapshotRequestId = ""
        _seedOnSnapshot = false
        _rebaseOnSnapshot = ""
        _rebaseExpectation = ({})
        _pendingProfileSeed = ({})
        _applyRequestId = ""
        _applyPending = false
        _profileTemperatureRequestId = ""
        _recipeUpdateRequests = ({})
        _bagUpdateRequests = ({})
        _pendingRecipeSwitches = ({})
        _profileLoadRequests = ({})
    }

    // Keep the quick-switch list on the shared canonical recipe inventory.
    Connections {
        target: RecipeService
        function onInventoryReady(recipes) {
            root.recipeChoices = recipes
            if (root.visible && root._recipeEditableBaselineId < 0)
                root.captureRecipeEditableBaseline()
        }
        function onRecipesChanged() {
            root.recipeChoices = RecipeService.recipes
            if (root.visible && root._recipeEditableBaselineId < 0)
                root.captureRecipeEditableBaseline()
        }
        function onOperationCompleted(requestId, operation, success, resultId, reason) {
            if (root._recipeUpdateRequests[requestId] !== undefined) {
                const update = root._recipeUpdateRequests[requestId]
                const requests = Object.assign({}, root._recipeUpdateRequests)
                delete requests[requestId]
                root._recipeUpdateRequests = requests
                if (!success && root.visible) {
                    if (reason === "conflict") {
                        root.staleBrewSettings = true
                        root.showRecipeError(TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again."))
                    } else {
                        root.showRecipeError(reason === "busy"
                            ? TranslationManager.translate("brewDialog.recipeUpdateBusy", "The database was busy — tap Update Recipe again.")
                            : TranslationManager.translate("brewDialog.recipeUpdateFailed", "Couldn't save to the recipe"))
                    }
                } else if (success && root.visible) {
                    root.staleBrewSettings = true
                    root.advanceRecipeBaseline(update.patch)
                    root.clearRecipeError()
                    root.requestBrewSnapshot(false, update.kind, update.patch)
                }
                return
            }
            if (root._pendingRecipeSwitches[requestId] !== undefined) {
                const pending = root._pendingRecipeSwitches[requestId]
                const switches = Object.assign({}, root._pendingRecipeSwitches)
                delete switches[requestId]
                root._pendingRecipeSwitches = switches
                if (root._pendingRecipeSwitchId === requestId)
                    root._pendingRecipeSwitchId = ""
                if (root.visible) {
                    if (success)
                        root.requestBrewSnapshot(true)
                    else {
                        root.selectedRecipeName = root.activeRecipe.name || ""
                        root.showRecipeError(TranslationManager.translate("brewDialog.recipeSwitchFailed", "Couldn't switch to that recipe"))
                    }
                }
            }
        }
    }

    Connections {
        target: ProfileService
        function onOperationCompleted(requestId, operation, filename, success, reason, result) {
            const pending = root._profileLoadRequests[requestId]
            if (!pending)
                return
            const requests = Object.assign({}, root._profileLoadRequests)
            delete requests[requestId]
            root._profileLoadRequests = requests
            if (!success) {
                root.selectedProfileTitle = root.currentProfile.title || ""
                if (root.visible)
                    root.showRecipeError(TranslationManager.translate("brewDialog.profileLoadFailed", "Couldn't load that profile"))
                return
            }
            if (!root.visible)
                return
            root._pendingProfileSeed = pending
            root.requestBrewSnapshot(false)
        }
    }

    Connections {
        target: BagService
        function onOperationCompleted(requestId, operation, bagId, success, result, reason) {
            if (root._bagUpdateRequests[requestId] === undefined)
                return
            const update = root._bagUpdateRequests[requestId]
            const requests = Object.assign({}, root._bagUpdateRequests)
            delete requests[requestId]
            root._bagUpdateRequests = requests
            if (!root.visible)
                return
            if (!success) {
                const stale = reason === "conflict" || reason === "staleData"
                if (stale)
                    root.staleBrewSettings = true
                root.showRecipeError(stale
                    ? TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again.")
                    : TranslationManager.translate("brewDialog.bagUpdateFailed", "Couldn't save to the beans"))
            } else {
                root.staleBrewSettings = true
                root.clearRecipeError()
                root.requestBrewSnapshot(false, "bag-yield", update)
            }
        }
    }

    Connections {
        target: MachineControlService
        function onCompleted(requestId, operation, success, message, result) {
            if (requestId === root._snapshotRequestId) {
                root._snapshotRequestId = ""
                root.brewSnapshotLoading = false
                if (!success || !result.snapshot) {
                    root._rebaseOnSnapshot = ""
                    root._rebaseExpectation = ({})
                    if (root.visible)
                        root.showRecipeError(TranslationManager.translate("brewDialog.snapshotFailed", "Couldn't read current brew settings"))
                    return
                }
                const seed = root._seedOnSnapshot
                const rebaseKind = root._rebaseOnSnapshot
                const rebaseExpectation = root._rebaseExpectation
                root._seedOnSnapshot = false
                root._rebaseOnSnapshot = ""
                root._rebaseExpectation = ({})
                root.updateFromBrewSnapshot(result.snapshot, seed, rebaseKind, rebaseExpectation)
                if (Object.keys(root._pendingProfileSeed).length > 0) {
                    const pending = root._pendingProfileSeed
                    root._pendingProfileSeed = ({})
                    root.profileTemperature = Number(result.snapshot.profileTemperatureC || 0)
                    root.profileTargetWeight = Number(result.snapshot.profileTargetWeightG || 0)
                    root.temperatureValue = root.profileTemperature
                    const newGroup = root.currentProfile.beverageGroup || ""
                    if (!(root.anchorMode === "ratio" && pending.groupBefore === newGroup))
                        root.seedYieldFromSnapshot(result.snapshot)
                }
                return
            }
            if (requestId === root._applyRequestId) {
                root._applyRequestId = ""
                root._applyPending = false
                if (success) {
                    if (result.snapshot)
                        root.updateFromBrewSnapshot(result.snapshot, false)
                    root.accept()
                } else if (root.visible) {
                    if (result.status === "conflict")
                        root.staleBrewSettings = true
                    root.showRecipeError(result.status === "conflict"
                        ? TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again.")
                        : TranslationManager.translate("brewDialog.applyFailed", "Couldn't apply brew settings"))
                }
                return
            }
            if (requestId === root._profileTemperatureRequestId) {
                root._profileTemperatureRequestId = ""
                if (success) {
                    root.staleBrewSettings = true
                    if (result.snapshot)
                        root.updateFromBrewSnapshot(result.snapshot, false, "profile-temperature",
                                                    {valueC: root._profileTemperatureTarget})
                    if (root.visible && result.saved === false && !root.staleBrewSettings)
                        root.showRecipeError(TranslationManager.translate("brewDialog.profileTemperatureAppliedNotSaved", "Temperature applied, but the profile could not be saved to disk."))
                    else if (root.visible && !root.staleBrewSettings)
                        root.clearRecipeError()
                } else if (root.visible) {
                    if (result.status === "conflict")
                        root.staleBrewSettings = true
                    root.showRecipeError(result.status === "conflict"
                        ? TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again.")
                        : TranslationManager.translate("brewDialog.profileTemperatureFailed", "Couldn't save the profile temperature"))
                }
            }
        }
    }

    // The named-ratio chooser, opened from the Ratio row's 1:X.X readout.
    // PICK-ONLY: it fills this dialog's local dial (anchoring the ratio, per
    // the last-written rule) rather than arming the session — the session is
    // armed by OK, like every other field here.
    RatioPresetDialog {
        id: brewRatioPicker
        pickOnly: true
        compareRatio: root.ratio
        onRatioPicked: function(r) {
            root.anchorMode = "ratio"
            root.ratio = r
            if (root.doseValue > 0)
                root.targetValue = root.doseValue * r
        }
    }

    SwitchEquipmentDialog {
        id: switchEquipmentDialog
    }

    EquipmentInfoDialog {
        id: equipmentInfoDialog
    }

    background: Rectangle {
        color: Theme.dialogBackgroundColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: KeyboardAwareContainer {
        id: keyboardContainer
        implicitHeight: Math.min(mainColumn.implicitHeight,
                                 root.parent ? root.parent.height * 0.9 : mainColumn.implicitHeight)
        implicitWidth: root._frameWidth
        inOverlay: true
        textFields: [
            profileInput.textField, recipeInput.textField
        ]
        targetFlickable: brewFlickable

        Flickable {
            id: brewFlickable
            anchors.fill: parent
            contentHeight: mainColumn.implicitHeight
                           + keyboardContainer.estimatedKeyboardHeight
            contentWidth: parent.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: mainColumn
            width: root._frameWidth
            spacing: 0

        // Header — title + the primary actions (Clear / Cancel / OK) pinned at
        // the top next to the title. At standard size the fields fit without
        // scrolling, so keeping the actions here means they never scroll off.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)
            Layout.topMargin: Theme.scaled(10)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(20)
                anchors.rightMargin: Theme.scaled(8)
                spacing: Theme.scaled(8)

                Text {
                    // Takes the slack (pushing the actions right) and elides so a
                    // long localized title can't crowd or clip the buttons.
                    Layout.fillWidth: true
                    text: TranslationManager.translate("brewDialog.title", "Brew Settings")
                    font: Theme.titleFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true  // Dialog title announced on open
                }

                // Clear all overrides
                AccessibleButton {
                    id: clearButton
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate("brewDialog.clear", "Clear")
                    accessibleName: TranslationManager.translate("brewDialog.clearAllOverrides", "Clear all overrides")
                    onClicked: {
                        // Reset to the latest host snapshot, not a stale open-time copy.
                        root.profileTemperature = Number(root.brewSnapshot.profileTemperatureC || 0)
                        root.profileTargetWeight = Number(root.brewSnapshot.profileTargetWeightG || 0)
                        // Reset the dose to the active bag's dose (the bean's remembered
                        // weight), otherwise default 18 g.
                        root.doseValue = Number(root.brewSnapshot.doseG) > 0 ? Number(root.brewSnapshot.doseG) : 18.0
                        root.selectedProfileTitle = root.currentProfile.title || ""
                        root.grindSetting = root.brewSnapshot.grindSetting || ""
                        root.grindRpm = Number(root.brewSnapshot.grindRpm || 0)
                        // Clear returns each override field to the ACTIVE baseline —
                        // the recipe's/bag's own spec when one designs a yield, the
                        // profile default otherwise. It only strips per-brew
                        // deviations; it never edits the stored values (that is the
                        // Update button's job). The restore is MODE-AWARE: a ratio
                        // store restores {ratio, mode ratio} — the old code restored
                        // activeRecipe.yieldG and would restore nothing for a
                        // ratio-anchored recipe.
                        root.temperatureValue = root.recipeTempBaseline
                        if (root.baselineIsStoreAnchor) {
                            root.anchorMode = root.baselineYieldMode
                            if (root.baselineYieldMode === "ratio") {
                                root.ratio = root.baselineYieldValue
                                root.targetValue = root.doseValue > 0
                                    ? root.doseValue * root.ratio : root.targetValue
                            } else {
                                root.targetValue = root.baselineYieldValue
                                root.ratio = root.doseValue > 0
                                    ? root.targetValue / root.doseValue : Number(root.brewSnapshot.lastUsedRatio || 2.0)
                            }
                        } else {
                            root.anchorMode = "none"
                            let profileTarget = Number(root.brewSnapshot.profileTargetWeightG || 0)
                            root.ratio = (profileTarget > 0 && root.doseValue > 0) ? profileTarget / root.doseValue : Number(root.brewSnapshot.lastUsedRatio || 2.0)
                            root.targetValue = profileTarget > 0 ? profileTarget : root.doseValue * root.ratio
                        }
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.warningColor
                    }
                    contentItem: Text {
                        text: clearButton.text
                        font: Theme.bodyFont
                        color: Theme.warningColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Cancel
                AccessibleButton {
                    id: cancelButton
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate("brewDialog.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("brewDialog.cancelBrewSettings", "Cancel brew settings")
                    enabled: !root.hostWritePending
                    onClicked: root.reject()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondaryColor
                    }
                    contentItem: Text {
                        text: cancelButton.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // OK (primary)
                AccessibleButton {
                    id: okButton
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate("brewDialog.ok", "OK")
                    accessibleName: TranslationManager.translate("brewDialog.confirmBrewSettings", "Confirm brew settings")
                    enabled: root.canWriteBrew
                    onClicked: {
                        root.saveApply()
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.buttonRadius
                        color: okButton.down || okButton.isPressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: okButton.text
                        font: Theme.bodyFont
                        color: Theme.primaryContrastColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                HideKeyboardButton {
                    Layout.alignment: Qt.AlignVCenter
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

        // Profile selector — hidden in recipe mode (the recipe owns the profile;
        // an invisible layout child takes no vertical space, so the row collapses).
        RowLayout {
            visible: !root.recipeActive
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(12)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.profileLabel", "Profile:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: Theme.scaled(75)
                Accessible.ignored: true
            }

            SuggestionField {
                id: profileInput
                Layout.fillWidth: true
                enabled: root.canWriteBrew
                label: ""
                accessibleName: TranslationManager.translate("brewDialog.profile", "Profile")
                text: root.selectedProfileTitle
                suggestions: root.getProfileSuggestions()
                onTextEdited: function(t) {
                    root.selectedProfileTitle = t
                    // Only load profile on exact match (e.g. suggestion selection),
                    // not partial typing that might accidentally match a short title
                    if (root.getProfileSuggestions().indexOf(t) >= 0)
                        root.loadProfileByTitle(t)
                }
            }
        }

        // Recipe quick-switch — the Profile row's slot in recipe mode. Picking a
        // different recipe re-activates through the single activation path
        // (MainController.activateRecipe); the dial-in fields re-seed when
        // activeRecipeId settles on the new recipe.
        RowLayout {
            visible: root.recipeActive
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(12)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.recipeLabel", "Recipe:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: Theme.scaled(75)
                Accessible.ignored: true
            }

            SuggestionField {
                id: recipeInput
                Layout.fillWidth: true
                enabled: root.canWriteBrew
                label: ""
                accessibleName: TranslationManager.translate("brewDialog.recipe", "Recipe")
                text: root.selectedRecipeName
                suggestions: root.getRecipeSuggestions()
                onTextEdited: function(t) {
                    root.selectedRecipeName = t
                    // The field re-emits its unchanged text on blur, so the active
                    // recipe's own name is never a switch — otherwise a stray tap
                    // into the field would activate a same-named twin (the resolver
                    // prefers a non-active id on duplicates). A twin sharing the
                    // active name is unreachable from a name-only control anyway.
                    if (t === (root.activeRecipe.name || ""))
                        return
                    // Only switch on an exact name match (suggestion selection or a
                    // fully typed name); re-selecting the active recipe is a no-op.
                    var id = root.resolveRecipeId(t)
                    if (id >= 0 && id !== RecipeService.activeRecipeId && !root._pendingRecipeSwitchId) {
                        const requestId = RecipeService.requestActivateRecipe(id)
                        if (requestId) {
                            root._pendingRecipeSwitchId = requestId
                            const switches = Object.assign({}, root._pendingRecipeSwitches)
                            switches[requestId] = id
                            root._pendingRecipeSwitches = switches
                        }
                    }
                }
            }
        }

        // Beans: read-only summary of the active bag + Change Beans — hidden in
        // recipe mode (the recipe owns the bean link).
        RowLayout {
            visible: !root.recipeActive
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(8)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.beansLabel", "Beans:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: Theme.scaled(75)
                Accessible.ignored: true
            }

            BeanSummary {
                id: brewBeanSummary
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }

            AccessibleButton {
                Layout.preferredHeight: Theme.scaled(44)
                Layout.alignment: Qt.AlignVCenter
                text: brewBeanSummary.hasBeans
                    ? TranslationManager.translate("beans.button.change", "Change Beans")
                    : TranslationManager.translate("beans.button.select", "Select Beans")
                accessibleName: TranslationManager.translate("beans.button.accessible.change", "Change the selected beans")
                onClicked: brewChangeBeansDialog.open()
            }

            ChangeBeansDialog {
                id: brewChangeBeansDialog
                context: "brew"
            }
        }

        // Content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(8)
            spacing: Theme.scaled(8)

            // Recipe operation failure banner (update/switch) — auto-dismisses.
            Rectangle {
                Layout.fillWidth: true
                visible: root.recipeErrorText.length > 0
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.errorColor
                radius: Theme.scaled(8)
                implicitHeight: Math.max(recipeErrorLabel.implicitHeight,
                                         reloadBrewButton.visible ? reloadBrewButton.implicitHeight : 0)
                                + Theme.scaled(24)

                Accessible.role: Accessible.AlertMessage
                Accessible.name: recipeErrorLabel.text
                Accessible.focusable: true

                Text {
                    id: recipeErrorLabel
                    anchors.left: parent.left
                    anchors.right: reloadBrewButton.visible ? reloadBrewButton.left : parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(12)
                    text: root.recipeErrorText
                    font: Theme.bodyFont
                    color: Theme.errorColor
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true  // Parent handles accessibility
                }

                AccessibleButton {
                    id: reloadBrewButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: Theme.scaled(8)
                    visible: root.staleBrewSettings
                    enabled: !root.brewSnapshotLoading
                    text: TranslationManager.translate("brewDialog.reloadCurrent", "Reload")
                    accessibleName: TranslationManager.translate("brewDialog.reloadCurrentAccessible", "Discard local changes and reload current brew settings")
                    onClicked: root.requestBrewSnapshot(true)
                }
            }

            // Low dose warning
            Rectangle {
                Layout.fillWidth: true
                visible: root.lowDoseWarning
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.warningColor
                radius: Theme.scaled(8)
                implicitHeight: warningText.implicitHeight + Theme.scaled(24)

                // Accessibility: announce warning and make it focusable
                Accessible.role: Accessible.AlertMessage
                Accessible.name: warningText.text
                Accessible.focusable: true

                // Announce warning when it becomes visible
                onVisibleChanged: {
                    if (visible && typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
                        AccessibilityManager.announce(TranslationManager.translate("brewDialog.warningPrefix", "Warning: ") + warningText.text)
                    }
                }

                Text {
                    id: warningText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(12)
                    text: TranslationManager.translate("brewDialog.scaleWarning", "Please put the portafilter with coffee on the scale")
                    font: Theme.bodyFont
                    color: Theme.warningColor
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true  // Parent handles accessibility
                }
            }

            // Temperature input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: TranslationManager.translate("brewDialog.tempDeltaLabel", "Temp Delta:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                        // "Temp Delta:" is wider than the shared 75px column, so size
                        // to content (with a 75px floor) like the Equipment label,
                        // rather than a fixed 75px that clips it and lets the input
                        // butt right against it. The RowLayout spacing adds the gap.
                        Layout.minimumWidth: Theme.scaled(75)
                        Accessible.ignored: true  // Label for sighted users; input has accessibleName
                    }

                    // The control is an OFFSET applied to the whole profile, not an
                    // absolute temperature: it reads 0° at the active BASELINE and
                    // +N°/-N° when adjusted. The baseline is the recipe's own
                    // temperature when a recipe is active (its yield/temp are the
                    // recipe's design, not a deviation), else the profile default —
                    // recipeTempBaseline folds both. temperatureValue stays absolute
                    // internally so the OK / Update paths are unchanged; only the
                    // presentation is a delta. In no-recipe mode recipeTempBaseline ==
                    // profileTemperature, so this is byte-identical to before.
                    ValueInput {
                        id: tempInput
                        Layout.fillWidth: true
                        // The offset is entered/shown in the user's unit (a delta scales
                        // ×9/5 for °F, no origin shift); stored back as a Celsius delta.
                        readonly property real delta: root.temperatureValue - root.recipeTempBaseline
                        readonly property real displayDelta: Theme.cDeltaToDisplay(delta)
                        // Overridden ⟺ Clear would change it (Clear restores delta 0
                        // relative to the recipe baseline).
                        readonly property bool overridden: Math.abs(delta) > 0.1
                        value: displayDelta
                        from: Theme.cDeltaToDisplay(70 - root.recipeTempBaseline)
                        to: Theme.cDeltaToDisplay(100 - root.recipeTempBaseline)
                        stepSize: 1
                        decimals: 0
                        suffix: "°"
                        displayText: (displayDelta > 0 ? "+" : "") + displayDelta.toFixed(0) + "°"
                        valueColor: overridden ? Theme.highlightColor : Theme.textColor
                        accentColor: overridden ? Theme.highlightColor : Theme.primaryColor
                        accessibleName: TranslationManager.translate("brewDialog.brewTempDelta", "Brew temperature offset")
                        onValueModified: function(newValue) {
                            root.temperatureValue = root.recipeTempBaseline + Theme.displayToCDelta(newValue)
                        }
                    }

                    // Save the shown temperature to the baseline: the profile normally,
                    // the active recipe's tempOffsetC in recipe mode (writing the
                    // shared profile there would leak into every recipe on it).
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(44)
                        text: root.recipeActive
                            ? TranslationManager.translate("brewDialog.updateRecipe", "Update Recipe")
                            : TranslationManager.translate("brewDialog.updateProfile", "Update Profile")
                        accessibleName: root.recipeActive
                            ? TranslationManager.translate("brewDialog.saveTemperatureToRecipe", "Save temperature to recipe")
                            : TranslationManager.translate("brewDialog.saveTemperatureToProfile", "Save temperature to profile")
                        primary: true
                        // Enabled ⟺ the value deviates from the active baseline —
                        // exactly the field's override-highlight state. Reusing
                        // `overridden` (which measures against recipeTempBaseline, so it
                        // handles an unset offset by falling back to the profile,
                        // and collapses to the profile in no-recipe mode) keeps the
                        // invariant "Update enabled ⟺ value highlighted" and lets the
                        // recipe baseline move to any value, including back to the
                        // profile default (the reporter's "reset-to-default greyed out
                        // Update Recipe" bug). Comparing against the raw stored value
                        // instead wrongly enabled this at delta 0 when the override was
                        // unset (stored 0 vs the profile temperature).
                        enabled: tempInput.overridden && root.canWriteBrew
                        onClicked: {
                            if (root.recipeActive) {
                                // Persist the OFFSET (dialed − profile temp), never the
                                // absolute (recipe-relative-temp-offset); activation
                                // recomputes profileTemp + offset at apply time.
                                // recipeUpdated → MainController refreshes m_activeRecipe.
                                let newOffset = root.temperatureValue - root.profileTemperature
                                if (Math.abs(newOffset) < 0.05)
                                    newOffset = 0
                                const id = RecipeService.activeRecipeId
                                if (id < 0 || id !== root._recipeEditableBaselineId
                                        || Number(root.brewSnapshot.activeRecipeId) !== id
                                        || Object.keys(root._recipeEditableBaseline).length === 0) {
                                    root.staleBrewSettings = true
                                    root.showRecipeError(TranslationManager.translate("brewDialog.staleBrewSettings", "Brew settings changed elsewhere. Review your values and try again."))
                                    return
                                }
                                const requestId = RecipeService.requestUpdateRecipe(
                                    id, {"tempOffsetC": newOffset}, root._recipeEditableBaseline)
                                if (requestId) {
                                    const requests = Object.assign({}, root._recipeUpdateRequests)
                                    requests[requestId] = {
                                        kind: "recipe-temperature",
                                        patch: {tempOffsetC: newOffset}
                                    }
                                    root._recipeUpdateRequests = requests
                                }
                            } else {
                                const requestId = MachineControlService.updateProfileTemperature(
                                    root.brewRevision, root.temperatureValue,
                                    SteamService.state.controlContextId)
                                if (requestId) {
                                    root._profileTemperatureRequestId = requestId
                                    root._profileTemperatureTarget = root.temperatureValue
                                } else
                                    root.showRecipeError(TranslationManager.translate("brewDialog.profileTemperatureFailed", "Couldn't save the profile temperature"))
                            }
                        }
                    }
                }

                // The temperature(s) the dial RESOLVES TO — the frames the machine
                // will actually brew, updating live as the Temp Delta stepper moves
                // (e.g. dialing -2° turns "Recipe: 81 · 91°C" into "Recipe: 79 · 89°C").
                // NOT a signed offset tag: the stepper directly above already shows the
                // "-2°", so repeating it here read as a doubled "-2°". The eventual temp
                // stays useful because the numbers move; the stepper owns the offset.
                Text {
                    id: tempSubtext
                    // Highlighted when the dial deviates from the active baseline.
                    readonly property bool deviatesFromBaseline: Math.abs(root.temperatureValue - root.recipeTempBaseline) > 0.1
                    visible: root.profileTemperature > 0
                    text: {
                        // Read the unit in QML so this binding reacts to unit changes.
                        void(Settings.app.temperatureUnit)
                        // Shift every profile frame by the FULL dialed offset from the
                        // profile so the line shows the resulting brew temps; hasOverride
                        // is false to suppress the redundant signed tag. The anchor is the
                        // dialed value too, so a frame-less profile still shows the eventual.
                        var body = TemperatureDisplay.formatForSteps(
                            ProfileService.currentProfileSummary.stepTempsC || [],
                            root.temperatureValue, false, root.temperatureValue,
                            Settings.app.temperatureUnit === "fahrenheit",
                            root.temperatureValue - root.profileTemperature)
                        return root.recipeActive
                            ? TranslationManager.translate("brewDialog.recipeTempStructure", "Recipe: %1").arg(body)
                            : TranslationManager.translate("brewDialog.profileTempStructure", "Profile: %1").arg(body)
                    }
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(14)
                    font.italic: true
                    color: deviatesFromBaseline ? Theme.highlightColor : Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            // Dose input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.doseLabel", "Dose:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.minimumWidth: Theme.scaled(75)
                    Accessible.ignored: true  // Label for sighted users; input has accessibleName
                }

                ValueInput {
                    id: doseInput
                    Layout.fillWidth: true
                    // Overridden ⟺ Clear would change it (Clear restores the bag's
                    // remembered dose, else 18 g).
                    readonly property bool overridden: Math.abs(root.doseValue - (Number(root.preferences.dyeBeanWeight) > 0 ? Number(root.preferences.dyeBeanWeight) : 18)) > 0.05
                    value: root.doseValue
                    from: 1
                    to: 50
                    stepSize: 0.1
                    decimals: 1
                    suffix: "g"
                    valueColor: overridden ? Theme.highlightColor : Theme.textColor
                    accentColor: overridden ? Theme.highlightColor : Theme.primaryColor
                    accessibleName: TranslationManager.translate("brewDialog.doseWeight", "Dose weight")
                    onValueModified: function(newValue) {
                        // A dose edit never changes the yield anchor — the
                        // non-anchored row re-derives via onDoseValueChanged.
                        root.doseValue = newValue
                        if (newValue >= 3) {
                            root.showScaleWarning = false
                        }
                    }
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(44)
                    // With a dose cup saved, beans auto-capture when stable, so the
                    // manual grab is redundant — hide it. With no cup (tare 0) auto-
                    // capture is off, so this is the only way to pull dose from scale.
                    visible: Number(root.preferences.doseCupTareWeight || 0) <= 0
                    text: TranslationManager.translate("brewDialog.getFromScale", "Get from scale")
                    accessibleName: TranslationManager.translate("brewDialog.getDoseFromScale", "Get dose from scale")
                    primary: true
                    onClicked: {
                        // Net beans = reading minus the empty-scale virtual zero and the
                        // stored cup tare (0 here, since this button only shows with no cup).
                        var net = MachineState.scaleWeight - root.scaleVirtualZero - Number(root.preferences.doseCupTareWeight || 0)
                        if (net >= 3) {
                            root.showScaleWarning = false
                            // A measurement never changes the anchor.
                            root.doseValue = net
                        } else {
                            // Show warning but don't change dose
                            root.showScaleWarning = true
                        }
                    }
                }
            }

            // Profile recommended dose indicator
            Text {
                readonly property double recommendedDose: Number(root.currentProfileSummary.recommendedDose || 0)
                visible: root.currentProfileSummary.hasRecommendedDose === true && Math.abs(root.doseValue - recommendedDose) > 0.05
                text: TranslationManager.translate("brewDialog.profileDoseIndicator", "Profile: %1g").arg(recommendedDose.toFixed(1))
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(11)
                font.italic: true
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignHCenter
                Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("brewDialog.profileRecommendedDose", "Profile recommended dose: %1 grams").arg(recommendedDose.toFixed(1))
            }

            // Dose cup section: shows the stored empty-cup weight and lets you
            // adjust it (type or +/-) or re-weigh it. With a cup saved, its weight is
            // subtracted by the auto-capture detector (net = load − virtualZero −
            // cupTare); "Get from scale" only appears when no cup is saved. Set once;
            // no per-shot taring.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.cupTareLabel", "Dose cup:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.minimumWidth: Theme.scaled(75)
                    Accessible.ignored: true
                }

                ValueInput {
                    id: cupTareInput
                    Layout.fillWidth: true
                    value: Number(root.preferences.doseCupTareWeight || 0)
                    from: 0
                    to: 100
                    stepSize: 0.1
                    decimals: 1
                    suffix: "g"
                    // Never highlighted: Clear does not reset the cup tare.
                    valueColor: Theme.textColor
                    accentColor: Theme.primaryColor
                    accessibleName: TranslationManager.translate("brewDialog.doseCupWeight", "Dose cup weight")
                    onValueModified: function(newValue) {
                        root.queuePreferenceWrite("doseCupTareWeight", newValue)
                    }
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(44)
                    text: TranslationManager.translate("brewDialog.weighCup", "Weigh")
                    accessibleName: TranslationManager.translate("brewDialog.weighEmptyCup", "Weigh empty cup from scale")
                    primary: true
                    // Store the cup as a delta from the empty-scale virtual zero, so a
                    // non-zeroed scale doesn't inflate the saved weight. Disabled (dimmed)
                    // until the cup is actually on the scale, so the button can't no-op.
                    enabled: MachineState.scaleWeight - root.scaleVirtualZero > 0
                    onClicked: root.queuePreferenceWrite("doseCupTareWeight", MachineState.scaleWeight - root.scaleVirtualZero)
                }

                // Bell toggle: enable/disable the confirmation ding on auto-capture.
                Item {
                    Layout.preferredWidth: Theme.scaled(36)
                    Layout.preferredHeight: Theme.scaled(36)
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        id: dingBell
                        anchors.centerIn: parent
                        source: root.preferences.doseCaptureSoundEnabled ? "qrc:/icons/bell.svg" : "qrc:/icons/bell-off.svg"
                        height: Theme.scaled(20)
                        sourceSize.height: Theme.scaled(40)
                        fillMode: Image.PreserveAspectFit
                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: root.preferences.doseCaptureSoundEnabled ? Theme.primaryColor : Theme.textSecondaryColor
                        }
                        Accessible.ignored: true
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleRole: Accessible.CheckBox
                        accessibleChecked: root.preferences.doseCaptureSoundEnabled === true
                        accessibleName: root.preferences.doseCaptureSoundEnabled
                            ? TranslationManager.translate("brewDialog.captureSoundOn", "Capture sound on")
                            : TranslationManager.translate("brewDialog.captureSoundOff", "Capture sound off")
                        onAccessibleClicked: root.queuePreferenceWrite("doseCaptureSoundEnabled", !root.preferences.doseCaptureSoundEnabled)
                    }
                }
            }

            // Ratio input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.ratioLabel2", "Ratio:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.minimumWidth: Theme.scaled(75)
                    Accessible.ignored: true  // Label for sighted users; input has accessibleName
                }

                // The ratio is a STYLE choice, not a fine dial: tapping opens
                // the named-ratio chooser (Ristretto / Normale / Lungo — the
                // user's own configured presets, with the descriptions that
                // explain them), which is where a ratio is actually decided.
                // The +/- stepper is gone deliberately — the Stop-at row below
                // is the precise-number control, and the two rows are two
                // views of ONE anchor, so a second numeric dial here was
                // redundant. A custom ratio still comes from the chooser's
                // Edit mode, which tunes the presets themselves.
                //
                // Framed like the other fields so it reads as a value, with an
                // edit icon so it reads as tappable (a bare colored number
                // did not).
                Rectangle {
                    id: ratioPick
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    // Overridden ≈ Clear would change it: the baseline ratio
                    // derives from the anchor pair (baselineRatio — the
                    // stored ratio verbatim when the store is ratio-anchored,
                    // else the stored/profile grams ÷ the dose). The
                    // tolerance is the stop-at row's 0.1 g converted through
                    // the dose, so the two rows can never disagree about
                    // whether the user has deviated. Baseline 0 (volume/
                    // timer profile, no anchor) stays inert, as before.
                    readonly property bool overridden: root.baselineRatio > 0
                        && Math.abs(root.ratio - root.baselineRatio)
                           * (root.doseValue > 0 ? root.doseValue : 18) > 0.1
                    radius: Theme.scaled(8)
                    color: ratioPickMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : "transparent"
                    border.width: 1
                    border.color: overridden ? Theme.highlightColor : Theme.textSecondaryColor

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("brewDialog.brewRatio", "Brew ratio")
                        + " 1:" + root.ratio.toFixed(1)
                        + (root.anchorMode === "ratio"
                           ? TranslationManager.translate("brewDialog.anchored", " (anchored)")
                           : TranslationManager.translate("brewDialog.derived", " (derived)"))
                        + ". " + TranslationManager.translate(
                            "brewDialog.tapToChooseRatio", "Tap to choose a ratio")
                    Accessible.focusable: true
                    Accessible.onPressAction: brewRatioPicker.open()

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall
                        Text {
                            Layout.fillWidth: true
                            text: "1:" + root.ratio.toFixed(1)
                            font: Theme.bodyFont
                            color: ratioPick.overridden ? Theme.highlightColor : Theme.textColor
                            horizontalAlignment: Text.AlignHCenter
                            Accessible.ignored: true
                        }
                        ColoredIcon {
                            source: "qrc:/icons/edit.svg"
                            iconWidth: Theme.scaled(16)
                            iconHeight: Theme.scaled(16)
                            iconColor: ratioPick.overridden ? Theme.highlightColor : Theme.primaryColor
                            Accessible.ignored: true
                        }
                    }
                    MouseArea {
                        id: ratioPickMa
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: brewRatioPicker.open()
                    }
                }

                // The single yield/ratio persist button, shown HERE only when
                // the ratio is the anchor — its location is the anchor
                // indicator (editing the stop-at moves it there).
                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(44)
                    visible: root.anchorMode === "ratio" && root.yieldPersistTarget !== ""
                    text: root.yieldPersistLabel
                    accessibleName: root.yieldPersistTarget === "recipe"
                        ? TranslationManager.translate("brewDialog.saveRatioToRecipe", "Save ratio to recipe")
                        : TranslationManager.translate("brewDialog.saveRatioToBag", "Save ratio to bean")
                    primary: true
                    enabled: root.yieldPersistEnabled && root.canWriteBrew
                    onClicked: root.persistYieldAnchor()
                }
            }

            // Yield input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: TranslationManager.translate("brewDialog.stopAtWeightLabel", "Stop at:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                        Layout.minimumWidth: Theme.scaled(75)
                        Accessible.ignored: true  // Label for sighted users; input has accessibleName
                    }

                    ValueInput {
                        id: targetInput
                        Layout.fillWidth: true
                        // Overridden ≈ Clear would change it. The baseline is
                        // the anchor pair's stop-at (baselineStopAt): the
                        // stored grams when the store is absolute-anchored,
                        // the stored ratio x the dose when it is
                        // ratio-anchored — so a ratio recipe sitting at its
                        // designed yield reads at-baseline, and a dose change
                        // moves value and baseline together (no spurious
                        // highlight). Baseline 0 (volume/timer profile, no
                        // anchor): any dialed stop-at reads as an override,
                        // as before.
                        readonly property bool overridden: Math.abs(root.targetValue - root.baselineStopAt) > 0.1
                        value: root.targetValue
                        from: 1
                        to: 500
                        stepSize: 1
                        decimals: 0
                        suffix: "g"
                        valueColor: overridden ? Theme.highlightColor : Theme.textColor
                        accentColor: overridden ? Theme.highlightColor : Theme.primaryColor
                        accessibleName: TranslationManager.translate("brewDialog.stopAtWeight", "Stop at weight")
                            + (root.anchorMode === "absolute"
                               ? TranslationManager.translate("brewDialog.anchored", " (anchored)")
                               : TranslationManager.translate("brewDialog.derived", " (derived)"))
                        onValueModified: function(newValue) {
                            // Editing the stop-at ANCHORS the absolute
                            // (last-written rule): the ratio becomes the
                            // derived quantity. Touching the number field is
                            // the honest gesture for "I want this weight,
                            // absolutely" — it also converts a ratio recipe
                            // back to absolute via the Update button.
                            root.anchorMode = "absolute"
                            root.targetValue = newValue
                            if (root.doseValue > 0) {
                                root.ratio = newValue / root.doseValue
                            }
                        }
                    }

                    // The single yield/ratio persist button, shown HERE only
                    // when the stop-at is the anchor (editing the ratio moves
                    // it to the Ratio row). Destination follows the ladder —
                    // recipe, else bag; never the profile ("Update Profile"
                    // for yield lives in the profile editors now).
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(44)
                        visible: root.anchorMode === "absolute" && root.yieldPersistTarget !== ""
                        text: root.yieldPersistLabel
                        accessibleName: root.yieldPersistTarget === "recipe"
                            ? TranslationManager.translate("brewDialog.saveStopWeightToRecipe", "Save stop-at-weight to recipe")
                            : TranslationManager.translate("brewDialog.saveStopWeightToBag", "Save stop-at-weight to bean")
                        primary: true
                        enabled: root.yieldPersistEnabled && root.canWriteBrew
                        onClicked: root.persistYieldAnchor()
                    }
                }

                // Baseline reference, shown only while the stop-at deviates
                // from the ACTIVE baseline (baselineStopAt via targetInput —
                // the anchor pair's derived stop-at), so a recipe sitting at
                // its own yield — ratio-anchored included — never shows a
                // spurious amber "Profile: Xg" (#1485). A ratio baseline
                // renders as its derived grams here; the anchored form
                // ("1:2") is visible on the Ratio row itself.
                Text {
                    visible: targetInput.overridden && root.baselineStopAt > 0
                    text: root.recipeActive
                        ? TranslationManager.translate("brewDialog.recipeDefault", "Recipe: %1g").arg(root.baselineStopAt.toFixed(0))
                        : (root.baselineIsStoreAnchor
                           ? TranslationManager.translate("brewDialog.bagDefault", "Bean: %1g").arg(root.baselineStopAt.toFixed(0))
                           : TranslationManager.translate("brewDialog.profileDefault", "Profile: %1g").arg(root.profileTargetWeight.toFixed(0)))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.highlightColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.recipeActive
                        ? TranslationManager.translate("brewDialog.recipeStopWeight", "Recipe stop-at-weight: %1 grams").arg(root.baselineStopAt.toFixed(0))
                        : (root.baselineIsStoreAnchor
                           ? TranslationManager.translate("brewDialog.bagStopWeight", "Bean stop-at-weight: %1 grams").arg(root.baselineStopAt.toFixed(0))
                           : TranslationManager.translate("brewDialog.profileDefaultStopWeight", "Profile default stop-at-weight: %1 grams").arg(root.profileTargetWeight.toFixed(0)))
                }
            }

            // Equipment package (read-only) + Switch Equipment button. Identity
            // is managed in the Equipment window / Switch dialog, not edited here.
            // Hidden in recipe mode (the recipe owns the equipment package).
            RowLayout {
                visible: !root.recipeActive
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.equipmentLabel", "Equipment:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    // "Equipment:" is the widest label here, so let it size to its
                    // content (min = the shared 75px column) rather than a fixed 75px
                    // that clips it and lets the package name butt right against it.
                    Layout.minimumWidth: Theme.scaled(75)
                    Accessible.ignored: true
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equipmentLabel.length > 0
                          ? root.equipmentLabel
                          : TranslationManager.translate("brewDialog.equipmentNotSet", "Not set")
                    font: Theme.bodyFont
                    color: root.equipmentLabel.length > 0 ? Theme.textColor : Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("brewDialog.equipmentAccessible", "Equipment: %1").arg(
                        root.equipmentLabel.length > 0 ? root.equipmentLabel
                        : TranslationManager.translate("brewDialog.equipmentNotSet", "Not set"))
                }

                // Info: show the active package's full contents.
                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(40)
                    visible: Number(root.brewSnapshot.activeEquipmentId || -1) > 0
                    icon.source: "qrc:/icons/info.svg"
                    accessibleName: TranslationManager.translate("equipment.info.button", "Equipment details")
                    onClicked: equipmentInfoDialog.openFor(Number(root.brewSnapshot.activeEquipmentId || -1))
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(40)
                    text: root.equipmentLabel.length > 0
                          ? TranslationManager.translate("brewDialog.switchEquipment", "Switch")
                          : TranslationManager.translate("brewDialog.addEquipment", "Add")
                    accessibleName: TranslationManager.translate("brewDialog.switchEquipmentAccessible", "Switch equipment package")
                    onClicked: switchEquipmentDialog.openPicker()
                }
            }

            // Grind setting (+ rpm when the grinder is rpm-adjustable) — dial-in.
            // One tap-to-open control for both halves ("grind · rpm"): tapping
            // opens the grind picker (wheels + keyboard entry). Commits land on
            // the dialog's PENDING grindSetting/grindRpm; the dual write-through
            // (active bag + package) still happens on the dialog's own commit
            // button, unchanged (brew-settings-equipment). Context is the active
            // grinder — this dialog edits the live dial-in.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Text {
                    text: TranslationManager.translate("brewDialog.grindLabel", "Grind:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.minimumWidth: Theme.scaled(75)
                    Accessible.ignored: true
                }

                GrindField {
                    // Content-hugging, like the other value controls in this
                    // dialog — a full-width box around a short grind value
                    // reads wrong next to the steppers.
                    Layout.preferredWidth: implicitWidth
                    presentation: "field"
                    grinderBrand: root.equipmentBrand
                    grinderModel: root.equipmentModel
                    grindSetting: root.grindSetting
                    rpmValue: root.grindRpm
                    accessibleName: TranslationManager.translate("brewDialog.grinderSetting", "Grinder setting")
                    // Clears ARE applied here, unlike the pill. This dialog
                    // edits PENDING state (root.grindSetting/grindRpm) that the
                    // user still reviews before OK, and the inline RPM field
                    // this replaced could be emptied to 0 — swallowing the
                    // clear would silently regress a working operation. The
                    // pill stays the sole exception: it writes the live dial-in
                    // straight through with no review step.
                    onGrindCommitted: function(v) { root.grindSetting = v }
                    onRpmCommitted: function(rpm) { root.grindRpm = rpm }
                }
            }
        }
        } // ColumnLayout (mainColumn)
        } // Flickable
    } // KeyboardAwareContainer (contentItem)
}
