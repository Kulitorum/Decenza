pragma ComponentBehavior: Bound

import QtQml
import Decenza

// One requester-owned host export: asks a service to prepare a bundle, then
// saves it through LocalFileDownload. Callers forward the service's
// operationCompleted signal to completeOperation().
QtObject {
    id: hostDownload
    required property var executeOperation
    property bool ready: true
    readonly property bool busy: prepareRequestId.length > 0 || fileDownload.busy
    property string prepareRequestId: ""
    property string prepareOperation: ""
    property bool saveCompletionSeen: false

    Component.onDestruction: hostDownload.cancel()

    function prepare(operation: string, args: var): bool {
        if (!ready || busy) return false
        prepareOperation = operation
        prepareRequestId = executeOperation(operation, args)
        return prepareRequestId.length > 0
    }

    function cancel() {
        const pending = prepareRequestId
        if (pending.length > 0)
            executeOperation("cancelFileOperation", { "requestId": pending })
        hostDownload.fileDownload.cancelSilently()
        prepareRequestId = ""
    }

    function completeOperation(id: string, operation: string, success: bool, result: var, reason: string): void {
        hostDownload.fileDownload.completeOperation(id, operation, success, result, reason)
        if (id !== prepareRequestId || operation !== prepareOperation) return
        prepareRequestId = ""
        if (!success) {
            hostDownload.finished(false, reason)
            return
        }
        saveCompletionSeen = false
        if (hostDownload.fileDownload.start(result)) return
        if (!saveCompletionSeen) {
            executeOperation("releaseBundle", { "bundleId": result.bundleId })
            hostDownload.finished(false, "saveUnavailable")
        }
    }

    readonly property LocalFileDownload fileDownload: LocalFileDownload {
        executeOperation: hostDownload.executeOperation
        onFinished: function(success, reason) {
            hostDownload.saveCompletionSeen = true
            hostDownload.finished(success, reason)
        }
    }

    signal finished(bool success, string reason)
}
