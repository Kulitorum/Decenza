// The swatch Repeater delegate below reads this file's `picker` id; Bound makes it
// statically resolvable. That delegate already declares its one injected role,
// `modelData`, required, so Bound cannot break role injection here.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Reusable "Color" section for the readout-widget option editors. Renders the
// WidgetColor palette as labelled swatches and persists the choice per instance
// via the shared LayoutService. Used by ReadoutOptionsPopup.
ColumnLayout {
    id: picker

    property string itemId: ""
    property string pendingItemId: ""
    readonly property bool writeBusy: layoutWrite.busy
    // Current selection. The host popup assigns this in its openForItem().
    property string colorChoice: "default"

    spacing: Theme.spacingMedium

    function pick(name) {
        var requestId = layoutWrite.submit("setItemProperty", { itemId: picker.itemId, key: "color", value: name }, -1)
        if (requestId.length > 0) {
            pendingItemId = picker.itemId
            picker.colorChoice = name
        }
    }

    LayoutWrite {
        id: layoutWrite
        onFailed: message => layoutToast.show(message)
        onFinished: function(requestId, operation, success, reason) {
            if (operation !== "setItemProperty") return
            var props = LayoutService.getItemProperties(picker.pendingItemId)
            if (picker.pendingItemId === picker.itemId)
                picker.colorChoice = props.color || "default"
            picker.pendingItemId = ""
        }
    }
    StatusToast { id: layoutToast; parent: Overlay.overlay; error: true }

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
            model: WidgetColor.choices
            delegate: Rectangle {
                id: swatchCell
                required property var modelData
                readonly property bool sel: picker.colorChoice === modelData.value
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(56)
                radius: Theme.buttonRadius
                color: "transparent"
                border.color: sel ? Theme.primaryColor : Theme.borderColor
                border.width: sel ? 2 : 1
                Accessible.role: Accessible.Button
                Accessible.name: modelData.label
                Accessible.focusable: true
                Accessible.onPressAction: swatchMa.clicked(null)
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: Theme.scaled(20)
                        implicitHeight: Theme.scaled(20)
                        radius: width / 2
                        color: WidgetColor.swatch(swatchCell.modelData.value)
                        border.color: Theme.borderColor
                        border.width: 1
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: swatchCell.modelData.label
                        color: Theme.textColor
                        font: Theme.labelFont
                    }
                }
                MouseArea { id: swatchMa; anchors.fill: parent; onClicked: picker.pick(swatchCell.modelData.value) }
            }
        }
    }
}
