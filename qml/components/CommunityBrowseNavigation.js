.pragma library

// Decide how a restored community page advances after one request completes.
// This behavior belongs with the shared page, not browser route serialization.
function restoreStep(restoring, requestMatched, success, completedPage, targetPage, total, pageSize) {
    if (!requestMatched) return "ignore"
    if (!restoring) return "normal"
    if (!success) return "cancel"
    var availablePages = Math.max(1, Math.ceil(Math.max(0, total) / Math.max(1, pageSize)))
    return completedPage < Math.min(targetPage, availablePages) ? "next" : "apply"
}
