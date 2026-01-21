import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotMetadataPage
    objectName: "shotMetadataPage"
    background: Rectangle { color: Theme.backgroundColor }

    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("beaninfo.title", "Beans")

    property bool hasPendingShot: false  // Set to true by goToShotMetadata() after a shot
    property int editShotId: 0  // Set > 0 to edit existing shot from history
    property var editShotData: ({})  // Loaded shot data when editing
    property bool isEditMode: editShotId > 0
    property bool keyboardVisible: Qt.inputMethod.visible
    property Item focusedField: null

    // Preset dialog properties
    property int editPresetIndex: -1
    property string editPresetName: ""

    // Keyboard offset for popups - set when popup opens, reset when it closes
    property real popupKeyboardOffset: 0

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("beaninfo.title", "Beans")
        if (editShotId > 0) {
            loadShotForEditing()
        }
    }

    // Persisted graph height (like ShotComparisonPage)
    property real graphHeight: Settings.value("shotMetadata/graphHeight", Theme.scaled(200))

    // Load shot data for editing
    function loadShotForEditing() {
        if (editShotId <= 0) return
        editShotData = MainController.shotHistory.getShot(editShotId)
        if (editShotData.id) {
            // Populate editing fields
            editBeanBrand = editShotData.beanBrand || ""
            editBeanType = editShotData.beanType || ""
            editRoastDate = editShotData.roastDate || ""
            editRoastLevel = editShotData.roastLevel || ""
            editGrinderModel = editShotData.grinderModel || ""
            editGrinderSetting = editShotData.grinderSetting || ""
            editBarista = editShotData.barista || ""
            editDoseWeight = editShotData.doseWeight || 0
            editDrinkWeight = editShotData.finalWeight || 0
            editDrinkTds = editShotData.drinkTds || 0
            editDrinkEy = editShotData.drinkEy || 0
            editEnjoyment = editShotData.enjoyment || 75
            editNotes = editShotData.espressoNotes || ""
        }
    }

    // Editing fields (separate from Settings.dye* to avoid polluting current session)
    property string editBeanBrand: ""
    property string editBeanType: ""
    property string editRoastDate: ""
    property string editRoastLevel: ""
    property string editGrinderModel: ""
    property string editGrinderSetting: ""
    property string editBarista: ""
    property double editDoseWeight: 0
    property double editDrinkWeight: 0
    property double editDrinkTds: 0
    property double editDrinkEy: 0
    property int editEnjoyment: 75
    property string editNotes: ""

    // Save edited shot back to history
    function saveEditedShot() {
        if (editShotId <= 0) return
        var metadata = {
            "beanBrand": editBeanBrand,
            "beanType": editBeanType,
            "roastDate": editRoastDate,
            "roastLevel": editRoastLevel,
            "grinderModel": editGrinderModel,
            "grinderSetting": editGrinderSetting,
            "barista": editBarista,
            "doseWeight": editDoseWeight,
            "finalWeight": editDrinkWeight,
            "drinkTds": editDrinkTds,
            "drinkEy": editDrinkEy,
            "enjoyment": editEnjoyment,
            "espressoNotes": editNotes
        }
        MainController.shotHistory.updateShotMetadata(editShotId, metadata)
        root.goBack()
    }

    function scrollToFocusedField() {
        if (!focusedField) return
        // Get field center position in content coordinates
        let fieldY = focusedField.mapToItem(flickable.contentItem, 0, 0).y
        let fieldCenter = fieldY + focusedField.height / 2
        // Get keyboard height (real or estimated)
        let kbHeight = Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio
        if (kbHeight <= 0) kbHeight = shotMetadataPage.height * 0.5
        // Calculate center of visible area above keyboard
        let visibleHeight = shotMetadataPage.height - kbHeight - Theme.pageTopMargin
        let visibleCenter = visibleHeight / 2
        // Scroll so field center is at center of visible area
        let targetY = fieldCenter - visibleCenter
        // Clamp to valid scroll range
        let maxScroll = flickable.contentHeight - flickable.height
        flickable.contentY = Math.max(0, Math.min(targetY, maxScroll))
    }

    function hideKeyboard() {
        Qt.inputMethod.hide()
        flickable.contentY = 0
        flickable.forceActiveFocus()
    }

    // Scroll to focused field when it changes
    onFocusedFieldChanged: {
        if (focusedField) {
            scrollTimer.restart()
        }
    }

    Timer {
        id: scrollTimer
        interval: 150
        onTriggered: scrollToFocusedField()
    }

    // Reset focusedField when focus leaves all text fields
    Timer {
        id: focusResetTimer
        interval: 100
        onTriggered: {
            if (focusedField && !focusedField.activeFocus) {
                focusedField = null
                flickable.contentY = 0
            }
        }
    }

    // Announce upload status changes
    Connections {
        target: MainController.visualizer
        function onUploadingChanged() {
            if (AccessibilityManager.enabled) {
                if (MainController.visualizer.uploading) {
                    AccessibilityManager.announce(TranslationManager.translate("shotmetadata.accessible.uploadingtovisualizer", "Uploading to Visualizer"), true)
                }
            }
        }
        function onLastUploadStatusChanged() {
            if (AccessibilityManager.enabled && MainController.visualizer.lastUploadStatus.length > 0) {
                AccessibilityManager.announce(MainController.visualizer.lastUploadStatus, true)
            }
        }
    }

    // Tap background to dismiss keyboard
    MouseArea {
        anchors.fill: parent
        visible: focusedField !== null
        onClicked: {
            focusedField = null
            hideKeyboard()
        }
        z: -1
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        // Add bottom padding for keyboard: use real height if available, else estimate when focused
        property real kbHeight: Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio
        contentHeight: mainColumn.height + (kbHeight > 0 ? kbHeight : (focusedField ? shotMetadataPage.height * 0.5 : 0))
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: mainColumn
            width: parent.width
            spacing: Theme.scaled(6)

            // Resizable Graph (only visible in edit mode when editing a shot from history)
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), shotMetadataPage.graphHeight))
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: isEditMode && editShotData.pressure && editShotData.pressure.length > 0

                HistoryShotGraph {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    pressureData: editShotData.pressure || []
                    flowData: editShotData.flow || []
                    temperatureData: editShotData.temperature || []
                    weightData: editShotData.weight || []
                    phaseMarkers: editShotData.phases || []
                    maxTime: editShotData.duration || 60
                }

                // Resize handle at bottom
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"

                    // Visual indicator (three lines)
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Repeater {
                            model: 3
                            Rectangle {
                                width: Theme.scaled(30)
                                height: 1
                                color: Theme.textSecondaryColor
                                opacity: resizeMouseArea.containsMouse || resizeMouseArea.pressed ? 0.8 : 0.4
                            }
                        }
                    }

                    MouseArea {
                        id: resizeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeVerCursor
                        preventStealing: true

                        property real startY: 0
                        property real startHeight: 0

                        onPressed: function(mouse) {
                            startY = mouse.y + resizeHandle.mapToItem(shotMetadataPage, 0, 0).y
                            startHeight = graphCard.Layout.preferredHeight
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(shotMetadataPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                // Clamp between min and max
                                newHeight = Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), newHeight))
                                shotMetadataPage.graphHeight = newHeight
                            }
                        }

                        onReleased: {
                            // Save the height
                            Settings.setValue("shotMetadata/graphHeight", shotMetadataPage.graphHeight)
                        }
                    }
                }
            }

            // Bean presets section (only in non-edit mode)
            Rectangle {
                id: presetsCard
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(90)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: !isEditMode

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "beaninfo.presets.title"
                        fallback: "Bean Preset"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                    }

                    // Bean preset pills with drag-and-drop
                    Row {
                        id: beanPresetsRow
                        spacing: Theme.scaled(8)

                        property int draggedIndex: -1
                        // Store reference to Settings for use in deeply nested delegates
                        property var settings: Settings

                        Repeater {
                            id: beanRepeater
                            model: Settings.beanPresets

                            Item {
                                id: beanDelegate
                                width: beanPill.width
                                height: Theme.scaled(36)

                                property int beanIndex: index

                                Rectangle {
                                    id: beanPill
                                    width: beanText.implicitWidth + 24
                                    height: Theme.scaled(36)
                                    radius: Theme.scaled(18)
                                    color: beanDelegate.beanIndex === Settings.selectedBeanPreset ? Theme.primaryColor : Theme.backgroundColor
                                    border.color: beanDelegate.beanIndex === Settings.selectedBeanPreset ? Theme.primaryColor : Theme.textSecondaryColor
                                    border.width: 1
                                    opacity: beanDragArea.drag.active ? 0.8 : 1.0

                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.name + " " + TranslationManager.translate("beaninfo.accessibility.preset", "preset") +
                                                     (beanDelegate.beanIndex === Settings.selectedBeanPreset ?
                                                      ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                                    Accessible.focusable: true

                                    Drag.active: beanDragArea.drag.active
                                    Drag.source: beanDelegate
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2

                                    states: State {
                                        when: beanDragArea.drag.active
                                        ParentChange { target: beanPill; parent: beanPresetsRow }
                                        AnchorChanges { target: beanPill; anchors.verticalCenter: undefined }
                                    }

                                    Text {
                                        id: beanText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        color: beanDelegate.beanIndex === Settings.selectedBeanPreset ? "white" : Theme.textColor
                                        font: Theme.bodyFont
                                    }

                                    MouseArea {
                                        id: beanDragArea
                                        anchors.fill: parent
                                        drag.target: beanPill
                                        drag.axis: Drag.XAxis

                                        property bool held: false
                                        property bool moved: false

                                        onPressed: {
                                            held = false
                                            moved = false
                                            beanHoldTimer.start()
                                        }

                                        onReleased: {
                                            beanHoldTimer.stop()
                                            var s = beanPresetsRow.settings
                                            if (!moved && !held) {
                                                // Remove focus from any text fields first to ensure clean state
                                                shotMetadataPage.forceActiveFocus()

                                                // Save current values to the previously selected preset before switching
                                                if (s.selectedBeanPreset >= 0 && s.selectedBeanPreset !== beanDelegate.beanIndex) {
                                                    var oldPreset = s.getBeanPreset(s.selectedBeanPreset)
                                                    s.updateBeanPreset(s.selectedBeanPreset,
                                                        oldPreset.name || "",
                                                        s.dyeBeanBrand,
                                                        s.dyeBeanType,
                                                        s.dyeRoastDate,
                                                        s.dyeRoastLevel,
                                                        s.dyeGrinderModel,
                                                        s.dyeGrinderSetting)
                                                }
                                                // Now select and apply the new preset
                                                s.selectedBeanPreset = beanDelegate.beanIndex
                                                s.applyBeanPreset(beanDelegate.beanIndex)
                                            }
                                            beanPill.Drag.drop()
                                            beanPresetsRow.draggedIndex = -1
                                        }

                                        onPositionChanged: {
                                            if (drag.active) {
                                                moved = true
                                                beanPresetsRow.draggedIndex = beanDelegate.beanIndex
                                            }
                                        }

                                        onDoubleClicked: {
                                            beanHoldTimer.stop()
                                            held = true  // Prevent single-click selection on release
                                            editPresetIndex = beanDelegate.beanIndex
                                            var preset = beanPresetsRow.settings.getBeanPreset(beanDelegate.beanIndex)
                                            editPresetName = preset.name || ""
                                            editPresetDialog.open()
                                        }

                                        Timer {
                                            id: beanHoldTimer
                                            interval: 500
                                            onTriggered: {
                                                if (!beanDragArea.moved) {
                                                    beanDragArea.held = true
                                                    editPresetIndex = beanDelegate.beanIndex
                                                    var preset = beanPresetsRow.settings.getBeanPreset(beanDelegate.beanIndex)
                                                    editPresetName = preset.name || ""
                                                    editPresetDialog.open()
                                                }
                                            }
                                        }
                                    }
                                }

                                DropArea {
                                    anchors.fill: parent
                                    onEntered: function(drag) {
                                        var fromIndex = drag.source.beanIndex
                                        var toIndex = beanDelegate.beanIndex
                                        if (fromIndex !== toIndex) {
                                            beanPresetsRow.settings.moveBeanPreset(fromIndex, toIndex)
                                        }
                                    }
                                }
                            }
                        }

                        // Add button
                        Rectangle {
                            id: addBeanButton
                            width: Theme.scaled(36)
                            height: Theme.scaled(36)
                            radius: Theme.scaled(18)
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                            }

                            AccessibleTapHandler {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("beaninfo.accessibility.addpreset", "Add new bean preset")
                                accessibleItem: addBeanButton
                                onAccessibleClicked: savePresetDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        key: "beaninfo.hint.reorder"
                        fallback: "Drag to reorder, hold or double-click to edit"
                        color: Theme.textSecondaryColor
                        font: Theme.labelFont
                    }
                }
            }

            // 3-column grid for all fields
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 6

                // === ROW 1: Bean info ===
                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roaster", "Roaster")
                    text: isEditMode ? editBeanBrand : Settings.dyeBeanBrand
                    suggestions: MainController.shotHistory.getDistinctBeanBrands()
                    onTextEdited: function(t) { if (isEditMode) editBeanBrand = t; else Settings.dyeBeanBrand = t; }
                    onInputFocused: function(field) { focusedField = field; focusResetTimer.stop() }
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.coffee", "Coffee")
                    text: isEditMode ? editBeanType : Settings.dyeBeanType
                    suggestions: MainController.shotHistory.getDistinctBeanTypes()
                    onTextEdited: function(t) { if (isEditMode) editBeanType = t; else Settings.dyeBeanType = t; }
                    onInputFocused: function(field) { focusedField = field; focusResetTimer.stop() }
                }

                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roastdate", "Roast date")
                    text: isEditMode ? editRoastDate : Settings.dyeRoastDate
                    onTextEdited: function(t) { if (isEditMode) editRoastDate = t; else Settings.dyeRoastDate = t; }
                }

                // === ROW 2: Roast level, Grinder ===
                LabeledComboBox {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roastlevel", "Roast level")
                    model: ["",
                        TranslationManager.translate("shotmetadata.roastlevel.light", "Light"),
                        TranslationManager.translate("shotmetadata.roastlevel.mediumlight", "Medium-Light"),
                        TranslationManager.translate("shotmetadata.roastlevel.medium", "Medium"),
                        TranslationManager.translate("shotmetadata.roastlevel.mediumdark", "Medium-Dark"),
                        TranslationManager.translate("shotmetadata.roastlevel.dark", "Dark")]
                    currentValue: isEditMode ? editRoastLevel : Settings.dyeRoastLevel
                    onValueChanged: function(v) { if (isEditMode) editRoastLevel = v; else Settings.dyeRoastLevel = v; }
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.grinder", "Grinder")
                    text: isEditMode ? editGrinderModel : Settings.dyeGrinderModel
                    suggestions: MainController.shotHistory.getDistinctGrinders()
                    onTextEdited: function(t) { if (isEditMode) editGrinderModel = t; else Settings.dyeGrinderModel = t; }
                    onInputFocused: function(field) { focusedField = field; focusResetTimer.stop() }
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.setting", "Setting")
                    text: isEditMode ? editGrinderSetting : Settings.dyeGrinderSetting
                    suggestions: MainController.shotHistory.getDistinctGrinderSettings()
                    onTextEdited: function(t) { if (isEditMode) editGrinderSetting = t; else Settings.dyeGrinderSetting = t; }
                    onInputFocused: function(field) { focusedField = field; focusResetTimer.stop() }
                }

                // === ROW 3: Barista (spans all 3 columns) ===
                SuggestionField {
                    Layout.fillWidth: true
                    Layout.columnSpan: 3
                    label: TranslationManager.translate("shotmetadata.label.barista", "Barista")
                    text: isEditMode ? editBarista : Settings.dyeBarista
                    suggestions: MainController.shotHistory.getDistinctBaristas()
                    onTextEdited: function(t) { if (isEditMode) editBarista = t; else Settings.dyeBarista = t; }
                    onInputFocused: function(field) { focusedField = field; focusResetTimer.stop() }
                }
            }

            Item { Layout.preferredHeight: 10 }
        }
    }

    // Hide keyboard button - appears below status bar when a field has focus
    Rectangle {
        id: hideKeyboardButton
        visible: focusedField !== null
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin + 4
        width: hideKeyboardText.width + 24
        height: Theme.scaled(28)
        radius: Theme.scaled(14)
        color: Theme.primaryColor
        z: 100

        Tr {
            id: hideKeyboardText
            anchors.centerIn: parent
            key: "shotmetadata.button.hidekeyboard"
            fallback: "Hide keyboard"
            color: "white"
            font.pixelSize: Theme.scaled(13)
            font.bold: true
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                focusedField = null
                hideKeyboard()
            }
        }
    }

    // Bottom bar
    BottomBar {
        barColor: "transparent"

        onBackClicked: {
            // Remove focus first to ensure any text field changes are committed
            shotMetadataPage.forceActiveFocus()

            // Save current values to the selected preset (if any)
            if (!isEditMode && Settings.selectedBeanPreset >= 0) {
                var preset = Settings.getBeanPreset(Settings.selectedBeanPreset)
                Settings.updateBeanPreset(Settings.selectedBeanPreset,
                    preset.name || "",
                    Settings.dyeBeanBrand,
                    Settings.dyeBeanType,
                    Settings.dyeRoastDate,
                    Settings.dyeRoastLevel,
                    Settings.dyeGrinderModel,
                    Settings.dyeGrinderSetting)
            }
            root.goBack()
        }

        // Save button - visible in edit mode only
        Rectangle {
            id: saveButton
            visible: isEditMode
            Layout.preferredWidth: saveButtonContent.width + 40
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: saveArea.pressed ? Qt.darker("#2E7D32", 1.2) : "#2E7D32"  // Dark green

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("beaninfo.button.save", "Save Changes")
            Accessible.onPressAction: saveArea.clicked(null)

            Row {
                id: saveButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Text {
                    text: "\u2713"  // Checkmark
                    font.pixelSize: Theme.scaled(18)
                    font.bold: true
                    color: "white"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Tr {
                    key: "beaninfo.button.save"
                    fallback: "Save Changes"
                    color: "white"
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: saveArea
                anchors.fill: parent
                onClicked: saveEditedShot()
            }
        }
    }

    // === Inline Components ===

    component LabeledField: Item {
        property string label: ""
        property string text: ""
        property int inputHints: Qt.ImhNone
        property string inputMask: ""
        signal textEdited(string text)

        implicitHeight: fieldLabel.height + Theme.scaled(48) + 2

        Text {
            id: fieldLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(14)
        }

        StyledTextField {
            id: fieldInput
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: fieldLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            height: Theme.scaled(48)
            text: parent.text
            inputMethodHints: parent.inputHints
            inputMask: parent.inputMask
            EnterKey.type: Qt.EnterKeyNext
            Keys.onReturnPressed: nextItemInFocusChain().forceActiveFocus()
            onTextChanged: parent.textEdited(text)
            onActiveFocusChanged: {
                if (activeFocus) {
                    focusedField = fieldInput
                    focusResetTimer.stop()
                    if (AccessibilityManager.enabled) {
                        let announcement = parent.label + ". " + (text.length > 0 ? text : TranslationManager.translate("shotmetadata.accessible.empty", "Empty"))
                        AccessibilityManager.announce(announcement)
                    }
                } else {
                    focusResetTimer.restart()
                }
            }

            Accessible.role: Accessible.EditableText
            Accessible.name: parent.label
            Accessible.description: text.length > 0 ? text : TranslationManager.translate("shotmetadata.accessible.empty", "Empty")
        }
    }

    component LabeledComboBox: Item {
        property string label: ""
        property var model: []
        property string currentValue: ""
        signal valueChanged(string value)

        implicitHeight: comboLabel.height + Theme.scaled(48) + 2

        Text {
            id: comboLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(14)
        }

        StyledComboBox {
            id: combo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: comboLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            height: Theme.scaled(48)
            model: parent.model
            currentIndex: Math.max(0, model.indexOf(parent.currentValue))
            font.pixelSize: Theme.scaled(18)

            Accessible.role: Accessible.ComboBox
            Accessible.name: parent.label
            Accessible.description: currentIndex > 0 ? currentText : TranslationManager.translate("shotmetadata.accessible.notset", "Not set")

            onActiveFocusChanged: {
                if (activeFocus && AccessibilityManager.enabled) {
                    let value = currentIndex > 0 ? currentText : TranslationManager.translate("shotmetadata.accessible.notset", "Not set")
                    AccessibilityManager.announce(parent.label + ". " + value)
                }
            }

            background: Rectangle {
                color: Theme.backgroundColor
                radius: Theme.scaled(4)
                border.color: combo.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                border.width: 1
            }

            contentItem: Text {
                text: combo.currentIndex === 0 && combo.model[0] === "" ? parent.parent.label : combo.displayText
                color: Theme.textColor
                font.pixelSize: Theme.scaled(18)
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.scaled(12)
            }

            indicator: Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(12)
                anchors.verticalCenter: parent.verticalCenter
                text: "▼"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(10)
            }

            delegate: ItemDelegate {
                width: combo.width
                height: Theme.scaled(40)
                contentItem: Text {
                    text: modelData || TranslationManager.translate("shotmetadata.option.none", "(None)")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(18)
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.scaled(12)
                }
                background: Rectangle {
                    color: highlighted ? Theme.primaryColor : Theme.surfaceColor
                }
                highlighted: combo.highlightedIndex === index

                Accessible.role: Accessible.ListItem
                Accessible.name: modelData || TranslationManager.translate("shotmetadata.accessible.none", "None")
            }

            popup: Popup {
                y: combo.height
                width: combo.width
                implicitHeight: Math.min(contentItem.implicitHeight, Theme.scaled(250))
                padding: 1
                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: combo.popup.visible ? combo.delegateModel : null
                }
                background: Rectangle {
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    radius: Theme.scaled(4)
                }
            }

            onCurrentTextChanged: parent.valueChanged(currentText)
        }
    }

    // Add Preset Popup
    Popup {
        id: savePresetDialog
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - popupKeyboardOffset
        padding: 20
        modal: true
        focus: true

        onOpened: {
            popupKeyboardOffset = shotMetadataPage.height * 0.25
            newBeanNameInput.text = ""
            newBeanNameInput.forceActiveFocus()
        }

        onClosed: {
            popupKeyboardOffset = 0
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "beaninfo.popup.addPreset"
                fallback: "Add Bean Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: newBeanNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "beaninfo.placeholder.presetName"
                        fallback: "Preset name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: qsTr("Cancel saving bean preset")
                    onClicked: savePresetDialog.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.add", "Add")
                    accessibleName: qsTr("Add new bean preset with current settings")
                    onClicked: {
                        if (newBeanNameInput.text.trim().length > 0) {
                            Settings.saveBeanPresetFromCurrent(newBeanNameInput.text.trim())
                            Settings.selectedBeanPreset = Settings.beanPresets.length - 1
                            newBeanNameInput.text = ""
                            savePresetDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Edit Preset Popup
    Popup {
        id: editPresetDialog
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2 - popupKeyboardOffset
        padding: 20
        modal: true
        focus: true

        onOpened: {
            popupKeyboardOffset = shotMetadataPage.height * 0.25
            editBeanNameInput.text = editPresetName
            editBeanNameInput.forceActiveFocus()
        }

        onClosed: {
            popupKeyboardOffset = 0
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "beaninfo.popup.editPreset"
                fallback: "Edit Bean Preset"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            Rectangle {
                Layout.preferredWidth: Theme.scaled(280)
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.backgroundColor
                border.color: Theme.textSecondaryColor
                border.width: 1
                radius: Theme.scaled(4)

                TextInput {
                    id: editBeanNameInput
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: TextInput.AlignVCenter
                    inputMethodHints: Qt.ImhNoPredictiveText

                    Tr {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        key: "beaninfo.placeholder.presetName"
                        fallback: "Preset name"
                        color: Theme.textSecondaryColor
                        font: parent.font
                        visible: !parent.text && !parent.activeFocus
                    }
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)

                AccessibleButton {
                    id: deleteBeanBtn
                    text: TranslationManager.translate("common.delete", "Delete")
                    accessibleName: qsTr("Delete this bean preset")
                    onClicked: {
                        Settings.removeBeanPreset(editPresetIndex)
                        editPresetDialog.close()
                    }
                    background: Rectangle {
                        implicitWidth: Theme.scaled(100)
                        implicitHeight: Theme.scaled(36)
                        radius: Theme.scaled(6)
                        color: deleteBeanBtn.down ? Qt.darker(Theme.errorColor, 1.1) : Theme.errorColor
                    }
                    contentItem: Text {
                        text: deleteBeanBtn.text
                        font.pixelSize: Theme.scaled(14)
                        font.family: Theme.bodyFont.family
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.scaled(16)
                        rightPadding: Theme.scaled(16)
                    }
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: qsTr("Cancel editing bean preset")
                    onClicked: editPresetDialog.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.save", "Save")
                    accessibleName: qsTr("Save changes to bean preset")
                    onClicked: {
                        if (editBeanNameInput.text.trim().length > 0 && editPresetIndex >= 0) {
                            var preset = Settings.getBeanPreset(editPresetIndex)
                            Settings.updateBeanPreset(editPresetIndex,
                                editBeanNameInput.text.trim(),
                                preset.brand || "",
                                preset.type || "",
                                preset.roastDate || "",
                                preset.roastLevel || "",
                                preset.grinderModel || "",
                                preset.grinderSetting || "")
                        }
                        editPresetDialog.close()
                    }
                }
            }
        }
    }

}
