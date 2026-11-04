pragma ComponentBehavior: Bound

import QtQml
import Decenza

// Reads a small ASCII text file the user picked (a PEM certificate, say) on
// this client, native or browser. Bytes are decoded as Latin-1; callers that
// need UTF-8 should upload the file to the host instead.
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
            // QByteArray arrives as an ArrayBuffer (Qt 6.12 qv4engine.cpp:1845-1846).
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

    signal finished(bool success, string text, string reason)
}
