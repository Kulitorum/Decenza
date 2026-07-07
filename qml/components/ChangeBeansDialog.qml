import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "DateUtils.js" as DateUtils

// Unified "Change Beans" dialog (bean-bag-inventory): one ranked search across
// inventory bags (Tier 0), Bean Base canonical, and the local shot history,
// followed by a Bag Details form for non-inventory picks. Selection semantics
// depend on `context`:
//   "brew" / "inventory" / "idle"  ->  set Settings.dye.activeBagId
//   "postShot"                     ->  set activeBagId AND rewrite the just-saved
//                                      shot's snapshot (the "wrong bag" fix path)
//   "historicalShot"               ->  rewrite only that shot's snapshot;
//                                      activeBagId untouched
//
// Extra entry point (bag inventory page): openForEdit(bag) updates the same
// bag row in place.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(560), parent ? parent.width * 0.95 : Theme.scaled(560))
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    property string context: "brew"   // "brew" | "inventory" | "idle" | "postShot" | "historicalShot"
    property var shotId: 0            // shot to retag (postShot / historicalShot)

    // Emitted after the context's selection semantics ran. `bag` is the
    // selected/created bag's map (CoffeeBag-shaped keys).
    signal bagSelected(int bagId, var bag)

    // "search" -> ranked result list; "form" -> bag details form
    property string mode: "search"
    // "create" -> requestCreateBag on confirm; "edit" -> requestUpdateBag
    property string formMode: "create"
    property int editBagId: -1
    // Identity known from the picked result -> shown as read-only confirmation,
    // not editable fields ("show fields only for unknown values").
    property bool identityKnown: false
    property bool _awaitingCreate: false
    property bool _armedForm: false   // openForEdit pre-armed the form
    property string errorMessage: ""

    // Form state (editable controls write back via onTextEdited)
    property string fRoaster: ""
    property string fCoffee: ""
    property string fRoastDate: ""
    property string fRoastLevel: ""
    property string fBeanBaseId: ""
    property string fBeanBaseData: ""
    // True when the user linked/unlinked Bean Base in this edit session —
    // Save then propagates the bag's link to all its shots.
    property bool fLinkDirty: false
    // Grinder identity (brand/model/burrs) is owned by the equipment package, not
    // the bag (add-equipment-packages); only the grind-setting dial-in stays here.
    // The bag points at a package via fEquipmentId; brand/model/burrs are the
    // resolved read-only display (refreshed via EquipmentStorage.packageReady).
    property string fGrinderSetting: ""
    property string fRpm: ""           // grinder rpm dial-in (string form; "" = unset)
    property int fEquipmentId: -1
    property string fEquipmentName: ""  // package display name (resolved via packageReady)
    property string fEquipmentBrand: ""
    property string fEquipmentModel: ""
    property string fEquipmentBurrs: ""
    readonly property bool fEquipmentRpmCapable:
        Settings.dye.grinderRpmCapable(fEquipmentBrand, fEquipmentModel)
    // Display the package name (defaults to "{brand} {model}").
    readonly property string fEquipmentLabel: fEquipmentName.length > 0
        ? fEquipmentName
        : [fEquipmentBrand, fEquipmentModel].filter(function(s){ return s && s.length > 0 }).join(" ")
    property string fDose: ""         // text form; "" = unset
    property string fYield: ""
    property string fNotes: ""
    property bool fFreeze: false
    property string fFrozenDate: ""
    property string fDefrostDate: ""

    readonly property var formBeanBase: {
        if (!fBeanBaseData || fBeanBaseData.length === 0) return ({})
        try { return JSON.parse(fBeanBaseData) } catch (e) { return ({}) }
    }
    readonly property var _formAttrParts: {
        var parts = []
        if (formBeanBase.origin) parts.push(String(formBeanBase.origin))
        if (formBeanBase.variety) parts.push(String(formBeanBase.variety))
        if (formBeanBase.process) parts.push(String(formBeanBase.process))
        return parts
    }
    // Plain join for the accessibility string; joinWithBullet (styled bold dot,
    // HTML-escaped) for the displayed line.
    readonly property string formAttrLine: _formAttrParts.join("  ·  ")
    readonly property string formAttrLineRich: Theme.joinWithBullet(_formAttrParts)

    // --- Manual-entry autosuggest (history + Bean Base canonical) ---
    // Canonical entries for the current form query; refreshed as the user
    // types in the roaster/coffee fields (C++ debounces + caches).
    property var formCanonicalEntries: []
    property string _formCanonicalQuery: ""
    // Bumped when the shot-history distinct cache refreshes (suggestions
    // re-evaluate, mirroring BrewDialog's pattern).
    property int _distinctVersion: 0

    function requestFormCanonical(q) {
        q = q.trim()
        if (q.length < 2) {
            formCanonicalEntries = []
            _formCanonicalQuery = ""
            return
        }
        _formCanonicalQuery = q.toLowerCase()
        MainController.beanbase.search(q)
    }

    function roasterSuggestions() {
        var _ = _distinctVersion
        var out = MainController.shotHistory ? MainController.shotHistory.getDistinctBeanBrands().slice() : []
        for (var i = 0; i < formCanonicalEntries.length; i++) {
            var name = formCanonicalEntries[i].roasterName
            if (name && out.indexOf(name) === -1) out.push(name)
        }
        return out
    }

    function coffeeSuggestions() {
        var _ = _distinctVersion
        var out = MainController.shotHistory ? MainController.shotHistory.getDistinctBeanTypesForBrand(fRoaster).slice() : []
        for (var i = 0; i < formCanonicalEntries.length; i++) {
            var entry = formCanonicalEntries[i]
            if (fRoaster.length > 0 && entry.roasterName
                && entry.roasterName.toLowerCase() !== fRoaster.toLowerCase())
                continue
            if (entry.roastName && out.indexOf(entry.roastName) === -1) out.push(entry.roastName)
        }
        return out
    }

    // A picked coffee suggestion that came from Bean Base carries the
    // canonical link — apply it like a search-bar pick (enriched async).
    function adoptCanonicalByName(coffeeName) {
        for (var i = 0; i < formCanonicalEntries.length; i++) {
            var entry = formCanonicalEntries[i]
            if (entry.roastName !== coffeeName) continue
            if (fRoaster.length > 0 && entry.roasterName
                && entry.roasterName.toLowerCase() !== fRoaster.toLowerCase())
                continue
            fBeanBaseId = String(entry.id || "")
            fBeanBaseData = JSON.stringify(entry)
            if (entry.roasterName) fRoaster = entry.roasterName
            fLinkDirty = true
            MainController.beanbase.fetchCanonicalDetails(entry)
            return
        }
    }

    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() { root._distinctVersion++ }
    }

    Connections {
        target: MainController.beanbase
        function onSearchResults(query, entries) {
            if (root.mode !== "form") return
            if (query.toLowerCase() !== root._formCanonicalQuery) return
            root.formCanonicalEntries = entries
        }
    }

    function todayIso() {
        var now = new Date()
        return now.getFullYear() + "-"
            + String(now.getMonth() + 1).padStart(2, "0") + "-"
            + String(now.getDate()).padStart(2, "0")
    }

    function sourceLabel(sources, tier) {
        if (tier === 0)
            return TranslationManager.translate("changebeans.source.inventory", "In inventory")
        switch (sources) {
        case "beanbase":
            return TranslationManager.translate("changebeans.source.beanbase", "Bean Base")
        case "history":
            return TranslationManager.translate("changebeans.source.history", "History")
        case "beanbase+history":
            return TranslationManager.translate("changebeans.source.beanbase", "Bean Base")
                + "  ·  " + TranslationManager.translate("changebeans.source.history", "History")
        }
        return ""
    }

    function resetForm() {
        fRoaster = ""; fCoffee = ""; fRoastDate = ""; fRoastLevel = ""
        fBeanBaseId = ""; fBeanBaseData = ""
        fLinkDirty = false
        fGrinderSetting = ""
        fEquipmentId = -1; fEquipmentName = ""; fEquipmentBrand = ""; fEquipmentModel = ""; fEquipmentBurrs = ""
        fRpm = ""
        fDose = ""; fYield = ""; fNotes = ""
        fFreeze = false; fFrozenDate = ""; fDefrostDate = ""
        identityKnown = false
        errorMessage = ""
    }

    function prefillFromBag(bag) {
        fRoaster = bag.roasterName || ""
        fCoffee = bag.coffeeName || ""
        fRoastLevel = bag.roastLevel || ""
        fBeanBaseId = bag.beanBaseId ? String(bag.beanBaseId) : ""
        fBeanBaseData = bag.beanBaseData || ""
        fGrinderSetting = bag.grinderSetting || ""
        fEquipmentId = bag.equipmentId || -1
        fRpm = (bag.rpm ?? 0) > 0 ? String(bag.rpm) : ""
        // Resolve the package's name + grinder identity for the read-only label
        // (packageReady fills fEquipmentName/Brand/Model/Burrs below).
        fEquipmentName = ""; fEquipmentBrand = ""; fEquipmentModel = ""; fEquipmentBurrs = ""
        if (fEquipmentId > 0 && MainController.equipmentStorage)
            MainController.equipmentStorage.requestPackage(fEquipmentId)
        // toFixed(1) (not String()) so a non-exact double like 37.8 prefills as
        // "37.8", not "37.800000000000004" — matching the brew-settings format.
        fDose = (bag.doseWeightG ?? 0) > 0 ? Number(bag.doseWeightG).toFixed(1) : ""
        fYield = (bag.yieldOverrideG ?? 0) > 0 ? Number(bag.yieldOverrideG).toFixed(1) : ""
    }

    // Tier 1-4 search result -> creation form. Roast date is ALWAYS blank and
    // never inferred — a new bag is a new roast date.
    function openFormFromResult(row) {
        resetForm()
        formMode = "create"
        editBagId = -1
        prefillFromBag(row)
        fRoastDate = ""
        identityKnown = fRoaster.length > 0 || fCoffee.length > 0
        mode = "form"
    }

    function openManualEntry() {
        resetForm()
        formMode = "create"
        editBagId = -1
        mode = "form"
    }

    // Edit mode: update the existing bag row in place (activeBagId untouched).
    // Pre-filled INCLUDING the roast date.
    function openForEdit(bag) {
        resetForm()
        formMode = "edit"
        editBagId = bag.id
        prefillFromBag(bag)
        fRoastDate = bag.roastDate || ""
        fNotes = bag.notes || ""
        fFrozenDate = bag.frozenDate || ""
        fDefrostDate = bag.defrostDate || ""
        fFreeze = fFrozenDate.length > 0
        mode = "form"
        _armedForm = true
        open()
        if (fBeanBaseId.length === 0)
            editLinkBar.prefill([fRoaster, fCoffee].filter(function(x) { return x.length > 0 }).join(" "))
    }

    // "Find in Bean Base" on the bag card: edit mode with the link search
    // pre-run, so the canonical results pop up immediately.
    function openForEditAndLink(bag) {
        openForEdit(bag)
        if (fBeanBaseId.length === 0)
            editLinkBar.prefillAndSearch([fRoaster, fCoffee].filter(function(x) { return x.length > 0 }).join(" "))
    }

    // Context-dependent selection semantics. `bag` must carry the bag-shaped keys.
    function applySelection(bagId, bag) {
        if (root.context === "historicalShot") {
            updateShotSnapshot(bagId, bag)
        } else {
            Settings.dye.activeBagId = bagId
            if (root.context === "postShot")
                updateShotSnapshot(bagId, bag)
        }
        root.bagSelected(bagId, bag)
    }

    function updateShotSnapshot(bagId, bag) {
        var sid = root.shotId ?? 0
        if (sid <= 0 || !MainController.shotHistory) return
        MainController.shotHistory.requestUpdateShotMetadata(sid, {
            "beanBrand": bag.roasterName || "",
            "beanType": bag.coffeeName || "",
            "roastDate": bag.roastDate || "",
            "roastLevel": bag.roastLevel || "",
            "beanBaseJson": bag.beanBaseData || "",
            "beanBaseId": bag.beanBaseId ? String(bag.beanBaseId) : "",
            "bagId": bagId,
            "frozenDate": bag.frozenDate || "",
            "defrostDate": bag.defrostDate || ""
        })
    }

    // Inventory rows carry the bag id under "id" (CoffeeBag::toVariantMap);
    // the model's bagId role/key is only set on non-inventory rows (-1).
    // Accept either so a future C++ normalization doesn't break this.
    function resolveBagId(row) {
        if (row.bagId !== undefined && row.bagId > 0) return row.bagId
        if (row.id !== undefined && row.id > 0) return row.id
        return -1
    }

    function selectResult(row) {
        var bagId = resolveBagId(row)
        if (row.tier === 0 && bagId > 0) {
            if (root.context === "inventory") {
                // "Add New Bag" → picking an existing inventory bag means
                // "another bag of the same coffee" (e.g. a fresh purchase):
                // open the creation form pre-filled from it with the bean
                // identity LOCKED — same as picking a History result — and the
                // roast date blank. A separate bag is created; two bags of one
                // coffee, with their own dates/freeze, is expected and fine.
                openFormFromResult(row)
            } else {
                // Switching contexts (brew / idle / post-shot / historical):
                // pick the existing bag, no new row.
                applySelection(bagId, row)
                root.close()
            }
        } else {
            openFormFromResult(row)
        }
    }

    function parseWeight(text) {
        var v = parseFloat(String(text).replace(",", "."))
        return (isNaN(v) || v < 0) ? 0 : v
    }

    function confirmForm() {
        Qt.inputMethod.commit()
        errorMessage = ""
        if (fRoaster.trim().length === 0 && fCoffee.trim().length === 0) {
            errorMessage = TranslationManager.translate(
                "changebeans.form.identityRequired", "Enter a roaster or coffee name")
            return
        }
        var fields = {
            "roasterName": fRoaster.trim(),
            "coffeeName": fCoffee.trim(),
            "roastDate": fRoastDate.length === 10 ? fRoastDate : "",
            "roastLevel": fRoastLevel,
            "beanBaseId": fBeanBaseId,
            "beanBaseData": fBeanBaseData,
            "grinderSetting": fGrinderSetting.trim(),
            "rpm": parseInt(fRpm) || 0,
            "doseWeightG": parseWeight(fDose),
            "yieldOverrideG": parseWeight(fYield),
            "notes": fNotes,
            "frozenDate": fFreeze ? (fFrozenDate.length === 10 ? fFrozenDate : todayIso()) : ""
        }
        if (formMode === "edit") {
            fields["defrostDate"] = fFreeze ? (fDefrostDate.length === 10 ? fDefrostDate : "") : ""
            // Re-point the bag's equipment package (<=0 -> NULL via the column hook).
            fields["equipmentId"] = fEquipmentId
            // A link change fixes the whole bag: propagate the (new or
            // cleared) canonical link onto every shot referencing it.
            MainController.bagStorage.requestUpdateBag(editBagId, fields, fLinkDirty)
            // If this is the active bag, sync the active equipment selection so
            // Brew Settings reflects the change.
            if (editBagId === Settings.dye.activeBagId)
                Settings.dye.activeEquipmentId = fEquipmentId > 0 ? fEquipmentId : -1
            root.close()
        } else {
            fields["defrostDate"] = ""
            fields["inInventory"] = true
            // Persist the equipment package picked in the create form too (the
            // picker row is shown in both modes); <=0 -> NULL via the column hook.
            fields["equipmentId"] = fEquipmentId
            _awaitingCreate = true
            MainController.bagStorage.requestCreateBag(fields)
        }
    }

    Connections {
        target: MainController.bagStorage
        function onBagCreated(bagId, bag) {
            if (!root._awaitingCreate) return
            root._awaitingCreate = false
            if (bagId <= 0) {
                root.errorMessage = TranslationManager.translate(
                    "changebeans.form.createFailed", "Could not save the bag — please try again")
                return
            }
            root.applySelection(bagId, bag)
            root.close()
        }
    }

    // Re-point THIS bag's equipment package. The picker doesn't switch the
    // active bag (applyToActiveBag:false); we record the chosen id and resolve
    // its grinder identity for the label via packageReady below. Persisted on
    // Save (fields.equipmentId).
    SwitchEquipmentDialog {
        id: bagEquipmentDialog
        applyToActiveBag: false
        onPackageSaved: function(packageId) {
            root.fEquipmentId = packageId
            if (MainController.equipmentStorage)
                MainController.equipmentStorage.requestPackage(packageId)
        }
    }
    EquipmentInfoDialog {
        id: bagEquipmentInfoDialog
    }
    Connections {
        target: MainController.equipmentStorage
        // Fills the read-only label for whichever package this bag now points at
        // (both the edit-open prefill and a fresh pick funnel through fEquipmentId).
        function onPackageReady(packageId, pkg) {
            if (packageId !== root.fEquipmentId) return
            root.fEquipmentBrand = pkg.grinderBrand || ""
            root.fEquipmentModel = pkg.grinderModel || ""
            root.fEquipmentBurrs = pkg.grinderBurrs || ""
            root.fEquipmentName = (pkg.name && String(pkg.name).length > 0) ? String(pkg.name) : ""
        }
    }

    onAboutToShow: {
        if (!_armedForm) {
            mode = "search"
            errorMessage = ""
            searchField.text = ""
            MainController.beanSearch.query = ""
            MainController.beanSearch.refresh()
        }
        _armedForm = false
    }

    onOpened: {
        if (mode === "search")
            searchField.forceActiveFocus()
        else if (!identityKnown)
            roasterInput.forceActiveFocus()
        else
            roastDateField.textField.forceActiveFocus()

        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(mode === "search"
                ? TranslationManager.translate("changebeans.accessible.searchOpened", "Change Beans dialog. Search beans")
                : formTitleText.text)
        }
    }

    onClosed: _awaitingCreate = false

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: KeyboardAwareContainer {
        id: keyboardContainer
        inOverlay: true
        implicitWidth: root.width
        implicitHeight: Math.min(mainColumn.implicitHeight,
                                 root.parent ? root.parent.height * 0.9 : mainColumn.implicitHeight)
        textFields: [searchField, roasterInput.textField, coffeeInput.textField, roastDateField.textField,
                     grindSettingInput, doseInput, yieldInput,
                     notesInput, frozenDateField.textField, defrostDateField.textField]
        targetFlickable: formFlickable

        ColumnLayout {
            id: mainColumn
            anchors.fill: parent
            spacing: 0

            // ===== Header =====
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    id: formTitleText
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        var _ = TranslationManager.translationVersion
                        if (root.mode === "search")
                            return root.context === "inventory"
                                ? TranslationManager.translate("changebeans.title.addBag", "Add Bag")
                                : TranslationManager.translate("changebeans.title", "Change Beans")
                        return root.formMode === "edit"
                            ? TranslationManager.translate("changebeans.title.editBag", "Edit Bag")
                            : TranslationManager.translate("changebeans.title.newBag", "New Bag")
                    }
                    font: Theme.titleFont
                    color: Theme.textColor
                    Accessible.ignored: true  // announced on open
                }

                // Form actions live in the header so they're reachable
                // without scrolling past the fields.
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(8)

                    HideKeyboardButton {
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    AccessibleButton {
                        visible: root.mode === "form"
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.scaled(38)
                        leftPadding: Theme.scaled(14)
                        rightPadding: Theme.scaled(14)
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("changebeans.form.accessible.cancel", "Cancel bag details")
                        onClicked: root.close()
                    }

                    AccessibleButton {
                        visible: root.mode === "form"
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.scaled(38)
                        leftPadding: Theme.scaled(14)
                        rightPadding: Theme.scaled(14)
                        primary: true
                        enabled: !root._awaitingCreate
                        text: root.formMode === "edit"
                            ? TranslationManager.translate("common.save", "Save")
                            : TranslationManager.translate("changebeans.form.create", "Add Bag")
                        accessibleName: root.formMode === "edit"
                            ? TranslationManager.translate("changebeans.form.accessible.save", "Save bag changes")
                            : TranslationManager.translate("changebeans.form.accessible.create", "Create bag")
                        onClicked: root.confirmForm()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // ===== Search page =====
            ColumnLayout {
                visible: root.mode === "search"
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(16)
                spacing: Theme.scaled(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    StyledTextField {
                        id: searchField
                        Layout.fillWidth: true
                        placeholder: TranslationManager.translate("changebeans.search.placeholder", "Search roaster or coffee")
                        accessibleName: TranslationManager.translate("changebeans.search.accessible", "Search beans")
                        onTextEdited: MainController.beanSearch.query = text
                    }

                    BusyIndicator {
                        running: MainController.beanSearch.searching
                        visible: running
                        implicitWidth: Theme.scaled(28)
                        implicitHeight: Theme.scaled(28)
                        Accessible.ignored: true
                    }
                }

                ListView {
                    id: resultsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(340)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: MainController.beanSearch
                    spacing: Theme.scaled(4)

                    delegate: Rectangle {
                        id: resultRow
                        width: resultsList.width
                        height: Theme.scaled(56)
                        radius: Theme.scaled(8)
                        color: Theme.backgroundColor
                        border.width: 1
                        border.color: resultRow.isActiveBag ? Theme.primaryColor : Theme.borderColor
                        Accessible.ignored: true

                        // Delegates are recreated on every model reset, so the
                        // one-shot get() read stays in sync with the row data.
                        readonly property int rowBagId: root.resolveBagId(MainController.beanSearch.get(index))
                        readonly property bool isActiveBag: model.tier === 0 && rowBagId > 0
                            && rowBagId === Settings.dye.activeBagId

                        readonly property string primaryText: {
                            var coffee = model.coffeeName || ""
                            var roaster = model.roasterName || ""
                            if (coffee.length > 0 && roaster.length > 0) return roaster + " " + coffee
                            return coffee.length > 0 ? coffee : roaster
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            spacing: Theme.scaled(8)

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(2)

                                Text {
                                    Layout.fillWidth: true
                                    text: resultRow.primaryText
                                    // Bean Base free text — never let AutoText parse it as markup
                                    textFormat: Text.PlainText
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.bodyFont.pixelSize
                                    font.bold: true
                                    color: Theme.textColor
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    // Bean Base rows have no roast date; their detail line
                                    // (roast level · origin · notes) is what tells the
                                    // canonical DB's same-name near-duplicates apart.
                                    text: model.roastDate || model.detail || ""
                                    // Bean Base free text — never let AutoText parse it as markup
                                    textFormat: Text.PlainText
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }
                            }

                            // Source chip
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: chipText.implicitWidth + Theme.scaled(16)
                                implicitHeight: chipText.implicitHeight + Theme.scaled(8)
                                radius: height / 2
                                color: model.tier === 0 ? Theme.primaryColor : Theme.backgroundColor
                                border.width: model.tier === 0 ? 0 : 1
                                border.color: Theme.textSecondaryColor

                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: root.sourceLabel(model.sources, model.tier)
                                    font: Theme.captionFont
                                    color: model.tier === 0 ? Theme.primaryContrastColor : Theme.textSecondaryColor
                                    Accessible.ignored: true
                                }
                            }
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: resultRow.primaryText
                                + (model.detail ? ", " + model.detail : "")
                                + ", " + root.sourceLabel(model.sources, model.tier)
                                + (resultRow.isActiveBag
                                    ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                            accessibleItem: resultRow
                            onAccessibleClicked: {
                                Qt.inputMethod.commit()
                                root.selectResult(MainController.beanSearch.get(index))
                            }
                        }
                    }

                    // Tier 5: manual entry — static row, not in the model.
                    // Top of the list while the search is empty (a fresh bag
                    // is one tap away); last row once a query narrows things.
                    header: MainController.beanSearch.query.length === 0 ? manualEntryComponent : null
                    footer: MainController.beanSearch.query.length > 0 ? manualEntryComponent : null

                    Component {
                        id: manualEntryComponent
                        Item {
                        width: resultsList.width
                        height: Theme.scaled(60)

                        Rectangle {
                            id: manualRow
                            anchors.fill: parent
                            anchors.topMargin: Theme.scaled(4)
                            anchors.bottomMargin: Theme.scaled(4)
                            radius: Theme.scaled(8)
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.textSecondaryColor
                            Accessible.ignored: true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.scaled(12)
                                spacing: Theme.scaled(8)

                                ColoredIcon {
                                    source: "qrc:/icons/plus.svg"
                                    iconWidth: Theme.scaled(16)
                                    iconHeight: Theme.scaled(16)
                                    iconColor: Theme.textColor
                                    Accessible.ignored: true
                                }

                                Tr {
                                    Layout.fillWidth: true
                                    key: "changebeans.enterManually"
                                    fallback: "Enter manually"
                                    font: Theme.bodyFont
                                    color: Theme.textColor
                                    Accessible.ignored: true
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("changebeans.accessible.enterManually", "Enter bean details manually")
                                accessibleItem: manualRow
                                onAccessibleClicked: {
                                    Qt.inputMethod.commit()
                                    root.openManualEntry()
                                }
                            }
                        }
                        }
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("changebeans.accessible.cancel", "Cancel changing beans")
                    onClicked: root.close()
                }
            }

            // ===== Bag details form =====
            Flickable {
                id: formFlickable
                visible: root.mode === "form"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(formColumn.implicitHeight + keyboardContainer.estimatedKeyboardHeight,
                                                 root.parent ? root.parent.height * 0.9 - Theme.scaled(60) : formColumn.implicitHeight)
                contentHeight: formColumn.implicitHeight + keyboardContainer.estimatedKeyboardHeight
                contentWidth: width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                ColumnLayout {
                    id: formColumn
                    width: formFlickable.width
                    spacing: Theme.scaled(10)

                    Item { Layout.preferredHeight: Theme.scaled(6) }

                    // --- Known identity: read-only confirmation ---
                    ColumnLayout {
                        visible: root.identityKnown
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        spacing: Theme.scaled(2)

                        Accessible.role: Accessible.StaticText
                        Accessible.name: [root.fRoaster, root.fCoffee].filter(function(s) { return s.length > 0 }).join(" ")
                            + (root.fBeanBaseId.length > 0
                                ? ", " + TranslationManager.translate("beans.summary.accessible.verified", "linked to Bean Base") : "")
                            + (root.formAttrLine.length > 0 ? ", " + root.formAttrLine : "")
                        Accessible.focusable: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(6)

                            ColoredIcon {
                                visible: root.fBeanBaseId.length > 0
                                source: "qrc:/icons/tick.svg"
                                iconWidth: Theme.scaled(14)
                                iconHeight: Theme.scaled(14)
                                iconColor: Theme.primaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: [root.fRoaster, root.fCoffee].filter(function(s) { return s.length > 0 }).join(" ")
                                font: Theme.subtitleFont
                                color: Theme.textColor
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.formAttrLine.length > 0
                            text: root.formAttrLineRich
                            textFormat: Text.StyledText
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }

                    // --- Bean Base link (edit mode): upgrade a free-text bag
                    // to its canonical record. Saving then propagates the
                    // link to every shot pulled with this bag.
                    Item {
                        visible: root.formMode === "edit"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        implicitHeight: editLinkBar.implicitHeight

                        BeanBaseSearchBar {
                            id: editLinkBar
                            anchors.left: parent.left
                            anchors.right: parent.right
                            linked: root.fBeanBaseId.length > 0
                            linkedLabel: {
                                var bb = root.formBeanBase
                                var name = [bb.roasterName || root.fRoaster, bb.roastName || root.fCoffee]
                                    .filter(function(x) { return x && x.length > 0 }).join(" ")
                                return name
                            }

                            onEntrySelected: function(entry) {
                                root.fBeanBaseId = String(entry.id || "")
                                root.fBeanBaseData = JSON.stringify(entry)
                                if (entry.roasterName) root.fRoaster = entry.roasterName
                                if (entry.roastName) root.fCoffee = entry.roastName
                                root.fLinkDirty = true
                                // Best-effort attribute enrichment (origin,
                                // variety, process, ...) — merged below when
                                // canonicalDetails arrives.
                                MainController.beanbase.fetchCanonicalDetails(entry)
                                // Warm the bag-photo file cache so the
                                // inventory card shows the image right away.
                                MainController.beanbase.ensureBagImage(
                                    String(entry.id || ""), entry.roastName || "", entry.link || "")
                            }
                            onUnlinkRequested: {
                                root.fBeanBaseId = ""
                                root.fBeanBaseData = ""
                                root.fLinkDirty = true
                            }
                        }
                    }

                    Connections {
                        target: MainController.beanbase
                        function onCanonicalDetails(canonicalId, attrs) {
                            if (root.mode !== "form" || root.fBeanBaseId !== canonicalId)
                                return
                            try {
                                var blob = JSON.parse(root.fBeanBaseData)
                                for (var key in attrs)
                                    blob[key] = attrs[key]
                                root.fBeanBaseData = JSON.stringify(blob)
                            } catch (e) {
                                // Corrupt staged blob: keep the minimal entry
                            }
                        }
                    }

                    // --- Unknown identity: editable fields (manual entry / edit mode) ---
                    FieldRow {
                        visible: !root.identityKnown
                        labelKey: "changebeans.form.roaster"
                        labelFallback: "Roaster:"

                        SuggestionField {
                            id: roasterInput
                            Layout.fillWidth: true
                            text: root.fRoaster
                            suggestions: root.roasterSuggestions()
                            accessibleName: TranslationManager.translate("changebeans.form.roaster.accessible", "Roaster")
                            onTextEdited: function(t) {
                                root.fRoaster = t
                                root.requestFormCanonical(t)
                            }
                        }
                    }

                    FieldRow {
                        visible: !root.identityKnown
                        labelKey: "changebeans.form.coffee"
                        labelFallback: "Coffee:"

                        SuggestionField {
                            id: coffeeInput
                            Layout.fillWidth: true
                            text: root.fCoffee
                            suggestions: root.coffeeSuggestions()
                            accessibleName: TranslationManager.translate("changebeans.form.coffee.accessible", "Coffee name")
                            onTextEdited: function(t) {
                                root.fCoffee = t
                                root.requestFormCanonical(root.fRoaster.length > 0 ? root.fRoaster + " " + t : t)
                            }
                            // A Bean Base suggestion pick links the bag too.
                            onSuggestionSelected: function(t) { root.adoptCanonicalByName(t) }
                        }
                    }

                    // --- Roast date: ALWAYS blank in create modes, optional ---
                    BeanDateField {
                        id: roastDateField
                        labelKey: "changebeans.form.roastDate"
                        labelFallback: "Roasted:"
                        value: root.fRoastDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.roastDate.accessible", "Roast date, optional.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.roastDate.openCalendar", "Open calendar to pick roast date")
                        onValueEdited: root.fRoastDate = dateString
                    }

                    // --- Roast level: editable only when not supplied by the pick ---
                    FieldRow {
                        visible: !root.identityKnown
                        labelKey: "changebeans.form.roastLevel"
                        labelFallback: "Roast level:"

                        StyledComboBox {
                            id: roastLevelCombo
                            Layout.fillWidth: true
                            accessibleLabel: TranslationManager.translate("changebeans.form.roastLevel.accessible", "Roast level")
                            model: ["",
                                TranslationManager.translate("shotmetadata.roastlevel.light", "Light"),
                                TranslationManager.translate("shotmetadata.roastlevel.mediumlight", "Medium-Light"),
                                TranslationManager.translate("shotmetadata.roastlevel.medium", "Medium"),
                                TranslationManager.translate("shotmetadata.roastlevel.mediumdark", "Medium-Dark"),
                                TranslationManager.translate("shotmetadata.roastlevel.dark", "Dark")]
                            currentIndex: Math.max(0, model.indexOf(root.fRoastLevel))
                            onActivated: root.fRoastLevel = currentIndex > 0 ? currentText : ""
                        }
                    }

                    // --- Freeze tracking (under roast level, above grind) ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "changebeans.form.freeze"
                            fallback: "Frozen bag"
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        StyledSwitch {
                            id: freezeSwitch
                            checked: root.fFreeze
                            accessibleName: TranslationManager.translate("changebeans.form.freeze.accessible", "Track this bag as frozen")
                            onToggled: {
                                root.fFreeze = checked
                                if (checked && root.fFrozenDate.length !== 10)
                                    root.fFrozenDate = root.todayIso()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    BeanDateField {
                        id: frozenDateField
                        visible: root.fFreeze
                        labelKey: "changebeans.form.frozenDate"
                        labelFallback: "Frozen:"
                        value: root.fFrozenDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.frozenDate.accessible", "Frozen date.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.frozenDate.openCalendar", "Open calendar to pick frozen date")
                        onValueEdited: root.fFrozenDate = dateString
                    }

                    // Defrost date is only directly editable in edit mode
                    // ("Next Portion" on the bag card is the everyday path)
                    BeanDateField {
                        id: defrostDateField
                        visible: root.fFreeze && root.formMode === "edit"
                        labelKey: "changebeans.form.defrostDate"
                        labelFallback: "Defrosted:"
                        value: root.fDefrostDate
                        fieldAccessibleName: TranslationManager.translate("changebeans.form.defrostDate.accessible", "Defrost date, optional.")
                        calendarAccessibleName: TranslationManager.translate("changebeans.form.defrostDate.openCalendar", "Open calendar to pick defrost date")
                        onValueEdited: root.fDefrostDate = dateString
                    }

                    // --- Grinder setting + dose (the per-bag dial-in fields) ---
                    FieldRow {
                        labelKey: "changebeans.form.grindSetting"
                        labelFallback: "Grind:"

                        StyledTextField {
                            id: grindSettingInput
                            Layout.fillWidth: true
                            text: root.fGrinderSetting
                            accessibleName: TranslationManager.translate("changebeans.form.grindSetting.accessible", "Grinder setting")
                            onTextEdited: root.fGrinderSetting = text
                        }
                    }

                    // RPM dial-in — only when the bag's grinder is rpm-adjustable.
                    FieldRow {
                        labelKey: "changebeans.form.rpm"
                        labelFallback: "RPM:"
                        visible: root.fEquipmentRpmCapable

                        StyledTextField {
                            Layout.fillWidth: true
                            text: root.fRpm
                            inputMethodHints: Qt.ImhDigitsOnly
                            accessibleName: TranslationManager.translate("changebeans.form.rpm.accessible", "Grinder rpm")
                            onTextEdited: root.fRpm = text
                        }
                    }

                    // Equipment package (read-only NAME + info + re-point button).
                    // Grinder identity is owned by the package, not the bag; tap
                    // Switch/Add to point this bag at a different package, or the
                    // info button to see the package's contents.
                    FieldRow {
                        labelKey: "changebeans.form.equipment"
                        labelFallback: "Equipment:"

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: root.fEquipmentLabel.length > 0
                                  ? root.fEquipmentLabel
                                  : TranslationManager.translate("changebeans.form.equipmentNotSet", "Not set")
                            font: Theme.bodyFont
                            color: root.fEquipmentLabel.length > 0 ? Theme.textColor : Theme.textSecondaryColor
                            Accessible.role: Accessible.StaticText
                            Accessible.name: TranslationManager.translate("changebeans.form.equipment", "Equipment:") + " " + text
                        }
                        AccessibleButton {
                            visible: root.fEquipmentId > 0
                            icon.source: "qrc:/icons/info.svg"
                            accessibleName: TranslationManager.translate("equipment.info.button", "Equipment details")
                            onClicked: bagEquipmentInfoDialog.openFor(root.fEquipmentId)
                        }
                        AccessibleButton {
                            text: root.fEquipmentLabel.length > 0
                                  ? TranslationManager.translate("changebeans.form.switchEquipment", "Switch")
                                  : TranslationManager.translate("changebeans.form.addEquipment", "Add")
                            accessibleName: TranslationManager.translate("changebeans.form.switchEquipmentAccessible", "Switch equipment package")
                            onClicked: bagEquipmentDialog.openPicker()
                        }
                    }

                    FieldRow {
                        labelKey: "changebeans.form.dose"
                        labelFallback: "Dose:"

                        StyledTextField {
                            id: doseInput
                            Layout.fillWidth: true
                            text: root.fDose
                            placeholder: TranslationManager.translate("changebeans.form.grams", "g")
                            accessibleName: TranslationManager.translate("changebeans.form.dose.accessible", "Dose weight in grams")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: root.fDose = text
                        }

                        Tr {
                            key: "changebeans.form.yieldOverride"
                            fallback: "Yield override:"
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        StyledTextField {
                            id: yieldInput
                            Layout.fillWidth: true
                            text: root.fYield
                            // Blank = follow the profile's target weight; a value
                            // overrides it for this bean (see yieldOverrideG).
                            placeholder: TranslationManager.translate("changebeans.form.yieldOverride.placeholder", "Profile default")
                            accessibleName: TranslationManager.translate("changebeans.form.yieldOverride.accessible", "Yield override in grams, blank to follow the profile default")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: root.fYield = text
                        }
                    }

                    // Notes — always visible. Grinder IDENTITY is no longer edited
                    // here: it's owned by the Equipment package (add-equipment-
                    // packages), set via Switch Equipment in Brew Settings. The bag
                    // keeps only its grind-setting dial-in (above).
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        FieldRow {
                            labelKey: "changebeans.form.notes"
                            labelFallback: "Notes:"

                            StyledTextField {
                                id: notesInput
                                Layout.fillWidth: true
                                text: root.fNotes
                                accessibleName: TranslationManager.translate("changebeans.form.notes.accessible", "Bag notes")
                                onTextEdited: root.fNotes = text
                            }
                        }
                    }

                    // Error message (create failure / validation)
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        visible: root.errorMessage.length > 0
                        text: root.errorMessage
                        font: Theme.bodyFont
                        color: Theme.warningColor
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.AlertMessage
                        Accessible.name: text
                        Accessible.focusable: true
                    }

                    // (Cancel / Save live in the dialog header — reachable
                    // without scrolling past the form fields.)
                    Item { Layout.preferredHeight: Theme.scaled(8) }
                }
            }
        }
    }

    // Labelled form row: fixed-width label + caller-supplied controls
    component FieldRow: RowLayout {
        property string labelKey: ""
        property string labelFallback: ""

        Layout.fillWidth: true
        Layout.leftMargin: Theme.scaled(20)
        Layout.rightMargin: Theme.scaled(20)
        spacing: Theme.scaled(6)

        Tr {
            key: labelKey
            fallback: labelFallback
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Theme.scaled(85)
            Accessible.ignored: true
        }
    }

    // Bean date entry row (roast / frozen / defrost): the labelled FieldRow plus a
    // locale-aware date text field and a calendar-picker button. Single-sources the
    // accessibility contract for bean dates:
    //   - No inputMask. A masked field pre-fills a "____-__-__" skeleton that Qt
    //     exposes via displayText() to the accessibility tree, so TalkBack/VoiceOver
    //     announce a skeleton of blanks. This field starts genuinely empty instead.
    //   - Entry order + separator follow the host locale (US month-first, most of
    //     the world day-first, ISO year-first); the value is always STORED as ISO
    //     yyyy-mm-dd — only the displayed order is localized.
    //   - Digits-only keypad, separators inserted progressively as the user types.
    // The caller binds `value` (ISO) and writes it back in onValueEdited (a signal,
    // so the field never touches caller state).
    component BeanDateField: FieldRow {
        id: dateField

        property string value: ""                  // stored ISO yyyy-mm-dd (or "")
        property string fieldAccessibleName: ""
        property string calendarAccessibleName: ""
        // Exposes the text input so the dialog's KeyboardAwareContainer can track
        // it and onOpened can focus it — same convention as roasterInput.textField.
        property alias textField: dateInput
        signal valueEdited(string dateString)      // emits ISO yyyy-mm-dd (or "")

        // Locale-derived entry order/separator (pure helpers in DateUtils).
        readonly property string _dateFormat: Qt.locale().dateFormat(Locale.ShortFormat)
        readonly property var _order: DateUtils.dateOrderFromFormat(dateField._dateFormat)
        readonly property string _sep: DateUtils.dateSeparatorFromFormat(dateField._dateFormat)
        readonly property string _placeholder: dateField._order.map(function(k) {
            return k === "y" ? "yyyy" : (k === "M" ? "mm" : "dd")
        }).join(dateField._sep)
        // Spoken order hint, fully translated: each segment word and the joiner are
        // looked up here (QML can reach TranslationManager; DateUtils cannot), so a
        // non-English screen reader hears the order in its own language.
        readonly property string _orderHint:
            TranslationManager.translate("dateentry.orderHint", "Enter %1.").replace("%1",
                DateUtils.orderWords(dateField._order,
                    ({ y: TranslationManager.translate("dateentry.segment.year", "year"),
                       M: TranslationManager.translate("dateentry.segment.month", "month"),
                       d: TranslationManager.translate("dateentry.segment.day", "day") }),
                    TranslationManager.translate("dateentry.orderConnector", ", then ")))

        StyledTextField {
            id: dateInput
            Layout.fillWidth: true
            placeholder: dateField._placeholder
            // Persistent label + spoken order + calendar hint, assembled via a
            // translatable template so translators control ordering and spacing.
            accessibleName: TranslationManager.translate("dateentry.fieldAccessible", "%1 %2 %3")
                .replace("%1", dateField.fieldAccessibleName)
                .replace("%2", dateField._orderHint)
                .replace("%3", TranslationManager.translate("dateentry.calendarHint", "Or use the calendar button next to this field."))
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhPreferNumbers

            // value (ISO) -> displayed localized text. Set imperatively, not via a
            // `text:` binding, because user editing breaks a text binding; this
            // Connections re-syncs the display whenever the stored value changes
            // (calendar selection, form load/reset) and the user isn't mid-edit.
            Component.onCompleted:
                text = DateUtils.isoToLocalized(dateField.value, dateField._order, dateField._sep)
            Connections {
                target: dateField
                function onValueChanged() {
                    if (!dateInput.activeFocus)
                        dateInput.text = DateUtils.isoToLocalized(dateField.value, dateField._order, dateField._sep)
                }
            }

            // Progressive formatting: separators appear as segments fill. Reassigning
            // `text` would send the caret to the end, so preserve it by the count of
            // digits before the caret (stable across separator insertion) and restore
            // it after reformatting — lets the user edit an earlier segment in place.
            onTextEdited: {
                var digitsBeforeCaret = text.substring(0, cursorPosition).replace(/\D/g, "").length
                var formatted = DateUtils.formatAsTyped(text, dateField._order, dateField._sep)
                if (formatted !== text)
                    text = formatted
                cursorPosition = DateUtils.caretForDigits(text, digitsBeforeCaret)
            }

            // Commit: parse localized -> ISO, store it, and reconcile the display so
            // shown and stored can't diverge. Empty stores ""; a complete valid date
            // stores its ISO; an incomplete/invalid entry is reverted to the stored
            // value (never left diverging from what a subsequent Save would persist).
            onEditingFinished: {
                if (text.replace(/\D/g, "").length === 0) {
                    dateField.valueEdited("")
                    text = ""
                    return
                }
                var iso = DateUtils.localizedToIso(text, dateField._order)
                if (iso.length > 0) {
                    dateField.valueEdited(iso)
                    text = DateUtils.isoToLocalized(iso, dateField._order, dateField._sep)
                } else {
                    // Incomplete/invalid: revert to the stored value so the shown text
                    // matches what Save would persist (no silent divergence).
                    text = DateUtils.isoToLocalized(dateField.value, dateField._order, dateField._sep)
                }
            }
        }

        AccessibleButton {
            Layout.preferredWidth: Theme.scaled(44)
            Layout.preferredHeight: Theme.scaled(44)
            accessibleName: dateField.calendarAccessibleName
            leftPadding: Theme.scaled(8)
            rightPadding: Theme.scaled(8)
            icon.source: "qrc:/emoji/1f4c5.svg"
            icon.width: Theme.scaled(20)
            icon.height: Theme.scaled(20)
            text: ""
            onClicked: datePicker.openWithDate(dateField.value)
        }

        DatePickerDialog {
            id: datePicker
            onDateSelected: function(dateString) { dateField.valueEdited(dateString) }
        }
    }
}
