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
    property string displayMode: "text"
    property bool showRatio: true

    function openForItem(id, mode, display, color, ratio) {
        popup.itemId = id
        popup.dataMode = (mode && mode.length > 0) ? mode : "gross"
        popup.displayMode = (display && display.length > 0) ? display : "text"
        popup.showRatio = (ratio === undefined) ? true : ratio
        colorPicker.colorChoice = (color && color.length > 0) ? color : "default"
        popup.open()
    }

    function pick(mode) {
        popup.dataMode = mode
        Settings.network.setItemProperty(popup.itemId, "dataMode", mode)
    }

    function pickDisplay(mode) {
        popup.displayMode = mode
        Settings.network.setItemProperty(popup.itemId, "displayMode", mode)
    }

    function setShowRatio(v) {
        popup.showRatio = v
        Settings.network.setItemProperty(popup.itemId, "showRatio", v)
    }

    readonly property var displayChoices: [
        { value: "text", label: TranslationManager.translate("layoutEditor.displayText", "Text") },
        { value: "icon", label: TranslationManager.translate("layoutEditor.displayIcon", "Icon + value") }
    ]

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
    width: Math.min(Theme.scaled(520), parent.width - Theme.spacingLarge * 2)
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
                model: popup.displayChoices
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
                    Accessible.onPressAction: dispMa.clicked(null)
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: parent.sel ? Theme.primaryContrastColor : Theme.textColor
                        font: Theme.labelFont
                    }
                    MouseArea { id: dispMa; anchors.fill: parent; onClicked: popup.pickDisplay(modelData.value) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: TranslationManager.translate("layoutEditor.scaleShowRatio", "Show ratio")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                Text {
                    text: TranslationManager.translate("layoutEditor.scaleShowRatioHint", "Off = weight only, no 1:X.X suffix")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }
            StyledSwitch {
                accessibleName: TranslationManager.translate("layoutEditor.scaleShowRatio", "Show ratio")
                checked: popup.showRatio
                onToggled: popup.setShowRatio(checked)
            }
        }

        WidgetColorPicker {
            id: colorPicker
            Layout.fillWidth: true
            itemId: popup.itemId
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
