import QtQuick
import QtQuick.Layouts
import Decenza

// Native adapter for the service-free EquipmentSummaryView. Keep this public
// input/output surface stable for inventory cards and shot pages; all summary
// wording and line composition live in the shared view.
ColumnLayout {
    id: summary

    property string grinderName: ""
    property string grinderBrand: ""
    property string grinderModel: ""
    property string grinderBurrs: ""
    property string grindSetting: ""
    property int rpm: 0
    property bool rpmCapable: false
    property string basketBrand: ""
    property string basketModel: ""
    property string puckPrepCanonical: ""
    property string equipmentState: ""

    readonly property string title: view.title
    readonly property string basketLine: view.basketLine
    readonly property string puckPrepLine: view.puckPrepLine
    readonly property string puckPrepLineRich: view.puckPrepLineRich
    readonly property string lastDialLine: view.lastDialLine
    readonly property string lastDialLineRich: view.lastDialLineRich
    readonly property string lineageText: view.lineageText
    readonly property string accessibleSummary: view.accessibleSummary

    spacing: 0

    function translated(key, fallback) {
        var _ = TranslationManager.translationVersion
        return TranslationManager.translate(key, fallback)
    }

    function translatedPuckPrepLabels(canonical) {
        var _ = TranslationManager.translationVersion
        return PuckPrepLabels.labelsFor(canonical)
    }

    EquipmentSummaryView {
        id: view
        Layout.fillWidth: true

        grinderName: summary.grinderName
        grinderBrand: summary.grinderBrand
        grinderModel: summary.grinderModel
        grinderBurrs: summary.grinderBurrs
        grindSetting: summary.grindSetting
        rpm: summary.rpm
        rpmCapable: summary.rpmCapable
        basketBrand: summary.basketBrand
        basketModel: summary.basketModel
        puckPrepLabels: summary.translatedPuckPrepLabels(summary.puckPrepCanonical)
        equipmentState: summary.equipmentState

        basketTemplate: summary.translated("equipment.card.basket", "Basket: %1")
        puckPrepTemplate: summary.translated("equipment.card.puckPrep", "Prep: %1")
        lastGrindTemplate: summary.translated("equipment.card.lastGrind", "Grind %1")
        lastRpmTemplate: summary.translated("equipment.card.lastRpm", "%1 rpm")
        olderText: summary.translated("shotdetail.equipmentOlder", "Older equipment — a newer version is now in use")
        retiredText: summary.translated("shotdetail.equipmentRetired", "Retired equipment — no longer in inventory")

        richTextFor: function(value, size) {
            return Theme.replaceEmojiWithImg(value, size)
        }

        bodyFont: Theme.bodyFont
        labelFont: Theme.labelFont
        captionFont: Theme.captionFont
        titlePixelSize: Theme.subtitleFont.pixelSize
        primaryTextColor: Theme.textColor
        secondaryTextColor: Theme.textSecondaryColor
        lineSpacing: Theme.scaled(2)
    }
}
