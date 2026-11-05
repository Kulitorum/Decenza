import QtQuick
import QtQuick.Dialogs

// Use this, never a bare FolderDialog: same dangling Android listener as
// DecenzaFileDialog.qml (#2176), which carries the sources.
FolderDialog {
    Component.onDestruction: if (visible) close()
}
