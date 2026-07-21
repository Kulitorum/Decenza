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
    width: Math.min(Theme.scaled(720), parent ? parent.width * 0.95 : Theme.scaled(720))
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    property string mode: "list"          // "list" | "form"
    property string formMode: "create"    // "create" | "edit"
    property int editPackageId: -1

    property var packages: []
    // Shown in the form when a save is REFUSED by storage. The dialog stays open
    // so the user's input survives and they can see why.
    property string saveError: ""
    property string _editFailReason: ""
    property string fName: ""          // user label; blank -> storage derives "{brand} {model}"
    property string fBrand: ""
    property string fModel: ""
    property string fBurrs: ""
    // Optional basket identity (add-basket-equipment). Blank brand+model = no basket.
    property string fBasketBrand: ""
    property string fBasketModel: ""
    // Optional puck-prep flags (add-puckprep-equipment). All false = no puck prep.
    property var fPuck: ({ wdt: false, shaker: false, puckScreen: false, paperFilter: false, rdt: false })
    // The checkbox rows, in display order (key + label). The KEY SET here must stay
    // in sync with C++ PuckPrep::flagKeys() (core/puckprep.h): puckCanonical() below
    // builds the dedup string from these keys and compares it against the C++-stored
    // canonical, so a key added/renamed in one place must change in both.
    readonly property var puckPrepRows: [
        { key: "wdt",         label: TranslationManager.translate("equipment.dialog.puckWdt", "WDT") },
        { key: "shaker",      label: TranslationManager.translate("equipment.dialog.puckShaker", "Shaker") },
        { key: "puckScreen",  label: TranslationManager.translate("equipment.dialog.puckScreen", "Puck screen") },
        { key: "paperFilter", label: TranslationManager.translate("equipment.dialog.puckPaper", "Bottom paper filter") },
        { key: "rdt",         label: TranslationManager.translate("equipment.dialog.puckRdt", "RDT (spritz)") }
    ]

    // Parse a canonical "shaker,wdt" string into the fPuck flag object.
    function puckFromCanonical(canon) {
        var keys = (canon || "").split(",")
        return {
            wdt: keys.indexOf("wdt") >= 0,
            shaker: keys.indexOf("shaker") >= 0,
            puckScreen: keys.indexOf("puckScreen") >= 0,
            paperFilter: keys.indexOf("paperFilter") >= 0,
            rdt: keys.indexOf("rdt") >= 0
        }
    }
    // Set one puck flag (QML can't mutate a single key of a var object in place).
    function setPuck(key, on) {
        var p = {}
        for (var k in root.fPuck) p[k] = root.fPuck[k]
        p[key] = on
        root.fPuck = p
    }

    // When true (default, the Brew Settings / Equipment-window use), selecting or
    // creating a package switches the ACTIVE bag's equipment. When false, the
    // dialog is a pure picker for a specific shot/bag: it does NOT touch the
    // active selection — the caller applies the result via packageSaved(id).
    property bool applyToActiveBag: true

    signal packageSaved(int packageId)

    // Save-failure wording, as hidden Tr instances so the Connections handlers can
    // read .text (a handler can't use a Tr element inline).
    Tr {
        id: trNameInUse
        visible: false
        key: "equipment.dialog.nameInUse"
        fallback: "That name is already in use — choose a different name."
    }
    Tr {
        id: trSaveFailed
        visible: false
        key: "equipment.dialog.saveFailed"
        fallback: "Couldn't save this equipment — please try again."
    }

    // Always load the inventory: the list mode renders it, and the form mode
    // checks the entered identity against it to block creating a duplicate.
    onAboutToShow: MainController.equipmentStorage.requestInventory()

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(list) { root.packages = list }
        function onPackagesChanged() {
            // Refresh in BOTH modes: while the form is open the cached `packages`
            // list is what the duplicate-name hint reads, so a package created
            // elsewhere (web, MCP, another device) would otherwise leave the hint
            // stale and let a doomed save through.
            if (root.visible)
                MainController.equipmentStorage.requestInventory()
        }
        function onPackageCreated(packageId, pkg) {
            if (!root._awaitingCreate) return
            root._awaitingCreate = false
            if (packageId > 0) {
                // A freshly added package becomes the active equipment (unless the
                // dialog is targeting a specific shot/bag — then the caller applies it).
                if (root.applyToActiveBag)
                    Settings.dye.switchToEquipment(pkg)
                root.packageSaved(packageId)
                root.close()
                return
            }
            // Keep the dialog OPEN on failure: closing discarded everything the
            // user typed and was indistinguishable from a successful save.
            root.saveError = (pkg && pkg.error === "nameInUse")
                ? trNameInUse.text : trSaveFailed.text
        }
        function onPackageUpdateFailed(packageId, reason) {
            // Lands just before packageUpdated(false) and names the cause.
            if (root._awaitingEdit && reason === "nameInUse")
                root._editFailReason = reason
        }
        function onPackageUpdated(resultId, success) {
            if (!root._awaitingEdit) return
            root._awaitingEdit = false
            if (success) {
                // If the edited package was active, repoint to the (possibly forked
                // or merged) result so the resolved identity refreshes.
                if (root._editWasActive && resultId > 0)
                    Settings.dye.activeEquipmentId = resultId
                root.packageSaved(resultId)
                root.close()
                root._editFailReason = ""
                return
            }
            root.saveError = root._editFailReason === "nameInUse"
                ? trNameInUse.text : trSaveFailed.text
            root._editFailReason = ""
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
        // Seed from the currently active package — people reuse most of the same
        // gear, so pre-filling the grinder + basket means they only change what
        // actually differs (often just the basket) instead of re-entering it all.
        // Name stays blank so storage derives a fresh "{brand} {model}".
        fName = ""
        _originalName = ""
        saveError = ""
        fBrand = Settings.dye.dyeGrinderBrand || ""
        fModel = Settings.dye.dyeGrinderModel || ""
        fBurrs = Settings.dye.dyeGrinderBurrs || ""
        fBasketBrand = Settings.dye.dyeBasketBrand || ""
        fBasketModel = Settings.dye.dyeBasketModel || ""
        fPuck = puckFromCanonical(Settings.dye.dyePuckPrepCanonical || "")
        mode = "form"
        open()
    }

    function openForEdit(pkg) {
        formMode = "edit"
        editPackageId = pkg && pkg.id !== undefined ? pkg.id : -1
        fName = (pkg && pkg.name) || ""
        // The name this package ARRIVED with — the duplicate gate below only fires
        // on an actual rename, so a package whose derived name already collides
        // stays editable (block-duplicate-active-names).
        _originalName = fName
        saveError = ""
        fBrand = (pkg && pkg.grinderBrand) || ""
        fModel = (pkg && pkg.grinderModel) || ""
        fBurrs = (pkg && pkg.grinderBurrs) || ""
        fBasketBrand = (pkg && pkg.basketBrand) || ""
        fBasketModel = (pkg && pkg.basketModel) || ""
        fPuck = {
            wdt: !!(pkg && pkg.puckPrep_wdt),
            shaker: !!(pkg && pkg.puckPrep_shaker),
            puckScreen: !!(pkg && pkg.puckPrep_puckScreen),
            paperFilter: !!(pkg && pkg.puckPrep_paperFilter),
            rdt: !!(pkg && pkg.puckPrep_rdt)
        }
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

    // Basket pickers (add-basket-equipment): vendor-first, two-level. The model
    // level carries a differentiator subtitle from the registry summary so similar
    // models within a brand (e.g. S-Works billets, Decent waisted siblings) stay
    // legible. No history fallback — baskets are a curated registry only.
    function basketBrandSuggestions() { return Settings.dye.knownBasketBrands() }
    function basketModelSuggestions() { return Settings.dye.knownBasketModels(root.fBasketBrand) }
    function basketModelDescriptions() {
        var out = {}
        var models = Settings.dye.knownBasketModels(root.fBasketBrand)
        for (var i = 0; i < models.length; ++i)
            out[models[i]] = Settings.dye.basketModelSummary(root.fBasketBrand, models[i])
        return out
    }

    function packageTitle(pkg) {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""].filter(function(s) { return s.length > 0 }).join(" ")
    }

    // Canonical puck-prep string for the form (sorted set flags, like C++
    // PuckPrep::canonical) — the dedup identity and the save payload key.
    function puckCanonical() {
        var on = []
        for (var i = 0; i < puckPrepRows.length; ++i)
            if (fPuck[puckPrepRows[i].key]) on.push(puckPrepRows[i].key)
        on.sort()
        return on.join(",")
    }

    // The id of an existing in-inventory package whose FULL identity (grinder +
    // basket + puck prep) matches the form, or -1. Mirrors the storage dedup key
    // so the UI can't create a duplicate — the same gear is one package, not many.
    // The package being edited is excluded (an unchanged edit isn't a "duplicate").
    readonly property int duplicateOfId: {
        var gb = fBrand.trim().toLowerCase(), gm = fModel.trim().toLowerCase()
        var gbu = fBurrs.trim().toLowerCase()
        var bb = fBasketBrand.trim().toLowerCase(), bm = fBasketModel.trim().toLowerCase()
        var pc = puckCanonical()
        for (var i = 0; i < packages.length; ++i) {
            var p = packages[i]
            if (!p || p.id === undefined) continue
            if (editPackageId > 0 && p.id === editPackageId) continue
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

    // The name the package being edited arrived with (empty when creating).
    property string _originalName: ""

    // The id of another IN-INVENTORY package that already uses the entered name
    // (block-duplicate-active-names), or -1. Trimmed + case-insensitive, excluding
    // the package being edited. Relies on `packages` being the inventory list —
    // EquipmentStorage::loadInventoryStatic filters in_inventory = 1 — so this is
    // scoped to active packages; findPackageByNameStatic is the authoritative
    // guard, so drift there degrades this hint, not the invariant.
    //
    // Only an actual RENAME can collide. A blank name is legal (the title is then
    // derived from brand+model) and never counts; and re-saving the name a package
    // already has never counts either — derived names are not unique by design
    // (two packages differing only in basket both derive "Niche Zero"), so testing
    // the entered name alone disabled Save on both and made them uneditable.
    readonly property int nameDuplicateOfId: {
        var n = fName.trim().toLowerCase()
        if (n.length === 0) return -1
        if (n === _originalName.trim().toLowerCase()) return -1
        for (var i = 0; i < packages.length; ++i) {
            var p = packages[i]
            if (!p || p.id === undefined) continue
            if (editPackageId > 0 && p.id === editPackageId) continue
            if (String(p.name || "").trim().toLowerCase() === n)
                return p.id
        }
        return -1
    }

    // A package needs SOME identity — grinder OR basket. Basket-only
    // (grinder-less) packages are valid tea setups (add-recipe-wizard-tea).
    readonly property bool canSave: (fBrand.trim().length > 0 || fModel.trim().length > 0
                                     || fBasketBrand.trim().length > 0 || fBasketModel.trim().length > 0)
                                    && duplicateOfId < 0 && nameDuplicateOfId < 0
    property bool _awaitingCreate: false

    EquipmentInfoDialog {
        id: pickerInfoDialog
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
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
        readonly property real formCap: (root.parent ? root.parent.height : Theme.scaled(640)) * 0.85
        readonly property real formNatural: formHeader.implicitHeight + fieldsColumn.implicitHeight
                                            + 3 * Theme.spacingMedium
        implicitHeight: root.mode === "list"
                        ? listColumn.implicitHeight + 2 * Theme.spacingMedium
                        : Math.min(formNatural, formCap)

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
                    spacing: Theme.spacingSmall

                    // Select-this-package row — shows the package NAME only
                    // (burrs/grinder identity live in the info popup).
                    AccessibleButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(44)
                        primary: !!pkgRow.modelData && pkgRow.pkg.id === Settings.dye.activeEquipmentId
                        text: root.packageTitle(pkgRow.pkg)
                        accessibleName: text + (primary
                                                ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                        onClicked: {
                            if (!pkgRow.modelData) return
                            if (root.applyToActiveBag)
                                Settings.dye.switchToEquipment(pkgRow.modelData)
                            root.packageSaved(pkgRow.modelData.id)
                            root.close()
                        }
                    }

                    // Info: show this package's full contents.
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(44)
                        visible: !!pkgRow.modelData && pkgRow.pkg.id > 0
                        icon.source: "qrc:/icons/info.svg"
                        accessibleName: TranslationManager.translate("equipment.info.button", "Equipment details")
                        onClicked: pickerInfoDialog.openFor(pkgRow.pkg.id)
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
            textFields: [nameField.textField, brandField.textField, modelField.textField, burrsField.textField,
                         basketBrandField.textField, basketModelField.textField]

            ColumnLayout {
                id: formColumn
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                // Pinned header: title on the left, Cancel/Save on the right of the
                // same line so they stay reachable while the fields scroll below.
                RowLayout {
                    id: formHeader
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    Tr {
                        Layout.fillWidth: true
                        key: root.formMode === "edit" ? "equipment.dialog.editTitle" : "equipment.dialog.addTitle"
                        fallback: root.formMode === "edit" ? "Edit Equipment" : "Add Equipment"
                        font: Theme.titleFont
                        color: Theme.textColor
                        elide: Text.ElideRight
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

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

                // Explains the disabled Save when the entered gear already exists
                // (e.g. the create form was pre-filled and nothing was changed yet).
                Text {
                    Layout.fillWidth: true
                    visible: root.duplicateOfId > 0
                    text: TranslationManager.translate("equipment.dialog.duplicate",
                              "You already have this exact equipment — change something to make it distinct.")
                    font: Theme.captionFont
                    color: Theme.warningColor
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
                    font: Theme.bodyFont
                    color: Theme.errorColor
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
                        spacing: Theme.spacingMedium

                        // User-editable label (add-equipment-packages 4b.1). Blank uses the
                        // derived "{brand} {model}"; set it to a kit name like "Espresso setup".
                        SuggestionField {
                            id: nameField
                            Layout.fillWidth: true
                            label: TranslationManager.translate("equipment.dialog.name", "Name (optional)")
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
                            text: TranslationManager.translate("equipment.dialog.nameInUse",
                                      "That name is already in use — choose a different name.")
                            font: Theme.captionFont
                            color: Theme.warningColor
                            wrapMode: Text.Wrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // Grinder brand + model share a row — both short and related, so
                        // pairing them halves the form height (the burr name below is long
                        // and keeps its own full-width line).
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium

                            SuggestionField {
                                id: brandField
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
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
                                Layout.preferredWidth: 1
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

                        // --- Basket (optional) — vendor-first, two-level (add-basket-equipment).
                        //     Brand + model share a row, mirroring the grinder pair above.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium

                            SuggestionField {
                                id: basketBrandField
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: TranslationManager.translate("equipment.dialog.basketBrand", "Basket brand (optional)")
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
                                    var models = Settings.dye.knownBasketModels(t)
                                    if (models.length === 1) root.fBasketModel = models[0]
                                }
                            }

                            SuggestionField {
                                id: basketModelField
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                label: TranslationManager.translate("equipment.dialog.basketModel", "Basket model")
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
                            Layout.topMargin: Theme.spacingSmall
                            text: TranslationManager.translate("equipment.dialog.puckPrep", "Puck prep (optional)")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingMedium
                            rowSpacing: Theme.spacingSmall

                            Repeater {
                                model: root.puckPrepRows
                                StyledSwitch {
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
        Qt.inputMethod.commit()
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
        for (var i = 0; i < puckPrepRows.length; ++i)
            fields["puckPrep_" + puckPrepRows[i].key] = !!fPuck[puckPrepRows[i].key]
        if (formMode === "edit" && editPackageId > 0) {
            // Editing identity may copy-on-write into a new package id; wait for
            // the result so we can repoint the active selection if needed.
            _editWasActive = (editPackageId === Settings.dye.activeEquipmentId)
            _awaitingEdit = true
            MainController.equipmentStorage.requestUpdatePackage(editPackageId, fields)
        } else {
            MainController.equipmentStorage.requestCreatePackage(fields)
            _awaitingCreate = true
        }
    }

    property bool _awaitingEdit: false
    property bool _editWasActive: false
}
