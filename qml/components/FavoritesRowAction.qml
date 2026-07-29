import QtQuick

// Base type for a FavoritesListView trailing-action delegate.
//
// The contract lives on the delegate that CONSUMES it, not on the slot that supplies it —
// same shape as LayoutWidgetItem. FavoritesListView loads the delegate through a Loader and
// binds these three in `onLoaded` behind a checked cast, so a delegate rooted at the wrong
// type produces a named warning at load instead of a silently inert button.
//
// The alternative — putting the properties on the Loader and having the delegate climb to
// them through `parent` — types just as well but costs the delegate a `parent as …` alias and
// a guard at every read, and leaks the whole Loader API (`source`, `active`, `sourceComponent`)
// into the delegate's reach.
//
// Root your delegate at this type and nest whatever control you want inside it; QML has no
// multiple inheritance, so a delegate cannot be both this and a Button.
Item {
    // The row's backing data. Always a QVariantMap JS object, never a QObject — see the
    // note on FavoritesListView.displayTextFn. Null until the row is populated.
    property var row: null

    // Live position of the row, which changes as rows are dragged.
    property int rowIndex: -1

    // Whether this row is the selected one.
    property bool selected: false
}
