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

    // Grid column math (BeanInfoPage pattern: fixed base width, computed
    // columns) — one implementation for both card grids.
    function cardWidth(avail) {
        var columns = Math.max(1, Math.floor(avail / Theme.scaled(380)))
        return (avail - (columns - 1) * Theme.spacingMedium) / columns
    }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("recipes.title", "Recipes")
        MainController.recipeStorage.requestInventory()
        MainController.recipeStorage.requestArchived()
        MainController.bagStorage.requestInventory()
        addRecipeButton.forceActiveFocus()
    }

    // Open-bag inventory: feeds the stale card's re-point picker and the
    // cards' product-link lookup for the photo cache (a non-canonical bag's
    // photo is cached under the BAG's image key "bag-<id>").
    property var _bags: []
    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) { recipesPage._bags = bags }
        function onBagsChanged() { MainController.bagStorage.requestInventory() }
    }

    Connections {
        target: MainController.recipeStorage
        function onInventoryReady(list) { recipesPage.recipes = list }
        function onArchivedReady(list) { recipesPage.archivedRecipes = list }
        function onRecipesChanged() {
            MainController.recipeStorage.requestInventory()
            MainController.recipeStorage.requestArchived()
        }
        // The stale card's re-point is fire-and-forget from the picker — a
        // failure (bag deleted between snapshot and tap, DB error) must not
        // look like the tap was ignored.
        function onRecipeUpdated(recipeId, success) {
            if (recipeId !== recipesPage._repointPendingId)
                return
            recipesPage._repointPendingId = -1
            if (!success) {
                repointFailedToast.opacity = 1
                repointFailedToastTimer.restart()
                if (AccessibilityManager.enabled)
                    AccessibilityManager.announce(trRepointFailed.text, true)
            }
        }
    }
    property int _repointPendingId: -1
    Tr {
        id: trRepointFailed
        key: "recipes.repoint.failed"
        fallback: "Couldn't move the recipe to that bag"
        visible: false
    }
    Rectangle {
        id: repointFailedToast
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        width: repointFailedLabel.implicitWidth + Theme.scaled(32)
        height: repointFailedLabel.implicitHeight + Theme.scaled(16)
        radius: Theme.cardRadius
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1
        opacity: 0
        visible: opacity > 0
        z: 600
        Accessible.ignored: true
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Text {
            id: repointFailedLabel
            anchors.centerIn: parent
            text: trRepointFailed.text
            color: Theme.textColor
            font.pixelSize: Theme.scaled(13)
            Accessible.ignored: true
        }
    }
    Timer {
        id: repointFailedToastTimer
        interval: 4000
        onTriggered: repointFailedToast.opacity = 0
    }

    Tr { id: trCopyOf; key: "recipes.list.copyOf"; fallback: "Copy of %1"; visible: false }

    function openClone(recipe) {
        // Clone lands on the wizard summary as a prefilled copy with the name
        // focused — rename, tweak, save. Provenance points at the source;
        // the golden-shot link is not copied.
        var copy = JSON.parse(JSON.stringify(recipe))
        delete copy.id
        delete copy.shotCount
        copy.createdFromShotId = 0
        copy.clonedFromRecipeId = recipe.id
        copy.name = trCopyOf.text.arg(recipe.name)
        pageStack.push(Qt.resolvedUrl("RecipeWizardPage.qml"), { mode: "create", prefill: copy })
    }

    // The stale card's one-tap re-point: an open-bag picker scoped to one
    // recipe (recipe-bag-lifecycle "manual re-point"). Selecting a bag only
    // moves the bag link (grind pin/inherit is untouched by construction).
    property var _repointRecipe: null
    Dialog {
        id: repointPicker
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(520), parent.width - Theme.scaled(40))
        height: Math.min(Theme.scaled(620), parent.height - Theme.scaled(80))
        background: Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius; border.color: Theme.borderColor; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: Theme.spacingSmall
            Label {
                Layout.fillWidth: true
                text: TranslationManager.translate("recipes.repoint.title", "Choose beans for %1")
                    .arg(recipesPage._repointRecipe ? (recipesPage._repointRecipe.name || "") : "")
                font: Theme.subtitleFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // Kind-matched open bags: a tea recipe re-points to tea bags.
                model: {
                    var r = recipesPage._repointRecipe
                    var wantTea = r && String(DrinkType.fromRecipeMap(r)).indexOf("tea") === 0
                    var out = []
                    for (var i = 0; i < recipesPage._bags.length; ++i) {
                        var b = recipesPage._bags[i]
                        var isTea = String(b.kind || "") === "tea"
                        if (isTea === wantTea)
                            out.push(b)
                    }
                    return out
                }
                delegate: ItemDelegate {
                    width: ListView.view.width
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: ((modelData.roasterName || "") + " " + (modelData.coffeeName || "")).trim()
                            font: Theme.bodyFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: (modelData.roastDate || "") !== ""
                            text: modelData.roastDate || ""
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: ((modelData.roasterName || "") + " " + (modelData.coffeeName || "")).trim()
                    onClicked: {
                        if (recipesPage._repointRecipe) {
                            recipesPage._repointPendingId = recipesPage._repointRecipe.id
                            MainController.recipeStorage.requestRelinkRecipeToBag(
                                recipesPage._repointRecipe.id, modelData.id)
                        }
                        repointPicker.close()
                    }
                }
            }
        }
    }

    // Recipe card: the shared RecipeDrinkCard (also the wizard summary's
    // hero — design D4) plus this page's action row and tap-to-activate.
    component RecipeCard: RecipeDrinkCard {
        id: card
        property bool archivedCard: false

        readonly property bool selected: recipe && recipe.id !== undefined
            && recipe.id === Settings.dye.activeRecipeId
        readonly property bool hasShots: recipe && (recipe.shotCount ?? 0) > 0

        active: selected
        stale: recipe && recipe.stale === true
        showStaleAction: !archivedCard
        onStaleActionClicked: {
            recipesPage._repointRecipe = card.recipe
            repointPicker.open()
        }
        onPlanClicked: {
            if (!card.archivedCard)
                MainController.activateRecipe(card.recipe.id)
        }

        // Bean photo cache key: canonical Bean Base id when the recipe has
        // one, else the linked BAG's key ("bag-<id>") — a manual bag's photo
        // is cached under the bag key.
        imageKey: {
            if (recipe && recipe.beanBaseId && String(recipe.beanBaseId).length > 0)
                return String(recipe.beanBaseId)
            if (recipe && (recipe.bagId || 0) > 0)
                return "bag-" + recipe.bagId
            return ""
        }
        // Product-page link from the linked bag's blob (lets the cache
        // backfill a manual bag's photo, same as BagCard).
        imageLink: {
            if (!recipe || (recipe.bagId || 0) <= 0) return ""
            for (var i = 0; i < recipesPage._bags.length; ++i) {
                var b = recipesPage._bags[i]
                if (b.id === recipe.bagId && b.beanBaseData && String(b.beanBaseData).length > 0) {
                    try { return JSON.parse(b.beanBaseData).link || "" } catch (e) { return "" }
                }
            }
            return ""
        }

        // The plan line needs the profile's base temperature and target
        // weight (the recipe stores only its overrides). One synchronous
        // profile read per card — the list is small.
        function refreshProfileNumbers() {
            profileTempC = 0
            profileYieldG = 0
            var t = recipe && recipe.profileTitle ? String(recipe.profileTitle) : ""
            if (t === "")
                return
            var fn = ProfileManager.findProfileByTitle(t)
            if (fn && fn !== "") {
                var d = ProfileManager.getProfileByFilename(fn)
                profileTempC = d.espresso_temperature || 0
                profileYieldG = d.target_weight || 0
            }
        }
        onRecipeChanged: refreshProfileNumbers()
        Component.onCompleted: refreshProfileNumbers()

        opacity: archivedCard ? 0.7 : 1.0

        // Tap anywhere on the card to activate (buttons overlay and win).
        AccessibleMouseArea {
            anchors.fill: parent
            z: -1
            enabled: !card.archivedCard
            accessibleName: TranslationManager.translate("recipes.accessible.activate", "Activate recipe %1").arg(card.recipe.name || "")
            accessibleItem: card
            onAccessibleClicked: {
                MainController.activateRecipe(card.recipe.id)
            }
        }

            // Action row — compact, BagCard-style — rides the shared card's
            // footer slot so the border encloses it.
            footer: Flow {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)

                StyledIconButton {
                    visible: !card.archivedCard
                    width: Theme.scaled(36)
                    height: Theme.scaled(36)
                    icon.source: "qrc:/icons/edit.svg"
                    accessibleName: TranslationManager.translate("recipes.accessible.edit", "Edit recipe")
                    onClicked: pageStack.push(Qt.resolvedUrl("RecipeWizardPage.qml"),
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
                    onClicked: pageStack.push(Qt.resolvedUrl("RecipeWizardPage.qml"), { mode: "create" })
                }
            }

            // Empty state: two starter tiles teach both creation paths —
            // promote a good shot from history, or walk the wizard.
            Flow {
                visible: recipesPage.recipes.length === 0
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                component StarterTile: Rectangle {
                    id: tile
                    property string icon: ""
                    property string title: ""
                    property string subtitle: ""
                    signal tapped()
                    width: {
                        var avail = flickable.width
                        var columns = avail >= Theme.scaled(640) ? 2 : 1
                        return (avail - (columns - 1) * Theme.spacingMedium) / columns
                    }
                    implicitHeight: tileColumn.implicitHeight + 2 * Theme.spacingLarge
                    radius: Theme.cardRadius
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: 1
                    ColumnLayout {
                        id: tileColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.spacingLarge
                        anchors.rightMargin: Theme.spacingLarge
                        spacing: Theme.spacingSmall
                        ColoredIcon {
                            Layout.alignment: Qt.AlignHCenter
                            source: tile.icon
                            iconWidth: Theme.scaled(40)
                            iconHeight: Theme.scaled(40)
                            iconColor: Theme.primaryColor
                            Accessible.ignored: true
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: tile.title
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: tile.subtitle
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: tile.title + ", " + tile.subtitle
                        accessibleItem: tile
                        onAccessibleClicked: tile.tapped()
                    }
                }

                StarterTile {
                    icon: "qrc:/icons/history.svg"
                    title: TranslationManager.translate("recipes.empty.fromShot", "Start from a good shot")
                    subtitle: TranslationManager.translate("recipes.empty.fromShotHint",
                        "Pick a shot you liked in history and save it as a recipe")
                    onTapped: pageStack.push(Qt.resolvedUrl("ShotHistoryPage.qml"))
                }
                StarterTile {
                    icon: "qrc:/icons/plus.svg"
                    title: TranslationManager.translate("recipes.empty.fromScratch", "Build from scratch")
                    subtitle: TranslationManager.translate("recipes.empty.fromScratchHint",
                        "Walk through drink, beans, profile, and details")
                    onTapped: pageStack.push(Qt.resolvedUrl("RecipeWizardPage.qml"), { mode: "create" })
                }
            }

            // Card grid (BeanInfoPage pattern: fixed base width, computed columns)
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: recipesPage.recipes
                    delegate: RecipeCard {
                        recipe: modelData
                        width: recipesPage.cardWidth(flickable.width)
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
                        width: recipesPage.cardWidth(flickable.width)
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
