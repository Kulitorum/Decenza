pragma ComponentBehavior: Bound

import QtQml
import Decenza

// Reads a small text file the user picked on this client (native or browser).
// Bytes decode as Latin-1, so only ASCII/Latin-1 content survives; anything
// needing UTF-8 should be uploaded to the host instead.
QtObject {
    id: reader
    property int maximumBytes: 64 * 1024
    property string requestId: ""
    property var parts: []
    readonly property bool busy: requestId.length > 0

    function read(url: url): bool {
        if (busy) return false
        parts = []
        requestId = LocalFileSelection.newRequestId()
        LocalFileSelection.openFile(requestId, url)
        return true
    }

    function fail(reason: string): void {
        LocalFileSelection.closeFile(requestId)
        requestId = ""
        parts = []
        reader.finished(false, "", reason)
    }

    readonly property Connections fileEvents: Connections {
        target: LocalFileSelection
        function onFileOpened(id, success, fileName, fileSize, reason) {
            if (id !== reader.requestId) return
            if (!success) reader.fail(reason)
            else if (fileSize > reader.maximumBytes) reader.fail("fileTooLarge")
            else LocalFileSelection.readNextChunk(id)
        }
        function onChunkReady(id, offset, data, lastChunk, reason) {
            if (id !== reader.requestId) return
            if (reason.length > 0) { reader.fail(reason); return }
            // QByteArray arrives as an ArrayBuffer (qtdeclarative/src/qml/jsruntime/qv4engine.cpp:1845-1846).
            const bytes = new Uint8Array(data)
            let text = ""
            for (let i = 0; i < bytes.length; ++i) text += String.fromCharCode(bytes[i])
            reader.parts.push(text)
            LocalFileSelection.acknowledgeChunk(id)
            if (!lastChunk) {
                LocalFileSelection.readNextChunk(id)
                return
            }
            const result = reader.parts.join("")
            LocalFileSelection.closeFile(id)
            reader.requestId = ""
            reader.parts = []
            reader.finished(true, result, "")
        }
    }

    Component.onDestruction: if (busy) LocalFileSelection.closeFile(requestId)

    signal finished(bool success, string text, string reason)
}
