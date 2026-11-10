import QtQuick
import Decenza

// The steam pitcher pills and the milk placement prompt. The Steam widget shows this
// both as the Home row and as its compact-bar popup, so the two cannot drift.
Column {
    id: root

    property real maxWidth: 0
    // The milk-offer owner for this surface, or "" where it offers none.
    property string milkSurface: ""
    readonly property WeightCaptureState milk: PreparationService.state.milk
    // Refreshes the live net-milk pill suffix.
    property int suffixVersion: 0

    // A tap started steam or a purge, rather than selecting a pitcher.
    signal operationStarted()

    spacing: Theme.scaled(8)

    Connections {
        target: MachineState
        function onScaleWeightChanged() { if (root.visible) root.suffixVersion++ }
    }
    Connections {
        target: PreparationService.state
        function onChanged() { if (root.visible) root.suffixVersion++ }
    }
    Connections {
        target: SteamService
        function onStateChanged() { if (root.visible) root.suffixVersion++ }
    }

    PresetPillRow {
        id: pills
        anchors.horizontalCenter: parent.horizontalCenter
        maxWidth: root.maxWidth
        presets: SteamLabels.steamRow(SteamService.state.steamPitcherPresets)
        selectedIndex: SteamService.state.selectedSteamPitcherDisplayIndex
        supportLongPress: true
        pillSuffixMaxWidth: Theme.scaled(60)  // Reserve ~"(1234g)" worth of width
        pillSuffixVersion: root.suffixVersion

        // Only the selected pitcher is on the scale.
        pillSuffixFn: function(preset, index) {
            return index === pills.selectedIndex ? SteamLabels.pitcherPillSuffix(preset) : ""
        }
        pillLabelFn: function(preset, name) { return SteamLabels.pitcherPillLabel(preset, name) }

        onPresetSelected: function(index) {
            if (HomePresetActions.tapSteamRowEntry(index))
                root.operationStarted()
            else if (root.milkSurface !== "")
                // A lift after the capture may have closed the offer: re-offer for the new pitcher.
                HomePresetActions.offerMilkSample(root.milkSurface)
        }

        // Long-press a pitcher to open its settings on the steam page.
        onPresetLongPressed: function(index) {
            if (SteamLabels.isPurgePill(pills.presets[index]))
                return
            HomePresetActions.selectSteamPitcher(index)
            AppShell.steamRequested()
        }
    }

    // "or lift and replace": a scale never seen empty (connected, or tared, with the
    // pitcher already on) takes the pitcher as empty until it is lifted and set back.
    // The beep is promised only when the capture sound is on.
    Text {
        id: placePrompt
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, root.maxWidth)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        visible: root.milk.active && root.milk.seeded
                 && root.milk.rawWeight - root.milk.virtualZero < 50
        text: TranslationManager.translate("idle.label.placeOrReplacePitcher", "Place (or lift and replace) the milk pitcher on the scale") + "\n"
            + (RecipeContext.preferences.doseCaptureSoundEnabled
                ? TranslationManager.translate("idle.label.placePitcherHint", "(and wait for the beep before removing)")
                : TranslationManager.translate("idle.label.placeHintNoSound", "(hold still until the weight registers)"))
        color: Theme.textSecondaryColor
        font: Theme.labelFont
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        PromptPulse on opacity {
            running: placePrompt.visible
        }
    }
}
