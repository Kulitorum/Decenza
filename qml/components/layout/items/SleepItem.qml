import QtQuick
import Decenza

// Sleep was moved to the top status bar (dead-centre) so it no longer shares
// the bottom-left corner with the Back button — accidental sleeps during screen
// transitions were the result. This widget now renders nothing, so any saved
// layout that still places "sleep" in a zone shows no (duplicate) button.
Item {
    property bool isCompact: false
    property string itemId: ""
    implicitWidth: 0
    implicitHeight: 0
    visible: false
}
