import QtQuick
import QtQuick.Controls
import DecenzaDE1

ComboBox {
    id: control

    implicitHeight: Theme.scaled(36)

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
        width: control.width
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
            border.width: Theme.scaled(1)
        }
    }

    delegate: ItemDelegate {
        width: control.width
        height: Theme.scaled(36)

        contentItem: Text {
            text: control.textRole ? model[control.textRole] : modelData
            font: Theme.bodyFont
            color: Theme.textColor
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: highlighted ? Theme.primaryColor : "transparent"
        }

        highlighted: control.highlightedIndex === index
    }

    Accessible.role: Accessible.ComboBox
    Accessible.name: control.displayText
}
