import QtQuick
import QtQuick.Effects
import Decenza

LayoutWidgetItem {
    id: root

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function goToSettings() {
            AppShell.settingsRequested("")
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
            accessibleName: TranslationManager.translate("layout.settings.accessible.open", "Settings. Open application settings")
            supportDoubleClick: true
            onAccessibleClicked: root.goToSettings()
            // Tap already opens the page, so BOTH gestures are free to override.
            //
            // supportLongPress is what makes the handler below reachable AT ALL: with it
            // false the press timer never starts, so a long press ran no override and the
            // release opened the page instead -- the handler was declared and dead. It is
            // gated on a stored override, matching the compiled CustomItem twin
            // (CustomItem.qml): unset, the press falls through to the tap that opens this
            // page, and a stored "none" is consumed so the gesture is silent.
            supportLongPress: !!(root.modelData && root.modelData.longPressAction)
            onAccessibleLongPressed: LayoutActions.runGesture(root.modelData, "longPressAction", null)
            onAccessibleDoubleClicked: LayoutActions.runGesture(root.modelData, "doubleclickAction", null)
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
