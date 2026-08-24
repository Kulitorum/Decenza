// `layer.effect` declares an inline component, so this file's ids are not statically
// resolvable inside it without this pragma -- the gear now colorizes with the zone's
// contrast colour, which is a read of `root` from inside that component. No delegate in
// this file takes an injected model role, so no `required property` is needed; the two
// structural twins (HistoryItem, AutoFavoritesItem) carry the same header.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import Decenza

LayoutWidgetItem {
    id: root

    // Compact only: in a centre zone this type is compiled to CustomItem
    // (LayoutItemDelegate.isCompiled), so this file is never loaded with
    // isCompact false. The full-mode branch it used to carry rendered nowhere.
    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function goToSettings() {
            AppShell.settingsRequested("")
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
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
                colorizationColor: root.zoneTextColor
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("layout.settings.accessible.open", "Settings. Open application settings")
            accessibleDescription: LayoutActions.gestureHint(root.modelData)
            supportDoubleClick: true
            onAccessibleClicked: root.goToSettings()
            // Tap already opens the page, so BOTH gestures are free to override.
            //
            // Gated, not `true`: this widget reserves no destination
            // (gestureReservedDestination(), settings_network.cpp), so the release tap is
            // the ONLY route to its page -- and a long press that starts the timer
            // swallows that tap whether or not it dispatches anything. Unset therefore
            // has to leave the timer alone. See AccessibleTapHandler.supportLongPress.
            supportLongPress: !!(root.modelData && root.modelData.longPressAction)
            onAccessibleLongPressed: LayoutActions.runGesture(root.modelData, "longPressAction", null)
            onAccessibleDoubleClicked: LayoutActions.runGesture(root.modelData, "doubleclickAction", null)
        }
    }

}
