import QtQuick
import QtTest
import Decenza 1.0

TestCase {
    id: testCase
    name: "LocalFileTransfer"
    when: windowShown
    width: 320
    height: 200

    property int finishCount: 0
    property bool lastSuccess: true
    property string lastReason: ""
    property var lastResult: ({})
    property var downloadCalls: []
    property int downloadFinishCount: 0
    property bool downloadSuccess: false
    property string downloadReason: ""

    LocalFileDownload {
        id: download
        executeOperation: function(operation, args) {
            const id = "download-" + (testCase.downloadCalls.length + 1)
            testCase.downloadCalls = testCase.downloadCalls.concat([{id: id, operation: operation, args: args}])
            return id
        }
        onFinished: function(success, reason) {
            testCase.downloadFinishCount++
            testCase.downloadSuccess = success
            testCase.downloadReason = reason
        }
    }

    LocalFileTransfer {
        id: transfer
        onFinished: function(success, result, reason) {
            testCase.finishCount++
            testCase.lastSuccess = success
            testCase.lastResult = result
            testCase.lastReason = reason
        }
    }

    Component {
        id: transientTransfer
        LocalFileTransfer {}
    }

    function init() {
        download.cancelSilently()
        download.downloadDirectly = false
        downloadCalls = []
        downloadFinishCount = 0
        downloadSuccess = false
        downloadReason = ""
        finishCount = 0
        lastSuccess = true
        lastReason = ""
        lastResult = ({})
        LocalFileSelection.nextId = 0
        LocalFileSelection.lastSelectionId = ""
        LocalFileSelection.lastClosedSelectionId = ""
        LocalFileSelection.acceptSave = true
        LocalFileSelection.acceptChunk = true
        LocalFileSelection.acceptFinish = true
        LocalFileSelection.cancelledSaveId = ""
        ImportService.nextId = 0
        ImportService.calls = []
    }

    function cleanup() {
        if (transfer.busy) transfer.abort("testCleanup")
    }

    function jsonBundle() {
        return {bundleId: "a1234567-b123-c123-d123-e123456789ab", displayName: "translations-de.json",
                size: 4, mimeType: "application/json", filter: "JSON files (*.json)"}
    }

    function test_jsonDownloadUsesInjectedServiceAndIgnoresUnrelatedCompletions() {
        download.downloadDirectly = true
        verify(download.start(jsonBundle()))
        compare(downloadCalls.length, 0)
        verify(download.waitingForSave)
        compare(LocalFileSelection.savedDirectly, true)
        compare(LocalFileSelection.savedMimeType, "application/json")
        compare(LocalFileSelection.savedFilter, "JSON files (*.json)")
        LocalFileSelection.saveFileReady("another-save", true, "")
        compare(downloadCalls.length, 0)
        LocalFileSelection.saveFileReady(download.requestId, true, "")
        verify(!download.waitingForSave)
        compare(downloadCalls.length, 1)
        LocalFileSelection.saveFileReady(download.requestId, true, "")
        compare(downloadCalls.length, 1)
        compare(downloadCalls[0].operation, "readBundleChunk")
        compare(downloadCalls[0].args.offset, 0)
        const id = downloadCalls[0].id
        download.completeOperation("another-request", "readBundleChunk", false, {}, "wrong")
        download.completeOperation(id, "releaseBundle", false, {}, "wrong")
        compare(downloadFinishCount, 0)
        verify(download.busy)
        download.completeOperation(id, "readBundleChunk", true,
                                   {data: [1, 2], nextOffset: 2, complete: false}, "")
        compare(downloadCalls.length, 2)
        compare(downloadCalls[1].args.offset, 2)
        download.completeOperation(downloadCalls[1].id, "readBundleChunk", true,
                                   {data: [3, 4], nextOffset: 4, complete: true}, "")
        compare(downloadFinishCount, 1)
        verify(downloadSuccess)
        verify(!download.busy)
        compare(downloadCalls[2].operation, "releaseBundle")
        compare(downloadCalls[2].args.bundleId, jsonBundle().bundleId)
    }

    function test_logDownloadUsesTypedMetadataAndExistingChunkSavePath() {
        const bundle = {bundleId: jsonBundle().bundleId, displayName: "debug.log", size: 4,
                        mimeType: "text/plain", filter: "Log files (*.log)"}
        verify(download.start(bundle))
        compare(LocalFileSelection.savedMimeType, "text/plain")
        compare(LocalFileSelection.savedFilter, "Log files (*.log)")
        LocalFileSelection.saveFileReady(download.requestId, true, "")
        compare(downloadCalls.length, 1)
        download.completeOperation(downloadCalls[0].id, "readBundleChunk", true,
                                   {data: [76, 79, 71, 10], nextOffset: 4, complete: true}, "")
        compare(downloadFinishCount, 1)
        verify(downloadSuccess)
        compare(downloadCalls[1].operation, "releaseBundle")
        bundle.size = 4 * 1024 * 1024 + 1
        verify(!download.start(bundle))
        bundle.size = 4
        bundle.filter = "All files (*)"
        verify(!download.start(bundle))
        bundle.filter = "Log files (*.log)"
        bundle.mimeType = "text/html"
        verify(!download.start(bundle))
        compare(downloadFinishCount, 1)
    }

    function test_saveCancellationReleasesExactlyOnce() {
        LocalFileSelection.acceptSave = false
        verify(!download.start(jsonBundle()))
        compare(downloadFinishCount, 1)
        compare(downloadReason, "saveCancelled")
        compare(downloadCalls.length, 1)
        compare(downloadCalls[0].operation, "releaseBundle")
        download.cancelSilently()
        compare(downloadCalls.length, 1)
    }

    function test_failedChunkCancelsLocalSaveAndReleasesBundle() {
        verify(download.start(jsonBundle()))
        LocalFileSelection.saveFileReady(download.requestId, true, "")
        const saveId = download.requestId
        download.completeOperation(downloadCalls[0].id, "readBundleChunk", true,
                                   {data: [1], nextOffset: 5, complete: true}, "")
        compare(downloadReason, "invalidBundleChunk")
        compare(downloadFinishCount, 1)
        compare(LocalFileSelection.cancelledSaveId, saveId)
        compare(downloadCalls[1].operation, "releaseBundle")
        verify(!download.busy)
    }

    function test_zipDefaultIsPreservedAndUnsupportedMetadataDoesNotStartSave() {
        verify(download.start({bundleId: jsonBundle().bundleId, displayName: "profiles.zip", size: 4}))
        compare(LocalFileSelection.savedMimeType, "application/zip")
        compare(LocalFileSelection.savedFilter, "ZIP archives (*.zip)")
        download.cancelSilently()
        const bundle = jsonBundle()
        bundle.size = 8 * 1024 * 1024 + 1
        verify(download.start(bundle))
        compare(download.totalBytes, bundle.size)
        download.cancelSilently()
        bundle.size = 4
        bundle.mimeType = "text/html"
        verify(!download.start(bundle))
        compare(downloadFinishCount, 0)
    }

    function test_largeZipDownloadAcceptsSafeIntegerSizeWithoutAnArchiveCap() {
        const bundle = {bundleId: jsonBundle().bundleId, displayName: "backup.zip",
                        size: 3 * 1024 * 1024 * 1024}
        verify(download.start(bundle))
        compare(download.totalBytes, bundle.size)
        verify(download.waitingForSave)
        compare(downloadCalls.length, 0)
        download.cancelSilently()
        compare(downloadCalls.length, 1)
        compare(downloadCalls[0].operation, "releaseBundle")
        bundle.size = 9007199254740992
        verify(!download.start(bundle))
        compare(downloadCalls.length, 1)
        compare(downloadFinishCount, 0)
    }

    function test_asyncPickerCancellationReleasesWithoutRequestingBytes() {
        verify(download.start(jsonBundle()))
        const saveId = download.requestId
        compare(downloadCalls.length, 0)
        LocalFileSelection.saveFileReady(saveId, false, "saveCancelled")
        compare(downloadFinishCount, 1)
        compare(downloadReason, "saveCancelled")
        compare(downloadCalls.length, 1)
        compare(downloadCalls[0].operation, "releaseBundle")
        verify(!download.busy)
    }

    function test_cancellingPendingPickerIgnoresItsLateReadiness() {
        verify(download.start(jsonBundle()))
        const saveId = download.requestId
        compare(downloadCalls.length, 0)
        download.cancelSilently()
        compare(LocalFileSelection.cancelledSaveId, saveId)
        compare(downloadCalls.length, 1)
        compare(downloadCalls[0].operation, "releaseBundle")
        LocalFileSelection.saveFileReady(saveId, true, "")
        compare(downloadCalls.length, 1)
        compare(downloadFinishCount, 0)
        verify(!download.busy)
    }

    function test_cancelDirectorySelectionIgnoresLatePickerResult() {
        verify(transfer.startDirectory("shotFiles", {}))
        compare(transfer.busy, true)
        compare(transfer.directorySelectionPending, true)
        const cancelledId = transfer.selectionRequestId
        compare(LocalFileSelection.lastSelectionId, cancelledId)

        verify(transfer.cancelDirectorySelection())
        compare(LocalFileSelection.lastClosedSelectionId, cancelledId)
        compare(transfer.busy, false)
        compare(transfer.directorySelectionPending, false)
        compare(transfer.selectionRequestId, "")
        compare(finishCount, 1)
        compare(lastSuccess, false)
        compare(lastReason, "cancelled")

        // The picker can resolve after the user used Cancel. Its old request
        // ID must not start a transfer or emit a second completion.
        LocalFileSelection.inspectionCompleted(cancelledId, true,
                                               [{ name: "shot.json", size: 10 }], "")
        compare(transfer.busy, false)
        compare(finishCount, 1)
    }

    function test_cancelIsUnavailableAfterDirectorySelectionEnds() {
        verify(transfer.startDirectory("shotFiles", {}))
        const requestId = transfer.selectionRequestId
        LocalFileSelection.inspectionCompleted(requestId, true,
                                               [{ name: "shot.json", size: 10 }], "")
        compare(transfer.directorySelectionPending, false)
        verify(transfer.busy)
        verify(!transfer.cancelDirectorySelection())
    }

    function test_abortBeforeBeginReplyCancelsByRequestAndIgnoresLateReply() {
        verify(transfer.start(["file:///tmp/translations.json"], "languageJson", ({})))
        const selectionId = transfer.selectionRequestId
        LocalFileSelection.inspectionCompleted(selectionId, true,
                                               [{name: "translations.json", size: 3}], "")
        compare(ImportService.calls.length, 1)
        const begin = ImportService.calls[0]
        compare(begin.operation, "beginTransfer")
        compare(transfer.transferId, "")

        transfer.abort("cancelled")
        compare(ImportService.calls.length, 2)
        const cancel = ImportService.calls[1]
        compare(cancel.operation, "cancelTransfer")
        compare(cancel.args.requestId, begin.requestId)
        verify(!("transferId" in cancel.args))
        compare(LocalFileSelection.lastClosedSelectionId, selectionId)
        verify(!transfer.busy)
        compare(finishCount, 1)
        verify(!lastSuccess)
        compare(lastReason, "cancelled")

        ImportService.operationCompleted(begin.requestId, begin.operation, true,
                                         {transferId: "cancelled-transfer"}, "")
        ImportService.operationCompleted(cancel.requestId, cancel.operation, true, {}, "")
        compare(transfer.readerRequestId, "")
        compare(transfer.transferId, "")
        compare(finishCount, 1)
        compare(ImportService.calls.length, 2)

        // An old reply must also stay unrelated after another upload starts.
        verify(transfer.start(["file:///tmp/translations.json"], "languageJson", ({})))
        LocalFileSelection.inspectionCompleted(transfer.selectionRequestId, true,
                                               [{name: "translations.json", size: 3}], "")
        const replacement = ImportService.calls[2]
        compare(replacement.operation, "beginTransfer")
        verify(replacement.requestId !== begin.requestId)
        ImportService.operationCompleted(begin.requestId, begin.operation, true,
                                         {transferId: "cancelled-transfer"}, "")
        compare(transfer.operationRequestId, replacement.requestId)
        compare(transfer.readerRequestId, "")
        ImportService.operationCompleted(replacement.requestId, replacement.operation, true,
                                         {transferId: "replacement-transfer"}, "")
        verify(transfer.readerRequestId.length > 0)
        compare(transfer.transferId, "replacement-transfer")
        compare(finishCount, 1)
        transfer.abort("cancelled")
        compare(ImportService.calls[3].operation, "cancelTransfer")
        compare(ImportService.calls[3].args.transferId, "replacement-transfer")
        verify(!("requestId" in ImportService.calls[3].args))
    }

    function test_destructionBeforeBeginReplyCancelsByRequest() {
        const pending = createTemporaryObject(transientTransfer, testCase)
        verify(pending)
        verify(pending.start(["file:///tmp/translations.json"], "languageJson", ({})))
        const selectionId = pending.selectionRequestId
        LocalFileSelection.inspectionCompleted(selectionId, true,
                                               [{name: "translations.json", size: 3}], "")
        compare(ImportService.calls.length, 1)
        const begin = ImportService.calls[0]
        compare(begin.operation, "beginTransfer")
        pending.destroy()
        tryVerify(function() { return ImportService.calls.length === 2 })
        compare(ImportService.calls[1].operation, "cancelTransfer")
        compare(ImportService.calls[1].args.requestId, begin.requestId)
        verify(!("transferId" in ImportService.calls[1].args))
        compare(LocalFileSelection.lastClosedSelectionId, selectionId)
        ImportService.operationCompleted(begin.requestId, begin.operation, true,
                                         {transferId: "cancelled-transfer"}, "")
        compare(ImportService.calls.length, 2)
    }

    function test_byteArrayUploadChunkCompletesTransfer() {
        verify(transfer.start(["file:///tmp/translations.json"], "languageJson", ({})))
        const selectionId = transfer.selectionRequestId
        LocalFileSelection.inspectionCompleted(selectionId, true,
                                               [{name: "translations.json", size: 3}], "")
        compare(ImportService.calls.length, 1)
        compare(ImportService.calls[0].operation, "beginTransfer")

        const begin = ImportService.calls[0]
        ImportService.operationCompleted(begin.requestId, begin.operation, true,
                                         {transferId: "transfer-1"}, "")
        const readerId = transfer.readerRequestId
        verify(readerId.length > 0)
        LocalFileSelection.fileOpened(readerId, true, "translations.json", 3, "")

        // This C++ helper emits QByteArray from the typed chunkReady signal.
        // Qt 6.12 exposes that signal argument to QML as an ArrayBuffer.
        LocalFileSelection.emitByteArrayChunk(readerId)
        compare(ImportService.calls.length, 2)
        const append = ImportService.calls[1]
        compare(append.operation, "appendTransferChunk")
        compare(append.args.offset, 0)
        compare(append.args.data.byteLength, 3)
        compare(Array.from(new Uint8Array(append.args.data)).join(","), "0,65,255")

        ImportService.operationCompleted(append.requestId, append.operation, true,
                                         {nextOffset: 3, fileComplete: true}, "")
        compare(ImportService.calls.length, 3)
        const commit = ImportService.calls[2]
        compare(commit.operation, "commitTransfer")
        compare(commit.args.transferId, "transfer-1")
        ImportService.operationCompleted(commit.requestId, commit.operation, true,
                                         {transferId: "transfer-1"}, "")

        compare(finishCount, 1)
        verify(lastSuccess)
        compare(lastReason, "")
        compare(lastResult.transferId, "transfer-1")
        verify(!transfer.busy)
    }
}
