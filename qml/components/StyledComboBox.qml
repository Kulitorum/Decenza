import QtQuick
import QtQuick.Controls
import DecenzaDE1

ComboBox {
    id: control

    implicitWidth: Theme.scaled(100)
    implicitHeight: Theme.scaled(36)

    // Set this to the field label so TalkBack announces the label, not the selected value
    property string accessibleLabel: ""

    // Calculate max width of all items for popup auto-sizing
    property real popupContentWidth: control.width

    function updatePopupWidth() {
        var maxWidth = control.width
        var padding = Theme.scaled(30)  // Left + right padding
        for (var i = 0; i < count; i++) {
            var item = model[i]
            if (typeof item === 'object' && textRole)
                item = item[textRole]
            textMetrics.text = item || ""
            maxWidth = Math.max(maxWidth, textMetrics.width + padding)
        }
        popupContentWidth = maxWidth
    }

    onModelChanged: Qt.callLater(updatePopupWidth)
    onCountChanged: Qt.callLater(updatePopupWidth)
    Component.onCompleted: Qt.callLater(updatePopupWidth)

    TextMetrics {
        id: textMetrics
        font: Theme.bodyFont
    }

    contentItem: Text {
        text: control.displayText
        font: Theme.bodyFont
        color: control.enabled ? Theme.textColor : Theme.textSecondaryColor
        leftPadding: Theme.scaled(10)
        rightPadding: Theme.scaled(30)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - Theme.scaled(10)
        y: control.height / 2 - height / 2
        text: "▼"
        font.pixelSize: Theme.scaled(10)
        color: control.enabled ? Theme.textSecondaryColor : Qt.darker(Theme.textSecondaryColor, 1.5)
    }

    background: Rectangle {
        implicitHeight: Theme.scaled(36)
        color: control.enabled ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(128, 128, 128, 0.1)
        radius: Theme.scaled(6)
        border.color: control.activeFocus ? Theme.primaryColor : "transparent"
        border.width: control.activeFocus ? 1 : 0
    }

    popup: Popup {
        y: control.height
        width: Math.max(control.width, Math.min(control.popupContentWidth, Theme.scaled(300)))
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, Theme.scaled(200))
            model: control.popup.visible ? control.delegateModel : null
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(6)
            border.color: Theme.borderColor
            border.width: 1
        }
    }

    delegate: ItemDelegate {
        width: control.popup.width
        height: Theme.scaled(36)

        contentItem: Text {
            text: {
                if (control.textRole && model[control.textRole] !== undefined)
                    return model[control.textRole]
                if (modelData !== undefined)
                    return modelData
                return ""
            }
            font: Theme.bodyFont
            color: Theme.textColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: highlighted ? Theme.primaryColor : "transparent"
        }

        highlighted: control.highlightedIndex === index
    }

    Accessible.role: Accessible.ComboBox
    Accessible.name: control.accessibleLabel || control.displayText
    Accessible.focusable: true
}
