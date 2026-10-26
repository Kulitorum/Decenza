pragma ComponentBehavior: Bound

import QtQml
import Decenza

// One acknowledged chunk at a time; the selected local path never leaves this
// requester-side component. ImportService receives bytes plus a transfer ID.
QtObject {
    id: transfer

    property var urls: []
    property string kind: ""
    property var options: ({})
    property var selectedFiles: []
    property string selectionRequestId: ""
    property string readerRequestId: ""
    property string operationRequestId: ""
    property string operationName: ""
    property string transferId: ""
    property int fileIndex: 0
    property double offset: 0
    property int chunkLength: 0
    property bool lastChunk: false
    property bool busy: false
    // True only while a browser directory picker/inspection is outstanding.
    // Once files have been accepted and transfer begins, this Cancel button
    // no longer applies to the upload itself.
    property bool directorySelectionPending: false

    Component.onDestruction: {
        if (readerRequestId.length > 0) LocalFileSelection.closeFile(readerRequestId)
        if (selectionRequestId.length > 0) LocalFileSelection.closeSelection(selectionRequestId)
        transfer.releasePendingTransfer()
    }

    function start(fileUrls: var, fileKind: string, commandOptions: var): bool {
        if (busy || !fileUrls || fileUrls.length === 0) return false
        return begin(fileKind, commandOptions, function() {
            LocalFileSelection.inspectFiles(selectionRequestId, urls, kind)
        }, fileUrls, false)
    }

    function startDirectory(fileKind: string, commandOptions: var): bool {
        if (busy) return false
        return begin(fileKind, commandOptions, function() {
            LocalFileSelection.selectDirectory(selectionRequestId, kind)
        }, [], true)
    }

    function begin(fileKind: string, commandOptions: var, select: var, fileUrls: var,
                   isDirectorySelection: bool): bool {
        urls = fileUrls
        kind = fileKind
        options = commandOptions || ({})
        selectedFiles = []
        selectionRequestId = LocalFileSelection.newRequestId()
        readerRequestId = ""
        transferId = ""
        fileIndex = 0
        offset = 0
        chunkLength = 0
        directorySelectionPending = isDirectorySelection
        busy = true
        select()
        return true
    }

    function cancelDirectorySelection(): bool {
        if (!directorySelectionPending || !busy) return false
        abort("cancelled")
        return true
    }

    function command(operation: string, args: var): void {
        operationName = operation
        operationRequestId = ImportService.execute(operation, args || ({}))
        if (operationRequestId.length === 0)
            abort("serviceUnavailable")
    }

    function beginTransfer() {
        const args = Object.assign({}, options, {
            "kind": kind,
            "files": selectedFiles
        })
        command("beginTransfer", args)
    }

    function readFile() {
        readerRequestId = LocalFileSelection.newRequestId()
        LocalFileSelection.openSelectedFile(selectionRequestId, fileIndex, readerRequestId)
    }

    function readChunk() {
        LocalFileSelection.readNextChunk(readerRequestId)
    }

    function abort(reason: string): void {
        if (readerRequestId.length > 0) LocalFileSelection.closeFile(readerRequestId)
        if (selectionRequestId.length > 0) LocalFileSelection.closeSelection(selectionRequestId)
        releasePendingTransfer()
        busy = false
        directorySelectionPending = false
        readerRequestId = ""
        selectionRequestId = ""
        transferId = ""
        operationRequestId = ""
        operationName = ""
        finished(false, ({}), reason)
    }

    function releasePendingTransfer() {
        if (transferId.length > 0)
            ImportService.execute("cancelTransfer", {transferId: transferId})
        else if (operationName === "beginTransfer" && operationRequestId.length > 0)
            ImportService.execute("cancelTransfer", {requestId: operationRequestId})
    }

    function advanceFile() {
        LocalFileSelection.closeFile(readerRequestId)
        readerRequestId = ""
        fileIndex += 1
        offset = 0
        if (fileIndex < selectedFiles.length) {
            readFile()
        } else {
            command("commitTransfer", { "transferId": transferId })
        }
    }

    readonly property Connections localFileEvents: Connections {
        target: LocalFileSelection
        function onInspectionCompleted(requestId, success, files, reason) {
            if (requestId !== transfer.selectionRequestId) return
            transfer.directorySelectionPending = false
            if (!success) { transfer.abort(reason); return }
            transfer.selectedFiles = files
            transfer.beginTransfer()
        }
        function onFileOpened(requestId, success, fileName, fileSize, reason) {
            if (requestId !== transfer.readerRequestId) return
            const expected = transfer.selectedFiles[transfer.fileIndex]
            if (!success || !expected || fileName !== expected.name || fileSize !== expected.size) {
                transfer.abort(success ? "fileChanged" : reason)
                return
            }
            transfer.readChunk()
        }
        function onChunkReady(requestId, offset, data, lastChunk, reason) {
            if (requestId !== transfer.readerRequestId) return
            // QByteArray arrives as ArrayBuffer (Qt 6.12 qv4engine.cpp:1845-1846),
            // whose size is byteLength (qv4arraybuffer.cpp:215), not array length.
            const size = data.byteLength
            if (reason.length > 0 || !Number.isSafeInteger(size) || size <= 0 || size > 48 * 1024) {
                transfer.abort(reason.length > 0 ? reason : "readFailed")
                return
            }
            transfer.lastChunk = lastChunk
            transfer.chunkLength = size
            transfer.command("appendTransferChunk", {
                "transferId": transfer.transferId,
                "fileIndex": transfer.fileIndex,
                "offset": offset,
                "data": data
            })
        }
    }

    readonly property Connections importEvents: Connections {
        target: ImportService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId !== transfer.operationRequestId
                    || operation !== transfer.operationName) return
            transfer.operationRequestId = ""
            if (!success) { transfer.abort(reason); return }
            if (operation === "beginTransfer") {
                transfer.transferId = result.transferId || ""
                if (transfer.transferId.length === 0) { transfer.abort("invalidTransfer"); return }
                transfer.readFile()
            } else if (operation === "appendTransferChunk") {
                if (result.nextOffset !== transfer.offset + transfer.chunkLength) {
                    transfer.abort("invalidTransferOffset")
                    return
                }
                LocalFileSelection.acknowledgeChunk(transfer.readerRequestId)
                transfer.offset = result.nextOffset
                if (result.fileComplete === true) transfer.advanceFile()
                else transfer.readChunk()
            } else if (operation === "commitTransfer") {
                LocalFileSelection.closeFile(transfer.readerRequestId)
                transfer.readerRequestId = ""
                LocalFileSelection.closeSelection(transfer.selectionRequestId)
                transfer.transferId = ""
                transfer.busy = false
                transfer.directorySelectionPending = false
                transfer.finished(true, result, "")
            }
        }
    }

    signal finished(bool success, var result, string reason)
}
