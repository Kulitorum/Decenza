import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."
import "../ShotPlanConfig.js" as ShotPlanConfig

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

    // Ordered display items. New configs store `shotPlanItems` (a JSON array of
    // item keys — order is display order); configs saved before the chip editor
    // have at most the legacy shotPlanShow* booleans. The resolution rule (incl.
    // the legacy compound Profile & temperature boolean expanding to the two
    // independent items) is shared with the editor via ShotPlanConfig.js.
    readonly property var itemOrder: ShotPlanConfig.itemsFor(modelData)
    readonly property bool sentence: modelData.shotPlanSentence !== false
    // Stacked details (sentence mode only): the tail renders on its own
    // line(s) below the sentence. Compact bar placements ignore it — a bar is
    // a single-line context.
    readonly property bool stacked: modelData.shotPlanStacked === true
    // Yield: show only the effective target (drop the "profileDefault →" arrow).
    readonly property bool yieldTargetOnly: modelData.shotPlanYieldTargetOnly === true
    readonly property bool showSteamPlan: modelData.shotPlanShowSteamPlan !== false

    // When a recipe is active its own yield/temp are the BASELINE, not overrides
    // of the profile (recipe-baseline-not-override, #1485): pass them to the
    // ShotPlanText so a recipe's designed yield shows as a plain target ("40.0g",
    // not "36.0 → 40.0g") and neither yield nor temp is tinted at the recipe's
    // own values. 0 when no recipe (or the recipe pins none) → profile behavior.
    readonly property double _recipeBaselineYield:
        (Settings.dye.activeRecipeId >= 0 && MainController.activeRecipe.yieldG > 0)
            ? MainController.activeRecipe.yieldG : 0
    readonly property double _recipeBaselineTemp:
        (Settings.dye.activeRecipeId >= 0
         && ProfileManager.profileTargetTemperature > 0
         && Math.abs(MainController.activeRecipe.tempOffsetC || 0) > 0.05)
            ? ProfileManager.profileTargetTemperature + MainController.activeRecipe.tempOffsetC : 0

    // Steam context = steam selected on the idle screen, OR the full steam page, OR the
    // machine actively steaming. Theme.currentPageObjectName (set by main.qml's
    // page-change handler) and Theme.currentOperationMode (published by IdlePage) are
    // singleton properties, so these are plain reactive bindings.
    readonly property bool _steamContext: showSteamPlan && (
        Theme.currentOperationMode === "steam"
        || Theme.currentPageObjectName === "steamPage"
        || (typeof MachineState !== "undefined" && MachineState.phase === MachineStateType.Phase.Steaming))
    // Only actually swap when the steam plan has something to say — with the "Off"
    // pitcher its text is empty, and swapping then would blank the whole widget while
    // leaving a phantom focusable a11y node. Fall back to the shot plan instead.
    // (A stale preset index is NOT guaranteed empty: a remembered lastSteamMilkG still
    // renders a milk-only fragment. Both SteamPlanText instances bind identically, so
    // either text suffices; check both for safety.)
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
            // Never wider than the zone grants — the text inside wraps/elides
            // instead of painting past the widget bounds. Bars are single-line.
            width: Math.min(implicitWidth, parent.width)
            visible: !root._steamMode && text !== ""
            itemOrder: root.itemOrder
            sentence: root.sentence
            yieldTargetOnly: root.yieldTargetOnly
            recipeBaselineYield: root._recipeBaselineYield
            recipeBaselineTemp: root._recipeBaselineTemp
            maxLines: 1
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
            // Never wider than the zone grants; the full widget has vertical
            // room in its normal (center-zone) position, so allow a second line
            // before eliding.
            width: Math.min(implicitWidth, parent.width)
            visible: !root._steamMode && text !== ""
            itemOrder: root.itemOrder
            sentence: root.sentence
            yieldTargetOnly: root.yieldTargetOnly
            recipeBaselineYield: root._recipeBaselineYield
            recipeBaselineTemp: root._recipeBaselineTemp
            stacked: root.stacked
            // Stacked spends a line on the detail tail — give the sentence +
            // wrapped tail room before eliding. Gated on sentence so a stale
            // stacked flag (saved on, Sentence later turned off) doesn't widen
            // fragment mode's budget. (The profile-less recipe sentence needs
            // this extra line too — its detail tail stacks the same way.)
            maxLines: root.stacked && root.sentence ? 3 : 2
            onClicked: root.openBrewSettings()
        }
        SteamPlanText {
            id: fullSteamPlan
            anchors.centerIn: parent
            visible: root._steamMode && text !== ""
        }
    }
}
