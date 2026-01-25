import QtQuick
import "../"

Text {
    id: root

    signal clicked()

    elide: Text.ElideRight
    horizontalAlignment: Text.AlignHCenter
    font: Theme.bodyFont
    color: mouseArea.pressed ? Theme.accentColor : Theme.textSecondaryColor

    property string profileName: MainController.currentProfileName
    property double profileTemp: MainController.profileTargetTemperature
    property double overrideTemp: Settings.hasTemperatureOverride ? Settings.temperatureOverride : profileTemp
    property string beanName: {
        if (Settings.selectedBeanPreset >= 0 && Settings.selectedBeanPreset < Settings.beanPresets.length)
            return Settings.beanPresets[Settings.selectedBeanPreset].name
        if (Settings.dyeBeanBrand || Settings.dyeBeanType)
            return [Settings.dyeBeanBrand, Settings.dyeBeanType].filter(Boolean).join(" ")
        return ""
    }
    property string grindSize: Settings.visualizerExtendedMetadata
        ? (Settings.hasBrewGrindOverride ? Settings.brewGrindOverride : Settings.dyeGrinderSetting)
        : ""
    property double dose: Settings.hasBrewDoseOverride ? Settings.brewDoseOverride : Settings.dyeBeanWeight
    property double targetWeight: Settings.hasBrewYieldOverride ? Settings.brewYieldOverride : MainController.targetWeight

    text: {
        var parts = []
        var tempStr = profileTemp > 0 ? profileTemp.toFixed(0) + "\u00B0C" : ""
        if (Settings.hasTemperatureOverride && Math.abs(overrideTemp - profileTemp) > 0.1) {
            tempStr = profileTemp.toFixed(0) + " \u2192 " + overrideTemp.toFixed(0) + "\u00B0C"
        }
        if (profileName) {
            parts.push(profileName + (tempStr ? " (" + tempStr + ")" : ""))
        }
        if (beanName) {
            parts.push(beanName + (grindSize ? " (" + grindSize + ")" : ""))
        } else if (grindSize) {
            parts.push("Grind: " + grindSize)
        }
        if (dose > 0 || targetWeight > 0) {
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
