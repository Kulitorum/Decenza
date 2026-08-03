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
    Accessible.focusable: true

    Text {
        text: root.primaryText
        font: Theme.labelFont
        color: Theme.textColor
        elide: Text.ElideRight
        Layout.maximumWidth: root._maxTextWidth
        Accessible.ignored: true
    }

    Text {
        text: root.secondaryText
        font: Theme.captionFont
        color: Theme.textSecondaryColor
        elide: Text.ElideRight
        Layout.maximumWidth: root._maxTextWidth
        Accessible.ignored: true
    }
}
