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

    property var comparisonModel: ComparisonService
    // The displayed shots can change without changing the Repeater's count.
    property int _summaryVersion: 0
    Connections {
        target: shotComparisonPage.comparisonModel
        function onShotsChanged() { shotComparisonPage._summaryVersion++ }
    }
    readonly property var navigationContext: ({selectedShotIds: ComparisonService.selectedShotIds,
                                               baseShotId: ComparisonService.baseShotId,
                                               windowStart: ComparisonService.windowStart})

    // Persisted plot height. The readout sits under the plot in the same card, so
    // inspecting never needs a scroll; the page scrolls as one for the comparison.
    // Side by side, and a graph pinned above a scrolling comparison, were both tried:
    // each left the plot too small to read.
    property real graphHeight: RecipeContext.preferences.comparisonGraphHeight || Theme.scaled(320)
    property string _graphHeightRequestId: ""
    property string _graphHeightError: ""

    function saveGraphHeight() {
        if (!RecipeContext.ready) {
            graphHeight = RecipeContext.preferences.comparisonGraphHeight || Theme.scaled(320)
            _graphHeightError = TranslationManager.translate(
                "comparison.graphHeightSaveFailed", "Could not save graph size")
            return
        }
        _graphHeightError = ""
        _graphHeightRequestId = RecipeContext.newRequestId()
        RecipeContext.setPreference(_graphHeightRequestId, "comparisonGraphHeight",
            graphHeight, RecipeContext.revision)
    }

    Connections {
        target: RecipeContext
        function onPreferenceCompleted(requestId, name, success, preferences, reason) {
            if (requestId !== shotComparisonPage._graphHeightRequestId
                    || name !== "comparisonGraphHeight")
                return
            shotComparisonPage._graphHeightRequestId = ""
            if (success) {
                shotComparisonPage._graphHeightError = ""
                return
            }
            shotComparisonPage.graphHeight = preferences.comparisonGraphHeight
                || Theme.scaled(320)
            shotComparisonPage._graphHeightError = TranslationManager.translate(
                "comparison.graphHeightSaveFailed", "Could not save graph size")
        }
        function onPreferencesChanged() {
            if (shotComparisonPage._graphHeightRequestId.length === 0)
                shotComparisonPage.graphHeight = RecipeContext.preferences.comparisonGraphHeight
                    || Theme.scaled(320)
        }
    }

    // One chip per phase label, coloured as the graph draws it.
    readonly property var phaseEntries: {
        let result = [], seen = {}
        for (let i = 0; i < comparisonGraph.phaseData.length; i++) {
            const pd = comparisonGraph.phaseData[i]
            if (seen[pd.label]) continue
            seen[pd.label] = true
            result.push({ label: pd.label,
                          color: comparisonGraph.phaseColors[pd.phaseIndex % comparisonGraph.phaseColors.length] })
        }
        return result
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

            Text {
                Layout.fillWidth: true
                visible: ComparisonService.loading || ComparisonService.errorMessage.length > 0
                text: ComparisonService.errorMessage || TranslationManager.translate("common.loading", "Loading…")
                font: Theme.bodyFont
                color: ComparisonService.errorMessage ? Theme.errorColor : Theme.textSecondaryColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Text {
                Layout.fillWidth: true
                visible: shotComparisonPage._graphHeightError.length > 0
                text: shotComparisonPage._graphHeightError
                font: Theme.bodyFont
                color: Theme.errorColor
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            RowLayout {
                id: graphHeader
                Layout.fillWidth: true
                visible: shotComparisonPage.comparisonModel.totalShots > shotComparisonPage.comparisonModel.otherWindowSize + 1
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

            // Per-shot inputs use the recorded values, not the tolerance-collapsed
            // unchanged line in the detailed comparison.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Repeater {
                    model: shotComparisonPage.comparisonModel.shotCount
                    delegate: RowLayout {
                        id: inputSummary
                        required property int index
                        readonly property var info: {
                            let version = shotComparisonPage._summaryVersion
                            return shotComparisonPage.comparisonModel.getShotInfo(index)
                        }
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        ComparisonLineSwatch {
                            shotIndex: inputSummary.index
                            heavy: inputSummary.info.isBase === true
                        }
                        Text {
                            Layout.fillWidth: true
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.Wrap
                            text: {
                                const i = inputSummary.info
                                const grind = i.grinderSetting || "—"
                                const dose = i.doseG === null || i.doseG === undefined
                                    ? "—" : Number(i.doseG).toFixed(1) + " " + TranslationManager.translate("common.unit.grams", "g")
                                const temp = i.temperatureOverrideC === null || i.temperatureOverrideC === undefined
                                    ? "—" : Theme.formatTemperature(i.temperatureOverrideC, 1)
                                return (i.dateTime || "") + (i.isBase ? " · " + ComparisonText.txt("ui.base") : "")
                                    + " · " + ComparisonText.inputLabel("grinderSetting").replace(/[:：]\s*$/, "") + ": " + grind
                                    + " · " + ComparisonText.inputLabel("doseG").replace(/[:：]\s*$/, "") + ": " + dose
                                    + " · " + ComparisonText.inputLabel("temperatureOverrideC").replace(/[:：]\s*$/, "") + ": " + temp
                            }
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                        GraphOptionsButton {
                            visible: inputSummary.index === 0 && !graphHeader.visible
                            Layout.alignment: Qt.AlignTop
                        }
                    }
                }
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
                Accessible.onPressAction: comparisonGraph.inspectAtPosition(comparisonGraph.width / 2, comparisonGraph.height / 2)

                ComparisonGraph {
                    id: comparisonGraph
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: readout.top
                    anchors.margins: Theme.spacingSmall
                    comparisonModel: shotComparisonPage.comparisonModel
                }

                GraphReadout {
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
                        onReleased: shotComparisonPage.saveGraphHeight()
                    }
                }
            }

            // Curves, phases and alignment, one row of chips
            GraphChipRow {
                Layout.fillWidth: true
                phaseEntries: shotComparisonPage.phaseEntries
                hiddenPhaseLabels: comparisonGraph.hiddenPhaseLabels
                onPhaseToggled: label => comparisonGraph.togglePhaseLabel(label)
                showAlign: shotComparisonPage.comparisonModel.shotCount > 1
                alignActive: comparisonGraph.alignAtPourStart
                onAlignToggled: comparisonGraph.alignAtPourStart = !comparisonGraph.alignAtPourStart
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
