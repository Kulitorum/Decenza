import QtQuick
import Decenza

// Native data adapter for the shared equipment detail presentation. Existing
// callers continue to use openFor(packageId).
EquipmentInfoDialogView {
    id: root

    NativeEquipmentDialogStyle { id: nativeStyle }
    NativeEquipmentDialogText { id: nativeStrings }

    property int packageId: -1
    property var pkg: ({})

    equipmentStyle: nativeStyle
    strings: nativeStrings
    packageData: pkg
    puckPrepRows: EquipmentService.puckPrepFlags.map(function(flag) {
            return { key: flag.key, label: TranslationManager.translate(flag.labelKey, flag.label) }
        })

    function openFor(id) {
        packageId = id
        pkg = ({})
        if (id > 0)
            EquipmentService.requestPackage(id)
        open()
    }

    Connections {
        target: EquipmentService
        function onPackageReady(id, packageData) {
            if (id !== root.packageId) return
            root.pkg = packageData || ({})
        }
    }
}
