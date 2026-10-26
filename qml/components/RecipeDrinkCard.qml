pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza
import "RecipeCardText.js" as RecipeCardText

// Native adapter for the service-free RecipeDrinkCardView. This retains the
// existing native profile, translation, image-cache, and shot-plan behavior;
// the browser host feeds the same view from its HTTP recipe rows.
Item {
    id: card

    property var recipe: ({})
    property bool active: false
    property bool stale: false
    property bool showStaleAction: false
    property string imageKey: ""
    property string imageLink: ""
    property real profileTempC: 0
    property real profileYieldG: 0
    property var profileStepTemps: []

    property alias footer: cardView.footer

    readonly property var steam: {
        if (!recipe || !recipe.steamJson || String(recipe.steamJson).length === 0) return ({})
        try { return JSON.parse(recipe.steamJson) } catch (e) { return ({}) }
    }
    readonly property var hotWater: {
        if (!recipe || !recipe.hotWaterJson || String(recipe.hotWaterJson).length === 0) return ({})
        try { return JSON.parse(recipe.hotWaterJson) } catch (e) { return ({}) }
    }
    readonly property string drinkType: DrinkType.fromRecipeMap(recipe)
    readonly property bool isHotWaterOnly:
        (!recipe.profileTitle || String(recipe.profileTitle).trim() === "") && !!hotWater.hasWater

    readonly property bool profileMissing: {
        var title = (recipe && recipe.profileTitle ? String(recipe.profileTitle) : "")
        if (title.trim() === "")
            return false
        if (recipe.profileJson && String(recipe.profileJson).length > 0)
            return false
        return ProfileService.installedProfileTitles.indexOf(title) < 0
    }

    signal staleActionClicked()
    signal planClicked()

    Tr { id: trActiveBadge; key: "recipes.list.active"; fallback: "Active"; visible: false }
    Tr {
        id: trProfileMissing
        key: "recipes.list.profileMissing"
        fallback: "Profile \"%1\" is missing — edit this recipe to pick another"
        visible: false
    }
    Tr { id: trShotsWord; key: "recipes.list.shots"; fallback: "shots"; visible: false }
    Tr { id: trMilkWeight; key: "recipes.list.milkWeight"; fallback: "%1g milk"; visible: false }
    Tr { id: trHotWaterWord; key: "recipes.list.hotWater"; fallback: "Hot water"; visible: false }
    Tr {
        id: trBagFinishedAction
        key: "recipes.list.bagFinished"
        fallback: "Bag finished — tap to choose beans"
        visible: false
    }

    function drinkLine() {
        return RecipeCardText.drinkLine(recipe, drinkType, steam, hotWater, isHotWaterOnly,
            function(type) { return DrinkType.shortLabel(type) }, trHotWaterWord.text,
            function(weight) { return trMilkWeight.text.arg(weight) })
    }

    function beanLine() {
        return RecipeCardText.beanLine(recipe, trShotsWord.text)
    }

    function vesselLine() {
        return RecipeCardText.vesselLine(hotWater)
    }

    implicitWidth: cardView.implicitWidth
    implicitHeight: cardView.implicitHeight

    RecipeDrinkCardStyle {
        id: nativeCardStyle
        scale: Theme.scale
        cardPadding: Theme.spacingMedium
        smallSpacing: Theme.spacingSmall
        mediumSpacing: Theme.spacingMedium
        cardRadius: Theme.cardRadius
        cardBackgroundColor: Theme.cardBackgroundColor
        borderColor: Theme.borderColor
        accentColor: Theme.accentColor
        warningColor: Theme.warningColor
        textColor: Theme.textColor
        secondaryTextColor: Theme.textSecondaryColor
        titleFont: Theme.subtitleFont
        captionFont: Theme.captionFont
    }

    Component {
        id: nativeThumbnail
        BeanThumbnail {
            imageKey: card.imageKey
            fallbackName: card.recipe.coffeeName || ""
            link: card.imageLink
        }
    }

    Component {
        id: nativeDrinkIcon
        ColoredIcon {
            source: DrinkType.icon(card.drinkType)
            iconWidth: Theme.scaled(16)
            iconHeight: Theme.scaled(16)
            iconColor: Theme.textSecondaryColor
        }
    }

    Component {
        id: nativePlan
        ShotPlanText {
            exposePlainText: true
            Layout.fillWidth: true
            sentence: false
            maxLines: 3
            itemOrder: ["doseYield", "temperature", "grind"]
            profileName: card.recipe.profileTitle || ""
            profileTemp: card.profileTempC
            // Cards resolve their own profile frame data in the native host.
            profileStepTemps: card.profileStepTemps
            recipeTempOffsetC: card.recipe.tempOffsetC || 0
            tempOverridden: false
            dose: card.recipe.doseG || 0
            profileYield: card.profileYieldG
            targetWeight: {
                var mode = card.recipe.yieldMode || "none"
                var value = card.recipe.yieldValue || 0
                if (mode === "absolute" && value > 0) return value
                if (mode === "ratio" && value > 0)
                    return (card.recipe.doseG || 0) > 0 ? card.recipe.doseG * value : 0
                return card.profileYieldG
            }
            yieldAnchorMode: card.recipe.yieldMode || "none"
            yieldAnchorRatio: card.recipe.yieldMode === "ratio" ? (card.recipe.yieldValue || 0) : 0
            yieldOverridden: false
            grindSize: card.recipe.grindPinned || ""
            roasterBrand: ""
            coffeeName: ""
            onClicked: card.planClicked()
        }
    }

    RecipeDrinkCardView {
        id: cardView
        anchors.fill: parent
        cardStyle: nativeCardStyle
        recipeName: card.recipe.name || ""
        active: card.active
        activeLabel: trActiveBadge.text
        iconComponent: nativeDrinkIcon
        drinkLine: card.drinkLine()
        beanLine: card.beanLine()
        staleWarningVisible: false
        staleWarningText: trBagFinishedAction.text
        staleActionVisible: card.stale && card.showStaleAction
        staleActionText: trBagFinishedAction.text
        profileMissing: card.profileMissing
        profileMissingText: trProfileMissing.text.arg(card.recipe.profileTitle || "")
        planVisible: !card.isHotWaterOnly
        planComponent: nativePlan
        vesselVisible: card.isHotWaterOnly && card.vesselLine() !== ""
        vesselLine: card.vesselLine()
        thumbnailComponent: nativeThumbnail
        onStaleActionClicked: card.staleActionClicked()
        onPlanClicked: card.planClicked()
    }
}
