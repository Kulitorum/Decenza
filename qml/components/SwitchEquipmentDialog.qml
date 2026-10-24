import QtQuick
import Decenza

// Native service/style adapter. The same SharedEquipmentDialog QML source is
// used by the browser host; this wrapper only binds app services and theme.
SharedEquipmentDialog {
    id: root

    SharedEquipmentUiBackend { id: equipmentBackend }
    NativeEquipmentDialogStyle { id: nativeStyle }
    NativeEquipmentDialogText { id: nativeStrings }

    backend: equipmentBackend
    equipmentStyle: nativeStyle
    strings: nativeStrings
    logName: "SwitchEquipmentDialog"
    onLogShown: function(name) {
        WebDebugLogger.info("App", "Popup", name + " dialog shown")
    }
}
