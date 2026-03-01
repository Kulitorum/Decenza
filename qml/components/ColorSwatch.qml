import QtQuick
import QtQuick.Layouts
import DecenzaDE1

Rectangle {
    id: root

    property string colorName: "primaryColor"
    property string displayName: "Primary"
    property color colorValue: Theme.primaryColor
    property bool selected: false

    signal clicked()

    implicitHeight: Theme.scaled(44)
    color: selected ? Qt.lighter(Theme.surfaceColor, 1.3) : "transparent"
    radius: Theme.buttonRadius

    Row {
        anchors.fill: parent
        anchors.margins: Theme.scaled(8)
        spacing: Theme.scaled(12)

        // Color swatch
        Rectangle {
            width: Theme.scaled(32)
            height: Theme.scaled(32)
            radius: Theme.scaled(6)
            color: root.colorValue
            border.color: Theme.borderColor
            border.width: 1
            anchors.verticalCenter: parent.verticalCenter
        }

        // Name and hex combined
        Text {
            text: root.displayName + " " + root.colorValue
            color: Theme.textColor
            font.pixelSize: Theme.scaled(14)
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true
        }
    }

    // Selection indicator
    Rectangle {
        visible: root.selected
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.scaled(3)
        color: Theme.primaryColor
        radius: 1.5
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.displayName + " color" + (root.selected ? ", selected" : "")
    Accessible.focusable: true
    Accessible.onPressAction: swatchMa.clicked(null)

    MouseArea {
        id: swatchMa
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
