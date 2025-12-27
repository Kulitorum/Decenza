import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Rectangle {
    id: root

    property string title: ""
    property color barColor: Theme.primaryColor
    property bool showBackButton: true

    // Allow additional content to be added on the right side
    default property alias extraContent: extraContentItem.data

    signal backClicked()

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: Theme.bottomBarHeight
    color: barColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Back button (square hitbox)
        RoundButton {
            visible: root.showBackButton
            Layout.preferredWidth: Theme.touchTargetLarge
            Layout.preferredHeight: Theme.touchTargetLarge
            flat: true
            icon.source: "qrc:/icons/back.svg"
            icon.width: Theme.scaled(28)
            icon.height: Theme.scaled(28)
            icon.color: "white"
            display: AbstractButton.IconOnly
            onClicked: root.backClicked()
        }

        Text {
            visible: root.title !== ""
            text: root.title
            color: "white"
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        Item { Layout.fillWidth: true }

        // Container for additional content
        Item {
            id: extraContentItem
            Layout.fillHeight: true
            implicitWidth: childrenRect.width
        }
    }
}
