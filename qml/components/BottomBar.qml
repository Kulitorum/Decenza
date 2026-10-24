import QtQuick
import Decenza

// Native theme/accessibility adapter for the shared bottom navigation bar.
// Navigation remains owned by each page's existing backClicked handler.
SharedBottomBar {
    barStyle: NativeBottomBarStyle {}
    interaction: NativeButtonInteraction {}
    resolvedContentColor: Theme.contentColorOn(barColor, Theme.iconColor)
    backAccessibleName: TranslationManager.translate(
        "bottombar.button.back.accessible",
        "Back. Return to previous screen")
}
