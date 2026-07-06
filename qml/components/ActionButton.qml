import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

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

    // Set true when onDoubleClicked is connected — defers single-tap via TapHandler's built-in double-tap detection window
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

                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: control._contentColor
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.text
            color: control.enabled ? control._contentColor : Theme.textSecondaryColor
            font: Theme.bodyFont
            // Decorative - accessibility handled by Button itself
            Accessible.ignored: true
        }
    }

    readonly property color _effectiveBackground: control.backgroundColor
    readonly property color _contentColor: Theme.actionButtonContentColor

    background: Rectangle {
        radius: Theme.buttonRadius
        color: {
            if (!control.enabled) return Theme.buttonDisabled
            if (control._isPressed) return Qt.darker(control._effectiveBackground, 1.2)
            if (control.hovered || control.activeFocus) return Qt.lighter(control._effectiveBackground, 1.1)
            return control._effectiveBackground
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

    // TapHandler for touch/mouse interaction (no Accessible.* — Button handles that).
    // gesturePolicy: WithinBounds takes an exclusive grab, blocking Button's own internal
    // click emission and cancelling the gesture if the finger moves outside the bounds.
    // exclusiveSignals ensures singleTapped and doubleTapped are mutually exclusive —
    // without this, both fire on a double-tap (singleTapped for tap 1, doubleTapped for tap 2).
    TapHandler {
        enabled: control.enabled
        longPressThreshold: 0.5
        gesturePolicy: TapHandler.WithinBounds
        exclusiveSignals: TapHandler.SingleTap | TapHandler.DoubleTap
        cursorShape: Qt.PointingHandCursor

        // _longPressTriggered guards onTapped/onSingleTapped after a long-press fires.
        // TapHandler does not suppress tapped/singleTapped after longPressed — we must do it.
        property bool _longPressTriggered: false

        onPressedChanged: {
            control._isPressed = pressed
            if (!pressed) _longPressTriggered = false
        }

        onLongPressed: {
            _longPressTriggered = true
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
            if (accessibilityMode)
                control.doubleClicked()
            else
                control.pressAndHold()
        }

        onTapped: function(eventPoint, button) {
            if (_longPressTriggered) return
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
            if (accessibilityMode) {
                // Accessibility mode: first tap announces, second tap activates.
                // Double-tap detection is disabled — TalkBack's own double-tap gesture can be misdetected.
                if (AccessibilityManager.lastAnnouncedItem === control) {
                    control.clicked()
                } else {
                    AccessibilityManager.lastAnnouncedItem = control
                    AccessibilityManager.announce(control.text + ". " + control._computedAccessibleDescription)
                }
                return
            }
            // Without double-click support, activate immediately on every tap.
            // With double-click support, defer to onSingleTapped / onDoubleTapped.
            if (!control.supportDoubleClick)
                control.clicked()
        }

        onSingleTapped: function(eventPoint, button) {
            if (_longPressTriggered || !control.supportDoubleClick) return
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
            if (!accessibilityMode)
                control.clicked()
        }

        onDoubleTapped: function(eventPoint, button) {
            if (_longPressTriggered || !control.supportDoubleClick) return
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
            if (!accessibilityMode)
                control.doubleClicked()
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
