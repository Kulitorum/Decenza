import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

// The Shot Plan widget — page-aware: shows the brew/shot plan normally and, unless the
// per-instance "Steam plan" option is off, the steam plan while in steam context (steam
// selected on the idle screen, the steam page, or actively steaming). Page/mode state
// comes from the Theme singleton (a separately-loaded widget cannot see the pageStack id
// by scope; singleton properties bind reactively).
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
    readonly property bool showSteamPlan: modelData.shotPlanShowSteamPlan !== false

    // Steam context = steam selected on the idle screen, OR the full steam page, OR the
    // machine actively steaming. Theme.currentPageObjectName (set by main.qml's
    // page-change handler) and Theme.currentOperationMode (published by IdlePage) are
    // singleton properties, so these are plain reactive bindings.
    readonly property bool _steamContext: showSteamPlan && (
        Theme.currentOperationMode === "steam"
        || Theme.currentPageObjectName === "steamPage"
        || (typeof MachineState !== "undefined" && MachineState.phase === MachineStateType.Phase.Steaming))
    // Only actually swap when the steam plan has something to say — with the "Off"
    // pitcher (or a stale preset index) its text is empty, and swapping then would blank
    // the whole widget while leaving a phantom focusable a11y node. Fall back to the
    // shot plan instead. (Both SteamPlanText instances bind identically, so either
    // text suffices; check both for safety.)
    readonly property bool _steamMode: _steamContext
        && (compactSteamPlan.text !== "" || fullSteamPlan.text !== "")

    // Open the single global Brew Settings dialog (hosted at the app root) via the
    // window, so this works wherever the tile is placed — including the persistent
    // status bar, which is not a descendant of IdlePage.
    function openBrewSettings() {
        var win = root.Window.window
        if (win && win.openBrewSettings) win.openBrewSettings()
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // When showing the steam plan the widget is a read-only summary; otherwise it opens
    // Brew Settings on tap, so it's an activatable button.
    Accessible.role: root._steamMode ? Accessible.StaticText : Accessible.Button
    Accessible.name: {
        if (root._steamMode) {
            var sp = compactSteamPlan.text || fullSteamPlan.text || ""
            return sp ? TranslationManager.translate("plan.a11y.steamPlan", "Steam plan: %1").arg(sp)
                      : TranslationManager.translate("plan.a11y.steamPlanEmpty", "Steam plan")
        }
        var plan = compactShotPlan.text || fullShotPlan.text || ""
        return plan ? TranslationManager.translate("plan.a11y.shotPlan", "Shot plan: %1. Tap to edit").arg(plan)
                    : TranslationManager.translate("plan.a11y.shotPlanEmpty", "Shot plan")
    }
    Accessible.focusable: true
    Accessible.onPressAction: if (!root._steamMode) root.openBrewSettings()

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: root._steamMode ? compactSteamPlan.implicitWidth : compactShotPlan.implicitWidth
        implicitHeight: root._steamMode ? compactSteamPlan.implicitHeight : compactShotPlan.implicitHeight

        ShotPlanText {
            id: compactShotPlan
            anchors.centerIn: parent
            visible: !root._steamMode && text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showCoffee: root.showCoffee
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }
        SteamPlanText {
            id: compactSteamPlan
            anchors.centerIn: parent
            visible: root._steamMode && text !== ""
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: root._steamMode ? fullSteamPlan.implicitWidth : fullShotPlan.implicitWidth
        implicitHeight: root._steamMode ? fullSteamPlan.implicitHeight : fullShotPlan.implicitHeight

        ShotPlanText {
            id: fullShotPlan
            anchors.centerIn: parent
            visible: !root._steamMode && text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showCoffee: root.showCoffee
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }
        SteamPlanText {
            id: fullSteamPlan
            anchors.centerIn: parent
            visible: root._steamMode && text !== ""
        }
    }
}
