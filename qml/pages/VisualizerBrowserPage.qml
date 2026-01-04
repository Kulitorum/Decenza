import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: visualizerPage
    objectName: "visualizerBrowserPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Translatable strings for page title
    Tr { id: trPageTitle; key: "visualizer.title"; fallback: "Import from Visualizer"; visible: false }

    Component.onCompleted: root.currentPageTitle = trPageTitle.text
    StackView.onActivated: root.currentPageTitle = trPageTitle.text

    // Import success/failure handling
    Connections {
        target: MainController.visualizerImporter

        function onImportSuccess(profileTitle) {
            importStatus.statusMessage = TranslationManager.translate("visualizer.status.imported", "Imported:") + " " + profileTitle
            importStatus.statusColor = Theme.successColor
            importStatus.visible = true
            statusTimer.restart()
            shareCodeInput.text = ""
        }

        function onImportFailed(error) {
            importStatus.statusMessage = TranslationManager.translate("visualizer.status.error", "Error:") + " " + error
            importStatus.statusColor = Theme.errorColor
            importStatus.visible = true
            statusTimer.restart()
        }

        function onDuplicateFound(profileTitle, existingPath) {
            showDuplicateDialog(profileTitle)
        }
    }

    // Track if we're showing the duplicate choice
    property bool showingDuplicateChoice: false
    property bool showingNameInput: false
    property string duplicateProfileTitle: ""

    function showDuplicateDialog(title) {
        duplicateProfileTitle = title
        showingDuplicateChoice = true
        showingNameInput = false
    }

    function hideDuplicateDialog() {
        showingDuplicateChoice = false
        showingNameInput = false
    }

    Timer {
        id: statusTimer
        interval: 5000
        onTriggered: importStatus.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        spacing: Theme.scaled(0)

        // Status message
        Rectangle {
            id: importStatus
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Theme.scaled(40) : 0
            visible: false
            color: Theme.surfaceColor

            property string statusMessage: ""
            property color statusColor: Theme.textColor

            Text {
                anchors.centerIn: parent
                text: importStatus.statusMessage
                color: importStatus.statusColor
                font: Theme.bodyFont
            }

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 200 }
            }
        }

        // Main content area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Share code input (main view)
            Rectangle {
                anchors.fill: parent
                visible: !showingDuplicateChoice
                color: Theme.backgroundColor

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacingLarge
                    width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(500))

                    Tr {
                        key: "visualizer.heading.import"
                        fallback: "Import Profile from Visualizer"
                        color: Theme.textColor
                        font: Theme.headingFont
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Tr {
                        key: "visualizer.instruction.enterCode"
                        fallback: "Enter the 4-character share code from visualizer.coffee"
                        wrapMode: Text.Wrap
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textSecondaryColor
                        font: Theme.bodyFont
                    }

                    // Share code input field
                    TextField {
                        id: shareCodeInput
                        width: parent.width
                        height: Theme.scaled(60)
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24)
                        font.family: Theme.bodyFont.family
                        horizontalAlignment: Text.AlignHCenter
                        maximumLength: 4
                        placeholderTextColor: Theme.textSecondaryColor
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                        topPadding: Theme.scaled(12)
                        bottomPadding: Theme.scaled(12)

                        background: Rectangle {
                            color: Theme.surfaceColor
                            border.color: shareCodeInput.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: Theme.scaled(2)
                            radius: Theme.scaled(8)
                        }

                        // Auto-uppercase
                        onTextChanged: {
                            var upper = text.toUpperCase()
                            if (text !== upper) {
                                text = upper
                            }
                        }

                        Keys.onReturnPressed: {
                            if (text.length === 4) {
                                MainController.visualizerImporter.importFromShareCode(text)
                            }
                            focus = false
                            Qt.inputMethod.hide()
                        }
                        Keys.onEnterPressed: Keys.onReturnPressed(event)
                    }

                    // Import buttons row
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.spacingMedium

                        // Import Profile button (requires 4-char code)
                        Button {
                            id: importButton
                            enabled: shareCodeInput.text.length === 4 && !MainController.visualizerImporter.importing
                            width: Theme.scaled(160)
                            height: Theme.scaled(50)

                            Tr { id: trImporting; key: "visualizer.button.importing"; fallback: "Importing..."; visible: false }
                            Tr { id: trImportProfile; key: "visualizer.button.import"; fallback: "Import Profile"; visible: false }

                            onClicked: {
                                MainController.visualizerImporter.importFromShareCode(shareCodeInput.text)
                            }

                            background: Rectangle {
                                radius: Theme.scaled(8)
                                color: parent.enabled ? Theme.primaryColor : Theme.surfaceColor
                            }
                            contentItem: Text {
                                text: MainController.visualizerImporter.importing ? trImporting.text : trImportProfile.text
                                color: importButton.enabled ? "white" : Theme.textSecondaryColor
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        // Import Shared button (opens multi-import page)
                        Button {
                            id: importSharedButton
                            height: Theme.scaled(50)

                            onClicked: {
                                pageStack.push(Qt.resolvedUrl("VisualizerMultiImportPage.qml"))
                            }

                            background: Rectangle {
                                radius: Theme.scaled(8)
                                color: Theme.primaryColor
                                opacity: importSharedButton.pressed ? 0.8 : 1.0
                            }
                            contentItem: Text {
                                text: "Import profiles I shared"
                                color: "white"
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(16)
                                rightPadding: Theme.scaled(16)
                            }
                        }
                    }

                    // Loading indicator
                    BusyIndicator {
                        running: MainController.visualizerImporter.importing
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
                        visible: running
                    }

                    // Instructions
                    Rectangle {
                        width: parent.width
                        height: instructionsColumn.height + Theme.scaled(20)
                        color: Theme.surfaceColor
                        radius: Theme.scaled(8)

                        Column {
                            id: instructionsColumn
                            anchors.centerIn: parent
                            width: parent.width - Theme.scaled(20)
                            spacing: Theme.scaled(8)

                            Tr {
                                key: "visualizer.instructions.title"
                                fallback: "How to get a share code:"
                                color: Theme.textColor
                                font.bold: true
                                font.pixelSize: Theme.bodyFont.pixelSize
                            }

                            Tr {
                                key: "visualizer.instructions.step1"
                                fallback: "1. Open visualizer.coffee on your phone or computer"
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                width: parent.width
                            }

                            Tr {
                                key: "visualizer.instructions.step2"
                                fallback: "2. Find a shot with a profile you want"
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                width: parent.width
                            }

                            Tr {
                                key: "visualizer.instructions.step3"
                                fallback: "3. Tap 'Share' and copy the 4-character code"
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                width: parent.width
                            }

                            Tr {
                                key: "visualizer.instructions.step4"
                                fallback: "4. Enter the code above and tap Import"
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                width: parent.width
                            }
                        }
                    }
                }
            }

            // Duplicate profile choice
            Rectangle {
                anchors.fill: parent
                visible: showingDuplicateChoice && !showingNameInput
                color: Theme.backgroundColor

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacingLarge
                    width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))

                    Tr {
                        key: "visualizer.duplicate.title"
                        fallback: "Profile Already Exists"
                        color: Theme.textColor
                        font: Theme.headingFont
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    // Dynamic text with profile name - use Text with TranslationManager
                    Text {
                        text: TranslationManager.translate("visualizer.duplicate.message", "A profile named \"%1\" already exists.\n\nWhat would you like to do?").replace("%1", duplicateProfileTitle)
                        wrapMode: Text.Wrap
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }

                    Row {
                        spacing: Theme.spacingMedium
                        anchors.horizontalCenter: parent.horizontalCenter

                        Button {
                            id: overwriteButton
                            Tr { id: trOverwrite; key: "visualizer.button.overwrite"; fallback: "Overwrite"; visible: false }
                            onClicked: {
                                MainController.visualizerImporter.saveOverwrite()
                                hideDuplicateDialog()
                            }
                            background: Rectangle {
                                radius: Theme.scaled(4)
                                color: Theme.errorColor
                            }
                            contentItem: Text {
                                text: trOverwrite.text
                                color: "white"
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(16)
                                rightPadding: Theme.scaled(16)
                            }
                        }

                        Button {
                            id: saveAsNewButton
                            Tr { id: trSaveAsNew; key: "visualizer.button.saveAsNew"; fallback: "Save as New"; visible: false }
                            onClicked: {
                                newNameInput.text = duplicateProfileTitle + " (copy)"
                                showingNameInput = true
                            }
                            background: Rectangle {
                                radius: Theme.scaled(4)
                                color: Theme.primaryColor
                            }
                            contentItem: Text {
                                text: trSaveAsNew.text
                                color: "white"
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(16)
                                rightPadding: Theme.scaled(16)
                            }
                        }

                        Button {
                            id: cancelButton
                            Tr { id: trCancel; key: "visualizer.button.cancel"; fallback: "Cancel"; visible: false }
                            onClicked: hideDuplicateDialog()
                            background: Rectangle {
                                radius: Theme.scaled(4)
                                color: Theme.surfaceColor
                                border.color: Theme.textSecondaryColor
                                border.width: Theme.scaled(1)
                            }
                            contentItem: Text {
                                text: trCancel.text
                                color: Theme.textColor
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(16)
                                rightPadding: Theme.scaled(16)
                            }
                        }
                    }
                }
            }

            // Name input for Save as New
            FocusScope {
                id: nameInputPanel
                anchors.fill: parent
                visible: showingDuplicateChoice && showingNameInput
                focus: visible

                property real keyboardOffset: 0

                Timer {
                    id: keyboardResetTimer
                    interval: 100
                    onTriggered: {
                        if (!Qt.inputMethod.visible) {
                            nameInputPanel.keyboardOffset = 0
                        }
                    }
                }

                Connections {
                    target: Qt.inputMethod
                    function onVisibleChanged() {
                        if (Qt.inputMethod.visible) {
                            keyboardResetTimer.stop()
                            nameInputPanel.keyboardOffset = nameInputPanel.height * 0.3
                        } else {
                            keyboardResetTimer.restart()
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: Theme.backgroundColor
                }

                Column {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -nameInputPanel.keyboardOffset
                    spacing: Theme.spacingLarge
                    width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))

                    Tr {
                        key: "visualizer.newName.title"
                        fallback: "Enter New Name"
                        color: Theme.textColor
                        font: Theme.headingFont
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    TextField {
                        id: newNameInput
                        width: parent.width
                        height: Theme.scaled(50)
                        color: Theme.textColor
                        font: Theme.bodyFont
                        selectByMouse: true
                        focus: true
                        inputMethodHints: Qt.ImhNoAutoUppercase
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                        topPadding: Theme.scaled(12)
                        bottomPadding: Theme.scaled(12)
                        background: Rectangle {
                            color: Theme.surfaceColor
                            border.color: newNameInput.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: Theme.scaled(2)
                            radius: Theme.scaled(4)
                        }

                        Keys.onReturnPressed: { focus = false; Qt.inputMethod.hide() }
                        Keys.onEnterPressed: { focus = false; Qt.inputMethod.hide() }
                    }

                    Row {
                        spacing: Theme.spacingMedium
                        anchors.horizontalCenter: parent.horizontalCenter

                        Button {
                            id: saveButton
                            Tr { id: trSave; key: "visualizer.button.save"; fallback: "Save"; visible: false }
                            enabled: newNameInput.text.trim().length > 0
                            onClicked: {
                                MainController.visualizerImporter.saveWithNewName(newNameInput.text.trim())
                                hideDuplicateDialog()
                            }
                            background: Rectangle {
                                radius: Theme.scaled(4)
                                color: parent.enabled ? Theme.primaryColor : Theme.surfaceColor
                            }
                            contentItem: Text {
                                text: trSave.text
                                color: saveButton.enabled ? "white" : Theme.textSecondaryColor
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(16)
                                rightPadding: Theme.scaled(16)
                            }
                        }

                        Button {
                            id: backButton
                            Tr { id: trBack; key: "visualizer.button.back"; fallback: "Back"; visible: false }
                            onClicked: showingNameInput = false
                            background: Rectangle {
                                radius: Theme.scaled(4)
                                color: Theme.surfaceColor
                                border.color: Theme.textSecondaryColor
                                border.width: Theme.scaled(1)
                            }
                            contentItem: Text {
                                text: trBack.text
                                color: Theme.textColor
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: Theme.scaled(16)
                                rightPadding: Theme.scaled(16)
                            }
                        }
                    }
                }
            }
        }
    }

    // Translatable strings for bottom bar
    Tr { id: trBottomTitle; key: "visualizer.bottomBar.title"; fallback: "Visualizer"; visible: false }
    Tr { id: trBottomHint; key: "visualizer.bottomBar.hint"; fallback: "Enter share code to import"; visible: false }

    // Bottom bar
    BottomBar {
        title: trBottomTitle.text
        rightText: trBottomHint.text
        onBackClicked: root.goBack()
    }
}
