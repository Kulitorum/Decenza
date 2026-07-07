import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Recipe composer (add-recipes): ONE window for all recipe creation and
// editing. Three entry points land here — blank (RecipesPage "Add"),
// prefilled from a shot (promote buttons in history/detail/auto-favorites,
// via `prefill`), and clone (RecipesPage clone action, also via `prefill`).
// Only name + profile are required; bag and equipment offer "none" — the
// optionality ladder applies inside the recipe too.
Page {
    id: composerPage
    objectName: "recipeComposerPage"
    background: Rectangle { color: Theme.backgroundColor }

    // "create" | "edit". Edit loads the row; create starts from `prefill`
    // (possibly empty).
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
    property string fBeanBaseId: ""
    property string fRoaster: ""
    property string fCoffee: ""
    property real fEquipmentId: 0
    property string fEquipmentName: ""
    property string fGrindPinned: ""     // "" = inherit from the bean's bag
    property string fInheritedGrind: ""  // display-only: the linked bag's current grind
    property bool fHasMilk: false
    property real fMilkWeightG: 0
    property string fPitcherName: ""
    property int fPitcherDurationSec: 0
    property int fPitcherFlow: 0
    property real fPitcherTemperatureC: 0
    property string errorMessage: ""
    property string bagSwapHint: ""

    readonly property bool hasBean: fBeanBaseId !== "" || fRoaster !== "" || fCoffee !== ""

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
            try { beanBaseId = JSON.parse(shot.beanBaseJson).id || "" } catch (e) {}
        }
        var hasBean = beanBaseId !== "" || shot.beanBrand || shot.beanType
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
            // write-through); no bean → the shot's grind lives on the recipe.
            grindPinned: hasBean ? "" : (shot.grinderSetting || ""),
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

    Connections {
        target: MainController.shotHistory
        enabled: composerPage.promoteShotId > 0
        function onShotReady(id, shot) {
            if (id === composerPage.promoteShotId)
                composerPage.prefillFromShot(shot)
        }
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
        tempField.text = r.tempOverrideC > 0 ? Number(r.tempOverrideC).toFixed(1) : ""
        fGrindPinned = r.grindPinned || ""
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
            tempOverrideC: parseFloat(tempField.text) || 0,
            grindPinned: fGrindPinned.trim(),  // " " is only the form's "pin armed, value pending" marker
            steamJson: buildSteamJson()
        }
        if (prefill && prefill.createdFromShotId)
            map.createdFromShotId = prefill.createdFromShotId
        if (prefill && prefill.clonedFromRecipeId)
            map.clonedFromRecipeId = prefill.clonedFromRecipeId
        if (mode === "edit" && editRecipeId > 0)
            MainController.recipeStorage.requestUpdateRecipe(editRecipeId, map)
        else
            MainController.recipeStorage.requestCreateRecipe(map)
    }

    Connections {
        target: MainController.recipeStorage
        function onRecipeReady(recipeId, recipe) {
            if (composerPage.mode === "edit" && recipeId === composerPage.editRecipeId
                && Object.keys(recipe).length > 0)
                composerPage.applyRecipeMap(recipe)
        }
        function onRecipeCreated(recipeId, recipe) {
            if (composerPage.mode !== "edit") {
                if (recipeId > 0)
                    pageStack.pop()
                else
                    composerPage.errorMessage =
                        TranslationManager.translate("recipes.composer.errorSave", "Could not save the recipe")
            }
        }
        function onRecipeUpdated(recipeId, success) {
            if (composerPage.mode === "edit" && recipeId === composerPage.editRecipeId) {
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
                        return
                    }
                }
                composerPage.fInheritedGrind = ""
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
                        return
                    }
                }
            }
        }
    }

    // Hidden Tr instances for property-bound strings.
    Tr { id: trNone; key: "recipes.composer.none"; fallback: "None"; visible: false }
    Tr { id: trInherited; key: "recipes.composer.grindInherited"; fallback: "Follows the bag"; visible: false }

    KeyboardAwareContainer {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        textFields: [nameField, doseField, yieldField, tempField, grindPinField, milkField]

        Flickable {
            anchors.fill: parent
            contentHeight: formColumn.implicitHeight + Theme.scaled(20)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: formColumn
                width: parent.width
                spacing: Theme.spacingMedium

                Tr {
                    key: composerPage.mode === "edit" ? "recipes.composer.editTitle" : "recipes.composer.createTitle"
                    fallback: composerPage.mode === "edit" ? "Edit Recipe" : "New Recipe"
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                // --- Name (required) ---
                StyledTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: TranslationManager.translate("recipes.composer.namePlaceholder", "Recipe name (e.g. Morning cappuccino)")
                    Accessible.name: TranslationManager.translate("recipes.composer.nameLabel", "Recipe name")
                }

                // --- Profile (required) ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Tr {
                        key: "recipes.composer.profileLabel"; fallback: "Profile"
                        font: Theme.bodyFont; color: Theme.textColor
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: composerPage.fProfileTitle !== "" ? composerPage.fProfileTitle
                            : TranslationManager.translate("recipes.composer.chooseProfile", "Choose profile…")
                        onClicked: profilePicker.open()
                    }
                }

                // --- Bean (optional) ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Tr {
                        key: "recipes.composer.beanLabel"; fallback: "Bean"
                        font: Theme.bodyFont; color: Theme.textColor
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: composerPage.hasBean
                            ? (composerPage.fRoaster + " " + composerPage.fCoffee).trim()
                            : trNone.text
                        onClicked: { MainController.bagStorage.requestInventory(); bagPicker.open() }
                    }
                }
                Label {
                    visible: composerPage.bagSwapHint !== ""
                    Layout.fillWidth: true
                    text: composerPage.bagSwapHint
                    font: Theme.captionFont
                    color: Theme.secondaryTextColor
                    wrapMode: Text.WordWrap
                }

                // --- Equipment (optional) ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Tr {
                        key: "recipes.composer.equipmentLabel"; fallback: "Equipment"
                        font: Theme.bodyFont; color: Theme.textColor
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: composerPage.fEquipmentId > 0 && composerPage.fEquipmentName !== ""
                            ? composerPage.fEquipmentName : trNone.text
                        onClicked: { MainController.equipmentStorage.requestInventory(); equipmentPicker.open() }
                    }
                }

                // --- Dose / yield / temperature ---
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: Theme.spacingMedium
                    Tr { key: "recipes.composer.doseLabel"; fallback: "Dose (g)"; font: Theme.captionFont; color: Theme.secondaryTextColor }
                    Tr { key: "recipes.composer.yieldLabel"; fallback: "Yield (g)"; font: Theme.captionFont; color: Theme.secondaryTextColor }
                    Tr { key: "recipes.composer.tempLabel"; fallback: "Temp override (°C)"; font: Theme.captionFont; color: Theme.secondaryTextColor }
                    StyledTextField {
                        id: doseField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        Accessible.name: TranslationManager.translate("recipes.composer.doseLabel", "Dose (g)")
                    }
                    StyledTextField {
                        id: yieldField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        Accessible.name: TranslationManager.translate("recipes.composer.yieldLabel", "Yield (g)")
                    }
                    StyledTextField {
                        id: tempField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        Accessible.name: TranslationManager.translate("recipes.composer.tempLabel", "Temperature override in Celsius")
                    }
                }

                // --- Grind: inherit (default) or pin ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Tr {
                            key: "recipes.composer.grindLabel"; fallback: "Grind"
                            font: Theme.bodyFont; color: Theme.textColor
                        }
                        Label {
                            Layout.fillWidth: true
                            text: composerPage.fGrindPinned === ""
                                ? (composerPage.hasBean
                                    ? trInherited.text + (composerPage.fInheritedGrind !== "" ? ": " + composerPage.fInheritedGrind : "")
                                    : TranslationManager.translate("recipes.composer.grindNoBean", "Stored on the recipe (no bean linked)"))
                                : TranslationManager.translate("recipes.composer.grindPinnedHint", "Pinned — this recipe keeps its own grind")
                            font: Theme.captionFont
                            color: Theme.secondaryTextColor
                            wrapMode: Text.WordWrap
                        }
                    }
                    StyledSwitch {
                        id: pinSwitch
                        checked: composerPage.fGrindPinned !== "" || !composerPage.hasBean
                        enabled: composerPage.hasBean  // bean-less recipes always keep grind locally
                        Accessible.name: TranslationManager.translate("recipes.composer.pinGrind", "Pin grind to this recipe")
                        onToggled: {
                            if (checked)
                                composerPage.fGrindPinned = grindPinField.text !== "" ? grindPinField.text
                                    : (composerPage.fInheritedGrind !== "" ? composerPage.fInheritedGrind : " ")
                            else
                                composerPage.fGrindPinned = ""
                        }
                    }
                }
                StyledTextField {
                    id: grindPinField
                    Layout.fillWidth: true
                    visible: composerPage.fGrindPinned !== "" || !composerPage.hasBean
                    text: composerPage.fGrindPinned.trim()
                    placeholderText: TranslationManager.translate("recipes.composer.grindPlaceholder", "Grind setting (e.g. 2.4)")
                    Accessible.name: TranslationManager.translate("recipes.composer.grindLabel", "Grind")
                    onTextEdited: composerPage.fGrindPinned = text === "" ? " " : text
                }

                // --- Steam ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Tr {
                        key: "recipes.composer.milkDrink"; fallback: "Milk drink"
                        font: Theme.bodyFont; color: Theme.textColor
                    }
                    Item { Layout.fillWidth: true }
                    StyledSwitch {
                        checked: composerPage.fHasMilk
                        Accessible.name: TranslationManager.translate("recipes.composer.milkDrink", "Milk drink")
                        onToggled: composerPage.fHasMilk = checked
                    }
                }
                GridLayout {
                    visible: composerPage.fHasMilk
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingMedium
                    Tr { key: "recipes.composer.milkWeight"; fallback: "Milk (g)"; font: Theme.captionFont; color: Theme.secondaryTextColor }
                    Tr { key: "recipes.composer.pitcher"; fallback: "Pitcher"; font: Theme.captionFont; color: Theme.secondaryTextColor }
                    StyledTextField {
                        id: milkField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        text: composerPage.fMilkWeightG > 0 ? String(composerPage.fMilkWeightG) : ""
                        Accessible.name: TranslationManager.translate("recipes.composer.milkWeight", "Milk weight in grams")
                        onTextEdited: composerPage.fMilkWeightG = parseFloat(text) || 0
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: composerPage.fPitcherName !== "" ? composerPage.fPitcherName
                            : TranslationManager.translate("recipes.composer.choosePitcher", "Choose pitcher…")
                        onClicked: pitcherPicker.open()
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        onClicked: pageStack.pop()
                    }
                    ActionButton {
                        text: TranslationManager.translate("common.button.save", "Save")
                        highlighted: true
                        onClicked: composerPage.save()
                    }
                }
            }
        }
    }

    // --- Pickers: lightweight list dialogs (selection only — the full
    // management UIs stay on their own pages/dialogs) ---

    Dialog {
        id: profilePicker
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(600), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
        contentItem: ListView {
            clip: true
            model: ProfileManager.allProfilesList
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
                    profilePicker.close()
                }
            }
        }
    }

    Dialog {
        id: bagPicker
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(600), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
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
                        composerPage.fBeanBaseId = ""
                        composerPage.fRoaster = ""
                        composerPage.fCoffee = ""
                        composerPage.fInheritedGrind = ""
                        // No bean to inherit from: grind moves onto the recipe.
                        if (composerPage.fGrindPinned === "")
                            composerPage.fGrindPinned = composerPage.fInheritedGrind || " "
                    } else {
                        composerPage.fBeanBaseId = modelData.beanBaseId || ""
                        composerPage.fRoaster = modelData.roasterName || ""
                        composerPage.fCoffee = modelData.coffeeName || ""
                        composerPage.fInheritedGrind = modelData.grinderSetting || ""
                        if (hadBean && composerPage.fGrindPinned === "")
                            composerPage.bagSwapHint = TranslationManager.translate(
                                "recipes.composer.grindFollowsHint", "Grind now follows %1: %2")
                                .arg(contentItem.text).arg(composerPage.fInheritedGrind || "—")
                    }
                    bagPicker.close()
                }
            }
        }
    }

    Dialog {
        id: equipmentPicker
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(600), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
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
                    } else {
                        composerPage.fEquipmentId = modelData.id
                        composerPage.fEquipmentName = contentItem.text
                    }
                    equipmentPicker.close()
                }
            }
        }
    }

    Dialog {
        id: pitcherPicker
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(400), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
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
}
