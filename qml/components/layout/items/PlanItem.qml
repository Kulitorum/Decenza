import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

// A page-aware "plan" widget: shows the brew/shot plan on the home and espresso
// pages, and the steam plan on the steam page. Drop one in the persistent status bar
// and it reads correctly for whatever page you're on. Page detection uses
// Window.window.currentPageObjectName (a root property on the ApplicationWindow)
// rather than the pageStack id, which a separately-loaded widget cannot see by scope.
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

    // Page detection, mirrored imperatively from the window root: seed on completion / when the
    // window resolves, and refresh on the window's currentPageObjectNameChanged /
    // currentOperationModeChanged signals so it stays live across page changes.
    readonly property var winRoot: root.Window.window
    property string _pageName: ""
    property string _opMode: ""
    function _refreshPage() {
        root._pageName = winRoot ? (winRoot.currentPageObjectName || "") : ""
        root._opMode = winRoot ? (winRoot.currentOperationMode || "") : ""
    }
    onWinRootChanged: _refreshPage()
    Component.onCompleted: _refreshPage()
    Connections {
        target: root.winRoot
        ignoreUnknownSignals: true
        function onCurrentPageObjectNameChanged() { root._refreshPage() }
        function onCurrentOperationModeChanged() { root._refreshPage() }
    }
    // Steam context = steam selected on the idle screen, OR the full steam page, OR the machine actively
    // steaming. MachineState.phase is a singleton property so it's read directly; the window-root strings
    // are mirrored above (seeded on resolve, refreshed on their change signals — rename-fragile: a rename
    // of the mirrored main.qml root properties silently freezes this on its last value).
    readonly property bool _onSteamPage: _opMode === "steam"
        || _pageName === "steamPage"
        || (typeof MachineState !== "undefined" && MachineState.phase === MachineStateType.Phase.Steaming)

    function openBrewSettings() {
        var win = root.Window.window
        if (win && win.openBrewSettings) win.openBrewSettings()
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // On the steam page the widget is a read-only summary; elsewhere it opens Brew
    // Settings on tap, so it's an activatable button there.
    Accessible.role: root._onSteamPage ? Accessible.StaticText : Accessible.Button
    Accessible.name: {
        if (root._onSteamPage) {
            var sp = compactSteamPlan.text || fullSteamPlan.text || ""
            return sp ? TranslationManager.translate("plan.a11y.steamPlan", "Steam plan: %1").arg(sp)
                      : TranslationManager.translate("plan.a11y.steamPlanEmpty", "Steam plan")
        }
        var plan = compactShotPlan.text || fullShotPlan.text || ""
        return plan ? TranslationManager.translate("plan.a11y.shotPlan", "Shot plan: %1. Tap to edit").arg(plan)
                    : TranslationManager.translate("plan.a11y.shotPlanEmpty", "Shot plan")
    }
    Accessible.focusable: true
    Accessible.onPressAction: if (!root._onSteamPage) root.openBrewSettings()

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: root._onSteamPage ? compactSteamPlan.implicitWidth : compactShotPlan.implicitWidth
        implicitHeight: root._onSteamPage ? compactSteamPlan.implicitHeight : compactShotPlan.implicitHeight

        ShotPlanText {
            id: compactShotPlan
            anchors.centerIn: parent
            visible: !root._onSteamPage && text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }
        SteamPlanText {
            id: compactSteamPlan
            anchors.centerIn: parent
            visible: root._onSteamPage && text !== ""
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: root._onSteamPage ? fullSteamPlan.implicitWidth : fullShotPlan.implicitWidth
        implicitHeight: root._onSteamPage ? fullSteamPlan.implicitHeight : fullShotPlan.implicitHeight

        ShotPlanText {
            id: fullShotPlan
            anchors.centerIn: parent
            visible: !root._onSteamPage && text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }
        SteamPlanText {
            id: fullSteamPlan
            anchors.centerIn: parent
            visible: root._onSteamPage && text !== ""
        }
    }
}
