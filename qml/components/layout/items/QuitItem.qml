import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    // Zone style propagation — see LayoutItemDelegate. As with SleepItem this widget
    // took neither, so on a styled zone (or the background chooser's preview of a
    // candidate colour) it stayed on the applied theme while its neighbours followed
    // the zone.
    property color zoneTextColor: Theme.textColor
    property color zoneFillOverride: "transparent"

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactQuitRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSmall
            anchors.bottomMargin: Theme.spacingSmall
            color: {
                var base = Theme.actionButtonFillOn("#555555", root.zoneFillOverride)
                return quitCompactTap.isPressed ? Qt.darker(base, 1.2) : base
            }
            radius: Theme.cardRadius
        }

        RowLayout {
            id: compactQuitRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            Image {
                source: "qrc:/icons/quit.svg"
                sourceSize.width: Theme.scaled(28)
                sourceSize.height: Theme.scaled(28)
                Layout.alignment: Qt.AlignVCenter
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.zoneTextColor
                }
            }
            Tr {
                key: "idle.button.quit"
                fallback: "Quit"
                font: Theme.bodyFont
                // Was Theme.primaryContrastColor while the icon beside it used
                // Theme.textColor — the two halves of one label on two different
                // colours. Both follow the zone now.
                color: root.zoneTextColor
                verticalAlignment: Text.AlignVCenter
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            id: quitCompactTap
            anchors.fill: parent
            accessibleName: TranslationManager.translate("idle.accessible.quit", "Quit") + ". " + TranslationManager.translate("idle.accessible.quit.description", "Quit the application")
            onAccessibleClicked: Qt.quit()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(150)
        implicitHeight: Theme.scaled(120)

        ActionButton {
            anchors.fill: parent
            translationKey: "idle.button.quit"
            translationFallback: "Quit"
            iconSource: "qrc:/icons/quit.svg"
            backgroundColor: Theme.actionButtonFillOn("#555555", root.zoneFillOverride)
            onClicked: Qt.quit()
        }
    }
}
