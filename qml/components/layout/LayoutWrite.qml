pragma ComponentBehavior: Bound

import QtQml
import Decenza

// Correlates one layout edit and leaves state host-backed on refusal.
QtObject {
    id: write
    property string pendingRequestId: ""
    property string pendingOperation: ""
    property string errorMessage: ""
    readonly property bool busy: pendingRequestId.length > 0
    readonly property bool ready: LayoutService.ready && !busy

    function submit(operation: string, fields: var, expectedRevision: var): string {
        if (!ready) {
            errorMessage = TranslationManager.translate("common.settingsChangedRetry",
                "The setting could not be saved. Review the current value and try again.")
            failed(errorMessage)
            return ""
        }
        errorMessage = ""
        pendingOperation = operation
        pendingRequestId = LayoutService.execute(operation, fields, expectedRevision)
        if (pendingRequestId.length === 0) {
            pendingOperation = ""
            errorMessage = TranslationManager.translate("common.settingsChangedRetry",
                "The setting could not be saved. Review the current value and try again.")
            failed(errorMessage)
        }
        return pendingRequestId
    }

    readonly property Connections completion: Connections {
        target: LayoutService
        function onOperationCompleted(requestId, operation, success, revision, layoutConfiguration, reason) {
            if (requestId !== write.pendingRequestId || operation !== write.pendingOperation)
                return
            write.pendingRequestId = ""
            write.pendingOperation = ""
            write.finished(requestId, operation, success, reason)
            if (!success) {
                write.errorMessage = reason === "conflict"
                    ? TranslationManager.translate("settings.layout.conflict", "The layout changed elsewhere. Review it and try again.")
                    : TranslationManager.translate("settings.layout.saveFailed", "The layout change could not be saved. Try again.")
                write.failed(write.errorMessage)
            }
        }
    }

    signal finished(string requestId, string operation, bool success, string reason)
    signal failed(string message)
}
