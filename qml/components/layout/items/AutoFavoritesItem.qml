import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    // Zone style propagation — see LayoutItemDelegate. Added with SleepItem/QuitItem:
    // these widgets took neither, so on a styled zone (or the background chooser's
    // preview of a candidate colour) they stayed on the applied theme's text and
    // chrome while their neighbours followed the zone.
    property color zoneTextColor: Theme.textColor
    property color zoneFillOverride: "transparent"

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function goToAutoFavorites() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/AutoFavoritesPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactFavRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactFavRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/star.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.zoneTextColor
                }
            }
            Tr {
                key: "idle.button.autofavorites"
                fallback: "Favorites"
                font: Theme.bodyFont
                color: root.zoneTextColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("idle.accessible.autofavorites.description", "Open auto-favorites list of recent bean and profile combinations")
            onAccessibleClicked: root.goToAutoFavorites()
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
            translationKey: "idle.button.autofavorites"
            translationFallback: "Favorites"
            iconSource: "qrc:/icons/star.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Theme.actionButtonFillOn(Theme.primaryColor, root.zoneFillOverride)
            onClicked: root.goToAutoFavorites()
        }
    }
}
