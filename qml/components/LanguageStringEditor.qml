import QtQml

QtObject {
    id: editor
    required property var executeOperation
    property var displayedContext: ({})
    property var draft: null
    property string pendingEdit: ""
    property string errorReason: ""
    signal refreshRequested()

    function acceptRefresh(context) {
        if (draft !== null) return false
        displayedContext = Object.assign({}, context)
        return true
    }

    function beginEdit(fallback, original) {
        if (pendingEdit !== "" || (draft && draft.fallback !== fallback)) return false
        if (!draft) {
            draft = { fallback: fallback, text: original, original: original,
                      context: Object.assign({}, displayedContext) }
        }
        return true
    }

    function setText(fallback, text) {
        if (draft && draft.fallback === fallback && pendingEdit === "") draft = Object.assign({}, draft, { text: text })
    }

    function mutate(operation, args, context) {
        return executeOperation(operation, Object.assign({}, args || {}, context || displayedContext))
    }

    function finishEdit(fallback, text) {
        if (!draft || draft.fallback !== fallback || pendingEdit !== "") return
        const current = draft
        const trimmed = text.trim()
        setText(fallback, trimmed)
        if (trimmed === current.original) {
            discard()
        } else {
            errorReason = ""
            pendingEdit = mutate("setGroupTranslation",
                { fallback: current.fallback, translation: trimmed }, current.context)
            if (pendingEdit === "") errorReason = "unavailable"
        }
    }

    function discard() {
        if (pendingEdit !== "") return false
        draft = null
        errorReason = ""
        refreshRequested()
        return true
    }

    function complete(requestId, operation, success, reason) {
        if (requestId !== pendingEdit || operation !== "setGroupTranslation") return
        pendingEdit = ""
        if (success) {
            draft = null
            errorReason = ""
            refreshRequested()
        } else {
            errorReason = reason
        }
    }
}
