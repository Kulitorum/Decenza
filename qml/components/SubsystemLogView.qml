import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// A live, filtered view of one or more log subsystems, read from the system log.
//
// One component rather than a copy per view. The two on the connections page — DE1
// and scales — differ only in which markers they ask for, and each needs the same
// four behaviours: backfill the current session, follow new lines, stay bounded,
// and clear. Written twice, those four would be free to drift apart, and the DE1
// copy had already lost two of them (see maxLines and the Clear button below).
//
// Both halves of the filter come from C++ so the backfill and the live stream
// cannot disagree about what belongs here: sessionLinesMatching() for the history,
// lineMatches() for each new line. A marker/level test written in JavaScript here
// would be a second definition of "a scale line at INFO or above", with no access
// to the level ranking, drifting from the query that populated the view moments
// earlier — a line shown on arrival would vanish on reload, or the reverse.
Rectangle {
    id: root

    // Bracketed markers to show, e.g. ["[Scale]", "[Refractometer]"]. Substrings,
    // never patterns — a bracketed marker read as a regex is a character class
    // that matches almost every line.
    required property var markers

    // Minimum level. INFO is the tier that means "the user-facing narrative"; see
    // the severity/audience note in src/core/logtags.h. Empty means every level.
    property string minLevel: "INFO"

    // Hard cap on displayed lines. The DE1 view had NO cap: it was
    // `de1LogText.text += message` growing for the whole process lifetime, which
    // is why the per-frame serial RX line (~600 a shot) mattered so much. A view is
    // a window on the log, not a second copy of it — the full history is on disk
    // and reachable through Share.
    property int maxLines: 400

    // Trimming rebuilds the whole TextArea, so it happens in one chunk rather than
    // on every line past the cap. 25% keeps the rebuild rare without throwing away
    // so much that the view jumps.
    readonly property int trimChunk: Math.max(1, Math.floor(maxLines * 0.25))

    property bool showClear: true
    property bool showShare: false

    // Accessible name for the log area itself.
    property string accessibleName: ""

    signal shareRequested()

    // The displayed lines. Held as an array because trimming needs to drop from
    // the front, which a plain `text +=` cannot do.
    property var _lines: []

    color: Qt.darker(Theme.surfaceColor, 1.2)
    radius: Theme.scaled(4)

    function _render() {
        logText.text = root._lines.join("\n")
        // Follow the tail. Assigning position directly rather than animating: this
        // runs on every appended line.
        logScroll.ScrollBar.vertical.position =
            1.0 - logScroll.ScrollBar.vertical.size
    }

    function _append(line) {
        var next = root._lines
        next.push(line)
        if (next.length > root.maxLines)
            next = next.slice(root.trimChunk)
        root._lines = next
        root._render()
    }

    // Discards what is DISPLAYED and nothing else: the system log is untouched, the
    // other view keeps its own contents, and new lines continue to arrive here.
    // Previously the scale view's Clear also called BLEManager.clearScaleLog(),
    // which wiped the shared buffer that fed the share file — so clearing the
    // screen silently destroyed the log a user was about to send.
    function clearView() {
        root._lines = []
        root._render()
    }

    // Guard the MEMBER, not the name. A registered singleton with no instance is
    // TRUTHY in QML (qtdeclarative qv4qmlcontext.cpp:229), so `if (WebDebugLogger)`
    // passes and the first member read is what throws. install() runs from main()
    // before the engine exists, so in practice this is always present; the guard is
    // for the case where it is not, which must degrade to an empty view rather than
    // a broken page.
    readonly property bool _loggerReady:
        WebDebugLogger.sessionLinesMatching !== undefined

    Component.onCompleted: {
        if (!root._loggerReady)
            return
        // Backfill the session so opening the page after activity shows what
        // happened, instead of only what happens next.
        root._lines = WebDebugLogger.sessionLinesMatching(root.markers, root.minLevel)
        if (root._lines.length > root.maxLines)
            root._lines = root._lines.slice(root._lines.length - root.maxLines)
        root._render()
    }

    Connections {
        target: root._loggerReady ? WebDebugLogger : null

        // MUST NOT LOG. Anything logged here re-enters the global message handler
        // from inside its own emit. WebDebugLogger's per-thread guard stops the
        // recursion, but its cost is that the line logged from HERE is never
        // emitted: it reaches the ring buffer and the file, but no lineAppended
        // observer, so this view and a reload of the same session disagree about
        // it. Only that one line is affected — the guard clears when the outer
        // emit returns, so later lines are fine.
        //
        // A bare `console.log` carries no registered marker, so lineMatches()
        // would filter it anyway; the real hazard is calling into C++ that logs
        // through a helper, because that line IS one this view would have shown.
        function onLineAppended(type, line) {
            // `type` is intentionally unused: the line text carries its own level
            // tag and lineMatches() reads it from there, so QML never needs to know
            // about QtMsgType.
            if (WebDebugLogger.lineMatches(line, root.markers, root.minLevel))
                root._append(line)
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
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(11)
                font.family: Theme.monoFontFamily
                wrapMode: Text.Wrap
                background: null
                text: ""

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
