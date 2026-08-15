pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// "Your changes from <bundled profile>" — the block at the top of the profile
// knowledge dialog (change: summarize-profile-changes-from-builtin).
//
// The knowledge prose describes the BUNDLED profile: its temperature, its
// yield, its pressure targets. A user reading it against a re-tuned copy cannot
// otherwise tell which of those numbers still describe what they are about to
// brew. This says where the copy departs.
//
// Fed by ProfileManager.profileDialInDiff / profileDialInDiffForJson, which
// return raw values and a stable `kind`; every label, unit and number format is
// built here, because that is where TranslationManager and the user's
// temperature unit live.
Column {
    id: root

    // The map either invokable returned. Three outcomes, and they are NOT the
    // same thing: no base (render nothing), a base with no rows (say the copy is
    // unchanged, because that means the knowledge applies unqualified), a base
    // with rows (list them).
    property var diff: ({})

    readonly property bool hasBase: !!diff && diff.hasBase === true
    readonly property bool unchanged: hasBase && diff.unchanged === true
    readonly property string baseTitle: hasBase ? (diff.baseTitle || "") : ""
    readonly property var rows: hasBase ? (diff.rows || []) : []

    visible: hasBase
    spacing: Theme.spacingSmall

    // Rows are label + "old → new". The arrow is a plain font glyph, covered by
    // the bundled Noto Sans Math fallback — not an emoji, so it takes the text
    // colour like everything around it.
    function labelFor(row) {
        var byKind = {
            "targetWeight":      TranslationManager.translate("profilediff.field.targetWeight", "Yield"),
            "targetVolume":      TranslationManager.translate("profilediff.field.targetVolume", "Target volume"),
            "maximumPressure":   TranslationManager.translate("profilediff.field.maximumPressure", "Pressure limit"),
            "maximumFlow":       TranslationManager.translate("profilediff.field.maximumFlow", "Flow limit"),
            "temperature":       TranslationManager.translate("profilediff.field.temperature", "Temperature"),
            "pressure":          TranslationManager.translate("profilediff.field.pressure", "Pressure"),
            "flow":              TranslationManager.translate("profilediff.field.flow", "Flow"),
            "volume":            TranslationManager.translate("profilediff.field.volume", "Volume cap"),
            "exitPressureOver":  TranslationManager.translate("profilediff.field.exitPressureOver", "Exit above pressure"),
            "exitPressureUnder": TranslationManager.translate("profilediff.field.exitPressureUnder", "Exit below pressure"),
            "exitFlowOver":      TranslationManager.translate("profilediff.field.exitFlowOver", "Exit above flow"),
            "exitFlowUnder":     TranslationManager.translate("profilediff.field.exitFlowUnder", "Exit below flow"),
            "exitWeight":        TranslationManager.translate("profilediff.field.exitWeight", "Exit at weight"),
            "maxFlowOrPressure": TranslationManager.translate("profilediff.field.limiter", "Limiter"),
            "name":              TranslationManager.translate("profilediff.field.name", "Step name")
        }
        // An unmapped kind is a C++ field that reached the block without a label
        // here. Showing the raw identifier is ugly but truthful; showing nothing
        // would hide a real difference.
        var base = byKind[row.kind] !== undefined ? byKind[row.kind] : row.kind

        if (row.frameIndex < 0) return base
        var step = row.frameName && row.frameName.length > 0
            ? row.frameName
            : TranslationManager.translate("profilediff.step_number", "Step %1").arg(row.frameIndex + 1)
        return step + " · " + base
    }

    function formatNumber(row, value) {
        if (row.unit === "celsius")
            return Theme.cToDisplay(value).toFixed(1) + Theme.tempUnitSuffix()
        var suffix = ""
        if (row.unit === "bar") suffix = " " + TranslationManager.translate("common.unit.bar", "bar")
        else if (row.unit === "mlPerSec") suffix = " " + TranslationManager.translate("common.unit.mlPerSec", "mL/s")
        else if (row.unit === "g") suffix = " " + TranslationManager.translate("common.unit.gram", "g")
        else if (row.unit === "ml") suffix = " " + TranslationManager.translate("common.unit.ml", "mL")
        // One decimal, then drop a trailing ".0" — dial-in values are authored
        // at one decimal and "9 bar" reads better than "9.0 bar".
        var text = value.toFixed(1).replace(/\.0$/, "")
        return text + suffix
    }

    function changeText(row) {
        if (!row.numeric)
            return (row.oldText || "—") + " → " + (row.newText || "—")
        return formatNumber(row, row.oldValue) + " → " + formatNumber(row, row.newValue)
    }

    // Whole-block accessible summary. Individual rows are static text a screen
    // reader walks; the heading tells the user what they are walking into.
    readonly property string accessibleSummary: {
        if (!root.hasBase) return ""
        if (root.unchanged)
            return TranslationManager.translate("profilediff.unchanged",
                       "Unchanged copy of %1").arg(root.baseTitle)
        var parts = []
        for (var i = 0; i < root.rows.length; i++)
            parts.push(root.labelFor(root.rows[i]) + " " + root.changeText(root.rows[i]))
        return TranslationManager.translate("profilediff.heading",
                   "Your changes from %1").arg(root.baseTitle) + ": " + parts.join(", ")
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: TranslationManager.translate("profilediff.heading",
                         "Your changes from %1").arg(root.baseTitle)
    Accessible.description: root.accessibleSummary

    Rectangle {
        width: root.width
        height: blockContent.implicitHeight + Theme.spacingMedium * 2
        color: Theme.backgroundColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1

        Column {
            id: blockContent
            x: Theme.spacingMedium
            y: Theme.spacingMedium
            width: parent.width - Theme.spacingMedium * 2
            spacing: Theme.spacingSmall

            Text {
                width: parent.width
                text: root.unchanged
                    ? TranslationManager.translate("profilediff.unchanged",
                          "Unchanged copy of %1").arg(root.baseTitle)
                    : TranslationManager.translate("profilediff.heading",
                          "Your changes from %1").arg(root.baseTitle)
                font: Theme.subtitleFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            Repeater {
                model: root.rows

                delegate: Item {
                    id: diffRow

                    // `pragma ComponentBehavior: Bound` above is what lets this
                    // delegate reach `root` and `blockContent`, the outer ids —
                    // and it stops model roles arriving as context properties, so
                    // every role the delegate reads must be declared required in
                    // the same edit (QML_GOTCHAS.md). `modelData` is the only one
                    // read here; `index` is deliberately not declared because
                    // nothing uses it.
                    required property var modelData

                    width: blockContent.width
                    implicitHeight: Math.max(fieldLabel.implicitHeight, changeValue.implicitHeight)

                    Text {
                        id: fieldLabel
                        anchors.left: parent.left
                        anchors.right: changeValue.left
                        anchors.rightMargin: Theme.spacingSmall
                        text: root.labelFor(diffRow.modelData)
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }

                    Text {
                        id: changeValue
                        anchors.right: parent.right
                        text: root.changeText(diffRow.modelData)
                        font: Theme.captionFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
}
