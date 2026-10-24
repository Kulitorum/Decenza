import QtQuick
import Decenza

SharedTextField {
    controlStyle: NativeFormControlStyle {}
    accessibilityMode: AccessibilityManager.enabled

    onKeyboardShowRequested: Keyboard.show()
    onKeyboardHideRequested: Keyboard.hide()
}
