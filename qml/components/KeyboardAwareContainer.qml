import QtQuick

// Native keyboard-service adapter; layout and focus policy live in shared QML.
SharedKeyboardAwareContainer {
    keyboardInteraction: NativeKeyboardInteraction {}
}
