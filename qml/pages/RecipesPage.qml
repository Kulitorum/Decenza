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
        MainController.bagStorage.requestInventory()
        addRecipeButton.forceActiveFocus()
    }

    // Open-bag inventory, used by the cards to resolve each recipe's bean to
    // its bag — the bean photo of a non-canonical bag is cached under the
    // BAG's image key ("bag-<id>"), not a Bean Base id.
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
        // Bean photo from the same on-disk cache the bag cards use. Canonical
        // beans are keyed by their Bean Base id; a manual bag's photo lives
        // under the BAG's key ("bag-<id>"), so resolve the recipe's bean to
        // its open bag (same identity match as activation) for the fallback.
        readonly property var openBag: {
            if (!recipe) return null
            var bags = recipesPage._bags
            for (var i = 0; i < bags.length; ++i) {
                var b = bags[i]
                var match = recipe.beanBaseId && String(recipe.beanBaseId).length > 0
                    ? b.beanBaseId === recipe.beanBaseId
                    : (String(b.roasterName || "").toLowerCase() === String(recipe.roasterName || "").toLowerCase()
                       && String(b.coffeeName || "").toLowerCase() === String(recipe.coffeeName || "").toLowerCase()
                       && (String(recipe.roasterName || "") !== "" || String(recipe.coffeeName || "") !== ""))
                if (match) return b
            }
            return null
        }
        readonly property string imageKey: {
            if (recipe && recipe.beanBaseId && String(recipe.beanBaseId).length > 0)
                return String(recipe.beanBaseId)
            if (openBag)
                return openBag.beanBaseId && String(openBag.beanBaseId).length > 0
                    ? String(openBag.beanBaseId) : "bag-" + openBag.id
            return ""
        }
        property string cachedImagePath: ""
        function refreshBeanImage() {
            cachedImagePath = imageKey.length > 0
                ? MainController.beanbase.bagImagePath(imageKey) : ""
            if (imageKey.length > 0 && cachedImagePath.length === 0) {
                // The bag's product-page link (from its blob) lets the cache
                // backfill a manual bag's photo, same as BagCard.
                var link = ""
                if (openBag && openBag.beanBaseData && String(openBag.beanBaseData).length > 0) {
                    try { link = JSON.parse(openBag.beanBaseData).link || "" } catch (e) {}
                }
                MainController.beanbase.ensureBagImage(imageKey, recipe.coffeeName || "", link)
            }
        }
        onImageKeyChanged: refreshBeanImage()
        // The plan line needs the profile's base temperature and target
        // weight (the recipe stores only its overrides). One synchronous
        // profile read per card — the list is small.
        property real profileTempC: 0
        property real profileYieldG: 0
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
        Component.onCompleted: {
            refreshBeanImage()
            refreshProfileNumbers()
        }
        Connections {
            target: MainController.beanbase
            function onBagImageReady(key, path) {
                if (key === card.imageKey)
                    card.cachedImagePath = path
            }
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
                spacing: Theme.scaled(10)

                // Bean photo thumbnail (BagCard pattern): cached photo when the
                // recipe's bean is linked, dimmed beans icon otherwise so mixed
                // lists stay aligned.
                Rectangle {
                    Layout.preferredWidth: Theme.scaled(44)
                    Layout.preferredHeight: Theme.scaled(44)
                    Layout.alignment: Qt.AlignTop
                    radius: Theme.scaled(6)
                    color: Theme.backgroundColor
                    border.color: Theme.borderColor
                    border.width: 1

                    ColoredIcon {
                        anchors.centerIn: parent
                        visible: recipeThumb.status !== Image.Ready
                        source: "qrc:/icons/coffeebeans.svg"
                        iconWidth: Theme.scaled(22)
                        iconHeight: Theme.scaled(22)
                        iconColor: Theme.textSecondaryColor
                        opacity: 0.5
                        Accessible.ignored: true
                    }

                    Image {
                        id: recipeThumb
                        anchors.fill: parent
                        anchors.margins: 1
                        visible: status === Image.Ready
                        source: card.cachedImagePath.length > 0
                            ? "file:///" + card.cachedImagePath : ""
                        // Decode at thumbnail resolution — never the full photo.
                        sourceSize.width: Theme.scaled(88)
                        sourceSize.height: Theme.scaled(88)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
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

                    // The shot plan, phrased exactly like the idle Plan widget
                    // (fragment mode) but fed the RECIPE's values. Grind shows
                    // only when pinned — an inherited value belongs to the bag.
                    ShotPlanText {
                        Layout.fillWidth: true
                        sentence: false
                        maxLines: 2
                        itemOrder: ["doseYield", "temperature", "grind"]
                        profileName: card.recipe.profileTitle || ""
                        profileTemp: card.profileTempC
                        overrideTemp: card.recipe.tempOverrideC > 0 ? card.recipe.tempOverrideC : card.profileTempC
                        tempOverridden: card.recipe.tempOverrideC > 0
                        dose: card.recipe.doseG || 0
                        profileYield: card.profileYieldG
                        targetWeight: card.recipe.yieldG > 0 ? card.recipe.yieldG : card.profileYieldG
                        yieldOverridden: card.recipe.yieldG > 0 && card.profileYieldG > 0
                                         && Math.abs(card.recipe.yieldG - card.profileYieldG) > 0.1
                        grindSize: card.recipe.grindPinned || ""
                        roasterBrand: ""
                        coffeeName: ""
                        // Its internal MouseArea sits above the card's tap
                        // area — route the tap to the same activate action.
                        onClicked: {
                            if (!card.archivedCard && card.recipe.id !== Settings.dye.activeRecipeId)
                                MainController.activateRecipe(card.recipe.id)
                        }
                    }
                }
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
