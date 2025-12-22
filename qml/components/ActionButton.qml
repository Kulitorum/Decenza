import QtQuick
import QtQuick.Controls
import DE1App

Button {
    id: control

    property string iconSource: ""
    property color backgroundColor: Theme.primaryColor

    implicitWidth: 150
    implicitHeight: 120

    contentItem: Column {
        spacing: 10
        anchors.centerIn: parent

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: control.iconSource
            width: 48
            height: 48
            fillMode: Image.PreserveAspectFit
            visible: control.iconSource !== ""
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.text
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }

    background: Rectangle {
        radius: Theme.buttonRadius
        color: {
            if (!control.enabled) return Theme.buttonDisabled
            if (control.pressed) return Qt.darker(control.backgroundColor, 1.2)
            if (control.hovered) return Qt.lighter(control.backgroundColor, 1.1)
            return control.backgroundColor
        }

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }
}
