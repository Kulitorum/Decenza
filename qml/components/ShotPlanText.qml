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

    // --- Per-toggle segments (empty string = hidden). ---
    // Dose & yield: the shot's target output, plus dose-in (e.g. "18.0g in").
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
    readonly property string _beverage: {
        var _ = TranslationManager.translationVersion   // re-evaluate on a live language switch
        return TranslationManager.translate("idle.button.espresso", "Espresso")
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
        var dose = (_doseStr !== "") ? TranslationManager.translate("shotplan.doseIn", "%1 in").arg(fmt(_doseStr, true)) : ""
        var grind = (_grindStr !== "") ? TranslationManager.translate("shotplan.grind", "grind %1").arg(fmt(_grindStr, true)) : ""
        var roasted = (_roastDateStr !== "") ? TranslationManager.translate("shotplan.roasted", "roasted %1").arg(fmt(_roastDateStr, true)) : ""
        if (_yieldStr !== "" && _profileStr !== "" && _tempStr !== "") {
            var s = TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                .arg(fmt(_yieldStr, true)).arg(fmt(_beverage, false)).arg(fmt(_profileStr, true)).arg(fmt(_tempStr, true))
            var tail = []
            if (dose !== "") tail.push(dose)
            if (_roasterStr !== "") tail.push(fmt(_roasterStr, true))
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
        : (_tempOverride ? Theme.highlightColor : Theme.textColor)

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
