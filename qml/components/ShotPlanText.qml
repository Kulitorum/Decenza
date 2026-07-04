import QtQuick
import Decenza
import "../"

// The brew summary for the home screen, read as a sentence rather than a row of
// dotted fragments — "Brew 36.0g of Espresso, using <profile> at 92°C" — with
// the live values bolded and a leading cup icon, because the shot plan is the most
// important thing on the idle screen. Tapping it opens Brew Settings.
//
// Root is an Item (icon + text) but it preserves the old ShotPlanText API the
// ShotPlanItem wrapper relies on: a readonly `text` (plain sentence, for
// accessibility / visibility), a `clicked()` signal, and implicit sizing.
Item {
    id: root

    signal clicked()

    // Visibility flags (passed through by ShotPlanItem/PlanItem) — one per Shot Plan display option:
    // Profile & temperature, Roaster, Coffee, Grind (+ RPM), Roast date, Dose & yield. Each toggles
    // its segment both in the sentence/tail and in the fallback fragment list.
    property bool showProfile: true
    property bool showRoaster: true
    property bool showCoffee: true
    property bool showGrind: true
    property bool showRoastDate: false
    property bool showDoseYield: true

    property string profileName: ProfileManager.currentProfileName
    property double profileTemp: ProfileManager.profileTargetTemperature
    property double overrideTemp: Settings.brew.hasTemperatureOverride ? Settings.brew.temperatureOverride : profileTemp
    property string roasterBrand: Settings.dye.dyeBeanBrand || ""
    property string coffeeName: Settings.dye.dyeBeanType || ""
    property string roastDate: Settings.dye.dyeRoastDate
    property string grindSize: Settings.dye.dyeGrinderSetting
    property double dose: Settings.dye.dyeBeanWeight
    property double profileYield: ProfileManager.profileTargetWeight
    property double targetWeight: ProfileManager.targetWeight

    // Highlight (espresso-button yellow) on a brew TEMPERATURE override only.
    // Yield is intentionally excluded: the target output is dose × ratio and the
    // measured dose never exactly equals the profile's listed dose, so yield would
    // almost always differ — leaving the plan permanently highlighted.
    readonly property bool _tempOverride:
        Settings.brew.hasTemperatureOverride && Math.abs(overrideTemp - profileTemp) > 0.1

    // --- Per-toggle segments (empty string = hidden). ---
    // Dose & yield: the shot's target output, plus dose-in (e.g. "18.0g in"). A DELIBERATE yield
    // override (the hasBrewYieldOverride flag, not raw drift — measured dose never exactly matches
    // the profile's) renders as "36.0 → 40.0g", mirroring the temperature-override treatment.
    readonly property string _yieldStr: {
        if (!(showDoseYield && targetWeight > 0)) return ""
        if (Settings.brew.hasBrewYieldOverride && profileYield > 0
                && Math.abs(targetWeight - profileYield) > 0.1)
            return profileYield.toFixed(1) + " → " + targetWeight.toFixed(1) + "g"
        return targetWeight.toFixed(1) + "g"
    }
    readonly property string _doseStr: (showDoseYield && dose > 0) ? (dose.toFixed(1) + "g") : ""
    // Profile & temperature (one option → both). temperatureDisplay() follows the C/F display unit;
    // its Settings.app.temperatureUnit read is in C++, invisible to QML bindings, so read it here.
    readonly property string _profileStr: (showProfile && profileName) ? profileName : ""
    readonly property string _tempStr: {
        void(Settings.app.temperatureUnit)
        if (!(showProfile && profileTemp > 0)) return ""
        return ProfileManager.temperatureDisplay(profileTemp, Settings.brew.hasTemperatureOverride, overrideTemp)
    }
    // Roaster = brand only; Coffee = bean name only; Grind = grinder setting + RPM when recorded.
    // Each option gates exactly its named content so saved widget configs mean what they say.
    readonly property string _roasterStr: (showRoaster && roasterBrand) ? roasterBrand : ""
    readonly property string _coffeeStr: (showCoffee && coffeeName) ? coffeeName : ""
    readonly property string _grindStr: {
        if (!showGrind) return ""
        var parts = []
        if (grindSize.length > 0) parts.push(grindSize)
        if (Settings.dye.dyeGrinderRpm > 0)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(Settings.dye.dyeGrinderRpm))
        return parts.join(" · ")
    }
    readonly property string _roastDateStr: (showRoastDate && roastDate.length > 0) ? roastDate : ""
    // Beverage word from the profile's beverage_type: "Espresso" (the default type),
    // generic "coffee" for filter/pourover/any other coffee type, "tea" for tea
    // profiles ("tea"/"tea_portafilter"). Cleaning/descale profiles get their own
    // sentence in _build() — no bean/dose tail, plus the do-not-load-coffee warning.
    readonly property string _bevType: ProfileManager.currentProfileBeverageType
    readonly property bool _isCleaning: _bevType === "cleaning" || _bevType === "descale"
    readonly property string _beverage: {
        var _ = TranslationManager.translationVersion   // re-evaluate on a live language switch
        if (_bevType === "espresso") return TranslationManager.translate("idle.button.espresso", "Espresso")
        if (_bevType.indexOf("tea") === 0) return TranslationManager.translate("shotplan.beverage.tea", "tea")
        return TranslationManager.translate("shotplan.beverage.coffee", "coffee")
    }

    // Break a "%N" token in a user-supplied value so QString.arg can't substitute a later arg into it
    // (a profile/roaster/bean literally named e.g. "Ramp %2"). The zero-width space is invisible.
    function _argSafe(v) { return String(v).replace(/%(\d)/g, "%\u200B$1") }

    // ONE renderer for both the plain `text` (a11y label + `visible: text !== ""` check) and the bolded
    // `_rich` (display), so they can NEVER drift. fmt(value, live) formats one value: plain %-escapes,
    // rich HTML-escapes and bolds live values. Core sentence is yield + profile + temp; enabled extras
    // (dose, roaster, grind, roast date) trail after it, else it degrades to a fragment list.
    function _build(fmt, sep) {
        var _ = TranslationManager.translationVersion
        // Cleaning/descale run — beans are the enemy here. Short sentence, no
        // dose/bean tail, and the warning rides along into the a11y label too.
        if (_isCleaning) {
            return (_profileStr !== "")
                ? TranslationManager.translate("shotplan.sentenceCleaning", "Cleaning run with %1 — no coffee in the portafilter!")
                    .arg(fmt(_profileStr, true))
                : TranslationManager.translate("shotplan.cleaningNoProfile", "Cleaning run — no coffee in the portafilter!")
        }
        var dose = (_doseStr !== "") ? TranslationManager.translate("shotplan.doseIn", "%1 in").arg(fmt(_doseStr, true)) : ""
        // "grind" prefix only when there is a grinder setting; a recorded-RPM-only segment
        // already reads as "90 rpm" and "grind 90 rpm" would be wrong.
        var grind = (_grindStr === "") ? ""
            : (grindSize.length > 0 ? TranslationManager.translate("shotplan.grind", "grind %1").arg(fmt(_grindStr, true))
                                    : fmt(_grindStr, true))
        var roasted = (_roastDateStr !== "") ? TranslationManager.translate("shotplan.roasted", "roasted %1").arg(fmt(_roastDateStr, true)) : ""
        if (_yieldStr !== "" && _profileStr !== "" && _tempStr !== "") {
            var s = TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                .arg(fmt(_yieldStr, true)).arg(fmt(_beverage, false)).arg(fmt(_profileStr, true)).arg(fmt(_tempStr, true))
            var tail = []
            if (dose !== "") tail.push(dose)
            if (_roasterStr !== "") tail.push(fmt(_roasterStr, true))
            if (_coffeeStr !== "") tail.push(fmt(_coffeeStr, true))
            if (grind !== "") tail.push(grind)
            if (roasted !== "") tail.push(roasted)
            return tail.length > 0 ? (s + sep + tail.join(sep)) : s
        }
        var parts = []
        if (dose !== "") parts.push(dose)
        if (_yieldStr !== "") parts.push(fmt(_yieldStr, true))
        if (_profileStr !== "") parts.push(fmt(_profileStr, true))
        if (_tempStr !== "") parts.push(fmt(_tempStr, true))
        if (_roasterStr !== "") parts.push(fmt(_roasterStr, true))
        if (_coffeeStr !== "") parts.push(fmt(_coffeeStr, true))
        if (grind !== "") parts.push(grind)
        if (roasted !== "") parts.push(roasted)
        return parts.join(sep)
    }

    // Plain: for the accessibility label + `visible: text !== ""`.
    readonly property string text: _build(function(v, live) { return _argSafe(v) }, "  ·  ")
    // Rich: same content, live values bolded, all HTML-escaped; the styled bold safe-dot · separator.
    readonly property string _rich: _build(function(v, live) {
        var e = Theme.escapeHtml(_argSafe(v))
        return live ? ("<b>" + e + "</b>") : e
    }, " <font size=\"+1\"><b>·</b></font> ")

    readonly property color _color: mouseArea.pressed ? Theme.accentColor
        : (_isCleaning ? Theme.warningColor
                       : (_tempOverride ? Theme.highlightColor : Theme.textColor))

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        ColoredIcon {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/espresso.svg"
            iconWidth: Theme.scaled(20)
            iconHeight: Theme.scaled(20)
            iconColor: root._color
            Accessible.ignored: true
        }

        Text {
            id: planText
            anchors.verticalCenter: parent.verticalCenter
            text: root._rich
            textFormat: Text.StyledText
            font: Theme.bodyFont
            color: root._color
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingSmall
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
