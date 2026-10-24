import QtQuick
import Decenza

SharedBottomBarStyle {
    scaleFactor: Theme.scale
    barHeight: Theme.bottomBarHeight
    chartMarginSmall: Theme.chartMarginSmall
    spacingLarge: Theme.spacingLarge
    spacingMedium: Theme.spacingMedium
    titleFontSize: Theme.scaled(20)
    backIconSize: Theme.scaled(28)
    focusRadius: Theme.scaled(4)
    bodyFont: Theme.bodyFont
    defaultBarColor: Theme.bottomBarColor
    iconColor: Theme.iconColor
    glassBarColor: Theme.chromeFill(Theme.surfaceColor)
    borderColor: Theme.borderColor
    focusMargin: Theme.focusMargin
    focusBorderWidth: Theme.focusBorderWidth
    focusColor: Theme.focusColor
    glassChrome: Theme.glassChrome
}
