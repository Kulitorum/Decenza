pragma Singleton
import QtQuick
import Decenza

// Display names for the puck-prep flags a shot or package stores as a canonical
// "wdt,shaker" string, in display order. The names and their order come from C++
// PuckPrep::flagLabels(), the same table the equipment dialog and the shot
// comparison read.
QtObject {
    function labelsFor(canonical) {
        const flags = (canonical || "").split(",")
        const table = MainController.equipmentStorage ? MainController.equipmentStorage.puckPrepFlags : []
        let labels = []
        for (const f of table)
            if (flags.indexOf(f.key) >= 0) labels.push(TranslationManager.translate(f.labelKey, f.label))
        return labels
    }
}
