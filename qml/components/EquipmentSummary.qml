import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Read-only presentational core for an equipment package (add-equipment-packages).
// The single source of truth for how a package's identity renders: grinder title,
// burrs subtitle, last-dial line, basket line, and puck-prep line. Used by the
// inventory EquipmentCard (which adds the selected border + action buttons around
// it) and by the read-only Shot Detail / Post-Shot Review pages.
//
// Inputs are NORMALIZED SCALARS, not a package or shot object, so either shape can
// feed it. Puck prep is taken as the CANONICAL comma-joined flag string (see
// src/core/puckprep.h) — a package exposes it as `puckPrepCanonical`, a shot as
// `puckPrep` — so one code path renders both. equipmentState carries the
// "older"/"retired" lineage qualifier in shot contexts (empty for live packages).
ColumnLayout {
    id: summary

    property string grinderName: ""   // resolved display name; falls back to brand+model
    property string grinderBrand: ""
    property string grinderModel: ""
    property string grinderBurrs: ""
    property string grindSetting: ""
    property int rpm: 0
    property bool rpmCapable: false
    property string basketBrand: ""
    property string basketModel: ""
    property string puckPrepCanonical: ""
    property string equipmentState: ""  // "", "older", "retired"

    spacing: Theme.scaled(2)

    readonly property string title: {
        var combined = [grinderBrand, grinderModel]
            .filter(function(s) { return s.length > 0 }).join(" ")
        return grinderName.length > 0 ? grinderName : combined
    }

    // Puck-prep set flags as a "WDT • Shaker" summary, parsed from the canonical
    // string. MIRRORS PuckPrep::flagKeys() display order (src/core/puckprep.h).
    readonly property var _puckFlags: puckPrepCanonical.split(",")
    function _hasPuck(key) { return _puckFlags.indexOf(key) >= 0 }

    readonly property string basketLine: {
        var _ = TranslationManager.translationVersion
        var b = [basketBrand, basketModel]
            .filter(function(s) { return s.length > 0 }).join(" ")
        return b.length > 0 ? TranslationManager.translate("equipment.card.basket", "Basket: %1").arg(b) : ""
    }

    readonly property string puckPrepLine: {
        var _ = TranslationManager.translationVersion
        var labels = []
        if (_hasPuck("wdt")) labels.push(TranslationManager.translate("equipment.dialog.puckWdt", "WDT"))
        if (_hasPuck("shaker")) labels.push(TranslationManager.translate("equipment.dialog.puckShaker", "Shaker"))
        if (_hasPuck("puckScreen")) labels.push(TranslationManager.translate("equipment.dialog.puckScreen", "Puck screen"))
        if (_hasPuck("paperFilter")) labels.push(TranslationManager.translate("equipment.dialog.puckPaper", "Bottom paper filter"))
        if (_hasPuck("rdt")) labels.push(TranslationManager.translate("equipment.dialog.puckRdt", "RDT (spritz)"))
        return labels.length > 0
            ? TranslationManager.translate("equipment.card.puckPrep", "Prep: %1").arg(labels.join(" • ")) : ""
    }

    readonly property string lastDialLine: {
        var _ = TranslationManager.translationVersion
        var parts = []
        if (String(grindSetting).length > 0)
            parts.push(TranslationManager.translate("equipment.card.lastGrind", "Grind %1").arg(grindSetting))
        if (rpmCapable && rpm > 0)
            parts.push(TranslationManager.translate("equipment.card.lastRpm", "%1 rpm").arg(rpm))
        return parts.join(" • ")
    }

    // Lineage qualifier text — only meaningful when equipmentState is set.
    readonly property string lineageText: {
        var _ = TranslationManager.translationVersion
        if (equipmentState === "older")
            return TranslationManager.translate("shotdetail.equipmentOlder", "Older equipment — a newer version is now in use")
        if (equipmentState === "retired")
            return TranslationManager.translate("shotdetail.equipmentRetired", "Retired equipment — no longer in inventory")
        return ""
    }

    readonly property string accessibleSummary: {
        var bits = [title, grinderBurrs].filter(function(s) { return s.length > 0 })
        if (lastDialLine.length > 0) bits.push(lastDialLine)
        if (basketLine.length > 0) bits.push(basketLine)
        if (puckPrepLine.length > 0) bits.push(puckPrepLine)
        if (lineageText.length > 0) bits.push(lineageText)
        return bits.join(", ")
    }

    // Lineage qualifier first (shot contexts): notes when this shot's package is
    // superseded ("older") or gone ("retired"). Never baked into the name.
    Text {
        Layout.fillWidth: true
        visible: summary.lineageText.length > 0
        text: summary.lineageText
        font: Theme.labelFont
        color: Theme.textSecondaryColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        textFormat: Text.RichText
        text: Theme.replaceEmojiWithImg(summary.title, Theme.subtitleFont.pixelSize)
        font.family: Theme.bodyFont.family
        font.pixelSize: Theme.subtitleFont.pixelSize
        font.bold: true
        color: Theme.textColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        visible: summary.grinderBurrs.length > 0
        textFormat: Text.RichText
        text: Theme.replaceEmojiWithImg(summary.grinderBurrs, Theme.labelFont.pixelSize)
        font: Theme.labelFont
        color: Theme.textSecondaryColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        visible: summary.lastDialLine.length > 0
        text: summary.lastDialLine
        font: Theme.captionFont
        color: Theme.textColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    // Basket + puck prep last: separate equipment, so keep the grinder identity
    // (title + burrs) and its dial contiguous above them.
    Text {
        Layout.fillWidth: true
        visible: summary.basketLine.length > 0
        textFormat: Text.RichText
        text: Theme.replaceEmojiWithImg(summary.basketLine, Theme.labelFont.pixelSize)
        font: Theme.labelFont
        color: Theme.textSecondaryColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        visible: summary.puckPrepLine.length > 0
        text: summary.puckPrepLine
        font: Theme.labelFont
        color: Theme.textSecondaryColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
