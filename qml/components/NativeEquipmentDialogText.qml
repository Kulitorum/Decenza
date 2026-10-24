import QtQml
import Decenza

EquipmentDialogText {
    pickerTitle: TranslationManager.translate("equipment.dialog.switchTitle", "Switch Equipment")
    addNew: TranslationManager.translate("equipment.dialog.addNew", "Add New Equipment")
    cancel: TranslationManager.translate("common.button.cancel", "Cancel")
    close: TranslationManager.translate("common.button.close", "Close")
    details: TranslationManager.translate("equipment.info.button", "Equipment details")
    createTitle: TranslationManager.translate("equipment.dialog.addTitle", "Add Equipment")
    editTitle: TranslationManager.translate("equipment.dialog.editTitle", "Edit Equipment")
    save: TranslationManager.translate("common.button.save", "Save")
    duplicate: TranslationManager.translate("equipment.dialog.duplicate",
        "You already have this exact equipment — change something to make it distinct.")
    saveFailed: TranslationManager.translate("equipment.dialog.saveFailed",
        "Couldn't save this equipment — please try again.")
    equipmentChanged: TranslationManager.translate("equipment.dialog.changed",
        "Equipment changed since it was loaded. Reload and try again.")
    nameInUse: TranslationManager.translate("equipment.dialog.nameInUse",
        "That name is already in use — choose a different name.")
    nameLabel: TranslationManager.translate("equipment.dialog.name", "Name (optional)")
    brandLabel: TranslationManager.translate("equipment.dialog.brand", "Grinder brand")
    modelLabel: TranslationManager.translate("equipment.dialog.model", "Grinder model")
    burrsLabel: TranslationManager.translate("equipment.dialog.burrs", "Burrs")
    basketBrandLabel: TranslationManager.translate("equipment.dialog.basketBrand", "Basket brand (optional)")
    basketModelLabel: TranslationManager.translate("equipment.dialog.basketModel", "Basket model")
    puckPrepLabel: TranslationManager.translate("equipment.dialog.puckPrep", "Puck prep (optional)")
    infoTitle: TranslationManager.translate("equipment.info.title", "Equipment")
    infoGrinder: TranslationManager.translate("equipment.info.grinder", "Grinder")
    infoBurrs: TranslationManager.translate("equipment.info.burrs", "Burrs")
    infoLastGrind: TranslationManager.translate("equipment.info.lastGrind", "Last grind")
    infoBasket: TranslationManager.translate("equipment.info.basket", "Basket")
    infoBasketDetails: TranslationManager.translate("equipment.info.basketDetails", "Basket details")
    infoPuckPrep: TranslationManager.translate("equipment.info.puckPrep", "Puck prep")
    noMatches: TranslationManager.translate("suggestionfield.nomatches", "No matches - press Enter to add")
    noSuggestions: TranslationManager.translate("suggestionfield.nosuggestions", "No suggestions available")
    clearText: TranslationManager.translate("suggestionfield.clear", "Clear text")
    openSuggestions: TranslationManager.translate("suggestionfield.openDropdown", "Open suggestions")
    selected: TranslationManager.translate("combobox.selected", "Selected")
    accessibilitySelected: TranslationManager.translate("accessibility.selected", "selected")
    none: TranslationManager.translate("combobox.empty", "None")
    cancelSelection: TranslationManager.translate("combobox.cancel", "Cancel")
    addLabel: TranslationManager.translate("common.button.add", "Add")
    switchFallbackLabel: TranslationManager.translate("switch.accessibility.toggle", "Toggle")
    inventoryTitle: TranslationManager.translate("equipment.inventory.title", "Equipment")
    inventoryEmptyTitle: TranslationManager.translate("equipment.inventory.empty.title", "No equipment yet")
    inventoryEmptyHint: TranslationManager.translate("equipment.inventory.empty.hint",
        "Add your grinder to track its model, burrs and dial-in per bag.")
    inventoryAddButton: TranslationManager.translate("equipment.inventory.addButton", "Equipment")
    inventoryAccessibleAdd: TranslationManager.translate("equipment.inventory.accessible.add",
        "Add a new equipment package")
    cardRemove: TranslationManager.translate("equipment.card.remove", "Remove")
    cardRemoveAccessibleName: TranslationManager.translate("equipment.card.accessible.remove",
        "Remove this equipment from inventory; history is kept")
    cardEditAccessibleName: TranslationManager.translate("equipment.card.accessible.edit",
        "Edit equipment details")
    cardDeleteAccessibleName: TranslationManager.translate("equipment.card.accessible.delete",
        "Delete equipment")
    cardDeleteAccessibleDescription: TranslationManager.translate("equipment.card.accessible.deleteHint",
        "Deletes this unused equipment package entirely")
    cardDeleteRefused: TranslationManager.translate("equipment.card.deleteRefused",
        "This package is used by bags or shots — use Remove instead")
    cardBasketTemplate: TranslationManager.translate("equipment.card.basket", "Basket: %1")
    cardPrepTemplate: TranslationManager.translate("equipment.card.puckPrep", "Prep: %1")
    cardLastGrindTemplate: TranslationManager.translate("equipment.card.lastGrind", "Grind %1")
    cardLastRpmTemplate: TranslationManager.translate("equipment.card.lastRpm", "%1 rpm")
    puckPrepLabels: PuckPrepLabels.labelsByKey
}
