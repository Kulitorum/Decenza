import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    readonly property bool showProfile: modelData.shotPlanShowProfile !== false
    readonly property bool showRoaster: modelData.shotPlanShowRoaster !== false
    readonly property bool showGrind: modelData.shotPlanShowGrind !== false
    readonly property bool showRoastDate: modelData.shotPlanShowRoastDate === true
    readonly property bool showDoseYield: modelData.shotPlanShowDoseYield !== false

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
