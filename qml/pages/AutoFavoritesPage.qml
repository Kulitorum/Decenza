// The favourites-list delegate reads this file's ids (`autoFavoritesPage`,
// `favoritesListView`); Bound makes them statically resolvable. It declares its one
// injected role, `model`, required in the same edit -- without that, Bound stops role
// injection and every favourite card renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

T.Page {
    id: autoFavoritesPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("autofavorites.title", "Auto-Favorites")

    objectName: "autoFavoritesPage"
    background: ThemedPageBackground {}

    property bool _waitingForShotLoad: false

    // Wait for async loadShotWithMetadata to complete before popping
    Connections {
        target: MainController
        enabled: autoFavoritesPage._waitingForShotLoad
        function onShotMetadataLoaded(shotId, success) {
            autoFavoritesPage._waitingForShotLoad = false
            if (success)
                AppShell.backRequested()
        }
    }

    Component.onCompleted: {
        loadFavorites()
    }

    StackView.onActivated: {
        loadFavorites()
    }

    function loadFavorites() {
        favoritesModel.clear()
        MainController.shotHistory.requestAutoFavorites(
            Settings.network.autoFavoritesGroupBy,
            Settings.network.autoFavoritesMaxItems
        )
    }

    Connections {
        target: MainController.shotHistory
        function onAutoFavoritesReady(results) {
            favoritesModel.clear()
            for (var i = 0; i < results.length; i++) {
                if (Settings.network.autoFavoritesHideUnrated && results[i].avgEnjoyment <= 0)
                    continue
                favoritesModel.append(results[i])
            }
        }
    }

    // Which fields the current grouping actually keys on. Taking the mode as an
    // argument rather than reading Settings inside: a binding records no
    // dependency on a property a called function reads, so `groupByIncludes`
    // below would never re-evaluate when the user changes the mode.
    function includesFor(groupBy) {
        var hasGrindSetting = (groupBy === "bean_profile_grinder" || groupBy === "bean_profile_grinder_weight")
        var hasEquipment = (groupBy === "bean_profile" || hasGrindSetting)
        return {
            bean: (groupBy === "bean" || hasEquipment),
            profile: (groupBy === "profile" || hasEquipment),
            // The equipment PACKAGE and the grind SETTING are separate keys. They
            // were one flag, so the default mode — which groups by package across
            // every grind setting — told "Show" to filter on the latest setting
            // too, returning a strict subset of what the card aggregates.
            equipment: hasEquipment,
            grindSetting: hasGrindSetting
        }
    }

    readonly property var groupByIncludes: autoFavoritesPage.includesFor(Settings.network.autoFavoritesGroupBy)


    // Target yield for the card chip. The SQL returns the latest shot's saved
    // target weight (from the yield_override DB column). Weight mode uses the
    // group's exact bucket value, which is the same number by grouping. Legacy
    // shots with no saved target read 0 here and fall back to finalWeight.
    //
    // Note: this is an approximation of the value applyLoadedShotMetadata will
    // apply when Load is pressed. The loader falls back to finalWeight only
    // when the current profile's targetWeight is 0, while this helper falls
    // back unconditionally when targetWeight == 0. The mismatch is typically
    // sub-gram and only affects stale legacy rows.
    function recipeYield(targetWeight, finalWeight) {
        return targetWeight > 0 ? targetWeight : (finalWeight || 0)
    }

    // Build accessible text based on current groupBy setting
    function buildGroupByText(beanBrand, beanType, profileName, equipmentName, grinderBrand, grinderModel, grinderSetting, doseWeight, targetWeight, finalWeight, shotCount, avgEnjoyment) {
        var includes = autoFavoritesPage.groupByIncludes
        var parts = []

        var includeBean = includes.bean
        var includeProfile = includes.profile

        if (includeBean) {
            var bean = (beanBrand || "") + (beanType ? " - " + beanType : "")
            if (bean) parts.push(bean)
        }
        if (includeProfile && profileName)
            parts.push(profileName)
        if (includes.equipment) {
            // The package's own name where it has one, the grinder as the
            // fallback — the same text the card shows. The grind setting is
            // appended only in the modes that key on it; the default mode spans
            // every setting, so naming one would describe a group that does not
            // exist.
            var pkg = equipmentName
                || ((grinderBrand || "") + " " + (grinderModel || "")).trim()
            if (includes.grindSetting && grinderSetting)
                pkg = (pkg + " @ " + grinderSetting).trim()
            if (pkg) parts.push(pkg)
        }

        // Always include recipe summary
        parts.push((doseWeight || 0).toFixed(1) + "g to " + recipeYield(targetWeight, finalWeight).toFixed(1) + "g")
        parts.push(shotCount + " " + TranslationManager.translate("autofavorites.shots", "shots"))
        if (avgEnjoyment > 0)
            parts.push(avgEnjoyment + "% enjoyment")

        return parts.join(". ")
    }

    // Refresh when a new shot is saved
    Connections {
        target: MainController.shotHistory
        function onShotSaved(shotId) {
            if (autoFavoritesPage.visible) {
                autoFavoritesPage.loadFavorites()
            }
        }
    }

    ListModel {
        id: favoritesModel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Header with count and settings access
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: favoritesModel.count + " " +
                      TranslationManager.translate("autofavorites.combinations", "combinations")
                font.family: Theme.labelFont.family
                font.pixelSize: Theme.labelFont.pixelSize
                color: Theme.textSecondaryColor
            }

            Item { Layout.fillWidth: true }

            // Settings button (gear icon)
            Rectangle {
                id: settingsButton
                Layout.preferredWidth: Theme.scaled(36)
                Layout.preferredHeight: Theme.scaled(36)
                radius: Theme.scaled(18)
                color: Theme.cardBackgroundColor
                Accessible.ignored: true

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/settings.svg"
                    sourceSize.width: Theme.scaled(20)
                    sourceSize.height: Theme.scaled(20)
                    Accessible.ignored: true

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
                    accessibleItem: settingsButton
                    onAccessibleClicked: settingsPopup.open()
                }
            }
        }

        // Favorites list
        ListView {
            id: favoritesListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: favoritesModel

            delegate: Rectangle {
                id: favoriteDelegate
                required property var model

                width: favoritesListView.width
                height: contentColumn.implicitHeight + Theme.spacingMedium * 2
                radius: Theme.cardRadius
                color: Theme.cardBackgroundColor
                Accessible.ignored: true

                property string _beanText: {
                    var parts = []
                    if (favoriteDelegate.model.beanBrand) parts.push(favoriteDelegate.model.beanBrand)
                    if (favoriteDelegate.model.beanType) parts.push(favoriteDelegate.model.beanType)
                    return parts.join(" - ")
                }
                property bool _hasBean: !!(favoriteDelegate.model.beanBrand || favoriteDelegate.model.beanType)
                property bool _hasProfile: !!(favoriteDelegate.model.profileName && favoriteDelegate.model.profileName.length > 0)
                // Recipe identity of the group's LATEST shot (history-recipe-identity),
                // resolved by the same join the history list uses. A favorite is a
                // bean+profile group, so this is "what this drink is currently made
                // with", not a property of every shot in the group.
                property bool _hasRecipe: (favoriteDelegate.model.recipeId || 0) > 0
                    && !!favoriteDelegate.model.recipeName
                property bool _recipeArchived: _hasRecipe && (favoriteDelegate.model.recipeArchived === true)
                // The package NAME, the way a history row prefers the recipe name
                // over the profile: "Graph" is the thing the user set up, and
                // brand+model is the fallback for a package they never named.
                property string _equipmentText: favoriteDelegate.model.equipmentName
                    || ((favoriteDelegate.model.grinderBrand || "") + " " + (favoriteDelegate.model.grinderModel || "")).trim()
                // A recipe already names its equipment, so printing the package
                // beside it says the same thing twice.
                property bool _hasEquipment: !favoriteDelegate._hasRecipe
                    && autoFavoritesPage.groupByIncludes.equipment
                    && favoriteDelegate._equipmentText !== ""
                // Recipe first, and carrying the archived state as TEXT — the card
                // shows that state only by dimming, so without this it is
                // unreachable by screen reader.
                property string _recipeSpoken: !_hasRecipe ? ""
                    : (_recipeArchived
                       ? TranslationManager.translate("autofavorites.accessible.recipeArchived",
                                                      "%1 (archived recipe)").arg(favoriteDelegate.model.recipeName)
                       : favoriteDelegate.model.recipeName) + ". "
                property string _groupByText: _recipeSpoken + autoFavoritesPage.buildGroupByText(
                    favoriteDelegate.model.beanBrand, favoriteDelegate.model.beanType, favoriteDelegate.model.profileName,
                    favoriteDelegate.model.equipmentName,
                    favoriteDelegate.model.grinderBrand, favoriteDelegate.model.grinderModel, favoriteDelegate.model.grinderSetting,
                    favoriteDelegate.model.doseWeightG, favoriteDelegate.model.targetWeightG, favoriteDelegate.model.finalWeightG,
                    favoriteDelegate.model.shotCount, favoriteDelegate.model.avgEnjoyment)

                // Whole card announces full details based on groupBy setting
                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: favoriteDelegate._groupByText
                    accessibleItem: favoriteDelegate
                    onAccessibleClicked: {} // Informational only
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Main info
                    ColumnLayout {
                        id: contentColumn
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        // Same three-line grammar as a Shot History row: an
                        // identity line, a secondary line of the identity fields
                        // that did not win it, then the dial-in numbers. The
                        // identity is the recipe when there is one and the profile
                        // otherwise — history makes the same substitution.
                        //
                        // The identity keeps its OWN line rather than joining the
                        // Flow below with a separator: inside a Flow the separator
                        // is a sibling item, so when the bean wraps to the next
                        // line the separator stays behind as a dangling middot.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            ThemedIcon {
                                visible: favoriteDelegate._hasRecipe
                                source: DrinkType.icon(favoriteDelegate.model.recipeDrinkType || "")
                                iconSize: Theme.subtitleFont.pixelSize
                                color: favoriteDelegate._recipeArchived ? Theme.textSecondaryColor
                                                                        : Theme.primaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: favoriteDelegate._hasRecipe ? (favoriteDelegate.model.recipeName || "")
                                                                  : (favoriteDelegate.model.profileName || "")
                                font: Theme.subtitleFont
                                color: favoriteDelegate._recipeArchived ? Theme.textSecondaryColor
                                                                        : Theme.primaryColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                visible: text !== ""
                                Accessible.ignored: true
                            }
                        }

                        // Secondary line: whatever the identity line did not carry.
                        Text {
                            text: {
                                var parts = []
                                if (favoriteDelegate._hasRecipe && favoriteDelegate._hasProfile)
                                    parts.push(favoriteDelegate.model.profileName)
                                if (favoriteDelegate._hasBean) parts.push(favoriteDelegate._beanText)
                                if (favoriteDelegate._hasEquipment) parts.push(favoriteDelegate._equipmentText)
                                return parts.join("  \u00b7  ")
                            }
                            font: Theme.labelFont
                            color: Theme.textSecondaryColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            visible: text !== ""
                            Accessible.ignored: true
                        }

                        // Recipe summary
                        RowLayout {
                            spacing: Theme.spacingLarge

                            Text {
                                text: (favoriteDelegate.model.doseWeightG || 0).toFixed(1) + "g \u2192 " +
                                      autoFavoritesPage.recipeYield(favoriteDelegate.model.targetWeightG, favoriteDelegate.model.finalWeightG).toFixed(1) + "g"
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: favoriteDelegate.model.shotCount + " " +
                                      TranslationManager.translate("autofavorites.shots", "shots")
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: favoriteDelegate.model.avgEnjoyment > 0 ? favoriteDelegate.model.avgEnjoyment + "%" : ""
                                font: Theme.labelFont
                                color: Theme.warningColor
                                visible: favoriteDelegate.model.avgEnjoyment > 0
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Info button
                    Rectangle {
                        id: infoButton
                        Layout.preferredWidth: Theme.scaled(70)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.info", "Info")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.favoriteInfo", "Favorite info") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: infoButton
                            onAccessibleClicked: {
                                // In weight mode, model.doseBucket is the group's rounded dose
                                // (used to scope stats) while model.doseWeightG is the latest
                                // shot's raw dose (shown on the card). Pass the bucket so the
                                // Info page's averages cover the same shots the card aggregates.
                                AppShell.autoFavoriteInfoRequested({
                                    shotId: favoriteDelegate.model.shotId,
                                    groupBy: Settings.network.autoFavoritesGroupBy,
                                    beanBrand: favoriteDelegate.model.beanBrand || "",
                                    beanType: favoriteDelegate.model.beanType || "",
                                    profileName: favoriteDelegate.model.profileName || "",
                                    grinderBrand: favoriteDelegate.model.grinderBrand || "",
                                    grinderModel: favoriteDelegate.model.grinderModel || "",
                                    equipmentName: favoriteDelegate.model.equipmentName || "",
                                    equipmentId: favoriteDelegate.model.equipmentId || 0,
                                    grinderSetting: favoriteDelegate.model.grinderSetting || "",
                                    doseBucket: favoriteDelegate.model.doseBucket || 0,
                                    targetWeight: favoriteDelegate.model.targetWeightG || 0,
                                    avgEnjoyment: favoriteDelegate.model.avgEnjoyment || 0,
                                    shotCount: favoriteDelegate.model.shotCount || 0
                                })
                            }
                        }
                    }

                    // Show button — opens Shot History filtered to this group
                    Rectangle {
                        id: showButton
                        Layout.preferredWidth: Theme.scaled(70)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.show", "Show")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.showShots", "Show shots") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: showButton
                            onAccessibleClicked: {
                                var includes = autoFavoritesPage.groupByIncludes
                                var filter = {}

                                if (includes.bean) {
                                    if (favoriteDelegate.model.beanBrand) filter.beanBrand = favoriteDelegate.model.beanBrand
                                    if (favoriteDelegate.model.beanType) filter.beanType = favoriteDelegate.model.beanType
                                }
                                if (includes.profile && favoriteDelegate.model.profileName)
                                    filter.profileName = favoriteDelegate.model.profileName
                                if (includes.equipment) {
                                    // The package id, not the grinder's brand and model:
                                    // the card is grouped on the package, and two baskets
                                    // on one grinder are two cards. Filtering by brand and
                                    // model is the approximation the grouping query itself
                                    // stopped using. `undefined` means the row predates the
                                    // id, and is left unfiltered rather than sent as 0 —
                                    // bucket 0 is a real, different group.
                                    if (favoriteDelegate.model.equipmentId !== undefined)
                                        filter.equipmentId = favoriteDelegate.model.equipmentId
                                }
                                if (includes.grindSetting && favoriteDelegate.model.grinderSetting)
                                    filter.grinderSetting = favoriteDelegate.model.grinderSetting
                                // In weight mode the card also represents a specific 0.5 g dose
                                // bucket and an exact target yield. Mirror the bucket range and
                                // yield on the ShotHistory filter so "Show" scopes to the same
                                // shots the card aggregates, even though the card itself displays
                                // the latest shot's raw dose.
                                if (Settings.network.autoFavoritesGroupBy === "bean_profile_grinder_weight") {
                                    var bucket = favoriteDelegate.model.doseBucket || 0
                                    if (bucket > 0) {
                                        filter.minDose = bucket - 0.25
                                        filter.maxDose = bucket + 0.25
                                    }
                                    // Match on the saved target weight (yield_override DB
                                    // column) rather than final_weight, since the card groups
                                    // by exact target yield. minYield/maxYield would filter
                                    // actual pour weight, which almost never equals the target
                                    // to float precision.
                                    var t = favoriteDelegate.model.targetWeightG || 0
                                    if (t > 0)
                                        filter.targetWeight = t
                                }

                                var props = {}
                                if (Object.keys(filter).length > 0)
                                    props.initialFilter = filter
                                AppShell.shotHistoryRequested(props)
                            }
                        }
                    }

                    // Load button
                    Rectangle {
                        id: loadButton
                        Layout.preferredWidth: Theme.scaled(70)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.load", "Load")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.load", "Load") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: loadButton
                            onAccessibleClicked: {
                                if (Settings.network.autoFavoritesOpenBrewSettings)
                                    AppShell.pendingBrewDialog = true
                                autoFavoritesPage._waitingForShotLoad = true
                                // Pass the latest shot's raw dose so the loaded recipe matches
                                // what the card displays (and what the user last dialled in).
                                MainController.loadShotWithMetadata(favoriteDelegate.model.shotId, favoriteDelegate.model.doseWeightG || 0)
                            }
                        }
                    }

                    // Create-recipe button: an auto-favorite is literally "a
                    // shot you keep reloading" — a recipe announcing itself.
                    // Opens the composer prefilled from the group's shot.
                    // Hidden when the group's shot already came FROM a recipe —
                    // offering to create one from it then reads as broken. Shot
                    // Detail has gated this since shot-pages-card-cleanup; this
                    // surface and Shot History never got the same rule
                    // (history-recipe-identity).
                    Rectangle {
                        id: favRecipeButton
                        // Name-aware, matching the Shot History row: a DANGLING
                        // recipe_id (row gone after a transfer or partial restore)
                        // means there is no recipe to edit, so offering to create
                        // one is right. Gating on the id alone would hide the
                        // button and leave the user no route at all.
                        visible: !favoriteDelegate._hasRecipe
                        Layout.preferredWidth: Theme.scaled(70)
                        Layout.preferredHeight: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.recipe", "Recipe")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: Theme.primaryContrastColor
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.accessible.recipe", "Create recipe from this favorite") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: favRecipeButton
                            onAccessibleClicked: {
                                AppShell.recipeWizardRequested("create", { promoteShotId: favoriteDelegate.model.shotId })
                            }
                        }
                    }
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate("autofavorites.empty",
                      "No shots yet. Make some espresso to build your favorites!")
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                color: Theme.textSecondaryColor
                visible: favoritesModel.count === 0
                wrapMode: Text.Wrap
                width: parent.width - Theme.scaled(40)
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Settings dialog
    DecenzaDialog {
        id: settingsPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(320))
        modal: true
        padding: Theme.scaled(20)
        title: TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
        header: Item {} // Hide default Dialog header, we use our own

        onOpened: {
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
                AccessibilityManager.announce(TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings"))
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                color: Theme.textColor
                Accessible.ignored: true
            }

            // Group by setting
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Text {
                    text: TranslationManager.translate("autofavorites.groupby", "Group by")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }

                StyledComboBox {
                    id: groupByCombo
                    Layout.fillWidth: true
                    accessibleLabel: TranslationManager.translate("autofavorites.groupby", "Group by")
                    model: [
                        TranslationManager.translate("autofavorites.groupby.bean", "Bean only"),
                        TranslationManager.translate("autofavorites.groupby.profile", "Profile only"),
                        // NEW KEYS, deliberately. The English changed MEANING here:
                        // `beanprofile` was "Bean + Profile" and now also splits by
                        // equipment package, and `all`/`allweight` said "Grinder"
                        // where the mode keys on the grind SETTING. A reworded
                        // fallback keeps its existing translation
                        // (TranslationManager::noteSourceString), so reusing the
                        // keys would leave every non-English user reading labels
                        // for modes that no longer exist.
                        TranslationManager.translate("autofavorites.groupby.beanprofileequipment", "Bean + Profile + Equipment"),
                        TranslationManager.translate("autofavorites.groupby.equipmentgrind", "Bean + Profile + Grind setting"),
                        TranslationManager.translate("autofavorites.groupby.equipmentgrindweight", "Bean + Profile + Grind setting + Weight")
                    ]
                    currentIndex: {
                        switch(Settings.network.autoFavoritesGroupBy) {
                            case "bean": return 0
                            case "profile": return 1
                            case "bean_profile_grinder": return 3
                            case "bean_profile_grinder_weight": return 4
                            default: return 2  // bean_profile
                        }
                    }
                    onActivated: {
                        var values = ["bean", "profile", "bean_profile", "bean_profile_grinder", "bean_profile_grinder_weight"]
                        Settings.network.autoFavoritesGroupBy = values[currentIndex]
                        autoFavoritesPage.loadFavorites()
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
                            AccessibilityManager.announce(
                                TranslationManager.translate("autofavorites.groupby", "Group by") +
                                " " + displayText)
                        }
                    }
                }
            }

            // Max items setting
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("autofavorites.maxitems", "Max items")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }

                ValueInput {
                    value: Settings.network.autoFavoritesMaxItems
                    from: 5
                    to: 50
                    stepSize: 5
                    accessibleName: TranslationManager.translate("autofavorites.maxitems", "Max items") +
                        ", " + value
                    onValueModified: function(newValue) {
                        Settings.network.autoFavoritesMaxItems = newValue
                        autoFavoritesPage.loadFavorites()
                    }
                }
            }

            // Hide unrated favorites
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("autofavorites.hideUnrated", "Hide unrated favorites")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                StyledSwitch {
                    checked: Settings.network.autoFavoritesHideUnrated
                    accessibleName: TranslationManager.translate("autofavorites.hideUnrated", "Hide unrated favorites")
                    onToggled: {
                        Settings.network.autoFavoritesHideUnrated = checked
                        autoFavoritesPage.loadFavorites()
                    }
                }
            }

            // Open brew settings after load
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("autofavorites.openBrewSettings", "Open brew settings after load")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                StyledSwitch {
                    checked: Settings.network.autoFavoritesOpenBrewSettings
                    accessibleName: TranslationManager.translate("autofavorites.openBrewSettings", "Open brew settings after load")
                    onToggled: Settings.network.autoFavoritesOpenBrewSettings = checked
                }
            }

            // Close button
            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("common.close", "Close")
                accessibleName: TranslationManager.translate("common.close", "Close") + " " +
                    TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
                onClicked: settingsPopup.close()
            }
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("autofavorites.title", "Auto-Favorites")
        onBackClicked: AppShell.backRequested()
    }
}
