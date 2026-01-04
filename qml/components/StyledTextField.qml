import QtQuick
import QtQuick.Controls
import DecenzaDE1

TextField {
    id: control

    font: Theme.bodyFont
    color: Theme.textColor
    placeholderTextColor: Theme.textSecondaryColor

    // Explicit padding prevents Material theme's floating label animation
    leftPadding: Theme.scaled(12)
    rightPadding: Theme.scaled(12)
    topPadding: Theme.scaled(12)
    bottomPadding: Theme.scaled(12)

    background: Rectangle {
        color: Theme.backgroundColor
        radius: Theme.scaled(4)
        border.color: control.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
        border.width: Theme.scaled(1)
    }
}
