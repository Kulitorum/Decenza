import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Button {
    id: control

    property string iconSource: ""
    property color backgroundColor: Theme.primaryColor
    property int iconSize: Theme.scaled(48)

    // Translation support
    property string translationKey: ""
    property string translationFallback: ""

    // Accessibility description translation (optional override)
    property string accessibleDescriptionKey: "actionbutton.description.default"
    property string accessibleDescriptionFallback: "Tap to select preset. Long-press for more options."

    // Set true when onDoubleClicked is connected — gates the 250ms single-tap delay
    property bool supportDoubleClick: false

    // Auto-compute text from translation if translationKey is set (reactive to translation changes)
    text: translationKey !== "" ? _computedText : ""
    readonly property string _computedText: {
        var _ = TranslationManager.translationVersion  // Trigger re-evaluation
        return TranslationManager.translate(translationKey, translationFallback)
    }

    readonly property string _computedAccessibleDescription: {
        var _ = TranslationManager.translationVersion
        return TranslationManager.translate(accessibleDescriptionKey, accessibleDescriptionFallback)
    }

    // Track pressed state for visual feedback
    property bool _isPressed: false

    // Enable keyboard focus
    focusPolicy: Qt.StrongFocus
    activeFocusOnTab: true

    // Accessibility on the Button itself (not delegated to a child)
    Accessible.role: Accessible.Button
    Accessible.name: control.text + ". " + control._computedAccessibleDescription
    Accessible.focusable: true
    Accessible.onPressAction: {
        if (control.enabled) {
            control.clicked()
        }
    }

    implicitWidth: Theme.scaled(150)
    implicitHeight: Theme.scaled(120)

    contentItem: Column {
        spacing: Theme.scaled(10)
        anchors.centerIn: parent
        anchors.verticalCenterOffset: Theme.scaled(4)

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: iconImage.paintedWidth
            height: control.iconSize
            visible: control.iconSource !== ""

            Image {
                id: iconImage
                anchors.centerIn: parent
                source: control.iconSource
                height: control.iconSize
                sourceSize.height: control.iconSize * 2
                fillMode: Image.PreserveAspectFit
                opacity: control.enabled ? 1.0 : 0.5
                // Decorative - accessibility handled by Button itself
                Accessible.ignored: true
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.text
            color: control.enabled ? Theme.textColor : Theme.textSecondaryColor
            font: Theme.bodyFont
            // Decorative - accessibility handled by Button itself
            Accessible.ignored: true
        }
    }

    background: Rectangle {
        radius: Theme.buttonRadius
        color: {
            if (!control.enabled) return Theme.buttonDisabled
            if (control._isPressed) return Qt.darker(control.backgroundColor, 1.2)
            if (control.hovered || control.activeFocus) return Qt.lighter(control.backgroundColor, 1.1)
            return control.backgroundColor
        }

        // Focus indicator
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.focusMargin
            visible: control.activeFocus
            color: "transparent"
            border.width: Theme.focusBorderWidth
            border.color: Theme.focusColor
            radius: parent.radius + Theme.focusMargin
        }

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }

    // Clear lastAnnouncedItem when destroyed to prevent dangling pointer crash
    Component.onDestruction: {
        if (typeof AccessibilityManager !== "undefined" &&
            AccessibilityManager.lastAnnouncedItem === control) {
            AccessibilityManager.lastAnnouncedItem = null
        }
    }

    // Plain MouseArea for touch interaction (no Accessible.* properties — Button handles that)
    MouseArea {
        id: touchArea
        anchors.fill: parent
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor

        // Internal state
        property bool _longPressTriggered: false
        property real _lastTapTime: 0

        Timer {
            id: longPressTimer
            interval: 500
            onTriggered: {
                touchArea._longPressTriggered = true
                var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
                if (accessibilityMode) {
                    // In accessibility mode, long-press triggers the secondary action (same as double-tap in normal mode)
                    control.doubleClicked()
                } else {
                    control.pressAndHold()
                }
            }
        }

        // Timer for delayed single-tap action in normal mode (to wait for potential double-tap)
        Timer {
            id: singleTapTimer
            interval: 250
            onTriggered: {
                control.clicked()
            }
        }

        onPressed: function(mouse) {
            touchArea._longPressTriggered = false
            control._isPressed = true
            longPressTimer.start()
            mouse.accepted = true
        }

        onReleased: function(mouse) {
            longPressTimer.stop()
            control._isPressed = false

            if (touchArea._longPressTriggered) {
                return
            }

            // Only activate if released within bounds (matches AccessibleButton's onClicked behavior)
            if (mouse.x < 0 || mouse.x > touchArea.width ||
                mouse.y < 0 || mouse.y > touchArea.height) {
                return
            }

            var now = Date.now()
            var timeSinceLastTap = now - touchArea._lastTapTime
            var isDoubleTap = timeSinceLastTap < 250 && timeSinceLastTap > 50
            touchArea._lastTapTime = now

            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

            if (accessibilityMode) {
                // Accessibility mode: First tap announces, second tap (same item) activates
                // Quick double-tap detection is DISABLED in accessibility mode because
                // TalkBack's "double-tap to activate" gesture can be misdetected.
                if (AccessibilityManager.lastAnnouncedItem === control) {
                    control.clicked()
                } else {
                    AccessibilityManager.lastAnnouncedItem = control
                    AccessibilityManager.announce(control.text + ". " + control._computedAccessibleDescription)
                }
            } else if (control.supportDoubleClick) {
                // Normal mode with double-click support: wait 250ms for potential double-tap
                if (isDoubleTap) {
                    singleTapTimer.stop()
                    control.doubleClicked()
                } else {
                    singleTapTimer.restart()
                }
            } else {
                // Normal mode without double-click: activate immediately
                control.clicked()
            }
        }

        onCanceled: {
            longPressTimer.stop()
            singleTapTimer.stop()
            control._isPressed = false
        }
    }

    // Keyboard handling
    Keys.onReturnPressed: {
        control.clicked()
    }

    Keys.onEnterPressed: {
        control.clicked()
    }

    Keys.onSpacePressed: {
        control.clicked()
    }

    // Shift+Enter for long-press action
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
            (event.modifiers & Qt.ShiftModifier)) {
            control.pressAndHold()
            event.accepted = true
        }
    }

    // Announce button name when focused via keyboard (for accessibility)
    onActiveFocusChanged: {
        if (activeFocus && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            if (!control._isPressed) {
                AccessibilityManager.lastAnnouncedItem = control
                AccessibilityManager.announce(control.text)
            }
        }
    }
}
