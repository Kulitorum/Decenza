// Every Repeater delegate here declares the roles it reads as required, so Bound
// cannot break role injection, and it makes this file's ids statically resolvable.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: shotComparisonPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("shotcomparison.title", "Compare Shots")

    objectName: "shotComparisonPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    property var comparisonModel: MainController.shotComparison
    // Wording from the comparison's shared table (ShotComparisonText).
    function txt(id) {
        const e = (comparisonModel.texts || {})[id]
        return e ? TranslationManager.translate(e.key, e.label) : id
    }

    // Persisted plot height. The readout sits under the plot in the same card, so
    // inspecting never needs a scroll; the page scrolls as one for the comparison.
    // Side by side, and a graph pinned above a scrolling comparison, were both tried:
    // each left the plot too small to read.
    property real graphHeight: Settings.value("comparison/graphHeight", Theme.scaled(320))

    property bool showAllCurves: false

    // Unique phase entries [{label, phaseIndex}] derived from graph data
    readonly property var phaseEntries: {
        var result = [], seen = {}
        for (let i = 0; i < comparisonGraph.phaseData.length; i++) {
            let pd = comparisonGraph.phaseData[i]
            if (!seen[pd.label]) { seen[pd.label] = true; result.push({ label: pd.label, phaseIndex: pd.phaseIndex }) }
        }
        return result
    }

    readonly property var curveEntries: {
        var out = []
        for (const e of GraphSeries.entries) {
            if (e.portal) continue
            if (e.advanced && !Settings.graph.advancedMode) continue
            out.push(e)
        }
        return out
    }
    readonly property int hiddenCurveCount: {
        var n = 0
        for (const e of curveEntries) if (!Settings.graph[e.key]) n++
        return n
    }

    component Chip: Rectangle {
        id: chip
        property string label
        property string tip: ""
        property color dotColor: "transparent"
        property bool active: true
        property bool checkable: true
        property bool longPressShowing: false
        signal toggled()

        implicitHeight: Theme.scaled(28)
        implicitWidth: chipRow.implicitWidth + Theme.scaled(18)
        radius: height / 2
        color: active ? Qt.alpha(dotColor.a > 0 ? dotColor : Theme.primaryColor, 0.16) : "transparent"
        border.color: active ? (dotColor.a > 0 ? dotColor : Theme.primaryColor) : Theme.borderColor
        border.width: 1
        opacity: active ? 1.0 : 0.6
        Accessible.description: tip
        Behavior on color { ColorAnimation { duration: 120 } }

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: Theme.scaled(5)
            Rectangle {
                visible: chip.dotColor.a > 0
                width: Theme.scaled(7); height: width; radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: chip.dotColor
                Accessible.ignored: true
            }
            Text {
                text: chip.label
                font: Theme.captionFont
                color: chip.active ? Theme.textColor : Theme.textSecondaryColor
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true
            }
        }
        AccessibleMouseArea {
            id: chipArea
            anchors.fill: parent
            accessibleName: chip.label
            accessibleRole: chip.checkable ? Accessible.CheckBox : Accessible.Button
            accessibleChecked: chip.active
            hoverEnabled: chip.tip !== ""
            supportLongPress: chip.tip !== ""
            onAccessibleClicked: chip.toggled()
            onAccessibleLongPressed: {
                chip.longPressShowing = true
                tipHideTimer.restart()
            }
        }
        // UI auto-dismiss for a long-press tip, as CustomLegend does.
        Timer {
            id: tipHideTimer
            interval: 4000
            onTriggered: chip.longPressShowing = false
        }
        HoverTip {
            text: chip.tip
            shown: (chipArea.containsMouse && chipArea.pressedButtons === 0) || chip.longPressShowing
            immediate: chip.longPressShowing
        }
    }

    Flickable {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        contentWidth: width
        contentHeight: graphPanel.implicitHeight + Theme.spacingSmall
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: graphPanel
            width: content.width
            spacing: Theme.spacingSmall

            RowLayout {
                id: graphHeader
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                // Page through the other shots; the base stays in column 0.
                RowLayout {
                    visible: shotComparisonPage.comparisonModel.totalShots > shotComparisonPage.comparisonModel.otherWindowSize + 1
                    spacing: Theme.scaled(4)

                    StyledIconButton {
                        text: "\u2190"
                        enabled: shotComparisonPage.comparisonModel.canShiftLeft
                        accessibleName: TranslationManager.translate("comparison.previousShots", "Previous shots")
                        onClicked: shotComparisonPage.comparisonModel.shiftWindowLeft()
                    }
                    Text {
                        readonly property var m: shotComparisonPage.comparisonModel
                        text: TranslationManager.translate("comparison.windowPosition", "%1–%2 of %3 others")
                              .arg(m.windowStart + 1)
                              .arg(Math.min(m.windowStart + m.otherWindowSize, m.totalShots - 1))
                              .arg(m.totalShots - 1)
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    StyledIconButton {
                        text: "\u2192"
                        enabled: shotComparisonPage.comparisonModel.canShiftRight
                        accessibleName: TranslationManager.translate("comparison.nextShots", "Next shots")
                        onClicked: shotComparisonPage.comparisonModel.shiftWindowRight()
                    }
                }

                Item { Layout.fillWidth: true }

                GraphOptionsButton {}
            }

            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(180), Math.min(Theme.scaled(600), shotComparisonPage.graphHeight))
                    + readout.implicitHeight + resizeHandle.height + Theme.spacingSmall
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                clip: true

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("comparison.crosshair", "Graph crosshair inspector")
                Accessible.focusable: true
                Accessible.onPressAction: graphMouseArea.clicked(null)

                ComparisonGraph {
                    id: comparisonGraph
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: readout.top
                    anchors.margins: Theme.spacingSmall
                    comparisonModel: shotComparisonPage.comparisonModel
                }

                ComparisonReadout {
                    id: readout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: resizeHandle.top
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    graph: comparisonGraph
                }

                // Tap or horizontal drag to scrub the crosshair
                MouseArea {
                    id: graphMouseArea
                    anchors.fill: parent
                    anchors.bottomMargin: resizeHandle.height + readout.height

                    property bool scrubbing: false
                    property real pressX: 0
                    property real pressY: 0
                    readonly property real dragThreshold: Theme.scaled(10)

                    onPressed: function(mouse) {
                        scrubbing = false
                        pressX = mouse.x
                        pressY = mouse.y
                    }
                    onPositionChanged: function(mouse) {
                        if (!scrubbing) {
                            let dx = Math.abs(mouse.x - pressX)
                            let dy = Math.abs(mouse.y - pressY)
                            // Only steal the gesture for horizontal drags (scrubbing)
                            if (dx > dragThreshold && dx > dy) {
                                scrubbing = true
                                preventStealing = true
                            }
                        }
                        if (scrubbing) {
                            let graphPos = mapToItem(comparisonGraph, mouse.x, mouse.y)
                            comparisonGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                    }
                    onReleased: function(mouse) {
                        if (!scrubbing) {
                            let graphPos = mapToItem(comparisonGraph, mouse.x, mouse.y)
                            comparisonGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                        scrubbing = false
                        preventStealing = false
                    }
                }

                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.ignored: true

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)
                        Repeater {
                            model: 3
                            Rectangle {
                                width: Theme.scaled(30)
                                height: 1
                                color: Theme.textSecondaryColor
                                opacity: resizeMouseArea.containsMouse || resizeMouseArea.pressed ? 0.8 : 0.4
                            }
                        }
                    }

                    MouseArea {
                        id: resizeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeVerCursor
                        preventStealing: true

                        property real startY: 0
                        property real startHeight: 0

                        onPressed: function(mouse) {
                            startY = mouse.y + resizeHandle.mapToItem(shotComparisonPage, 0, 0).y
                            startHeight = Math.min(Theme.scaled(600), shotComparisonPage.graphHeight)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                let currentY = mouse.y + resizeHandle.mapToItem(shotComparisonPage, 0, 0).y
                                shotComparisonPage.graphHeight = Math.max(Theme.scaled(180),
                                    Math.min(Theme.scaled(600), startHeight + currentY - startY))
                            }
                        }
                        onReleased: Settings.setValue("comparison/graphHeight", shotComparisonPage.graphHeight)
                    }
                }
            }

            // Curves, phases and alignment, one row of chips
            Flow {
                id: chipFlow
                Layout.fillWidth: true
                spacing: Theme.scaled(6)

                Repeater {
                    model: shotComparisonPage.curveEntries
                    delegate: Chip {
                        required property var modelData
                        visible: active || shotComparisonPage.showAllCurves
                        label: modelData.shortLabel
                        tip: modelData.label + (modelData.tip ? ": " + modelData.tip : "")
                        dotColor: modelData.sColor
                        active: Settings.graph[modelData.key]
                        onToggled: Settings.graph[modelData.key] = !Settings.graph[modelData.key]
                    }
                }
                Chip {
                    visible: shotComparisonPage.hiddenCurveCount > 0
                    checkable: false
                    active: false
                    tip: shotComparisonPage.showAllCurves
                        ? shotComparisonPage.txt("tip.fewerCurves")
                        : shotComparisonPage.txt("tip.moreCurves")
                    label: shotComparisonPage.showAllCurves
                        ? shotComparisonPage.txt("ui.fewerCurves")
                        : "+" + shotComparisonPage.hiddenCurveCount
                    onToggled: shotComparisonPage.showAllCurves = !shotComparisonPage.showAllCurves
                }

                Rectangle {
                    visible: shotComparisonPage.phaseEntries.length > 0
                    width: 1
                    height: Theme.scaled(28)
                    color: Theme.borderColor
                }

                Repeater {
                    model: shotComparisonPage.phaseEntries
                    delegate: Chip {
                        required property var modelData
                        label: modelData.label
                        tip: shotComparisonPage.txt("tip.phase").arg(modelData.label)
                        dotColor: comparisonGraph.phaseColors[modelData.phaseIndex % comparisonGraph.phaseColors.length]
                        active: !comparisonGraph.hiddenPhaseLabels[modelData.label]
                        onToggled: comparisonGraph.togglePhaseLabel(modelData.label)
                    }
                }

                Chip {
                    visible: shotComparisonPage.comparisonModel.shotCount > 1
                    label: shotComparisonPage.txt("ui.alignPours")
                    tip: shotComparisonPage.txt("tip.alignPours")
                    active: comparisonGraph.alignAtPourStart
                    onToggled: comparisonGraph.alignAtPourStart = !comparisonGraph.alignAtPourStart
                }
            }
            Rectangle {
                id: comparisonCard
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                implicitHeight: shotTable.implicitHeight + Theme.spacingMedium * 2
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ComparisonShotTable {
                    id: shotTable
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    graph: comparisonGraph
                    comparisonModel: shotComparisonPage.comparisonModel
                }
            }
        }
    }

    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("comparison.title", "Compare Shots")
        rightText: shotComparisonPage.comparisonModel.totalShots + " " + TranslationManager.translate("comparison.shots", "shots")
        onBackClicked: AppShell.backRequested()
    }
}
