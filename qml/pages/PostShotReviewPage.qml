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
    property bool keyboardVisible: Qt.inputMethod.visible
    property Item focusedField: null
    property string pendingShotSummary: ""  // Shot summary waiting to be sent with user's question

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

    function scrollToFocusedField() {
        if (!focusedField) return
        // Get field center position in content coordinates
        let fieldY = focusedField.mapToItem(flickable.contentItem, 0, 0).y
        let fieldCenter = fieldY + focusedField.height / 2
        // Get keyboard height (real or estimated)
        let kbHeight = Qt.inputMethod.keyboardRectangle.height / Screen.devicePixelRatio
        if (kbHeight <= 0) kbHeight = postShotReviewPage.height * 0.5
        // Calculate center of visible area above keyboard
        let visibleHeight = postShotReviewPage.height - kbHeight - Theme.pageTopMargin
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
        contentHeight: mainColumn.height + (kbHeight > 0 ? kbHeight : (focusedField ? postShotReviewPage.height * 0.5 : 0))
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
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.roaster", "Roaster")
                    text: editBeanBrand
                    suggestions: MainController.shotHistory.getDistinctBeanBrands()
                    onTextEdited: function(t) { editBeanBrand = t }
                    onInputFocused: function(field) { focusedField = field }
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.coffee", "Coffee")
                    text: editBeanType
                    suggestions: MainController.shotHistory.getDistinctBeanTypesForBrand(editBeanBrand)
                    onTextEdited: function(t) { editBeanType = t }
                    onInputFocused: function(field) { focusedField = field }
                }

                LabeledField {
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
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.grinder", "Grinder")
                    text: editGrinderModel
                    suggestions: MainController.shotHistory.getDistinctGrinders()
                    onTextEdited: function(t) { editGrinderModel = t }
                    onInputFocused: function(field) { focusedField = field }
                }

                SuggestionField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("postshotreview.label.setting", "Setting")
                    text: editGrinderSetting
                    suggestions: MainController.shotHistory.getDistinctGrinderSettingsForGrinder(editGrinderModel)
                    onTextEdited: function(t) { editGrinderSetting = t }
                    onInputFocused: function(field) { focusedField = field }
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
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    label: TranslationManager.translate("postshotreview.label.barista", "Barista")
                    text: editBarista
                    suggestions: MainController.shotHistory.getDistinctBaristas()
                    onTextEdited: function(t) { editBarista = t }
                    onInputFocused: function(field) { focusedField = field }
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
                                }
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
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
                                }
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
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
                                }
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
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
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
                            }
                        }
                    }
                }

                // === ROW 5: Rating (spans 3 columns) ===
                Item {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredHeight: ratingLabel.height + 40 + 2

                    Tr {
                        id: ratingLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        key: "postshotreview.label.rating"
                        fallback: "Rating"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(11)
                    }

                    ValueInput {
                        id: ratingInput
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.scaled(5)
                        anchors.top: ratingLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        height: Theme.scaled(40)
                        from: 0
                        to: 100
                        stepSize: 1
                        decimals: 0
                        suffix: " %"
                        valueColor: Theme.primaryColor  // Blue (default accent)
                        value: editEnjoyment
                        accessibleName: TranslationManager.translate("postshotreview.label.rating", "Rating") + " " + value + " " + TranslationManager.translate("postshotreview.unit.percent", "percent")
                        onValueModified: function(newValue) {
                            ratingInput.value = newValue
                            editEnjoyment = newValue
                        }
                        onActiveFocusChanged: {
                            if (activeFocus) {
                                hideKeyboard()
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

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                focusedField = notesField
                                focusResetTimer.stop()
                                if (AccessibilityManager.enabled) {
                                    let announcement = TranslationManager.translate("postshotreview.label.notes", "Notes") + ". " + (text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty"))
                                    AccessibilityManager.announce(announcement)
                                }
                            } else {
                                focusResetTimer.restart()
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
            key: "postshotreview.button.hidekeyboard"
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
                    key: "postshotreview.button.save"
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
                }
            }

                MouseArea {
                id: aiAdviceArea
                anchors.fill: parent
                enabled: MainController.aiManager && MainController.aiManager.isConfigured && !MainController.aiManager.isAnalyzing
                onClicked: {
                    // Generate shot summary from history shot data
                    postShotReviewPage.pendingShotSummary = MainController.aiManager.generateHistoryShotSummary(editShotData)
                    // Open conversation overlay so user can type their question
                    conversationOverlay.visible = true
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
                }

                Tr {
                    key: "postshotreview.button.emailprompt"
                    fallback: "Email Prompt"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
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
        signal textEdited(string text)

        implicitHeight: fieldLabel.height + fieldInput.height + 2

        Text {
            id: fieldLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: parent.label
            color: Theme.textColor
            font.pixelSize: Theme.scaled(11)
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
                    focusedField = fieldInput
                    focusResetTimer.stop()
                    if (AccessibilityManager.enabled) {
                        let announcement = parent.label + ". " + (text.length > 0 ? text : TranslationManager.translate("postshotreview.accessible.empty", "Empty"))
                        AccessibilityManager.announce(announcement)
                    }
                } else {
                    focusResetTimer.restart()
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

    // Conversation overlay panel - full screen with keyboard awareness
    Rectangle {
        id: conversationOverlay
        visible: false
        anchors.fill: parent
        color: Theme.backgroundColor
        z: 200

        // Consume ALL mouse/touch events - prevent pass-through to background
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            // Don't propagate any events
        }

        KeyboardAwareContainer {
            id: conversationKeyboardContainer
            anchors.fill: parent
            anchors.topMargin: Theme.pageTopMargin
            anchors.bottomMargin: Theme.bottomBarHeight
            textFields: [conversationInput]

            // Main conversation content
            Rectangle {
                anchors.fill: parent
                color: Theme.backgroundColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.standardMargin
                    spacing: Theme.spacingSmall

                    // Clear button row at top
                    RowLayout {
                        Layout.fillWidth: true

                        Item { Layout.fillWidth: true }

                        // Clear conversation button
                        Rectangle {
                            width: clearText.width + Theme.scaled(16)
                            height: Theme.scaled(32)
                            radius: Theme.scaled(4)
                            color: Theme.errorColor
                            opacity: 0.8

                            Text {
                                id: clearText
                                anchors.centerIn: parent
                                text: TranslationManager.translate("postshotreview.conversation.clear", "Clear")
                                font.pixelSize: Theme.scaled(12)
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (MainController.aiManager && MainController.aiManager.conversation) {
                                        MainController.aiManager.conversation.clearHistory()
                                        MainController.aiManager.conversation.saveToStorage()
                                    }
                                    // Keep window open - shot data is still attached for a fresh conversation
                                }
                            }
                        }
                    }

                    // Conversation history
                    Flickable {
                        id: conversationFlickable
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: conversationText.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        TextArea {
                            id: conversationText
                            width: parent.width
                            text: MainController.aiManager && MainController.aiManager.conversation
                                  ? MainController.aiManager.conversation.getConversationText()
                                  : ""
                            textFormat: Text.MarkdownText
                            wrapMode: TextEdit.WordWrap
                            readOnly: true
                            font: Theme.bodyFont
                            color: Theme.textColor
                            background: null
                            padding: 0
                        }
                    }

                    // Loading indicator
                    RowLayout {
                        visible: MainController.aiManager && MainController.aiManager.conversation &&
                                 MainController.aiManager.conversation.busy
                        Layout.fillWidth: true

                        BusyIndicator {
                            running: true
                            Layout.preferredWidth: Theme.scaled(24)
                            Layout.preferredHeight: Theme.scaled(24)
                            palette.dark: Theme.primaryColor
                        }

                        Text {
                            text: TranslationManager.translate("postshotreview.conversation.thinking", "Thinking...")
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                        }
                    }

                    // Shot data attached indicator
                    Rectangle {
                        visible: postShotReviewPage.pendingShotSummary.length > 0
                        Layout.fillWidth: true
                        height: Theme.scaled(28)
                        radius: Theme.scaled(4)
                        color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                        border.color: Theme.primaryColor
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            spacing: Theme.scaled(6)

                            Text {
                                text: "📊"
                                font.pixelSize: Theme.scaled(12)
                            }

                            Text {
                                text: TranslationManager.translate("postshotreview.conversation.shotattached", "Shot data will be included with your message")
                                font.pixelSize: Theme.scaled(11)
                                color: Theme.primaryColor
                                Layout.fillWidth: true
                            }

                            Text {
                                text: "✕"
                                font.pixelSize: Theme.scaled(12)
                                color: Theme.textSecondaryColor

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    onClicked: postShotReviewPage.pendingShotSummary = ""
                                }
                            }
                        }
                    }

                    // Input row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        StyledTextField {
                            id: conversationInput
                            Layout.fillWidth: true
                            placeholder: postShotReviewPage.pendingShotSummary.length > 0
                                         ? TranslationManager.translate("postshotreview.conversation.placeholder.withshot", "Ask about this shot...")
                                         : TranslationManager.translate("postshotreview.conversation.placeholder", "Ask a follow-up question...")
                            enabled: MainController.aiManager && MainController.aiManager.conversation &&
                                     !MainController.aiManager.conversation.busy

                            Keys.onReturnPressed: sendFollowUp()
                            Keys.onEnterPressed: sendFollowUp()

                            function sendFollowUp() {
                                if (text.length === 0) return
                                if (!MainController.aiManager || !MainController.aiManager.conversation) return

                                var conversation = MainController.aiManager.conversation
                                var message = text

                                // If there's a pending shot, include it with the user's question
                                if (postShotReviewPage.pendingShotSummary.length > 0) {
                                    message = "Here's my latest shot:\n\n" + postShotReviewPage.pendingShotSummary +
                                              "\n\n" + text
                                    postShotReviewPage.pendingShotSummary = ""  // Clear after sending
                                }

                                // Use ask() for new conversation, followUp() for existing
                                if (!conversation.hasHistory) {
                                    var bevType = (editBeverageType || "espresso").toLowerCase()
                                    var isFilter = bevType === "filter" || bevType === "pourover"
                                    var systemPrompt = isFilter
                                        ? "You are an expert filter coffee consultant helping a user optimise brews made on a Decent DE1 profiling machine over multiple attempts. " +
                                          "Key principles: Taste is king — numbers serve taste, not the other way around. " +
                                          "Profile intent is the reference frame — evaluate actual vs. what the profile intended, not pour-over or drip norms. " +
                                          "DE1 filter uses low pressure (1-3 bar), high flow, and long ratios (1:10-1:17) — these are all intentional, not problems. " +
                                          "If grinder info is shared, consider burr geometry (flat vs conical) in your analysis. " +
                                          "Provide specific, actionable advice. Focus on one variable at a time."
                                        : "You are an expert espresso consultant helping a user dial in their shots. " +
                                          "The user has a Decent Espresso machine. " +
                                          "If grinder info is shared, consider burr geometry (flat vs conical) in your analysis. " +
                                          "Provide specific, actionable advice. Focus on one variable at a time. " +
                                          "Follow James Hoffmann's methodology for dialing in espresso."
                                    conversation.ask(systemPrompt, message)
                                } else {
                                    conversation.followUp(message)
                                }
                                text = ""
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(60)
                            Layout.preferredHeight: Theme.scaled(44)
                            radius: Theme.scaled(6)
                            color: conversationInput.text.length > 0 ? Theme.primaryColor : Theme.surfaceColor
                            border.color: Theme.borderColor
                            border.width: conversationInput.text.length > 0 ? 0 : 1

                            Text {
                                anchors.centerIn: parent
                                text: TranslationManager.translate("postshotreview.conversation.send", "Send")
                                font: Theme.bodyFont
                                color: conversationInput.text.length > 0 ? "white" : Theme.textSecondaryColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: conversationInput.text.length > 0 &&
                                         MainController.aiManager && MainController.aiManager.conversation &&
                                         !MainController.aiManager.conversation.busy
                                onClicked: conversationInput.sendFollowUp()
                            }
                        }
                    }
                }
            }
        }

        // Bottom bar for conversation overlay
        BottomBar {
            id: conversationBottomBar
            title: TranslationManager.translate("postshotreview.conversation.title", "Dialing Conversation")
            showBackButton: true
            onBackClicked: {
                // Save conversation before closing
                if (MainController.aiManager && MainController.aiManager.conversation) {
                    MainController.aiManager.conversation.saveToStorage()
                }
                conversationOverlay.visible = false
            }
        }

        // Update conversation text when response received
        Connections {
            target: MainController.aiManager ? MainController.aiManager.conversation : null
            function onResponseReceived(response) {
                // Scroll to bottom when new response arrives
                Qt.callLater(function() {
                    conversationFlickable.contentY = Math.max(0, conversationFlickable.contentHeight - conversationFlickable.height)
                })
            }
            function onHistoryChanged() {
                // Refresh conversation text
                conversationText.text = MainController.aiManager.conversation.getConversationText()
            }
        }
    }
}
