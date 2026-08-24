// `layer.effect` declares an inline component, so this file's ids are not statically
// resolvable inside it without this pragma. No delegate in this file takes an injected
// model role, so no `required property` is needed -- see ThemedIcon.qml for the same case.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

LayoutWidgetItem {
    id: root


    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function goToHistory() {
            AppShell.shotHistoryRequested({})
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
                source: "qrc:/icons/history.svg"
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
                key: "idle.button.history"
                fallback: "History"
                font: Theme.bodyFont
                color: root.zoneTextColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("idle.accessible.history.description", "View and compare past shots")
            supportDoubleClick: true
            onAccessibleClicked: root.goToHistory()
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
            translationKey: "idle.button.history"
            translationFallback: "History"
            iconSource: "qrc:/icons/history.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Theme.actionButtonFillOn(Theme.primaryColor, root.zoneFillOverride)
            onClicked: root.goToHistory()
        }
    }
}
