import QtQuick
import Decenza
import "../"

// The brew summary for the home screen, read as a sentence rather than a row of
// dotted fragments — "Brewing 36.0g of Espresso, using <profile> at 92°C" — with
// the live values bolded and a leading cup icon, because the shot plan is the most
// important thing on the idle screen. Tapping it opens Brew Settings.
//
// Root is an Item (icon + text) but it preserves the old ShotPlanText API the
// ShotPlanItem wrapper relies on: a readonly `text` (plain sentence, for
// accessibility / visibility), a `clicked()` signal, and implicit sizing.
Item {
    id: root

    signal clicked()

    // Visibility flags (passed through by ShotPlanItem) — one per Shot Plan display option:
    // Profile & temperature, Roaster, Coffee (grind), Roast date, Dose & yield. Each toggles its
    // segment both in the sentence/tail and in the fallback fragment list.
    property bool showProfile: true
    property bool showRoaster: true
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
    readonly property bool _overrideActive: _tempOverride

    // --- Per-toggle segments (empty string = hidden). ---
    // Dose & yield: the shot's target output, plus dose-in (restores the old "18.0g in").
    readonly property string _yieldStr: (showDoseYield && targetWeight > 0) ? (targetWeight.toFixed(1) + "g") : ""
    readonly property string _doseStr: (showDoseYield && dose > 0) ? (dose.toFixed(1) + "g") : ""
    // Profile & temperature (one option → both). temperatureDisplay() follows the C/F display unit;
    // its Settings.app.temperatureUnit read is in C++, invisible to QML bindings, so read it here.
    readonly property string _profileStr: (showProfile && profileName) ? profileName : ""
    readonly property string _tempStr: {
        void(Settings.app.temperatureUnit)
        if (!(showProfile && profileTemp > 0)) return ""
        return ProfileManager.temperatureDisplay(profileTemp, Settings.brew.hasTemperatureOverride, overrideTemp)
    }
    readonly property string _roasterStr: showRoaster ? ((roasterBrand + " " + coffeeName).trim()) : ""
    readonly property string _grindStr: (showGrind && grindSize.length > 0) ? grindSize : ""
    readonly property string _roastDateStr: (showRoastDate && roastDate.length > 0) ? roastDate : ""
    readonly property string _beverage: TranslationManager.translate("idle.button.espresso", "Espresso")

    // Styled separator (relative-sized bold safe-dot ·) for the rich (StyledText) version.
    readonly property string _richSep: " <font size=\"+1\"><b>·</b></font> "

    // Plain sentence — exposed as `text` for the wrapper's accessibility label and its
    // `visible: text !== ""` check. Core is the sentence (yield + profile + temp); any enabled extras
    // (dose, roaster, grind, roast date) trail after it, and if the core can't render we fall back to a
    // plain fragment list of everything enabled.
    readonly property string text: {
        var _ = TranslationManager.translationVersion
        var SEP = "  ·  "
        var dose = (_doseStr !== "") ? TranslationManager.translate("shotplan.doseIn", "%1 in").arg(_doseStr) : ""
        var grind = (_grindStr !== "") ? TranslationManager.translate("shotplan.grind", "grind %1").arg(_grindStr) : ""
        var roasted = (_roastDateStr !== "") ? TranslationManager.translate("shotplan.roasted", "roasted %1").arg(_roastDateStr) : ""
        if (_yieldStr !== "" && _profileStr !== "" && _tempStr !== "") {
            var s = TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                .arg(_yieldStr).arg(_beverage).arg(_profileStr).arg(_tempStr)
            var tail = []
            if (dose !== "") tail.push(dose)
            if (_roasterStr !== "") tail.push(_roasterStr)
            if (grind !== "") tail.push(grind)
            if (roasted !== "") tail.push(roasted)
            return tail.length > 0 ? (s + SEP + tail.join(SEP)) : s
        }
        var parts = []
        if (dose !== "") parts.push(dose)
        if (_yieldStr !== "") parts.push(_yieldStr)
        if (_profileStr !== "") parts.push(_profileStr)
        if (_tempStr !== "") parts.push(_tempStr)
        if (_roasterStr !== "") parts.push(_roasterStr)
        if (grind !== "") parts.push(grind)
        if (roasted !== "") parts.push(roasted)
        return parts.join(SEP)
    }

    // Rich version with the live values bolded (all parts HTML-escaped).
    readonly property string _rich: {
        var _ = TranslationManager.translationVersion
        function b(s) { return "<b>" + Theme.escapeHtml(s) + "</b>" }
        var dose = (_doseStr !== "") ? TranslationManager.translate("shotplan.doseIn", "%1 in").arg(b(_doseStr)) : ""
        var grind = (_grindStr !== "") ? TranslationManager.translate("shotplan.grind", "grind %1").arg(b(_grindStr)) : ""
        var roasted = (_roastDateStr !== "") ? TranslationManager.translate("shotplan.roasted", "roasted %1").arg(b(_roastDateStr)) : ""
        if (_yieldStr !== "" && _profileStr !== "" && _tempStr !== "") {
            var s = TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                .arg(b(_yieldStr)).arg(Theme.escapeHtml(_beverage)).arg(b(_profileStr)).arg(b(_tempStr))
            var tail = []
            if (dose !== "") tail.push(dose)
            if (_roasterStr !== "") tail.push(b(_roasterStr))
            if (grind !== "") tail.push(grind)
            if (roasted !== "") tail.push(roasted)
            return tail.length > 0 ? (s + _richSep + tail.join(_richSep)) : s
        }
        var parts = []
        if (dose !== "") parts.push(dose)
        if (_yieldStr !== "") parts.push(b(_yieldStr))
        if (_profileStr !== "") parts.push(b(_profileStr))
        if (_tempStr !== "") parts.push(b(_tempStr))
        if (_roasterStr !== "") parts.push(b(_roasterStr))
        if (grind !== "") parts.push(grind)
        if (roasted !== "") parts.push(roasted)
        return parts.join(_richSep)
    }

    readonly property color _color: mouseArea.pressed ? Theme.accentColor
        : (_overrideActive ? Theme.highlightColor : Theme.textColor)

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
