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
// style's `background` only ever fed the style's own background Rectangle, and all 131 sites
// already replace `background:` outright — none of it reached the screen. Same for
// `footer: DialogButtonBox`: no DecenzaDialog-rooted file sets `standardButtons`, so it never
// rendered. (`SettingsConnectionsTab.qml` does set it, on a `ComboBox.popup: Dialog` that is
// deliberately NOT migrated — so the scope of that statement is this base's users, not the
// tree. An earlier version of this comment said "no file in the tree", which is false.)
//
// The `header` IS carried, below — see the note there. Getting that wrong is the one defect
// this migration actually shipped into review.
//
// One base covers both shapes: 26 of the 27 file roots were `Dialog {}` and the last was
// `Popup {}`, and T.Dialog IS a QQuickPopup in C++, so a popup loses nothing by rooting here.
// Dialog-specific API is in use at exactly two sites — `onRejected` in BrewDialog.qml and
// SettingsScreensaverTab.qml — and T.Dialog has it.
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

    // THE TITLE. Material's Dialog.qml renders `title` through a header Label and nothing
    // else (qquickdialog.cpp:42: "Dialog's title is displayed by a style-specific title bar
    // that is assigned to header"). T.Dialog supplies no header at all, so omitting this
    // made three dialogs that set `title:` — SteamPage's steamWarningDialog,
    // ConversationOverlay's unsupportedBeverageDialog and ProfileImportPage's
    // duplicateDialog — open with no visible heading. Body and buttons still drew, so
    // nothing looked broken; the heading was simply gone.
    //
    // It is declared HERE rather than patched into those three because Material attached its
    // header to all 131 sites, gated on `title` being non-empty. Reproducing that gate
    // reproduces the previous behaviour everywhere at once: dialogs with no title get
    // nothing (as before), and the files that declare their own `header:` override this one
    // exactly as they overrode Material's.
    //
    // The visible condition is Material's own — a dialog reparented away from the overlay
    // (an inline/embedded one) does not draw a title bar. Styling is Theme's rather than
    // Material's dialogColor, since every dialog here paints its own background.
    //
    // VERIFIED RENDERING, by temporarily setting a `title:` on an untitled dialog that uses
    // this header (SteamPage's editPitcherPopup) and opening it: the heading drew above the
    // content, correctly padded, no overlap. That check was worth its two rebuilds — the
    // app log alone could not distinguish "the binding evaluates" from "the binding is
    // TRUE", because an untitled dialog short-circuits before the overlay comparison
    // decides anything. All three real cases are behind conditions too awkward to trigger
    // on demand, and one of them sits behind a button that would start a paid AI request.
    header: Text {
        text: control.title
        visible: parent?.parent === T.Overlay.overlay && control.title !== ""
        elide: Text.ElideRight
        color: Theme.textColor
        font.family: Theme.subtitleFont.family
        font.pixelSize: Theme.scaled(20)
        font.weight: Font.Medium
        padding: Theme.scaled(24)
        bottomPadding: 0
        Accessible.ignored: true   // T.Dialog announces `title` itself
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
