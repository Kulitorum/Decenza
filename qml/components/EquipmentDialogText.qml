import QtQml

// Canonical labels for the shared equipment inventory and dialog. Native and
// browser targets use the same translation adapter to replace these defaults.
QtObject {
    property string pickerTitle: "Switch Equipment"
    property string addNew: "Add New Equipment"
    property string cancel: "Cancel"
    property string close: "Close"
    property string details: "Equipment details"
    property string clearText: "Clear text"
    property string openSuggestions: "Open suggestions"
    property string createTitle: "Add Equipment"
    property string editTitle: "Edit Equipment"
    property string save: "Save"
    property string duplicate: "You already have this exact equipment — change something to make it distinct."
    property string saveFailed: "Couldn't save this equipment — please try again."
    property string equipmentChanged: "Equipment changed since it was loaded. Reload and try again."
    property string nameInUse: "That name is already in use — choose a different name."
    property string nameLabel: "Name (optional)"
    property string brandLabel: "Grinder brand"
    property string modelLabel: "Grinder model"
    property string burrsLabel: "Burrs"
    property string basketBrandLabel: "Basket brand (optional)"
    property string basketModelLabel: "Basket model"
    property string puckPrepLabel: "Puck prep (optional)"
    property string infoTitle: "Equipment"
    property string infoGrinder: "Grinder"
    property string infoBurrs: "Burrs"
    property string infoLastGrind: "Last grind"
    property string infoBasket: "Basket"
    property string infoBasketDetails: "Basket details"
    property string infoPuckPrep: "Puck prep"
    property string noMatches: "No matches - press Enter to add"
    property string noSuggestions: "No suggestions available"
    property string selected: "Selected"
    property string accessibilitySelected: "selected"
    property string none: "None"
    property string cancelSelection: "Cancel"
    property string addLabel: "Add"
    property string switchFallbackLabel: "Toggle"

    property string inventoryTitle: "Equipment"
    property string inventoryEmptyTitle: "No equipment yet"
    property string inventoryEmptyHint: "Add your grinder to track its model, burrs and dial-in per bag."
    property string inventoryAddButton: "Equipment"
    property string inventoryAccessibleAdd: "Add a new equipment package"
    property string cardRemove: "Remove"
    property string cardRemoveAccessibleName: "Remove this equipment from inventory; history is kept"
    property string cardEditAccessibleName: "Edit equipment details"
    property string cardDeleteAccessibleName: "Delete equipment"
    property string cardDeleteAccessibleDescription: "Deletes this unused equipment package entirely"
    property string cardDeleteRefused: "This package is used by bags or shots — use Remove instead"
    property string cardBasketTemplate: "Basket: %1"
    property string cardPrepTemplate: "Prep: %1"
    property string cardLastGrindTemplate: "Grind %1"
    property string cardLastRpmTemplate: "%1 rpm"
    property var puckPrepLabels: ({
        wdt: "WDT",
        shaker: "Shaker",
        puckScreen: "Puck screen",
        paperFilter: "Bottom paper filter",
        rdt: "RDT (spritz)"
    })
}
