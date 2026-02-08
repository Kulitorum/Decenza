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

    // Accessibility: Let AccessibleTapHandler handle screen reader interaction
    // to avoid duplicate focus elements (icon + label announced separately)
    Accessible.ignored: true

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
                // Decorative - accessibility handled by AccessibleTapHandler
                Accessible.ignored: true
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.text
            color: control.enabled ? Theme.textColor : Theme.textSecondaryColor
            font: Theme.bodyFont
            // Decorative - accessibility handled by AccessibleTapHandler
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

    // Custom interaction handling using TapHandler (better touch responsiveness than MouseArea)
    AccessibleTapHandler {
        anchors.fill: parent
        enabled: control.enabled

        accessibleName: control.text + ". " + control._computedAccessibleDescription
        accessibleItem: control
        supportLongPress: true
        supportDoubleClick: true
        doubleClickInterval: 250

        // Track pressed state for visual feedback
        onIsPressedChanged: control._isPressed = isPressed

        onAccessibleClicked: {
            console.log("[ActionButton] accessibleClicked received for:", control.text)
            control.clicked()
        }
        onAccessibleDoubleClicked: {
            console.log("[ActionButton] accessibleDoubleClicked received for:", control.text)
            control.doubleClicked()
        }
        onAccessibleLongPressed: {
            console.log("[ActionButton] accessibleLongPressed received for:", control.text)
            // In accessibility mode, long-press triggers the secondary action (same as double-tap in normal mode)
            // This gives blind users access to the "go to page" action since quick double-tap is disabled
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
            if (accessibilityMode) {
                control.doubleClicked()
            } else {
                control.pressAndHold()
            }
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
    // Touch taps are handled by AccessibleTapHandler which has more context
    onActiveFocusChanged: {
        if (activeFocus && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            // Only announce if not being pressed (tap in progress = AccessibleTapHandler will handle it)
            if (!control._isPressed) {
                AccessibilityManager.lastAnnouncedItem = control
                AccessibilityManager.announce(control.text)
            }
        }
    }
}
