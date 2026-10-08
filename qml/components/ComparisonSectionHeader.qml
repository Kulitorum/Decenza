import QtQuick
import QtQuick.Layouts
import Decenza

// Small spaced capitals naming a comparison section.
Text {
    Layout.fillWidth: true
    Layout.topMargin: Theme.spacingMedium
    font.family: Theme.captionFont.family
    font.pixelSize: Theme.captionFont.pixelSize
    font.capitalization: Font.AllUppercase
    font.letterSpacing: Theme.scaled(1)
    color: Theme.textSecondaryColor
    Accessible.role: Accessible.Heading
    Accessible.name: text
}
