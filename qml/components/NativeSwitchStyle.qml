import QtQuick
import Decenza

SharedSwitchStyle {
    scaleFactor: Theme.scale
    bodyFont: Theme.bodyFont
    primaryColor: Theme.primaryColor
    primaryContrastColor: Theme.primaryContrastColor
    insetBackgroundColor: Theme.insetBackgroundColor
    textColor: Theme.textColor
    textSecondaryColor: Theme.textSecondaryColor
    fallbackToggleLabel: TranslationManager.translate("switch.accessibility.toggle", "Toggle")
}
