import QtQuick
import QtQuick.Controls
import QtTest
import Decenza 1.0

TestCase {
    id: testCase
    name: "McpConfirmDialog"
    when: windowShown
    visible: true
    width: 640
    height: 600

    McpConfirmDialog { id: dialog }
    CrashReportDialog { id: crashDialog }
    SignalSpy { id: confirmedSpy; target: dialog; signalName: "confirmed" }
    SignalSpy { id: deniedSpy; target: dialog; signalName: "denied" }

    // Locate the real control through its public accessibility label; do not
    // invoke clicked() directly, which would bypass the enabled/pointer path.
    function buttonWithName(item, accessibleName) {
        if (item.accessibleName === accessibleName)
            return item
        for (const child of item.children || []) {
            const button = buttonWithName(child, accessibleName)
            if (button)
                return button
        }
        return null
    }

    function textContaining(item, value) {
        if (typeof item.text === "string" && item.text.indexOf(value) >= 0)
            return item
        for (const child of item.children || []) {
            const textItem = textContaining(child, value)
            if (textItem)
                return textItem
        }
        return null
    }

    function openConfirmation(confirmationId, description) {
        dialog.confirmationId = confirmationId
        dialog.toolDescription = description
        dialog.deadlineEpochMs = Date.now() + 15000
        dialog.open()
        tryCompare(dialog, "opened", true)
        const body = textContaining(dialog.contentItem, description)
        verify(body !== null)
        verify(body.visible)
        verify(dialog.countdown > 0)
    }

    function init() {
        GlobalSupportService.resetForTest()
        AccessibilityManager.lastAnnouncement = ""
        crashDialog.dialogState = "prompt"
        crashDialog.submitRequestId = ""
        crashDialog.notesValidationMessage = ""
        crashDialog.issueUrl = ""
        crashDialog.errorMessage = ""
        dialog.actionsEnabled = true
        confirmedSpy.clear()
        deniedSpy.clear()
    }

    function cleanup() {
        crashDialog.close()
        tryCompare(crashDialog, "visible", false)
        dialog.userResponded = true
        dialog.close()
        tryCompare(dialog, "visible", false)
    }

    function test_denyThenAllowEchoesDistinctIdsExactlyOnce() {
        openConfirmation("confirmation-A", "Change the test profile temperature")
        const deny = buttonWithName(dialog.contentItem, "Deny AI action")
        verify(deny !== null)
        mouseClick(deny)
        tryCompare(dialog, "visible", false)
        compare(deniedSpy.count, 1)
        compare(deniedSpy.signalArguments[0][0], "confirmation-A")
        compare(confirmedSpy.count, 0)

        openConfirmation("confirmation-B", "Rename the test profile")
        const allow = buttonWithName(dialog.contentItem, "Allow AI action: Rename the test profile")
        verify(allow !== null)
        mouseClick(allow)
        tryCompare(dialog, "visible", false)
        compare(confirmedSpy.count, 1)
        compare(confirmedSpy.signalArguments[0][0], "confirmation-B")
        // Closing after Allow must not also report Deny or replay event A.
        compare(deniedSpy.count, 1)
        compare(deniedSpy.signalArguments[0][0], "confirmation-A")
    }

    function test_disabledActionsCannotApprove() {
        openConfirmation("confirmation-stale", "Change the test profile temperature")
        const allow = buttonWithName(dialog.contentItem,
            "Allow AI action: Change the test profile temperature")
        const deny = buttonWithName(dialog.contentItem, "Deny AI action")
        verify(allow !== null)
        verify(deny !== null)
        dialog.actionsEnabled = false
        compare(allow.enabled, false)
        compare(deny.enabled, false)
        mouseClick(allow)
        mouseClick(deny)
        compare(confirmedSpy.count, 0)
        compare(deniedSpy.count, 0)
        compare(dialog.visible, true)
    }

    function notesInput(item) {
        if ("placeholderText" in item && item.Accessible.name === "What were you doing?")
            return item
        for (const child of item.children || []) {
            const input = notesInput(child)
            if (input)
                return input
        }
        return null
    }

    function test_crashNotesLimitPreservesDraftAndAllowsEditableRecovery() {
        crashDialog.open()
        tryCompare(crashDialog, "opened", true)
        const notes = notesInput(crashDialog.contentItem)
        verify(notes !== null)
        const send = buttonWithName(crashDialog.contentItem, "Send crash report")
        verify(send !== null)
        const oversized = "a".repeat(4001)
        notes.text = oversized
        mouseClick(send)
        compare(crashDialog.dialogState, "prompt")
        compare(notes.text, oversized)
        compare(GlobalSupportService.requests.length, 0)
        verify(crashDialog.notesValidationMessage.indexOf("4000") >= 0)
        compare(AccessibilityManager.lastAnnouncement, crashDialog.notesValidationMessage)
        const validation = textContaining(crashDialog.contentItem, "4000 characters")
        verify(validation !== null)
        verify(validation.visible)

        // Editing remains possible after refusal; the exact boundary is sent
        // without truncation through the shipping button and dialog function.
        notes.text = "a".repeat(3999)
        notes.forceActiveFocus()
        keyClick(Qt.Key_B)
        compare(notes.text.length, 4000)
        compare(crashDialog.notesValidationMessage, "")
        mouseClick(send)
        compare(GlobalSupportService.requests.length, 1)
        compare(GlobalSupportService.requests[0].arguments.userNotes, notes.text)
        compare(crashDialog.dialogState, "submitting")
        const admittedId = crashDialog.submitRequestId
        verify(admittedId.length > 0)
        GlobalSupportService.operationCompleted("someone-else", "submitCrashReport", true,
                                               {issueUrl: "https://example.test/unrelated"}, "")
        compare(crashDialog.dialogState, "submitting")
        compare(crashDialog.submitRequestId, admittedId)
        GlobalSupportService.operationCompleted(admittedId, "submitCrashReport", false, {}, "fixtureFailure")
        compare(crashDialog.dialogState, "error")
        compare(notes.text.length, 4000)
        compare(crashDialog.submitRequestId, "")
        const retry = buttonWithName(crashDialog.contentItem, "Retry sending crash report")
        verify(retry !== null)
        mouseClick(retry)
        compare(GlobalSupportService.requests.length, 2)
        const retriedId = crashDialog.submitRequestId
        verify(retriedId !== admittedId)
        GlobalSupportService.operationCompleted(admittedId, "submitCrashReport", true,
                                               {issueUrl: "https://example.test/stale"}, "")
        compare(crashDialog.dialogState, "submitting")
        GlobalSupportService.operationCompleted(retriedId, "submitCrashReport", true,
                                               {issueUrl: "https://example.test/recovered"}, "")
        compare(crashDialog.dialogState, "success")
        compare(crashDialog.issueUrl, "https://example.test/recovered")
    }

    function test_crashNotesRejectUnsupportedControlCharactersWithoutLosingDraft() {
        crashDialog.open()
        tryCompare(crashDialog, "opened", true)
        const notes = notesInput(crashDialog.contentItem)
        verify(notes !== null)
        notes.text = "keep\u0001draft"
        mouseClick(buttonWithName(crashDialog.contentItem, "Send crash report"))
        compare(crashDialog.dialogState, "prompt")
        compare(notes.text, "keep\u0001draft")
        compare(GlobalSupportService.requests.length, 0)
        verify(crashDialog.notesValidationMessage.length > 0)
        notes.text = "keep\tdraft\nwith details"
        mouseClick(buttonWithName(crashDialog.contentItem, "Send crash report"))
        compare(GlobalSupportService.requests.length, 1)
        compare(GlobalSupportService.requests[0].arguments.userNotes, notes.text)
    }
}
