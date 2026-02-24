import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function doSleep() {
        if (ScaleDevice && ScaleDevice.connected) {
            ScaleDevice.disableLcd()
        }
        DE1Device.goToSleep()
        root.goToScreensaver()
    }

    function goToScreensaver() {
        var win = Window.window
        if (win && typeof win.goToScreensaver === "function") {
            win.goToScreensaver()
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactSleepRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSmall
            anchors.bottomMargin: Theme.spacingSmall
            color: sleepCompactTap.isPressed ? Qt.darker("#555555", 1.2) : "#555555"
            radius: Theme.cardRadius
            opacity: 1.0
        }

        RowLayout {
            id: compactSleepRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            Image {
                source: "qrc:/icons/sleep.svg"
                sourceSize.width: Theme.scaled(28)
                sourceSize.height: Theme.scaled(28)
                Layout.alignment: Qt.AlignVCenter
                Accessible.ignored: true
            }
            Tr {
                key: "idle.button.sleep"
                fallback: "Sleep"
                font: Theme.bodyFont
                color: "white"
                verticalAlignment: Text.AlignVCenter
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            id: sleepCompactTap
            anchors.fill: parent
            supportLongPress: true
            longPressInterval: 1000
            accessibleName: TranslationManager.translate("idle.accessible.sleep", "Sleep") + ". " + TranslationManager.translate("idle.accessible.sleep.description", "Put the machine to sleep")
            onAccessibleClicked: root.doSleep()
            onAccessibleLongPressed: Qt.quit()
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
            translationKey: "idle.button.sleep"
            translationFallback: "Sleep"
            iconSource: "qrc:/icons/sleep.svg"
            backgroundColor: "#555555"
            onClicked: root.doSleep()
            onPressAndHold: Qt.quit()
        }
    }
}
