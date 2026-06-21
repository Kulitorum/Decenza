import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Per-instance editor for a Time (clock) widget: picks the display mode
// (text/icon) and the color for this instance only. Persists via
// Settings.network.setItemProperty.
Dialog {
    id: popup

    property string itemId: ""
    property string displayMode: "text"
    property string clockColor: "white"

    function openForItem(id, display, color) {
        popup.itemId = id
        popup.displayMode = (display && display.length > 0) ? display : "text"
        popup.clockColor = (color && color.length > 0) ? color : "white"
        popup.open()
    }

    function pickDisplay(mode) {
        popup.displayMode = mode
        Settings.network.setItemProperty(popup.itemId, "displayMode", mode)
    }

    function pickColor(name) {
        popup.clockColor = name
        Settings.network.setItemProperty(popup.itemId, "color", name)
    }

    // Mirrors ClockItem.colorFor — the named choices map to the same semantic
    // chart colors the rest of the page uses.
    function colorFor(name) {
        switch (name) {
        case "green":  return Theme.pressureColor
        case "red":    return Theme.temperatureColor
        case "blue":   return Theme.flowColor
        case "orange": return Theme.warningColor
        default:       return Theme.textColor // "white"
        }
    }

    readonly property var displayChoices: [
        { value: "text", label: TranslationManager.translate("layoutEditor.displayText", "Text") },
        { value: "icon", label: TranslationManager.translate("layoutEditor.displayIcon", "Icon + value") }
    ]

    readonly property var colorChoices: [
        { value: "white",  label: TranslationManager.translate("layoutEditor.colorWhite", "White") },
        { value: "green",  label: TranslationManager.translate("layoutEditor.colorGreen", "Green") },
        { value: "red",    label: TranslationManager.translate("layoutEditor.colorRed", "Red") },
        { value: "blue",   label: TranslationManager.translate("layoutEditor.colorBlue", "Blue") },
        { value: "orange", label: TranslationManager.translate("layoutEditor.colorOrange", "Orange") }
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

        Text {
            text: TranslationManager.translate("layoutEditor.colorMode", "Color")
            color: Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: popup.colorChoices
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool sel: popup.clockColor === modelData.value
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(56)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.color: sel ? Theme.primaryColor : Theme.borderColor
                    border.width: sel ? 2 : 1
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.focusable: true
                    Accessible.onPressAction: colorMa.clicked(null)
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: Theme.scaled(20)
                            implicitHeight: Theme.scaled(20)
                            radius: width / 2
                            color: popup.colorFor(modelData.value)
                            border.color: Theme.borderColor
                            border.width: 1
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.label
                            color: Theme.textColor
                            font: Theme.labelFont
                        }
                    }
                    MouseArea { id: colorMa; anchors.fill: parent; onClicked: popup.pickColor(modelData.value) }
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
