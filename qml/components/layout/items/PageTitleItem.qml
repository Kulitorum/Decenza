import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    // Access currentPageTitle from the ApplicationWindow (main.qml)
    property string pageTitle: {
        var win = Window.window
        return win ? (win.currentPageTitle || "") : ""
    }

    // True only when the idle/home page is on top. The long-press-to-Settings
    // rescue gesture below is gated on this: the status bar (and this title) is a
    // global overlay shown on every page, but the gesture must only fire on Idle,
    // where a user whose bottom bar overflowed can no longer reach the Settings
    // widget (issue #1586). Reads the window's currentPageObjectName (this widget
    // is not a child of any page, so it can't walk the parent chain to find it).
    readonly property bool onIdlePage: {
        var win = Window.window
        return win ? win.currentPageObjectName === "idlePage" : false
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.pageTitle
    Accessible.focusable: root.pageTitle.length > 0

    // --- COMPACT MODE (status bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: compactRow.implicitHeight

        Row {
            id: compactRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            Text {
                text: root.pageTitle
                color: DE1Device.simulationMode ? Theme.simulationIndicatorColor : Theme.textColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            Text {
                text: TranslationManager.translate("pageTitle.subStateSeparator", "- ") + DE1Device.subStateString
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                visible: MachineState.isFlowing
                Accessible.ignored: true
            }
        }

        // Escape hatch: long-press the Idle page title to open Settings. A user
        // whose bottom bar overflowed can lose the Settings widget off the screen
        // edge (issue #1586) with no other route into Settings — this gives one
        // that does not depend on where the Settings widget ended up. Idle-only
        // (onIdlePage); a short tap does NOT open Settings, so it cannot be
        // triggered by accident.
        //
        // Two limits worth knowing before relying on this as THE rescue:
        //  - It lives in the compact (status-bar) rendering only. The pageTitle
        //    widget is itself layout-editable, so a user who moved it to a center
        //    zone or removed it has no hatch — this narrows #1586, it does not
        //    make Settings unconditionally reachable.
        //  - Accessible.ignored keeps it out of the a11y TREE, but not out of the
        //    touch path: with a screen reader active a short tap still runs
        //    AccessibleTapHandler's announce(). It is ignored because the root
        //    Item already exposes the title as StaticText, and a second node over
        //    the same text would just duplicate that readout.
        // The reason no AT-facing action is offered here: nothing in the overflow
        // chain sets clip (see LayoutBarZone), so an off-screen Settings widget is
        // expected to remain in the a11y tree and focusable by swipe navigation —
        // i.e. screen-reader users are expected not to be the ones locked out.
        // That last step is an OS-level assumption, not something verified here;
        // if TalkBack turns out not to focus out-of-bounds nodes, this handler
        // should gain a real accessible action rather than stay ignored.
        AccessibleTapHandler {
            anchors.fill: parent
            enabled: root.onIdlePage
            visible: root.onIdlePage
            supportLongPress: true
            Accessible.ignored: true
            accessibleName: root.pageTitle
            // AccessibleTapHandler is a MouseArea and defaults to a pointing-hand
            // cursor. A click here does nothing, so on desktop that would promise
            // an affordance that isn't there — keep the plain arrow.
            cursorShape: Qt.ArrowCursor
            onAccessibleLongPressed: {
                var win = Window.window
                if (win && win.goToSettings)
                    win.goToSettings()
            }
        }
    }

    // --- FULL MODE (center rendering) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: root.width
                text: root.pageTitle
                color: DE1Device.simulationMode ? Theme.simulationIndicatorColor : Theme.textColor
                font: Theme.valueFont
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("pageTitle.pageTitle", "Page Title")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }
    }
}
