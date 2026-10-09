// `layer.effect` declares an inline component, so ids from this file are not statically
// resolvable inside it without this pragma. No delegate in this file takes model roles,
// so no `required property` is needed — see PresetPillRow.qml for the case that does.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

LayoutWidgetItem {
    id: root

    // Only ever rendered compact: in a centre zone LayoutItemDelegate compiles Sleep to a
    // CustomItem tile (LayoutActions.compiledTile) instead of loading this file.
    readonly property bool allowQuit: LayoutActions.sleepOption(modelData, "allowQuit")
    readonly property bool showIcon: LayoutActions.sleepOption(modelData, "showIcon")

    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function doSleep() {
        if (ScaleDevice && ScaleDevice.connected) {
            ScaleDevice.disableLcd()
        }
        DE1Device.goToSleep()
        AppShell.screensaverRequested()
    }

    Item {
        id: compactContent
        anchors.fill: parent
        implicitWidth: compactSleepRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSmall
            anchors.bottomMargin: Theme.spacingSmall
            color: {
                var base = Theme.actionButtonFillOn(Theme.buttonDisabled, root.zoneFillOverride)
                return sleepCompactTap.isPressed ? Qt.darker(base, 1.2) : base
            }
            radius: Theme.cardRadius
            opacity: 1.0
        }

        RowLayout {
            id: compactSleepRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            Image {
                visible: root.showIcon
                source: "qrc:/icons/sleep.svg"
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
                key: "idle.button.sleep"
                fallback: "Sleep"
                font: Theme.bodyFont
                color: root.zoneTextColor
                verticalAlignment: Text.AlignVCenter
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            id: sleepCompactTap
            anchors.fill: parent
            supportLongPress: root.allowQuit
            longPressInterval: 1000
            accessibleName: TranslationManager.translate("idle.accessible.sleep", "Sleep") + ". " + TranslationManager.translate("idle.accessible.sleep.description", "Put the machine to sleep")
            accessibleDescription: root.allowQuit ? TranslationManager.translate("idle.accessible.sleep.hint", "Long-press to quit the app.") : ""
            onAccessibleClicked: root.doSleep()
            onAccessibleLongPressed: if (root.allowQuit) Qt.quit()
        }
    }
}
