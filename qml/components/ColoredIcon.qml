import QtQuick
import QtQuick.Controls

// Display-only tinted SVG icon. Uses Button's internal IconLabel to apply
// icon.color tinting to monochrome SVGs. Not interactive — use inside a
// parent that handles clicks (MouseArea, AccessibleButton, etc.).
//
// Note: We avoid `enabled: false` because Material style dims disabled
// buttons to 38% opacity. Instead we block interaction via focusPolicy
// and an absorbing MouseArea.
Button {
    id: root

    required property url source
    property alias iconWidth: root.icon.width
    property alias iconHeight: root.icon.height
    property alias iconColor: root.icon.color

    flat: true
    padding: 0
    focusPolicy: Qt.NoFocus
    icon.source: root.source
    background: Item {}
    Accessible.ignored: true

    // Absorb clicks so this doesn't act as a button
    MouseArea { anchors.fill: parent }
}
