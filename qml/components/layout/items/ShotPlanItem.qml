import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

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
            onClicked: brewDialog.open()
        }
    }

    BrewDialog {
        id: brewDialog
    }
}
