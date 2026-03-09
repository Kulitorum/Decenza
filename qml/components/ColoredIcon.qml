import QtQuick
import QtQuick.Controls

// Display-only tinted SVG icon. Uses Button's internal IconLabel to apply
// icon.color tinting to monochrome SVGs. Not interactive — use inside a
// parent that handles clicks (MouseArea, AccessibleButton, etc.).
Button {
    id: root

    required property url source
    property alias iconWidth: root.icon.width
    property alias iconHeight: root.icon.height
    property alias iconColor: root.icon.color

    flat: true
    padding: 0
    enabled: false
    focusPolicy: Qt.NoFocus
    icon.source: root.source
    background: Item {}
    Accessible.ignored: true
}
