import QtQuick
import QtQuick.Templates as T

T.Switch {
    id: control
    required property SharedSwitchStyle switchStyle
    property string accessibleName: ""

    padding: switchStyle.scaled(8)
    implicitWidth: Math.max(control.implicitContentWidth,
                            switchStyle.scaled(44)) + control.leftPadding + control.rightPadding
    implicitHeight: Math.max(switchStyle.scaled(28), control.implicitContentHeight,
                             switchStyle.scaled(24)) + control.topPadding + control.bottomPadding
    opacity: enabled ? 1.0 : 0.4

    indicator: Rectangle {
        x: control.width - width
        y: parent.height / 2 - height / 2
        width: control.switchStyle.scaled(44)
        height: control.switchStyle.scaled(24)
        radius: height / 2
        color: control.checked ? control.switchStyle.primaryColor : control.switchStyle.insetBackgroundColor
        border.color: control.checked ? control.switchStyle.primaryColor : control.switchStyle.textSecondaryColor
        border.width: 1
        Rectangle {
            x: control.checked ? parent.width - width - control.switchStyle.scaled(3)
                               : control.switchStyle.scaled(3)
            y: parent.height / 2 - height / 2
            width: control.switchStyle.scaled(18)
            height: width
            radius: height / 2
            color: control.checked ? control.switchStyle.primaryContrastColor
                                   : control.switchStyle.textSecondaryColor
            Behavior on x { NumberAnimation { duration: 100; easing.type: Easing.OutQuad } }
        }
    }

    contentItem: Text {
        text: control.text
        font: control.switchStyle.bodyFont
        color: control.switchStyle.textColor
        verticalAlignment: Text.AlignVCenter
        leftPadding: 0
        rightPadding: control.indicator.width + control.switchStyle.scaled(8)
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: control.accessibleName || control.text || control.switchStyle.fallbackToggleLabel
    Accessible.checked: control.checked
    Accessible.focusable: true
}
