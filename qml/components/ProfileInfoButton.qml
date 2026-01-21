import QtQuick
import QtQuick.Controls
import DecenzaDE1

// A small (i) info button that can be placed next to any profile name
// Emits clicked() signal - parent handles navigation to ProfileInfoPage

Item {
    id: root

    // Profile to show info for
    property string profileFilename: ""
    property string profileName: ""

    // Size
    property real buttonSize: Theme.scaled(28)

    width: buttonSize
    height: buttonSize

    signal clicked()

    Rectangle {
        id: button
        anchors.fill: parent
        radius: width / 2
        color: mouseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.15) :
               mouseArea.containsMouse ? Theme.surfaceColor : "transparent"
        border.width: 1
        border.color: mouseArea.containsMouse || mouseArea.pressed ? Theme.borderColor : "transparent"

        Behavior on color { ColorAnimation { duration: 100 } }
        Behavior on border.color { ColorAnimation { duration: 100 } }

        Text {
            anchors.centerIn: parent
            text: "i"
            font.pixelSize: Theme.scaled(14)
            font.bold: true
            font.italic: true
            font.family: "serif"
            color: Theme.primaryColor
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor

            onClicked: root.clicked()
        }

        // Accessibility
        Accessible.role: Accessible.Button
        Accessible.name: TranslationManager.translate("profileinfo.button.accessible",
            "Profile info for") + " " + (root.profileName || root.profileFilename)
        Accessible.focusable: true
    }

    // Focus indicator
    Rectangle {
        anchors.fill: button
        anchors.margins: -Theme.focusMargin
        visible: button.activeFocus
        color: "transparent"
        border.width: Theme.focusBorderWidth
        border.color: Theme.focusColor
        radius: button.radius + Theme.focusMargin
    }
}
