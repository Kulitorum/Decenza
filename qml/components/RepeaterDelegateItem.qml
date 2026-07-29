import QtQuick

// Root type for a Repeater/DelegateModel delegate that code OUTSIDE the delegate has to
// reach into — a sibling's key handler focusing the next pill, a DropArea asking where the
// dragged item came from.
//
// Both routes hand back an untyped object: `Repeater.itemAt(i)` is a QQuickItem and a
// DropArea's `drag.source` is a QObject, so `itemAt(i).focusTarget` and
// `drag.source.itemIndex` were unchecked member reads on an anonymous inline delegate —
// they compile, and throw only when the key is pressed or the drag crosses a row. Naming
// the delegate's root type makes both a checked `as RepeaterDelegateItem` cast.
Item {
    // The delegate's LIVE position — after any drag reorder, not the original model row.
    // For a DelegateModel delegate that is `DelegateModel.itemsIndex`; for a plain Repeater
    // delegate it is the required `index`.
    //
    // Required rather than defaulted: this feeds `items.move()` and selected-preset settings,
    // where a -1 would read as a plausible "nothing selected" rather than as a wiring bug.
    // Every client instantiates this type declaratively, so an unbound one fails at load.
    required property int itemIndex

    // The focusable child inside this delegate. The delegate root is a plain layout wrapper
    // and never takes focus itself, so keyboard navigation between delegates has to be told
    // which child to focus. Null is legitimate — the three reorder-only delegates
    // (FavoritesListView, LayoutEditorZone, ScreensaverEditorPopup) have no focusable child,
    // which is why callers go through a guarded helper rather than dereferencing directly.
    property Item focusTarget: null
}
