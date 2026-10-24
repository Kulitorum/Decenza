import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: visualizerPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: trPageTitle.text

    objectName: "visualizerBrowserPage"
    background: ThemedPageBackground {}

    // Translatable strings for page title
    Tr { id: trPageTitle; key: "visualizer.title"; fallback: "Import from Visualizer"; visible: false }


    property string activeImportRequestId: ""
    readonly property bool browserHasUnsavedChanges:
        shareCodeInput.displayText.length > 0 || activeImportRequestId.length > 0

    // A request belongs to this page instance. If the page is removed before an
    // asynchronous duplicate response arrives, defer its cancellation by ID.
    Component.onDestruction: {
        if (activeImportRequestId.length > 0)
            VisualizerProfileImportService.cancelPending(activeImportRequestId)
    }

    // Import success/failure handling
    Connections {
        target: VisualizerProfileImportService

        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId !== visualizerPage.activeImportRequestId)
                return
            visualizerPage.activeImportRequestId = ""
            duplicateResolver.dismiss()
            if (success) {
                importStatus.statusMessage = TranslationManager.translate("visualizer.status.imported", "Imported:")
                    + " " + (result.title || "")
                importStatus.statusColor = Theme.successColor
                importStatus.visible = true
                statusTimer.restart()
                shareCodeInput.text = ""
            } else if (reason !== "cancelled") {
                importStatus.statusMessage = TranslationManager.translate("visualizer.status.error", "Error:")
                    + " " + reason
                importStatus.statusColor = Theme.errorColor
                importStatus.visible = true
                statusTimer.restart()
            }
        }

        function onDuplicateFound(requestId, profileTitle) {
            if (requestId === visualizerPage.activeImportRequestId)
                duplicateResolver.present(requestId, profileTitle)
        }

        function onOperationRejected(requestId, operation, reason) {
            if (requestId !== visualizerPage.activeImportRequestId
                || requestId !== duplicateResolver.requestId)
                return
            duplicateResolver.errorMessage = TranslationManager.translate(
                "visualizer.duplicate.invalidName", "That name could not be used. Check it and try again.")
        }
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
            color: Theme.cardBackgroundColor

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
                visible: !duplicateResolver.visible
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
                    StyledTextField {
                        id: shareCodeInput
                        width: parent.width
                        height: Theme.scaled(60)
                        font.pixelSize: Theme.scaled(24)
                        font.family: Theme.bodyFont.family
                        horizontalAlignment: Text.AlignHCenter
                        maximumLength: 4
                        placeholder: TranslationManager.translate("visualizer.codePlaceholder", "CODE")
                        accessibleName: TranslationManager.translate("visualizerBrowser.accessible.shareCode", "Share code")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

                        // Auto-uppercase
                        onTextChanged: {
                            var upper = text.toUpperCase()
                            if (text !== upper) {
                                text = upper
                            }
                        }

                        // Submit on both Return and Enter (numeric keypad). This used to read
                        // `Keys.onEnterPressed: Keys.onReturnPressed(event)`, which is not a
                        // callable — the keypad Enter key threw a TypeError instead of submitting.
                        function submitShareCode() {
                            // Before reading .text — on Android the IME's in-progress word is
                            // not in `text` until committed, so a genuinely 4-character code
                            // would read as 3 and fall through silently (CLAUDE.md IME rule).
                            Keyboard.commit()
                            if (VisualizerProfileImportService.ready && shareCodeInput.text.length === 4) {
                                visualizerPage.activeImportRequestId = VisualizerProfileImportService.importFromShareCode(shareCodeInput.text)
                            }
                            shareCodeInput.focus = false
                            Keyboard.hide()
                        }

                        Keys.onReturnPressed: shareCodeInput.submitShareCode()
                        Keys.onEnterPressed: shareCodeInput.submitShareCode()
                    }

                    // Import buttons row
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.spacingMedium

                        // Import Profile button (requires 4-char code)
                        AccessibleButton {
                            id: importButton
                            primary: true
                            enabled: VisualizerProfileImportService.ready && shareCodeInput.text.length === 4 && !VisualizerProfileImportService.importing
                            text: VisualizerProfileImportService.importing
                                ? TranslationManager.translate("visualizer.button.importing", "Importing...")
                                : TranslationManager.translate("visualizer.button.import", "Import Profile")
                            accessibleName: VisualizerProfileImportService.importing
                                ? TranslationManager.translate("visualizerBrowser.importingPleaseWait", "Importing profile, please wait")
                                : TranslationManager.translate("visualizerBrowser.importUsingShareCode", "Import profile using the 4-character share code")
                            onClicked: {
                                Keyboard.commit()
                                visualizerPage.activeImportRequestId = VisualizerProfileImportService.importFromShareCode(shareCodeInput.text)
                            }
                        }

                        // Import Shared button (opens multi-import page)
                        AccessibleButton {
                            id: importSharedButton
                            primary: true
                            text: TranslationManager.translate("visualizer.button.importShared", "Import profiles I shared")
                            accessibleName: TranslationManager.translate("visualizerBrowser.importSharedProfiles", "Import all profiles you have shared on Visualizer")
                            onClicked: {
                                AppShell.visualizerMultiImportRequested()
                            }
                        }
                    }

                    // Loading indicator
                    BusyIndicator {
                        running: VisualizerProfileImportService.importing
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
                        visible: running
                    }

                    // Instructions
                    Rectangle {
                        width: parent.width
                        height: instructionsColumn.height + Theme.scaled(20)
                        color: Theme.cardBackgroundColor
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

            VisualizerDuplicateResolver { id: duplicateResolver }
        }
    }

    // Translatable strings for bottom bar
    Tr { id: trBottomTitle; key: "visualizer.bottomBar.title"; fallback: "Visualizer"; visible: false }
    Tr { id: trBottomHint; key: "visualizer.bottomBar.hint"; fallback: "Enter share code to import"; visible: false }

    // Bottom bar
    BottomBar {
        title: trBottomTitle.text
        rightText: trBottomHint.text
        onBackClicked: AppShell.backRequested()
    }
}
