pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one equipment picker/editor presentation shared by native and browser
// hosts. All persistence and registry access crosses the typed backend boundary.
SharedDecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    required property EquipmentUiBackend backend
    required property EquipmentDialogStyle equipmentStyle
    required property EquipmentDialogText strings
    width: Math.min(equipmentStyle.dialogStyle.scaled(720), parent ? parent.width * 0.95 : equipmentStyle.dialogStyle.scaled(720))
    modal: true
    closePolicy: saving ? Dialog.NoAutoClose : Dialog.CloseOnEscape
    browserBackDismissible: !saving
    browserUnloadDirty: visible && mode === "form"
        && (saving || JSON.stringify(currentEditableSnapshot()) !== JSON.stringify(_openedEditable))
    padding: 0
    dialogStyle: equipmentStyle.dialogStyle

    property string mode: "list"          // "list" | "form"
    property string formMode: "create"    // "create" | "edit"
    property var editPackageId: -1
    property var expectedEditable: ({})
    property var _openedEditable: ({})
    readonly property var packages: backend.packages
    readonly property var puckPrepRows: backend.puckPrepRows
    property string saveError: ""
    property string listError: ""
    property string fName: ""
    property string fBrand: ""
    property string fModel: ""
    property string fBurrs: ""
    property string fBasketBrand: ""
    property string fBasketModel: ""
    property var fPuck: ({})
    property bool applyToActiveBag: true
    property var _originalName: ""
    property int _requestSequence: 0
    property string _pendingRequestId: ""
    property string _pendingAction: ""
    property bool saving: false
    property var infoPackage: null

    onAboutToShow: backend.refresh()

    Connections {
        target: root.backend
        function onErrorMessageChanged() {
            if (!root.backend.errorMessage) {
                root.listError = ""
                root.saveError = ""
            } else if (root.mode === "list") {
                root.listError = root.backend.errorMessage
            } else {
                root.saveError = root.backend.errorMessage
            }
        }
        function onPackagesInvalidated() {
            if (root.visible) root.backend.refresh()
        }
        function onOperationCompleted(requestId, success, resultId, reason) {
            if (requestId !== root._pendingRequestId) return
            root.finishOperation(success, resultId, reason)
        }
    }

    function nextRequestId() {
        _requestSequence += 1
        return "equipment-dialog-" + _requestSequence
    }
    function puckFromCanonical(canonical) {
        var keys = String(canonical || "").split(",")
        var result = {}
        for (let i = 0; i < puckPrepRows.length; ++i)
            result[puckPrepRows[i].key] = keys.indexOf(puckPrepRows[i].key) >= 0
        return result
    }
    function setPuck(key, on) {
        var result = {}
        for (let keyName in fPuck) result[keyName] = fPuck[keyName]
        result[key] = on
        fPuck = result
    }
    function puckCanonical() {
        var enabled = []
        for (let i = 0; i < puckPrepRows.length; ++i)
            if (fPuck[puckPrepRows[i].key]) enabled.push(puckPrepRows[i].key)
        enabled.sort()
        return enabled.join(",")
    }
    function editableSnapshot(pkg) {
        function raw(key) {
            return pkg && pkg[key] !== undefined && pkg[key] !== null ? String(pkg[key]) : ""
        }
        var canonical = raw("puckPrepCanonical")
        if (pkg && pkg.puckPrepCanonical === undefined) {
            var enabled = []
            for (let i = 0; i < puckPrepRows.length; ++i)
                if (pkg["puckPrep_" + puckPrepRows[i].key]) enabled.push(puckPrepRows[i].key)
            enabled.sort()
            canonical = enabled.join(",")
        }
        return {
            name: raw("name"),
            grinderBrand: raw("grinderBrand"),
            grinderModel: raw("grinderModel"),
            grinderBurrs: raw("grinderBurrs"),
            basketBrand: raw("basketBrand"),
            basketModel: raw("basketModel"),
            puckPrepCanonical: canonical
        }
    }
    function openPicker() {
        if (saving) return
        mode = "list"
        listError = ""
        open()
    }
    function openForCreate() {
        if (saving) return
        var seed = backend.createSeed || ({})
        formMode = "create"
        editPackageId = -1
        expectedEditable = ({})
        fName = ""
        _originalName = ""
        saveError = backend.errorMessage || ""
        fBrand = seed.grinderBrand || ""
        fModel = seed.grinderModel || ""
        fBurrs = seed.grinderBurrs || ""
        fBasketBrand = seed.basketBrand || ""
        fBasketModel = seed.basketModel || ""
        fPuck = puckFromCanonical(seed.puckPrepCanonical || "")
        mode = "form"
        _openedEditable = currentEditableSnapshot()
        open()
    }
    function openForEdit(pkg) {
        if (saving) return
        formMode = "edit"
        editPackageId = pkg && pkg.id !== undefined ? pkg.id : -1
        expectedEditable = editableSnapshot(pkg)
        fName = expectedEditable.name
        _originalName = expectedEditable.name
        saveError = backend.errorMessage || ""
        fBrand = expectedEditable.grinderBrand
        fModel = expectedEditable.grinderModel
        fBurrs = expectedEditable.grinderBurrs
        fBasketBrand = expectedEditable.basketBrand
        fBasketModel = expectedEditable.basketModel
        fPuck = puckFromCanonical(expectedEditable.puckPrepCanonical)
        mode = "form"
        _openedEditable = currentEditableSnapshot()
        open()
    }
    function currentEditableSnapshot() {
        return editableSnapshot({
            name: fName,
            grinderBrand: fBrand,
            grinderModel: fModel,
            grinderBurrs: fBurrs,
            basketBrand: fBasketBrand,
            basketModel: fBasketModel,
            puckPrepCanonical: puckCanonical()
        })
    }
    function packageTitle(pkg) {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""]
                .filter(function(value) { return value.length > 0 }).join(" ")
    }
    function normalizedName(value) { return String(value || "").trim().toLowerCase() }
    function nameFor(pkg) { return String(pkg && pkg.name || "") }

    function startActivation(packageId, action) {
        if (!applyToActiveBag) {
            completeSave(packageId)
            return
        }
        _pendingAction = action
        _pendingRequestId = nextRequestId()
        saving = true
        backend.activatePackage(packageId, true, _pendingRequestId)
    }
    function selectPackage(pkg) {
        if (saving || backend.loading || backend.errorMessage.length > 0
            || !pkg || pkg.id === undefined) return
        listError = ""
        if (applyToActiveBag) startActivation(pkg.id, "picker-activation")
        else completeSave(pkg.id)
    }
    function completeSave(packageId) {
        saving = false
        _pendingAction = ""
        _pendingRequestId = ""
        packageSaved(packageId)
        close()
    }
    function operationError(reason) {
        if (reason === "nameInUse" || reason === "duplicateName") return strings.nameInUse
        if (reason === "conflict") return strings.equipmentChanged
        return strings.saveFailed
    }
    function finishOperation(success, resultId, reason) {
        var action = _pendingAction
        if (action === "picker-activation") {
            if (success) completeSave(resultId)
            else { saving = false; _pendingRequestId = ""; _pendingAction = ""; listError = operationError(reason) }
            return
        }
        saving = false
        if (!success || !(resultId > 0)) {
            _pendingRequestId = ""
            _pendingAction = ""
            saveError = operationError(reason)
            return
        }
        completeSave(resultId)
    }

    function brandSuggestions() { return backend.grinderBrandSuggestions() }
    function modelSuggestions() { return backend.grinderModelSuggestions(fBrand) }
    function burrsSuggestions() { return backend.burrsSuggestions(fBrand, fModel) }
    function basketBrandSuggestions() { return backend.basketBrandSuggestions() }
    function basketModelSuggestions() { return backend.basketModelSuggestions(fBasketBrand) }
    function basketModelDescriptions() { return backend.basketModelDescriptions(fBasketBrand) }
    function puckPrepSummary(canonical) {
        var keys = String(canonical || "").split(","), labels = []
        for (let i = 0; i < puckPrepRows.length; ++i)
            if (keys.indexOf(puckPrepRows[i].key) >= 0) labels.push(puckPrepRows[i].label)
        return labels.join(" · ")
    }
    function showInfo(pkg) {
        infoPackage = pkg
        infoDialog.open()
    }

    readonly property var duplicateOfId: {
        var gb = fBrand.trim().toLowerCase(), gm = fModel.trim().toLowerCase()
        var gbu = fBurrs.trim().toLowerCase()
        var bb = fBasketBrand.trim().toLowerCase(), bm = fBasketModel.trim().toLowerCase()
        var pc = puckCanonical()
        for (let i = 0; i < packages.length; ++i) {
            let p = packages[i]
            if (!p || p.id === undefined) continue
            if (formMode === "edit" && p.id === editPackageId) continue
            if (String(p.grinderBrand || "").trim().toLowerCase() === gb
                && String(p.grinderModel || "").trim().toLowerCase() === gm
                && String(p.grinderBurrs || "").trim().toLowerCase() === gbu
                && String(p.basketBrand || "").trim().toLowerCase() === bb
                && String(p.basketModel || "").trim().toLowerCase() === bm
                && String(p.puckPrepCanonical || "") === pc)
                return p.id
        }
        return -1
    }
    readonly property var nameDuplicateOfId: {
        var n = normalizedName(fName)
        if (n.length === 0 || n === normalizedName(_originalName)) return -1
        for (let i = 0; i < packages.length; ++i) {
            let p = packages[i]
            if (!p || p.id === undefined) continue
            if (formMode === "edit" && p.id === editPackageId) continue
            if (normalizedName(nameFor(p)) === n) return p.id
        }
        return -1
    }
    readonly property bool canSave: (fBrand.trim().length > 0 || fModel.trim().length > 0
        || fBasketBrand.trim().length > 0 || fBasketModel.trim().length > 0)
        && duplicateOfId < 0 && nameDuplicateOfId < 0 && !backend.loading
        && backend.errorMessage.length === 0

    signal packageSaved(int packageId)

    EquipmentInfoDialogView {
        id: infoDialog
        equipmentStyle: root.equipmentStyle
        strings: root.strings
        packageData: root.infoPackage || ({})
        puckPrepRows: root.puckPrepRows
    }

    background: Rectangle {
        color: root.equipmentStyle.dialogStyle.surfaceColor
        radius: root.equipmentStyle.cardRadius
        border.color: root.equipmentStyle.dialogStyle.borderColor
        border.width: 1
    }

    contentItem: Item {
        id: contentRoot
        // Size to the active mode's inner ColumnLayout. NOTE: reference
        // formColumn (the layout), not formContainer (the KeyboardAwareContainer)
        // — the container's child fills it via anchors, so the container's own
        // implicitHeight is 0 and the dialog would collapse to just the header.
        //
        // Form mode is CAPPED at a fraction of the screen and the fields scroll
        // inside (a grinder+basket package has enough fields to overflow a short
        // screen); the pinned header keeps Cancel/Save reachable without scrolling.
        readonly property real formCap: (root.parent ? root.parent.height : root.equipmentStyle.dialogStyle.scaled(640)) * 0.85
        readonly property real formNatural: formHeader.implicitHeight + fieldsColumn.implicitHeight
                                            + 3 * root.equipmentStyle.spacingMedium
        implicitHeight: root.mode === "list"
                        ? listColumn.implicitHeight + 2 * root.equipmentStyle.spacingMedium
                        : Math.min(formNatural, formCap)

        // --- LIST (picker) ---
        ColumnLayout {
            id: listColumn
            visible: root.mode === "list"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: root.equipmentStyle.spacingMedium
            spacing: root.equipmentStyle.spacingMedium

            Text {
                Layout.fillWidth: true
                text: root.strings.pickerTitle
                font: root.equipmentStyle.titleFont
                color: root.equipmentStyle.dialogStyle.textColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Repeater {
                model: root.packages
                RowLayout {
                    id: pkgRow
                    // Declare modelData required so the Repeater assigns the
                    // QVariantMap element: inside this Dialog-content delegate the
                    // implicit context modelData/index did NOT resolve (rows
                    // rendered blank; index threw ReferenceError). A required
                    // property is bound by the view, so pkg gets the real package.
                    required property var modelData
                    readonly property var pkg: modelData || ({})
                    Layout.fillWidth: true
                    spacing: root.equipmentStyle.spacingSmall

                    // Select-this-package row — shows the package NAME only
                    // (burrs/grinder identity live in the info popup).
                    SharedEquipmentDialogButton {
                        equipmentStyle: root.equipmentStyle
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.equipmentStyle.dialogStyle.scaled(44)
                        primary: !!pkgRow.modelData && pkgRow.pkg.id === root.backend.activePackageId
                        text: root.packageTitle(pkgRow.pkg)
                        accessibleName: text + (primary
                                                ? ", " + root.strings.accessibilitySelected : "")
                        enabled: !root.backend.loading && root.backend.errorMessage.length === 0
                        onClicked: root.selectPackage(pkgRow.modelData)
                    }

                    // Info: show this package's full contents.
                    SharedEquipmentDialogButton {
                        equipmentStyle: root.equipmentStyle
                        Layout.preferredHeight: root.equipmentStyle.dialogStyle.scaled(44)
                        visible: !!pkgRow.modelData && pkgRow.pkg.id > 0
                        icon.source: "qrc:/icons/info.svg"
                        accessibleName: root.strings.details
                        onClicked: root.showInfo(pkgRow.pkg)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.listError.length > 0
                text: root.listError
                font: root.equipmentStyle.dialogStyle.bodyFont
                color: root.equipmentStyle.errorColor
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            SharedEquipmentDialogButton {
                equipmentStyle: root.equipmentStyle
                Layout.fillWidth: true
                Layout.preferredHeight: root.equipmentStyle.dialogStyle.scaled(44)
                icon.source: "qrc:/icons/plus.svg"
                text: root.strings.addNew
                accessibleName: text
                enabled: !root.backend.loading && root.backend.errorMessage.length === 0
                onClicked: root.openForCreate()
            }

            SharedEquipmentDialogButton {
                equipmentStyle: root.equipmentStyle
                Layout.alignment: Qt.AlignRight
                text: root.strings.cancel
                enabled: !root.saving
                accessibleName: text
                onClicked: root.close()
            }
        }

        // --- FORM (create / edit) ---
        SharedKeyboardAwareContainer {
            id: formContainer
            keyboardInteraction: root.equipmentStyle.keyboardInteraction
            visible: root.mode === "form"
            anchors.fill: parent
            textFields: [nameField.textField, brandField.textField, modelField.textField, burrsField.textField,
                         basketBrandField.textField, basketModelField.textField]
            readonly property bool hasPreedit: textFields.some(function(field) {
                return field.preeditText.length > 0
            })

            ColumnLayout {
                id: formColumn
                anchors.fill: parent
                anchors.margins: root.equipmentStyle.spacingMedium
                spacing: root.equipmentStyle.spacingMedium

                // The same responsive title/actions layout as the inventory page.
                SharedSectionHeader {
                    id: formHeader
                    Layout.fillWidth: true
                    title: root.formMode === "edit"
                           ? root.strings.editTitle : root.strings.createTitle
                    titleFont: root.equipmentStyle.titleFont
                    titleColor: root.equipmentStyle.dialogStyle.textColor
                    spacing: root.equipmentStyle.spacingMedium

                    SharedEquipmentDialogButton {
                        equipmentStyle: root.equipmentStyle
                        text: root.strings.cancel
                        enabled: !root.saving
                        accessibleName: text
                        onClicked: root.close()
                    }

                    SharedEquipmentDialogButton {
                        equipmentStyle: root.equipmentStyle
                        primary: true
                        // A composing word can supply the first gear value or
                        // resolve a duplicate. Validate it after committing.
                        enabled: !root.saving && !root.backend.loading
                            && root.backend.errorMessage.length === 0
                            && (root.canSave || formContainer.hasPreedit)
                        text: root.strings.save
                        accessibleName: text
                        onClicked: root.save()
                    }
                }

                // Explains the disabled Save when the entered gear already exists
                // (e.g. the create form was pre-filled and nothing was changed yet).
                Text {
                    Layout.fillWidth: true
                    visible: root.duplicateOfId > 0
                    text: root.strings.duplicate
                    font: root.equipmentStyle.captionFont
                    color: root.equipmentStyle.warningColor
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                // A save REFUSED by storage. Pinned with the header so it can't be
                // scrolled out of sight, and the dialog stays open behind it so the
                // user's input is preserved.
                Text {
                    Layout.fillWidth: true
                    visible: root.saveError !== ""
                    text: root.saveError
                    font: root.equipmentStyle.dialogStyle.bodyFont
                    color: root.equipmentStyle.errorColor
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                // Scrollable field area — a grinder+basket package has enough fields
                // to overflow a short screen.
                ScrollView {
                    id: formScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        id: fieldsColumn
                        width: formScroll.availableWidth
                        spacing: root.equipmentStyle.spacingMedium

                        // User-editable label (add-equipment-packages 4b.1). Blank uses the
                        // derived "{brand} {model}"; set it to a kit name like "Espresso setup".
                        SharedEquipmentSuggestionField {
                            id: nameField
                            equipmentStyle: root.equipmentStyle
                            strings: root.strings
                            Layout.fillWidth: true
                            label: root.strings.nameLabel
                            accessibleName: label
                            text: root.fName
                            suggestions: []
                            onTextEdited: function(t) { root.fName = t }
                        }

                        // Blocks Save when the entered name already belongs to another
                        // in-inventory package (block-duplicate-active-names).
                        Text {
                            Layout.fillWidth: true
                            visible: root.nameDuplicateOfId > 0
                            text: root.strings.nameInUse
                            font: root.equipmentStyle.captionFont
                            color: root.equipmentStyle.warningColor
                            wrapMode: Text.Wrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // Related fields share a row where space permits; narrow forms
                        // keep each label and input at full width.
                        GridLayout {
                            objectName: "sharedEquipmentGrinderFields"
                            Layout.fillWidth: true
                            columns: fieldsColumn.width >= root.equipmentStyle.dialogStyle.scaled(480) ? 2 : 1
                            columnSpacing: root.equipmentStyle.spacingMedium
                            rowSpacing: root.equipmentStyle.spacingMedium

                            SharedEquipmentSuggestionField {
                                id: brandField
                                equipmentStyle: root.equipmentStyle
                                strings: root.strings
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: root.strings.brandLabel
                                accessibleName: label
                                text: root.fBrand
                                suggestions: root.brandSuggestions()
                                onTextEdited: function(t) { root.fBrand = t }
                                onSuggestionSelected: function(t) {
                                    root.fBrand = t
                                    var models = root.backend.grinderModelSuggestions(t)
                                    if (models.length === 1) {
                                        root.fModel = models[0]
                                        let burrs = root.backend.burrsSuggestions(t, models[0])
                                        if (burrs.length === 1) root.fBurrs = burrs[0]
                                    }
                                }
                            }

                            SharedEquipmentSuggestionField {
                                id: modelField
                                equipmentStyle: root.equipmentStyle
                                strings: root.strings
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: root.strings.modelLabel
                                accessibleName: label
                                text: root.fModel
                                suggestions: root.modelSuggestions()
                                onTextEdited: function(t) { root.fModel = t }
                                onSuggestionSelected: function(t) {
                                    var burrs = root.backend.burrsSuggestions(root.fBrand, t)
                                    if (burrs.length === 1) root.fBurrs = burrs[0]
                                }
                            }
                        }

                        SharedEquipmentSuggestionField {
                            id: burrsField
                            equipmentStyle: root.equipmentStyle
                            strings: root.strings
                            Layout.fillWidth: true
                            label: root.strings.burrsLabel
                            accessibleName: label
                            text: root.fBurrs
                            suggestions: root.burrsSuggestions()
                            onTextEdited: function(t) { root.fBurrs = t }
                            onSuggestionSelected: function(t) { root.fBurrs = t }
                        }

                        // --- Basket (optional) — vendor-first, two-level (add-basket-equipment).
                        //     Brand + model share a row, mirroring the grinder pair above.
                        GridLayout {
                            objectName: "sharedEquipmentBasketFields"
                            Layout.fillWidth: true
                            columns: fieldsColumn.width >= root.equipmentStyle.dialogStyle.scaled(480) ? 2 : 1
                            columnSpacing: root.equipmentStyle.spacingMedium
                            rowSpacing: root.equipmentStyle.spacingMedium

                            SharedEquipmentSuggestionField {
                                id: basketBrandField
                                equipmentStyle: root.equipmentStyle
                                strings: root.strings
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: root.strings.basketBrandLabel
                                accessibleName: label
                                text: root.fBasketBrand
                                suggestions: root.basketBrandSuggestions()
                                onTextEdited: function(t) {
                                    // Changing the brand invalidates the model (it belongs to a brand).
                                    if (t !== root.fBasketBrand) root.fBasketModel = ""
                                    root.fBasketBrand = t
                                }
                                onSuggestionSelected: function(t) {
                                    root.fBasketBrand = t
                                    var models = root.backend.basketModelSuggestions(t)
                                    if (models.length === 1) root.fBasketModel = models[0]
                                }
                            }

                            SharedEquipmentSuggestionField {
                                id: basketModelField
                                equipmentStyle: root.equipmentStyle
                                strings: root.strings
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: root.strings.basketModelLabel
                                accessibleName: label
                                text: root.fBasketModel
                                suggestions: root.basketModelSuggestions()
                                // Differentiator subtitle keeps similar models legible.
                                descriptions: root.basketModelDescriptions()
                                onTextEdited: function(t) { root.fBasketModel = t }
                                onSuggestionSelected: function(t) { root.fBasketModel = t }
                            }
                        }

                        // --- Puck prep (optional) — checkbox flags (add-puckprep-equipment).
                        //     All unchecked = no puck prep. ---
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: root.equipmentStyle.spacingSmall
                            text: root.strings.puckPrepLabel
                            color: root.equipmentStyle.dialogStyle.textColor
                            font.pixelSize: root.equipmentStyle.dialogStyle.scaled(14)
                        }

                        GridLayout {
                            objectName: "sharedEquipmentPrepFields"
                            Layout.fillWidth: true
                            columns: fieldsColumn.width >= root.equipmentStyle.dialogStyle.scaled(480) ? 2 : 1
                            columnSpacing: root.equipmentStyle.spacingMedium
                            rowSpacing: root.equipmentStyle.spacingSmall

                            Repeater {
                                model: root.puckPrepRows
                                SharedStyledSwitch {
                                    switchStyle: root.equipmentStyle.switchStyle
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    accessibleName: modelData.label
                                    checked: !!root.fPuck[modelData.key]
                                    onToggled: root.setPuck(modelData.key, checked)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function save() {
        if (saving) return
        root.equipmentStyle.keyboardInteraction.commitRequested()
        if (!canSave) return
        var fields = {
            // Blank name -> storage derives "{brand} {model}" on create, or clears
            // the custom label (display falls back to brand+model) on edit.
            "name": fName.trim(),
            "grinderBrand": fBrand.trim(),
            "grinderModel": fModel.trim(),
            "grinderBurrs": fBurrs.trim(),
            // Blank basket brand+model -> no basket (storage clears any existing one).
            "basketBrand": fBasketBrand.trim(),
            "basketModel": fBasketModel.trim()
        }
        // Puck-prep flags as namespaced keys (storage builds the canonical form;
        // all-false clears any existing puck prep).
        for (let i = 0; i < puckPrepRows.length; ++i)
            fields["puckPrep_" + puckPrepRows[i].key] = !!fPuck[puckPrepRows[i].key]
        if (formMode === "edit" && editPackageId > 0) {
            _pendingAction = "update"
            _pendingRequestId = nextRequestId()
            saving = true
            backend.updatePackage(editPackageId, fields, expectedEditable, _pendingRequestId)
        } else {
            _pendingAction = "create"
            _pendingRequestId = nextRequestId()
            saving = true
            backend.createPackage(fields, applyToActiveBag, _pendingRequestId)
        }
    }
}
