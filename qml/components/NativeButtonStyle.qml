import QtQuick
import Decenza

SharedButtonStyle {
    scaleFactor: Theme.scale
    bodyFont: Theme.bodyFont
    primaryColor: Theme.primaryColor
    primaryContrastColor: Theme.primaryContrastColor
    warningButtonColor: Theme.warningButtonColor
    warningForegroundColor: Theme.contrastColorFor(Theme.warningButtonColor)
    errorColor: Theme.errorColor
    surfaceColor: Theme.surfaceColor
    cardBackgroundColor: Theme.cardBackgroundColor
    textColor: Theme.textColor
    textSecondaryColor: Theme.textSecondaryColor
    borderColor: Theme.borderColor
    focusMargin: Theme.focusMargin
    focusBorderWidth: Theme.focusBorderWidth
    focusColor: Theme.focusColor
    focusPulseColor: Qt.lighter(Theme.primaryColor, 1.3)
}
