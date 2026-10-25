import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 480
    height: 480
    title: "Qt Wasm controls accessibility reproduction"

    Column {
        anchors.centerIn: parent
        spacing: 20
        TextField {
            id: field
            width: 320
            text: "initial value"
            Accessible.role: Accessible.EditableText
            Accessible.name: "Example field"
        }
        Button {
            text: "Set programmatically"
            onClicked: field.text = "updated value"
        }
        Label { text: "Current QML text: " + field.text }
        ComboBox {
            id: choice
            width: 320
            model: ["First", "Second", "Third"]
            Accessible.name: "Example choice"
        }
        Label { text: "Current QML choice: " + choice.currentText }
        CheckBox {
            id: check
            text: "Example checkbox"
        }
        Button {
            text: "Reset checkbox"
            onClicked: check.checked = false
        }
        Label { text: "Current QML checked: " + check.checked }
    }
}
