import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    // Read configuration from layout properties, refreshed on any layout change.
    property bool showProfile: true
    property bool showRoaster: true
    property bool showGrind: true
    property bool showRoastDate: false
    property bool showDoseYield: true

    function refreshProps() {
        if (!itemId) return
        var p = Settings.getItemProperties(itemId)
        showProfile = p.shotPlanShowProfile !== false
        showRoaster = p.shotPlanShowRoaster !== false
        showGrind = p.shotPlanShowGrind !== false
        showRoastDate = p.shotPlanShowRoastDate === true
        showDoseYield = p.shotPlanShowDoseYield !== false
    }

    onItemIdChanged: refreshProps()

    Connections {
        target: Settings
        function onLayoutConfigurationChanged() { root.refreshProps() }
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: {
        var plan = compactShotPlan.text || fullShotPlan.text || ""
        return plan ? "Shot plan: " + plan + ". Tap to edit" : "Shot plan"
    }
    Accessible.focusable: true

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactShotPlan.implicitWidth
        implicitHeight: compactShotPlan.implicitHeight

        ShotPlanText {
            id: compactShotPlan
            anchors.centerIn: parent
            visible: text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: brewDialog.open()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullShotPlan.implicitWidth
        implicitHeight: fullShotPlan.implicitHeight

        ShotPlanText {
            id: fullShotPlan
            anchors.centerIn: parent
            visible: text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: brewDialog.open()
        }
    }

    BrewDialog {
        id: brewDialog
    }
}
