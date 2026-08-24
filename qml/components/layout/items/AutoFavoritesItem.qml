// `layer.effect` declares an inline component, so this file's ids are not statically
// resolvable inside it without this pragma. No delegate in this file takes an injected
// model role, so no `required property` is needed -- see ThemedIcon.qml for the same case.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza

LayoutWidgetItem {
    id: root


    // Compact only: in a centre zone this type is compiled to CustomItem
    // (LayoutItemDelegate.isCompiled), so this file is never loaded with
    // isCompact false. The full-mode branch it used to carry rendered nowhere.
    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function goToAutoFavorites() {
            AppShell.autoFavoritesRequested()
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
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
            accessibleDescription: LayoutActions.gestureHint(root.modelData)
            supportDoubleClick: true
            onAccessibleClicked: root.goToAutoFavorites()
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
