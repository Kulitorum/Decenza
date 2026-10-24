pragma ComponentBehavior: Bound

import QtQuick

// Shared pointer/accessibility gesture policy for buttons and card-wide taps.
// Hosts provide the typed accessibility bridge; the visual control owns its
// role, labels and actions.
MouseArea {
    id: root

    required property SharedButtonInteraction interaction
    required property string accessibleName
    property string accessibleDescription: ""
    property Item accessibleItem: parent as Item
    property int accessibleRole: Accessible.Button
    property bool accessibleChecked: false
    property bool exposeAccessibleElement: true
    property bool supportLongPress: false
    property bool supportDoubleClick: false
    property int longPressInterval: 500
    property int doubleClickInterval: 300

    property bool _longPressTriggered: false
    property bool _isPressed: false
    property real _lastTapTime: 0
    readonly property bool isPressed: _isPressed

    signal accessibleClicked()
    signal accessibleDoubleClicked()
    signal accessibleLongPressed()

    cursorShape: Qt.PointingHandCursor
    Accessible.ignored: !root.exposeAccessibleElement
    Accessible.role: root.accessibleRole
    Accessible.name: root.accessibleName
    Accessible.description: root.accessibleDescription
    Accessible.checked: root.accessibleChecked
    Accessible.focusable: true
    Accessible.onPressAction: root.accessibleClicked()
    Accessible.onToggleAction: root.accessibleClicked()

    Timer {
        id: longPressTimer
        interval: root.longPressInterval
        onTriggered: {
            if (root.supportLongPress) {
                root._longPressTriggered = true
                root.accessibleLongPressed()
            }
        }
    }

    Timer {
        id: singleTapTimer
        interval: root.doubleClickInterval
        onTriggered: root.accessibleClicked()
    }

    Component.onDestruction: root.interaction.clearIf(root.accessibleItem)

    onPressed: function(mouse) {
        _longPressTriggered = false
        _isPressed = true
        if (supportLongPress)
            longPressTimer.start()
        mouse.accepted = true
    }

    onReleased: {
        longPressTimer.stop()
        _isPressed = false
    }

    onClicked: {
        if (_longPressTriggered)
            return

        const now = Date.now()
        const timeSinceLastTap = now - _lastTapTime
        const isDoubleTap = timeSinceLastTap < doubleClickInterval && timeSinceLastTap > 50
        _lastTapTime = now

        if (interaction.accessibilityEnabled) {
            // Match AccessibleTapHandler: screen-reader double-tap gestures
            // must stay the ordinary announce-then-activate interaction. A
            // fast second tap is not interpreted as the visual double-click
            // action while accessibility mode is enabled.
            if (interaction.lastAnnouncedItem === accessibleItem) {
                accessibleClicked()
            } else {
                interaction.remember(accessibleItem)
                interaction.announce(accessibleName)
            }
        } else if (isDoubleTap && supportDoubleClick) {
            singleTapTimer.stop()
            accessibleDoubleClicked()
        } else if (supportDoubleClick) {
            singleTapTimer.restart()
        } else {
            accessibleClicked()
        }
    }

    onCanceled: {
        longPressTimer.stop()
        singleTapTimer.stop()
        _isPressed = false
    }
}
