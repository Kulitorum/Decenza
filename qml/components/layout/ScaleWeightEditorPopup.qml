import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Per-instance editor for a Scale Weight widget (composable-brew-bar): picks the
// data mode for this instance only. Persists via Settings.network.setItemProperty.
Dialog {
    id: popup

    property string itemId: ""
    property string dataMode: ""

    function openForItem(id, mode) {
        popup.itemId = id
        popup.dataMode = (mode && mode.length > 0) ? mode : "gross"
        popup.open()
    }

    function pick(mode) {
        popup.dataMode = mode
        Settings.network.setItemProperty(popup.itemId, "dataMode", mode)
    }

    readonly property var choices: [
        { value: "gross",        label: TranslationManager.translate("layoutEditor.scaleGross", "Gross weight") },
        { value: "netBeans",     label: TranslationManager.translate("layoutEditor.scaleNetBeans", "Net beans (minus dose tare)") },
        { value: "netMilk",      label: TranslationManager.translate("layoutEditor.scaleNetMilk", "Net milk (minus pitcher)") },
        { value: "contextAware", label: TranslationManager.translate("layoutEditor.scaleContext", "Context-aware (milk while steaming, else beans)") }
    ]

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(460), parent.width - Theme.spacingLarge * 2)
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
            text: TranslationManager.translate("layoutEditor.scaleDataMode", "Scale data mode")
            color: Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        Repeater {
            model: popup.choices
            delegate: Rectangle {
                required property var modelData
                readonly property bool sel: popup.dataMode === modelData.value
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
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: parent.sel ? Theme.primaryContrastColor : Theme.textColor
                    font: Theme.labelFont
                    elide: Text.ElideRight
                }
                MouseArea { id: choiceMa; anchors.fill: parent; onClicked: popup.pick(modelData.value) }
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
