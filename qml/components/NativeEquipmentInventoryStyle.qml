import QtQuick
import Decenza

EquipmentInventoryStyle {
    titleFont: Theme.titleFont
    subtitleFont: Theme.subtitleFont
    bodyFont: Theme.bodyFont
    textColor: Theme.textColor
    textSecondaryColor: Theme.textSecondaryColor
    pageTopMargin: Theme.pageTopMargin
    pageBottomMargin: Theme.bottomBarHeight
    pageHorizontalMargin: Theme.standardMargin
    contentBottomPadding: Theme.scaled(20)
    contentSpacing: Theme.spacingMedium
    headerSpacing: Theme.spacingMedium
    emptyTopMargin: Theme.scaled(40)
    emptySpacing: Theme.spacingSmall
    cardSpacing: Theme.spacingMedium
    minimumColumnWidth: Theme.scaled(380)
}
