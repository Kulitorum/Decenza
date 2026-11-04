import QtQuick
import QtQuick.Dialogs

// Use this, never a bare FileDialog. Android's helper registers an activity-result
// listener in show() and removes it only in hide() (qandroidplatformfiledialoghelper.cpp:229,
// :235-240); a dialog destroyed while open deletes it without hiding
// (qquickabstractdialog.cpp:123-126, :499-502). A page popped under the system
// picker then left a dangling listener, and the picker's result crashed (#2176).
// onDestruction runs before the dialog is deleted (qqmlengine.cpp:205-217). close()
// emits rejected(), so a consumer's onRejected runs during page teardown.
FileDialog {
    Component.onDestruction: if (visible) close()
}
