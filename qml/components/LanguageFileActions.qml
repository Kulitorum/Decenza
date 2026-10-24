import QtQml

// The page owns the platform picker and byte transfer components. This object
// retains only the user's captured language context and command correlation.
QtObject {
    id: actions
    required property var executeOperation
    required property var cancelTransfer
    property string phase: ""
    readonly property bool busy: phase !== ""
    property var capturedContext: ({})
    property string pendingRequest: ""
    property string pendingOperation: ""
    property string transferId: ""

    signal downloadRequested(var bundle)
    signal finished(bool success, var result, string reason)

    Component.onDestruction: {
        actions.cancelPendingRequest()
        actions.releaseTransfer()
    }

    function cancelPendingRequest() {
        const request = pendingRequest
        pendingRequest = ""
        pendingOperation = ""
        if (request !== "") executeOperation("cancelFileOperation", {requestId: request})
    }

    function capture(context) {
        if (busy || !context || typeof context.sourceId !== "string" || context.sourceId === ""
                || typeof context.language !== "string" || context.language === ""
                || context.revision === undefined || context.revision === null)
            return false
        capturedContext = {sourceId: context.sourceId, language: context.language, revision: context.revision}
        return true
    }

    function beginImport(context) {
        if (!capture(context)) return false
        phase = "picker"
        return true
    }

    function cancelPicker() {
        if (phase !== "picker") return false
        reset()
        return true
    }

    function acceptSelection() {
        if (phase !== "picker") return false
        phase = "transfer"
        return true
    }

    function transferCompleted(success, result, reason) {
        if (phase !== "transfer") return
        if (!success) { finish(false, result, reason); return }
        transferId = result.transferId || ""
        if (transferId === "") { finish(false, result, "invalidTransfer"); return }
        command("importTranslation", Object.assign({}, capturedContext, {transferId: transferId}))
    }

    function beginExport(context) {
        if (!capture(context)) return false
        command("exportTranslation", capturedContext)
        return true
    }

    function command(operation, args) {
        phase = "request"
        pendingOperation = operation
        pendingRequest = executeOperation(operation, args)
        if (pendingRequest === "") finish(false, ({}), "serviceUnavailable")
    }

    function completeOperation(requestId, operation, success, result, reason) {
        if (pendingRequest === "" || requestId !== pendingRequest || operation !== pendingOperation) return
        pendingRequest = ""
        pendingOperation = ""
        if (operation === "exportTranslation" && success) {
            phase = "download"
            downloadRequested(result)
        } else {
            finish(success, result, reason)
        }
    }

    function downloadCompleted(success, reason) {
        if (phase === "download") finish(success, ({}), reason)
    }

    function refuseDownload(bundle) {
        if (phase !== "download") return
        if (bundle && typeof bundle.bundleId === "string" && bundle.bundleId !== "")
            executeOperation("releaseBundle", {bundleId: bundle.bundleId})
        finish(false, ({}), "invalidBundle")
    }

    function releaseTransfer() {
        const id = transferId
        transferId = ""
        if (id !== "") cancelTransfer(id)
    }

    function reset() {
        releaseTransfer()
        phase = ""
        capturedContext = ({})
        pendingRequest = ""
        pendingOperation = ""
    }

    function finish(success, result, reason) {
        reset()
        finished(success, result || ({}), reason || "")
    }
}
