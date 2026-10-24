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
        dialog.actionsEnabled = true
        confirmedSpy.clear()
        deniedSpy.clear()
    }

    function cleanup() {
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
}
