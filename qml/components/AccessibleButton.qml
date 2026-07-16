import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Decenza

// Button with required accessibility - enforces accessibleName at compile time
Button {
    id: root

    // Required property - will cause compile error if not provided
    required property string accessibleName

    // Optional description for additional context
    property string accessibleDescription: ""

    // Style variants (only one should be true)
    property bool primary: false      // Filled primary color background
    property bool subtle: false       // Glass-like semi-transparent (for dark backgrounds)
    property bool destructive: false  // Red/error color for delete actions
    property bool warning: false      // Orange/warning color for update actions

    // For external reference
    property Item accessibleItem: root

    // Live press state — Button.down stays false because touchArea below accepts
    // the press. Bind custom background visuals to this instead of `down`.
    readonly property alias isPressed: touchArea.pressed

    // The current variant's fill, before press darkening.
    readonly property color _fillColor: {
        if (root.subtle) return Qt.rgba(1, 1, 1, 0.2)
        if (root.destructive) return Theme.errorColor
        if (root.warning) return Theme.warningButtonColor
        if (root.primary) return Theme.primaryColor
        return Theme.surfaceColor
    }

    // Foreground (icon + label) for the enabled state.
    //
    // Only warning derives its foreground: amber is the one fill here that white fails
    // outright on (white on #FFA500 is 1.97:1, against a 4.5:1 minimum), and both black
    // and white are unambiguous choices for it. Every other variant keeps
    // primaryContrastColor — deriving them would change established looks across the
    // whole app. That leaves errorColor's white at 3.4:1, a real gap, but one that
    // belongs to its own change rather than riding along here.
    readonly property color _foregroundColor: {
        if (root.warning) return Theme.contrastColorFor(root._fillColor)
        if (root.primary || root.subtle || root.destructive) return Theme.primaryContrastColor
        return Theme.textColor
    }

    // Optional font overrides (0/-1 = use defaults)
    property int _customFontSize: 0
    property int _customFontWeight: -1

    implicitHeight: Theme.scaled(44)
    leftPadding: Theme.scaled(20)
    rightPadding: Theme.scaled(20)

    // Icon styling — set icon.source to show an icon before the text.
    // tintIcon: recolor a monochrome SVG icon to icon.color (opt-in — leave false
    // for multicolor icons/emoji, which must keep their native colors).
    property bool tintIcon: false
    icon.width: Theme.scaled(16)
    icon.height: Theme.scaled(16)
    icon.color: {
        if (!root.enabled) {
            // Disabled primary/colored buttons: use semi-transparent contrast color
            if (root.primary || root.destructive || root.warning) return Qt.rgba(root._foregroundColor.r, root._foregroundColor.g, root._foregroundColor.b, 0.5)
            if (root.subtle) return Qt.rgba(root._foregroundColor.r, root._foregroundColor.g, root._foregroundColor.b, 0.4)
            return Theme.textSecondaryColor
        }
        return root._foregroundColor
    }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Accessible.ignored: true

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: root.icon.source.toString() !== "" && root.text !== "" ? Theme.scaled(6) : 0
            Accessible.ignored: true

            Image {
                source: root.icon.source
                sourceSize.width: root.icon.width
                sourceSize.height: root.icon.height
                anchors.verticalCenter: parent.verticalCenter
                visible: root.icon.source.toString() !== ""
                opacity: root.enabled ? 1.0 : 0.5
                Accessible.ignored: true
                layer.enabled: root.tintIcon
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.icon.color
                }
            }

            Text {
                text: root.text
                font.pixelSize: root._customFontSize > 0 ? root._customFontSize : Theme.scaled(16)
                font.family: Theme.bodyFont.family
                font.weight: root._customFontWeight >= 0 ? root._customFontWeight : Font.Normal
                color: root.icon.color
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                visible: root.text !== ""
                Accessible.ignored: true
            }
        }
    }

    background: Rectangle {
        implicitHeight: Theme.scaled(44)
        // Needs both: touchArea fills the whole button and accepts every pointer press
        // (see isPressed above), so `down` never goes true for a pointer — but keyboard
        // activation sets `down` without touchArea ever seeing it.
        readonly property bool showPressed: root.down || root.isPressed
        color: {
            var fill
            if (root.subtle) {
                fill = showPressed ? Qt.rgba(1, 1, 1, 0.3) : Qt.rgba(1, 1, 1, 0.2)
            } else if (root.destructive || root.warning || root.primary) {
                fill = showPressed ? Qt.darker(root._fillColor, 1.1) : root._fillColor
            } else {
                fill = showPressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
            }
            // Fade the fill when disabled, not just the label. Filled variants draw no
            // border, so without this a disabled button keeps a full-strength background
            // and reads as active — only its text goes half-transparent, which looks more
            // like low-contrast text than a disabled control.
            return root.enabled ? fill : Qt.rgba(fill.r, fill.g, fill.b, fill.a * 0.5)
        }
        border.width: (root.primary || root.subtle || root.destructive || root.warning) ? 0 : 1
        border.color: (root.primary || root.subtle || root.destructive || root.warning) ? "transparent" : (root.enabled ? Theme.borderColor : Qt.darker(Theme.borderColor, 1.2))
        radius: Theme.scaled(6)

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }

    // Accessibility on the Button itself (not delegated to a child)
    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleDescription ? (root.accessibleName + ". " + root.accessibleDescription) : root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: {
        if (root.enabled) {
            root.clicked()
        }
    }

    // Focus indicator
    FocusIndicator {
        targetItem: root
        visible: root.activeFocus
    }

    // Clear lastAnnouncedItem when destroyed to prevent dangling pointer crash
    Component.onDestruction: {
        if (typeof AccessibilityManager !== "undefined" &&
            AccessibilityManager.lastAnnouncedItem === root) {
            AccessibilityManager.lastAnnouncedItem = null
        }
    }

    // Plain MouseArea for tap-to-announce in accessibility mode (no Accessible.* — Button handles that)
    MouseArea {
        id: touchArea
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor

        onPressed: function(mouse) {
            mouse.accepted = true
        }

        onClicked: {
            var accessibilityMode = typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

            if (accessibilityMode) {
                if (AccessibilityManager.lastAnnouncedItem === root) {
                    // Second tap on same item = activate
                    root.clicked()
                } else {
                    // First tap = announce only
                    AccessibilityManager.lastAnnouncedItem = root
                    AccessibilityManager.announce(root.Accessible.name)
                }
            } else {
                // Normal mode: activate immediately
                root.clicked()
            }
        }
    }

    // Announce button name when focused via keyboard (for accessibility).
    // Accessible.name, not accessibleName: the former folds in accessibleDescription,
    // and callers use that to carry state the label itself doesn't say.
    onActiveFocusChanged: {
        if (activeFocus && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.lastAnnouncedItem = root
            AccessibilityManager.announce(root.Accessible.name)
        }
    }
}
