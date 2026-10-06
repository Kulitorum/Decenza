import QtQuick
import QtQuick.Layouts
import Decenza

// Sort field picker and direction toggle for a list page. The page owns
// field/direction and persists them on sortChanged.
RowLayout {
    id: root

    property var keys: []
    property var labels: ({})
    // Direction a field starts in when picked: recency newest-first, text A→Z.
    property var defaultDirections: ({})
    property string field: ""
    property string direction: "DESC"

    // A stored value the page doesn't offer (an old backup, an MCP write) shows
    // as the first key, which is also what every page sorts by for it; anything
    // but "ASC" is descending, as RecipeSearch.sortedCopy and Shot History read it.
    readonly property string effectiveField: keys.indexOf(field) >= 0 ? field : (keys[0] || "")
    readonly property string effectiveDirection: direction === "ASC" ? "ASC" : "DESC"

    // The user picked a field (with its default direction) or flipped the direction.
    signal sortChanged(string field, string direction)

    spacing: Theme.spacingSmall

    AccessibleButton {
        text: root.labels[root.effectiveField] || ""
        accessibleName: TranslationManager.translate("common.accessible.sortBy", "Sort by %1").arg(text)
        onClicked: picker.open()
    }

    AccessibleButton {
        icon.source: root.effectiveDirection === "DESC" ? "qrc:/icons/SortDescending.svg" : "qrc:/icons/SortAscending.svg"
        tintIcon: true
        accessibleName: root.effectiveDirection === "DESC"
            ? TranslationManager.translate("common.accessible.sortDescending", "Sort descending, tap to sort ascending")
            : TranslationManager.translate("common.accessible.sortAscending", "Sort ascending, tap to sort descending")
        onClicked: root.sortChanged(root.effectiveField, root.effectiveDirection === "DESC" ? "ASC" : "DESC")
    }

    SelectionDialog {
        id: picker
        title: TranslationManager.translate("common.sortByTitle", "Sort By")
        options: root.keys.map(function(key) { return root.labels[key] || key })
        currentIndex: root.keys.indexOf(root.effectiveField)
        onSelected: function(index, value) {
            const picked = root.keys[index]
            root.sortChanged(picked, root.defaultDirections[picked] || "DESC")
        }
    }
}
