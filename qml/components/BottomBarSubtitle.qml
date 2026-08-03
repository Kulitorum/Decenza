import QtQuick
import QtQuick.Layouts
import Decenza

// Profile name + date pair that sits in a BottomBar's leftContent slot. It stays
// visible while the user scrolls, providing context once the page header is
// off-screen, and reads as a subtitle to the page title — which is why it lives in
// leftContent (beside the title) rather than in the action slot.
//
// Shared by Shot Review and Shot Detail; both had a verbatim copy of this block.
// Width is capped both by a share of the page and by whatever the bar has left after
// its title and buttons (see BottomBar.leftContentMaxWidth), so a long profile name
// elides instead of pushing the action buttons off the right edge.
ColumnLayout {
    id: root

    // The bar this sits in — supplies the remaining-width budget.
    required property BottomBar bar
    // The page (or any item) whose width sets the proportional cap.
    required property Item page

    property string primaryText: ""
    property string secondaryText: ""

    // Share of the page width this may take when the bar has room to spare.
    property real widthFraction: 0.3

    readonly property real _maxTextWidth:
        Math.min(root.page.width * root.widthFraction, root.bar.leftContentMaxWidth)

    visible: primaryText !== ""
    spacing: 0
    Layout.alignment: Qt.AlignVCenter

    Accessible.role: Accessible.StaticText
    Accessible.name: root.primaryText + (root.secondaryText !== "" ? ", " + root.secondaryText : "")
    // Both halves are required together — Accessible.focusable alone leaves the item
    // unreachable by keyboard Tab. Neither page set activeFocusOnTab before this block
    // was extracted; fixed here rather than carried forward. See ACCESSIBILITY.md.
    Accessible.focusable: true
    activeFocusOnTab: true

    Text {
        text: root.primaryText
        font: Theme.labelFont
        // The bar's own content colour, not Theme.textColor: under a background preset
        // textColor is derived from the PAGE while barColor is a different surface, which
        // is exactly the divergence BottomBar.contentColor exists to absorb. The title one
        // row over already uses it; both pages had this line reading Theme.textColor.
        color: root.bar.contentColor
        elide: Text.ElideRight
        Layout.maximumWidth: root._maxTextWidth
        Accessible.ignored: true
    }

    Text {
        text: root.secondaryText
        font: Theme.captionFont
        // Softened contentColor rather than textSecondaryColor, for the same reason.
        color: Qt.rgba(root.bar.contentColor.r, root.bar.contentColor.g, root.bar.contentColor.b, 0.7)
        elide: Text.ElideRight
        Layout.maximumWidth: root._maxTextWidth
        Accessible.ignored: true
    }
}
