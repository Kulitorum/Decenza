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

    property string itemType: "profile"  // "profile", "recipe", or "shot"
    property bool canSave: true
    property bool showSaveAs: true  // Set to false for items that don't support "Save As"

    signal discardClicked()
    signal saveAsClicked()
    signal saveClicked()

    onOpened: {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(TranslationManager.translate("unsavedChanges.announcement", "Unsaved Changes. You have unsaved changes to this %1. What would you like to do? Discard, Save As, or Save.").arg(root.itemType))
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: "white"
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)
            Layout.topMargin: Theme.scaled(10)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: TranslationManager.translate("unsavedChanges.title", "Unsaved Changes")
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
            text: TranslationManager.translate("unsavedChanges.message", "You have unsaved changes to this %1.\nWhat would you like to do?").arg(root.itemType)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
        }

        // Buttons - use Grid for equal sizing
        Grid {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            columns: root.showSaveAs ? 3 : 2
            spacing: Theme.scaled(10)

            property real buttonWidth: root.showSaveAs ? (width - spacing * 2) / 3 : (width - spacing) / 2
            property real buttonHeight: Theme.scaled(50)

            AccessibleButton {
                width: parent.buttonWidth
                height: parent.buttonHeight
                text: TranslationManager.translate("unsavedChanges.discard", "Discard")
                accessibleName: TranslationManager.translate("unsavedChanges.discardChanges", "Discard changes")
                onClicked: {
                    root.close()
                    root.discardClicked()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
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

            AccessibleButton {
                visible: root.showSaveAs
                width: visible ? parent.buttonWidth : 0
                height: parent.buttonHeight
                text: TranslationManager.translate("unsavedChanges.saveAs", "Save As")
                accessibleName: TranslationManager.translate("unsavedChanges.saveAsNew", "Save as new %1").arg(root.itemType)
                onClicked: {
                    root.close()
                    root.saveAsClicked()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
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
                width: parent.buttonWidth
                height: parent.buttonHeight
                text: TranslationManager.translate("unsavedChanges.save", "Save")
                accessibleName: TranslationManager.translate("unsavedChanges.saveItem", "Save %1").arg(root.itemType)
                enabled: root.canSave
                onClicked: {
                    root.close()
                    root.saveClicked()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
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
