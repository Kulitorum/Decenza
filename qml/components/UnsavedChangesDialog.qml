import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Dialog {
    id: root
    anchors.centerIn: parent
    width: Theme.scaled(400)
    modal: true
    padding: 0

    property string itemType: "profile"  // "profile" or "recipe"
    property bool canSave: true

    signal discardClicked()
    signal saveAsClicked()
    signal saveClicked()

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Unsaved Changes")
                font: Theme.titleFont
                color: Theme.textColor
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        // Message
        Text {
            text: qsTr("You have unsaved changes to this %1.\nWhat would you like to do?").arg(root.itemType)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            spacing: Theme.scaled(10)

            AccessibleButton {
                text: qsTr("Discard")
                accessibleName: qsTr("Discard changes")
                onClicked: {
                    root.close()
                    root.discardClicked()
                }
                background: Rectangle {
                    implicitWidth: Theme.scaled(90)
                    implicitHeight: Theme.scaled(44)
                    radius: Theme.buttonRadius
                    color: parent.down ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }

            AccessibleButton {
                text: qsTr("Save As...")
                accessibleName: qsTr("Save as new %1").arg(root.itemType)
                onClicked: {
                    root.close()
                    root.saveAsClicked()
                }
                background: Rectangle {
                    implicitWidth: Theme.scaled(100)
                    implicitHeight: Theme.scaled(44)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.primaryColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: Theme.primaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                text: qsTr("Save")
                accessibleName: qsTr("Save %1").arg(root.itemType)
                enabled: root.canSave
                onClicked: {
                    root.close()
                    root.saveClicked()
                }
                background: Rectangle {
                    implicitWidth: Theme.scaled(80)
                    implicitHeight: Theme.scaled(44)
                    radius: Theme.buttonRadius
                    color: parent.enabled
                        ? (parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor)
                        : Theme.buttonDisabled
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
