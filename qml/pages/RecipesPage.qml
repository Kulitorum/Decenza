import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Recipes management (add-recipes): mirrors BeanInfoPage — cards in a Flow
// grid, tap a card to activate the recipe, compact action row per card.
// A recipe with shots archives (provenance must survive); one without is a
// mistaken creation and deletes outright — same lifecycle as bags.
Page {
    id: recipesPage
    objectName: "recipesPage"
    background: Rectangle { color: Theme.backgroundColor }

    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("recipes.title", "Recipes")

    property var recipes: []
    property var archivedRecipes: []
    property bool showArchived: false

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("recipes.title", "Recipes")
        MainController.recipeStorage.requestInventory()
        MainController.recipeStorage.requestArchived()
        addRecipeButton.forceActiveFocus()
    }

    Connections {
        target: MainController.recipeStorage
        function onInventoryReady(list) { recipesPage.recipes = list }
        function onArchivedReady(list) { recipesPage.archivedRecipes = list }
        function onRecipesChanged() {
            MainController.recipeStorage.requestInventory()
            MainController.recipeStorage.requestArchived()
        }
    }

    Tr { id: trShots; key: "recipes.list.shots"; fallback: "shots"; visible: false }
    Tr { id: trActive; key: "recipes.list.active"; fallback: "Active"; visible: false }
    Tr { id: trCopyOf; key: "recipes.list.copyOf"; fallback: "Copy of %1"; visible: false }
    Tr { id: trMilk; key: "recipes.list.milk"; fallback: "milk"; visible: false }

    function openClone(recipe) {
        // Clone lands in the composer as a prefilled copy with the name
        // focused — rename, tweak, save. Provenance points at the source;
        // the golden-shot link is not copied.
        var copy = JSON.parse(JSON.stringify(recipe))
        delete copy.id
        delete copy.shotCount
        copy.createdFromShotId = 0
        copy.clonedFromRecipeId = recipe.id
        copy.name = trCopyOf.text.arg(recipe.name)
        pageStack.push(Qt.resolvedUrl("RecipeComposerPage.qml"), { mode: "create", prefill: copy })
    }

    // Recipe card: tap = activate (like tapping a bag card selects the bag).
    component RecipeCard: Rectangle {
        id: card
        property var recipe: ({})
        property bool archivedCard: false

        readonly property bool selected: recipe && recipe.id !== undefined
            && recipe.id === Settings.dye.activeRecipeId
        readonly property bool hasShots: recipe && (recipe.shotCount ?? 0) > 0
        readonly property var steam: {
            if (!recipe || !recipe.steamJson || String(recipe.steamJson).length === 0) return ({})
            try { return JSON.parse(recipe.steamJson) } catch (e) { return ({}) }
        }

        implicitHeight: cardColumn.implicitHeight + 2 * Theme.spacingMedium
        radius: Theme.cardRadius
        color: Theme.surfaceColor
        border.color: selected ? Theme.accentColor : Theme.borderColor
        border.width: selected ? 2 : 1
        opacity: archivedCard ? 0.7 : 1.0

        function subtitle() {
            var r = card.recipe
            var parts = []
            var bean = ((r.roasterName || "") + " " + (r.coffeeName || "")).trim()
            if (bean !== "") parts.push(bean)
            if (r.profileTitle) parts.push(r.profileTitle)
            if (r.doseG > 0 && r.yieldG > 0)
                parts.push(Number(r.doseG).toFixed(1) + "g → " + Number(r.yieldG).toFixed(1) + "g")
            if (card.steam.hasMilk)
                parts.push(trMilk.text + (card.steam.milkWeightG ? " " + card.steam.milkWeightG + "g" : ""))
            if (r.shotCount > 0)
                parts.push(r.shotCount + " " + trShots.text)
            return parts.join(" · ")
        }

        // Tap anywhere on the card to activate (buttons overlay and win).
        AccessibleMouseArea {
            anchors.fill: parent
            enabled: !card.archivedCard
            accessibleName: TranslationManager.translate("recipes.accessible.activate", "Activate recipe %1").arg(card.recipe.name || "")
            accessibleItem: card
            onAccessibleClicked: {
                if (card.recipe.id !== Settings.dye.activeRecipeId)
                    MainController.activateRecipe(card.recipe.id)
            }
        }

        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Label {
                    text: card.recipe.name || ""
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: card.selected
                    text: trActive.text
                    font: Theme.captionFont
                    color: Theme.accentColor
                }
            }

            Label {
                Layout.fillWidth: true
                text: card.subtitle()
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }

            // Action row — compact, BagCard-style.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)

                StyledIconButton {
                    visible: !card.archivedCard
                    width: Theme.scaled(36)
                    height: Theme.scaled(36)
                    icon.source: "qrc:/icons/edit.svg"
                    accessibleName: TranslationManager.translate("recipes.accessible.edit", "Edit recipe")
                    onClicked: pageStack.push(Qt.resolvedUrl("RecipeComposerPage.qml"),
                                              { mode: "edit", editRecipeId: card.recipe.id })
                }

                AccessibleButton {
                    visible: !card.archivedCard
                    height: Theme.scaled(36)
                    _customFontSize: Theme.captionFont.pixelSize
                    leftPadding: Theme.scaled(10)
                    rightPadding: Theme.scaled(10)
                    text: TranslationManager.translate("recipes.action.clone", "Clone")
                    accessibleName: TranslationManager.translate("recipes.accessible.clone", "Clone this recipe")
                    onClicked: recipesPage.openClone(card.recipe)
                }

                // Used recipe: archive (history keeps its name). Same rule
                // and wording pattern as "Bag finished".
                AccessibleButton {
                    visible: !card.archivedCard && card.hasShots
                    height: Theme.scaled(36)
                    _customFontSize: Theme.captionFont.pixelSize
                    leftPadding: Theme.scaled(10)
                    rightPadding: Theme.scaled(10)
                    text: TranslationManager.translate("recipes.action.archive", "Archive")
                    accessibleName: TranslationManager.translate("recipes.accessible.archive", "Archive: remove from the list; shot history is kept")
                    onClicked: {
                        if (card.recipe.id === Settings.dye.activeRecipeId)
                            MainController.deactivateRecipe()
                        MainController.recipeStorage.requestArchiveRecipe(card.recipe.id)
                    }
                }

                // No shots yet: a mistaken creation — delete outright.
                StyledIconButton {
                    visible: !card.archivedCard && !card.hasShots
                    width: Theme.scaled(36)
                    height: Theme.scaled(36)
                    icon.source: "qrc:/icons/trash.svg"
                    accessibleName: TranslationManager.translate("recipes.accessible.delete", "Delete recipe")
                    accessibleDescription: TranslationManager.translate("recipes.accessible.deleteHint", "Deletes this unused recipe entirely")
                    onClicked: {
                        if (card.recipe.id === Settings.dye.activeRecipeId)
                            MainController.deactivateRecipe()
                        MainController.recipeStorage.requestDeleteRecipe(card.recipe.id)
                    }
                }

                // Archived card: restore is the only action.
                AccessibleButton {
                    visible: card.archivedCard
                    height: Theme.scaled(36)
                    _customFontSize: Theme.captionFont.pixelSize
                    leftPadding: Theme.scaled(10)
                    rightPadding: Theme.scaled(10)
                    text: TranslationManager.translate("recipes.action.restore", "Restore")
                    accessibleName: TranslationManager.translate("recipes.accessible.restore", "Restore this archived recipe")
                    onClicked: MainController.recipeStorage.requestUnarchiveRecipe(card.recipe.id)
                }
            }
        }
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentHeight: contentColumn.implicitHeight + Theme.scaled(20)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: flickable.width
            spacing: Theme.spacingMedium

            // Header row: title + Add Recipe (BeanInfoPage pattern)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                Tr {
                    key: "recipes.title"
                    fallback: "Recipes"
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    id: addRecipeButton
                    primary: true
                    Layout.preferredHeight: Theme.scaled(44)
                    text: TranslationManager.translate("recipes.addButton", "Add Recipe")
                    accessibleName: TranslationManager.translate("recipes.accessible.add", "Add a new recipe")
                    onClicked: pageStack.push(Qt.resolvedUrl("RecipeComposerPage.qml"), { mode: "create" })
                }
            }

            Tr {
                visible: recipesPage.recipes.length === 0
                key: "recipes.emptyHint"
                fallback: "No recipes yet. Save one from a good shot in History, or add one here."
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // Card grid (BeanInfoPage pattern: fixed base width, computed columns)
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: recipesPage.recipes
                    delegate: RecipeCard {
                        recipe: modelData
                        width: {
                            var avail = flickable.width
                            var cardW = Theme.scaled(380)
                            var columns = Math.max(1, Math.floor(avail / cardW))
                            return (avail - (columns - 1) * Theme.spacingMedium) / columns
                        }
                    }
                }
            }

            // --- Archived section ---
            AccessibleButton {
                visible: recipesPage.archivedRecipes.length > 0
                height: Theme.scaled(36)
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: (recipesPage.showArchived
                       ? TranslationManager.translate("recipes.archived.hide", "Hide archived")
                       : TranslationManager.translate("recipes.archived.show", "Show archived"))
                      + " (" + recipesPage.archivedRecipes.length + ")"
                accessibleName: text
                onClicked: recipesPage.showArchived = !recipesPage.showArchived
            }

            Flow {
                visible: recipesPage.showArchived
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: recipesPage.showArchived ? recipesPage.archivedRecipes : []
                    delegate: RecipeCard {
                        recipe: modelData
                        archivedCard: true
                        width: {
                            var avail = flickable.width
                            var cardW = Theme.scaled(380)
                            var columns = Math.max(1, Math.floor(avail / cardW))
                            return (avail - (columns - 1) * Theme.spacingMedium) / columns
                        }
                    }
                }
            }
        }
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: root.goBack()
    }
}
