import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    readonly property bool showProfile: modelData.shotPlanShowProfile !== false
    readonly property bool showRoaster: modelData.shotPlanShowRoaster !== false
    readonly property bool showCoffee: modelData.shotPlanShowCoffee !== false
    readonly property bool showGrind: modelData.shotPlanShowGrind !== false
    readonly property bool showRoastDate: modelData.shotPlanShowRoastDate === true
    readonly property bool showDoseYield: modelData.shotPlanShowDoseYield !== false

    // Open the single global Brew Settings dialog (hosted at the app root) via the
    // window, so this works wherever the tile is placed — including the persistent
    // status bar, which is not a descendant of IdlePage.
    function openBrewSettings() {
        var win = root.Window.window
        if (win && win.openBrewSettings) win.openBrewSettings()
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.Button
    Accessible.name: {
        var plan = compactShotPlan.text || fullShotPlan.text || ""
        return plan ? TranslationManager.translate("plan.a11y.shotPlan", "Shot plan: %1. Tap to edit").arg(plan)
                    : TranslationManager.translate("plan.a11y.shotPlanEmpty", "Shot plan")
    }
    Accessible.focusable: true
    Accessible.onPressAction: root.openBrewSettings()

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
            showCoffee: root.showCoffee
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
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
            showCoffee: root.showCoffee
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }
    }
}
