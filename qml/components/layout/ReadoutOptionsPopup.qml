import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Unified per-instance editor for readout widgets. The sections shown are
// exactly the option keys the widget type declares in the readout capability
// schema (Settings.network.optionKeysForType): dataMode, displayMode,
// showRatio, color. Persists via Settings.network.setItemProperty.
Dialog {
    id: popup

    property string itemId: ""
    property string widgetType: ""
    property var optionKeys: []
    property string dataMode: "gross"
    property string displayMode: "text"
    property bool showRatio: true

    // An absent stored displayMode always means "today's rendering": "icon" for
    // the battery readouts, "text" for the rest. Keep in sync with the web
    // editor's dispDefault (shotserver_layout.cpp) and the item components'
    // displayMode defaults.
    function defaultDisplayMode(type) {
        return (type === "batteryLevel" || type === "scaleBattery") ? "icon" : "text"
    }

    function openForItem(id, props) {
        popup.itemId = id
        popup.widgetType = props.type || ""
        popup.optionKeys = Settings.network.optionKeysForType(popup.widgetType)
        popup.dataMode = props.dataMode || "gross"
        popup.displayMode = props.displayMode || defaultDisplayMode(popup.widgetType)
        popup.showRatio = props.showRatio !== undefined ? props.showRatio : true
        colorPicker.colorChoice = props.color || "default"
        popup.open()
    }

    function hasOption(key) {
        return popup.optionKeys.indexOf(key) >= 0
    }

    function pickDataMode(mode) {
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
        { value: "text", label: TranslationManager.translate("layoutEditor.displayValueOnly", "Value only") },
        { value: "icon", label: TranslationManager.translate("layoutEditor.displayIcon", "Icon + value") }
    ]

    readonly property var dataModeChoices: [
        { value: "gross",        label: TranslationManager.translate("layoutEditor.scaleGross", "Gross weight") },
        { value: "netBeans",     label: TranslationManager.translate("layoutEditor.scaleNetBeans", "Net beans (minus dose tare)") },
        { value: "netMilk",      label: TranslationManager.translate("layoutEditor.scaleNetMilk", "Net milk (minus pitcher)") },
        { value: "contextAware", label: TranslationManager.translate("layoutEditor.scaleContext", "Context-aware (milk while steaming, else beans)") },
        { value: "expectedYield", label: TranslationManager.translate("layoutEditor.scaleExpectedYield", "Expected output (target weight)") }
    ]

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(520), parent.width - Theme.spacingLarge * 2)
    // Cap to the window so the Done button stays reachable; the option
    // sections scroll inside optionsFlick when they don't fit (the scale
    // weight instance stacks four sections). Same pattern as CustomEditorPopup.
    height: Math.min(implicitHeight, parent.height * 0.85)
    padding: Theme.spacingMedium

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Flickable {
            id: optionsFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: optionsCol.implicitHeight
            contentWidth: width
            contentHeight: optionsCol.implicitHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: optionsFlick.contentHeight > optionsFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: optionsCol
                width: optionsFlick.width
                spacing: Theme.spacingMedium

                Text {
                    visible: popup.hasOption("dataMode")
                    text: TranslationManager.translate("layoutEditor.scaleDataMode", "Scale data mode")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(20)
                    font.bold: true
                }

                Repeater {
                    model: popup.hasOption("dataMode") ? popup.dataModeChoices : []
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
                        Accessible.onPressAction: dataMa.clicked(null)
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
                        MouseArea { id: dataMa; anchors.fill: parent; onClicked: popup.pickDataMode(modelData.value) }
                    }
                }

                Text {
                    visible: popup.hasOption("displayMode")
                    text: TranslationManager.translate("layoutEditor.displayMode", "Display")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(20)
                    font.bold: true
                }

                RowLayout {
                    visible: popup.hasOption("displayMode")
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
                    visible: popup.hasOption("showRatio")
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
                    visible: popup.hasOption("color")
                    Layout.fillWidth: true
                    itemId: popup.itemId
                }
            }
        }

        // Done stays outside the scroll area so it is always visible.
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
