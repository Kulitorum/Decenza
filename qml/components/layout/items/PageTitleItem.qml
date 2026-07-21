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
        // that never depends on the customizable layout. Idle-only (onIdlePage);
        // a short tap does nothing so it can't be triggered by accident.
        //
        // Accessible.ignored on purpose: screen-reader users are not the ones
        // locked out — an off-screen Settings widget is unclipped and still in the
        // a11y tree, so TalkBack/VoiceOver can focus it by swipe navigation. This
        // gesture is a rescue for sighted touch users, who cannot see or reach an
        // off-screen button. Announcing it as a second interactive node over the
        // title would only duplicate the title readout for AT.
        AccessibleTapHandler {
            anchors.fill: parent
            enabled: root.onIdlePage
            visible: root.onIdlePage
            supportLongPress: true
            Accessible.ignored: true
            accessibleName: root.pageTitle
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
