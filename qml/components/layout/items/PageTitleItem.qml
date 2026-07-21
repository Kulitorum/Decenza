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
    // widget (issue #1586). Uses the existing Theme singleton (kept current by
    // main.qml's updateCurrentPageScale on every page change) rather than walking
    // the parent chain, which this widget cannot do — it is not a child of any page.
    readonly property bool onIdlePage: Theme.currentPageObjectName === "idlePage"

    function openSettings() {
        var win = Window.window
        if (win && win.goToSettings)
            win.goToSettings()
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.pageTitle
    Accessible.focusable: root.pageTitle.length > 0
    // Yield the tree while the long-press hatch below is live: that handler is a
    // focusable Button carrying this same title plus its long-press description,
    // so leaving this StaticText in place would announce the title twice.
    Accessible.ignored: root.isCompact && root.onIdlePage

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
        // (onIdlePage); an ordinary short tap does NOT open Settings, so it cannot
        // be triggered by accident. (With a screen reader active, activation DOES
        // open it — see onAccessibleClicked below, which is that user's only way
        // in, since they cannot long-press.)
        //
        // It lives in the compact (status-bar) rendering only. The pageTitle widget
        // is itself layout-editable, so a user who moved it to a center zone or
        // removed it has no hatch — this narrows #1586, it does not make Settings
        // unconditionally reachable.
        //
        // The handler stays in the accessibility tree and announces its long-press
        // (ACCESSIBILITY.md rules 1 and 2: an interactive element must be
        // discoverable, and a secondary action must be described). An earlier draft
        // marked it Accessible.ignored on the theory that a screen reader could
        // still reach an overflowed Settings widget — nothing in the chain sets
        // clip, so it stays laid out — but that is an OS-level assumption, and
        // hiding the one rescue path from the users least able to see the layout is
        // the wrong way to bet. The root Item yields its StaticText role while the
        // handler is live so the title is not announced twice.
        AccessibleTapHandler {
            anchors.fill: parent
            enabled: root.onIdlePage
            visible: root.onIdlePage
            supportLongPress: true
            accessibleName: root.pageTitle
            accessibleDescription: TranslationManager.translate(
                "pageTitle.accessible.longPressSettings",
                "Long-press to open Settings")
            // AccessibleTapHandler is a MouseArea and defaults to a pointing-hand
            // cursor. A plain click does nothing, so on desktop that would promise
            // an affordance that isn't there — keep the plain arrow.
            cursorShape: Qt.ArrowCursor
            onAccessibleLongPressed: root.openSettings()
            onAccessibleClicked: {
                // Only a screen-reader activation opens Settings here. In normal
                // mode this same signal fires on an ordinary short tap, and
                // navigating on that would make the title a trip hazard on every
                // idle screen — "a short tap does nothing" is what keeps the
                // gesture deliberate. AT users cannot long-press, so activation is
                // their equivalent path.
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                    root.openSettings()
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
