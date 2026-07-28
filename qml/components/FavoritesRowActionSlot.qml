import QtQuick

// The trailing-action slot inside a FavoritesListView row.
//
// Exists as a named type purely so the contract it offers its loaded delegate — `row`,
// `rowIndex`, `selected` — is declared rather than tacked onto an anonymous Loader. A
// delegate reads them through `parent`, which is otherwise typed QQuickItem: every access
// was unverifiable, and a renamed property would have failed silently at runtime. With a
// type name the delegate can write `(parent as FavoritesRowActionSlot).row` and be checked.
Loader {
    // The row's backing data. Always a QVariantMap JS object, never a QObject — see the
    // note on FavoritesListView.displayTextFn.
    property var row: null

    // Live position of the row, which changes as rows are dragged.
    property int rowIndex: -1

    // Whether this row is the selected one.
    property bool selected: false
}
