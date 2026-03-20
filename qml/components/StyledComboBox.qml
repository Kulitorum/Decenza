import QtQuick
import QtQuick.Controls
import Decenza

ComboBox {
    id: control

    implicitWidth: Theme.scaled(100)
    implicitHeight: Theme.scaled(36)

    // Set this to the field label so TalkBack announces the label, not the selected value
    property string accessibleLabel: ""

    // Optional function(index) → string for custom dialog item text.
    // When set, the dialog shows this text instead of textAt(i).
    // Useful for object models where you need richer display (e.g. name + IP).
    property var textFunction: null

    // Text shown in the dialog for empty/blank items (e.g. "(None)" for an empty first option)
    property string emptyItemText: ""

    contentItem: Text {
        text: control.displayText
        font: Theme.bodyFont
        color: control.enabled ? Theme.textColor : Theme.textSecondaryColor
        leftPadding: Theme.scaled(10)
        rightPadding: Theme.scaled(30)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    indicator: Text {
        x: control.width - width - Theme.scaled(10)
        y: control.height / 2 - height / 2
        text: "\u25BC"
        font.pixelSize: Theme.scaled(10)
        color: control.enabled ? Theme.textSecondaryColor : Qt.darker(Theme.textSecondaryColor, 1.5)
        Accessible.ignored: true
    }

    background: Rectangle {
        implicitHeight: Theme.scaled(36)
        color: control.enabled ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(128, 128, 128, 0.1)
        radius: Theme.scaled(6)
        border.color: control.activeFocus ? Theme.primaryColor : "transparent"
        border.width: control.activeFocus ? 1 : 0
    }

    // Suppress the native popup entirely — we use a Dialog instead
    popup: Popup {
        width: 0
        height: 0
        visible: false
    }

    // Delegate is unused (Dialog has its own delegates) but required by ComboBox
    delegate: ItemDelegate {
        width: 0
        height: 0
    }

    Accessible.role: Accessible.ComboBox
    Accessible.name: control.accessibleLabel || control.displayText
    Accessible.focusable: true
    Accessible.onPressAction: if (!selectionDialog.visible) selectionDialog.open()

    // Close dialog when ComboBox becomes invisible (page popped, tab switched)
    onVisibleChanged: if (!visible) selectionDialog.close()

    // Keyboard support: open dialog with Space/Enter (native popup is suppressed)
    Keys.onSpacePressed: if (!selectionDialog.visible) selectionDialog.open()
    Keys.onReturnPressed: if (!selectionDialog.visible) selectionDialog.open()
    Keys.onEnterPressed: if (!selectionDialog.visible) selectionDialog.open()

    // Intercept all taps to open our Dialog instead of the native Popup
    MouseArea {
        anchors.fill: parent
        z: 100
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
        Accessible.ignored: true

        onClicked: {
            if (!selectionDialog.visible) selectionDialog.open()
        }
    }

    // Build a plain JS array of display strings from the ComboBox model.
    // This avoids model-type issues (QVariant wrapping, delegateModel, etc.)
    function _buildItemList() {
        var items = []
        for (var i = 0; i < control.count; i++) {
            items.push(control.textFunction ? control.textFunction(i) : control.textAt(i))
        }
        return items
    }

    SelectionDialog {
        id: selectionDialog
        title: control.accessibleLabel || control.displayText
        options: control._buildItemList()
        currentIndex: control.currentIndex
        emptyItemText: control.emptyItemText
        onSelected: function(index, value) {
            control.currentIndex = index
            control.activated(index)
        }
    }
}
