pragma ComponentBehavior: Bound

import QtQml
import Decenza

// One requester-owned log export, using the same save boundary on native/Wasm.
QtObject {
    id: logDownload
    property bool active: false
    readonly property bool ready: active && SystemLogService.ready
    readonly property bool busy: prepareRequestId.length > 0 || fileDownload.busy
    property string prepareRequestId: ""
    property bool saveCompletionSeen: false

    onActiveChanged: SystemLogService.setViewActive(logDownload, active || busy)
    onBusyChanged: SystemLogService.setViewActive(logDownload, active || busy)
    Component.onCompleted: SystemLogService.setViewActive(logDownload, active || busy)
    Component.onDestruction: {
        logDownload.cancel()
        SystemLogService.setViewActive(logDownload, false)
    }

    function start(format: string): bool {
        if (!ready || busy || (format !== "log" && format !== "zip")) return false
        prepareRequestId = SystemLogService.execute("prepareLogDownload", { "format": format })
        return prepareRequestId.length > 0
    }

    function cancel() {
        const pending = prepareRequestId
        if (pending.length > 0)
            SystemLogService.execute("cancelFileOperation", { "requestId": pending })
        logDownload.fileDownload.cancelSilently()
        prepareRequestId = ""
    }

    readonly property LocalFileDownload fileDownload: LocalFileDownload {
        executeOperation: function(operation, args) { return SystemLogService.execute(operation, args) }
        onFinished: function(success, reason) {
            logDownload.saveCompletionSeen = true
            logDownload.finished(success, reason)
        }
    }
    readonly property Connections serviceEvents: Connections {
        target: SystemLogService
        function onOperationCompleted(id, operation, success, result, reason) {
            logDownload.fileDownload.completeOperation(id, operation, success, result, reason)
            if (id !== logDownload.prepareRequestId || operation !== "prepareLogDownload") return
            logDownload.prepareRequestId = ""
            if (!success) logDownload.finished(false, reason)
            else {
                logDownload.saveCompletionSeen = false
                if (logDownload.fileDownload.start(result)) return
                if (!logDownload.saveCompletionSeen) {
                    SystemLogService.execute("releaseBundle", { "bundleId": result.bundleId })
                    logDownload.finished(false, "saveUnavailable")
                }
            }
        }
    }

    signal finished(bool success, string reason)
}
