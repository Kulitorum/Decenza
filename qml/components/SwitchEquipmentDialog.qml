import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Create / edit an equipment package (add-equipment-packages). v1 covers the
// grinder component: brand / model / burrs with registry-backed suggestions
// (the same sources the old Brew Settings grinder fields used). rpmCapable is
// derived from the registry in storage, not entered here. Picking an existing
// package to switch the active bag is handled inline on the Equipment page /
// Brew Settings; this dialog is the add/edit form.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(520), parent ? parent.width * 0.95 : Theme.scaled(520))
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    // "create" -> requestCreatePackage; "edit" -> requestUpdatePackage
    property string formMode: "create"
    property int editPackageId: -1

    property string fBrand: ""
    property string fModel: ""
    property string fBurrs: ""

    signal packageSaved(int packageId)

    function openForCreate() {
        formMode = "create"
        editPackageId = -1
        fBrand = ""
        fModel = ""
        fBurrs = ""
        open()
    }

    function openForEdit(pkg) {
        formMode = "edit"
        editPackageId = pkg && pkg.id !== undefined ? pkg.id : -1
        fBrand = (pkg && pkg.grinderBrand) || ""
        fModel = (pkg && pkg.grinderModel) || ""
        fBurrs = (pkg && pkg.grinderBurrs) || ""
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

    function modelSuggestions() {
        return Settings.dye.knownGrinderModels(root.fBrand)
    }

    function burrsSuggestions() {
        if (!Settings.dye.isBurrSwappable(root.fBrand, root.fModel))
            return []
        return Settings.dye.suggestedBurrs(root.fBrand, root.fModel)
    }

    readonly property bool canSave: fBrand.trim().length > 0 || fModel.trim().length > 0

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: KeyboardAwareContainer {
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
                    root.fModel = t
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
            // packageCreated carries the new id; surface it then close.
            _awaitingCreate = true
        }
    }

    property bool _awaitingCreate: false

    Connections {
        target: MainController.equipmentStorage
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
}
