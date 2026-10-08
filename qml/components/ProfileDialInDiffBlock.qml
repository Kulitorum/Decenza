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
// return raw values, a stable `kind` and the decimal count to show them at; the
// label is translated and the unit applied here, because that is where
// TranslationManager and the user's temperature unit live.
Column {
    id: root

    // The map either invokable returned. Three outcomes, and they are NOT the
    // same thing: no base (render nothing), a base with no rows (say the copy is
    // unchanged, because that means the knowledge applies unqualified), a base
    // with rows (list them).
    property var diff: ({})
    // Replaces the "Your changes from X" wording for a caller whose base is not a
    // bundled profile — the shot comparison diffs one shot's profile against another's.
    property string headingOverride: ""
    readonly property string heading: headingOverride.length > 0 ? headingOverride
        : unchanged ? TranslationManager.translate("profilediff.unchanged", "Unchanged copy of %1").arg(baseTitle)
        : TranslationManager.translate("profilediff.heading", "Your changes from %1").arg(baseTitle)

    readonly property bool hasBase: !!diff && diff.hasBase === true
    readonly property bool unchanged: hasBase && diff.unchanged === true
    readonly property string baseTitle: hasBase ? (diff.baseTitle || "") : ""
    readonly property var rows: hasBase ? (diff.rows || []) : []

    visible: hasBase
    spacing: Theme.spacingSmall

    // Rows are label + "old → new". The arrow is a plain font glyph, covered by
    // the bundled Noto Sans Math fallback — not an emoji, so it takes the text
    // colour like everything around it.
    // Labels come from ProfileManager.dialInLabels, the table the web page reads
    // too. An unmapped kind shows its raw identifier: ugly but truthful, where
    // showing nothing would hide a real difference.
    function lbl(id) {
        const e = ProfileManager.dialInLabels[id]
        return e ? TranslationManager.translate(e.key, e.label) : id
    }

    function labelFor(row) {
        var base = lbl(row.kind)
        if (row.frameIndex < 0) return base
        var step = row.frameName && row.frameName.length > 0
            ? row.frameName : lbl("step").arg(row.frameIndex + 1)
        return step + " · " + base
    }

    function formatNumber(row, value, decimals) {
        // "celsiusTank" is celsius that C++ compares at a looser tolerance
        // (ProfileJson writes the tank target at one decimal, not two). It is
        // a separate token there ONLY so the tolerance can differ; it displays
        // exactly like any other temperature, through the same Theme helper
        // every other temperature in the app goes through.
        if (row.unit === "celsius" || row.unit === "celsiusTank")
            return Theme.formatTemperature(value, decimals)
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
        return value.toFixed(decimals) + suffix
    }

    function changeText(row) {
        if (!row.numeric)
            return (row.oldText || "—") + " → " + (row.newText || "—")
        return formatNumber(row, row.oldValue, row.decimals) + " → " + formatNumber(row, row.newValue, row.decimals)
    }

    // Whole-block accessible summary. Individual rows are static text a screen
    // reader walks; the heading tells the user what they are walking into.
    readonly property string accessibleSummary: {
        if (!root.hasBase) return ""
        if (root.unchanged)
            return TranslationManager.translate("profilediff.unchanged",
                       "Unchanged copy of %1").arg(root.baseTitle)
        var parts = []
        for (let i = 0; i < root.rows.length; i++)
            parts.push(root.labelFor(root.rows[i]) + " " + root.changeText(root.rows[i]))
        return root.heading + ": " + parts.join(", ")
    }

    Accessible.role: Accessible.Grouping
    // The same `heading` the visible text shows, so the two cannot disagree again
    // (it once said "Your changes from X" while the screen said "Unchanged copy of X").
    Accessible.name: root.heading
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
                text: root.heading
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
