import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Recipe composer (add-recipes): ONE window for all recipe creation and
// editing. Three entry points land here — blank (RecipesPage "Add"),
// prefilled from a shot (promote buttons in history/detail/auto-favorites,
// via `promoteShotId`), and clone (RecipesPage clone action, via `prefill`).
// Only name + profile are required; bean and equipment offer "none" — the
// optionality ladder applies inside the recipe too.
//
// Layout: everything on one page. A centered column with the name + Save/
// Cancel up top, then four section cards in two INDEPENDENT columns on wide
// screens (Profile+Equipment | Bean+Steam), single column when narrow. Grind
// lives inside the Bean card behind an override switch. Pickers are
// lightweight in-page list dialogs; the profile picker has a search box.
Page {
    id: composerPage
    objectName: "recipeComposerPage"
    background: Rectangle { color: Theme.backgroundColor }

    // "create" | "edit". Edit loads the row; create starts from `prefill`
    // (possibly empty) or `promoteShotId`.
    property string mode: "create"
    property int editRecipeId: -1
    // Recipe-shaped map (camelCase RecipeStorage keys) used to seed the form:
    // clone prefills through this directly.
    property var prefill: ({})
    // Promote-from-shot entry point: the source shot id. The composer loads
    // the shot record itself and builds the prefill from it (steam comes from
    // the shot's snapshot when present, else the current steam settings).
    property real promoteShotId: 0

    // --- form state ---
    property string fProfileTitle: ""
    property string fProfileJson: ""
    // Temp override works like the shot plan / Brew Settings: an OFFSET on
    // the profile's temperature ("+2°"), stored absolute (profile + delta)
    // in the recipe. Needs the profile's base temp to be meaningful.
    property real fProfileTempC: 0
    property real fTempDeltaC: 0
    // The recipe's saved absolute temp override, preserved verbatim when the
    // profile's base temp can't be resolved (profile not installed locally) —
    // so editing such a recipe doesn't silently drop its override to 0.
    property real fLoadedTempOverrideC: 0
    // True while our own save() is in flight — so the composer only pops on
    // OUR create, not on an unrelated recipe created by MCP/web/clone
    // elsewhere (recipeCreated carries no request correlation).
    property bool _submitting: false
    property string fBeanBaseId: ""
    property string fRoaster: ""
    property string fCoffee: ""
    property real fEquipmentId: 0
    property string fEquipmentName: ""
    // Whether the linked package's grinder has adjustable rpm (registry-
    // derived). RPM is only offered when it does — same rule as the dye
    // rpm field elsewhere in the app.
    property bool fEquipmentRpmCapable: false
    // Grind override (add-recipes): OFF (default, bean linked) = the recipe
    // follows the bean's bag — shown read-only. ON = grind + rpm are this
    // recipe's own. Bean-less recipes always store grind locally (no switch).
    property bool fGrindOverride: false
    property string fInheritedGrind: ""  // display-only: the linked bag's current grind
    property real fInheritedRpm: 0       // display-only: the linked bag's current rpm
    property bool fHasMilk: false
    property real fMilkWeightG: 0
    property string fPitcherName: ""
    property int fPitcherDurationSec: 0
    property int fPitcherFlow: 0
    property real fPitcherTemperatureC: 0
    property string errorMessage: ""
    property string bagSwapHint: ""
    // Auto-suggested name ("<bean> <profile>"): applied while the field is
    // empty or still holds the previous suggestion — never over a user edit.
    property string _autoName: ""

    function suggestName() {
        var bean = (fCoffee !== "" ? fCoffee : fRoaster).trim()
        var parts = []
        if (bean !== "") parts.push(bean)
        if (fProfileTitle !== "") parts.push(fProfileTitle)
        var suggestion = parts.join(" · ")
        if (suggestion === "" || (nameField.text !== "" && nameField.text !== _autoName))
            return
        nameField.text = suggestion
        _autoName = suggestion
    }

    readonly property bool hasBean: fBeanBaseId !== "" || fRoaster !== "" || fCoffee !== ""
    // Four cards — Profile | Bean / Equipment | Steam — as a 2x2 grid on
    // tablets, stacked when narrow. With Save/Cancel in the top row the
    // whole composer fits one page.
    readonly property int gridColumns: width >= Theme.scaled(680) ? 2 : 1

    StackView.onActivated: root.currentPageTitle = mode === "edit"
        ? TranslationManager.translate("recipes.composer.editTitle", "Edit Recipe")
        : TranslationManager.translate("recipes.composer.createTitle", "New Recipe")

    Component.onCompleted: {
        root.currentPageTitle = mode === "edit"
            ? TranslationManager.translate("recipes.composer.editTitle", "Edit Recipe")
            : TranslationManager.translate("recipes.composer.createTitle", "New Recipe")
        if (mode === "edit" && editRecipeId > 0)
            MainController.recipeStorage.requestRecipe(editRecipeId)
        else if (promoteShotId > 0)
            MainController.shotHistory.requestShot(promoteShotId)
        else if (prefill && Object.keys(prefill).length > 0)
            applyRecipeMap(prefill)
        nameField.forceActiveFocus()
    }

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
        if (fEquipmentId > 0)
            MainController.equipmentStorage.requestInventory()
        refreshInheritedGrind()
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
            console.warn("RecipeComposer: bad steam JSON:", e)
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
                console.warn("RecipeComposer: bad beanBaseJson on promoted shot:", e)
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
            // Linked bean → inherit (the bag already carries the dial via
            // write-through); no bean → the shot's grind/rpm live on the recipe.
            grindPinned: hasBeanData ? "" : (shot.grinderSetting || ""),
            rpmPinned: hasBeanData ? 0 : (shot.rpm || 0),
            steamJson: shot.steamJson && shot.steamJson !== "" ? shot.steamJson
                                                               : currentSteamSnapshot(),
            createdFromShotId: promoteShotId
        })
        applyRecipeMap(prefill)
        // Suggest a name from the drink so saving is one edit, not two.
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

    // Resolve the linked bean's current open-bag grind for the inherit hint.
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
            errorMessage = TranslationManager.translate("recipes.composer.errorNoName", "A recipe needs a name")
            return
        }
        if (fProfileTitle === "") {
            errorMessage = TranslationManager.translate("recipes.composer.errorNoProfile", "A recipe needs a profile")
            return
        }
        var map = {
            name: name,
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
            // recompute the absolute — preserve the loaded override verbatim
            // rather than dropping it to 0.
            tempOverrideC: fProfileTempC > 0
                ? (Math.abs(fTempDeltaC) > 0.05 ? fProfileTempC + fTempDeltaC : 0)
                : fLoadedTempOverrideC,
            // Override OFF (with a bean) = inherit: store nothing. Bean-less
            // recipes always keep their grind/rpm on the recipe.
            grindPinned: (!hasBean || fGrindOverride) ? grindField.text.trim() : "",
            // Keep rpm when the grinder is rpm-capable OR a value is already
            // present (loaded/typed) — the capability flag arrives async, so
            // gating on it alone could drop a valid pin saved before the
            // equipment inventory resolved.
            rpmPinned: ((!hasBean || fGrindOverride)
                        && (fEquipmentRpmCapable || (parseInt(rpmField.text) || 0) > 0))
                ? (parseInt(rpmField.text) || 0) : 0,
            steamJson: buildSteamJson()
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

    Connections {
        target: MainController.shotHistory
        enabled: composerPage.promoteShotId > 0
        function onShotReady(id, shot) {
            if (id === composerPage.promoteShotId)
                composerPage.prefillFromShot(shot)
        }
    }

    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, recipe) {
            if (composerPage.mode === "edit" && recipeId === composerPage.editRecipeId
                && Object.keys(recipe).length > 0)
                composerPage.applyRecipeMap(recipe)
        }
        function onRecipeCreated(recipeId, recipe) {
            // Only react to OUR own submission — recipeCreated carries no
            // request id, so an unrelated create (MCP/web/clone elsewhere)
            // must not pop this page and discard the user's in-progress edit.
            if (composerPage.mode !== "edit" && composerPage._submitting) {
                composerPage._submitting = false
                if (recipeId > 0)
                    pageStack.pop()
                else
                    composerPage.errorMessage =
                        TranslationManager.translate("recipes.composer.errorSave", "Could not save the recipe")
            }
        }
        function onRecipeUpdated(recipeId, success) {
            if (composerPage.mode === "edit" && recipeId === composerPage.editRecipeId
                && composerPage._submitting) {
                composerPage._submitting = false
                if (success)
                    pageStack.pop()
                else
                    composerPage.errorMessage =
                        TranslationManager.translate("recipes.composer.errorSave", "Could not save the recipe")
            }
        }
    }

    // Inventory arrives for the bag picker, the inherit-grind hint, and the
    // equipment display name.
    property var _bags: []
    property var _packages: []
    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            composerPage._bags = bags
            if (composerPage.hasBean) {
                for (var i = 0; i < bags.length; ++i) {
                    var b = bags[i]
                    var match = composerPage.fBeanBaseId !== ""
                        ? b.beanBaseId === composerPage.fBeanBaseId
                        : (String(b.roasterName).toLowerCase() === composerPage.fRoaster.toLowerCase()
                           && String(b.coffeeName).toLowerCase() === composerPage.fCoffee.toLowerCase())
                    if (match) {
                        composerPage.fInheritedGrind = b.grinderSetting || ""
                        composerPage.fInheritedRpm = b.rpm || 0
                        return
                    }
                }
                composerPage.fInheritedGrind = ""
                composerPage.fInheritedRpm = 0
            }
        }
    }
    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            composerPage._packages = packages
            if (composerPage.fEquipmentId > 0) {
                for (var i = 0; i < packages.length; ++i) {
                    if (packages[i].id === composerPage.fEquipmentId) {
                        composerPage.fEquipmentName = packages[i].name
                            || (packages[i].grinderBrand + " " + packages[i].grinderModel).trim()
                        composerPage.fEquipmentRpmCapable = !!packages[i].rpmCapable
                        return
                    }
                }
                composerPage.fEquipmentRpmCapable = false
            }
        }
    }

    // Hidden Tr instances for property-bound strings.
    Tr { id: trNone; key: "recipes.composer.none"; fallback: "None"; visible: false }
    Tr { id: trInherited; key: "recipes.composer.grindInherited"; fallback: "Follows the bag"; visible: false }

    // --- Reusable pieces -----------------------------------------------

    // A labeled, field-styled picker button: label above, current value in a
    // bordered field with a chevron. Reads as an input, not a giant button.
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

    // A labeled numeric field (label above the input).
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

    // Section card with a title.
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
                text: sectionCard.title
                font: Theme.subtitleFont
                color: Theme.textColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }
    }

    // --- Page body ------------------------------------------------------

    KeyboardAwareContainer {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        textFields: [nameField, doseField.input, yieldField.input, grindField, rpmField, milkField.input]

        Flickable {
            anchors.fill: parent
            contentHeight: outerColumn.implicitHeight + Theme.scaled(24)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: outerColumn
                width: Math.min(parent.width - 2 * Theme.standardMargin, Theme.scaled(1400))
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingMedium

                // Top row: the name field with Save/Cancel beside it — the
                // actions stay on screen without their own row. The field is
                // labeled (it opens focused, which hides the placeholder).
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
                        text: TranslationManager.translate("common.save", "Save")
                        accessibleName: TranslationManager.translate("recipes.composer.accessible.save", "Save the recipe")
                        onClicked: composerPage.save()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: composerPage.gridColumns
                    columnSpacing: Theme.spacingMedium
                    rowSpacing: Theme.spacingMedium

                    // Two INDEPENDENT columns (not grid rows): a growing
                    // card (e.g. the grind override revealing its fields)
                    // only pushes its own column down — Equipment stays
                    // right under Profile.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.spacingMedium
                        // ------ Profile + targets ------
                        SectionCard {
                            title: TranslationManager.translate("recipes.composer.sectionProfile", "Profile")

                            PickerField {
                                Layout.fillWidth: true
                                label: TranslationManager.translate("recipes.composer.profileLabel", "Profile") + " *"
                                value: composerPage.fProfileTitle
                                placeholder: TranslationManager.translate("recipes.composer.chooseProfile", "Choose profile…")
                                onActivated: profilePicker.openPicker()
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                NumberField {
                                    id: doseField
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: Theme.scaled(120)
                                    label: TranslationManager.translate("recipes.composer.doseLabel", "Dose (g)")
                                }
                                NumberField {
                                    id: yieldField
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: Theme.scaled(120)
                                    label: TranslationManager.translate("recipes.composer.yieldLabel", "Yield (g)")
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: Theme.scaled(120)
                                    spacing: Theme.scaled(4)
                                    Label {
                                        text: TranslationManager.translate("recipes.composer.tempOffsetLabel", "Temp offset")
                                        font: Theme.captionFont
                                        color: Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                    // Same control as Brew Settings: an offset on the
                                    // profile's temperature, 0° = no override.
                                    ValueInput {
                                        id: tempInput
                                        Layout.fillWidth: true
                                        enabled: composerPage.fProfileTempC > 0
                                        readonly property real displayDelta: Theme.cDeltaToDisplay(composerPage.fTempDeltaC)
                                        value: displayDelta
                                        from: composerPage.fProfileTempC > 0
                                            ? Theme.cDeltaToDisplay(70 - composerPage.fProfileTempC) : -10
                                        to: composerPage.fProfileTempC > 0
                                            ? Theme.cDeltaToDisplay(100 - composerPage.fProfileTempC) : 10
                                        stepSize: 1
                                        decimals: 0
                                        suffix: "°"
                                        displayText: (displayDelta > 0 ? "+" : "") + displayDelta.toFixed(0) + "°"
                                        valueColor: Math.abs(composerPage.fTempDeltaC) > 0.1 ? Theme.temperatureColor : Theme.textSecondaryColor
                                        accentColor: Theme.temperatureColor
                                        accessibleName: TranslationManager.translate("recipes.composer.tempOffsetAccessible", "Brew temperature offset")
                                        onValueModified: function(newValue) {
                                            composerPage.fTempDeltaC = Theme.displayToCDelta(newValue)
                                        }
                                    }
                                }
                            }
                        }
                        // ------ Equipment ------
                        SectionCard {
                            title: TranslationManager.translate("recipes.composer.sectionEquipment", "Equipment")

                            PickerField {
                                Layout.fillWidth: true
                                label: TranslationManager.translate("recipes.composer.equipmentLabel", "Grinder / basket package")
                                value: composerPage.fEquipmentId > 0 ? composerPage.fEquipmentName : ""
                                placeholder: trNone.text
                                onActivated: { MainController.equipmentStorage.requestInventory(); equipmentPicker.open() }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.spacingMedium
                        // ------ Beans & grind ------
                        SectionCard {
                            title: TranslationManager.translate("recipes.composer.sectionBean", "Bean")

                            PickerField {
                                Layout.fillWidth: true
                                label: TranslationManager.translate("recipes.composer.beanLabel", "Coffee bag")
                                value: composerPage.hasBean
                                    ? (composerPage.fRoaster + " " + composerPage.fCoffee).trim() : ""
                                placeholder: trNone.text
                                onActivated: { MainController.bagStorage.requestInventory(); bagPicker.open() }
                            }
                            Label {
                                visible: composerPage.bagSwapHint !== ""
                                Layout.fillWidth: true
                                text: composerPage.bagSwapHint
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }

                            // Grind: one block. Bean linked + no override →
                            // read-only "Follows the bean" text; override ON
                            // (or no bean) → the grind/rpm fields sit exactly
                            // where that text was.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(4)
                                    Label {
                                        text: TranslationManager.translate("recipes.composer.grindLabel", "Grind")
                                            + (composerPage.hasBean ? ""
                                               : " — " + TranslationManager.translate("recipes.composer.grindNoBean", "stored on the recipe (no bean linked)"))
                                        font: Theme.captionFont
                                        color: Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                    Label {
                                        visible: composerPage.hasBean && !composerPage.fGrindOverride
                                        Layout.fillWidth: true
                                        text: {
                                            var inherited = trInherited.text
                                            if (composerPage.fInheritedGrind !== "") {
                                                inherited += ": " + composerPage.fInheritedGrind
                                                if (composerPage.fEquipmentRpmCapable && composerPage.fInheritedRpm > 0)
                                                    inherited += " · " + composerPage.fInheritedRpm + " rpm"
                                            }
                                            return inherited
                                        }
                                        font: Theme.bodyFont
                                        color: Theme.textColor
                                        wrapMode: Text.WordWrap
                                    }
                                    RowLayout {
                                        visible: !composerPage.hasBean || composerPage.fGrindOverride
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
                                            visible: composerPage.fEquipmentRpmCapable
                                            Layout.preferredWidth: Theme.scaled(110)
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            placeholder: TranslationManager.translate("recipes.composer.rpmLabel", "RPM")
                                            Accessible.name: TranslationManager.translate("recipes.composer.rpmLabel", "RPM")
                                        }
                                    }
                                }
                                ColumnLayout {
                                    visible: composerPage.hasBean
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
                                        checked: composerPage.fGrindOverride
                                        Accessible.name: TranslationManager.translate("recipes.composer.grindOverride", "Override grind for this recipe")
                                        onToggled: {
                                            composerPage.fGrindOverride = checked
                                            if (checked && grindField.text === "") {
                                                // Start the override from the inherited values.
                                                grindField.text = composerPage.fInheritedGrind
                                                rpmField.text = composerPage.fEquipmentRpmCapable
                                                        && composerPage.fInheritedRpm > 0
                                                    ? String(composerPage.fInheritedRpm) : ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        // ------ Steam ------
                        SectionCard {
                            title: TranslationManager.translate("recipes.composer.sectionSteam", "Steam")

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                Label {
                                    Layout.fillWidth: true
                                    text: TranslationManager.translate("recipes.composer.milkDrink", "Milk drink")
                                    font: Theme.bodyFont
                                    color: Theme.textColor
                                    Accessible.ignored: true
                                }
                                StyledSwitch {
                                    checked: composerPage.fHasMilk
                                    Accessible.name: TranslationManager.translate("recipes.composer.milkDrink", "Milk drink")
                                    onToggled: composerPage.fHasMilk = checked
                                }
                            }
                            Label {
                                visible: composerPage.fHasMilk
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipes.composer.milkHint",
                                      "Keeps the steam heater warm while this recipe is active (5–9 min warm-up).")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                visible: composerPage.fHasMilk
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium
                                PickerField {
                                    Layout.fillWidth: true
                                    label: TranslationManager.translate("recipes.composer.pitcher", "Pitcher")
                                    value: composerPage.fPitcherName
                                    placeholder: TranslationManager.translate("recipes.composer.choosePitcher", "Choose pitcher…")
                                    onActivated: pitcherPicker.open()
                                }
                                NumberField {
                                    id: milkField
                                    Layout.fillWidth: true
                                    label: TranslationManager.translate("recipes.composer.milkWeight", "Milk (g)")
                                    text: composerPage.fMilkWeightG > 0 ? String(composerPage.fMilkWeightG) : ""
                                    onEdited: function(newText) { composerPage.fMilkWeightG = parseFloat(newText) || 0 }
                                }
                            }
                        }
                    }

                }

                Label {
                    visible: composerPage.errorMessage !== ""
                    Layout.fillWidth: true
                    text: composerPage.errorMessage
                    font: Theme.bodyFont
                    color: Theme.errorColor
                    wrapMode: Text.WordWrap
                }

            }
        }
    }

    // --- Pickers: lightweight list dialogs (selection only — the full
    // management UIs stay on their own pages/dialogs) ---

    component PickerDialog: Dialog {
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(520), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(620), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
    }

    PickerDialog {
        id: profilePicker
        property string filter: ""
        function openPicker() {
            filter = ""
            open()
            profileSearchField.forceActiveFocus()
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacingSmall
            StyledTextField {
                id: profileSearchField
                Layout.fillWidth: true
                placeholder: TranslationManager.translate("profileselector.search", "Search profiles…")
                Accessible.name: placeholderText
                onTextChanged: profilePicker.filter = text.trim().toLowerCase()
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: {
                    var all = ProfileManager.allProfilesList
                    if (profilePicker.filter === "")
                        return all
                    var out = []
                    for (var i = 0; i < all.length; ++i) {
                        if (String(all[i].title).toLowerCase().indexOf(profilePicker.filter) >= 0)
                            out.push(all[i])
                    }
                    return out
                }
                delegate: ItemDelegate {
                    width: ListView.view.width
                    contentItem: Label {
                        text: modelData.title
                        font: Theme.bodyFont
                        color: Theme.textColor
                        elide: Text.ElideRight
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.title
                    onClicked: {
                        composerPage.fProfileTitle = modelData.title
                        composerPage.fProfileJson = ""  // installed profile: resolve by title
                        // Pull the profile's own numbers into empty fields so
                        // picking a profile seeds the drink — never clobbers a
                        // value the user (or a shot prefill) already set.
                        var detail = ProfileManager.getProfileByFilename(modelData.name)
                        if (detail.recommended_dose > 0 && doseField.text === "")
                            doseField.text = Number(detail.recommended_dose).toFixed(1)
                        if (detail.target_weight > 0 && yieldField.text === "")
                            yieldField.text = Number(detail.target_weight).toFixed(1)
                        composerPage.fProfileTempC = detail.espresso_temperature || 0
                        composerPage.suggestName()
                        profilePicker.close()
                    }
                }
            }
        }
    }

    PickerDialog {
        id: bagPicker
        contentItem: ListView {
            clip: true
            // "None" sentinel first, then the open-bag inventory.
            model: [{ isNone: true }].concat(composerPage._bags)
            delegate: ItemDelegate {
                width: ListView.view.width
                contentItem: Label {
                    text: modelData.isNone ? trNone.text
                        : ((modelData.roasterName || "") + " " + (modelData.coffeeName || "")).trim()
                    font: Theme.bodyFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                }
                Accessible.role: Accessible.Button
                Accessible.name: contentItem.text
                onClicked: {
                    var hadBean = composerPage.hasBean
                    if (modelData.isNone) {
                        // No bean to inherit from: carry the current dial onto
                        // the recipe so nothing is silently lost.
                        if (!composerPage.fGrindOverride && grindField.text === "") {
                            grindField.text = composerPage.fInheritedGrind
                            rpmField.text = composerPage.fInheritedRpm > 0
                                ? String(composerPage.fInheritedRpm) : ""
                        }
                        composerPage.fBeanBaseId = ""
                        composerPage.fRoaster = ""
                        composerPage.fCoffee = ""
                        composerPage.fInheritedGrind = ""
                        composerPage.fInheritedRpm = 0
                    } else {
                        composerPage.fBeanBaseId = modelData.beanBaseId || ""
                        composerPage.fRoaster = modelData.roasterName || ""
                        composerPage.fCoffee = modelData.coffeeName || ""
                        composerPage.fInheritedGrind = modelData.grinderSetting || ""
                        composerPage.fInheritedRpm = modelData.rpm || 0
                        if (hadBean && !composerPage.fGrindOverride)
                            composerPage.bagSwapHint = TranslationManager.translate(
                                "recipes.composer.grindFollowsHint", "Grind now follows %1: %2")
                                .arg(contentItem.text).arg(composerPage.fInheritedGrind || "—")
                        composerPage.suggestName()
                    }
                    bagPicker.close()
                }
            }
        }
    }

    PickerDialog {
        id: equipmentPicker
        contentItem: ListView {
            clip: true
            model: [{ isNone: true }].concat(composerPage._packages)
            delegate: ItemDelegate {
                width: ListView.view.width
                contentItem: Label {
                    text: modelData.isNone ? trNone.text
                        : (modelData.name || ((modelData.grinderBrand || "") + " " + (modelData.grinderModel || "")).trim())
                    font: Theme.bodyFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                }
                Accessible.role: Accessible.Button
                Accessible.name: contentItem.text
                onClicked: {
                    if (modelData.isNone) {
                        composerPage.fEquipmentId = 0
                        composerPage.fEquipmentName = ""
                        composerPage.fEquipmentRpmCapable = false
                    } else {
                        composerPage.fEquipmentId = modelData.id
                        composerPage.fEquipmentName = contentItem.text
                        composerPage.fEquipmentRpmCapable = !!modelData.rpmCapable
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
                    // Snapshot BY VALUE (never a preset index): the recipe
                    // keeps steaming as saved even if presets change later.
                    composerPage.fPitcherName = modelData.name || ""
                    composerPage.fPitcherDurationSec = modelData.duration || 0
                    composerPage.fPitcherFlow = modelData.flow || 0
                    composerPage.fPitcherTemperatureC = modelData.temperature || 0
                    pitcherPicker.close()
                }
            }
        }
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: root.goBack()
    }
}
