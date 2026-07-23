import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"
import "../components/RecipeSearch.js" as RecipeSearch

// Recipes management (add-recipes): mirrors BeanInfoPage — cards in a Flow
// grid, tap a card to activate the recipe, compact action row per card.
// A recipe with shots archives (provenance must survive); one without is a
// mistaken creation and deletes outright — same lifecycle as bags.
Page {
    id: recipesPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("recipes.title", "Recipes")

    objectName: "recipesPage"
    background: ThemedPageBackground {}

    StackView.onActivated: {
        // Search is transient — clear it whenever the page becomes active.
        searchField.text = ""
        recipesPage.searchQuery = ""
        // Drop the profile-number cache so a profile edited elsewhere while this
        // page stayed instantiated is re-resolved on re-entry.
        recipesPage._profileNumbersCache = ({})
    }

    property var recipes: []
    property var archivedRecipes: []
    property bool showArchived: false

    // Search + sort (recipe-list-organization). Sort field/direction persist in
    // Settings.network; the search query is transient and resets on page entry.
    // Defaults reproduce the page's prior order (most-recently-used first).
    property string sortField: Settings.network.recipeSortField
    property string sortDirection: Settings.network.recipeSortDirection
    property string searchQuery: ""

    readonly property var sortFieldLabels: ({
        "dateUsed": TranslationManager.translate("recipes.sort.dateUsed", "Date used"),
        "dateCreated": TranslationManager.translate("recipes.sort.dateCreated", "Date created"),
        "coffee": TranslationManager.translate("recipes.sort.coffee", "Coffee"),
        "profile": TranslationManager.translate("recipes.sort.profile", "Profile"),
        "name": TranslationManager.translate("recipes.sort.name", "Name")
    })
    readonly property var sortFieldKeys: ["dateUsed", "dateCreated", "coffee", "profile", "name"]
    // Sensible default direction per key when the user first picks it: recency
    // newest-first, text A→Z.
    readonly property var defaultSortDirections: ({
        "dateUsed": "DESC", "dateCreated": "DESC", "coffee": "ASC", "profile": "ASC", "name": "ASC"
    })

    // Filtered + sorted views the card grids render (never mutate the source
    // arrays). Re-evaluates when the source list, query, field, or direction
    // change — the binding tracks every property read here.
    readonly property var visibleRecipes: filterAndSort(recipes, searchQuery, sortField, sortDirection)
    readonly property var visibleArchivedRecipes: filterAndSort(archivedRecipes, searchQuery, sortField, sortDirection)

    // Sort key for a recipe map under the chosen field. Numeric for date used,
    // lower-cased string otherwise so comparison is case-insensitive.
    function _sortKey(r, field) {
        if (field === "dateCreated")
            return Number(r.createdEpoch) || 0
        if (field === "coffee")
            return ((r.roasterName || "") + " " + (r.coffeeName || "")).trim().toLowerCase()
        if (field === "profile")
            return (r.profileTitle || "").toLowerCase()
        if (field === "name")
            return (r.name || "").toLowerCase()
        return Number(r.lastUsedEpoch) || 0   // dateUsed (default)
    }

    // A recipe with no value for the sort key (bean-less, never-used, etc.)
    // sorts to the end regardless of direction so blanks never float to the top.
    function _isBlankKey(k) {
        return (typeof k === "number") ? (k <= 0) : (String(k).length === 0)
    }

    function filterAndSort(list, query, field, dir) {
        // Tokenized AND match (RecipeSearch.js): `-` `/` `.` deleted, query split on
        // whitespace, every token must be a substring of the combined text. So a
        // cross-field query like "Yirg Df" matches a Yirgacheffe recipe on a
        // "D-Flow / Q" profile ("D-Flow" collapses to "dflow"). Empty query => all pass.
        // The haystack spans the same five fields as the web /recipes matcher
        // (shotserver_recipes.cpp): name, roaster, coffee, profile, and the drink-type
        // label — so "latte" or "tea" narrows the list too.
        var tokens = RecipeSearch.tokenize(query)
        var out = []
        for (var i = 0; i < list.length; ++i) {
            var r = list[i]
            if (tokens.length > 0) {
                var hay = (r.name || "") + " " + (r.roasterName || "") + " "
                          + (r.coffeeName || "") + " " + (r.profileTitle || "") + " "
                          + DrinkType.shortLabel(DrinkType.fromRecipeMap(r))
                if (!RecipeSearch.matches(hay, tokens))
                    continue
            }
            out.push(r)
        }
        var asc = (dir !== "DESC")
        out.sort(function(a, b) {
            var ka = _sortKey(a, field), kb = _sortKey(b, field)
            var ba = _isBlankKey(ka), bb = _isBlankKey(kb)
            if (ba !== bb) return ba ? 1 : -1   // blanks always last, both directions
            var cmp = (typeof ka === "number") ? (ka - kb) : ka.localeCompare(kb)
            // Deterministic tiebreak by id (Array.sort isn't guaranteed stable).
            if (cmp === 0) cmp = (Number(a.id) || 0) - (Number(b.id) || 0)
            return asc ? cmp : -cmp
        })
        return out
    }

    // Cache of profile display numbers resolved from the installed-profile
    // CATALOG, keyed by title. Filtering/sorting rebuilds the grid's delegates
    // on each change, and every card resolves these numbers; without the cache,
    // re-sorts/re-filters and cards sharing a title each re-run a synchronous
    // ProfileManager catalog read (the expensive part) — with it they become
    // O(1) map hits after the first resolution of that title. Only the catalog
    // path is cached: it is deterministic per title. The embedded-JSON fallback
    // (uninstalled/renamed title) depends on the recipe's OWN snapshot, so it is
    // computed per-call and never cached under the shared title key. Cleared on
    // inventory reload and on page re-entry (StackView.onActivated), so a
    // profile edited elsewhere is re-resolved. null = unresolvable profile.
    property var _profileNumbersCache: ({})

    // Extract {tempC, yieldG, stepTemps} from a parsed profile object, or null.
    function _numbersFromProfileObj(d) {
        if (!d)
            return null
        var tempC = Number(d.espresso_temperature) || 0
        var yieldG = Number(d.target_weight) || 0
        var temps = []
        var steps = d.steps || []
        for (var i = 0; i < steps.length; ++i) {
            var st = steps[i]
            var stTemp = st ? Number(st.temperature) : 0
            if (stTemp > 0)
                temps.push(stTemp)
        }
        // A resolved profile with no explicit per-step temps must still yield a
        // non-empty array — otherwise ShotPlanText's _tempStr falls through to
        // the loaded-profile branch, breaking "cards render their own profile"
        // and silently dropping the recipe's offset. Fall back to the base temp.
        return {
            tempC: tempC,
            yieldG: yieldG,
            stepTemps: temps.length > 0 ? temps : (tempC > 0 ? [tempC] : [])
        }
    }

    function profileNumbersFor(title, profileJson) {
        if (title === "")
            return null
        var hit = _profileNumbersCache[title]
        if (hit !== undefined)
            return hit
        var fn = ProfileManager.findProfileByTitle(title)
        if (fn && fn !== "") {
            // Catalog hit: deterministic per title → resolve once and cache.
            var result = _numbersFromProfileObj(ProfileManager.getProfileByFilename(fn))
            _profileNumbersCache[title] = result
            return result
        }
        // Title not installed: the numbers come from this recipe's own embedded
        // snapshot, which two recipes sharing a (renamed/uninstalled) title may
        // NOT share — so compute per-call, never cache under the title key.
        if (profileJson && String(profileJson).length > 0) {
            try { return _numbersFromProfileObj(JSON.parse(profileJson)) } catch (e) {
                console.warn("RecipesPage: unparsable embedded profile JSON for", title, ":", e)
            }
        }
        return null
    }

    // Grid column math (BeanInfoPage pattern: fixed base width, computed
    // columns) — one implementation for both card grids.
    function cardWidth(avail) {
        var columns = Math.max(1, Math.floor(avail / Theme.scaled(380)))
        return (avail - (columns - 1) * Theme.spacingMedium) / columns
    }

    Component.onCompleted: {
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
        // Reloaded data may reflect edited profiles — drop the cached numbers
        // so cards re-resolve once, then hit the cache on subsequent rebuilds.
        function onInventoryReady(list) { recipesPage._profileNumbersCache = ({}); recipesPage.recipes = list }
        function onArchivedReady(list) { recipesPage._profileNumbersCache = ({}); recipesPage.archivedRecipes = list }
        function onRecipesChanged() {
            MainController.recipeStorage.requestInventory()
            MainController.recipeStorage.requestArchived()
        }
        // The stale card's re-point is fire-and-forget from the picker — a
        // failure (bag deleted between snapshot and tap, DB error) must not
        // look like the tap was ignored.
        function onRecipeUpdated(recipeId, success) {
            if (recipeId === recipesPage._restorePendingId) {
                recipesPage._restorePendingId = -1
                if (!success) {
                    // Restoring can now be refused: an active recipe may have
                    // taken this name while it was archived. Say which fix works.
                    recipesPage.showToast(
                        recipesPage._restoreFailReason === "nameInUse"
                            ? trRestoreNameInUse.text : trRestoreFailed.text)
                }
                recipesPage._restoreFailReason = ""
                return
            }
            if (recipeId !== recipesPage._repointPendingId)
                return
            recipesPage._repointPendingId = -1
            if (!success)
                recipesPage.showToast(trRepointFailed.text)
        }
        function onRecipeUpdateFailed(recipeId, reason) {
            // Lands just before recipeUpdated(false) and names the cause.
            if (recipeId === recipesPage._restorePendingId)
                recipesPage._restoreFailReason = reason
        }
    }
    property int _repointPendingId: -1
    // Text currently shown in the toast (see showToast).
    property string _toastMessage: ""
    // Restore is fire-and-forget from the archived card; these track the one in
    // flight so a refusal can be explained instead of looking like a dead button.
    property int _restorePendingId: -1
    property string _restoreFailReason: ""

    function showToast(message) {
        recipesPage._toastMessage = message
        repointFailedToast.opacity = 1
        repointFailedToastTimer.restart()
        if (AccessibilityManager.enabled)
            AccessibilityManager.announce(message, true)
    }

    Tr {
        id: trRestoreFailed
        key: "recipes.restore.failed"
        fallback: "Couldn't restore that recipe"
        visible: false
    }
    Tr {
        id: trRestoreNameInUse
        key: "recipes.restore.nameInUse"
        fallback: "An active recipe already uses this name — rename that one first"
        visible: false
    }
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
            text: recipesPage._toastMessage
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
    // moves the bag link (the recipe's own grind is untouched by construction).
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
        // recipe-auto-load: this card is the current auto-load target.
        readonly property bool isAutoLoad: recipe && recipe.id !== undefined
            && recipe.id === Settings.dye.autoLoadRecipeId

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

        // The plan line needs the profile's base temperature, target weight,
        // and frame temperatures (the recipe stores only its deltas). These are
        // resolved once per profile title and memoized on the page
        // (profileNumbersFor) so the repeated delegate rebuilds from
        // filtering/sorting don't each re-read the profile catalog. The frame
        // temps make the card render ITS OWN profile, never the loaded one
        // (recipe-relative-temp-offset); the recipe's embedded profile JSON
        // is the fallback for a renamed/uninstalled profile, and a recipe
        // resolvable by neither shows no temperature segment at all.
        function refreshProfileNumbers() {
            var nums = recipesPage.profileNumbersFor(
                recipe && recipe.profileTitle ? String(recipe.profileTitle) : "",
                recipe ? recipe.profileJson : "")
            profileTempC = nums ? nums.tempC : 0
            profileYieldG = nums ? nums.yieldG : 0
            profileStepTemps = nums ? nums.stepTemps : []
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

                // recipe-auto-load: pin this recipe as the auto-load target.
                // Reuses pin.svg (ProfileSelectorPage's auto-load glyph) and
                // StyledIconButton's built-in active/inactive tint — no
                // confirmation, immediate toggle + toast, matching the
                // profile card's pattern. Setting this silently clears any
                // prior profile OR recipe auto-load (mutual exclusion is
                // wired in Settings; see settings.cpp).
                StyledIconButton {
                    visible: !card.archivedCard
                    width: Theme.scaled(36)
                    height: Theme.scaled(36)
                    icon.source: "qrc:/icons/pin.svg"
                    active: card.isAutoLoad
                    accessibleName: card.isAutoLoad
                        ? TranslationManager.translate("recipes.accessible.autoLoadDisable", "Disable auto-load")
                        : TranslationManager.translate("recipes.accessible.autoLoadSet", "Set auto-load")
                    onClicked: {
                        if (card.isAutoLoad) {
                            Settings.dye.autoLoadRecipeId = -1
                            recipesPage.showToast(TranslationManager.translate("recipes.toast.auto_load_disabled", "Auto-load disabled"))
                        } else {
                            var displaced = Settings.app.autoLoadProfileFilename !== ""
                                || Settings.dye.autoLoadRecipeId !== -1
                            Settings.dye.autoLoadRecipeId = card.recipe.id
                            recipesPage.showToast(displaced
                                ? TranslationManager.translate("recipes.toast.auto_load_set_displaced", "Auto-load set for %1 (replaced the previous auto-load)").arg(card.recipe.name || "")
                                : TranslationManager.translate("recipes.toast.auto_load_set", "Auto-load set for %1").arg(card.recipe.name || ""))
                        }
                    }
                }

                // recipe-auto-load: on-card status for the current auto-load
                // target only. Spells out "Auto-load" in words (a bare pin
                // icon doesn't communicate its meaning on its own) plus the
                // revert-minutes stepper shared with the profile side. No
                // recipe name (redundant with the card's own title) and no
                // clear button (redundant with the toggle button above,
                // which already clears on a second tap).
                Row {
                    visible: card.isAutoLoad
                    height: Theme.scaled(36)
                    spacing: Theme.scaled(4)

                    ColoredIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/pin.svg"
                        iconWidth: Theme.scaled(14)
                        iconHeight: Theme.scaled(14)
                        iconColor: Theme.primaryColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: TranslationManager.translate("recipes.autoLoad.label", "Auto-load")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }

                    ValueInput {
                        id: cardAutoLoadRevertInput
                        anchors.verticalCenter: parent.verticalCenter
                        height: Math.round(Theme.captionFont.pixelSize * 1.9)
                        valueFontPixelSize: Theme.captionFont.pixelSize
                        value: Settings.app.autoLoadRevertMinutes
                        from: 0
                        to: 60
                        stepSize: 1
                        suffix: TranslationManager.translate("recipes.autoLoad.minutesShort", "min")
                        displayText: value === 0
                            ? TranslationManager.translate("recipes.autoLoad.off", "off")
                            : value + " " + TranslationManager.translate("recipes.autoLoad.minutesShort", "min")
                        accessibleName: TranslationManager.translate("recipes.accessible.autoLoadRevertAfter", "Auto-load revert after, minutes")
                        // main.qml's Connections on Settings.app.autoLoadRevertMinutesChanged
                        // resets the idle countdown reactively — no extra call needed here.
                        onValueModified: function(newValue) {
                            Settings.app.autoLoadRevertMinutes = newValue
                        }
                    }
                }

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
                    onClicked: {
                        recipesPage._restorePendingId = card.recipe.id
                        recipesPage._restoreFailReason = ""
                        MainController.recipeStorage.requestUnarchiveRecipe(card.recipe.id)
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
                    onClicked: pageStack.push(Qt.resolvedUrl("RecipeWizardPage.qml"), { mode: "create" })
                }
            }

            // Search + sort bar (recipe-list-organization): mirrors the
            // ShotHistoryPage pattern. Shown once there is anything to organize;
            // over the empty-library starter tiles it would be pointless clutter.
            RowLayout {
                Layout.fillWidth: true
                visible: recipesPage.recipes.length > 0 || recipesPage.archivedRecipes.length > 0
                spacing: Theme.spacingSmall

                StyledTextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholder: TranslationManager.translate("recipes.searchPlaceholder", "Search recipes...")
                    rightPadding: searchClearButton.visible ? Theme.scaled(36) : Theme.scaled(12)
                    // displayText includes the IME preedit so the filter updates
                    // per keystroke; a short debounce keeps re-sorts cheap.
                    inputMethodHints: Qt.ImhNoPredictiveText
                    accessibleName: TranslationManager.translate("recipes.accessible.search", "Search recipes")
                    onDisplayTextChanged: searchTimer.restart()

                    // Inline clear button (hidden in accessibility mode to avoid
                    // overlapping elements — the standalone button below serves it).
                    Item {
                        id: searchClearButton
                        width: Theme.scaled(20)
                        height: Theme.scaled(20)
                        visible: searchField.displayText.length > 0
                                 && !(typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.scaled(10)
                        anchors.verticalCenter: parent.verticalCenter
                        ColoredIcon {
                            anchors.centerIn: parent
                            source: "qrc:/icons/cross.svg"
                            iconWidth: Theme.scaled(14)
                            iconHeight: Theme.scaled(14)
                            iconColor: Theme.textSecondaryColor
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.scaled(6)
                            onClicked: {
                                searchField.text = ""
                                recipesPage.searchQuery = ""
                                searchField.focus = false
                            }
                        }
                    }
                }

                // Accessible clear button (outside the field for TalkBack discovery).
                AccessibleButton {
                    visible: searchField.displayText.length > 0
                             && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
                    accessibleName: TranslationManager.translate("recipes.accessible.clearSearch", "Clear search")
                    icon.source: "qrc:/icons/cross.svg"
                    onClicked: {
                        searchField.text = ""
                        recipesPage.searchQuery = ""
                        searchField.focus = false
                    }
                }

                // Sort field button
                AccessibleButton {
                    text: recipesPage.sortFieldLabels[recipesPage.sortField] || recipesPage.sortFieldLabels["dateUsed"]
                    accessibleName: TranslationManager.translate("recipes.accessible.sortBy", "Sort by %1")
                        .arg(recipesPage.sortFieldLabels[recipesPage.sortField] || "")
                    onClicked: sortPickerDialog.open()
                }

                // Sort direction toggle
                AccessibleButton {
                    icon.source: recipesPage.sortDirection === "DESC"
                        ? "qrc:/icons/SortDescending.svg" : "qrc:/icons/SortAscending.svg"
                    tintIcon: true
                    accessibleName: recipesPage.sortDirection === "DESC"
                        ? TranslationManager.translate("recipes.accessible.sortDescending", "Sort descending, tap to sort ascending")
                        : TranslationManager.translate("recipes.accessible.sortAscending", "Sort ascending, tap to sort descending")
                    onClicked: {
                        recipesPage.sortDirection = (recipesPage.sortDirection === "DESC") ? "ASC" : "DESC"
                        Settings.network.recipeSortDirection = recipesPage.sortDirection
                    }
                }

                Timer {
                    id: searchTimer
                    interval: 250
                    onTriggered: recipesPage.searchQuery = searchField.displayText.trim()
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
                    color: Theme.cardBackgroundColor
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
                    model: recipesPage.visibleRecipes
                    delegate: RecipeCard {
                        recipe: modelData
                        width: recipesPage.cardWidth(flickable.width)
                    }
                }
            }

            // No-matches state: a search that matches nothing anywhere —
            // neither active nor archived (so a match hiding in the collapsed
            // archived section never triggers a false "nothing matches"). The
            // archived toggle below still shows its matching count so the user
            // can reach any archived-only matches. Distinct from the "no
            // recipes yet" starter tiles (a genuinely empty library).
            Label {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingMedium
                visible: recipesPage.searchQuery.length > 0
                         && (recipesPage.recipes.length + recipesPage.archivedRecipes.length) > 0
                         && recipesPage.visibleRecipes.length === 0
                         && recipesPage.visibleArchivedRecipes.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: TranslationManager.translate("recipes.noMatches", "No recipes match your search")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            // --- Archived section ---
            // Count and visibility track the FILTERED archived set: with a
            // search active the toggle only appears when archived recipes match
            // and its count reflects the matches (with no search,
            // visibleArchivedRecipes == archivedRecipes, so behaviour is
            // unchanged).
            AccessibleButton {
                visible: recipesPage.visibleArchivedRecipes.length > 0
                Layout.preferredHeight: Theme.scaled(36)   // Layout child: raw height is ignored
                _customFontSize: Theme.captionFont.pixelSize
                leftPadding: Theme.scaled(10)
                rightPadding: Theme.scaled(10)
                text: (recipesPage.showArchived
                       ? TranslationManager.translate("recipes.archived.hide", "Hide archived")
                       : TranslationManager.translate("recipes.archived.show", "Show archived"))
                      + " (" + recipesPage.visibleArchivedRecipes.length + ")"
                accessibleName: text
                onClicked: recipesPage.showArchived = !recipesPage.showArchived
            }

            Flow {
                visible: recipesPage.showArchived
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Repeater {
                    model: recipesPage.showArchived ? recipesPage.visibleArchivedRecipes : []
                    delegate: RecipeCard {
                        recipe: modelData
                        archivedCard: true
                        width: recipesPage.cardWidth(flickable.width)
                    }
                }
            }
        }
    }

    // Sort picker dialog (recipe-list-organization)
    SelectionDialog {
        id: sortPickerDialog
        title: TranslationManager.translate("recipes.sortByTitle", "Sort By")
        options: recipesPage.sortFieldKeys.map(function(key) { return recipesPage.sortFieldLabels[key] || key })
        currentIndex: recipesPage.sortFieldKeys.indexOf(recipesPage.sortField)
        onSelected: function(index, value) {
            recipesPage.sortField = recipesPage.sortFieldKeys[index]
            recipesPage.sortDirection = recipesPage.defaultSortDirections[recipesPage.sortField] || "DESC"
            Settings.network.recipeSortField = recipesPage.sortField
            Settings.network.recipeSortDirection = recipesPage.sortDirection
        }
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: root.goBack()
    }
}
