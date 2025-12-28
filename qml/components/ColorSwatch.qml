import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property string colorName: "primaryColor"
    property string displayName: "Primary"
    property color colorValue: "#4e85f4"
    property bool selected: false

    signal clicked()

    height: 44
    color: selected ? Qt.lighter(Theme.surfaceColor, 1.3) : "transparent"
    radius: Theme.buttonRadius

    Row {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        // Color swatch
        Rectangle {
            width: 32
            height: 32
            radius: 6
            color: root.colorValue
            border.color: Theme.borderColor
            border.width: 1
            anchors.verticalCenter: parent.verticalCenter
        }

        // Name and hex combined
        Text {
            text: root.displayName + " " + root.colorValue
            color: Theme.textColor
            font.pixelSize: 14
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Selection indicator
    Rectangle {
        visible: root.selected
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: Theme.primaryColor
        radius: 1.5
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
