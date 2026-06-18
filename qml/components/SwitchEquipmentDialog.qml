import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Switch / create / edit an equipment package (add-equipment-packages). Two
// modes, mirroring ChangeBeansDialog:
//   "list" -> pick an existing package (tap switches the active bag to it) or
//             add a new one
//   "form" -> create or edit a grinder package (brand/model/burrs with
//             registry-backed suggestions; rpmCapable is derived in storage)
// Opening with open() shows the picker; openForCreate()/openForEdit() jump
// straight to the form.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(520), parent ? parent.width * 0.95 : Theme.scaled(520))
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    property string mode: "list"          // "list" | "form"
    property string formMode: "create"    // "create" | "edit"
    property int editPackageId: -1

    property var packages: []
    property string fBrand: ""
    property string fModel: ""
    property string fBurrs: ""

    signal packageSaved(int packageId)

    onAboutToShow: if (mode === "list") MainController.equipmentStorage.requestInventory()

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(list) { root.packages = list }
        function onPackagesChanged() {
            if (root.visible && root.mode === "list")
                MainController.equipmentStorage.requestInventory()
        }
        function onPackageCreated(packageId, pkg) {
            if (!root._awaitingCreate) return
            root._awaitingCreate = false
            if (packageId > 0) {
                // A freshly added package becomes the active equipment.
                Settings.dye.switchToEquipment(pkg)
                root.packageSaved(packageId)
            }
            root.close()
        }
    }

    // Open the picker (list of packages). onAboutToShow requests the inventory.
    function openPicker() {
        mode = "list"
        open()
    }

    function openForCreate() {
        formMode = "create"
        editPackageId = -1
        fBrand = ""; fModel = ""; fBurrs = ""
        mode = "form"
        open()
    }

    function openForEdit(pkg) {
        formMode = "edit"
        editPackageId = pkg && pkg.id !== undefined ? pkg.id : -1
        fBrand = (pkg && pkg.grinderBrand) || ""
        fModel = (pkg && pkg.grinderModel) || ""
        fBurrs = (pkg && pkg.grinderBurrs) || ""
        mode = "form"
        open()
    }

    function brandSuggestions() {
        var known = Settings.dye.knownGrinderBrands()
        var history = MainController.shotHistory ? MainController.shotHistory.getDistinctGrinderBrands() : []
        var seen = {}, out = []
        for (var i = 0; i < known.length; ++i) { if (!seen[known[i]]) { seen[known[i]] = true; out.push(known[i]) } }
        for (var j = 0; j < history.length; ++j) { if (history[j] && !seen[history[j]]) { seen[history[j]] = true; out.push(history[j]) } }
        return out
    }
    function modelSuggestions() { return Settings.dye.knownGrinderModels(root.fBrand) }
    function burrsSuggestions() {
        if (!Settings.dye.isBurrSwappable(root.fBrand, root.fModel)) return []
        return Settings.dye.suggestedBurrs(root.fBrand, root.fModel)
    }

    function packageTitle(pkg) {
        if (pkg && pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""].filter(function(s) { return s.length > 0 }).join(" ")
    }

    readonly property bool canSave: fBrand.trim().length > 0 || fModel.trim().length > 0
    property bool _awaitingCreate: false

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: Item {
        implicitHeight: (root.mode === "list" ? listColumn.implicitHeight : formContainer.implicitHeight)
                        + 2 * Theme.spacingMedium

        // --- LIST (picker) ---
        ColumnLayout {
            id: listColumn
            visible: root.mode === "list"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium

            Tr {
                Layout.fillWidth: true
                key: "equipment.dialog.switchTitle"
                fallback: "Switch Equipment"
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Repeater {
                model: root.packages
                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    primary: modelData.id === Settings.dye.activeEquipmentId
                    text: root.packageTitle(modelData)
                          + (modelData.grinderBurrs && String(modelData.grinderBurrs).length > 0
                             ? " · " + modelData.grinderBurrs : "")
                    accessibleName: text + (modelData.id === Settings.dye.activeEquipmentId
                                            ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                    onClicked: {
                        Settings.dye.switchToEquipment(modelData)
                        root.packageSaved(modelData.id)
                        root.close()
                    }
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)
                icon.source: "qrc:/icons/plus.svg"
                text: TranslationManager.translate("equipment.dialog.addNew", "Add New Equipment")
                accessibleName: text
                onClicked: root.openForCreate()
            }

            AccessibleButton {
                Layout.alignment: Qt.AlignRight
                text: TranslationManager.translate("common.button.cancel", "Cancel")
                accessibleName: text
                onClicked: root.close()
            }
        }

        // --- FORM (create / edit) ---
        KeyboardAwareContainer {
            id: formContainer
            visible: root.mode === "form"
            anchors.fill: parent
            textFields: [brandField.textField, modelField.textField, burrsField.textField]

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Tr {
                    Layout.fillWidth: true
                    key: root.formMode === "edit" ? "equipment.dialog.editTitle" : "equipment.dialog.addTitle"
                    fallback: root.formMode === "edit" ? "Edit Equipment" : "Add Equipment"
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                SuggestionField {
                    id: brandField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("equipment.dialog.brand", "Grinder brand")
                    accessibleName: label
                    text: root.fBrand
                    suggestions: root.brandSuggestions()
                    onTextEdited: function(t) { root.fBrand = t }
                    onSuggestionSelected: function(t) {
                        root.fBrand = t
                        var models = Settings.dye.knownGrinderModels(t)
                        if (models.length === 1) {
                            root.fModel = models[0]
                            var burrs = Settings.dye.suggestedBurrs(t, models[0])
                            if (burrs.length === 1) root.fBurrs = burrs[0]
                        }
                    }
                }

                SuggestionField {
                    id: modelField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("equipment.dialog.model", "Grinder model")
                    accessibleName: label
                    text: root.fModel
                    suggestions: root.modelSuggestions()
                    onTextEdited: function(t) { root.fModel = t }
                    onSuggestionSelected: function(t) {
                        var burrs = Settings.dye.suggestedBurrs(root.fBrand, t)
                        if (burrs.length === 1) root.fBurrs = burrs[0]
                    }
                }

                SuggestionField {
                    id: burrsField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("equipment.dialog.burrs", "Burrs")
                    accessibleName: label
                    text: root.fBurrs
                    suggestions: root.burrsSuggestions()
                    onTextEdited: function(t) { root.fBurrs = t }
                    onSuggestionSelected: function(t) { root.fBurrs = t }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingSmall
                    spacing: Theme.spacingMedium

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        accessibleName: text
                        onClicked: root.close()
                    }

                    AccessibleButton {
                        primary: true
                        enabled: root.canSave
                        text: TranslationManager.translate("common.button.save", "Save")
                        accessibleName: text
                        onClicked: root.save()
                    }
                }
            }
        }
    }

    function save() {
        Qt.inputMethod.commit()
        var fields = {
            "grinderBrand": fBrand.trim(),
            "grinderModel": fModel.trim(),
            "grinderBurrs": fBurrs.trim()
        }
        if (formMode === "edit" && editPackageId > 0) {
            MainController.equipmentStorage.requestUpdatePackage(editPackageId, fields)
            packageSaved(editPackageId)
            close()
        } else {
            MainController.equipmentStorage.requestCreatePackage(fields)
            _awaitingCreate = true
        }
    }
}
