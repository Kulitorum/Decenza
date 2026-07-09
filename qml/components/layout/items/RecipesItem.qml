import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

// Recipes quick-switch (add-recipes): structural mirror of BeansItem. Tap
// toggles a pill row of the five most-recently-used recipes; a pill tap
// activates the recipe (profile + bag + equipment + dose/yield/temp + grind
// routing + steam, via MainController's single activation path). Double-tap
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

    // The five most-recently-used non-archived recipes (inventoryReady is
    // MRU-ordered); the full list lives on the Recipes page.
    property var inventoryRecipes: []

    Component.onCompleted: MainController.recipeStorage.requestInventory()

    Connections {
        target: MainController.recipeStorage
        function onInventoryReady(recipes) {
            root.inventoryRecipes = recipes.slice(0, 5)
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
                source: "qrc:/icons/pin.svg"
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
        onOpened: {
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var recipes = root.inventoryRecipes
            if (recipes.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < recipes.length; ++i) {
                names.push(recipes[i].name)
                if (recipes[i].id === Settings.dye.activeRecipeId) selectedName = recipes[i].name
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
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: PresetPillRow {
            id: recipesPillRow
            maxWidth: Theme.scaled(600)
            // Drink-type icon per pill (add-recipe-wizard-tea): the stored
            // drinkType, with a block-derived fallback for legacy rows (no
            // profile lookup here — milk/water are the cheap signals).
            presets: root.inventoryRecipes.map(function(r) {
                var t = r.drinkType || ""
                if (t === "") {
                    var milk = false, water = false
                    try { milk = !!(r.steamJson && JSON.parse(r.steamJson).hasMilk) } catch (e) {}
                    try { water = !!(r.hotWaterJson && JSON.parse(r.hotWaterJson).hasWater) } catch (e) {}
                    t = milk ? "latte" : (water ? "americano" : "espresso")
                }
                var icon = "qrc:/icons/espresso.svg"
                if (t === "filter") icon = "qrc:/icons/filter.svg"
                else if (t === "americano" || t === "long_black") icon = "qrc:/icons/water.svg"
                else if (t === "latte") icon = "qrc:/icons/steam.svg"
                else if (t === "tea" || t === "tea_hotwater") icon = "qrc:/icons/tea.svg"
                return { name: r.name, icon: icon }
            })
            selectedIndex: {
                var list = root.inventoryRecipes
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === Settings.dye.activeRecipeId) return i
                }
                return -1
            }

            onPresetSelected: function(index) {
                var recipe = root.inventoryRecipes[index]
                if (!recipe) return
                MainController.activateRecipe(recipe.id)
                presetPopup.close()
            }
        }
    }
}
