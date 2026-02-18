import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: postShotReviewPage
    objectName: "postShotReviewPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("postshotreview.title", "Shot Review")
        if (editShotId > 0) {
            loadShotForEditing()
        }
    }
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("postshotreview.title", "Shot Review")

    property int editShotId: 0  // Shot ID to edit (always use edit mode now)
    property var editShotData: ({})  // Loaded shot data when editing
    property bool isEditMode: editShotId > 0
    // Persisted graph height (like ShotComparisonPage)
    property real graphHeight: Settings.value("postShotReview/graphHeight", Theme.scaled(200))

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
            editEnjoyment = editShotData.enjoyment ?? 0  // Use ?? to avoid treating 0 as falsy
            editNotes = editShotData.espressoNotes || ""
            editBeverageType = editShotData.beverageType || "espresso"
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
    property int editEnjoyment: 0  // 0 = unrated
    property string editNotes: ""
    property string editBeverageType: "espresso"

    // Auto-calculate EY from TDS, dose weight, and beverage weight
    // Formula: EY(%) = (beverageWeight × TDS%) / doseWeight
    function calculateEy() {
        if (editDoseWeight > 0 && editDrinkWeight > 0 && editDrinkTds > 0) {
            var ey = (editDrinkWeight * editDrinkTds) / editDoseWeight
            ey = Math.round(ey * 10) / 10  // Round to 1 decimal
            editDrinkEy = ey
            eyInput.value = ey
        }
    }

    // Track if any edits were made
    property bool hasUnsavedChanges: isEditMode && (
        editBeanBrand !== (editShotData.beanBrand || "") ||
        editBeanType !== (editShotData.beanType || "") ||
        editRoastDate !== (editShotData.roastDate || "") ||
        editRoastLevel !== (editShotData.roastLevel || "") ||
        editGrinderModel !== (editShotData.grinderModel || "") ||
        editGrinderSetting !== (editShotData.grinderSetting || "") ||
        editBarista !== (editShotData.barista || "") ||
        editDoseWeight !== (editShotData.doseWeight || 0) ||
        editDrinkWeight !== (editShotData.finalWeight || 0) ||
        editDrinkTds !== (editShotData.drinkTds || 0) ||
        editDrinkEy !== (editShotData.drinkEy || 0) ||
        editEnjoyment !== (editShotData.enjoyment ?? 0) ||
        editNotes !== (editShotData.espressoNotes || "") ||
        editBeverageType !== (editShotData.beverageType || "espresso")
    )

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
            "espressoNotes": editNotes,
            "beverageType": editBeverageType
        }
        MainController.shotHistory.updateShotMetadata(editShotId, metadata)

        // Sync sticky metadata back to Settings (bean/grinder info) for the next shot
        // but NOT enjoyment/notes which are shot-specific
        Settings.dyeBeanBrand = editBeanBrand
        Settings.dyeBeanType = editBeanType
        Settings.dyeRoastDate = editRoastDate
        Settings.dyeRoastLevel = editRoastLevel
        Settings.dyeGrinderModel = editGrinderModel
        Settings.dyeGrinderSetting = editGrinderSetting
        Settings.dyeBarista = editBarista
        Settings.dyeBeanWeight = editDoseWeight
        Settings.dyeDrinkWeight = editDrinkWeight
        Settings.dyeDrinkTds = editDrinkTds
        Settings.dyeDrinkEy = editDrinkEy
        // Note: enjoyment and notes are NOT synced back - they're shot-specific

        // Reload the shot data to mark changes as saved (clears hasUnsavedChanges)
        loadShotForEditing()
    }

    // Handle upload status changes
    Connections {
        target: MainController.visualizer
        function onUploadingChanged() {
            if (AccessibilityManager.enabled) {
                if (MainController.visualizer.uploading) {
                    AccessibilityManager.announce(TranslationManager.translate("postshotreview.accessible.uploadingtovisualizer", "Uploading to Visualizer"), true)
                }
            }
        }
        function onLastUploadStatusChanged() {
            if (AccessibilityManager.enabled && MainController.visualizer.lastUploadStatus.length > 0) {
                AccessibilityManager.announce(MainController.visualizer.lastUploadStatus, true)
            }
        }
        function onUploadSuccess(shotId, url) {
            // Update the shot history with visualizer info
            if (editShotId > 0) {
                MainController.shotHistory.updateVisualizerInfo(editShotId, shotId, url)
                // Reload shot data to update the UI
                loadShotForEditing()
            }
        }
        function onUpdateSuccess(visualizerId) {
            // Reload shot data to refresh the UI after metadata update
            if (editShotId > 0) {
                loadShotForEditing()
            }
        }
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        textFields: [
            roasterField.textField, coffeeField.textField, roastDateField.textField,
            grinderField.textField, settingField.textField, baristaField.textField,
            notesField
        ]

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentHeight: mainColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: mainColumn
            width: parent.width
            spacing: Theme.scaled(6)

            // Resizable Graph (visible when we have shot data)
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), postShotReviewPage.graphHeight))
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: editShotData.pressure && editShotData.pressure.length > 0

                HistoryShotGraph {
                    id: reviewGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    pressureData: editShotData.pressure || []
                    flowData: editShotData.flow || []
                    temperatureData: editShotData.temperature || []
                    weightData: editShotData.weight || []
                    weightFlowRateData: editShotData.weightFlowRate || []
                    phaseMarkers: editShotData.phases || []
                    maxTime: editShotData.duration || 60
                }

                // Tap-to-announce overlay (reads out curve values at tapped position)
                MouseArea {
                    anchors.fill: reviewGraph
                    onClicked: function(mouse) {
                        reviewGraph.announceAtPosition(mouse.x, mouse.y)
                    }
                }

                // Resize handle at bottom
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.ignored: true

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
                            startY = mouse.y + resizeHandle.mapToItem(postShotReviewPage, 0, 0).y
                            startHeight = graphCard.Layout.preferredHeight
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(postShotReviewPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                // Clamp between min and max
                                newHeight = Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), newHeight))
                                postShotReviewPage.graphHeight = newHeight
                            }
                        }

                        onReleased: {
                            // Save the height
                            Settings.setValue("postShotReview/graphHeight", postShotReviewPage.graphHeight)
                        }
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
                    id: roasterField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.roaster", "Roaster")
                    text: editBeanBrand
                    suggestions: MainController.shotHistory.getDistinctBeanBrands()
                    onTextEdited: function(t) { editBeanBrand = t }
                }

                SuggestionField {
                    id: coffeeField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.coffee", "Coffee")
                    text: editBeanType
                    suggestions: MainController.shotHistory.getDistinctBeanTypesForBrand(editBeanBrand)
                    onTextEdited: function(t) { editBeanType = t }
                }

                LabeledField {
                    id: roastDateField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.roastdate", "Roast date (yyyy-mm-dd)")
                    text: editRoastDate
                    inputHints: Qt.ImhDate
                    inputMask: "9999-99-99"
                    onTextEdited: function(t) { editRoastDate = t }
                }

                // === ROW 2: Roast level, Grinder ===
                LabeledComboBox {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.roastlevel", "Roast level")
                    model: ["",
                        TranslationManager.translate("postshotreview.roastlevel.light", "Light"),
                        TranslationManager.translate("postshotreview.roastlevel.mediumlight", "Medium-Light"),
                        TranslationManager.translate("postshotreview.roastlevel.medium", "Medium"),
                        TranslationManager.translate("postshotreview.roastlevel.mediumdark", "Medium-Dark"),
                        TranslationManager.translate("postshotreview.roastlevel.dark", "Dark")]
                    currentValue: editRoastLevel
                    onValueChanged: function(v) { editRoastLevel = v }
                }

                SuggestionField {
                    id: grinderField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.grinder", "Grinder")
                    text: editGrinderModel
                    suggestions: MainController.shotHistory.getDistinctGrinders()
                    onTextEdited: function(t) { editGrinderModel = t }
                }

                SuggestionField {
                    id: settingField
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.setting", "Setting")
                    text: editGrinderSetting
                    suggestions: MainController.shotHistory.getDistinctGrinderSettingsForGrinder(editGrinderModel)
                    onTextEdited: function(t) { editGrinderSetting = t }
                }

                // === ROW 3: Beverage type, Barista, Preset, Shot Date ===
                LabeledComboBox {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.beveragetype", "Beverage type")
                    model: ["espresso", "filter", "pourover", "tea_portafilter", "tea", "calibrate", "cleaning", "descale", "manual"]
                    currentValue: editBeverageType
                    onValueChanged: function(v) { editBeverageType = v }
                }

                SuggestionField {
                    id: baristaField
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    label: TranslationManager.translate("postshotreview.label.barista", "Barista")
                    text: editBarista
                    suggestions: MainController.shotHistory.getDistinctBaristas()
                    onTextEdited: function(t) { editBarista = t }
                }

                // Preset (read-only display)
                Item {
                    Layout.fillWidth: true
                    implicitHeight: presetLabel.height + presetValue.height + 2

                    Text {
                        id: presetLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: TranslationManager.translate("postshotreview.label.preset", "Preset")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: presetValue
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: presetLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        height: Theme.scaled(48)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: Theme.textSecondaryColor
                        border.width: 1

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            text: editShotData.profileName || ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("postshotreview.label.preset", "Preset") + ": " + (editShotData.profileName || "")
                    }
                }

                // Shot date/time (read-only display)
                Item {
                    Layout.fillWidth: true
                    implicitHeight: shotDateLabel.height + shotDateValue.height + 2

                    Text {
                        id: shotDateLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: TranslationManager.translate("postshotreview.label.shotdate", "Shot date")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: shotDateValue
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: shotDateLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        height: Theme.scaled(48)
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: Theme.textSecondaryColor
                        border.width: 1

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            text: editShotData.dateTime || ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("postshotreview.label.shotdate", "Shot date") + ": " + (editShotData.dateTime || "")
                    }
                }

                // === ROW 4: Measurements (4 ValueInputs spanning 3 columns) ===
                Item {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredHeight: measurementsLabel.height + measurementsRow.height + 4

                    Tr {
                        id: measurementsLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        key: "postshotreview.section.measurements"
                        fallback: "Measurements"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    RowLayout {
                        id: measurementsRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: measurementsLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        spacing: Theme.scaled(6)

                        // Dose (bean weight)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "postshotreview.label.dose"
                                fallback: "Dose"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }
                            ValueInput {
                                id: doseInput
                                Layout.fillWidth: true
                                height: Theme.scaled(40)
                                from: 0
                                to: 40
                                stepSize: 0.1
                                decimals: 1
                                suffix: "g"
                                valueColor: Theme.dyeDoseColor
                                value: editDoseWeight
                                accessibleName: TranslationManager.translate("postshotreview.label.dose", "Dose") + " " + value + " " + TranslationManager.translate("postshotreview.unit.grams", "grams")
                                onValueModified: function(newValue) {
                                    doseInput.value = newValue
                                    editDoseWeight = newValue
                                    calculateEy()
                                }
                                onActiveFocusChanged: if (activeFocus) Qt.inputMethod.hide()
                            }
                        }

                        // Out (drink weight)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "postshotreview.label.out"
                                fallback: "Out"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }
                            ValueInput {
                                id: outInput
                                Layout.fillWidth: true
                                height: Theme.scaled(40)
                                from: 0
                                to: 500
                                stepSize: 0.1
                                decimals: 1
                                suffix: "g"
                                valueColor: Theme.dyeOutputColor
                                value: editDrinkWeight
                                accessibleName: TranslationManager.translate("postshotreview.accessible.output", "Output") + " " + value + " " + TranslationManager.translate("postshotreview.unit.grams", "grams")
                                onValueModified: function(newValue) {
                                    outInput.value = newValue
                                    editDrinkWeight = newValue
                                    calculateEy()
                                }
                                onActiveFocusChanged: if (activeFocus) Qt.inputMethod.hide()
                            }
                        }

                        // TDS
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "postshotreview.label.tds"
                                fallback: "TDS"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }
                            ValueInput {
                                id: tdsInput
                                Layout.fillWidth: true
                                height: Theme.scaled(40)
                                from: 0
                                to: 20
                                stepSize: 0.01
                                decimals: 2
                                suffix: "%"
                                valueColor: Theme.dyeTdsColor
                                value: editDrinkTds
                                accessibleName: TranslationManager.translate("postshotreview.label.tds", "TDS") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                                onValueModified: function(newValue) {
                                    tdsInput.value = newValue
                                    editDrinkTds = newValue
                                    calculateEy()
                                }
                                onActiveFocusChanged: if (activeFocus) Qt.inputMethod.hide()
                            }
                        }

                        // EY
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "postshotreview.label.ey"
                                fallback: "EY"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }
                            ValueInput {
                                id: eyInput
                                Layout.fillWidth: true
                                height: Theme.scaled(40)
                                from: 0
                                to: 30
                                stepSize: 0.1
                                decimals: 1
                                suffix: "%"
                                valueColor: Theme.dyeEyColor
                                value: editDrinkEy
                                accessibleName: TranslationManager.translate("postshotreview.accessible.extractionyield", "Extraction yield") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                                onValueModified: function(newValue) {
                                    eyInput.value = newValue
                                    editDrinkEy = newValue
                                }
                                onActiveFocusChanged: if (activeFocus) Qt.inputMethod.hide()
                            }
                        }
                    }
                }

                // === ROW 5: Rating (spans 3 columns) ===
                Item {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredHeight: ratingLabel.height + ratingBox.height + Theme.scaled(2)

                    Tr {
                        id: ratingLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        key: "postshotreview.label.rating"
                        fallback: "Rating"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: ratingBox
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: ratingLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        height: Theme.scaled(44)
                        radius: Theme.scaled(12)
                        color: Theme.surfaceColor
                        border.width: 1
                        border.color: Theme.textSecondaryColor

                        RatingInput {
                            id: ratingInput
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(4)
                            value: editEnjoyment
                            accessibleName: TranslationManager.translate("postshotreview.label.rating", "Rating") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                            onValueModified: function(newValue) {
                                editEnjoyment = newValue
                            }
                        }
                    }
                }

                // === ROW 6: Notes (spans 3 columns) ===
                Item {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredHeight: notesLabel.height + notesField.height + 2

                    Tr {
                        id: notesLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        key: "postshotreview.label.notes"
                        fallback: "Notes"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                        Accessible.ignored: true
                    }

                    TextArea {
                        id: notesField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: notesLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        // Size to content with minimum height of 100px
                        height: Math.max(100, contentHeight + topPadding + bottomPadding)
                        text: editNotes
                        font: Theme.bodyFont
                        color: Theme.textColor
                        placeholderTextColor: Theme.textSecondaryColor
                        wrapMode: TextEdit.Wrap
                        leftPadding: 8; rightPadding: 8; topPadding: 6; bottomPadding: Theme.scaled(6)
                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: notesField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1
                        }
                        onTextChanged: editNotes = text

                        Accessible.role: Accessible.EditableText
                        Accessible.name: TranslationManager.translate("postshotreview.label.notes", "Notes")
                        Accessible.description: text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty")
                        Accessible.focusable: true

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                if (AccessibilityManager.enabled) {
                                    let announcement = TranslationManager.translate("postshotreview.label.notes", "Notes") + ". " + (text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty"))
                                    AccessibilityManager.announce(announcement)
                                }
                            }
                        }

                        // Note: TextArea doesn't support EnterKey.type, but we keep
                        // the multiline behavior. User can tap outside or use back gesture.
                    }
                }
            }

            Item { Layout.preferredHeight: 10 }
        }
    }

    } // KeyboardAwareContainer

    // Bottom bar (stays visible under keyboard)
    BottomBar {
        onBackClicked: {
            if (hasUnsavedChanges) {
                unsavedChangesDialog.open()
            } else {
                // If editing from history, pop back to history; otherwise go to idle
                if (isEditMode) {
                    pageStack.pop()
                } else {
                    goToIdle()
                }
            }
        }

        // Save button - only visible when there are unsaved changes
        Rectangle {
            id: saveButton
            visible: hasUnsavedChanges
            Layout.preferredWidth: saveButtonContent.width + 40
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: saveArea.pressed ? Qt.darker("#2E7D32", 1.2) : "#2E7D32"  // Dark green

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("postshotreview.button.save", "Save Changes")
            Accessible.focusable: true
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
                    Accessible.ignored: true
                }

                Tr {
                    key: "postshotreview.button.save"
                    fallback: "Save Changes"
                    color: "white"
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: saveArea
                anchors.fill: parent
                onClicked: saveEditedShot()
            }
        }

        // Upload / Re-Upload to Visualizer button
        Rectangle {
            id: uploadButton
            visible: editShotData.id > 0 && !MainController.visualizer.uploading
            Layout.preferredWidth: uploadButtonContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: uploadArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: editShotData.visualizerId
                ? TranslationManager.translate("postshotreview.button.reupload", "Re-Upload to Visualizer")
                : TranslationManager.translate("postshotreview.button.upload", "Upload to Visualizer")
            Accessible.focusable: true
            Accessible.onPressAction: uploadArea.clicked(null)

            Row {
                id: uploadButtonContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Text {
                    text: "\u2601"  // Cloud icon
                    font.pixelSize: Theme.scaled(16)
                    color: "white"
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }

                Tr {
                    key: editShotData.visualizerId
                         ? "postshotreview.button.reupload"
                         : "postshotreview.button.upload"
                    fallback: editShotData.visualizerId
                              ? "Re-Upload to Visualizer"
                              : "Upload to Visualizer"
                    color: "white"
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: uploadArea
                anchors.fill: parent
                onClicked: {
                    // Auto-save any unsaved changes before uploading
                    if (hasUnsavedChanges) {
                        saveEditedShot()
                    }

                    if (editShotData.visualizerId) {
                        // Re-upload: PATCH metadata from current edit fields
                        var currentData = {
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
                            "espressoNotes": editNotes,
                            "profileName": editShotData.profileName || ""
                        }
                        MainController.visualizer.updateShotOnVisualizer(
                            editShotData.visualizerId, currentData)
                    } else {
                        // First upload: POST full shot data
                        MainController.visualizer.uploadShotFromHistory(
                            MainController.shotHistory.getShot(editShotId))
                    }
                }
            }
        }

        // Uploading/Updating indicator
        Tr {
            visible: MainController.visualizer.uploading
            key: editShotData.visualizerId
                 ? "postshotreview.status.updating"
                 : "postshotreview.status.uploading"
            fallback: editShotData.visualizerId ? "Updating..." : "Uploading..."
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }

        // AI Advice button - visible when we have shot data
        Rectangle {
            id: aiAdviceButton
            visible: MainController.aiManager && editShotData.id > 0
            Layout.preferredWidth: aiAdviceContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: MainController.aiManager && MainController.aiManager.isConfigured
                   ? Theme.primaryColor : Theme.surfaceColor
            opacity: MainController.aiManager && MainController.aiManager.isAnalyzing ? 0.6 : 1.0

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("postshotreview.accessible.getaiadvice", "Get AI Advice")
            Accessible.focusable: true
            Accessible.onPressAction: aiAdviceArea.clicked(null)

            Row {
                id: aiAdviceContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                    Accessible.ignored: true
                }

                Tr {
                    key: MainController.aiManager && MainController.aiManager.isAnalyzing
                          ? "postshotreview.button.analyzing" : "postshotreview.button.aiadvice"
                    fallback: MainController.aiManager && MainController.aiManager.isAnalyzing
                          ? "Analyzing..." : "AI Advice"
                    color: MainController.aiManager && MainController.aiManager.isConfigured
                           ? "white" : Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

                MouseArea {
                id: aiAdviceArea
                anchors.fill: parent
                enabled: MainController.aiManager && MainController.aiManager.isConfigured && !MainController.aiManager.isAnalyzing
                onClicked: {
                    conversationOverlay.openWithShot(editShotData, editBeanBrand, editBeanType, editShotData.profileName, editShotId)
                }
            }
        }

        // Email Prompt button - fallback for users without API keys
        Rectangle {
            id: emailPromptButton
            visible: MainController.aiManager && !MainController.aiManager.isConfigured && editShotData.id > 0
            Layout.preferredWidth: emailPromptContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: Theme.surfaceColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("postshotreview.accessible.emailprompt", "Email AI prompt to yourself")
            Accessible.focusable: true
            Accessible.onPressAction: emailPromptArea.clicked(null)

            Row {
                id: emailPromptContent
                anchors.centerIn: parent
                spacing: Theme.scaled(6)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    width: Theme.scaled(18)
                    height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: status === Image.Ready
                    opacity: 0.6
                    Accessible.ignored: true
                }

                Tr {
                    key: "postshotreview.button.emailprompt"
                    fallback: "Email Prompt"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: emailPromptArea
                anchors.fill: parent
                onClicked: {
                    var prompt = MainController.aiManager.generateHistoryShotSummary(editShotData)
                    // Open mailto: with prompt in body
                    Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent("Espresso Shot Analysis") +
                                        "&body=" + encodeURIComponent(prompt))
                }
            }
        }

    }

    // === Inline Components ===

    component LabeledField: Item {
        property string label: ""
        property string text: ""
        property int inputHints: Qt.ImhNone
        property string inputMask: ""
        property alias textField: fieldInput  // Expose for KeyboardAwareContainer registration
        signal textEdited(string text)

        implicitHeight: fieldLabel.height + fieldInput.height + 2

        Text {
            id: fieldLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(11)
            Accessible.ignored: true
        }

        StyledTextField {
            id: fieldInput
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: fieldLabel.bottom
            anchors.topMargin: Theme.scaled(2)
            text: parent.text
            inputMethodHints: parent.inputHints
            inputMask: parent.inputMask
            EnterKey.type: Qt.EnterKeyNext
            Keys.onReturnPressed: nextItemInFocusChain().forceActiveFocus()
            onTextChanged: parent.textEdited(text)
            onActiveFocusChanged: {
                if (activeFocus) {
                    if (AccessibilityManager.enabled) {
                        let announcement = parent.label + ". " + (text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty"))
                        AccessibilityManager.announce(announcement)
                    }
                }
            }

            Accessible.role: Accessible.EditableText
            Accessible.name: parent.label
            Accessible.description: text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty")
        }
    }

    component LabeledComboBox: Item {
        property string label: ""
        property var model: []
        property string currentValue: ""
        signal valueChanged(string value)

        implicitHeight: comboLabel.height + 48 + 2

        Text {
            id: comboLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(11)
            Accessible.ignored: true
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
            font.pixelSize: Theme.scaled(14)

            Accessible.role: Accessible.ComboBox
            Accessible.name: parent.label
            Accessible.description: currentIndex > 0 ? currentText : TranslationManager.translate("postshotreview.accessible.notset", "Not set")

            onActiveFocusChanged: {
                if (activeFocus && AccessibilityManager.enabled) {
                    let value = currentIndex > 0 ? currentText : TranslationManager.translate("postshotreview.accessible.notset", "Not set")
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
                font.pixelSize: Theme.scaled(14)
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
                height: Theme.scaled(32)
                contentItem: Text {
                    text: modelData || TranslationManager.translate("postshotreview.option.none", "(None)")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: highlighted ? Theme.primaryColor : Theme.surfaceColor
                }
                highlighted: combo.highlightedIndex === index

                Accessible.role: Accessible.ListItem
                Accessible.name: modelData || TranslationManager.translate("postshotreview.accessible.none", "None")
            }

            popup: Popup {
                y: combo.height
                width: combo.width
                implicitHeight: Math.min(contentItem.implicitHeight, 200)
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

    // Unsaved changes dialog for edit mode
    UnsavedChangesDialog {
        id: unsavedChangesDialog
        itemType: "shot"
        showSaveAs: false
        onDiscardClicked: {
            if (isEditMode) {
                pageStack.pop()
            } else {
                goToIdle()
            }
        }
        onSaveClicked: {
            saveEditedShot()
            if (isEditMode) {
                pageStack.pop()
            } else {
                goToIdle()
            }
        }
    }

    ConversationOverlay {
        id: conversationOverlay
        anchors.fill: parent
        overlayTitle: TranslationManager.translate("postshotreview.conversation.title", "Dialing Conversation")
    }
}
