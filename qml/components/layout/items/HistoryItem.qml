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

    function goToHistory() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/ShotHistoryPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactHistoryRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactHistoryRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/espresso.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
            }
            Tr {
                key: "idle.button.history"
                fallback: "History"
                font: Theme.bodyFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("idle.accessible.history.description", "View and compare past shots")
            onAccessibleClicked: root.goToHistory()
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
            translationKey: "idle.button.history"
            translationFallback: "History"
            iconSource: "qrc:/icons/espresso.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Theme.primaryColor
            onClicked: root.goToHistory()
        }
    }
}
