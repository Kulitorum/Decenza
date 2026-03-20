import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function goToSettings() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/SettingsPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.bottomBarHeight
        implicitHeight: Theme.bottomBarHeight

        Image {
            anchors.centerIn: parent
            source: "qrc:/icons/settings.svg"
            sourceSize.width: Theme.scaled(32)
            sourceSize.height: Theme.scaled(32)
            Accessible.ignored: true

            layer.enabled: true
            layer.smooth: true
            layer.effect: MultiEffect {
                colorization: 1.0
                colorizationColor: Theme.textColor
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: "Settings. Open application settings"
            onAccessibleClicked: root.goToSettings()
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
            translationKey: "idle.button.settings"
            translationFallback: "Settings"
            iconSource: "qrc:/icons/settings.svg"
            enabled: true
            onClicked: root.goToSettings()
        }
    }
}
