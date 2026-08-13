import QtQuick
import Decenza

// The card-wide "tap anywhere to select this thing" target, as used by the bag,
// equipment, recipe and profile-tile cards. Declare it as a child of the card's
// root item and give it an accessibleName plus an onAccessibleClicked handler;
// everything else is the shared part:
//
//   anchors.fill  — the whole card, so no padding or inter-button gap is dead
//                   space (#1798: a tap area scoped to the info row alone reads
//                   as "selection only works on the text").
//   z: -1         — the buttons and any other handler inside the card render at
//                   the default z and are hit-tested first, so they still win
//                   their own taps. The card's root Rectangle accepts no mouse
//                   buttons, so it is never a pointer target and cannot swallow
//                   the tap on its way here.
//
// Both are easy to leave out and neither fails loudly: without the fill the
// card has dead zones, without the z the card swallows its own buttons. Hence
// one component rather than a fifth hand-copy.
AccessibleMouseArea {
    anchors.fill: parent
    z: -1
}
