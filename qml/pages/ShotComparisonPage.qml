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

    // Curve visibility toggles
    property bool showPressure: true
    property bool showFlow: true
    property bool showWeight: true
    property bool showWeightFlow: true

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

            // Legend
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                Repeater {
                    model: comparisonModel.shots  // Use shots array so items refresh

                    RowLayout {
                        spacing: Theme.spacingSmall
                        property int globalIndex: comparisonModel.windowStart + index

                        Rectangle {
                            width: Theme.scaled(16)
                            height: Theme.scaled(16)
                            radius: Theme.scaled(4)
                            color: comparisonModel.getShotColor(globalIndex % 3)
                        }

                        Text {
                            text: {
                                var info = comparisonModel.getShotInfo(index)
                                return (globalIndex + 1) + ". " + info.profileName + " - " + info.dateTime
                            }
                            font: Theme.labelFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.preferredWidth: Theme.scaled(200)
                        }
                    }
                }
            }

            // Graph with resize handle and swipe navigation
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(150), Math.min(Theme.scaled(500), shotComparisonPage.graphHeight))
                Layout.topMargin: Theme.spacingSmall
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                clip: true

                // Visual offset during swipe
                transform: Translate { x: graphSwipeArea.swipeOffset * 0.3 }

                ComparisonGraph {
                    id: comparisonGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    comparisonModel: shotComparisonPage.comparisonModel
                    showPressure: shotComparisonPage.showPressure
                    showFlow: shotComparisonPage.showFlow
                    showWeight: shotComparisonPage.showWeight
                    showWeightFlow: shotComparisonPage.showWeightFlow
                }

                // Swipe handler overlay (above graph, below resize handle)
                SwipeableArea {
                    id: graphSwipeArea
                    anchors.fill: parent
                    anchors.bottomMargin: resizeHandle.height
                    canSwipeLeft: comparisonModel.canShiftRight
                    canSwipeRight: comparisonModel.canShiftLeft

                    onSwipedLeft: comparisonModel.shiftWindowRight()
                    onSwipedRight: comparisonModel.shiftWindowLeft()
                }

                // Position indicator (only show if more than 3 shots)
                Rectangle {
                    visible: comparisonModel.totalShots > 3
                    anchors.bottom: resizeHandle.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: Theme.spacingSmall
                    width: windowPositionText.width + Theme.scaled(16)
                    height: Theme.scaled(24)
                    radius: Theme.scaled(12)
                    color: Qt.rgba(0, 0, 0, 0.5)

                    Text {
                        id: windowPositionText
                        anchors.centerIn: parent
                        text: (comparisonModel.windowStart + 1) + "-" +
                              Math.min(comparisonModel.windowStart + 3, comparisonModel.totalShots) +
                              " / " + comparisonModel.totalShots
                        font: Theme.captionFont
                        color: "white"
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

            // Curve type toggle buttons
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacingMedium

                // Pressure toggle
                Rectangle {
                    width: pressureToggleContent.width + Theme.scaled(16)
                    height: Theme.scaled(32)
                    radius: Theme.scaled(16)
                    color: showPressure ? Theme.surfaceColor : "transparent"
                    border.color: showPressure ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                    opacity: showPressure ? 1.0 : 0.5

                    RowLayout {
                        id: pressureToggleContent
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall

                        Rectangle { width: Theme.scaled(20); height: 2; color: showPressure ? Theme.textColor : Theme.textSecondaryColor }
                        Text {
                            text: TranslationManager.translate("comparison.pressure", "Pressure")
                            font: Theme.captionFont
                            color: showPressure ? Theme.textColor : Theme.textSecondaryColor
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: showPressure = !showPressure
                    }
                }

                // Flow toggle
                Rectangle {
                    width: flowToggleContent.width + Theme.scaled(16)
                    height: Theme.scaled(32)
                    radius: Theme.scaled(16)
                    color: showFlow ? Theme.surfaceColor : "transparent"
                    border.color: showFlow ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                    opacity: showFlow ? 1.0 : 0.5

                    RowLayout {
                        id: flowToggleContent
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall

                        Rectangle {
                            width: Theme.scaled(20); height: 2; color: showFlow ? Theme.textColor : Theme.textSecondaryColor
                            Rectangle { anchors.fill: parent; color: "transparent"; border.color: showFlow ? Theme.textColor : Theme.textSecondaryColor; border.width: 1 }
                        }
                        Text {
                            text: TranslationManager.translate("comparison.flow", "Flow")
                            font: Theme.captionFont
                            color: showFlow ? Theme.textColor : Theme.textSecondaryColor
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: showFlow = !showFlow
                    }
                }

                // Weight toggle
                Rectangle {
                    width: weightToggleContent.width + Theme.scaled(16)
                    height: Theme.scaled(32)
                    radius: Theme.scaled(16)
                    color: showWeight ? Theme.surfaceColor : "transparent"
                    border.color: showWeight ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                    opacity: showWeight ? 1.0 : 0.5

                    RowLayout {
                        id: weightToggleContent
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall

                        Row {
                            spacing: Theme.scaled(3)
                            Repeater {
                                model: 4
                                Rectangle { width: 3; height: 2; color: showWeight ? Theme.textColor : Theme.textSecondaryColor }
                            }
                        }
                        Text {
                            text: TranslationManager.translate("comparison.weight", "Weight")
                            font: Theme.captionFont
                            color: showWeight ? Theme.textColor : Theme.textSecondaryColor
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: showWeight = !showWeight
                    }
                }

                // Weight Flow toggle
                Rectangle {
                    width: weightFlowToggleContent.width + Theme.scaled(16)
                    height: Theme.scaled(32)
                    radius: Theme.scaled(16)
                    color: showWeightFlow ? Theme.surfaceColor : "transparent"
                    border.color: showWeightFlow ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                    opacity: showWeightFlow ? 1.0 : 0.5

                    RowLayout {
                        id: weightFlowToggleContent
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall

                        Rectangle { width: Theme.scaled(20); height: 2; color: showWeightFlow ? Theme.weightFlowColor : Theme.textSecondaryColor }
                        Text {
                            text: TranslationManager.translate("comparison.weightFlow", "Weight Flow")
                            font: Theme.captionFont
                            color: showWeightFlow ? Theme.textColor : Theme.textSecondaryColor
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: showWeightFlow = !showWeightFlow
                    }
                }
            }

            // Shot columns
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                Layout.bottomMargin: Theme.spacingSmall
                spacing: Theme.spacingMedium

                Repeater {
                    model: comparisonModel.shots  // Use shots array so items refresh

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: shotColumn.height + Theme.spacingMedium * 2
                        Layout.alignment: Qt.AlignTop
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        property int globalIndex: comparisonModel.windowStart + index
                        property color shotColor: comparisonModel.getShotColor(globalIndex % 3)

                        ColumnLayout {
                            id: shotColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingSmall

                            // Shot header with color indicator
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    width: Theme.scaled(12)
                                    height: Theme.scaled(12)
                                    radius: Theme.scaled(3)
                                    color: shotColor
                                }

                                Text {
                                    text: TranslationManager.translate("comparison.shot", "Shot") + " " + (globalIndex + 1)
                                    font: Theme.subtitleFont
                                    color: shotColor
                                }
                            }

                            // Profile (Temp)
                            Text {
                                text: {
                                    var info = comparisonModel.getShotInfo(index)
                                    var name = info.profileName || "-"
                                    var t = info.temperatureOverride
                                    if (t !== undefined && t !== null && t > 0) {
                                        return name + " (" + Math.round(t) + "\u00B0C)"
                                    }
                                    return name
                                }
                                font: Theme.labelFont
                                color: Theme.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Date
                            Text {
                                text: comparisonModel.getShotInfo(index).dateTime || "-"
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                            }

                            // Separator
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(1)
                                color: Theme.borderColor
                            }

                            // Metrics grid
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: Theme.spacingSmall
                                rowSpacing: Theme.spacingSmall

                                Tr { key: "comparison.duration"; fallback: "Duration"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).duration || 0).toFixed(1) + "s"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.dose"; fallback: "Dose"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).doseWeight || 0).toFixed(1) + "g"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.output"; fallback: "Output"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text {
                                    text: {
                                        var info = comparisonModel.getShotInfo(index)
                                        var actual = (info.finalWeight || 0).toFixed(1) + "g"
                                        var y = info.yieldOverride
                                        if (y !== undefined && y !== null && y > 0
                                            && Math.abs(y - info.finalWeight) > 0.5) {
                                            return actual + " (" + Math.round(y) + "g)"
                                        }
                                        return actual
                                    }
                                    font: Theme.labelFont; color: Theme.textColor
                                }

                                Tr { key: "comparison.ratio"; fallback: "Ratio"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: comparisonModel.getShotInfo(index).ratio || "-"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.rating"; fallback: "Rating"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).enjoyment || 0) + "%"; font: Theme.labelFont; color: Theme.warningColor }

                                Tr { key: "comparison.bean"; fallback: "Bean"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text {
                                    text: {
                                        var info = comparisonModel.getShotInfo(index)
                                        var bean = (info.beanBrand || "") + (info.beanType ? " " + info.beanType : "")
                                        var grind = info.grinderSetting || ""
                                        if (bean && grind) return bean + " (" + grind + ")"
                                        if (bean) return bean
                                        if (grind) return "(" + grind + ")"
                                        return "-"
                                    }
                                    font: Theme.labelFont
                                    color: Theme.textColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Tr { key: "comparison.roast"; fallback: "Roast"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text {
                                    text: {
                                        var info = comparisonModel.getShotInfo(index)
                                        var parts = []
                                        if (info.roastLevel) parts.push(info.roastLevel)
                                        if (info.roastDate) parts.push(info.roastDate)
                                        return parts.length > 0 ? parts.join(", ") : "-"
                                    }
                                    font: Theme.labelFont
                                    color: Theme.textColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Tr { key: "comparison.tdsEy"; fallback: "TDS/EY"; font: Theme.captionFont; color: Theme.textSecondaryColor; visible: comparisonModel.getShotInfo(index).drinkTds > 0 || comparisonModel.getShotInfo(index).drinkEy > 0 }
                                Text {
                                    visible: comparisonModel.getShotInfo(index).drinkTds > 0 || comparisonModel.getShotInfo(index).drinkEy > 0
                                    text: {
                                        var info = comparisonModel.getShotInfo(index)
                                        var parts = []
                                        if (info.drinkTds > 0) parts.push(info.drinkTds.toFixed(2) + "%")
                                        if (info.drinkEy > 0) parts.push(info.drinkEy.toFixed(1) + "%")
                                        return parts.join(" / ")
                                    }
                                    font: Theme.labelFont
                                    color: Theme.textColor
                                }

                                Tr { key: "comparison.barista"; fallback: "Barista"; font: Theme.captionFont; color: Theme.textSecondaryColor; visible: comparisonModel.getShotInfo(index).barista !== "" }
                                Text {
                                    visible: comparisonModel.getShotInfo(index).barista !== ""
                                    text: comparisonModel.getShotInfo(index).barista || ""
                                    font: Theme.labelFont
                                    color: Theme.textColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            // Separator
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(1)
                                color: Theme.borderColor
                            }

                            // Notes section
                            Tr {
                                key: "comparison.notes"
                                fallback: "Notes"
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: comparisonModel.getShotInfo(index).notes || "-"
                                font: Theme.labelFont
                                color: Theme.textColor
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
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
