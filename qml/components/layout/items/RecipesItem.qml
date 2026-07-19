import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

// Recipes quick-switch (add-recipes): structural mirror of BeansItem. Tap
// toggles a pill row of the five most-recently-used recipes; a pill tap
// activates the recipe (profile + bag + equipment + dose/yield/temp + the
// recipe's own grind + steam, via MainController's single activation path). Double-tap
// or long-press opens the Recipes management page; with zero recipes a plain
// tap goes straight there. MRU replaces any favorite flag.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    // True when the app is allowed to start machine operations on-screen — same
    // gate as the profile pills (EspressoItem). An active GHC takes exclusive
    // control, so on-screen starts are only valid headless or in simulation.
    readonly property bool canStartOperations: DE1Device.isHeadless || DE1Device.simulationMode

    // Two-tap select-then-start, identical to the regular-layout recipe pill
    // (IdlePage.tryStartRecipe) so both layouts behave the same. Selection is
    // the synchronous, shared MainController.selectedRecipeId; the first tap
    // selects (activates), a tap on the already-selected recipe starts the shot.
    function tryStartRecipe(recipe) {
        var alreadySelected = (recipe.id === MainController.selectedRecipeId)
        if (!alreadySelected) {
            MainController.activateRecipe(recipe.id)  // sets selectedRecipeId synchronously
            return
        }
        if (!root.canStartOperations) {
            console.log("[recipe pill/compact] start blocked: app cannot start operations (active GHC?) — recipe=" + recipe.id
                        + " isHeadless=" + DE1Device.isHeadless + " simulationMode=" + DE1Device.simulationMode)
        } else if (!MachineState.isReady) {
            console.log("[recipe pill/compact] start blocked: machine not ready — recipe=" + recipe.id
                        + " phase=" + MachineState.phase)
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
        } else {
            // Deferred in MainController until the recipe's profile is applied,
            // so a fast second tap can't pull a shot on the previous profile.
            console.log("[recipe pill/compact] requesting start — recipe=" + recipe.id + " phase=" + MachineState.phase)
            MainController.startSelectedRecipeShotWhenApplied()
        }
    }

    // The full MRU-ordered non-archived recipe list (inventoryReady is MRU-ordered);
    // the popup pages through it five at a time (add-idle-pill-pagination). The full
    // list also lives on the Recipes page.
    property var inventoryRecipes: []
    readonly property int pillPageSize: 5
    property int recipePageIndex: 0
    readonly property int recipePageCount: Math.max(1, Math.ceil(inventoryRecipes.length / root.pillPageSize))
    readonly property var visibleRecipes: inventoryRecipes.slice(recipePageIndex * root.pillPageSize,
                                                                recipePageIndex * root.pillPageSize + root.pillPageSize)

    Component.onCompleted: MainController.recipeStorage.requestInventory()

    Connections {
        target: MainController.recipeStorage
        function onInventoryReady(recipes) {
            root.inventoryRecipes = recipes
            root.recipePageIndex = Math.max(0, Math.min(root.recipePageIndex, root.recipePageCount - 1))
        }
        function onRecipesChanged() {
            MainController.recipeStorage.requestInventory()
        }
    }

    // Highlight while this mode is selected on the home screen, or — in
    // compact mode, where tapping opens presetPopup — while the popup is open.
    readonly property bool isActive:
        (idlePage ? idlePage.activePresetFunction : "") === "recipes" || presetPopup.visible

    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function togglePresets() {
        if (root.isCompact) {
            if (root.inventoryRecipes.length === 0) {
                goToRecipes()
            } else {
                presetPopup.visible ? presetPopup.close() : presetPopup.open()
            }
        } else if (root.inventoryRecipes.length === 0) {
            goToRecipes()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "recipes") ? "" : "recipes"
        }
    }

    function goToRecipes() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/RecipesPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/espresso.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.isActive ? Theme.accentColor : Theme.textColor
                }
            }
            Tr {
                key: "idle.button.recipes"
                fallback: "Recipes"
                font: Theme.bodyFont
                color: root.isActive ? Theme.accentColor : Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.recipes", "Recipes")
                            + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: TranslationManager.translate("idle.accessible.recipes.hint", "Tap to toggle recipe pills. Double-tap or long-press for the recipe list.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToRecipes()
            onAccessibleLongPressed: root.goToRecipes()
        }
    }

    // --- RECIPE PILL POPUP ---
    // Dialog (not Popup) so TalkBack can trap focus inside the pill list —
    // same reasoning as BeansItem's bag pill popup.
    Dialog {
        id: presetPopup
        modal: true
        dim: false
        header: null
        footer: null
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        // Announce the list for screen-reader users (feature parity with the
        // full-mode path, which announces via IdlePage).
        onAboutToShow: root.recipePageIndex = 0  // Always open on the most-recent five.

        onOpened: {
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var recipes = root.visibleRecipes
            if (recipes.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < recipes.length; ++i) {
                names.push(recipes[i].name)
                if (recipes[i].id === MainController.selectedRecipeId) selectedName = recipes[i].name
            }
            var announcement = recipes.length + " " + TranslationManager.translate("idle.accessible.recipes", "recipes") + ": " + names.join(", ")
            if (selectedName !== "") {
                announcement += ". " + selectedName + " " + TranslationManager.translate("idle.accessible.isSelected", "is selected")
            }
            AccessibilityManager.announce(announcement)
        }

        width: {
            var win = root.Window.window
            var w = Theme.scaled(600) + 2 * padding
            return win ? Math.min(w, win.width) : w
        }

        y: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
            if (win) {
                var globalY = root.mapToItem(null, 0, 0).y
                var spaceBelow = win.height - globalY - root.height - Theme.spacingSmall
                var spaceAbove = globalY - Theme.spacingSmall
                if (height > spaceBelow && spaceAbove > spaceBelow)
                    return -height - Theme.spacingSmall
            }
            return parent.height + Theme.spacingSmall
        }

        x: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
            if (win) {
                var globalX = root.mapToItem(null, 0, 0).x
                var centered = -width / 2 + parent.width / 2
                if (globalX + centered + width > win.width)
                    centered = win.width - globalX - width
                if (globalX + centered < 0)
                    centered = -globalX
                return centered
            }
            return -width / 2 + parent.width / 2
        }

        background: Rectangle {
            // Over a custom background image, float the pills directly (matching
            // the center inline preset rows) instead of showing a panel; keep the
            // opaque surface panel when no background image is set.
            readonly property bool hasBackgroundImage: Settings.theme.backgroundImagePath.length > 0
            color: hasBackgroundImage ? "transparent" : Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: hasBackgroundImage ? 0 : 1
        }

        contentItem: PresetPillRow {
            id: recipesPillRow
            maxWidth: Theme.scaled(600)
            // Drink-type icon per pill (add-recipe-wizard-tea): the stored
            // drinkType, with a block-derived fallback for legacy rows (the
            // DrinkType singleton — no profile lookup). A stale recipe (its
            // linked bag finished) dims but still activates.
            presets: root.visibleRecipes.map(function(r) {
                return { name: r.name,
                         icon: DrinkType.icon(DrinkType.fromRecipeMap(r)),
                         dimmed: r.stale === true,
                         stateHint: r.stale === true ? TranslationManager.translate(
                             "recipes.pill.bagFinished", "bag finished") : "" }
            })
            selectedIndex: {
                var list = root.visibleRecipes
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === MainController.selectedRecipeId) return i
                }
                return -1
            }

            pageCount: root.recipePageCount
            pageIndex: root.recipePageIndex
            prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousRecipes", "Previous recipes")
            nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextRecipes", "Next recipes")
            onPageChangeRequested: function(delta) {
                root.recipePageIndex = Math.max(0, Math.min(root.recipePageIndex + delta, root.recipePageCount - 1))
            }

            onPresetSelected: function(index) {
                var recipe = root.visibleRecipes[index]
                if (!recipe) return
                // First tap selects (activates); tapping the selected recipe
                // again starts the shot — same as the regular-layout pill row.
                root.tryStartRecipe(recipe)
                presetPopup.close()
            }
        }
    }
}
