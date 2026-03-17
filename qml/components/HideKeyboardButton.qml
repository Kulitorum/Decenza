import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Decenza

// Circular button to dismiss the soft keyboard inside modal dialogs.
// The global hide-keyboard button in main.qml is behind the modal overlay
// and can't be tapped, so modal dialogs with text inputs need their own.
//
// Usage: place inside a dialog header or content area, anchored to a right edge.
// Automatically hides on desktop and when no text input has focus.
Rectangle {
    id: root

    width: Theme.scaled(36)
    height: Theme.scaled(36)
    radius: Theme.scaled(18)
    color: Theme.primaryColor
    visible: _hasTextInputFocus && (Qt.platform.os === "android" || Qt.platform.os === "ios")

    // Track whether a text input has focus. Uses Window.window attached property
    // which is reactive in Qt 6 (windowChanged + activeFocusItemChanged signals).
    property bool _hasTextInputFocus: {
        var item = root.Window.window ? root.Window.window.activeFocusItem : null
        if (!item) return false
        return "cursorPosition" in item
    }

    Accessible.ignored: true

    Image {
        anchors.centerIn: parent
        width: Theme.scaled(20)
        height: Theme.scaled(20)
        source: "qrc:/icons/hide-keyboard.svg"
        sourceSize: Qt.size(width, height)
        Accessible.ignored: true
    }

    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: TranslationManager.translate("main.hidekeyboard", "Hide keyboard")
        accessibleItem: root
        onAccessibleClicked: {
            // Must clear focus BEFORE hiding keyboard, otherwise
            // KeyboardAwareContainer sees focus + no keyboard and reopens it
            var window = root.Window.window
            if (window && window.activeFocusItem)
                window.activeFocusItem.focus = false
            Qt.inputMethod.hide()
        }
    }
}
