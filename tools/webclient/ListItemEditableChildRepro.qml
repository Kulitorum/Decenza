pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 520
    height: 300
    title: "Qt Wasm ListItem editable-child reproduction"

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Button {
            text: "Change accessible row name"
            onClicked: rows.model.setProperty(0, "label", "Updated row name")
        }

        ListView {
            id: rows
            width: parent.width
            height: 180
            model: ListModel {
                ListElement {
                    label: "Initial row name"
                    value: "Editable child value"
                }
            }

            delegate: Rectangle {
                id: row
                required property string label
                required property string value

                width: rows.width
                height: 120
                color: "#e8e8e8"

                Accessible.role: Accessible.ListItem
                Accessible.name: row.label

                TextEdit {
                    anchors.fill: parent
                    anchors.margins: 12
                    text: row.value
                    Accessible.role: Accessible.EditableText
                    Accessible.name: "Editable child"
                    Accessible.focusable: true
                }
            }
        }
    }
}
