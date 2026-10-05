import QtQuick
import QtQuick.Templates as T
import Decenza

Rectangle {
    id: focusIndicator

    // The Button / TabButton / RoundButton this ring belongs to: a Control, for visualFocus.
    property T.Control targetItem: parent as T.Control

    // Corner radius of the control this ring surrounds, so the ring follows its shape.
    // Every caller is a Button/TabButton/RoundButton, whose radius lives on its `background`
    // Rectangle and not on the control itself — the old `parent.radius` was therefore always
    // undefined and every ring drew at the 4px fallback, including around circular
    // RoundButtons. The ring is inflated by focusMargin on each side, so adding focusMargin
    // to the control's radius keeps a circle circular.
    property real targetRadius: 0

    anchors.fill: parent
    anchors.margins: -Theme.focusMargin

    // Keyboard focus only, as a browser's :focus-visible: Qt sets visualFocus when
    // focus arrived by Tab, Backtab or a shortcut (qquickcontrol.cpp:124-127,
    // 1433-1437), so a page's initial forceActiveFocus() draws no ring for
    // mouse and touch users. Key handlers that move focus pass Qt.TabFocusReason.
    // With a screen reader on, any focus shows.
    visible: focusIndicator.targetItem.visualFocus === true
             || (focusIndicator.targetItem.activeFocus && AccessibilityManager.enabled)
    color: "transparent"
    border.width: Theme.focusBorderWidth
    border.color: Theme.focusColor
    radius: focusIndicator.targetRadius > 0 ? focusIndicator.targetRadius + Theme.focusMargin : 4

    // Pulsing animation when focused
    SequentialAnimation on border.color {
        running: focusIndicator.visible
        loops: Animation.Infinite

        ColorAnimation {
            to: Qt.lighter(Theme.primaryColor, 1.3)
            duration: 500
        }
        ColorAnimation {
            to: Theme.primaryColor
            duration: 500
        }
    }
}
