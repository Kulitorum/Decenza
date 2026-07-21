import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    // Zone style propagation — see LayoutItemDelegate. This widget did not take either
    // before: its icon and label were pinned to Theme.textColor and its fill to Theme's
    // chrome, so on a styled zone (or the background chooser's preview of a candidate
    // colour) it stayed on the applied theme while everything beside it followed the
    // zone — a white-on-navy capsule sitting in a light tan bar.
    property color zoneTextColor: Theme.textColor
    property color zoneFillOverride: "transparent"

    // Per-instance option (composable-status-bar): whether long-press quits the
    // app. Default true (current behaviour). When false, the widget sleeps on tap
    // only — a centred Sleep with no hidden exit. The explicit Quit widget remains.
    readonly property bool allowQuit: (modelData && modelData.allowQuit !== undefined) ? modelData.allowQuit : true

    // Per-instance option: show the sleep icon. Default true (current behaviour).
    readonly property bool showIcon: (modelData && modelData.showIcon !== undefined) ? modelData.showIcon : true

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
            iconSource: root.showIcon ? "qrc:/icons/sleep.svg" : ""
            backgroundColor: Theme.actionButtonFillOn(Theme.buttonDisabled, root.zoneFillOverride)
            onClicked: root.doSleep()
            onPressAndHold: if (root.allowQuit) Qt.quit()

            Accessible.description: root.allowQuit ? TranslationManager.translate("idle.accessible.sleep.hint", "Long-press to quit the app.") : ""
        }
    }
}
