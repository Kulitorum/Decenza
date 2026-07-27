import QtQuick
import Decenza

// Reusable MouseArea with accessibility support:
// - Normal mode: single tap activates, double-tap for special action
// - Accessibility mode: first tap announces, tap-again activates, quick double-tap for special action
// Also supports long-press detection

MouseArea {
    id: root

    // REQUIRED: The text to announce when tapped in accessibility mode
    required property string accessibleName

    // The item to track for "tap again to activate" (defaults to parent)
    property var accessibleItem: parent

    // Optional: override accessible role (default Button, set to Accessible.CheckBox for toggles)
    property int accessibleRole: Accessible.Button

    // Optional: checked state for CheckBox role
    property bool accessibleChecked: false

    // Accessibility: Make this a proper interactive element for screen readers
    Accessible.role: root.accessibleRole
    Accessible.name: root.accessibleName
    Accessible.checked: root.accessibleChecked
    Accessible.focusable: true

    // Handle TalkBack/VoiceOver activation via accessibility press action
    // This is the proper way to receive screen reader activations (double-tap in TalkBack)
    Accessible.onPressAction: {
        // Screen reader activation = primary action
        root.accessibleClicked()
    }

    // Clear lastAnnouncedItem when this item is destroyed to prevent dangling pointer crash
    Component.onDestruction: {
        if (typeof AccessibilityManager !== "undefined" &&
            AccessibilityManager.lastAnnouncedItem === accessibleItem) {
            AccessibilityManager.lastAnnouncedItem = null
        }
    }

    // Whether long-press is supported
    property bool supportLongPress: false

    // Whether double-tap is supported
    property bool supportDoubleClick: false

    // Long-press interval in ms
    property int longPressInterval: 500

    // Double-tap detection interval in ms
    property int doubleClickInterval: 300

    // Signals
    signal accessibleClicked()        // Tap activated (normal mode or 2nd tap in a11y mode)
    signal accessibleDoubleClicked()  // Quick double-tap detected
    signal accessibleLongPressed()    // Long press detected

    // Internal state
    property bool _longPressTriggered: false
    property bool _isPressed: false
    property real _lastTapTime: 0

    // Expose pressed state for visual feedback
    readonly property bool isPressed: _isPressed

    cursorShape: Qt.PointingHandCursor

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

    // Timer for delayed single-tap action in normal mode (to wait for potential double-tap)
    Timer {
        id: singleTapTimer
        interval: root.doubleClickInterval
        onTriggered: {
            // Single tap confirmed - activate
            root.accessibleClicked()
        }
    }

    onPressed: {
        _longPressTriggered = false
        _isPressed = true
        if (supportLongPress) {
            longPressTimer.start()
        }
    }

    onReleased: {
        longPressTimer.stop()
        _isPressed = false

        if (_longPressTriggered) {
            // Long press already handled
            return
        }

        var now = Date.now()
        var timeSinceLastTap = now - _lastTapTime
        var isDoubleTap = timeSinceLastTap < doubleClickInterval && timeSinceLastTap > 50  // > 50ms to avoid bounce
        _lastTapTime = now

        var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

        if (accessibilityMode) {
            // Accessibility mode: First tap announces, second tap (same item) activates
            // This allows blind users to explore UI without accidentally triggering actions
            if (isDoubleTap && supportDoubleClick) {
                // Quick double-tap = special action (edit, etc.)
                singleTapTimer.stop()
                accessibleDoubleClicked()
            } else if (AccessibilityManager.lastAnnouncedItem === accessibleItem) {
                // Second tap on same item = activate
                accessibleClicked()
            } else {
                // First tap = announce only, don't activate
                AccessibilityManager.lastAnnouncedItem = accessibleItem
                AccessibilityManager.announce(root.accessibleName)
            }
        } else {
            // Normal mode
            if (isDoubleTap && supportDoubleClick) {
                // Double-tap = special action
                singleTapTimer.stop()
                accessibleDoubleClicked()
            } else if (supportDoubleClick) {
                // Wait to see if double-tap is coming
                singleTapTimer.restart()
            } else {
                // No double-tap support, activate immediately
                accessibleClicked()
            }
        }
    }

    onCanceled: {
        longPressTimer.stop()
        singleTapTimer.stop()
        _isPressed = false
    }
}
