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
    property int itemIndex: -1

    // The focusable child inside this delegate. The delegate root is a plain layout wrapper
    // and never takes focus itself, so keyboard navigation between delegates has to be told
    // which child to focus.
    property Item focusTarget: null
}
