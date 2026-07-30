import QtQuick
import QtQuick.Templates as T
import Decenza

// The base every dialog and popup in the app roots at.
//
// WHY IT EXISTS — AOT. A `Dialog {}` from QtQuick.Controls resolves to the active style's
// Dialog.qml, a composite whose base chain qmlcachegen cannot walk at build time, so every
// `root.<prop>` in a dialog and every property set on a dialog instance elsewhere lost AOT
// compilation. Same defect #1715 fixed for pages and #1717 fixed for the button family;
// dialogs were the largest class left.
//
// WHY A SHARED BASE RATHER THAN 27 RE-ROOTINGS. Unlike the buttons, the style's Dialog.qml
// contributed things that were genuinely ON SCREEN and that no dialog in this app overrode:
// the grow-fade enter/exit transitions, and the modal dim behind the dialog. Re-rooting each
// file at T.Dialog directly would have meant 27 copies of both — the drift opportunity
// CLAUDE.md's "centralize anything produced at more than one site" rule exists to prevent.
// Declared once here, they cannot disagree.
//
// WHAT IT DELIBERATELY DOES NOT CARRY. `Material.elevation`, `Material.roundedScale` and the
// style's `background` only ever fed the style's own background Rectangle, and all 27 files
// already replace `background:` outright — none of it reached the screen. Likewise the
// `header: Label` and `footer: DialogButtonBox`: the four files that show a header or footer
// declare their own, and no file in the tree uses `standardButtons`.
//
// One base covers both shapes: 26 of the 27 were `Dialog {}` and the last was `Popup {}`, and
// T.Dialog IS a QQuickPopup in C++, so a popup loses nothing by rooting here. The only
// Dialog-specific API in use anywhere is BrewDialog's `onRejected`, which T.Dialog has.
T.Dialog {
    id: control

    // From qtdeclarative/src/quickcontrols/material/Dialog.qml. The implicit-size formulas
    // live only in style QML — QQuickControl computes the inputs in C++ but leaves the
    // result to the style (qquickcontrol.cpp:1749-1757) — so without them a dialog that
    // does not set an explicit width is zero-sized. Most of ours do set one; the ones that
    // don't relied entirely on this.
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    // Also Material's. Roughly half the dialogs set `padding: 0` and override this; the
    // other half never set padding at all and have been laid out against these numbers
    // since they were written, so dropping them would reflow every one of those.
    padding: 24
    topPadding: 16

    modal: true

    // grow_fade_in / shrink_fade_out, copied exactly from Material's Dialog.qml. Not one
    // dialog in the app declares `enter`/`exit`, so all 27 have always animated with these
    // — losing them would make every dialog in the app pop in and out instantly.
    // VERIFIED RUNNING, not just assigned. Temporary instrumentation on this Transition
    // (`onRunningChanged` logging, since removed) measured the exit at 220-224 ms across
    // repeated opens of GrindPickerDialog — an inline site, i.e. one of the 104 rewritten
    // mechanically — which is exactly the `duration: 220` below. `Overlay.modal` resolved
    // SET on the same instances. Worth recording because a screenshot cannot tell a
    // running transition from a dialog that pops, and a non-null `enter` does not prove
    // Qt honours it; those are different failures with different fixes.
    enter: Transition {
        NumberAnimation { property: "scale"; from: 0.9; to: 1.0; easing.type: Easing.OutQuint; duration: 220 }
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; easing.type: Easing.OutCubic; duration: 150 }
    }

    exit: Transition {
        NumberAnimation { property: "scale"; from: 1.0; to: 0.9; easing.type: Easing.OutQuint; duration: 220 }
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; easing.type: Easing.OutCubic; duration: 150 }
    }

    // The dimmer behind the dialog. A popup's own attached Overlay.modal wins over the
    // window-wide default (qquickpopup.cpp:1274-1279), so declaring it here covers every
    // dialog that roots at this file, and a dialog that wants something else can still
    // override it. ProfilePreviewPopup already does.
    T.Overlay.modal: Rectangle {
        color: Theme.dialogDimColor
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    T.Overlay.modeless: Rectangle {
        color: Theme.dialogDimColor
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
}
