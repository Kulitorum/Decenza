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
            color: quitCompactTap.isPressed ? Qt.darker("#555555", 1.2) : "#555555"
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
            }
            Tr {
                key: "idle.button.quit"
                fallback: "Quit"
                font: Theme.bodyFont
                color: "white"
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
            backgroundColor: "#555555"
            onClicked: Qt.quit()
        }
    }
}
