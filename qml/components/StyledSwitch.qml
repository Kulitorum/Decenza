import QtQuick
import QtQuick.Controls
import Decenza

Switch {
    id: control

    // Optional accessibility label for context when text is empty
    property string accessibleName: ""

    // Sized from the indicator PLUS the control's padding. A flat 48 ignored the padding,
    // so the control overhung whatever width a layout reserved — visible as the Theme Mode
    // switches sitting on top of their card's border instead of inside it. Keeping the
    // padding in the width also keeps the touch target larger than the drawn switch.
    implicitWidth: control.leftPadding + Theme.scaled(44) + control.rightPadding
    implicitHeight: Math.max(Theme.scaled(28),
                             control.topPadding + Theme.scaled(24) + control.bottomPadding)

    // A disabled switch must LOOK disabled (same principle as ActionButton's
    // disabled dimming, though that one dims per-part) — without this an
    // `enabled:` gate is functionally dead but visually live.
    opacity: enabled ? 1.0 : 0.4

    indicator: Rectangle {
        // Flush with the control's right edge, not inset by rightPadding. These sit at the
        // end of a settings row after a filler, so the control's right edge IS the card
        // margin — drawing at leftPadding left the switch visually short of the margin
        // while the combo boxes and buttons in the same card sat flush against it. The
        // padding stays in implicitWidth, so the touch target is still the larger area.
        x: control.width - width
        y: parent.height / 2 - height / 2
        width: Theme.scaled(44)
        height: Theme.scaled(24)
        radius: height / 2
        color: control.checked ? Theme.primaryColor : Theme.insetBackgroundColor
        border.color: control.checked ? Theme.primaryColor : Theme.textSecondaryColor
        border.width: 1

        Rectangle {
            x: control.checked ? parent.width - width - Theme.scaled(3) : Theme.scaled(3)
            y: parent.height / 2 - height / 2
            width: Theme.scaled(18)
            height: Theme.scaled(18)
            radius: height / 2
            color: control.checked ? Theme.primaryContrastColor : Theme.textSecondaryColor

            Behavior on x {
                NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
            }
        }
    }

    contentItem: Text {
        text: control.text
        font: Theme.bodyFont
        color: Theme.textColor
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + Theme.scaled(8)
        Accessible.ignored: true
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: control.accessibleName || control.text || TranslationManager.translate("switch.accessibility.toggle", "Toggle")
    Accessible.checked: control.checked
    Accessible.focusable: true
    Accessible.onPressAction: control.toggle()
}
