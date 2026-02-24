import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: autoFavoriteInfoPage
    objectName: "autoFavoriteInfoPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Properties passed from AutoFavoritesPage
    property int shotId: 0
    property string groupBy: ""
    property string beanBrand: ""
    property string beanType: ""
    property string profileName: ""
    property string grinderModel: ""
    property string grinderSetting: ""
    property int avgEnjoyment: 0
    property int shotCount: 0

    // Loaded data
    property var shotData: ({})
    property var groupDetails: ({})

    // Persisted graph height
    property real graphHeight: Settings.value("autoFavoriteInfo/graphHeight", Theme.scaled(250))

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("autofavoriteinfo.title", "Favorite Details")
        loadData()
    }

    function loadData() {
        if (shotId > 0)
            MainController.shotHistory.requestShot(shotId)

        groupDetails = MainController.shotHistory.getAutoFavoriteGroupDetails(
            groupBy, beanBrand, beanType, profileName, grinderModel, grinderSetting)
    }

    // Handle async shot data
    Connections {
        target: MainController.shotHistory
        function onShotReady(id, shot) {
            if (id !== autoFavoriteInfoPage.shotId) return
            shotData = shot
        }
    }

    // Helper properties for conditional display
    property bool _hasBean: !!(beanBrand || beanType)
    property bool _hasProfile: !!(profileName && profileName.length > 0)
    property bool _hasGrinder: !!(grinderModel || grinderSetting)
    property string _beanText: {
        var parts = []
        if (beanBrand) parts.push(beanBrand)
        if (beanType) parts.push(beanType)
        return parts.join(" - ")
    }
    property string _grinderText: (grinderModel || "") +
        (grinderSetting ? " @ " + grinderSetting : "")
    property var _notes: groupDetails.notes || []
    property bool _hasRoastDate: !!(shotData.roastDate && shotData.roastDate !== "")
    property bool _hasRoastLevel: !!(shotData.roastLevel && shotData.roastLevel !== "")
    property bool _hasBeanCardData: _hasBean || _hasRoastDate || _hasRoastLevel

    ScrollView {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            // Header: Bean · Profile · Grinder + shot count
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Flow {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        text: _beanText
                        font: Theme.titleFont
                        color: Theme.textColor
                        visible: _hasBean
                        Accessible.ignored: true
                    }

                    Text {
                        text: "  \u00b7  "
                        font: Theme.titleFont
                        color: Theme.textSecondaryColor
                        visible: _hasBean && _hasProfile
                        Accessible.ignored: true
                    }

                    Text {
                        text: profileName || ""
                        font: Theme.titleFont
                        color: Theme.primaryColor
                        visible: _hasProfile
                        Accessible.ignored: true
                    }

                    Text {
                        text: "  \u00b7  "
                        font: Theme.titleFont
                        color: Theme.textSecondaryColor
                        visible: _hasGrinder && (_hasBean || _hasProfile)
                        Accessible.ignored: true
                    }

                    Text {
                        text: _grinderText
                        font: Theme.titleFont
                        color: Theme.textSecondaryColor
                        visible: _hasGrinder
                        Accessible.ignored: true
                    }
                }

                Text {
                    text: shotCount + " " +
                          TranslationManager.translate("autofavorites.shots", "shots")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
            }

            // Graph inspect bar
            GraphInspectBar { graph: shotGraph; visible: shotId > 0 }

            // Shot graph (most recent shot)
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), autoFavoriteInfoPage.graphHeight))
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                clip: true
                visible: shotId > 0

                Accessible.role: Accessible.Graphic
                Accessible.name: TranslationManager.translate("autofavoriteinfo.graph", "Most recent shot graph")
                Accessible.focusable: true
                Accessible.onPressAction: graphTapArea.clicked(null)

                HistoryShotGraph {
                    id: shotGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    pressureData: shotData.pressure || []
                    flowData: shotData.flow || []
                    temperatureData: shotData.temperature || []
                    weightData: shotData.weight || []
                    weightFlowRateData: shotData.weightFlowRate || []
                    resistanceData: shotData.resistance || []
                    phaseMarkers: shotData.phases || []
                    maxTime: shotData.duration || 60
                    Accessible.ignored: true
                }

                // Tap handler for graph interaction
                MouseArea {
                    id: graphTapArea
                    anchors.fill: parent
                    anchors.bottomMargin: resizeHandle.height
                    onClicked: function(mouse) {
                        var graphPos = mapToItem(shotGraph, mouse.x, mouse.y)
                        if (graphPos.x > shotGraph.plotArea.x + shotGraph.plotArea.width) {
                            shotGraph.toggleRightAxis()
                        } else {
                            shotGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                    }
                    onPositionChanged: function(mouse) {
                        if (pressed) {
                            var graphPos = mapToItem(shotGraph, mouse.x, mouse.y)
                            shotGraph.inspectAtPosition(graphPos.x, graphPos.y)
                        }
                    }
                }

                // Resize handle
                Rectangle {
                    id: resizeHandle
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.scaled(16)
                    color: "transparent"
                    Accessible.role: Accessible.Slider
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.resizegraph", "Resize graph")
                    Accessible.focusable: true
                    Accessible.onPressAction: resizeMouseArea.clicked(null)

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
                            startY = mouse.y + resizeHandle.mapToItem(autoFavoriteInfoPage, 0, 0).y
                            startHeight = graphCard.Layout.preferredHeight
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var currentY = mouse.y + resizeHandle.mapToItem(autoFavoriteInfoPage, 0, 0).y
                                var delta = currentY - startY
                                var newHeight = startHeight + delta
                                newHeight = Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), newHeight))
                                autoFavoriteInfoPage.graphHeight = newHeight
                            }
                        }
                        onReleased: {
                            Settings.setValue("autoFavoriteInfo/graphHeight", autoFavoriteInfoPage.graphHeight)
                        }
                    }
                }
            }

            // Graph legend
            GraphLegend { graph: shotGraph; visible: shotId > 0 }

            // Metrics row: Avg Duration, Avg Dose, Avg Yield, Avg Rating
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: (groupDetails.avgDuration || 0) > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgduration", "Avg Duration") + ": " +
                        (groupDetails.avgDuration || 0).toFixed(1) + "s"
                    Tr {
                        key: "autofavoriteinfo.avgduration"
                        fallback: "Avg Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (groupDetails.avgDuration || 0).toFixed(1) + "s"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: (groupDetails.avgDose || 0) > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgdose", "Avg Dose") + ": " +
                        (groupDetails.avgDose || 0).toFixed(1) + "g"
                    Tr {
                        key: "autofavoriteinfo.avgdose"
                        fallback: "Avg Dose"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (groupDetails.avgDose || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeDoseColor
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: (groupDetails.avgYield || 0) > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgyield", "Avg Yield") + ": " +
                        (groupDetails.avgYield || 0).toFixed(1) + "g"
                    Tr {
                        key: "autofavoriteinfo.avgyield"
                        fallback: "Avg Yield"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (groupDetails.avgYield || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeOutputColor
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: avgEnjoyment > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgrating", "Avg Rating") + ": " +
                        avgEnjoyment + "%"
                    Tr {
                        key: "autofavoriteinfo.avgrating"
                        fallback: "Avg Rating"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: avgEnjoyment + "%"
                        font: Theme.subtitleFont
                        color: Theme.warningColor
                        Accessible.ignored: true
                    }
                }
            }

            // Analysis card (TDS/EY)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: analysisColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: (groupDetails.avgTds || 0) > 0 || (groupDetails.avgEy || 0) > 0
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("shotdetail.analysis", "Analysis")

                ColumnLayout {
                    id: analysisColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.analysis"
                        fallback: "Analysis"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }

                    RowLayout {
                        spacing: Theme.spacingLarge

                        ColumnLayout {
                            visible: (groupDetails.avgTds || 0) > 0
                            spacing: Theme.scaled(2)
                            Tr { key: "autofavoriteinfo.avgtds"; fallback: "Avg TDS"; font: Theme.captionFont; color: Theme.textSecondaryColor; Accessible.ignored: true }
                            Text { text: (groupDetails.avgTds || 0).toFixed(2) + "%"; font: Theme.bodyFont; color: Theme.dyeTdsColor; Accessible.ignored: true }
                        }

                        ColumnLayout {
                            visible: (groupDetails.avgEy || 0) > 0
                            spacing: Theme.scaled(2)
                            Tr { key: "autofavoriteinfo.avgey"; fallback: "Avg EY"; font: Theme.captionFont; color: Theme.textSecondaryColor; Accessible.ignored: true }
                            Text { text: (groupDetails.avgEy || 0).toFixed(1) + "%"; font: Theme.bodyFont; color: Theme.dyeEyColor; Accessible.ignored: true }
                        }
                    }
                }
            }

            // Notes section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: _notes.length > 0

                Tr {
                    key: "shotdetail.notes"
                    fallback: "Notes"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                Repeater {
                    model: _notes

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: modelData.dateTime || ""
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        ExpandableTextArea {
                            Layout.fillWidth: true
                            inlineHeight: Theme.scaled(100)
                            text: modelData.text || ""
                            accessibleName: TranslationManager.translate("shotdetail.notes", "Notes") +
                                " " + (modelData.dateTime || "")
                            textFont: Theme.bodyFont
                            readOnly: true
                        }
                    }
                }
            }

            // Bean info card
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: beanColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: _hasBeanCardData
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("shotdetail.beaninfo", "Beans")

                ColumnLayout {
                    id: beanColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.beaninfo"
                        fallback: "Beans"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Theme.spacingLarge
                        rowSpacing: Theme.spacingSmall
                        Layout.fillWidth: true

                        Tr { key: "shotdetail.roaster"; fallback: "Roaster:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: beanBrand !== ""; Accessible.ignored: true }
                        Text { text: beanBrand; font: Theme.labelFont; color: Theme.textColor; visible: beanBrand !== ""; Accessible.ignored: true }

                        Tr { key: "shotdetail.coffee"; fallback: "Coffee:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: beanType !== ""; Accessible.ignored: true }
                        Text { text: beanType; font: Theme.labelFont; color: Theme.textColor; visible: beanType !== ""; Accessible.ignored: true }

                        Tr { key: "shotdetail.roastdate"; fallback: "Roast Date:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: _hasRoastDate; Accessible.ignored: true }
                        Text { text: shotData.roastDate || ""; font: Theme.labelFont; color: Theme.textColor; visible: _hasRoastDate; Accessible.ignored: true }

                        Tr { key: "shotdetail.roastlevel"; fallback: "Roast Level:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: _hasRoastLevel; Accessible.ignored: true }
                        Text { text: shotData.roastLevel || ""; font: Theme.labelFont; color: Theme.textColor; visible: _hasRoastLevel; Accessible.ignored: true }
                    }
                }
            }

            // Bottom spacer
            Item { Layout.preferredHeight: Theme.spacingLarge }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("autofavoriteinfo.title", "Favorite Details")
        onBackClicked: root.goBack()
    }
}
