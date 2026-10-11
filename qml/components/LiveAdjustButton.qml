import QtQuick
import QtQuick.Layouts
import Decenza

// The live-adjust buttons on operation pages (espresso -5 g/+5 g, flush -5 s/+5 s).
// Sized through Layout attached properties, so place it inside a Layout.
AccessibleButton {
    Layout.minimumWidth: implicitWidth
    Layout.preferredWidth: Theme.scaled(72)
    Layout.preferredHeight: Theme.touchTargetMedium
    Layout.alignment: Qt.AlignVCenter
    // The blue accent is an opaque slab on the glass chrome; there it takes the
    // neutral bordered fill instead.
    primary: !Theme.glassChrome
    topInset: 0
    bottomInset: 0
    leftPadding: Theme.scaled(12)
    rightPadding: Theme.scaled(12)
    verticalPadding: Theme.spacingSmall
    _customFontWeight: Font.Medium
    activeFocusOnTab: true
}
