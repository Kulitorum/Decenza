// Every delegate below declares the roles it reads as required properties, so Bound
// cannot break role injection, and it lets them reach `root` statically.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// The comparison against the base shot: shot headers, a summary sentence per
// shot, what the user changed, and what the shot did. Everything is read from
// ShotComparisonModel.comparison (ShotComparison::compare); this file only lays
// it out and words it.
ColumnLayout {
    id: root

    required property var graph
    required property var comparisonModel

    readonly property var cmp: comparisonModel.comparison || ({})
    readonly property var shots: cmp.shots || []
    readonly property var comparisons: cmp.comparisons || []
    readonly property int columnCount: shots.length
    property bool showMore: false

    readonly property real labelColW: Theme.scaled(118)
    readonly property real cellW: columnCount > 0
        ? (width - labelColW - Theme.spacingSmall * columnCount) / columnCount : 0

    spacing: Theme.spacingSmall

    readonly property font numberFont: Theme.bodyFont

    function shotInfo(i) { return comparisonModel.getShotInfo(i) }

    // ── Wording ─────────────────────────────────────────────────────────────
    // ShotComparisonText: the same table the web page reads, translated here.
    readonly property var texts: comparisonModel.texts || ({})
    function txt(id, fallback) {
        const e = texts[id]
        return e ? TranslationManager.translate(e.key, e.label) : (fallback !== undefined ? fallback : id)
    }
    function inputLabel(key) { return txt("input." + key, key) }
    function metricLabel(key) { return txt("metric." + key, key) }
    function badgeLabel(key) { return txt("badge." + key, key) }
    function stopText(by) { return by ? txt("stop." + by, "\u2014") : "\u2014" }
    function puckLabels(canonical) { return PuckPrepLabels.labelsFor(canonical) }

    function inputText(row, cell) {
        const hasValue = cell.value !== null && cell.value !== undefined
        // No override means the profile's own temperature, not a missing value.
        if (row.key === "temperatureOverrideC" && !hasValue)
            return txt("input.profileTemp")
        if (row.key === "puckPrep")
            return puckLabels(cell.text).join(" \u00b7 ") || "\u2014"
        if (!hasValue || row.unit === "")
            return cell.text || "\u2014"
        switch (row.unit) {
        case "g":       return cell.value.toFixed(1) + " " + TranslationManager.translate("common.unit.grams", "g")
        case "rpm":     return Math.round(cell.value) + " " + txt("unit.rpm")
        case "celsius": return Theme.formatTemperature(cell.value, 1)
        }
        return cell.text || String(cell.value)
    }

    function inputDelta(row, delta) {
        if (delta === null || delta === undefined) return ""
        const d = row.unit === "rpm" ? Math.round(delta) : row.unit === "celsius"
            ? Theme.cDeltaToDisplay(delta) : delta
        const decimals = row.unit === "rpm" ? 0 : row.key === "grinderSetting" ? 2 : 1
        // Trim zeros only after a decimal point: "+0.50" reads "+0.5", "+100" stays.
        const t = signed(d, decimals)
        return t.indexOf(".") >= 0 ? t.replace(/0+$/, "").replace(/\.$/, "") : t
    }

    function unitLabel(unit) {
        switch (unit) {
        case "s":            return TranslationManager.translate("common.unit.seconds", "s")
        case "g":            return TranslationManager.translate("common.unit.grams", "g")
        case "bar":          return TranslationManager.translate("espresso.unit.bar", "bar")
        case "mlPerSec":     return TranslationManager.translate("espresso.unit.flowRate", "mL/s")
        case "gPerSec":      return txt("unit.gPerSec")
        case "celsius":
        case "celsiusDelta": return Theme.tempUnitSuffix()
        case "percent":      return "%"
        }
        return ""
    }

    function displayValue(row, v) {
        if (row.unit === "celsius") return Theme.cToDisplay(v)
        if (row.unit === "celsiusDelta") return Theme.cDeltaToDisplay(v)
        return v
    }

    function metricText(row, v) {
        if (v === null || v === undefined) return "—"
        const s = displayValue(row, v).toFixed(row.decimals)
        return row.key === "ratio" ? "1:" + s : s
    }

    function signed(v, decimals) {
        return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(decimals)
    }

    function metricDelta(row, d) {
        if (d === null || d === undefined) return ""
        const shown = row.unit === "celsius" || row.unit === "celsiusDelta" ? Theme.cDeltaToDisplay(d) : d
        return signed(shown, row.decimals)
    }

    function ratingText(s) {
        let parts = []
        if (s.rating0to100 !== null && s.rating0to100 !== undefined) parts.push(s.rating0to100 + "%")
        if (s.tasteBalance) parts.push(txt("taste." + s.tasteBalance, s.tasteBalance))
        if (s.tasteBody) parts.push(txt("taste." + s.tasteBody, s.tasteBody))
        return parts.length > 0 ? parts.join(" · ") : "—"
    }

    // One compared shot's summary, from the facts C++ chose and ordered; this only
    // words them and formats the numbers.
    function summaryFor(c) {
        let out = []
        for (const f of (c.summary || [])) {
            switch (f.kind) {
            case "sameSetup":
                out.push(txt("phrase.sameSetup"))
                break
            case "noNotable":
                out.push(txt("phrase.noNotable"))
                break
            case "input":
                out.push(inputLabel(f.key) + " " + _plain(f.key, f.from) + " \u2192 " + _plain(f.key, f.to))
                break
            case "inputChanged":
                out.push(txt("phrase.changed").arg(inputLabel(f.key)))
                break
            case "moreInputs":
                out.push(txt("phrase.moreInputs").arg(f.count))
                break
            case "metric": {
                const amount = metricDelta(f, Math.abs(f.delta)).replace(/^\+/, "")
                    + (unitLabel(f.unit) ? " " + unitLabel(f.unit) : "")
                out.push(txt(f.phrase).arg(metricLabel(f.key)).arg(amount))
                break
            }
            case "stopped":
                out.push(txt("stopped." + f.stoppedBy, ""))
                break
            case "badgeAppeared":
                out.push(txt("phrase.badgeAppeared").arg(badgeLabel(f.badge)))
                break
            case "badgeGone":
                out.push(txt("phrase.badgeGone").arg(badgeLabel(f.badge)))
                break
            }
        }
        out = out.filter(function(t) { return t.length > 0 })
        return out.length > 0 ? out.join("  \u00b7  ") : txt("phrase.noNotable")
    }

    function _plain(key, v) {
        if (typeof v === "number")
            return key === "rpm" ? String(Math.round(v)) : key === "temperatureOverrideC"
                ? Theme.formatTemperature(v, 1) : v.toFixed(1)
        return String(v)
    }

    readonly property var visibleMetrics: {
        const rows = cmp.metrics || []
        let out = []
        for (let i = 0; i < rows.length; i++) if (!rows[i].more || showMore) out.push(rows[i])
        return out
    }
    readonly property int hiddenMetricCount: {
        const rows = cmp.metrics || []
        let n = 0
        for (let i = 0; i < rows.length; i++) if (rows[i].more) n++
        return n
    }
    readonly property bool stopDiffers: {
        for (let i = 1; i < shots.length; i++) if (shots[i].stoppedBy !== shots[0].stoppedBy) return true
        return false
    }
    readonly property var differingBadges: {
        let seen = {}, count = {}
        for (let i = 0; i < shots.length; i++)
            for (const b of (shots[i].badges || [])) { seen[b] = true; count[b] = (count[b] || 0) + 1 }
        let out = []
        for (const b in seen) if (count[b] < shots.length) out.push(b)
        return out
    }
    readonly property bool anyRated: {
        for (let i = 0; i < shots.length; i++)
            if (shots[i].rating0to100 !== null && shots[i].rating0to100 !== undefined
                    || shots[i].tasteBalance || shots[i].tasteBody) return true
        return false
    }
    // Inputs whose value names itself in the "same for all" line.
    readonly property var selfDescribing: ["profile", "grinder", "burrs", "basket", "bean", "roast"]

    readonly property string unchangedText: {
        const items = cmp.unchanged || []
        let parts = []
        for (let i = 0; i < items.length; i++) {
            const it = items[i]
            let v = it.text
            if (it.value !== null && it.value !== undefined && it.unit !== "")
                v = inputText(it, it)
            if (it.key === "profile") {
                let sameVersion = comparisons.length > 0
                for (const c of comparisons) sameVersion = sameVersion && c.profile && c.profile.sameVersion === true
                if (sameVersion) v += " " + root.txt("ui.sameVersion")
            } else if (it.key === "puckPrep") {
                // Commas, not the line's own dots, so the prep reads as one item.
                v = root.txt("ui.prep")
                    .arg(puckLabels(it.text).join(", "))
            } else if (selfDescribing.indexOf(it.key) < 0 && it.unit !== "rpm") {
                // A bare date or number says nothing on its own; name it.
                v = inputLabel(it.key) + " " + v
            }
            parts.push(v)
        }
        return parts.join(" · ")
    }

    // ── Shot headers ────────────────────────────────────────────────────────
    ListView {
        id: headerList
        Layout.fillWidth: true
        Layout.leftMargin: root.labelColW + Theme.spacingSmall
        Layout.preferredHeight: Theme.scaled(38)
        orientation: ListView.Horizontal
        interactive: false
        spacing: Theme.spacingSmall
        model: root.shots
        // A re-base rebuilds the row; easing it in marks the new order.
        populate: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
        }

        delegate: Rectangle {
            id: headerCard
            required property var modelData
            required property int index
            readonly property bool isBase: modelData.isBase === true
            readonly property var info: root.shotInfo(index)
            readonly property bool shownOnGraph: root.graph.shotVisible(index)

            width: root.cellW
            height: headerList.height
            radius: height / 2
            color: Theme.surfaceColor
            border.color: isBase ? Theme.primaryColor : Theme.borderColor
            border.width: isBase ? Theme.scaled(2) : 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(10)
                anchors.rightMargin: Theme.scaled(4)
                spacing: Theme.scaled(8)

                ComparisonLineSwatch {
                    shotIndex: headerCard.index
                    heavy: headerCard.isBase
                }
                Text {
                    Layout.fillWidth: true
                    text: headerCard.info.dateTime || ""
                    font: Theme.labelFont
                    color: Theme.textColor
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Rectangle {
                    visible: headerCard.isBase
                    Layout.preferredHeight: baseTag.implicitHeight + Theme.scaled(2)
                    Layout.preferredWidth: baseTag.implicitWidth + Theme.scaled(12)
                    radius: height / 2
                    color: Qt.alpha(Theme.primaryColor, 0.18)
                    Text {
                        id: baseTag
                        anchors.centerIn: parent
                        text: root.txt("ui.base")
                        font: Theme.captionFont
                        color: Theme.primaryColor
                        Accessible.ignored: true
                    }
                }

                // Graph visibility; its own target, so it never re-bases.
                Item {
                    Layout.preferredWidth: Theme.scaled(30)
                    Layout.fillHeight: true
                    ThemedIcon {
                        anchors.centerIn: parent
                        source: headerCard.shownOnGraph ? "qrc:/icons/eye.svg" : "qrc:/icons/eye-off.svg"
                        iconSize: Theme.scaled(16)
                        color: headerCard.shownOnGraph ? Theme.textColor : Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: (headerCard.shownOnGraph
                            ? root.txt("ui.hideOnGraph")
                            : root.txt("ui.showOnGraph"))
                            + ", " + (headerCard.info.dateTime || "")
                        accessibleRole: Accessible.CheckBox
                        accessibleChecked: headerCard.shownOnGraph
                        onAccessibleClicked: root.graph.toggleShot(headerCard.index)
                    }
                }
            }

            AccessibleMouseArea {
                anchors.fill: parent
                anchors.rightMargin: Theme.scaled(34)
                enabled: !headerCard.isBase
                accessibleName: headerCard.isBase
                    ? root.txt("ui.baseShot").arg(headerCard.info.dateTime || "")
                    : root.txt("ui.makeBase").arg(headerCard.info.dateTime || "")
                onAccessibleClicked: root.comparisonModel.setBaseShot(headerCard.modelData.shotId)
            }
        }
    }

    // ── At a glance: one line per compared shot, keyed by its line style ─────
    Repeater {
        model: root.comparisons
        delegate: RowLayout {
            id: summaryLine
            required property var modelData
            required property int index
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(4)
            spacing: Theme.scaled(10)

            ComparisonLineSwatch {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.scaled(6)
                shotIndex: summaryLine.index + 1
            }
            Text {
                Layout.fillWidth: true
                text: root.summaryFor(summaryLine.modelData)
                font: Theme.labelFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: (root.shotInfo(summaryLine.index + 1).dateTime || "") + ": " + text
            }
        }
    }

    // ── What you changed ────────────────────────────────────────────────────
    ComparisonSectionHeader {
        text: root.txt("ui.changed")
    }

    Repeater {
        model: root.cmp.inputs || []
        delegate: ComparisonRow {
            id: inputRow
            required property var modelData
            Layout.fillWidth: true
            labelWidth: root.labelColW
            cellWidth: root.cellW
            label: root.inputLabel(modelData.key)
            cells: modelData.cells
            textFor: function(cell) { return root.inputText(inputRow.modelData, cell) }
            deltaFor: function(cell) { return root.inputDelta(inputRow.modelData, cell.delta) }
            deltaSignFor: function(cell) { return cell.delta === null || cell.delta === undefined ? 0 : Math.sign(cell.delta) }
            mutedFor: function(cell) { return cell.state === "same" }
            numberFont: root.numberFont
        }
    }

    Text {
        // Per comparison, not by input rows: a profile-only retune has no input row
        // and is still a change.
        visible: root.comparisons.length > 0
                 && root.comparisons.every(function(c) { return c.nothingChanged === true })
        Layout.fillWidth: true
        text: root.txt("ui.nothingChanged")
        font: Theme.labelFont
        color: Theme.textSecondaryColor
        wrapMode: Text.WordWrap
    }

    Text {
        visible: root.unchangedText.length > 0
        Layout.fillWidth: true
        text: (root.columnCount > 2
               ? root.txt("ui.sameForAll")
               : root.txt("ui.sameForBoth"))
              + "  ·  " + root.unchangedText
        font: Theme.captionFont
        color: Theme.textSecondaryColor
        wrapMode: Text.WordWrap
        lineHeight: 1.2
    }

    Repeater {
        model: root.comparisons
        delegate: ProfileDialInDiffBlock {
            id: profileBlock
            required property var modelData
            required property int index
            readonly property var diffRows: modelData.profile ? (modelData.profile.rows || []) : []
            visible: diffRows.length > 0
            Layout.fillWidth: true
            headingOverride: root.txt("ui.profileChanged")
                             .arg(root.shotInfo(index + 1).dateTime || "")
            diff: ({ hasBase: true, unchanged: false, baseTitle: "", rows: diffRows })
        }
    }

    // ── What happened ───────────────────────────────────────────────────────
    ComparisonSectionHeader {
        text: root.txt("ui.happened")
    }

    Repeater {
        model: root.visibleMetrics
        delegate: ComparisonRow {
            id: metricRow
            required property var modelData
            Layout.fillWidth: true
            labelWidth: root.labelColW
            cellWidth: root.cellW
            label: root.metricLabel(modelData.key)
            unit: root.unitLabel(modelData.unit)
            cells: modelData.cells
            textFor: function(cell) { return root.metricText(metricRow.modelData, cell.value) }
            deltaFor: function(cell) { return root.metricDelta(metricRow.modelData, cell.delta) }
            deltaSignFor: function(cell) { return cell.delta === null || cell.delta === undefined ? 0 : Math.sign(cell.delta) }
            mutedFor: function(cell) { return false }
            numberFont: root.numberFont
        }
    }

    ComparisonRow {
        visible: root.stopDiffers
        Layout.fillWidth: true
        labelWidth: root.labelColW
        cellWidth: root.cellW
        label: root.txt("row.stopped")
        cells: root.shots
        textFor: function(s) { return root.stopText(s.stoppedBy) }
        numberFont: root.numberFont
    }

    Repeater {
        model: root.differingBadges
        delegate: ComparisonRow {
            id: badgeRow
            required property var modelData
            Layout.fillWidth: true
            labelWidth: root.labelColW
            cellWidth: root.cellW
            label: root.badgeLabel(modelData)
            cells: root.shots
            textFor: function(s) {
                return (s.badges || []).indexOf(badgeRow.modelData) >= 0
                    ? root.txt("ui.yes")
                    : root.txt("ui.no")
            }
            warnFor: function(s) { return (s.badges || []).indexOf(badgeRow.modelData) >= 0 }
            numberFont: root.numberFont
        }
    }

    ComparisonRow {
        id: ratingRow
        visible: root.anyRated
        Layout.fillWidth: true
        labelWidth: root.labelColW
        cellWidth: root.cellW
        label: root.txt("row.rating")
        cells: root.shots
        textFor: function(s) { return root.ratingText(s) }
        // Only between two rated shots; the base is the first cell.
        function ratingDelta(s) {
            const b = root.shots[0]
            if (s === b || s.rating0to100 === null || s.rating0to100 === undefined
                    || b.rating0to100 === null || b.rating0to100 === undefined) return 0
            return s.rating0to100 - b.rating0to100
        }
        deltaFor: function(s) { const d = ratingRow.ratingDelta(s); return d === 0 ? "" : root.signed(d, 0) }
        deltaSignFor: function(s) { return Math.sign(ratingRow.ratingDelta(s)) }
        numberFont: root.numberFont
    }

    AccessibleButton {
        visible: root.hiddenMetricCount > 0
        Layout.alignment: Qt.AlignHCenter
        subtle: true
        text: root.showMore
            ? root.txt("ui.showLess")
            : root.txt("ui.showMore").arg(root.hiddenMetricCount)
        accessibleName: text
        onClicked: root.showMore = !root.showMore
    }

    Repeater {
        model: root.shots
        delegate: Text {
            id: noteText
            required property var modelData
            required property int index
            visible: !!modelData.notes
            Layout.fillWidth: true
            text: (root.shotInfo(index).dateTime || "") + "  “" + (modelData.notes || "") + "”"
            font.family: Theme.bodyFont.family
            font.pixelSize: Theme.bodyFont.pixelSize
            font.italic: true
            color: Theme.textSecondaryColor
            wrapMode: Text.WordWrap
        }
    }
}
