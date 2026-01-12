import QtQuick
import QtQuick.Controls
import DecenzaDE1

Switch {
    id: control

    // Optional accessibility label for context when text is empty
    property string accessibleName: ""

    implicitWidth: Theme.scaled(48)
    implicitHeight: Theme.scaled(28)

    indicator: Rectangle {
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        width: Theme.scaled(44)
        height: Theme.scaled(24)
        radius: height / 2
        color: control.checked ? Theme.primaryColor : Theme.backgroundColor
        border.color: control.checked ? Theme.primaryColor : Theme.textSecondaryColor
        border.width: 1

        Rectangle {
            x: control.checked ? parent.width - width - Theme.scaled(3) : Theme.scaled(3)
            y: parent.height / 2 - height / 2
            width: Theme.scaled(18)
            height: Theme.scaled(18)
            radius: height / 2
            color: control.checked ? "white" : Theme.textSecondaryColor

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
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: {
        var label = control.accessibleName || control.text || TranslationManager.translate("switch.accessibility.toggle", "Toggle")
        var state = control.checked ?
            TranslationManager.translate("accessibility.on", "On") :
            TranslationManager.translate("accessibility.off", "Off")
        return label + ", " + state
    }
    Accessible.checked: control.checked
    Accessible.focusable: true
}
