pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// "Shot results": one shot's measured outcome, from ShotComparison::compare() of the
// previous shot on its profile (the base) and this one, or of this shot alone, so
// every value reads as it does in a comparison. With a previous shot the card opens by
// naming it, then the comparison's one-line summary and what was changed, and each
// metric carries its Δ against it.
Rectangle {
    id: root

    property var comparison: ({})
    // The previous shot's time as a label, from the same outcome as `comparison`.
    property string previousWhen: ""
    property bool showMore: false
    signal compareRequested()

    readonly property var shots: comparison.shots || []
    readonly property int me: shots.length - 1
    readonly property var shot: me >= 0 ? shots[me] : ({})
    readonly property var previous: shots.length > 1 ? shots[0] : null
    readonly property var pair: (comparison.comparisons || [])[0] || null
    readonly property var metricRows: comparison.metrics || []
    readonly property var visibleMetrics: metricRows.filter(function(r) { return !r.more || root.showMore })
    readonly property int hiddenMetricCount: metricRows.filter(function(r) { return r.more }).length
    readonly property var changedInputs: {
        let out = []
        for (const r of (comparison.inputs || [])) {
            const b = r.cells[r.cells.length - 1]
            if (!b || b.state === "same") continue
            const d = ComparisonText.inputDelta(r, b.delta)
            out.push(ComparisonText.inputLabel(r.key) + "  " + ComparisonText.inputText(r, r.cells[0])
                     + " → " + ComparisonText.inputText(r, b) + (d ? "  (" + d + ")" : ""))
        }
        return out
    }
    readonly property var profileRows: pair && pair.profile ? (pair.profile.rows || []) : []
    readonly property string previousText: previous
        ? TranslationManager.translate("shotpage.comparedWith", "Compared with your last %1 shot · %2")
              .arg(previous.profileName || "").arg(previousWhen)
        : ""
    readonly property real labelColW: Theme.scaled(150)

    visible: me >= 0
    implicitHeight: column.implicitHeight + Theme.spacingMedium * 2
    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius

    function valueText(row, cell) {
        const t = ComparisonText.metricText(row, cell.value)
        const target = root.shot.targetYieldG
        if (row.key === "yieldG" && target && cell.value !== null && cell.value !== undefined)
            return TranslationManager.translate("shotpage.yieldOfTarget", "%1 of %2").arg(t).arg(target.toFixed(1))
        return t
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMedium
        spacing: 0

        ComparisonSectionHeader {
            Layout.topMargin: 0
            text: TranslationManager.translate("shotpage.results", "Shot results")
        }

        // The shot every Δ below is measured from, and the way into the full comparison.
        RowLayout {
            visible: root.previous !== null
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(4)
            spacing: Theme.spacingSmall
            Text {
                Layout.fillWidth: true
                text: root.previousText
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }
            AccessibleButton {
                subtle: true
                text: TranslationManager.translate("shotpage.compare", "Compare") + " ›"
                accessibleName: TranslationManager.translate("shotpage.compareAccessible", "Compare with your last shot")
                onClicked: root.compareRequested()
            }
        }

        Text {
            visible: root.pair !== null
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(4)
            text: root.pair ? ComparisonText.summaryFor(root.pair) : ""
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: root.previousText + ". " + text
        }

        Repeater {
            model: root.changedInputs
            delegate: Text {
                required property string modelData
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(2)
                text: "•  " + modelData
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }
        }

        ProfileDialInDiffBlock {
            visible: root.profileRows.length > 0
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(4)
            headingOverride: ComparisonText.txt("phrase.changed").arg(ComparisonText.inputLabel("profileSettings"))
            diff: ({ hasBase: true, unchanged: false, baseTitle: "", rows: root.profileRows })
        }

        Item { Layout.preferredHeight: Theme.spacingSmall }

        Repeater {
            model: root.visibleMetrics
            delegate: ComparisonRow {
                id: metricRow
                required property var modelData
                Layout.fillWidth: true
                labelWidth: root.labelColW
                cellWidth: column.width - root.labelColW - Theme.spacingSmall
                label: ComparisonText.metricLabel(modelData.key)
                unit: ComparisonText.unitLabel(modelData.unit)
                cells: [modelData.cells[root.me] || ({})]
                textFor: function(cell) { return root.valueText(metricRow.modelData, cell) }
                deltaFor: function(cell) { return ComparisonText.metricDelta(metricRow.modelData, cell.delta) }
                deltaSignFor: function(cell) { return cell.delta === null || cell.delta === undefined ? 0 : Math.sign(cell.delta) }
            }
        }

        ComparisonRow {
            visible: !!root.shot.stoppedBy
            Layout.fillWidth: true
            labelWidth: root.labelColW
            cellWidth: column.width - root.labelColW - Theme.spacingSmall
            label: ComparisonText.txt("row.stopped")
            cells: [root.shot]
            textFor: function(s) { return ComparisonText.stopText(s.stoppedBy) }
        }

        AccessibleButton {
            visible: root.hiddenMetricCount > 0
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingSmall
            subtle: true
            text: root.showMore
                ? ComparisonText.txt("ui.showLess")
                : ComparisonText.txt("ui.showMore").arg(root.hiddenMetricCount)
            accessibleName: text
            onClicked: root.showMore = !root.showMore
        }
    }
}
