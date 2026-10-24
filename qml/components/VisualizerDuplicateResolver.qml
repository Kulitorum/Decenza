import QtQuick
import QtQuick.Layouts
import Decenza

// Shared per-client resolution UI for duplicate Visualizer imports.
// The request identity remains in VisualizerProfileImportService; this component
// only owns the user's choice and name draft.
Item {
    id: resolver
    anchors.fill: parent
    z: 1000
    visible: false
    focus: visible

    property string requestId: ""
    property string profileTitle: ""
    property string errorMessage: ""
    property bool showNameInput: false
    property bool resolutionPending: false
    property real keyboardOffset: 0
    // This full-screen resolver keeps its existing Item presentation, but
    // participates in the same client-local Back/unload policy as dialogs.
    readonly property bool browserBackDismissible: !resolutionPending
    readonly property bool browserUnloadDirty: visible
        && (resolutionPending || (showNameInput && nameField.text.trim() !== profileTitle + " (copy)"))

    function present(importRequestId: string, title: string): void {
        requestId = importRequestId
        profileTitle = title
        errorMessage = ""
        showNameInput = false
        resolutionPending = false
        visible = true
    }

    function dismiss(): void {
        visible = false
        showNameInput = false
        keyboardOffset = 0
        resolutionPending = false
        requestId = ""
    }

    function updateKeyboardOffset(): void {
        keyboardOffset = Keyboard.visible && nameField.activeFocus
            ? height * 0.3 : 0
    }

    function cancel(): void {
        if (resolutionPending)
            return
        if (requestId.length > 0)
            VisualizerProfileImportService.cancelPending(requestId)
        dismiss()
    }

    function dismissForBrowserBack(): void {
        cancel()
    }

    function updateBrowserDialogPolicy(): void {
        if (visible) {
            AppShell.updateBrowserDialog(resolver, browserBackDismissible, browserUnloadDirty)
        }
    }

    function requestResolution(operation: string, payload: var): void {
        if (resolutionPending || requestId.length === 0)
            return
        const id = VisualizerProfileImportService.execute(operation, payload, requestId)
        if (id.length > 0) {
            resolutionPending = true
            errorMessage = ""
        } else {
            errorMessage = TranslationManager.translate(
                "common.error.notConnected", "The host service is unavailable.")
        }
    }

    Keys.onEscapePressed: cancel()

    onVisibleChanged: {
        if (visible) {
            AppShell.registerBrowserDialog(resolver, browserBackDismissible,
                browserUnloadDirty, function() { resolver.dismissForBrowserBack() })
        } else {
            AppShell.unregisterBrowserDialog(resolver)
        }
    }
    onResolutionPendingChanged: updateBrowserDialogPolicy()
    onBrowserUnloadDirtyChanged: updateBrowserDialogPolicy()
    Component.onDestruction: AppShell.unregisterBrowserDialog(resolver)

    Connections {
        target: Keyboard
        function onVisibleChanged() { resolver.updateKeyboardOffset() }
    }

    Connections {
        target: nameField
        function onActiveFocusChanged() { resolver.updateKeyboardOffset() }
    }

    Connections {
        target: VisualizerProfileImportService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === resolver.requestId)
                resolver.dismiss()
        }
        function onOperationRejected(requestId, operation, reason) {
            if (resolver.visible && requestId === resolver.requestId) {
                resolver.resolutionPending = false
                resolver.errorMessage = TranslationManager.translate(
                    "visualizer.duplicate.invalidName",
                    "That name could not be used. Check it and try again.")
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundColor
    }

    ColumnLayout {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: resolver.showNameInput ? -resolver.keyboardOffset : 0
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        spacing: Theme.spacingLarge
        visible: !resolver.showNameInput

        Tr {
            key: "visualizer.duplicate.title"
            fallback: "Profile Already Exists"
            color: Theme.textColor
            font: Theme.headingFont
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: TranslationManager.translate("visualizer.duplicate.message",
                "A profile named \"%1\" already exists.\n\nWhat would you like to do?").replace("%1", resolver.profileTitle)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textColor
            font: Theme.bodyFont
        }

        Text {
            Layout.fillWidth: true
            visible: resolver.errorMessage !== ""
            text: resolver.errorMessage
            color: Theme.errorColor
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacingMedium

            AccessibleButton {
                text: TranslationManager.translate("visualizer.button.overwrite", "Overwrite")
                enabled: !resolver.resolutionPending
                accessibleName: TranslationManager.translate("visualizerBrowser.overwriteExisting",
                    "Overwrite existing profile with imported version")
                destructive: true
                onClicked: resolver.requestResolution("saveOverwrite", {})
            }
            AccessibleButton {
                primary: true
                enabled: !resolver.resolutionPending
                text: TranslationManager.translate("visualizer.button.saveAsNew", "Save as New")
                accessibleName: TranslationManager.translate("visualizerBrowser.saveAsNewProfile",
                    "Save as a new profile with different name")
                onClicked: {
                    nameField.text = resolver.profileTitle + " (copy)"
                    resolver.errorMessage = ""
                    resolver.showNameInput = true
                    nameField.forceActiveFocus()
                }
            }
            AccessibleButton {
                text: TranslationManager.translate("visualizer.button.cancel", "Cancel")
                enabled: !resolver.resolutionPending
                accessibleName: TranslationManager.translate("visualizerBrowser.cancelImport", "Cancel import")
                onClicked: resolver.cancel()
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -resolver.keyboardOffset
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        spacing: Theme.spacingLarge
        visible: resolver.showNameInput

        Tr {
            key: "visualizer.newName.title"
            fallback: "Enter New Name"
            color: Theme.textColor
            font: Theme.headingFont
            Layout.alignment: Qt.AlignHCenter
        }

        StyledTextField {
            id: nameField
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)
            selectByMouse: true
            inputMethodHints: Qt.ImhNoAutoUppercase
            placeholder: TranslationManager.translate("visualizer.newName.placeholder", "Profile name")
            accessibleName: TranslationManager.translate("visualizer.newName.placeholder", "Profile name")
            Keys.onReturnPressed: {
                Keyboard.commit()
                if (text.trim().length > 0) {
                    resolver.requestResolution("saveWithNewName", { "name": text.trim() })
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: resolver.errorMessage !== ""
            text: resolver.errorMessage
            color: Theme.errorColor
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacingMedium

            AccessibleButton {
                primary: true
                enabled: VisualizerProfileImportService.ready
                    && !resolver.resolutionPending && nameField.text.trim().length > 0
                text: TranslationManager.translate("visualizer.button.save", "Save")
                accessibleName: TranslationManager.translate("visualizerBrowser.saveWithNewName",
                    "Save profile with the new name")
                onClicked: {
                    Keyboard.commit()
                    resolver.requestResolution("saveWithNewName",
                        { "name": nameField.text.trim() })
                }
            }
            AccessibleButton {
                text: TranslationManager.translate("visualizer.button.back", "Back")
                enabled: !resolver.resolutionPending
                accessibleName: TranslationManager.translate("visualizerBrowser.goBackToChoose",
                    "Go back to choose a different option")
                onClicked: {
                    resolver.errorMessage = ""
                    resolver.showNameInput = false
                }
            }
        }
    }
}
