pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// Shared service adapter. The complete card, including every action button,
// is implemented once in EquipmentCardView and also compiled for Wasm.
EquipmentCardView {
    id: card

    cardStyle: EquipmentCardStyle {
        bodyFont: Theme.bodyFont
        labelFont: Theme.labelFont
        captionFont: Theme.captionFont
        subtitleFont: Theme.subtitleFont
        textColor: Theme.textColor
        textSecondaryColor: Theme.textSecondaryColor
        warningColor: Theme.warningColor
        backgroundColor: Theme.cardBackgroundColor
        borderColor: Theme.borderColor
        selectedBorderColor: Theme.primaryColor
        cardWidth: Theme.scaled(360)
        cardRadius: Theme.cardRadius
        outerVerticalPadding: Theme.scaled(24)
        contentPadding: Theme.scaled(12)
        contentSpacing: Theme.scaled(6)
        summaryLineSpacing: Theme.scaled(2)
        actionSize: Theme.scaled(36)
        removeHorizontalPadding: Theme.scaled(10)
    }
    buttonStyle: NativeButtonStyle {}
    interaction: NativeButtonInteraction {}
    puckPrepLabels: PuckPrepLabels.labelsFor(pkg.puckPrepCanonical || "")
    selected: !archivedCard && pkg.id !== undefined && pkg.id === EquipmentService.activePackageId
    richTextFor: function(value, size) { return Theme.replaceEmojiWithImg(value, size) }

    property int _serviceRequestSequence: 0

    function nextServiceRequestId(action: string): string {
        _serviceRequestSequence += 1
        return "equipment-card-" + String(pkg.id) + "-" + action + "-" + _serviceRequestSequence
    }

    function translated(key: string, fallback: string): string {
        return TranslationManager.translate(key, fallback)
    }
    basketTemplate: translated("equipment.card.basket", "Basket: %1")
    puckPrepTemplate: translated("equipment.card.puckPrep", "Prep: %1")
    selectedText: translated("accessibility.selected", "selected")
    removeText: translated("equipment.card.archive", "Archive")
    restoreText: translated("equipment.card.restore", "Restore")
    restoreAccessibleName: translated("equipment.card.accessible.restore", "Restore: put this equipment back in inventory")
    removeAccessibleName: translated("equipment.card.accessible.archive", "Archive this equipment; its history is kept and it can be restored")
    editAccessibleName: translated("equipment.card.accessible.edit", "Edit equipment details")
    deleteAccessibleName: translated("equipment.card.accessible.delete", "Delete equipment")
    deleteAccessibleDescription: translated("equipment.card.accessible.deleteHint", "Deletes this unused equipment package entirely")
    noticeText: translated("equipment.card.deleteRefused", "This package is used by bags or shots — use Archive instead")

    onActivated: {
        if (pkg.id !== undefined)
            EquipmentService.activatePackage(pkg.id, true, nextServiceRequestId("activate"))
    }
    calibrationComponent: Component {
        GrindCalibrationSection { packageId: card.pkg.id > 0 ? card.pkg.id : -1 }
    }

    onRestoreRequested: function(p) { EquipmentService.requestRestorePackage(p.id) }
    onRemoveRequested: function(p) { EquipmentService.requestMarkRemoved(p.id) }
    onDeleteRequested: function(p) { EquipmentService.requestDeletePackage(p.id) }

    Connections {
        target: EquipmentService
        function onPackageDeleted(packageId, success) {
            if (packageId !== card.pkg.id || success)
                return
            card.showDeleteRefused()
        }
    }
}
