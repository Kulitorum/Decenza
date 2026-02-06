import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

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
            }
            Tr {
                key: "idle.button.autofavorites"
                fallback: "Favorites"
                font: Theme.bodyFont
                color: Theme.textColor
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
            backgroundColor: Theme.primaryColor
            onClicked: root.goToAutoFavorites()
        }
    }
}
