import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

/**
 * RecipeRow - A label + content row for the Recipe Editor
 * Provides consistent layout for parameter inputs
 */
RowLayout {
    id: root

    property string label: ""
    default property alias content: contentItem.children

    Layout.fillWidth: true
    spacing: Theme.scaled(12)

    Text {
        text: root.label
        font: Theme.captionFont
        color: Theme.textSecondaryColor
        Layout.preferredWidth: Theme.scaled(80)
    }

    Item {
        id: contentItem
        Layout.fillWidth: true
        implicitHeight: children.length > 0 ? children[0].implicitHeight : 0
    }
}
