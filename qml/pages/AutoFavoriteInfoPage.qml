// The notes Repeater delegate and the resize-grip Repeater read this file's ids
// (`autoFavoriteInfoPage`, `resizeMouseArea`); Bound makes them statically resolvable.
// The notes delegate declares its one injected role, `modelData`, required in the same
// edit -- without that, Bound stops role injection and every note renders blank at
// RUNTIME, silently. The grip delegate takes no role.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Page {
    id: autoFavoriteInfoPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("autofavoriteinfo.title", "Favorite Details")

    objectName: "autoFavoriteInfoPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    // Properties passed from AutoFavoritesPage
    property int shotId: 0
    property string groupBy: ""
    property string beanBrand: ""
    property string beanType: ""
    property string profileName: ""
    property string grinderBrand: ""
    property string grinderModel: ""
    property string grinderSetting: ""
    property real doseBucket: 0
    property real targetWeight: 0
    property int avgEnjoyment: 0
    property int shotCount: 0

    // Loaded data
    property var shotData: ({})
    property var groupDetails: ({})

    // Persisted graph height
    property real graphHeight: Settings.value("autoFavoriteInfo/graphHeight", Theme.scaled(250))

    // Re-assert on every activation, not just creation — returning here after a
    // page was pushed on top would otherwise keep that page's header title.
    StackView.onActivated: {
    }

    Component.onCompleted: {
        loadData()
    }

    function loadData() {
        if (shotId > 0)
            MainController.shotHistory.requestShot(shotId)

        MainController.shotHistory.requestAutoFavoriteGroupDetails(
            groupBy, beanBrand, beanType, profileName, grinderBrand, grinderModel, grinderSetting,
            doseBucket, targetWeight)
    }

    // Handle async shot data and group details
    Connections {
        target: MainController.shotHistory
        function onShotReady(id, shot) {
            if (id !== autoFavoriteInfoPage.shotId) return
            autoFavoriteInfoPage.shotData = shot
            Qt.callLater(function() { (scrollView.contentItem as Flickable).returnToBounds() })
        }
        function onAutoFavoriteGroupDetailsReady(details) {
            autoFavoriteInfoPage.groupDetails = details
        }
    }

    // Helper properties for conditional display
    property bool _hasBean: !!(beanBrand || beanType)
    property bool _hasProfile: !!(profileName && profileName.length > 0)
    property bool _hasGrinder: !!(grinderBrand || grinderModel || grinderSetting)
    property string _beanText: {
        var parts = []
        if (beanBrand) parts.push(beanBrand)
        if (beanType) parts.push(beanType)
        return parts.join(" - ")
    }
    property string _grinderText: {
        var name = ((grinderBrand || "") + " " + (grinderModel || "")).trim()
        return name + (grinderSetting ? " @ " + grinderSetting : "")
    }
    property var _notes: groupDetails.notes || []
    property bool _hasRoastDate: !!(shotData.roastDate && shotData.roastDate !== "")
    property bool _hasRoastLevel: !!(shotData.roastLevel && shotData.roastLevel !== "")
    property bool _hasBeanCardData: _hasBean || _hasRoastDate || _hasRoastLevel || _hasGrinder

    ScrollView {
        id: scrollView
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
                        text: autoFavoriteInfoPage._beanText
                        font: Theme.titleFont
                        color: Theme.textColor
                        visible: autoFavoriteInfoPage._hasBean
                        width: Math.min(implicitWidth, parent.width)
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }

                    Text {
                        text: "  ·  "
                        // Bold separator dot (sub-properties, not font: + font.bold — QML gotcha)
                        font.family: Theme.titleFont.family
                        font.pixelSize: Theme.titleFont.pixelSize
                        font.bold: true
                        color: Theme.textSecondaryColor
                        visible: autoFavoriteInfoPage._hasBean && autoFavoriteInfoPage._hasProfile
                        Accessible.ignored: true
                    }

                    Text {
                        text: autoFavoriteInfoPage.profileName || ""
                        font: Theme.titleFont
                        color: Theme.primaryColor
                        visible: autoFavoriteInfoPage._hasProfile
                        width: Math.min(implicitWidth, parent.width)
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }

                    Text {
                        text: "  ·  "
                        // Bold separator dot (sub-properties, not font: + font.bold — QML gotcha)
                        font.family: Theme.titleFont.family
                        font.pixelSize: Theme.titleFont.pixelSize
                        font.bold: true
                        color: Theme.textSecondaryColor
                        visible: autoFavoriteInfoPage._hasGrinder && (autoFavoriteInfoPage._hasBean || autoFavoriteInfoPage._hasProfile)
                        Accessible.ignored: true
                    }

                    Text {
                        text: autoFavoriteInfoPage._grinderText
                        font: Theme.titleFont
                        color: Theme.textSecondaryColor
                        visible: autoFavoriteInfoPage._hasGrinder
                        width: Math.min(implicitWidth, parent.width)
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                }

                Text {
                    text: autoFavoriteInfoPage.shotCount + " " +
                          TranslationManager.translate("autofavorites.shots", "shots")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }
            }

            // Graph inspect bar
            GraphInspectBar { graph: shotGraph; visible: autoFavoriteInfoPage.shotId > 0 }

            // Shot graph (most recent shot)
            Rectangle {
                id: graphCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.scaled(100), Math.min(Theme.scaled(400), autoFavoriteInfoPage.graphHeight))
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                clip: true
                visible: autoFavoriteInfoPage.shotId > 0

                Accessible.role: Accessible.Graphic
                Accessible.name: TranslationManager.translate("autofavoriteinfo.graph", "Most recent shot graph")
                Accessible.focusable: true
                Accessible.onPressAction: graphTapArea.clicked(null)

                HistoryShotGraph {
                    id: shotGraph
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall + resizeHandle.height
                    pressureData: autoFavoriteInfoPage.shotData.pressure || []
                    flowData: autoFavoriteInfoPage.shotData.flow || []
                    temperatureData: autoFavoriteInfoPage.shotData.temperature || []
                    weightData: autoFavoriteInfoPage.shotData.weight || []
                    weightFlowRateData: autoFavoriteInfoPage.shotData.weightFlowRate || []
                    resistanceData: autoFavoriteInfoPage.shotData.resistance || []
                    pressureGoalData: autoFavoriteInfoPage.shotData.pressureGoal || []
                    flowGoalData: autoFavoriteInfoPage.shotData.flowGoal || []
                    temperatureGoalData: autoFavoriteInfoPage.shotData.temperatureGoal || []
                    temperatureMixData: autoFavoriteInfoPage.shotData.temperatureMix || []
                    temperatureMixGoalData: autoFavoriteInfoPage.shotData.temperatureMixGoal || []
                    phaseMarkers: autoFavoriteInfoPage.shotData.phases || []
                    maxTime: autoFavoriteInfoPage.shotData.durationSec || 60
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
                            Qt.callLater(function() { (scrollView.contentItem as Flickable).returnToBounds() })
                        }
                    }
                }
            }

            // Graph legend
            GraphLegend { graph: shotGraph; visible: autoFavoriteInfoPage.shotId > 0 }

            // Metrics row: Avg Duration, Avg Dose, Avg Yield, Avg Rating
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: (autoFavoriteInfoPage.groupDetails.avgDuration || 0) > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgduration", "Avg Duration") + ": " +
                        (autoFavoriteInfoPage.groupDetails.avgDuration || 0).toFixed(1) + "s"
                    Tr {
                        key: "autofavoriteinfo.avgduration"
                        fallback: "Avg Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (autoFavoriteInfoPage.groupDetails.avgDuration || 0).toFixed(1) + "s"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: (autoFavoriteInfoPage.groupDetails.avgDose || 0) > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgdose", "Avg Dose") + ": " +
                        (autoFavoriteInfoPage.groupDetails.avgDose || 0).toFixed(1) + "g"
                    Tr {
                        key: "autofavoriteinfo.avgdose"
                        fallback: "Avg Dose"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (autoFavoriteInfoPage.groupDetails.avgDose || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeDoseColor
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: (autoFavoriteInfoPage.groupDetails.avgYield || 0) > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgyield", "Avg Yield") + ": " +
                        (autoFavoriteInfoPage.groupDetails.avgYield || 0).toFixed(1) + "g"
                    Tr {
                        key: "autofavoriteinfo.avgyield"
                        fallback: "Avg Yield"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: (autoFavoriteInfoPage.groupDetails.avgYield || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeOutputColor
                        Accessible.ignored: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(2)
                    visible: autoFavoriteInfoPage.avgEnjoyment > 0
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("autofavoriteinfo.avgrating", "Avg Rating") + ": " +
                        autoFavoriteInfoPage.avgEnjoyment + "%"
                    Tr {
                        key: "autofavoriteinfo.avgrating"
                        fallback: "Avg Rating"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.ignored: true
                    }
                    Text {
                        text: autoFavoriteInfoPage.avgEnjoyment + "%"
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
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                visible: (autoFavoriteInfoPage.groupDetails.avgTds || 0) > 0 || (autoFavoriteInfoPage.groupDetails.avgEy || 0) > 0
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
                            visible: (autoFavoriteInfoPage.groupDetails.avgTds || 0) > 0
                            spacing: Theme.scaled(2)
                            Tr { key: "autofavoriteinfo.avgtds"; fallback: "Avg TDS"; font: Theme.captionFont; color: Theme.textSecondaryColor; Accessible.ignored: true }
                            Text { text: (autoFavoriteInfoPage.groupDetails.avgTds || 0).toFixed(2) + "%"; font: Theme.bodyFont; color: Theme.dyeTdsColor; Accessible.ignored: true }
                        }

                        ColumnLayout {
                            visible: (autoFavoriteInfoPage.groupDetails.avgEy || 0) > 0
                            spacing: Theme.scaled(2)
                            Tr { key: "autofavoriteinfo.avgey"; fallback: "Avg EY"; font: Theme.captionFont; color: Theme.textSecondaryColor; Accessible.ignored: true }
                            Text { text: (autoFavoriteInfoPage.groupDetails.avgEy || 0).toFixed(1) + "%"; font: Theme.bodyFont; color: Theme.dyeEyColor; Accessible.ignored: true }
                        }
                    }
                }
            }

            // Notes section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: autoFavoriteInfoPage._notes.length > 0

                Tr {
                    key: "shotdetail.notes"
                    fallback: "Notes"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                Repeater {
                    model: autoFavoriteInfoPage._notes

                    ColumnLayout {
                        id: noteEntry
                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: noteEntry.modelData.dateTime || ""
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            Accessible.ignored: true
                        }

                        ExpandableTextArea {
                            Layout.fillWidth: true
                            inlineHeight: Theme.scaled(100)
                            fitContent: true
                            text: noteEntry.modelData.text || ""
                            accessibleName: TranslationManager.translate("shotdetail.notes", "Notes") +
                                " " + (noteEntry.modelData.dateTime || "")
                            textFont: Theme.bodyFont
                            readOnly: true
                        }
                    }
                }
            }

            // Bean & Grinder info card
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: beanColumn.height + Theme.spacingMedium
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius
                visible: autoFavoriteInfoPage._hasBeanCardData
                Accessible.role: Accessible.Grouping
                Accessible.name: TranslationManager.translate("autofavoriteinfo.beanandgrinder", "Beans & Grinder")

                ColumnLayout {
                    id: beanColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSmall
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: Theme.scaled(2)

                    GridLayout {
                        columns: 2
                        columnSpacing: Theme.spacingLarge
                        rowSpacing: Theme.scaled(2)
                        Layout.fillWidth: true

                        // Bean info
                        Tr { key: "shotdetail.roaster"; fallback: "Roaster:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: autoFavoriteInfoPage.beanBrand !== ""; Accessible.ignored: true }
                        Text { textFormat: Text.StyledText; text: Theme.replaceEmojiWithImg(autoFavoriteInfoPage.beanBrand, Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: autoFavoriteInfoPage.beanBrand !== ""; Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                        Tr { key: "shotdetail.coffee"; fallback: "Coffee:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: autoFavoriteInfoPage.beanType !== ""; Accessible.ignored: true }
                        Text { textFormat: Text.StyledText; text: Theme.replaceEmojiWithImg(autoFavoriteInfoPage.beanType, Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: autoFavoriteInfoPage.beanType !== ""; Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                        Tr { key: "shotdetail.roastdate"; fallback: "Roast Date:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: autoFavoriteInfoPage._hasRoastDate; Accessible.ignored: true }
                        Text { textFormat: Text.StyledText; text: Theme.replaceEmojiWithImg(autoFavoriteInfoPage.shotData.roastDate || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: autoFavoriteInfoPage._hasRoastDate; Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                        Tr { key: "shotdetail.roastlevel"; fallback: "Roast Level:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: autoFavoriteInfoPage._hasRoastLevel; Accessible.ignored: true }
                        Text { textFormat: Text.StyledText; text: Theme.replaceEmojiWithImg(autoFavoriteInfoPage.shotData.roastLevel || "", Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: autoFavoriteInfoPage._hasRoastLevel; Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                        // Grinder info
                        Tr { key: "shotdetail.grinder"; fallback: "Grinder:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: autoFavoriteInfoPage.grinderBrand !== "" || autoFavoriteInfoPage.grinderModel !== ""; Accessible.ignored: true }
                        Text { textFormat: Text.StyledText; text: Theme.replaceEmojiWithImg(((autoFavoriteInfoPage.grinderBrand || "") + " " + (autoFavoriteInfoPage.grinderModel || "")).trim(), Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: autoFavoriteInfoPage.grinderBrand !== "" || autoFavoriteInfoPage.grinderModel !== ""; Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }

                        Tr { key: "shotdetail.grindersetting"; fallback: "Grind Setting:"; font: Theme.labelFont; color: Theme.textSecondaryColor; visible: autoFavoriteInfoPage.grinderSetting !== ""; Accessible.ignored: true }
                        Text { textFormat: Text.StyledText; text: Theme.replaceEmojiWithImg(autoFavoriteInfoPage.grinderSetting, Theme.labelFont.pixelSize); font: Theme.labelFont; color: Theme.textColor; visible: autoFavoriteInfoPage.grinderSetting !== ""; Layout.fillWidth: true; elide: Text.ElideRight; Accessible.ignored: true }
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
        onBackClicked: AppShell.backRequested()
    }
}
