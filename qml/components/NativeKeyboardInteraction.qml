import QtQuick
import Decenza

SharedKeyboardInteraction {
    keyboardHeight: Keyboard.rectangle.height
    onShowRequested: Keyboard.show()
    onHideRequested: Keyboard.hide()
    onCommitRequested: Keyboard.commit()
}
