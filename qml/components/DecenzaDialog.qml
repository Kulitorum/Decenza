import QtQuick
import Decenza

// Native logging adapter around the shared dialog presentation.
SharedDecenzaDialog {
    id: root
    dialogStyle: NativeDialogStyle {}
    onLogShown: function(name) { WebDebugLogger.info("App", "Popup", name + " dialog shown") }
}
