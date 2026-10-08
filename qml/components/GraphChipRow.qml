pragma ComponentBehavior: Bound

import QtQuick
import Decenza

// The chips under a shot graph: one per curve (the Settings.graph toggles the
// live graph's GraphLegend shows, rarely used ones behind "+N"), then one per
// phase, then an optional extra chip such as the comparison's pour alignment.
// They are the graph's legend as well as its switches.
Flow {
    id: root

    // [{label, color}], one per phase, in the order the shot entered them.
    property var phaseEntries: []
    property var hiddenPhaseLabels: ({})
    signal phaseToggled(string label)

    property bool showAlign: false
    property bool alignActive: false
    signal alignToggled()

    property bool showAllCurves: false
    // A single shot recorded with a PORTAL shows its curves; a comparison never does.
    property bool portalAvailable: false

    readonly property var curveEntries: {
        let out = []
        for (const e of GraphSeries.entries) {
            if (e.portal && !portalAvailable) continue
            if (e.advanced && !Settings.graph.advancedMode) continue
            out.push(e)
        }
        return out
    }
    readonly property int hiddenCurveCount: {
        let n = 0
        for (const e of curveEntries) if (!Settings.graph[e.key]) n++
        return n
    }

    spacing: Theme.scaled(6)

    Repeater {
        model: root.curveEntries
        delegate: GraphChip {
            required property var modelData
            visible: active || root.showAllCurves
            label: modelData.shortLabel
            tip: modelData.label + (modelData.tip ? ": " + modelData.tip : "")
            dotColor: modelData.sColor
            active: Settings.graph[modelData.key]
            onToggled: Settings.graph[modelData.key] = !Settings.graph[modelData.key]
        }
    }
    GraphChip {
        visible: root.hiddenCurveCount > 0
        checkable: false
        active: false
        tip: root.showAllCurves ? ComparisonText.txt("tip.fewerCurves") : ComparisonText.txt("tip.moreCurves")
        label: root.showAllCurves ? ComparisonText.txt("ui.fewerCurves") : "+" + root.hiddenCurveCount
        onToggled: root.showAllCurves = !root.showAllCurves
    }

    Rectangle {
        visible: root.phaseEntries.length > 0
        width: 1
        height: Theme.scaled(28)
        color: Theme.borderColor
    }

    Repeater {
        model: root.phaseEntries
        delegate: GraphChip {
            required property var modelData
            label: modelData.label
            tip: ComparisonText.txt("tip.phase").arg(modelData.label)
            dotColor: modelData.color
            active: !root.hiddenPhaseLabels[modelData.label]
            onToggled: root.phaseToggled(modelData.label)
        }
    }

    GraphChip {
        visible: root.showAlign
        label: ComparisonText.txt("ui.alignPours")
        tip: ComparisonText.txt("tip.alignPours")
        active: root.alignActive
        onToggled: root.alignToggled()
    }
}
