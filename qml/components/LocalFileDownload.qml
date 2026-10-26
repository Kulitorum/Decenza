pragma ComponentBehavior: Bound

import QtQml
import Decenza

// Pulls host-owned output by opaque ID and saves it on the requesting client.
// All payloads remain <=48 KiB; host paths and whole-file QByteArrays never
// cross QtRO.
QtObject {
    id: download

    // Callers bind a statically resolved service execute() and forward that
    // service's operationCompleted signal to completeOperation().
    required property var executeOperation
    property bool downloadDirectly: false
    property bool busy: false
    property bool waitingForSave: false
    property string bundleId: ""
    property string displayName: ""
    property string requestId: ""
    property string operationId: ""
    property string operationName: ""
    property double totalBytes: 0
    property double offset: 0

    Component.onDestruction: download.cancelSilently()

    function start(bundle: var): bool {
        if (busy || typeof executeOperation !== "function" || !bundle || typeof bundle.bundleId !== "string"
                || bundle.bundleId.length === 0 || bundle.bundleId.length > 128
                || typeof bundle.displayName !== "string"
                || bundle.displayName.length === 0 || bundle.displayName.length > 128
                || !Number.isSafeInteger(bundle.size) || bundle.size <= 0)
            return false

        const mimeType = bundle.mimeType || "application/zip"
        const filter = bundle.filter || "ZIP archives (*.zip)"
        const isJson = mimeType === "application/json" && filter === "JSON files (*.json)"
        const isZip = mimeType === "application/zip" && filter === "ZIP archives (*.zip)"
        const isLog = mimeType === "text/plain" && filter === "Log files (*.log)"
        const isText = mimeType === "text/plain" && filter === "Text files (*.txt)"
        if ((!isJson && !isZip && !isLog && !isText)
                || (isLog && bundle.size > 4 * 1024 * 1024))
            return false

        bundleId = bundle.bundleId
        displayName = bundle.displayName
        totalBytes = bundle.size
        offset = 0
        requestId = LocalFileSelection.newRequestId()
        busy = true
        waitingForSave = true
        if (!LocalFileSelection.beginSaveFile(requestId, displayName, totalBytes, mimeType, filter, downloadDirectly)) {
            fail("saveCancelled")
            return false
        }
        return true
    }

    function readChunk() {
        operationName = "readBundleChunk"
        operationId = executeOperation(operationName, {
            "bundleId": bundleId,
            "offset": offset
        })
        if (operationId.length === 0) fail("serviceUnavailable")
    }

    function fail(reason: string): void {
        const saveId = requestId
        const releaseId = bundleId
        busy = false
        waitingForSave = false
        requestId = ""
        operationId = ""
        operationName = ""
        bundleId = ""
        if (saveId.length > 0) LocalFileSelection.cancelSaveFile(saveId)
        if (releaseId.length > 0)
            executeOperation("releaseBundle", { "bundleId": releaseId })
        finished(false, reason)
    }

    function cancelSilently() {
        const saveId = requestId
        const releaseId = bundleId
        busy = false
        waitingForSave = false
        requestId = ""
        operationId = ""
        operationName = ""
        bundleId = ""
        if (saveId.length > 0) LocalFileSelection.cancelSaveFile(saveId)
        if (releaseId.length > 0)
            executeOperation("releaseBundle", { "bundleId": releaseId })
    }

    readonly property Connections localSaveEvents: Connections {
        target: LocalFileSelection
        function onSaveFileReady(id, success, reason) {
            if (!download.busy || !download.waitingForSave || id !== download.requestId) return
            download.waitingForSave = false
            if (!success) { download.fail(reason); return }
            download.readChunk()
        }
    }

    signal finished(bool success, string reason)

    function completeOperation(id: string, operation: string, success: bool, result: var, reason: string): void {
        if (id !== download.operationId || operation !== download.operationName) return
        download.operationId = ""
        if (!success) { download.fail(reason); return }
        if (operation !== "readBundleChunk") return

        const bytes = result.data
        const nextOffset = result.nextOffset
        if (!bytes || !Number.isSafeInteger(nextOffset)
                || nextOffset <= download.offset
                || nextOffset > download.offset + 48 * 1024
                || nextOffset > download.totalBytes
                || !LocalFileSelection.appendSaveFileChunk(
                    download.requestId, download.offset, bytes)) {
            download.fail("invalidBundleChunk")
            return
        }
        download.offset = nextOffset
        if (result.complete === true) {
            if (download.offset !== download.totalBytes
                    || !LocalFileSelection.finishSaveFile(download.requestId)) {
                download.fail("incompleteBundle")
                return
            }
            download.requestId = ""
            executeOperation("releaseBundle", { "bundleId": download.bundleId })
            download.bundleId = ""
            download.busy = false
            download.finished(true, "")
        } else {
            if (download.offset >= download.totalBytes) {
                download.fail("incompleteBundle")
                return
            }
            download.readChunk()
        }
    }
}
