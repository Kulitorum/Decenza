import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

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
    property string fGrinderBrand: ""
    property string fGrinderModel: ""
    property string fGrinderBurrs: ""
    property string fGrinderSetting: ""
    property string fDose: ""         // text form; "" = unset
    property string fYield: ""
    property string fStartWeight: ""
    property string fNotes: ""
    property bool fFreeze: false
    property string fFrozenDate: ""
    property string fDefrostDate: ""

    readonly property var formBeanBase: {
        if (!fBeanBaseData || fBeanBaseData.length === 0) return ({})
        try { return JSON.parse(fBeanBaseData) } catch (e) { return ({}) }
    }
    readonly property string formAttrLine: {
        var parts = []
        if (formBeanBase.origin) parts.push(String(formBeanBase.origin))
        if (formBeanBase.variety) parts.push(String(formBeanBase.variety))
        if (formBeanBase.process) parts.push(String(formBeanBase.process))
        return parts.join(" · ")
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
                + " · " + TranslationManager.translate("changebeans.source.history", "History")
        }
        return ""
    }

    function resetForm() {
        fRoaster = ""; fCoffee = ""; fRoastDate = ""; fRoastLevel = ""
        fBeanBaseId = ""; fBeanBaseData = ""
        fGrinderBrand = ""; fGrinderModel = ""; fGrinderBurrs = ""; fGrinderSetting = ""
        fDose = ""; fYield = ""; fStartWeight = ""; fNotes = ""
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
        fGrinderBrand = bag.grinderBrand || ""
        fGrinderModel = bag.grinderModel || ""
        fGrinderBurrs = bag.grinderBurrs || ""
        fGrinderSetting = bag.grinderSetting || ""
        fDose = (bag.doseWeightG ?? 0) > 0 ? String(bag.doseWeightG) : ""
        fYield = (bag.yieldTargetG ?? 0) > 0 ? String(bag.yieldTargetG) : ""
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
        fStartWeight = (bag.startWeightG ?? 0) > 0 ? String(bag.startWeightG) : ""
        fNotes = bag.notes || ""
        fFrozenDate = bag.frozenDate || ""
        fDefrostDate = bag.defrostDate || ""
        fFreeze = fFrozenDate.length > 0
        mode = "form"
        _armedForm = true
        open()
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
            // Inventory bag: apply immediately, no details form
            applySelection(bagId, row)
            root.close()
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
            "roastDate": fRoastDate.replace(/_/g, "").length === 10 ? fRoastDate : "",
            "roastLevel": fRoastLevel,
            "beanBaseId": fBeanBaseId,
            "beanBaseData": fBeanBaseData,
            "grinderBrand": fGrinderBrand.trim(),
            "grinderModel": fGrinderModel.trim(),
            "grinderBurrs": fGrinderBurrs.trim(),
            "grinderSetting": fGrinderSetting.trim(),
            "doseWeightG": parseWeight(fDose),
            "yieldTargetG": parseWeight(fYield),
            "startWeightG": parseWeight(fStartWeight),
            "notes": fNotes,
            "frozenDate": fFreeze ? (fFrozenDate.replace(/_/g, "").length === 10 ? fFrozenDate : todayIso()) : ""
        }
        if (formMode === "edit") {
            fields["defrostDate"] = fFreeze ? (fDefrostDate.replace(/_/g, "").length === 10 ? fDefrostDate : "") : ""
            MainController.bagStorage.requestUpdateBag(editBagId, fields)
            root.close()
        } else {
            fields["defrostDate"] = ""
            fields["inInventory"] = true
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
            roastDateInput.forceActiveFocus()

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
        textFields: [searchField, roasterInput, coffeeInput, roastDateInput,
                     grindSettingInput, doseInput, yieldInput,
                     grinderBrandInput, grinderModelInput, grinderBurrsInput,
                     startWeightInput, notesInput, frozenDateInput, defrostDateInput]
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
                            return TranslationManager.translate("changebeans.title", "Change Beans")
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
                                    text: model.roastDate || ""
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
                            accessibleName: resultRow.primaryText + ", " + root.sourceLabel(model.sources, model.tier)
                                + (model.tier === 0 && model.bagId === Settings.dye.activeBagId
                                    ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                            accessibleItem: resultRow
                            onAccessibleClicked: {
                                Qt.inputMethod.commit()
                                root.selectResult(MainController.beanSearch.get(index))
                            }
                        }
                    }

                    // Tier 5: manual entry — static last row, not in the model
                    footer: Item {
                        width: resultsList.width
                        height: Theme.scaled(60)

                        Rectangle {
                            id: manualRow
                            anchors.fill: parent
                            anchors.topMargin: Theme.scaled(4)
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
                            text: root.formAttrLine
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }

                    // --- Unknown identity: editable fields (manual entry / edit mode) ---
                    FieldRow {
                        visible: !root.identityKnown
                        labelKey: "changebeans.form.roaster"
                        labelFallback: "Roaster:"

                        StyledTextField {
                            id: roasterInput
                            Layout.fillWidth: true
                            text: root.fRoaster
                            accessibleName: TranslationManager.translate("changebeans.form.roaster.accessible", "Roaster")
                            onTextEdited: root.fRoaster = text
                        }
                    }

                    FieldRow {
                        visible: !root.identityKnown
                        labelKey: "changebeans.form.coffee"
                        labelFallback: "Coffee:"

                        StyledTextField {
                            id: coffeeInput
                            Layout.fillWidth: true
                            text: root.fCoffee
                            accessibleName: TranslationManager.translate("changebeans.form.coffee.accessible", "Coffee name")
                            onTextEdited: root.fCoffee = text
                        }
                    }

                    // --- Roast date: ALWAYS blank in create modes, optional ---
                    FieldRow {
                        labelKey: "changebeans.form.roastDate"
                        labelFallback: "Roasted:"

                        StyledTextField {
                            id: roastDateInput
                            Layout.fillWidth: true
                            text: root.fRoastDate
                            placeholder: TranslationManager.translate("changebeans.form.roastDate.placeholder", "yyyy-mm-dd (optional)")
                            accessibleName: TranslationManager.translate("changebeans.form.roastDate.accessible", "Roast date, optional")
                            inputMethodHints: Qt.ImhDate
                            inputMask: "9999-99-99"
                            onTextEdited: root.fRoastDate = text.replace(/_/g, "")
                        }

                        AccessibleButton {
                            Layout.preferredWidth: Theme.scaled(44)
                            Layout.preferredHeight: Theme.scaled(44)
                            accessibleName: TranslationManager.translate("datepicker.openCalendar", "Open calendar")
                            leftPadding: Theme.scaled(8)
                            rightPadding: Theme.scaled(8)
                            icon.source: "qrc:/emoji/1f4c5.svg"
                            icon.width: Theme.scaled(20)
                            icon.height: Theme.scaled(20)
                            text: ""
                            onClicked: roastDatePicker.openWithDate(root.fRoastDate)
                        }

                        DatePickerDialog {
                            id: roastDatePicker
                            onDateSelected: function(dateString) { root.fRoastDate = dateString }
                        }
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
                                if (checked && root.fFrozenDate.replace(/_/g, "").length !== 10)
                                    root.fFrozenDate = root.todayIso()
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    FieldRow {
                        visible: root.fFreeze
                        labelKey: "changebeans.form.frozenDate"
                        labelFallback: "Frozen:"

                        StyledTextField {
                            id: frozenDateInput
                            Layout.fillWidth: true
                            text: root.fFrozenDate
                            accessibleName: TranslationManager.translate("changebeans.form.frozenDate.accessible", "Frozen date")
                            inputMethodHints: Qt.ImhDate
                            inputMask: "9999-99-99"
                            onTextEdited: root.fFrozenDate = text.replace(/_/g, "")
                        }

                        AccessibleButton {
                            Layout.preferredWidth: Theme.scaled(44)
                            Layout.preferredHeight: Theme.scaled(44)
                            accessibleName: TranslationManager.translate("datepicker.openCalendar", "Open calendar")
                            leftPadding: Theme.scaled(8)
                            rightPadding: Theme.scaled(8)
                            icon.source: "qrc:/emoji/1f4c5.svg"
                            icon.width: Theme.scaled(20)
                            icon.height: Theme.scaled(20)
                            text: ""
                            onClicked: frozenDatePicker.openWithDate(root.fFrozenDate)
                        }

                        DatePickerDialog {
                            id: frozenDatePicker
                            onDateSelected: function(dateString) { root.fFrozenDate = dateString }
                        }
                    }

                    // Defrost date is only directly editable in edit mode
                    // ("Next Portion" on the bag card is the everyday path)
                    FieldRow {
                        visible: root.fFreeze && root.formMode === "edit"
                        labelKey: "changebeans.form.defrostDate"
                        labelFallback: "Defrosted:"

                        StyledTextField {
                            id: defrostDateInput
                            Layout.fillWidth: true
                            text: root.fDefrostDate
                            placeholder: TranslationManager.translate("changebeans.form.roastDate.placeholder", "yyyy-mm-dd (optional)")
                            accessibleName: TranslationManager.translate("changebeans.form.defrostDate.accessible", "Defrost date, optional")
                            inputMethodHints: Qt.ImhDate
                            inputMask: "9999-99-99"
                            onTextEdited: root.fDefrostDate = text.replace(/_/g, "")
                        }

                        AccessibleButton {
                            Layout.preferredWidth: Theme.scaled(44)
                            Layout.preferredHeight: Theme.scaled(44)
                            accessibleName: TranslationManager.translate("datepicker.openCalendar", "Open calendar")
                            leftPadding: Theme.scaled(8)
                            rightPadding: Theme.scaled(8)
                            icon.source: "qrc:/emoji/1f4c5.svg"
                            icon.width: Theme.scaled(20)
                            icon.height: Theme.scaled(20)
                            text: ""
                            onClicked: defrostDatePicker.openWithDate(root.fDefrostDate)
                        }

                        DatePickerDialog {
                            id: defrostDatePicker
                            onDateSelected: function(dateString) { root.fDefrostDate = dateString }
                        }
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
                            key: "changebeans.form.yield"
                            fallback: "Yield:"
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        StyledTextField {
                            id: yieldInput
                            Layout.fillWidth: true
                            text: root.fYield
                            placeholder: TranslationManager.translate("changebeans.form.grams", "g")
                            accessibleName: TranslationManager.translate("changebeans.form.yield.accessible", "Yield target in grams")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextEdited: root.fYield = text
                        }
                    }

                    // Starting weight / notes / grinder hardware / freeze —
                    // always visible (the "More options" expander was removed:
                    // it only added a click).
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        FieldRow {
                            labelKey: "changebeans.form.startWeight"
                            labelFallback: "Bag size:"

                            StyledTextField {
                                id: startWeightInput
                                Layout.fillWidth: true
                                text: root.fStartWeight
                                placeholder: TranslationManager.translate("changebeans.form.startWeight.placeholder", "Starting weight (g)")
                                accessibleName: TranslationManager.translate("changebeans.form.startWeight.accessible", "Starting weight in grams")
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onTextEdited: root.fStartWeight = text
                            }
                        }

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

                        // Grinder hardware (rarely changes — tucked away)
                        FieldRow {
                            labelKey: "changebeans.form.grinder"
                            labelFallback: "Grinder:"

                            StyledTextField {
                                id: grinderBrandInput
                                Layout.fillWidth: true
                                text: root.fGrinderBrand
                                placeholder: TranslationManager.translate("changebeans.form.grinderBrand.placeholder", "Brand")
                                accessibleName: TranslationManager.translate("changebeans.form.grinderBrand.accessible", "Grinder brand")
                                onTextEdited: root.fGrinderBrand = text
                            }

                            StyledTextField {
                                id: grinderModelInput
                                Layout.fillWidth: true
                                text: root.fGrinderModel
                                placeholder: TranslationManager.translate("changebeans.form.grinderModel.placeholder", "Model")
                                accessibleName: TranslationManager.translate("changebeans.form.grinderModel.accessible", "Grinder model")
                                onTextEdited: root.fGrinderModel = text
                            }

                            StyledTextField {
                                id: grinderBurrsInput
                                Layout.fillWidth: true
                                text: root.fGrinderBurrs
                                placeholder: TranslationManager.translate("changebeans.form.grinderBurrs.placeholder", "Burrs")
                                accessibleName: TranslationManager.translate("changebeans.form.grinderBurrs.accessible", "Grinder burrs")
                                onTextEdited: root.fGrinderBurrs = text
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
}
