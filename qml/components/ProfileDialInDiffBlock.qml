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
            "minimumPressure":   TranslationManager.translate("profilediff.field.minimumPressure", "Minimum pressure"),
            "tankTemperature":   TranslationManager.translate("profilediff.field.tankTemperature", "Tank preheat"),
            "espressoTemperature": TranslationManager.translate("profilediff.field.espressoTemperature", "Brew temperature"),
            "recommendedDose":   TranslationManager.translate("profilediff.field.recommendedDose", "Dose"),
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
        // "celsiusTank" is celsius that C++ compares at a looser tolerance
        // (ProfileJson writes the tank target at one decimal, not two). It is
        // a separate token there ONLY so the tolerance can differ; it displays
        // exactly like any other temperature.
        if (row.unit === "celsius" || row.unit === "celsiusTank")
            return Theme.cToDisplay(value).toFixed(1) + Theme.tempUnitSuffix()
        var suffix = ""
        if (row.unit === "bar") suffix = " " + TranslationManager.translate("espresso.unit.bar", "bar")
        else if (row.unit === "mlPerSec") suffix = " " + TranslationManager.translate("espresso.unit.flowRate", "mL/s")
        else if (row.unit === "g") suffix = " " + TranslationManager.translate("common.unit.grams", "g")
        else if (row.unit === "ml") suffix = " " + TranslationManager.translate("common.unit.ml", "mL")
        // An unmapped token appends the RAW token rather than nothing. A bare
        // unitless number is not "ugly but truthful" the way an unmapped label
        // is — "25 → 30" with no unit is simply wrong for a duration or a mass,
        // and it looks finished, so nobody reports it. C++ can already mint two
        // tokens this does not map ("s", "count"); both are developer-only
        // today, and promoting one to dial-in is a one-word edit.
        else if (row.unit && row.unit.length > 0) suffix = " " + row.unit
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
    // Same ternary as the visible heading below. It used to be unconditionally
    // "Your changes from X" while the screen said "Unchanged copy of X" — the
    // two states the design calls load-bearing, told to a screen-reader user in
    // reverse.
    Accessible.name: root.unchanged
        ? TranslationManager.translate("profilediff.unchanged",
              "Unchanged copy of %1").arg(root.baseTitle)
        : TranslationManager.translate("profilediff.heading",
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

                    // `pragma ComponentBehavior: Bound` binds this delegate to
                    // its DEFINING context, so `root` and `blockContent` resolve
                    // statically rather than through a runtime scope walk. It
                    // does not grant access to outer ids — a delegate can reach
                    // them either way — and reading it that way is the
                    // QML_GOTCHAS.md trap in reverse. What it does change is
                    // that model roles stop arriving as context properties, so
                    // every role read here must be `required`. `modelData` is
                    // the only one read; `index` is deliberately not declared.
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
