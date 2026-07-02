import QtQuick
import Decenza
import "../"

Text {
    id: root

    signal clicked()

    elide: Text.ElideRight
    horizontalAlignment: Text.AlignHCenter
    font: Theme.bodyFont
    // Highlight (espresso-button yellow) whenever a brew override is in effect —
    // temperature or yield differs from the active profile's default — so the
    // summary signals "this isn't the plain profile/preset". Mirrors the exact
    // override conditions used to build the text below.
    readonly property bool _overrideActive:
        (Settings.brew.hasTemperatureOverride && Math.abs(overrideTemp - profileTemp) > 0.1)
        || (Settings.brew.hasBrewYieldOverride && profileYield > 0
            && Math.abs(targetWeight - profileYield) > 0.1)
    color: mouseArea.pressed ? Theme.accentColor
         : (_overrideActive ? Theme.highlightColor : Theme.textSecondaryColor)

    // Visibility flags — all default true except roastDate
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

    text: {
        // temperatureDisplay() reads Settings.app.temperatureUnit in C++, which QML
        // can't capture as a binding dependency — reference it here so the shot-plan
        // widget re-renders when the user toggles °C/°F.
        void(Settings.app.temperatureUnit)
        var parts = []
        if (showProfile && profileName) {
            // Adaptive temperature rendering (single / list / ellipsis + delta tag).
            // Shared with BrewDialog via ProfileManager.temperatureDisplay so a
            // multi-temp profile is shown honestly and the override delta applies
            // to all steps. Bindings re-evaluate on profile + override changes via
            // the referenced profileTemp / Settings.brew properties.
            var tempStr = profileTemp > 0
                ? ProfileManager.temperatureDisplay(profileTemp,
                        Settings.brew.hasTemperatureOverride, overrideTemp)
                : ""
            parts.push(profileName + (tempStr ? " (" + tempStr + ")" : ""))
        }
        if (showRoaster && roasterBrand)
            parts.push(roasterBrand)
        if (showGrind) {
            var coffeeParts = []
            if (coffeeName) coffeeParts.push(coffeeName)
            if (grindSize) coffeeParts.push("(" + grindSize + ")")
            if (coffeeParts.length > 0)
                parts.push(coffeeParts.join(" "))
        }
        if (showRoastDate && roastDate)
            parts.push(roastDate)
        if (showDoseYield && (dose > 0 || targetWeight > 0)) {
            var yieldParts = []
            if (dose > 0) yieldParts.push(dose.toFixed(1) + "g in")
            if (targetWeight > 0) {
                var yieldStr = targetWeight.toFixed(1) + "g out"
                if (Settings.brew.hasBrewYieldOverride && profileYield > 0
                        && Math.abs(targetWeight - profileYield) > 0.1) {
                    yieldStr = profileYield.toFixed(1) + " → " + targetWeight.toFixed(1) + "g out"
                }
                yieldParts.push(yieldStr)
            }
            parts.push(yieldParts.join(", "))
        }
        return parts.length > 0 ? parts.join(" \u00B7 ") : ""
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingSmall
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
