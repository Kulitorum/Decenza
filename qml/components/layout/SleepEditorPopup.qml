import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Per-instance editor for the Sleep widget's allowQuit, confirmQuit and showIcon options
// (composable-status-bar). Persists via Settings.network.setItemProperty.
DecenzaDialog {
    id: popup

    property string itemId: ""
    // All three are written by openForItem() before every open.
    property bool allowQuit
    property bool confirmQuit
    property bool showIcon

    function openForItem(id, props) {
        popup.itemId = id
        popup.allowQuit = LayoutActions.sleepOption(props, "allowQuit")
        popup.confirmQuit = LayoutActions.sleepOption(props, "confirmQuit")
        popup.showIcon = LayoutActions.sleepOption(props, "showIcon")
        popup.open()
    }

    function setAllowQuit(v) {
        popup.allowQuit = v
        Settings.network.setItemProperty(popup.itemId, "allowQuit", v)
    }

    function setConfirmQuit(v) {
        popup.confirmQuit = v
        Settings.network.setItemProperty(popup.itemId, "confirmQuit", v)
    }

    function setShowIcon(v) {
        popup.showIcon = v
        Settings.network.setItemProperty(popup.itemId, "showIcon", v)
    }

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(440), parent.width - Theme.spacingLarge * 2)
    padding: Theme.spacingMedium

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Text {
            text: TranslationManager.translate("layoutEditor.sleepOptions", "Sleep button")
            color: Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate("layoutEditor.sleepAllowQuit", "Long-press to quit")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate("layoutEditor.sleepAllowQuitHint", "Off = sleep on tap only, no hidden exit")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }
            StyledSwitch {
                accessibleName: TranslationManager.translate("layoutEditor.sleepAllowQuit", "Long-press to quit")
                checked: popup.allowQuit
                onToggled: popup.setAllowQuit(checked)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            enabled: popup.allowQuit
            opacity: enabled ? 1.0 : 0.5
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate("layoutEditor.sleepConfirmQuit", "Ask before quitting")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate("layoutEditor.sleepConfirmQuitHint", "Off = long-press quits at once")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }
            StyledSwitch {
                accessibleName: TranslationManager.translate("layoutEditor.sleepConfirmQuit", "Ask before quitting")
                checked: popup.confirmQuit
                onToggled: popup.setConfirmQuit(checked)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate("layoutEditor.sleepShowIcon", "Show icon")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate("layoutEditor.sleepShowIconHint", "Off = label only")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }
            StyledSwitch {
                accessibleName: TranslationManager.translate("layoutEditor.sleepShowIcon", "Show icon")
                checked: popup.showIcon
                onToggled: popup.setShowIcon(checked)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.buttonRadius
            color: doneMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("common.button.done", "Done")
            Accessible.focusable: true
            Accessible.onPressAction: doneMa.clicked(null)
            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate("common.button.done", "Done")
                color: Theme.primaryContrastColor
                font: Theme.bodyFont
            }
            MouseArea { id: doneMa; anchors.fill: parent; onClicked: popup.close() }
        }
    }
}
