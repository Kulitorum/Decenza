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

    // Target (yield) value and tracking
    property double targetValue: doseValue * ratio
    property double profileTargetWeight: ProfileManager.profileTargetWeight
    property bool targetManuallySet: false

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
    readonly property double recipeYieldBaseline: (recipeActive && MainController.activeRecipe.yieldG > 0)
                                                  ? MainController.activeRecipe.yieldG : profileTargetWeight
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

        // Yield: use override if active, otherwise use profile default
        targetValue = Settings.brew.hasBrewYieldOverride ? Settings.brew.brewYieldOverride : profileTargetWeight
        ratio = doseValue > 0 ? targetValue / doseValue : Settings.brew.lastUsedRatio
        targetManuallySet = Settings.brew.hasBrewYieldOverride
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
            if (!root.targetManuallySet)
                root.targetValue = root.profileTargetWeight
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
            if (root.visible && Settings.dye.dyeBeanWeight > 0) {
                root.targetManuallySet = false
                root.doseValue = Settings.dye.dyeBeanWeight
            }
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

    // Recalculate target when dose or ratio changes (unless manually overridden)
    onDoseValueChanged: {
        if (!targetManuallySet) {
            targetValue = doseValue * ratio
        }
    }

    onRatioChanged: {
        if (!targetManuallySet) {
            targetValue = doseValue * ratio
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
                        root.targetManuallySet = false
                        // Clear returns each override field to the ACTIVE baseline —
                        // the recipe's own yield/temp when a recipe is active, the
                        // profile default otherwise. It only strips per-brew deviations;
                        // it never edits the recipe's stored values (that is "Update
                        // Recipe"'s job). recipeTempBaseline folds the recipe-vs-profile
                        // choice for temperature; the yield branch below restores a
                        // pinned recipe yield exactly, else keeps the profile-derived
                        // fallback the no-recipe dialog has always used (which handles
                        // volume/timer profiles with target 0 via lastUsedRatio).
                        root.temperatureValue = root.recipeTempBaseline
                        if (root.recipeActive && MainController.activeRecipe.yieldG > 0) {
                            root.targetValue = MainController.activeRecipe.yieldG
                            root.ratio = root.doseValue > 0 ? root.targetValue / root.doseValue : Settings.brew.lastUsedRatio
                        } else {
                            var profileTarget = ProfileManager.profileTargetWeight
                            root.ratio = (profileTarget > 0 && root.doseValue > 0) ? profileTarget / root.doseValue : Settings.brew.lastUsedRatio
                            root.targetValue = root.doseValue * root.ratio
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
                        Settings.brew.lastUsedRatio = root.ratio
                        // Grinder identity is managed via Switch Equipment; only the
                        // dial-in (grind setting + rpm) is saved from here.
                        Settings.dye.dyeGrinderSetting = root.grindSetting
                        Settings.dye.dyeGrinderRpm = root.grindRpm
                        ProfileManager.activateBrewWithOverrides(
                            root.doseValue,
                            root.targetValue,
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
                        root.targetManuallySet = false  // Reset manual flag when dose changes
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
                            root.targetManuallySet = false  // Reset manual flag
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

                ValueInput {
                    id: ratioInput
                    Layout.fillWidth: true
                    // Overridden ≈ Clear would change it: the baseline ratio is the
                    // active baseline YIELD ÷ the current dose. In recipe mode that
                    // yield is the recipe's own (recipeYieldBaseline), so a recipe whose
                    // yield differs from the profile reads its ratio as at-baseline
                    // (white) rather than as an override — matching the Stop-at field.
                    // recipeYieldBaseline collapses to profileTargetWeight with no recipe
                    // (unchanged there), and is 0 for volume/timer profiles with no
                    // recipe yield → inert, as before.
                    readonly property bool overridden: Math.abs(root.ratio - ((root.doseValue > 0 && root.recipeYieldBaseline > 0) ? root.recipeYieldBaseline / root.doseValue : root.ratio)) > 0.05
                    value: root.ratio
                    from: 0.5
                    to: 20.0
                    stepSize: 0.1
                    decimals: 1
                    valueColor: overridden ? Theme.highlightColor : Theme.textColor
                    accentColor: overridden ? Theme.highlightColor : Theme.primaryColor
                    accessibleName: TranslationManager.translate("brewDialog.brewRatio", "Brew ratio")
                    onValueModified: function(newValue) {
                        root.targetManuallySet = false  // Reset manual flag when ratio changes
                        root.ratio = newValue
                    }
                }

                // Show the multiplier (the stepper) as a ratio too, so it's clear
                // 1.8 == 1:1.8 (matches the ratio widget / scale widget display).
                Text {
                    text: "1:" + root.ratio.toFixed(1)
                    font: Theme.bodyFont
                    color: Theme.primaryColor
                    Layout.preferredWidth: Theme.scaled(60)
                    horizontalAlignment: Text.AlignRight
                    Accessible.ignored: true
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
                        // Overridden ≈ Clear would change it. The baseline is the
                        // recipe's own yieldG when a recipe is active (its yield is the
                        // recipe's design, not a deviation), else the profile target
                        // weight — recipeYieldBaseline folds both. On volume/timer
                        // profiles with no recipe yield the baseline is 0, so any dialed
                        // stop-at reads as an override, as before. In no-recipe mode
                        // recipeYieldBaseline == profileTargetWeight (unchanged).
                        readonly property bool overridden: Math.abs(root.targetValue - root.recipeYieldBaseline) > 0.1
                        value: root.targetValue
                        from: 1
                        to: 500
                        stepSize: 1
                        decimals: 0
                        suffix: "g"
                        valueColor: overridden ? Theme.highlightColor : Theme.textColor
                        accentColor: overridden ? Theme.highlightColor : Theme.primaryColor
                        accessibleName: TranslationManager.translate("brewDialog.stopAtWeight", "Stop at weight") + (root.targetManuallySet ? TranslationManager.translate("brewDialog.manual", " (manual)") : TranslationManager.translate("brewDialog.calculated", " (calculated)"))
                        onValueModified: function(newValue) {
                            root.targetManuallySet = true  // Mark as manually set
                            root.targetValue = newValue
                            // Update ratio to match (yield / dose)
                            if (root.doseValue > 0) {
                                root.ratio = newValue / root.doseValue
                            }
                        }
                    }

                    // Save the shown stop-at weight to the baseline: the profile
                    // normally, the active recipe's yieldG in recipe mode (writing
                    // the shared profile there would leak into its sibling recipes).
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(44)
                        text: root.recipeActive
                            ? TranslationManager.translate("brewDialog.updateRecipe", "Update Recipe")
                            : TranslationManager.translate("brewDialog.updateProfile", "Update Profile")
                        accessibleName: root.recipeActive
                            ? TranslationManager.translate("brewDialog.saveStopWeightToRecipe", "Save stop-at-weight to recipe")
                            : TranslationManager.translate("brewDialog.saveStopWeightToProfile", "Save stop-at-weight to profile")
                        primary: true
                        // Enabled ⟺ the value deviates from the active baseline —
                        // exactly the field's override-highlight state (see the Temp
                        // Delta button for the full rationale). `overridden` measures
                        // against recipeYieldBaseline, so it handles an unset yieldG by
                        // falling back to the profile target and collapses to the
                        // profile in no-recipe mode, keeping "Update enabled ⟺ value
                        // highlighted" and letting the recipe baseline move to any value
                        // (including back to the profile default).
                        enabled: targetInput.overridden
                        onClicked: {
                            if (root.recipeActive) {
                                // Absolute grams, matching what activation reads back.
                                // recipeUpdated → MainController refreshes m_activeRecipe.
                                root._pendingRecipeUpdateId = Settings.dye.activeRecipeId
                                MainController.recipeStorage.requestUpdateRecipe(
                                    Settings.dye.activeRecipeId,
                                    {"yieldG": root.targetValue})
                                return
                            }
                            var profile = ProfileManager.getCurrentProfile()
                            if (profile) {
                                profile.target_weight = root.targetValue
                                ProfileManager.uploadProfile(profile)
                            }
                            root.profileTargetWeight = root.targetValue
                            if (ProfileManager.baseProfileName.length > 0) {
                                ProfileManager.saveProfile(ProfileManager.baseProfileName)
                            }
                        }
                    }
                }

                // Profile-default reference, shown only while the stop-at deviates
                // from the ACTIVE baseline (recipeYieldBaseline via targetInput —
                // the recipe's own yield when a recipe is active, else the profile),
                // so a recipe sitting at its own yield no longer shows a spurious
                // amber "Profile: Xg" (recipe-baseline-not-override, #1485).
                Text {
                    // Baseline reference the shown value deviates from — a baseline is
                    // a baseline: the active recipe's own yield when a recipe is active
                    // ("Recipe: 40g"), else the profile default ("Profile: 36g"),
                    // matching the temperature sub-line above.
                    visible: targetInput.overridden
                    text: root.recipeActive
                        ? TranslationManager.translate("brewDialog.recipeDefault", "Recipe: %1g").arg(root.recipeYieldBaseline.toFixed(0))
                        : TranslationManager.translate("brewDialog.profileDefault", "Profile: %1g").arg(root.profileTargetWeight.toFixed(0))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.highlightColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.recipeActive
                        ? TranslationManager.translate("brewDialog.recipeStopWeight", "Recipe stop-at-weight: %1 grams").arg(root.recipeYieldBaseline.toFixed(0))
                        : TranslationManager.translate("brewDialog.profileDefaultStopWeight", "Profile default stop-at-weight: %1 grams").arg(root.profileTargetWeight.toFixed(0))
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
