import QtQuick
import QtQuick.Layouts
import "EquipmentSummaryFormat.js" as EquipmentSummaryFormat

// Shared composition for the equipment summary. Both the native adapter and
// the browser host use this view, supplying translated labels, visual tokens,
// and only the small rich-text policies that differ between hosts.
ColumnLayout {
    id: root

    property string grinderName: ""
    property string grinderBrand: ""
    property string grinderModel: ""
    property string grinderBurrs: ""
    property string grindSetting: ""
    property int rpm: 0
    property bool rpmCapable: false
    property string basketBrand: ""
    property string basketModel: ""
    property var puckPrepLabels: []
    property string equipmentState: ""

    property string basketTemplate: "Basket: %1"
    property string puckPrepTemplate: "Prep: %1"
    property string lastGrindTemplate: "Grind %1"
    property string lastRpmTemplate: "%1 rpm"
    property string olderText: "Older equipment — a newer version is now in use"
    property string retiredText: "Retired equipment — no longer in inventory"

    property font bodyFont
    property font labelFont
    property font captionFont
    property real titlePixelSize: labelFont.pixelSize
    property color primaryTextColor: "black"
    property color secondaryTextColor: "gray"
    property real lineSpacing: 2

    // Host adapters can supply native emoji rendering and emphasized bullets.
    // Defaults safely escape StyledText and keep the same visible wording.
    property var richTextFor: function(value) { return root._escapeStyled(value) }
    property var joinWithBullet: function(parts) {
        return parts.map(function(part) { return root._escapeStyled(part) })
            .join(" <font size=\"+1\"><b>·</b></font> ")
    }

    readonly property var formatted: EquipmentSummaryFormat.format({
        grinderName: grinderName,
        grinderBrand: grinderBrand,
        grinderModel: grinderModel,
        grinderBurrs: grinderBurrs,
        grindSetting: grindSetting,
        rpm: rpm,
        rpmCapable: rpmCapable,
        basketBrand: basketBrand,
        basketModel: basketModel,
        puckPrepLabels: puckPrepLabels,
        equipmentState: equipmentState
    }, {
        basket: basketTemplate,
        puckPrep: puckPrepTemplate,
        lastGrind: lastGrindTemplate,
        lastRpm: lastRpmTemplate,
        older: olderText,
        retired: retiredText
    })

    readonly property string title: formatted.title
    readonly property string basketLine: formatted.basketLine
    readonly property string puckPrepLine: formatted.puckPrepLine
    readonly property string lastDialLine: formatted.lastDialLine
    readonly property string lineageText: formatted.lineageText
    readonly property string accessibleSummary: formatted.accessibleSummary
    readonly property string puckPrepLineRich: formatted.puckPrepLabels.length > 0
        ? formatted.puckPrepTemplate.replace(
              "%1", joinWithBullet(formatted.puckPrepLabels)) : ""
    readonly property string lastDialLineRich: joinWithBullet(formatted.dialParts)

    function _escapeStyled(value) {
        return String(value === undefined || value === null ? "" : value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
    }

    spacing: lineSpacing

    EquipmentSummaryRenderer {
        Layout.fillWidth: true
        lineageText: root.lineageText
        titleRichText: root.richTextFor(root.title, root.titlePixelSize)
        grinderBurrsRichText: root.richTextFor(root.grinderBurrs, root.labelFont.pixelSize)
        lastDialLineRichText: root.joinWithBullet(root.formatted.dialParts)
        basketLineRichText: root.richTextFor(root.basketLine, root.labelFont.pixelSize)
        puckPrepLineRichText: root.formatted.puckPrepLabels.length > 0
            ? root.formatted.puckPrepTemplate.replace(
                  "%1", root.joinWithBullet(root.formatted.puckPrepLabels)) : ""
        bodyFont: root.bodyFont
        labelFont: root.labelFont
        captionFont: root.captionFont
        titlePixelSize: root.titlePixelSize
        primaryTextColor: root.primaryTextColor
        secondaryTextColor: root.secondaryTextColor
        lineSpacing: root.lineSpacing
    }
}
