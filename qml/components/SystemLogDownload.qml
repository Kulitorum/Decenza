pragma ComponentBehavior: Bound

import QtQml
import Decenza

// One requester-owned log export, using the same save boundary on native/Wasm.
HostFileDownload {
    id: logDownload
    property bool active: false
    ready: active && SystemLogService.ready
    executeOperation: function(operation, args) { return SystemLogService.execute(operation, args) }

    onActiveChanged: SystemLogService.setViewActive(logDownload, active || busy)
    onBusyChanged: SystemLogService.setViewActive(logDownload, active || busy)
    Component.onCompleted: SystemLogService.setViewActive(logDownload, active || busy)
    Component.onDestruction: SystemLogService.setViewActive(logDownload, false)

    function start(format: string): bool {
        if (format !== "log" && format !== "zip") return false
        return prepare("prepareLogDownload", { "format": format })
    }

    readonly property Connections serviceEvents: Connections {
        target: SystemLogService
        function onOperationCompleted(id, operation, success, result, reason) {
            logDownload.completeOperation(id, operation, success, result, reason)
        }
    }
}
