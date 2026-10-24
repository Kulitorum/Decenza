pragma Singleton
import QtQuick
import Decenza

// Display names for the puck-prep flags a shot or package stores as a canonical
// "wdt,shaker" string, in display order. The names and their order come from C++
// PuckPrep::flagLabels(), the same table the equipment dialog and the shot
// comparison read.
QtObject {
    readonly property var rows: {
        const table = EquipmentService.puckPrepFlags
        return table.map(flag => ({
            key: flag.key,
            label: TranslationManager.translate(flag.labelKey, flag.label)
        }))
    }

    readonly property var labelsByKey: {
        let labels = {}
        for (const row of rows)
            labels[row.key] = row.label
        return labels
    }

    function labelsFor(canonical) {
        const flags = (canonical || "").split(",")
        return rows.filter(row => flags.indexOf(row.key) >= 0).map(row => row.label)
    }
}
