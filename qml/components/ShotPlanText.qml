import QtQuick
import Decenza
import "../"

Text {
    id: root

    signal clicked()

    elide: Text.ElideRight
    horizontalAlignment: Text.AlignHCenter
    font: Theme.bodyFont
    color: mouseArea.pressed ? Theme.accentColor : Theme.textSecondaryColor

    // Visibility flags — all default true except roastDate
    property bool showProfile: true
    property bool showRoaster: true     // Roaster name (e.g. "Caffe Lusso")
    property bool showGrind: true       // Coffee name + grind setting (e.g. "Gran Miscela Carmo Espresso Blend (14)")
    property bool showRoastDate: false
    property bool showDoseYield: true

    property string profileName: ProfileManager.currentProfileName
    property double profileTemp: ProfileManager.profileTargetTemperature
    property double overrideTemp: Settings.hasTemperatureOverride ? Settings.temperatureOverride : profileTemp
    property string roasterBrand: Settings.dyeBeanBrand || ""
    property string coffeeName: Settings.dyeBeanType || ""
    property string roastDate: Settings.dyeRoastDate
    property string grindSize: Settings.dyeGrinderSetting
    property double dose: Settings.dyeBeanWeight
    property double targetWeight: ProfileManager.targetWeight

    text: {
        var parts = []
        if (showProfile && profileName) {
            var tempStr = profileTemp > 0 ? profileTemp.toFixed(0) + "\u00B0C" : ""
            if (Settings.hasTemperatureOverride && Math.abs(overrideTemp - profileTemp) > 0.1) {
                tempStr = profileTemp.toFixed(0) + " \u2192 " + overrideTemp.toFixed(0) + "\u00B0C"
            }
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
            if (targetWeight > 0) yieldParts.push(targetWeight.toFixed(1) + "g out")
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
