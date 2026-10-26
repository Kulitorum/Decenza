import QtQuick
import QtQuick.Layouts
import Decenza

DecenzaDialog {
    id: root
    anchors.centerIn: parent
    width: Theme.scaled(400)
    modal: true
    padding: 0

    property string itemType: "profile"  // "profile", "recipe", or "shot"
    property bool canSave: true
    property bool canDiscard: true
    property bool showSave: true
    property bool showSaveAs: true  // Set to false for items that don't support "Save As"
    property bool showTry: false    // Show "Try" button to test changes without saving
    property bool showCancel: false
    property string titleText: ""
    property string messageText: ""

    property bool _choiceIsExit: false

    signal discardClicked()
    signal tryClicked()
    signal saveAsClicked()
    signal saveClicked()
    signal cancelClicked()
    signal leaveCancelled()

    onClosed: {
        if (!_choiceIsExit)
            leaveCancelled()
    }

    onOpened: {
        _choiceIsExit = false
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled) {
            let msg
            if (root.showTry && root.showSaveAs)
                msg = TranslationManager.translate("unsavedChanges.announcementWithTry", "Unsaved Changes. You have unsaved changes to this %1. What would you like to do? Discard, Use Unsaved, Save As, or Save.").arg(root.itemType)
            else if (root.showTry)
                msg = TranslationManager.translate("unsavedChanges.announcementTryNoSaveAs", "Unsaved Changes. You have unsaved changes to this %1. What would you like to do? Discard, Use Unsaved, or Save.").arg(root.itemType)
            else
                msg = TranslationManager.translate("unsavedChanges.announcement", "Unsaved Changes. You have unsaved changes to this %1. What would you like to do? Discard, Save As, or Save.").arg(root.itemType)
            AccessibilityManager.announce(root.messageText || msg)
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.primaryContrastColor
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
                text: root.titleText || TranslationManager.translate("unsavedChanges.title", "Unsaved Changes")
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
            text: root.messageText || TranslationManager.translate("unsavedChanges.message", "You have unsaved changes to this %1.\nWhat would you like to do?").arg(root.itemType)
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
        }

        // Buttons - use Grid for equal sizing
        // With showTry, use 2 rows: [Discard, Try] top, [Save As, Save] bottom
        // Without showTry, single row: [Discard, (Save As), Save]
        Grid {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            columns: root.showTry ? 2 : visibleColumns
            spacing: Theme.scaled(10)

            property int visibleColumns: Math.max(2, 1 + Number(root.showCancel)
                                                      + Number(root.showSaveAs)
                                                      + Number(root.showSave))
            property real buttonWidth: (width - spacing * (visibleColumns - 1)) / visibleColumns
            property real buttonHeight: Theme.scaled(50)

            AccessibleButton {
                id: discardButton
                width: parent.buttonWidth
                height: parent.buttonHeight
                enabled: root.canDiscard
                text: TranslationManager.translate("unsavedChanges.discard", "Discard")
                accessibleName: TranslationManager.translate("unsavedChanges.discardChanges", "Discard changes")
                onClicked: {
                    root._choiceIsExit = true
                    root.close()
                    root.discardClicked()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
                    radius: Theme.buttonRadius
                    color: !discardButton.enabled ? Theme.buttonDisabled
                        : (discardButton.down || discardButton.isPressed
                           ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor)
                }
                contentItem: Text {
                    text: discardButton.text
                    font: Theme.bodyFont
                    color: discardButton.enabled ? Theme.primaryContrastColor : Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                id: cancelButton
                visible: root.showCancel
                width: visible ? parent.buttonWidth : 0
                height: visible ? parent.buttonHeight : 0
                text: TranslationManager.translate("common.cancel", "Cancel")
                accessibleName: text
                onClicked: {
                    root._choiceIsExit = true
                    root.close()
                    root.cancelClicked()
                    root.leaveCancelled()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.textSecondaryColor
                }
                contentItem: Text {
                    text: cancelButton.text
                    font: Theme.bodyFont
                    color: Theme.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                id: useUnsavedButton
                visible: root.showTry
                width: visible ? parent.buttonWidth : 0
                height: visible ? parent.buttonHeight : 0
                text: TranslationManager.translate("unsavedChanges.useUnsaved", "Use Unsaved")
                accessibleName: TranslationManager.translate("unsavedChanges.useUnsavedChanges", "Use unsaved changes without saving")
                onClicked: {
                    root._choiceIsExit = true
                    root.close()
                    root.tryClicked()
                    root.leaveCancelled()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.successColor
                }
                contentItem: Text {
                    text: useUnsavedButton.text
                    font: Theme.bodyFont
                    color: Theme.successColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                id: saveAsButton
                visible: root.showSaveAs
                width: visible ? parent.buttonWidth : 0
                height: visible ? parent.buttonHeight : 0
                text: TranslationManager.translate("unsavedChanges.saveAs", "Save As")
                accessibleName: TranslationManager.translate("unsavedChanges.saveAsNew", "Save as new %1").arg(root.itemType)
                onClicked: {
                    root._choiceIsExit = true
                    root.close()
                    root.saveAsClicked()
                    root.leaveCancelled()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.primaryColor
                }
                contentItem: Text {
                    text: saveAsButton.text
                    font: Theme.bodyFont
                    color: Theme.primaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                id: saveButton
                visible: root.showSave
                width: parent.buttonWidth
                height: parent.buttonHeight
                text: TranslationManager.translate("unsavedChanges.save", "Save")
                accessibleName: TranslationManager.translate("unsavedChanges.saveItem", "Save %1").arg(root.itemType)
                enabled: root.canSave
                onClicked: {
                    root._choiceIsExit = true
                    root.close()
                    root.saveClicked()
                    root.leaveCancelled()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(60)
                    radius: Theme.buttonRadius
                    color: saveButton.enabled
                        ? (saveButton.down || saveButton.isPressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor)
                        : Theme.buttonDisabled
                }
                contentItem: Text {
                    text: saveButton.text
                    font: Theme.bodyFont
                    color: Theme.primaryContrastColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
