import QtQuick
import QtQuick.Dialogs

// Use this, never a bare FileDialog. Android's helper unregisters its
// activity-result listener only in hide() (qandroidplatformfiledialoghelper.cpp:229,
// :235-240), and a dialog destroyed while open deletes it without hiding
// (qquickabstractdialog.cpp:123-126, :499-502). A page popped under the system
// picker then left a dangling listener, and the picker's result crashed (#2176).
// onDestruction runs before the dialog is deleted (qqmlengine.cpp:205-217).
FileDialog {
    Component.onDestruction: if (visible) close()
}
