// Every delegate below declares the roles it reads as required properties, so Bound
// cannot break role injection, and it lets them reach `root` statically.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// The comparison against the base shot: shot headers, a summary sentence per
// shot, what the user changed, and what the shot did. Everything is read from
// ShotComparisonModel.comparison (ShotComparison::compare); this file lays it
// out, and ComparisonText words it.
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
                v = ComparisonText.inputText(it, it)
            if (it.key === "profile") {
                let sameVersion = comparisons.length > 0
                for (const c of comparisons) sameVersion = sameVersion && c.profile && c.profile.sameVersion === true
                if (sameVersion) v += " " + ComparisonText.txt("ui.sameVersion")
            } else if (it.key === "puckPrep") {
                // Commas, not the line's own dots, so the prep reads as one item.
                v = ComparisonText.txt("ui.prep")
                    .arg(PuckPrepLabels.labelsFor(it.text).join(", "))
            } else if (selfDescribing.indexOf(it.key) < 0 && it.unit !== "rpm") {
                // A bare date or number says nothing on its own; name it.
                v = ComparisonText.inputLabel(it.key) + " " + v
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
                        text: ComparisonText.txt("ui.base")
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
                            ? ComparisonText.txt("ui.hideOnGraph")
                            : ComparisonText.txt("ui.showOnGraph"))
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
                    ? ComparisonText.txt("ui.baseShot").arg(headerCard.info.dateTime || "")
                    : ComparisonText.txt("ui.makeBase").arg(headerCard.info.dateTime || "")
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
                text: ComparisonText.summaryFor(summaryLine.modelData)
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
        text: ComparisonText.txt("ui.changed")
    }

    Repeater {
        model: root.cmp.inputs || []
        delegate: ComparisonRow {
            id: inputRow
            required property var modelData
            Layout.fillWidth: true
            labelWidth: root.labelColW
            cellWidth: root.cellW
            label: ComparisonText.inputLabel(modelData.key)
            cells: modelData.cells
            textFor: function(cell) { return ComparisonText.inputText(inputRow.modelData, cell) }
            deltaFor: function(cell) { return ComparisonText.inputDelta(inputRow.modelData, cell.delta) }
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
        text: ComparisonText.txt("ui.nothingChanged")
        font: Theme.labelFont
        color: Theme.textSecondaryColor
        wrapMode: Text.WordWrap
    }

    Text {
        visible: root.unchangedText.length > 0
        Layout.fillWidth: true
        text: (root.columnCount > 2
               ? ComparisonText.txt("ui.sameForAll")
               : ComparisonText.txt("ui.sameForBoth"))
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
            headingOverride: ComparisonText.txt("ui.profileChanged")
                             .arg(root.shotInfo(index + 1).dateTime || "")
            diff: ({ hasBase: true, unchanged: false, baseTitle: "", rows: diffRows })
        }
    }

    // ── What happened ───────────────────────────────────────────────────────
    ComparisonSectionHeader {
        text: ComparisonText.txt("ui.happened")
    }

    Repeater {
        model: root.visibleMetrics
        delegate: ComparisonRow {
            id: metricRow
            required property var modelData
            Layout.fillWidth: true
            labelWidth: root.labelColW
            cellWidth: root.cellW
            label: ComparisonText.metricLabel(modelData.key)
            unit: ComparisonText.unitLabel(modelData.unit)
            cells: modelData.cells
            textFor: function(cell) { return ComparisonText.metricText(metricRow.modelData, cell.value) }
            deltaFor: function(cell) { return ComparisonText.metricDelta(metricRow.modelData, cell.delta) }
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
        label: ComparisonText.txt("row.stopped")
        cells: root.shots
        textFor: function(s) { return ComparisonText.stopText(s.stoppedBy) }
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
            label: ComparisonText.badgeLabel(modelData)
            cells: root.shots
            textFor: function(s) {
                return (s.badges || []).indexOf(badgeRow.modelData) >= 0
                    ? ComparisonText.txt("ui.yes")
                    : ComparisonText.txt("ui.no")
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
        label: ComparisonText.txt("row.rating")
        cells: root.shots
        textFor: function(s) { return ComparisonText.ratingText(s) }
        // Only between two rated shots; the base is the first cell.
        function ratingDelta(s) {
            const b = root.shots[0]
            if (s === b || s.rating0to100 === null || s.rating0to100 === undefined
                    || b.rating0to100 === null || b.rating0to100 === undefined) return 0
            return s.rating0to100 - b.rating0to100
        }
        deltaFor: function(s) { const d = ratingRow.ratingDelta(s); return d === 0 ? "" : ComparisonText.signed(d, 0) }
        deltaSignFor: function(s) { return Math.sign(ratingRow.ratingDelta(s)) }
        numberFont: root.numberFont
    }

    AccessibleButton {
        visible: root.hiddenMetricCount > 0
        Layout.alignment: Qt.AlignHCenter
        subtle: true
        text: root.showMore
            ? ComparisonText.txt("ui.showLess")
            : ComparisonText.txt("ui.showMore").arg(root.hiddenMetricCount)
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
