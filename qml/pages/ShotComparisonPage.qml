import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotComparisonPage
    objectName: "shotComparisonPage"
    background: Rectangle { color: Theme.backgroundColor }

    property var comparisonModel: MainController.shotComparison

    // Persisted graph height
    property real graphHeight: Settings.value("comparison/graphHeight", Theme.scaled(280))

    // Unique phase entries [{label, phaseIndex}] derived from graph data
    readonly property var phaseEntries: {
        var _dep = comparisonGraph.phaseData
        var seen = {}, result = []
        for (var i = 0; i < comparisonGraph.phaseData.length; i++) {
            var pd = comparisonGraph.phaseData[i]
            if (!seen[pd.label]) { seen[pd.label] = true; result.push({ label: pd.label, phaseIndex: pd.phaseIndex }) }
        }
        return result
    }

    Component.onCompleted: {
        root.currentPageTitle = "Compare Shots"
    }

    // Scrollable content area (vertical only)
    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        contentWidth: width  // Lock horizontal scroll
        contentHeight: contentColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: Theme.spacingSmall

            // Graph with resize handle and window navigation
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(150), Math.min(Theme.scaled(500), shotComparisonPage.graphHeight))
                Layout.topMargin: Theme.spacingSmall
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                clip: true

                ComparisonGraph {
                    id: comparisonGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    comparisonModel: shotComparisonPage.comparisonModel
                }

                // Crosshair drag handler (tap or horizontal drag to scrub)
                MouseArea {
                    id: graphMouseArea
                    anchors.fill: parent
                    anchors.bottomMargin: resizeHandle.height

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("comparison.crosshair", "Graph crosshair inspector")
                    Accessible.focusable: true
                    Accessible.onPressAction: graphMouseArea.clicked(null)

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
                            var dx = Math.abs(mouse.x - pressX)
                            var dy = Math.abs(mouse.y - pressY)
                            // Only steal the gesture for horizontal drags (scrubbing)
                            if (dx > dragThreshold && dx > dy) {
                                scrubbing = true
                                preventStealing = true
                            }
                        }
                        if (scrubbing) {
                            var graphPos = mapToItem(comparisonGraph, mouse.x, mouse.y)
                            comparisonGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                    }
                    onReleased: function(mouse) {
                        if (!scrubbing) {
                            // Simple tap — inspect at tap position
                            var graphPos = mapToItem(comparisonGraph, mouse.x, mouse.y)
                            comparisonGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                        scrubbing = false
                        preventStealing = false
                    }
                }

                // Window navigation bar (only show when more shots than display window)
                Row {
                    visible: comparisonModel.totalShots > 3
                    anchors.bottom: resizeHandle.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: Theme.spacingSmall
                    spacing: 0

                    Rectangle {
                        width: Theme.scaled(28)
                        height: Theme.scaled(24)
                        radius: Theme.scaled(12)
                        color: comparisonModel.canShiftLeft ? Qt.rgba(0, 0, 0, 0.6) : Qt.rgba(0, 0, 0, 0.3)
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("comparison.previousShots", "Previous shots")
                        Accessible.focusable: true
                        Accessible.onPressAction: leftArrowArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "\u25C0"
                            font.pixelSize: Theme.captionFont.pixelSize
                            color: comparisonModel.canShiftLeft ? "white" : Qt.rgba(1, 1, 1, 0.4)
                            Accessible.ignored: true
                        }
                        MouseArea {
                            id: leftArrowArea
                            anchors.fill: parent
                            enabled: comparisonModel.canShiftLeft
                            onClicked: comparisonModel.shiftWindowLeft()
                        }
                    }

                    Rectangle {
                        width: windowPositionText.width + Theme.scaled(12)
                        height: Theme.scaled(24)
                        color: Qt.rgba(0, 0, 0, 0.5)

                        Text {
                            id: windowPositionText
                            anchors.centerIn: parent
                            text: (comparisonModel.windowStart + 1) + "-" +
                                  Math.min(comparisonModel.windowStart + 3, comparisonModel.totalShots) +
                                  " / " + comparisonModel.totalShots
                            font: Theme.captionFont
                            color: "white"
                            Accessible.ignored: true
                        }
                    }

                    Rectangle {
                        width: Theme.scaled(28)
                        height: Theme.scaled(24)
                        radius: Theme.scaled(12)
                        color: comparisonModel.canShiftRight ? Qt.rgba(0, 0, 0, 0.6) : Qt.rgba(0, 0, 0, 0.3)
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("comparison.nextShots", "Next shots")
                        Accessible.focusable: true
                        Accessible.onPressAction: rightArrowArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "\u25B6"
                            font.pixelSize: Theme.captionFont.pixelSize
                            color: comparisonModel.canShiftRight ? "white" : Qt.rgba(1, 1, 1, 0.4)
                            Accessible.ignored: true
                        }
                        MouseArea {
                            id: rightArrowArea
                            anchors.fill: parent
                            enabled: comparisonModel.canShiftRight
                            onClicked: comparisonModel.shiftWindowRight()
                        }
                    }
                }

                // Resize handle at bottom
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"

                    // Visual indicator (three lines)
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
                            startHeight = graphCard.Layout.preferredHeight
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(shotComparisonPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                // Clamp between min and max
                                newHeight = Math.max(Theme.scaled(150), Math.min(Theme.scaled(500), newHeight))
                                shotComparisonPage.graphHeight = newHeight
                            }
                        }

                        onReleased: {
                            // Save the height
                            Settings.setValue("comparison/graphHeight", shotComparisonPage.graphHeight)
                        }
                    }
                }
            }

            // Card: crosshair data table + phase pills
            Rectangle {
                Layout.fillWidth: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                implicitHeight: dataTableCard.implicitHeight + Theme.spacingMedium * 2

                ColumnLayout {
                    id: dataTableCard
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    ComparisonDataTable {
                        graph: comparisonGraph
                        comparisonModel: shotComparisonPage.comparisonModel
                        Layout.fillWidth: true
                    }

                    // Phase toggle pills
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall
                        visible: shotComparisonPage.phaseEntries.length > 0

                        Repeater {
                            model: shotComparisonPage.phaseEntries

                            Rectangle {
                                required property var modelData
                                property color phaseColor: comparisonGraph.phaseColors[modelData.phaseIndex % comparisonGraph.phaseColors.length]
                                property bool phaseOn: !comparisonGraph.hiddenPhaseLabels[modelData.label]

                                height: Theme.scaled(26)
                                width: pillRow.implicitWidth + Theme.scaled(16)
                                radius: Theme.scaled(13)
                                color: phaseOn ? Qt.rgba(phaseColor.r, phaseColor.g, phaseColor.b, 0.18) : "transparent"
                                border.color: phaseOn ? phaseColor : Theme.borderColor
                                border.width: 1
                                opacity: phaseOn ? 1.0 : 0.55

                                Accessible.role: Accessible.CheckBox
                                Accessible.name: modelData.label
                                Accessible.checked: phaseOn
                                Accessible.focusable: true
                                Accessible.onPressAction: comparisonGraph.togglePhaseLabel(modelData.label)

                                Row {
                                    id: pillRow
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(4)
                                    Rectangle {
                                        width: Theme.scaled(6); height: Theme.scaled(6); radius: Theme.scaled(3)
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: phaseColor
                                        Accessible.ignored: true
                                    }
                                    Text {
                                        text: modelData.label
                                        font: Theme.captionFont
                                        color: phaseOn ? phaseColor : Theme.textSecondaryColor
                                        anchors.verticalCenter: parent.verticalCenter
                                        Accessible.ignored: true
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: comparisonGraph.togglePhaseLabel(modelData.label)
                                }
                            }
                        }
                    }
                }
            }

            // Shot comparison table (rows = metrics + phases, columns = shots)
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                Layout.bottomMargin: Theme.spacingSmall
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                implicitHeight: shotTable.implicitHeight + Theme.spacingMedium * 2

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

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("comparison.title", "Compare Shots")
        rightText: comparisonModel.shotCount + " " + TranslationManager.translate("comparison.shots", "shots")
        onBackClicked: root.goBack()
    }
}
