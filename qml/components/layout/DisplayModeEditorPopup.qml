import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Per-instance display-mode editor (composable-status-bar) for readout widgets:
// text vs icon. Persists via Settings.network.setItemProperty.
Dialog {
    id: popup

    property string itemId: ""
    property string displayMode: "text"

    function openForItem(id, mode) {
        popup.itemId = id
        popup.displayMode = (mode && mode.length > 0) ? mode : "text"
        popup.open()
    }

    function pick(mode) {
        popup.displayMode = mode
        Settings.network.setItemProperty(popup.itemId, "displayMode", mode)
    }

    readonly property var choices: [
        { value: "text", label: TranslationManager.translate("layoutEditor.displayText", "Text") },
        { value: "icon", label: TranslationManager.translate("layoutEditor.displayIcon", "Icon + value") }
    ]

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(420), parent.width - Theme.spacingLarge * 2)
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
            text: TranslationManager.translate("layoutEditor.displayMode", "Display")
            color: Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: popup.choices
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool sel: popup.displayMode === modelData.value
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    radius: Theme.buttonRadius
                    color: sel ? Theme.primaryColor : "transparent"
                    border.color: sel ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.focusable: true
                    Accessible.onPressAction: choiceMa.clicked(null)
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: parent.sel ? Theme.primaryContrastColor : Theme.textColor
                        font: Theme.labelFont
                    }
                    MouseArea { id: choiceMa; anchors.fill: parent; onClicked: popup.pick(modelData.value) }
                }
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
