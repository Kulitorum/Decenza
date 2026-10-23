import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// A bounded, client-filtered view of the host's one raw log stream.
Rectangle {
    id: root
    required property var markers
    property string minLevel: "INFO"
    property int maxLines: 400
    readonly property int trimChunk: Math.max(1, Math.floor(maxLines * 0.25))
    property bool showClear: true
    property bool showShare: false
    property string accessibleName: ""
    signal shareRequested()

    property var _lines: []
    property var _pendingLines: []
    property bool _filled: false
    property real _shownThrough: 0
    property string _snapshotRequestId: ""
    property string _boundaryRequestId: ""
    property bool _clearing: false
    readonly property int _maxPendingLines: 1000

    color: Qt.darker(Theme.surfaceColor, 1.2)
    radius: Theme.scaled(4)

    function _render() {
        logText.text = root._lines.join("\n")
        root._followTail()
    }

    function _followTail() {
        logScroll.ScrollBar.vertical.position =
                1.0 - logScroll.ScrollBar.vertical.size
    }

    function _append(line) {
        var next = root._lines
        next.push(line)
        if (next.length > root.maxLines) {
            root._lines = next.slice(root.trimChunk)
            root._render()
            return
        }
        root._lines = next
        logText.append(line)
        root._followTail()
    }

    function _consumeLine(line, sequence) {
        if (!root.visible || root._clearing || sequence <= root._shownThrough)
            return
        root._shownThrough = sequence
        if (SystemLogService.lineMatches(line, root.markers, root.minLevel))
            root._append(line)
    }

    function _fill() {
        if (!root.visible || root._snapshotRequestId.length > 0
                || root._filled || !SystemLogService.ready)
            return
        root._filled = false
        root._pendingLines = []
        root._lines = []
        root._render()
        root._snapshotRequestId = SystemLogService.requestSnapshot(
                    root.markers, root.minLevel, root.maxLines)
    }

    function _hide() {
        root._snapshotRequestId = ""
        root._boundaryRequestId = ""
        root._filled = false
        root._clearing = false
        root._pendingLines = []
        root._lines = []
        root._shownThrough = 0
        root._render()
    }

    function clearView() {
        // Cancel the snapshot correlation before asking for the boundary. A late
        // snapshot must never repopulate this client-only clear or move the floor
        // backwards.
        root._snapshotRequestId = ""
        root._filled = true
        root._lines = []
        root._render()
        root._pendingLines = []
        root._clearing = true
        root._boundaryRequestId = SystemLogService.requestBoundary()
    }

    Component.onCompleted: {
        SystemLogService.setViewActive(root, root.visible)
        root._fill()
    }
    Component.onDestruction: SystemLogService.setViewActive(root, false)
    onVisibleChanged: {
        if (visible) {
            SystemLogService.setViewActive(root, true)
            root._fill()
        } else {
            root._hide()
            SystemLogService.setViewActive(root, false)
        }
    }

    Connections {
        target: SystemLogService

        function onSnapshotCompleted(requestId, success, lines, sequence, reason) {
            if (requestId !== root._snapshotRequestId)
                return
            root._snapshotRequestId = ""
            root._filled = true
            if (!success) {
                root._pendingLines = []
                root._lines = [reason]
                root._render()
                return
            }
            root._shownThrough = sequence
            if (!root._clearing)
                root._lines = lines
            var pending = root._pendingLines
            root._pendingLines = []
            for (var i = 0; i < pending.length; ++i)
                root._consumeLine(pending[i].line, pending[i].sequence)
            root._render()
        }

        function onLineAppended(line, sequence) {
            if (!root.visible)
                return
            // Lines received while the boundary request is in flight are held
            // until the returned sequence tells us which side of the clear they
            // belong on.
            if (root._clearing || !root._filled) {
                var pending = root._pendingLines
                pending.push({ line: line, sequence: sequence })
                if (pending.length > root._maxPendingLines)
                    pending.shift()
                root._pendingLines = pending
                return
            }
            root._consumeLine(line, sequence)
        }

        function onBoundaryCompleted(requestId, success, sequence, reason) {
            if (requestId !== root._boundaryRequestId)
                return
            root._boundaryRequestId = ""
            root._clearing = false
            var pending = root._pendingLines
            root._pendingLines = []
            if (success) {
                root._shownThrough = sequence
                root._lines = []
                for (var i = 0; i < pending.length; ++i)
                    root._consumeLine(pending[i].line, pending[i].sequence)
            } else {
                for (var j = 0; j < pending.length; ++j)
                    root._shownThrough = Math.max(root._shownThrough,
                                                  pending[j].sequence)
                if (reason.length > 0)
                    root._lines = [reason]
            }
            root._render()
        }

        function onStateChanged() {
            if (!SystemLogService.ready) {
                // A dropped transport may never return an in-flight RPC result,
                // and lines emitted during the outage cannot be replayed by the
                // live stream. Invalidate IDs and refill from a bounded snapshot
                // after reconnect; late completions are ignored by their IDs.
                if (root.visible) {
                    root._snapshotRequestId = ""
                    root._boundaryRequestId = ""
                    root._clearing = false
                    root._filled = false
                    root._pendingLines = []
                    root._lines = [TranslationManager.translate(
                        "settings.connections.hostLogDisconnected",
                        "Host log disconnected.")]
                    root._render()
                }
                return
            }
            if (root.visible)
                root._fill()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(8)
        spacing: Theme.scaled(4)

        ScrollView {
            id: logScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: logText
                readOnly: true
                textFormat: TextEdit.PlainText
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(11)
                font.family: Theme.monoFontFamily
                wrapMode: Text.Wrap
                background: null
                Accessible.role: Accessible.EditableText
                Accessible.name: root.accessibleName
                Accessible.description: Theme.capAccessibleText(text)
                Accessible.focusable: true
                activeFocusOnTab: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)
            visible: root.showClear || root.showShare
            Item { Layout.fillWidth: true }
            AccessibleButton {
                visible: root.showClear
                text: TranslationManager.translate("settings.bluetooth.clearLog", "Clear")
                accessibleName: TranslationManager.translate(
                    "settings.connections.clearLogView", "Clear this log view")
                onClicked: root.clearView()
            }
            AccessibleButton {
                visible: root.showShare
                text: TranslationManager.translate("settings.bluetooth.shareLog", "Share Log")
                accessibleName: TranslationManager.translate(
                    "settings.connections.shareDebugLog", "Share the debug log")
                onClicked: root.shareRequested()
            }
        }
    }
}
