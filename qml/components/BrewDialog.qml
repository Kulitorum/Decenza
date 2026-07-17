import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    // 520 on tablets; caps to the viewport so it can't overflow a narrow phone
    // (the content column and the height cap both track this width).
    readonly property real _frameWidth: Math.min(Theme.scaled(520),
                                                  (root.parent ? root.parent.width : Theme.scaled(520)) * 0.95)
    width: _frameWidth
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    // Accessibility: Dialog is announced via onAboutToShow below
    // Note: Don't set 'title' property - it creates a built-in header frame
    // Note: Don't set Accessible properties on Dialog - it doesn't derive from Item

    // Temperature override
    property double temperatureValue: ProfileManager.profileTargetTemperature
    property double profileTemperature: ProfileManager.profileTargetTemperature

    // Empty-scale virtual zero, fed from IdlePage's bean capture. Lets "Weigh" store
    // the cup as a delta (reading - virtualZero) so a non-zeroed scale doesn't bake an
    // offset into the saved cup weight.
    property real scaleVirtualZero: 0

    // Dose value (editable, default 18g)
    property double doseValue: 18.0
    property double ratio: Settings.brew.lastUsedRatio

    // Target (yield) value and the YIELD ANCHOR (add-yield-ratio-anchor):
    // which of {ratio, stop-at} was last written. "ratio" = the ratio is the
    // anchor and the stop-at derives (dose x ratio); "absolute" = the stop-at
    // is the anchor and the ratio derives; "none" = neither is anchored (the
    // profile answers) until the user edits a row. This is the permanent home
    // of what targetManuallySet used to track per-dialog-session. Seeded from
    // the persisted session anchor (Settings.brew.brewYieldMode); mutated
    // ONLY by the user editing a row — never by a dose capture.
    property double targetValue: doseValue * ratio
    property double profileTargetWeight: ProfileManager.profileTargetWeight
    property string anchorMode: "none"

    // Equipment (read-only, resolved from the active package via SettingsDye) +
    // the dial-in fields that live in Brew Settings (grind setting + rpm).
    readonly property string equipmentBrand: Settings.dye.dyeGrinderBrand
    readonly property string equipmentModel: Settings.dye.dyeGrinderModel
    readonly property bool equipmentRpmCapable: Settings.dye.grinderRpmCapable(equipmentBrand, equipmentModel)
    // Show the package's display name (defaults to "{brand} {model}"), not the
    // raw grinder identity (add-equipment-packages).
    readonly property string equipmentLabel: Settings.dye.dyeEquipmentName
    property string grindSetting: ""
    property int grindRpm: 0

    // Profile
    property string selectedProfileTitle: ""
    property string originalProfileFilename: ""

    // Recipe mode: when a recipe is active it owns the profile/beans/equipment,
    // so those rows hide and a Recipe quick-switch row takes the Profile row's
    // place. activeRecipeId is a NOTIFYing property, so this re-evaluates live
    // (e.g. deactivation from another surface while the dialog is open).
    readonly property bool recipeActive: Settings.dye.activeRecipeId >= 0
    // Baselines for the two override fields (Temp Delta, Stop-at). A recipe's
    // yield/temp ARE the recipe's design — its baseline — not deviations from
    // the profile, so when a recipe is active the highlight, the Temp Delta
    // zero-point, and Clear all measure against the recipe's own values, not the
    // profile default. A recipe that never pinned a yield (stored 0 = unset)
    // falls back to the profile; for temperature, offset 0 explicitly MEANS
    // the profile's own temperature — the same fallback either way. NOTIFY-reactive
    // via recipeActive (activeRecipeId) + MainController.activeRecipe.
    // The temperature baseline is OFFSET-derived (recipe-relative-temp-offset):
    // profile temp + the recipe's stored delta, so a profile temperature edit
    // moves the recipe's baseline with it. Offset 0 = the profile itself.
    readonly property double recipeTempBaseline: (recipeActive && profileTemperature > 0
            && Math.abs(MainController.activeRecipe.tempOffsetC || 0) > 0.05)
        ? profileTemperature + MainController.activeRecipe.tempOffsetC : profileTemperature
    // Yield baseline as a TYPE-AWARE ANCHOR PAIR (add-yield-ratio-anchor):
    // the active store's own {value, mode} resolved through the ladder
    // (recipe -> bag -> profile; MainController folds that), with the OTHER
    // row's baseline derived through the dialog's dose. Because the derived
    // baseline moves with the dose exactly as the derived value does,
    // neither row spuriously highlights on a dose change — in either mode.
    // A ratio-anchored store never falls back to the profile's gram target
    // (that fallthrough is the #1485 spurious-override bug).
    readonly property double baselineYieldValue: MainController.activeBaselineYieldValue
    readonly property string baselineYieldMode: MainController.activeBaselineYieldMode
    // True when a recipe or bag actually designs a yield; false = the ladder
    // bottomed out at the profile (baselineYieldMode reads "absolute" there,
    // but Clear must restore anchor mode "none", not arm an absolute).
    readonly property bool baselineIsStoreAnchor:
        (recipeActive && (MainController.activeRecipe.yieldMode || "none") !== "none"
                      && (MainController.activeRecipe.yieldValue || 0) > 0)
        || (Settings.dye.activeBagYieldMode !== "none" && Settings.dye.activeBagYieldValue > 0)
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
    property int _pendingRecipeUpdateId: -1
    // Failure banner for the fire-and-forget recipe operations (update /
    // switch). Auto-dismisses — the allowed toast use of a timer.
    property string recipeErrorText: ""
    Timer {
        id: recipeErrorTimer
        interval: 4000
        onTriggered: root.recipeErrorText = ""
    }
    function showRecipeError(msg) {
        recipeErrorText = msg
        recipeErrorTimer.restart()
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
            AccessibilityManager.announce(msg, true)
    }

    function getRecipeSuggestions() {
        var names = []
        for (var i = 0; i < recipeChoices.length; i++)
            names.push(recipeChoices[i].name)
        return names
    }

    // Resolve a chosen name to a recipe id. Names can collide; prefer a match
    // whose id differs from the active one (so picking a duplicate name always
    // switches). Returns -1 when nothing matches.
    function resolveRecipeId(name) {
        var fallback = -1
        for (var i = 0; i < recipeChoices.length; i++) {
            var r = recipeChoices[i]
            if (r.name === name) {
                if (r.id !== Settings.dye.activeRecipeId)
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
    readonly property string yieldPersistTarget: {
        if (recipeActive) {
            if ((MainController.activeRecipe.yieldMode || "none") !== "none")
                return "recipe"
            // The recipe designs no yield: the ladder fell through to the
            // bag, so the store being shown — and edited — is the bag.
            if (Settings.dye.activeBagId >= 0)
                return "bag"
            return "recipe"  // bean-less recipe: nothing below it to edit
        }
        if (Settings.dye.activeBagId >= 0)
            return "bag"
        return ""
    }
    readonly property string yieldPersistLabel: yieldPersistTarget === "recipe"
        ? TranslationManager.translate("brewDialog.updateRecipe", "Update Recipe")
        : TranslationManager.translate("brewDialog.updateBag", "Update Bag")
    // The DESTINATION store's own stored spec (not the ladder baseline) —
    // the button gates on the shown anchor differing from what that store
    // already holds, comparing like with like. A mode change alone enables
    // it: persisting it genuinely changes behaviour on the next dose change
    // even when the gram value is identical.
    readonly property double storedYieldValue: yieldPersistTarget === "recipe"
        ? (MainController.activeRecipe.yieldValue || 0)
        : Settings.dye.activeBagYieldValue
    readonly property string storedYieldMode: yieldPersistTarget === "recipe"
        ? (MainController.activeRecipe.yieldMode || "none")
        : Settings.dye.activeBagYieldMode
    readonly property bool yieldPersistEnabled: {
        if (yieldPersistTarget === "" || anchorMode === "none")
            return false
        if (anchorMode !== storedYieldMode)
            return true
        var shown = anchorMode === "ratio" ? ratio : targetValue
        if (anchorMode === "ratio") {
            // One tolerance unit, converted through the dose (0.1 g).
            var d = doseValue > 0 ? doseValue : 18
            return Math.abs(shown - storedYieldValue) * d > 0.1
        }
        return Math.abs(shown - storedYieldValue) > 0.1
    }
    function persistYieldAnchor() {
        var value = anchorMode === "ratio" ? ratio : targetValue
        if (yieldPersistTarget === "recipe") {
            _pendingRecipeUpdateId = Settings.dye.activeRecipeId
            MainController.recipeStorage.requestUpdateRecipe(
                Settings.dye.activeRecipeId,
                {"yieldValue": value, "yieldMode": anchorMode})
        } else if (yieldPersistTarget === "bag") {
            Settings.dye.persistYieldSpecToBag(value, anchorMode)
        }
    }

    // Seed every dial-in field from the current DYE/profile/override state.
    // Called on open and after a recipe switch settles. Writes ONLY local
    // root.* values — never Settings — so re-seeding can't trigger a
    // dose/grind stamp on a just-activated recipe.
    function seedFromCurrentState() {
        // Update profile temperature, use override if active
        profileTemperature = ProfileManager.profileTargetTemperature
        profileTargetWeight = ProfileManager.profileTargetWeight
        temperatureValue = Settings.brew.hasTemperatureOverride ? Settings.brew.temperatureOverride : profileTemperature

        // Use DYE fields for dose and grind (source of truth). Grinder identity
        // is read-only here (resolved from the active package); only the dial-in
        // (grind setting + rpm) is editable.
        doseValue = Settings.dye.dyeBeanWeight > 0 ? Settings.dye.dyeBeanWeight : 18.0
        grindSetting = Settings.dye.dyeGrinderSetting
        grindRpm = Settings.dye.dyeGrinderRpm
        selectedProfileTitle = ProfileManager.currentProfileName
        selectedRecipeName = (recipeActive && MainController.activeRecipe.name) ? MainController.activeRecipe.name : ""

        // Yield: seed the anchor from the persisted session spec — the one
        // line where the stored mode enters the dialog. A ratio-anchored
        // session opens ratio-first (its identity used to be invisible:
        // activation wrote grams for any recipe, so the dialog always opened
        // yield-first). Mode "none" shows the profile's target, unanchored.
        anchorMode = Settings.brew.brewYieldMode
        if (anchorMode === "ratio") {
            ratio = Settings.brew.brewYieldOverride
            targetValue = doseValue > 0 ? doseValue * ratio : profileTargetWeight
        } else if (anchorMode === "absolute") {
            targetValue = Settings.brew.brewYieldOverride
            ratio = doseValue > 0 ? targetValue / doseValue : Settings.brew.lastUsedRatio
        } else {
            targetValue = profileTargetWeight
            ratio = doseValue > 0 && targetValue > 0 ? targetValue / doseValue
                                                     : Settings.brew.lastUsedRatio
        }
    }

    function getProfileSuggestions() {
        var profiles = ProfileManager.availableProfiles
        var titles = []
        for (var i = 0; i < profiles.length; i++)
            titles.push(profiles[i].title)
        return titles
    }

    function loadProfileByTitle(title) {
        var filename = ProfileManager.findProfileByTitle(title)
        if (filename.length > 0) {
            ProfileManager.loadProfile(filename)
            root.profileTemperature = ProfileManager.profileTargetTemperature
            root.temperatureValue = root.profileTemperature
            root.profileTargetWeight = ProfileManager.profileTargetWeight
            // Mode asymmetry (add-yield-ratio-anchor): a ratio anchor
            // survives the profile switch (1:2 is 1:2 on any profile — the
            // target keeps deriving from the dose); an absolute or
            // unanchored yield follows the new profile's target.
            if (root.anchorMode === "none")
                root.targetValue = root.profileTargetWeight
            else if (root.anchorMode === "absolute") {
                // The C++ reset cleared the absolute session anchor on the
                // switch; mirror it locally so OK doesn't re-arm a stale one.
                root.anchorMode = "none"
                root.targetValue = root.profileTargetWeight
                root.ratio = root.doseValue > 0 && root.targetValue > 0
                    ? root.targetValue / root.doseValue : root.ratio
            }
        }
    }

    // Grinder identity is now chosen via the Switch Equipment dialog, so only the
    // grind-setting field keeps a suggestion list (scoped to the active grinder).
    function getGrinderSettingSuggestions() {
        var suggestions = MainController.shotHistory ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(root.equipmentModel) : []
        if (Settings.dye.dyeGrinderSetting.length > 0 && suggestions.indexOf(Settings.dye.dyeGrinderSetting) === -1) {
            suggestions.unshift(Settings.dye.dyeGrinderSetting)
        }
        return suggestions
    }

    // Incremented when async distinct cache refreshes; referenced in suggestion bindings
    // to force QML re-evaluation (the >= 0 condition is always true by design)
    property int _distinctCacheVersion: 0

    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() {
            _distinctCacheVersion++
        }
    }

    // Low dose warning - shown when dose is low OR when scale read failed
    property bool showScaleWarning: false
    property bool lowDoseWarning: doseValue < 3 || showScaleWarning

    // Bean auto-capture lives in IdlePage as a single, persistent detector — it
    // stays armed across this dialog opening/closing, so a cup already weighed on
    // the home screen is NOT re-captured (no second ding) when you open settings.
    // The capture writes the canonical dyeBeanWeight; this watcher reflects it into
    // the editable dose whenever a capture lands while the dialog is open (rather
    // than the capture poking doseValue directly). The manual "Get from scale"
    // button below covers the no-dose-cup case.
    Connections {
        target: Settings.dye
        enabled: root.visible
        function onDyeBeanWeightChanged() {
            // A capture writes the DOSE and never flips the anchor
            // (add-yield-ratio-anchor: the scale owns the dose, the
            // recipe/bag owns the anchor). The old `targetManuallySet =
            // false` here silently re-anchored the ratio on every capture,
            // stomping an absolute yield.
            if (root.visible && Settings.dye.dyeBeanWeight > 0)
                root.doseValue = Settings.dye.dyeBeanWeight
        }
        // Deactivation while open (id → -1): refresh the returning Profile
        // row's seeded values. Successful switches re-seed off recipeActivated
        // instead — that signal is queued after activation's deferred dose
        // write, whereas this id flip happens before the dose lands.
        function onActiveRecipeIdChanged() {
            if (root.visible && Settings.dye.activeRecipeId < 0)
                root.seedFromCurrentState()
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
        if (!recipeActive && originalProfileFilename.length > 0 && Settings.app.currentProfile !== originalProfileFilename) {
            ProfileManager.loadProfile(originalProfileFilename)
        }
    }

    onAboutToShow: {
        // Announce dialog for accessibility
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var announcement = TranslationManager.translate("brewDialog.dialogAnnouncement", "Brew Settings dialog. Profile: ") + ProfileManager.currentProfileName
            if (Settings.dye.dyeBeanBrand.length > 0)
                announcement += ". " + TranslationManager.translate("brewDialog.roasterAnnouncementLabel", "Roaster: ") + Settings.dye.dyeBeanBrand
            if (Settings.dye.dyeBeanType.length > 0)
                announcement += ". " + TranslationManager.translate("brewDialog.coffeeAnnouncementLabel", "Coffee: ") + Settings.dye.dyeBeanType
            AccessibilityManager.announce(announcement)
        }

        seedFromCurrentState()
        originalProfileFilename = Settings.app.currentProfile
        showScaleWarning = false
        recipeErrorText = ""
        _pendingRecipeUpdateId = -1
        MainController.recipeStorage.requestInventory()
    }

    // Recipe quick-switch list: the same non-archived MRU inventory the pill
    // row uses. Refreshed on open (above) and on any recipe change while open
    // (e.g. after "Update Recipe" writes a field).
    Connections {
        target: MainController.recipeStorage
        function onInventoryReady(recipes) { root.recipeChoices = recipes }
        function onRecipesChanged() {
            if (root.visible)
                MainController.recipeStorage.requestInventory()
        }
        // "Update Recipe" is fire-and-forget and, with the auto-stamp gone,
        // the ONLY path a yield/temp value reaches the recipe — a swallowed
        // failure would silently lose the user's dialed value.
        function onRecipeUpdated(recipeId, success) {
            if (recipeId !== root._pendingRecipeUpdateId)
                return
            root._pendingRecipeUpdateId = -1
            if (!success && root.visible)
                root.showRecipeError(TranslationManager.translate("brewDialog.recipeUpdateFailed", "Couldn't save to the recipe"))
        }
    }

    // Re-seed after an in-dialog recipe switch. recipeActivated is emitted
    // QUEUED, deliberately after activation's deferred dose write — the one
    // event where "everything applied" is actually true (activeRecipeId flips
    // BEFORE the dose lands, so it can't be the re-seed trigger). On failure
    // nothing was applied: the same re-seed restores the truthful recipe name
    // in the quick-switch field, plus a banner so the miss isn't silent.
    Connections {
        target: MainController
        enabled: root.visible
        function onRecipeActivated(recipeId, success) {
            if (!root.visible)
                return
            root.seedFromCurrentState()
            if (!success)
                root.showRecipeError(TranslationManager.translate("brewDialog.recipeSwitchFailed", "Couldn't switch to that recipe"))
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
        color: Theme.surfaceColor
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
            profileInput.textField, recipeInput.textField,
            grindInput.textField, rpmInput
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
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate("brewDialog.clear", "Clear")
                    accessibleName: TranslationManager.translate("brewDialog.clearAllOverrides", "Clear all overrides")
                    onClicked: {
                        // Reset to current profile and bean preset values (not cached values from dialog open)
                        root.profileTemperature = ProfileManager.profileTargetTemperature
                        root.profileTargetWeight = ProfileManager.profileTargetWeight
                        // Reset the dose to the active bag's dose (the bean's remembered
                        // weight), otherwise default 18 g.
                        root.doseValue = Settings.dye.dyeBeanWeight > 0 ? Settings.dye.dyeBeanWeight : 18.0
                        root.selectedProfileTitle = ProfileManager.currentProfileName
                        root.grindSetting = Settings.dye.dyeGrinderSetting
                        root.grindRpm = Settings.dye.dyeGrinderRpm
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
                                    ? root.targetValue / root.doseValue : Settings.brew.lastUsedRatio
                            }
                        } else {
                            root.anchorMode = "none"
                            var profileTarget = ProfileManager.profileTargetWeight
                            root.ratio = (profileTarget > 0 && root.doseValue > 0) ? profileTarget / root.doseValue : Settings.brew.lastUsedRatio
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
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.warningColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Cancel
                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate("brewDialog.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("brewDialog.cancelBrewSettings", "Cancel brew settings")
                    onClicked: root.reject()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // OK (primary)
                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate("brewDialog.ok", "OK")
                    accessibleName: TranslationManager.translate("brewDialog.confirmBrewSettings", "Confirm brew settings")
                    onClicked: {
                        Qt.inputMethod.commit()
                        // lastUsedRatio survives only as PRESET MEMORY (which
                        // pick is highlighted; a fresh brew's seed) — it is
                        // never an authority a yield derives from, so only a
                        // deliberately-anchored ratio updates it.
                        if (root.anchorMode === "ratio")
                            Settings.brew.lastUsedRatio = root.ratio
                        // Grinder identity is managed via Switch Equipment; only the
                        // dial-in (grind setting + rpm) is saved from here.
                        Settings.dye.dyeGrinderSetting = root.grindSetting
                        Settings.dye.dyeGrinderRpm = root.grindRpm
                        // The session anchor is armed AS A SPEC — value in
                        // its mode's own unit. Mode "none" clears the yield
                        // override so the profile answers.
                        ProfileManager.activateBrewWithOverrides(
                            root.doseValue,
                            root.anchorMode === "ratio" ? root.ratio : root.targetValue,
                            root.anchorMode,
                            root.temperatureValue,
                            root.grindSetting
                        )
                        root.accept()
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
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
                    if (t === (MainController.activeRecipe.name || ""))
                        return
                    // Only switch on an exact name match (suggestion selection or a
                    // fully typed name); re-selecting the active recipe is a no-op.
                    var id = root.resolveRecipeId(t)
                    if (id >= 0 && id !== Settings.dye.activeRecipeId)
                        MainController.activateRecipe(id)
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
                implicitHeight: recipeErrorLabel.implicitHeight + Theme.scaled(24)

                Accessible.role: Accessible.AlertMessage
                Accessible.name: recipeErrorLabel.text
                Accessible.focusable: true

                Text {
                    id: recipeErrorLabel
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(12)
                    text: root.recipeErrorText
                    font: Theme.bodyFont
                    color: Theme.errorColor
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true  // Parent handles accessibility
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
                    if (visible && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
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
                        enabled: tempInput.overridden
                        onClicked: {
                            if (root.recipeActive) {
                                // Persist the OFFSET (dialed − profile temp), never the
                                // absolute (recipe-relative-temp-offset); activation
                                // recomputes profileTemp + offset at apply time.
                                // recipeUpdated → MainController refreshes m_activeRecipe.
                                var newOffset = root.temperatureValue - root.profileTemperature
                                if (Math.abs(newOffset) < 0.05)
                                    newOffset = 0
                                root._pendingRecipeUpdateId = Settings.dye.activeRecipeId
                                MainController.recipeStorage.requestUpdateRecipe(
                                    Settings.dye.activeRecipeId,
                                    {"tempOffsetC": newOffset})
                            } else {
                                // Bake the new temperature into the profile. Anchored on
                                // espressoTemperature (same as the live-brew override path)
                                // so save and brew shift every step by the same delta.
                                ProfileManager.applyTemperatureToProfile(root.temperatureValue)
                                root.profileTemperature = root.temperatureValue
                            }
                        }
                    }
                }

                // The baseline temperature(s) the Temp Delta control is measured
                // from — a baseline is a baseline. With a recipe active that is the
                // recipe's OWN temps (profile frames shifted by the recipe's delta,
                // e.g. "Recipe: 81 · 91°C"), matching the Temp Delta reading 0° at the
                // recipe; a tag appears only for a per-brew deviation FROM the recipe.
                // With no recipe it is the profile's temps ("Profile: 84 · 94°C" + the
                // offset tag when the dial is adjusted) — unchanged.
                Text {
                    id: tempSubtext
                    // Highlighted / tagged iff the dial deviates from the active baseline.
                    readonly property bool deviatesFromBaseline: Math.abs(root.temperatureValue - root.recipeTempBaseline) > 0.1
                    readonly property double _shift: root.recipeTempBaseline - root.profileTemperature
                    visible: root.profileTemperature > 0
                    text: {
                        // temperatureDisplay() reads the C/F unit in C++ (not a QML-
                        // capturable dependency), so read it here to re-evaluate on switch.
                        void(Settings.app.temperatureUnit)
                        var body = ProfileManager.temperatureDisplay(root.recipeTempBaseline, deviatesFromBaseline, root.temperatureValue, _shift)
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
                    readonly property bool overridden: Math.abs(root.doseValue - (Settings.dye.dyeBeanWeight > 0 ? Settings.dye.dyeBeanWeight : 18)) > 0.05
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
                    visible: Settings.brew.doseCupTareWeight <= 0
                    text: TranslationManager.translate("brewDialog.getFromScale", "Get from scale")
                    accessibleName: TranslationManager.translate("brewDialog.getDoseFromScale", "Get dose from scale")
                    primary: true
                    onClicked: {
                        // Net beans = reading minus the empty-scale virtual zero and the
                        // stored cup tare (0 here, since this button only shows with no cup).
                        var net = MachineState.scaleWeight - root.scaleVirtualZero - Settings.brew.doseCupTareWeight
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
                visible: ProfileManager.profileHasRecommendedDose && Math.abs(root.doseValue - ProfileManager.profileRecommendedDose) > 0.05
                text: TranslationManager.translate("brewDialog.profileDoseIndicator", "Profile: %1g").arg(ProfileManager.profileRecommendedDose.toFixed(1))
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(11)
                font.italic: true
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignHCenter
                Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("brewDialog.profileRecommendedDose", "Profile recommended dose: %1 grams").arg(ProfileManager.profileRecommendedDose.toFixed(1))
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
                    value: Settings.brew.doseCupTareWeight
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
                        Settings.brew.doseCupTareWeight = newValue
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
                    onClicked: Settings.brew.doseCupTareWeight = MachineState.scaleWeight - root.scaleVirtualZero
                }

                // Bell toggle: enable/disable the confirmation ding on auto-capture.
                Item {
                    Layout.preferredWidth: Theme.scaled(36)
                    Layout.preferredHeight: Theme.scaled(36)
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        id: dingBell
                        anchors.centerIn: parent
                        source: Settings.brew.doseCaptureSoundEnabled ? "qrc:/icons/bell.svg" : "qrc:/icons/bell-off.svg"
                        height: Theme.scaled(20)
                        sourceSize.height: Theme.scaled(40)
                        fillMode: Image.PreserveAspectFit
                        layer.enabled: true
                        layer.smooth: true
                        layer.effect: MultiEffect {
                            colorization: 1.0
                            colorizationColor: Settings.brew.doseCaptureSoundEnabled ? Theme.primaryColor : Theme.textSecondaryColor
                        }
                        Accessible.ignored: true
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleRole: Accessible.CheckBox
                        accessibleChecked: Settings.brew.doseCaptureSoundEnabled
                        accessibleName: Settings.brew.doseCaptureSoundEnabled
                            ? TranslationManager.translate("brewDialog.captureSoundOn", "Capture sound on")
                            : TranslationManager.translate("brewDialog.captureSoundOff", "Capture sound off")
                        onAccessibleClicked: Settings.brew.doseCaptureSoundEnabled = !Settings.brew.doseCaptureSoundEnabled
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
                    enabled: root.yieldPersistEnabled
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
                        enabled: root.yieldPersistEnabled
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
                    visible: Settings.dye.activeEquipmentId > 0
                    icon.source: "qrc:/icons/info.svg"
                    accessibleName: TranslationManager.translate("equipment.info.button", "Equipment details")
                    onClicked: equipmentInfoDialog.openFor(Settings.dye.activeEquipmentId)
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

            // Grind setting (+ rpm when the grinder is rpm-adjustable) — dial-in
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

                SuggestionField {
                    id: grindInput
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.scaled(110)
                    label: ""
                    accessibleName: TranslationManager.translate("brewDialog.grinderSetting", "Grinder setting")
                    text: root.grindSetting
                    suggestions: _distinctCacheVersion >= 0 ? root.getGrinderSettingSuggestions() : []
                    onTextEdited: function(t) { root.grindSetting = t }
                }

                Text {
                    visible: root.equipmentRpmCapable
                    text: TranslationManager.translate("brewDialog.rpmLabel", "RPM:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }

                StyledTextField {
                    id: rpmInput
                    visible: root.equipmentRpmCapable
                    Layout.preferredWidth: Theme.scaled(80)
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: root.grindRpm > 0 ? String(root.grindRpm) : ""
                    Accessible.name: TranslationManager.translate("brewDialog.rpmAccessible", "Grinder rpm")
                    onTextEdited: root.grindRpm = parseInt(text) || 0
                }
            }
        }
        } // ColumnLayout (mainColumn)
        } // Flickable
    } // KeyboardAwareContainer (contentItem)
}
