import QtQuick
import Decenza

// One coordinator shared by the native and Wasm shells. Dialog state is local to
// this client; decisions are correlated with exact host event IDs.
Item {
    id: root
    anchors.fill: parent

    // The native shell uses these aliases to preserve its modal queuing and
    // screensaver close behavior while the dialogs themselves are shared.
    property alias crashReportDialog: crashDialog
    property alias de1CommunicationErrorDialog: de1Dialog
    property alias profileRefusedDialog: profileDialog
    property alias mcpConfirmDialog: mcpDialog

    property bool crashPromptHandled: false
    property string crashOpenRequestId: ""
    property string crashDismissRequestId: ""
    property string de1CommunicationEventId: ""
    property string de1AcknowledgeRequestId: ""
    property string profileDismissRequestId: ""
    property string mcpResolveRequestId: ""

    signal crashPromptFinished()
    signal blockingDialogClosed()

    function syncDialogs(): void {
        if (!GlobalSupportService.ready)
            return

        const state = GlobalSupportService.state

        if (state.crashAvailable) {
            if (!crashPromptHandled && crashOpenRequestId.length === 0) {
                crashPromptHandled = true
                crashOpenRequestId = GlobalSupportService.openCrashReport()
                if (crashOpenRequestId.length === 0)
                    crashPromptHandled = false
            }
        } else {
            crashPromptHandled = false
            crashOpenRequestId = ""
            if (crashDialog.visible)
                crashDialog.close()
        }

        if (state.de1CommunicationFailure) {
            de1CommunicationEventId = state.de1CommunicationEventId
            if (!de1Dialog.visible)
                de1Dialog.open()
        } else {
            de1CommunicationEventId = ""
            if (de1Dialog.visible)
                de1Dialog.close()
        }

        if (state.profileRefusalEventId.length > 0) {
            profileDialog.eventId = state.profileRefusalEventId
            profileDialog.profileFilename = state.profileRefusal.filename || ""
            profileDialog.profileTitle = state.profileRefusal.title || ""
            profileDialog.unsupportedKeys = state.profileRefusal.unsupportedKeys || []
            profileDialog.malformedValues = state.profileRefusal.malformedValues || []
            if (!profileDialog.visible && profileDismissRequestId.length === 0)
                profileDialog.open()
        } else {
            profileDialog.eventId = ""
            if (profileDialog.visible)
                profileDialog.close()
        }

        if (state.mcpConfirmationId.length > 0) {
            mcpDialog.deadlineEpochMs = GlobalSupportService.mcpConfirmationDeadlineMs
            if (mcpDialog.confirmationId !== state.mcpConfirmationId) {
                if (mcpDialog.visible) {
                    mcpDialog.userResponded = true
                    mcpDialog.close()
                }
                mcpDialog.confirmationId = state.mcpConfirmationId
            }
            mcpDialog.toolDescription = state.mcpToolDescription
            if (!mcpDialog.visible && mcpResolveRequestId.length === 0)
                mcpDialog.open()
        } else if (mcpDialog.visible) {
            mcpDialog.userResponded = true
            mcpDialog.close()
        }
    }

    Connections {
        target: GlobalSupportService
        function onStateChanged() { root.syncDialogs() }
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === root.crashOpenRequestId && operation === "openCrashReport") {
                root.crashOpenRequestId = ""
                if (success && GlobalSupportService.state.crashAvailable) {
                    crashDialog.crashLog = result.crashLog || ""
                    crashDialog.debugLogTail = result.debugLogTail || ""
                    crashDialog.open()
                    crashDialog.syncSubmissionState()
                } else {
                    root.crashPromptHandled = false
                }
                return
            }
            if (requestId === root.crashDismissRequestId
                && operation === "dismissCrashReport") {
                root.crashDismissRequestId = ""
                root.crashPromptFinished()
                return
            }
            if (requestId === root.de1AcknowledgeRequestId
                && operation === "acknowledgeDe1CommunicationFailure") {
                root.de1AcknowledgeRequestId = ""
                if (!success)
                    root.syncDialogs()
                return
            }
            if (requestId === root.profileDismissRequestId
                && operation === "dismissProfileRefusal") {
                root.profileDismissRequestId = ""
                if (!success)
                    root.syncDialogs()
                return
            }
            if (requestId === root.mcpResolveRequestId
                && operation === "resolveMcpConfirmation") {
                root.mcpResolveRequestId = ""
                if (!success)
                    root.syncDialogs()
            }
        }
    }

    CrashReportDialog {
        id: crashDialog
        onClosed: root.blockingDialogClosed()
        onDismissed: root.dismissCrashReport()
        onReported: root.dismissCrashReport()
    }

    De1CommunicationErrorDialog {
        id: de1Dialog
        onClosed: root.blockingDialogClosed()
        actionsEnabled: GlobalSupportService.ready
            && root.de1AcknowledgeRequestId.length === 0
            && root.de1CommunicationEventId.length > 0
        onAcknowledgeRequested: {
            root.de1AcknowledgeRequestId = GlobalSupportService.execute(
                "acknowledgeDe1CommunicationFailure",
                { "eventId": root.de1CommunicationEventId })
        }
    }

    ProfileRefusedDialog {
        id: profileDialog
        onClosed: root.blockingDialogClosed()
        actionsEnabled: GlobalSupportService.ready
            && root.profileDismissRequestId.length === 0
            && profileDialog.eventId.length > 0
        onDismissRequested: function(eventId) {
            root.profileDismissRequestId = GlobalSupportService.execute(
                "dismissProfileRefusal", { "eventId": eventId })
        }
    }

    McpConfirmDialog {
        id: mcpDialog
        actionsEnabled: GlobalSupportService.ready
            && mcpDialog.confirmationId.length > 0
            && root.mcpResolveRequestId.length === 0
        onClosed: root.syncDialogs()
        onConfirmed: function(confirmationId) {
            root.mcpResolveRequestId = GlobalSupportService.execute(
                "resolveMcpConfirmation", {
                    "confirmationId": confirmationId,
                    "accepted": true,
                    "timedOut": false
                })
        }
        onDenied: function(confirmationId) {
            root.mcpResolveRequestId = GlobalSupportService.execute(
                "resolveMcpConfirmation", {
                    "confirmationId": confirmationId,
                    "accepted": false,
                    "timedOut": false
                })
        }
    }

    function dismissCrashReport(): void {
        if (crashDismissRequestId.length > 0)
            return
        crashDismissRequestId = GlobalSupportService.execute("dismissCrashReport")
        if (crashDismissRequestId.length === 0)
            crashPromptFinished()
    }

    // Bean Search Apply: the outcome, wherever the user is by then (the new-bag
    // window has already closed when its Apply runs).
    Connections {
        target: BeanUiService
        function onCommunitySuggestionApplied(key, ok, recipeName, selected) {
            var message
            if (ok && selected)
                message = TranslationManager.translate("main.toast.communityApplied", "Saved and selected recipe %1").arg(recipeName)
            else if (ok)
                message = TranslationManager.translate("main.toast.communitySaved", "Saved recipe %1").arg(recipeName)
            else if (recipeName !== "")
                message = TranslationManager.translate("main.toast.communityNotSelected", "Saved recipe %1, but couldn't select it").arg(recipeName)
            else
                message = TranslationManager.translate("main.toast.communityApplyFailed", "Couldn't apply the community suggestion")
            communityApplyToast.error = !ok
            communityApplyToast.show(message)
            if (AccessibilityManager.enabled)
                AccessibilityManager.announce(message)
        }
    }
    StatusToast { id: communityApplyToast }

    Component.onCompleted: syncDialogs()
}
