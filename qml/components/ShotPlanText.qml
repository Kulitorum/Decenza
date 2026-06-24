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

    // Visibility flags (passed through by ShotPlanItem). Roaster/grind/roast-date are
    // kept for source compatibility but are not part of the sentence by default.
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

    // Single target output weight for THIS shot — dose × selected ratio when a yield
    // is dialed (ProfileManager.targetWeight returns the override), else the profile
    // default. We intentionally show only this one number, not the profile default it
    // came from.
    readonly property string _yieldStr: {
        if (!(showDoseYield && targetWeight > 0)) return ""
        return targetWeight.toFixed(1) + "g"
    }
    // Use the shared adaptive formatter (single / mid-dot list / ellipsis + delta tag),
    // so multi-temperature profiles render honestly and an override shows as "+N°"
    // applied to every step. The formatter follows the C/F display unit.
    readonly property string _tempStr: {
        // temperatureDisplay() reads the C/F unit in C++, which QML can't capture as a
        // binding dependency — read it here so this re-evaluates when the unit switches.
        void(Settings.app.temperatureUnit)
        if (!(profileTemp > 0)) return ""
        return ProfileManager.temperatureDisplay(profileTemp, Settings.brew.hasTemperatureOverride, overrideTemp)
    }
    readonly property string _profileStr: (showProfile && profileName) ? profileName : ""
    readonly property string _beverage: TranslationManager.translate("idle.button.espresso", "Espresso")

    // Plain sentence — exposed as `text` for the wrapper's accessibility label and
    // its `visible: text !== ""` check.
    readonly property string text: {
        var _ = TranslationManager.translationVersion
        if (_profileStr !== "" && _tempStr !== "" && _yieldStr !== "")
            return TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                .arg(_yieldStr).arg(_beverage).arg(_profileStr).arg(_tempStr)
        // Degrade gracefully when a piece is missing (no scale target, no profile…).
        var parts = []
        if (_yieldStr !== "") parts.push(_yieldStr)
        if (_profileStr !== "") parts.push(_profileStr)
        if (_tempStr !== "") parts.push(_tempStr)
        return parts.join("  •  ")
    }

    // Rich version with the live values bolded (and parts HTML-escaped).
    readonly property string _rich: {
        var _ = TranslationManager.translationVersion
        function b(s) { return "<b>" + Theme.escapeHtml(s) + "</b>" }
        if (_profileStr !== "" && _tempStr !== "" && _yieldStr !== "")
            return TranslationManager.translate("shotplan.sentence", "Brew %1 of %2, using %3 at %4")
                .arg(b(_yieldStr)).arg(Theme.escapeHtml(_beverage)).arg(b(_profileStr)).arg(b(_tempStr))
        var parts = []
        if (_yieldStr !== "") parts.push(_yieldStr)
        if (_profileStr !== "") parts.push(_profileStr)
        if (_tempStr !== "") parts.push(_tempStr)
        return Theme.joinWithBullet(parts)
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
