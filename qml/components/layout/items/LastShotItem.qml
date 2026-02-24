import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // User-configurable properties (from settings popup)
    readonly property real shotScale: typeof modelData.shotScale === "number" ? modelData.shotScale : 1.0
    readonly property bool shotShowLabels: typeof modelData.shotShowLabels === "boolean" ? modelData.shotShowLabels : false
    readonly property bool shotShowPhaseLabels: typeof modelData.shotShowPhaseLabels === "boolean" ? modelData.shotShowPhaseLabels : true

    // Shot data cache
    property var shotData: ({})
    readonly property bool hasData: !!(shotData && shotData.pressure && shotData.pressure.length > 0)

    property int _pendingShotId: 0

    function loadLastShot() {
        var shotId = MainController.lastSavedShotId
        if (shotId <= 0) {
            // App just started - query most recent from history
            var recent = MainController.shotHistory.getShots(0, 1)
            if (recent.length > 0) shotId = recent[0].id
        }
        if (shotId > 0) {
            _pendingShotId = shotId
            MainController.shotHistory.requestShot(shotId)
        }
    }

    Component.onCompleted: loadLastShot()

    Connections {
        target: MainController
        function onLastSavedShotIdChanged() { root.loadLastShot() }
    }

    // Handle async shot data
    Connections {
        target: MainController.shotHistory
        function onShotReady(shotId, shot) {
            if (shotId !== root._pendingShotId) return
            shotData = shot
        }
    }

    function goToShotDetail() {
        if (hasData && shotData.id && typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/ShotDetailPage.qml"),
                          { shotId: shotData.id })
        }
    }

    // --- COMPACT MODE (bar zones) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: root.hasData ? Theme.bottomBarHeight * 2.5 * root.shotScale : compactPlaceholder.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        Loader {
            anchors.fill: parent
            anchors.margins: Theme.scaled(4)
            active: root.isCompact && root.hasData
            sourceComponent: HistoryShotGraph {
                pressureData: root.shotData.pressure || []
                flowData: root.shotData.flow || []
                temperatureData: root.shotData.temperature || []
                weightData: root.shotData.weight || []
                weightFlowRateData: root.shotData.weightFlowRate || []
                phaseMarkers: root.shotData.phases || []
                maxTime: root.shotData.duration || 60
                showLabels: root.shotShowLabels
                showPhaseLabels: false
            }
        }

        Text {
            id: compactPlaceholder
            anchors.centerIn: parent
            visible: !root.hasData
            text: "No shots"
            color: Theme.textSecondaryColor
            font: Theme.captionFont
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.goToShotDetail()
        }
    }

    // --- FULL MODE (center zones) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(200)
        implicitHeight: Theme.scaled(120)

        Rectangle {
            anchors.fill: parent
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        // Graph fills the card above the metadata bar
        Item {
            anchors.fill: parent
            anchors.margins: 1
            anchors.bottomMargin: root.hasData ? Theme.scaled(20) : 1
            clip: true

            Loader {
                anchors.fill: parent
                active: !root.isCompact && root.hasData
                sourceComponent: HistoryShotGraph {
                    pressureData: root.shotData.pressure || []
                    flowData: root.shotData.flow || []
                    temperatureData: root.shotData.temperature || []
                    weightData: root.shotData.weight || []
                    weightFlowRateData: root.shotData.weightFlowRate || []
                    phaseMarkers: root.shotData.phases || []
                    maxTime: root.shotData.duration || 60
                    showLabels: root.shotShowLabels
                    showPhaseLabels: root.shotShowPhaseLabels
                }
            }
        }

        // Metadata overlay at bottom
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.scaled(20)
            color: Qt.rgba(0, 0, 0, 0.6)
            radius: Theme.cardRadius
            visible: root.hasData

            // Cover top corners so only bottom is rounded
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: parent.radius
                color: parent.color
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(6)
                anchors.rightMargin: Theme.scaled(6)

                Text {
                    text: root.shotData.profileName || ""
                    color: Theme.textColor
                    font: Theme.captionFont
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: (root.shotData.finalWeight || 0).toFixed(1) + "g"
                    color: Theme.weightColor
                    font: Theme.captionFont
                }
                Text {
                    text: Math.round(root.shotData.duration || 0) + "s"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }
        }

        // No-data placeholder
        Column {
            anchors.centerIn: parent
            visible: !root.hasData
            spacing: Theme.scaled(4)

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "qrc:/icons/history.svg"
                sourceSize.height: Theme.scaled(24)
                opacity: 0.5
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "No shots yet"
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.goToShotDetail()
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.hasData
        ? "Last shot: " + (root.shotData.profileName || "")
          + ", " + (root.shotData.finalWeight || 0).toFixed(1) + " grams"
        : "No shots yet"
    Accessible.focusable: true
    Accessible.onPressAction: root.goToShotDetail()
}
