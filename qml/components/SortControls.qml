import QtQuick
import QtQuick.Layouts
import Decenza

// Sort field picker and direction toggle, shared by Recipes, Beans and Shot
// History. The page owns field/direction and persists them on sortChanged.
RowLayout {
    id: root

    property var keys: []
    property var labels: ({})
    // Direction a field starts in when picked: recency newest-first, text A→Z.
    property var defaultDirections: ({})
    property string field: ""
    property string direction: "DESC"

    // The user picked a field (with its default direction) or flipped the direction.
    signal sortChanged(string field, string direction)

    spacing: Theme.spacingSmall

    AccessibleButton {
        text: root.labels[root.field] || ""
        accessibleName: TranslationManager.translate("shothistory.sortBy", "Sort by %1").arg(root.labels[root.field] || "")
        onClicked: picker.open()
    }

    AccessibleButton {
        icon.source: root.direction === "DESC" ? "qrc:/icons/SortDescending.svg" : "qrc:/icons/SortAscending.svg"
        tintIcon: true
        accessibleName: root.direction === "DESC"
            ? TranslationManager.translate("shothistory.sortDescending", "Sort descending, tap to sort ascending")
            : TranslationManager.translate("shothistory.sortAscending", "Sort ascending, tap to sort descending")
        onClicked: root.sortChanged(root.field, root.direction === "DESC" ? "ASC" : "DESC")
    }

    SelectionDialog {
        id: picker
        title: TranslationManager.translate("shothistory.sortByTitle", "Sort By")
        options: root.keys.map(function(key) { return root.labels[key] || key })
        currentIndex: root.keys.indexOf(root.field)
        onSelected: function(index, value) {
            const picked = root.keys[index]
            root.sortChanged(picked, root.defaultDirections[picked] || "DESC")
        }
    }
}
