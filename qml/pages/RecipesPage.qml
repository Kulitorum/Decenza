import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

// Recipes management (add-recipes): mirrors EquipmentPage/BeanInfoPage. All
// non-archived recipes as rows with activate/edit/clone/archive actions;
// archived recipes in a collapsed section below (kept for shot-history
// provenance — a used recipe can never be deleted, only archived).
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

    function recipeSubtitle(r) {
        var parts = []
        if (r.profileTitle) parts.push(r.profileTitle)
        var bean = ((r.roasterName || "") + " " + (r.coffeeName || "")).trim()
        if (bean !== "") parts.push(bean)
        if (r.doseG > 0 && r.yieldG > 0)
            parts.push(Number(r.doseG).toFixed(1) + "g → " + Number(r.yieldG).toFixed(1) + "g")
        return parts.join(" · ")
    }

    Flickable {
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
            width: parent.width
            spacing: Theme.spacingMedium

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
                ActionButton {
                    id: addRecipeButton
                    text: TranslationManager.translate("recipes.addButton", "Add Recipe")
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

            Repeater {
                model: recipesPage.recipes
                delegate: Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: rowContent.implicitHeight + 2 * Theme.spacingMedium
                    radius: Theme.cardRadius
                    color: Theme.surfaceColor
                    border.color: modelData.id === Settings.dye.activeRecipeId ? Theme.accentColor : Theme.borderColor
                    border.width: 1

                    ColumnLayout {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium
                            Label {
                                text: modelData.name
                                font: Theme.subtitleFont
                                color: Theme.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: modelData.id === Settings.dye.activeRecipeId
                                text: trActive.text
                                font: Theme.captionFont
                                color: Theme.accentColor
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: recipesPage.recipeSubtitle(modelData)
                                  + (modelData.shotCount > 0 ? " · " + modelData.shotCount + " " + trShots.text : "")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall
                            ActionButton {
                                text: TranslationManager.translate("recipes.action.activate", "Activate")
                                enabled: modelData.id !== Settings.dye.activeRecipeId
                                onClicked: MainController.activateRecipe(modelData.id)
                            }
                            ActionButton {
                                text: TranslationManager.translate("common.button.edit", "Edit")
                                onClicked: pageStack.push(Qt.resolvedUrl("RecipeComposerPage.qml"),
                                                          { mode: "edit", editRecipeId: modelData.id })
                            }
                            ActionButton {
                                text: TranslationManager.translate("recipes.action.clone", "Clone")
                                onClicked: {
                                    // Clone lands in the composer as a prefilled copy with
                                    // the name focused — rename, tweak, save. Provenance
                                    // points at the source; the golden-shot link is not copied.
                                    var copy = JSON.parse(JSON.stringify(modelData))
                                    delete copy.id
                                    delete copy.shotCount
                                    copy.createdFromShotId = 0
                                    copy.clonedFromRecipeId = modelData.id
                                    copy.name = trCopyOf.text.arg(modelData.name)
                                    pageStack.push(Qt.resolvedUrl("RecipeComposerPage.qml"),
                                                   { mode: "create", prefill: copy })
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton {
                                // A recipe with history archives (provenance must
                                // survive); one with no shots is a mistaken creation
                                // and deletes outright — same rule as bags.
                                text: modelData.shotCount > 0
                                    ? TranslationManager.translate("recipes.action.archive", "Archive")
                                    : TranslationManager.translate("common.button.delete", "Delete")
                                onClicked: {
                                    if (modelData.id === Settings.dye.activeRecipeId)
                                        MainController.deactivateRecipe()
                                    if (modelData.shotCount > 0)
                                        MainController.recipeStorage.requestArchiveRecipe(modelData.id)
                                    else
                                        MainController.recipeStorage.requestDeleteRecipe(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            // --- Archived section ---
            ActionButton {
                visible: recipesPage.archivedRecipes.length > 0
                text: (recipesPage.showArchived
                       ? TranslationManager.translate("recipes.archived.hide", "Hide archived")
                       : TranslationManager.translate("recipes.archived.show", "Show archived"))
                      + " (" + recipesPage.archivedRecipes.length + ")"
                onClicked: recipesPage.showArchived = !recipesPage.showArchived
            }
            Repeater {
                model: recipesPage.showArchived ? recipesPage.archivedRecipes : []
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    Label {
                        text: modelData.name
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: TranslationManager.translate("recipes.action.restore", "Restore")
                        onClicked: MainController.recipeStorage.requestUnarchiveRecipe(modelData.id)
                    }
                }
            }
        }
    }
}
