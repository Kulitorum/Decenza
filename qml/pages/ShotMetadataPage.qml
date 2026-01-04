import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotMetadataPage
    objectName: "shotMetadataPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("shotmetadata.title", "Shot Info")
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("shotmetadata.title", "Shot Info")

    property bool hasPendingShot: false  // Set to true by goToShotMetadata() after a shot
    property bool keyboardVisible: Qt.inputMethod.visible
    property Item focusedField: null

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

            // 3-column grid for all fields
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 6

                // === ROW 1: Bean info ===
                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roaster", "Roaster")
                    text: Settings.dyeBeanBrand
                    onTextEdited: function(t) { Settings.dyeBeanBrand = t }
                }

                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.coffee", "Coffee")
                    text: Settings.dyeBeanType
                    onTextEdited: function(t) { Settings.dyeBeanType = t }
                }

                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.roastdate", "Roast date (yyyy-mm-dd)")
                    text: Settings.dyeRoastDate
                    inputHints: Qt.ImhDate
                    inputMask: "9999-99-99"
                    onTextEdited: function(t) { Settings.dyeRoastDate = t }
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
                    currentValue: Settings.dyeRoastLevel
                    onValueChanged: function(v) { Settings.dyeRoastLevel = v }
                }

                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.grinder", "Grinder")
                    text: Settings.dyeGrinderModel
                    onTextEdited: function(t) { Settings.dyeGrinderModel = t }
                }

                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.setting", "Setting")
                    text: Settings.dyeGrinderSetting
                    onTextEdited: function(t) { Settings.dyeGrinderSetting = t }
                }

                // === ROW 3: Barista, Preset, Shot Date ===
                LabeledField {
                    Layout.fillWidth: true
                    label: TranslationManager.translate("shotmetadata.label.barista", "Barista")
                    text: Settings.dyeBarista
                    onTextEdited: function(t) { Settings.dyeBarista = t }
                }

                // Preset (read-only display)
                Item {
                    Layout.fillWidth: true
                    implicitHeight: presetLabel.height + presetValue.height + 2

                    Text {
                        id: presetLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: TranslationManager.translate("shotmetadata.label.preset", "Preset")
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
                        border.width: Theme.scaled(1)

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            text: MainController.currentProfileName || ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("shotmetadata.label.preset", "Preset") + ": " + MainController.currentProfileName
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
                        text: TranslationManager.translate("shotmetadata.label.shotdate", "Shot date")
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
                        border.width: Theme.scaled(1)

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(12)
                            anchors.rightMargin: Theme.scaled(12)
                            text: Settings.dyeShotDateTime || ""
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Accessible.role: Accessible.StaticText
                        Accessible.name: TranslationManager.translate("shotmetadata.label.shotdate", "Shot date") + ": " + Settings.dyeShotDateTime
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
                        key: "shotmetadata.section.measurements"
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
                                key: "shotmetadata.label.dose"
                                fallback: "Dose"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                            }
                            ValueInput {
                                id: doseInput
                                Layout.fillWidth: true
                                height: Theme.scaled(40)
                                from: 0
                                to: 30
                                stepSize: 0.1
                                decimals: 1
                                suffix: "g"
                                valueColor: Theme.dyeDoseColor
                                value: Settings.dyeBeanWeight
                                accessibleName: TranslationManager.translate("shotmetadata.label.dose", "Dose") + " " + value + " " + TranslationManager.translate("shotmetadata.unit.grams", "grams")
                                onValueModified: function(newValue) {
                                    doseInput.value = newValue
                                    Settings.dyeBeanWeight = newValue
                                }
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
                            }
                        }

                        // Out (drink weight)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "shotmetadata.label.out"
                                fallback: "Out"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                            }
                            ValueInput {
                                id: outInput
                                Layout.fillWidth: true
                                height: Theme.scaled(40)
                                from: 0
                                to: 100
                                stepSize: 0.1
                                decimals: 1
                                suffix: "g"
                                valueColor: Theme.dyeOutputColor
                                value: Settings.dyeDrinkWeight
                                accessibleName: TranslationManager.translate("shotmetadata.accessible.output", "Output") + " " + value + " " + TranslationManager.translate("shotmetadata.unit.grams", "grams")
                                onValueModified: function(newValue) {
                                    outInput.value = newValue
                                    Settings.dyeDrinkWeight = newValue
                                }
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
                            }
                        }

                        // TDS
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "shotmetadata.label.tds"
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
                                value: Settings.dyeDrinkTds
                                accessibleName: TranslationManager.translate("shotmetadata.label.tds", "TDS") + " " + value + " " + TranslationManager.translate("shotmetadata.unit.percent", "percent")
                                onValueModified: function(newValue) {
                                    tdsInput.value = newValue
                                    Settings.dyeDrinkTds = newValue
                                }
                                onActiveFocusChanged: if (activeFocus) hideKeyboard()
                            }
                        }

                        // EY
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)
                            Tr {
                                key: "shotmetadata.label.ey"
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
                                value: Settings.dyeDrinkEy
                                accessibleName: TranslationManager.translate("shotmetadata.accessible.extractionyield", "Extraction yield") + " " + value + " " + TranslationManager.translate("shotmetadata.unit.percent", "percent")
                                onValueModified: function(newValue) {
                                    eyInput.value = newValue
                                    Settings.dyeDrinkEy = newValue
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
                        key: "shotmetadata.label.rating"
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
                        value: Settings.dyeEspressoEnjoyment > 0 ? Settings.dyeEspressoEnjoyment : 75
                        accessibleName: TranslationManager.translate("shotmetadata.label.rating", "Rating") + " " + value + " " + TranslationManager.translate("shotmetadata.unit.percent", "percent")
                        onValueModified: function(newValue) {
                            ratingInput.value = newValue
                            Settings.dyeEspressoEnjoyment = newValue
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
                        key: "shotmetadata.label.notes"
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
                        text: Settings.dyeEspressoNotes
                        font: Theme.bodyFont
                        color: Theme.textColor
                        placeholderTextColor: Theme.textSecondaryColor
                        wrapMode: TextEdit.Wrap
                        leftPadding: 8; rightPadding: 8; topPadding: 6; bottomPadding: Theme.scaled(6)
                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: notesField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: Theme.scaled(1)
                        }
                        onTextChanged: Settings.dyeEspressoNotes = text

                        Accessible.role: Accessible.EditableText
                        Accessible.name: TranslationManager.translate("shotmetadata.label.notes", "Notes")
                        Accessible.description: text.length > 0 ? text : TranslationManager.translate("shotmetadata.accessible.empty", "Empty")

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                focusedField = notesField
                                focusResetTimer.stop()
                                if (AccessibilityManager.enabled) {
                                    let announcement = TranslationManager.translate("shotmetadata.label.notes", "Notes") + ". " + (text.length > 0 ? text : TranslationManager.translate("shotmetadata.accessible.empty", "Empty"))
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

    // Bottom bar (stays visible under keyboard)
    BottomBar {
        onBackClicked: root.goBack()

        Tr {
            visible: MainController.visualizer.uploading
            key: "shotmetadata.status.uploading"
            fallback: "Uploading..."
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }

        // AI Advice button - visible whenever we have shot data (not just pending)
        Rectangle {
            id: aiAdviceButton
            visible: MainController.aiManager && MainController.shotDataModel && MainController.shotDataModel.maxTime > 0
            Layout.preferredWidth: aiAdviceContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: MainController.aiManager && MainController.aiManager.isConfigured
                   ? Theme.primaryColor : Theme.surfaceColor
            opacity: MainController.aiManager && MainController.aiManager.isAnalyzing ? 0.6 : 1.0

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("shotmetadata.accessible.getaiadvice", "Get AI Advice")
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
                          ? "shotmetadata.button.analyzing" : "shotmetadata.button.aiadvice"
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
                    // Navigate to results page first, then trigger analysis
                    pageStack.push(Qt.resolvedUrl("DialingAssistantPage.qml"))

                    // Trigger analysis with current metadata
                    MainController.aiManager.analyzeShot(
                        MainController.shotDataModel,
                        MainController.currentProfilePtr,
                        Settings.dyeBeanWeight,
                        Settings.dyeDrinkWeight,
                        {
                            "beanBrand": Settings.dyeBeanBrand,
                            "beanType": Settings.dyeBeanType,
                            "roastDate": Settings.dyeRoastDate,
                            "roastLevel": Settings.dyeRoastLevel,
                            "grinderModel": Settings.dyeGrinderModel,
                            "grinderSetting": Settings.dyeGrinderSetting,
                            "enjoymentScore": Settings.dyeEspressoEnjoyment,
                            "tastingNotes": Settings.dyeEspressoNotes
                        }
                    )
                }
            }
        }

        // Email Prompt button - fallback for users without API keys
        Rectangle {
            id: emailPromptButton
            visible: MainController.aiManager && !MainController.aiManager.isConfigured && MainController.shotDataModel && MainController.shotDataModel.maxTime > 0
            Layout.preferredWidth: emailPromptContent.width + 32
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: Theme.surfaceColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("shotmetadata.accessible.emailprompt", "Email AI prompt to yourself")
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
                    key: "shotmetadata.button.emailprompt"
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
                    var prompt = MainController.aiManager.generateEmailPrompt(
                        MainController.shotDataModel,
                        MainController.currentProfilePtr,
                        Settings.dyeBeanWeight,
                        Settings.dyeDrinkWeight,
                        {
                            "beanBrand": Settings.dyeBeanBrand,
                            "beanType": Settings.dyeBeanType,
                            "roastDate": Settings.dyeRoastDate,
                            "roastLevel": Settings.dyeRoastLevel,
                            "grinderModel": Settings.dyeGrinderModel,
                            "grinderSetting": Settings.dyeGrinderSetting,
                            "enjoymentScore": Settings.dyeEspressoEnjoyment,
                            "tastingNotes": Settings.dyeEspressoNotes
                        }
                    )
                    // Open mailto: with prompt in body
                    Qt.openUrlExternally("mailto:?subject=" + encodeURIComponent("Espresso Shot Analysis") +
                                        "&body=" + encodeURIComponent(prompt))
                }
            }
        }

        Rectangle {
            id: uploadButton
            visible: hasPendingShot && !MainController.visualizer.uploading
            Layout.preferredWidth: uploadText.width + 40
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(8)
            color: uploadArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("shotmetadata.button.upload", "Upload to Visualizer")
            Accessible.onPressAction: uploadArea.clicked(null)

            Tr {
                id: uploadText
                anchors.centerIn: parent
                key: "shotmetadata.button.upload"
                fallback: "Upload to Visualizer"
                color: "white"
                font: Theme.bodyFont
            }

            MouseArea {
                id: uploadArea
                anchors.fill: parent
                onClicked: {
                    MainController.uploadPendingShot()
                    hasPendingShot = false
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
                border.width: Theme.scaled(1)
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
                    text: modelData || TranslationManager.translate("shotmetadata.option.none", "(None)")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                    verticalAlignment: Text.AlignVCenter
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
}
