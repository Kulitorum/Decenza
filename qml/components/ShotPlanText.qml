pragma ComponentBehavior: Bound

import QtQuick
import Decenza
import "SharedShotPlanFormat.js" as ShotPlanFormat

// Shared Shot Plan presentation. Host preferences and recipe state have the
// same service contract in the native app and its paired browser.
Item {
    id: root

    property var itemOrder: ["doseYield", "profile", "temperature", "roaster", "coffee", "grind"]
    property bool sentence: true
    property bool stacked: false
    property bool yieldTargetOnly: false
    property double actualYield: 0
    property int maxLines: 2
    property bool exposePlainText: false

    property string profileName: ProfileService.currentProfileSummary.name || ""
    property double profileTemp: ProfileManager.profileTargetTemperature
    property double overrideTemp: Settings.brew.hasTemperatureOverride ? Settings.brew.temperatureOverride : profileTemp
    property string roasterBrand: RecipeContext.preferences.dyeBeanBrand || ""
    property string coffeeName: RecipeContext.preferences.dyeBeanType || ""
    property string roastDate: RecipeContext.preferences.dyeRoastDate || ""
    property string grindSize: RecipeContext.preferences.dyeGrinderSetting || ""
    property double dose: RecipeContext.preferences.dyeBeanWeight || 0
    property double profileYield: ProfileManager.profileTargetWeight
    property double targetWeight: ProfileManager.targetWeight
    property bool yieldOverridden: RecipeContext.preferences.hasBrewYieldOverride === true
    property bool tempOverridden: Settings.brew.hasTemperatureOverride
    property string yieldAnchorMode: RecipeContext.preferences.brewYieldMode || ""
    property double yieldAnchorRatio: yieldAnchorMode === "ratio" ? RecipeContext.preferences.brewYieldOverride : 0
    property double baselineYieldG: 0
    property double recipeBaselineTemp: 0
    property var profileStepTemps: []
    property double recipeTempOffsetC: 0
    readonly property double _yieldBaseline: baselineYieldG > 0 ? baselineYieldG : profileYield
    readonly property double _tempHlBaseline: recipeBaselineTemp > 0 ? recipeBaselineTemp : profileTemp
    property int grindRpm: RecipeContext.preferences.dyeGrinderRpm || 0
    property bool rpmCapable: RecipeContext.preferences.grinderRpmCapable === true
    property string beverageType: ProfileService.currentProfileSummary.beverageType || ""
    property bool isCleaning: ProfileService.currentProfileSummary.isMaintenance === true
    property string recipeName: {
        const id = RecipeService.activeRecipeId
        const rows = RecipeService.recipes
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].id === id) return rows[i].name || ""
        return ""
    }

    readonly property bool _tempOverride:
        tempOverridden && Math.abs(overrideTemp - _tempHlBaseline) > 0.1
    readonly property bool _yieldOverride:
        yieldOverridden && _yieldBaseline > 0 && Math.abs(targetWeight - _yieldBaseline) > 0.1
    readonly property bool _isCleaning: isCleaning

    function _has(key) { return itemOrder && itemOrder.indexOf(key) !== -1 }

    readonly property string _tempStr: {
        void(Settings.app.temperatureUnit)
        if (!(_has("temperature") && profileTemp > 0)) return ""
        if (profileStepTemps && profileStepTemps.length > 0)
            return TemperatureDisplay.formatForSteps(profileStepTemps, profileTemp, false, 0,
                Settings.app.temperatureUnit === "fahrenheit", recipeTempOffsetC)
        var effectiveTemp = tempOverridden ? overrideTemp : _tempHlBaseline
        return TemperatureDisplay.formatForSteps(ProfileService.currentProfileSummary.stepTempsC || [],
            effectiveTemp, false, effectiveTemp, Settings.app.temperatureUnit === "fahrenheit",
            effectiveTemp - profileTemp)
    }

    readonly property string _beverage: {
        void(TranslationManager.translationVersion)
        if (beverageType === "espresso") return TranslationManager.translate("idle.button.espresso", "Espresso")
        if (beverageType.indexOf("tea") === 0) return TranslationManager.translate("shotplan.beverage.tea", "tea")
        return TranslationManager.translate("shotplan.beverage.coffee", "coffee")
    }

    readonly property var _formatInput: ({
        itemOrder: itemOrder,
        sentence: sentence,
        stacked: stacked,
        isCleaning: _isCleaning,
        profileName: profileName,
        recipeName: recipeName,
        temperatureText: _tempStr,
        tempOverridden: tempOverridden,
        overrideTemp: overrideTemp,
        tempHlBaseline: _tempHlBaseline,
        profileYield: profileYield,
        baselineYieldG: baselineYieldG,
        targetWeight: targetWeight,
        yieldOverridden: yieldOverridden,
        yieldAnchorMode: yieldAnchorMode,
        yieldAnchorRatio: yieldAnchorRatio,
        yieldTargetOnly: yieldTargetOnly,
        actualYield: actualYield,
        dose: dose,
        roasterBrand: roasterBrand,
        coffeeName: coffeeName,
        roastDate: roastDate,
        grindSize: grindSize,
        grindRpm: grindRpm,
        rpmCapable: rpmCapable,
        beverageText: _beverage
    })

    readonly property string _hlHex: Theme.colorToHex(Theme.highlightColor)
    readonly property var _formatted: {
        void(TranslationManager.translationVersion)
        return ShotPlanFormat.format(_formatInput,
            function(key, fallback) { return TranslationManager.translate(key, fallback) },
            {
                plainSeparator: "  ·  ",
                richSeparator: Theme.bulletSep,
                highlightColor: _hlHex,
                escapeHtml: function(value) { return Theme.escapeHtml(value) },
                replaceEmoji: function(value) {
                    return Theme.replaceEmojiWithImg(value, Theme.bodyFont.pixelSize, true)
                }
            })
    }

    readonly property string text: _formatted.text
    readonly property string _rich: _formatted.rich
    readonly property color _color: planView.contentColor

    signal clicked()
    implicitWidth: planView.implicitWidth
    implicitHeight: planView.implicitHeight

    SharedShotPlanStyle {
        id: nativeStyle
        bodyFont: Theme.bodyFont
        textColor: Theme.textColor
        pressedColor: Theme.accentColor
        warningColor: Theme.errorColor
        spacing: Theme.spacingSmall
        iconSize: Theme.scaled(20)
        hitInset: Theme.spacingSmall
    }

    Component {
        id: nativeIcon
        ColoredIcon {
            source: "qrc:/icons/espresso.svg"
            iconWidth: nativeStyle.iconSize
            iconHeight: nativeStyle.iconSize
            iconColor: planView.contentColor
            Accessible.ignored: true
        }
    }

    SharedShotPlanView {
        id: planView
        anchors.fill: parent
        planStyle: nativeStyle
        plainText: root.text
        richText: root._rich
        isCleaning: root._isCleaning
        maxLines: root.maxLines
        interactive: true
        exposePlainText: root.exposePlainText
        iconComponent: nativeIcon
        onClicked: root.clicked()
    }
}
