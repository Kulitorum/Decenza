import QtQuick

// Shared card-wide selection target. Keep it behind action controls while
// filling the full card so the whitespace and padding remain tappable.
SharedAccessibleTapArea {
    anchors.fill: parent
    z: -1
}
