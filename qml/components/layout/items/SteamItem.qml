// `layer.effect` declares an inline component, so this file's ids are not statically
// resolvable inside it without this pragma. No delegate in this file takes an injected
// model role, so no `required property` is needed -- see ThemedIcon.qml for the same case.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza

LayoutWidgetItem {
    id: root

    // `Window` is an ATTACHED property: it resolves against the current scope, so it is read here
    // on the item itself rather than as `root.appWindow` from inside the popup below. That
    // spelling works at runtime but is an attached lookup through an id, which qmllint cannot see
    // — it reports `Member "Window" not found on type "SteamItem"`. Reading it once also removes the
    // duplicate lookups.
    readonly property var appWindow: Window.window

    // See EspressoItem.qml for rationale.
    readonly property bool canStartOperations: DE1Device.isHeadless || DE1Device.simulationMode

    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    // Highlight this button while its mode is selected on the home screen (the
    // centre preset row is expanded), or — in compact mode, where tapping opens
    // presetPopup instead of setting activePresetFunction — while its popup is open.
    readonly property bool isActive:
        (idlePage ? idlePage.activePresetFunction : "") === "steam" || presetPopup.visible

    // Compact (bar) rendering only: full-size placements of this type compile to
    // CustomItem in LayoutItemDelegate (isCompiled), so this item never loads
    // non-compact and carries no full-mode rendering.
    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function togglePresets() {
        if (root.isCompact) {
            presetPopup.visible ? presetPopup.close() : presetPopup.open()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "steam") ? "" : "steam"
        }
    }

    function goToSteam() {
            AppShell.steamRequested()
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactSteamRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactSteamRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/steam.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                opacity: DE1Device.guiEnabled ? 1.0 : 0.5
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.isActive ? Theme.accentColor : Theme.textColor
                }
            }
            Tr {
                key: "idle.button.steam"
                fallback: "Steam"
                font: Theme.bodyFont
                color: !DE1Device.guiEnabled ? Theme.textSecondaryColor
                       : (root.isActive ? Theme.accentColor : Theme.textColor)
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            enabled: DE1Device.guiEnabled
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.steam", "Steam")
                            + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: TranslationManager.translate("idle.accessible.steam.hint", "Tap to toggle presets. Double-tap or long-press to configure steam.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: LayoutActions.runGestureOrReserved(root.modelData, "doubleclickAction", "steam", { idlePage: root.idlePage })
            onAccessibleLongPressed: LayoutActions.runGestureOrReserved(root.modelData, "longPressAction", "steam", { idlePage: root.idlePage })
        }
    }

    // --- PRESET POPUP ---
    // Dialog (not Popup) so TalkBack/VoiceOver scope traversal to the pill list,
    // matching BeansItem/EquipmentItem/RecipesItem: Dialog carries the accessible
    // dialog role that screen readers use to trap focus, which `Popup { modal }`
    // alone (already set below) does not provide. header/footer null strip the
    // Dialog chrome so it still renders as the same bare dropdown.
    DecenzaDialog {
        id: presetPopup
        modal: true
        dim: false
        header: null
        footer: null
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        onClosed: { if (root.idlePage) root.idlePage.releasePanelClearance() }
        onOpened: {
            if (root.idlePage) {
                var rootTopInPage = root.mapToItem(root.idlePage, 0, 0).y
                root.idlePage.requestPanelClearance(rootTopInPage + presetPopup.y, presetPopup.height)
            }
            if (typeof MachineState !== "undefined" && MachineState !== null) MachineState.tareScale()

            // Full-mode steam path runs IdlePage.onActivePresetFunctionChanged which
            // announces the preset list to TalkBack. The compact-mode popup bypasses
            // that path, so announce here directly to keep feature parity.
            if (typeof AccessibilityManager === "undefined" || AccessibilityManager === null || !AccessibilityManager.enabled) return
            var presets = Settings.brew.steamPitcherPresets
            var names = []
            for (var i = 0; i < presets.length; ++i) {
                names.push(SteamLabels.pitcherName(presets[i]))
            }
            // Resolve through the helper, not by position: the built-in
            // "Heater off" pitcher is stored as a sentinel, so a `>= 0` index
            // test announced an empty name for a perfectly valid selection.
            // Same fix as the full-mode path in IdlePage.
            var selectedName = SteamLabels.pitcherName(
                Settings.brew.getSteamPitcherPreset(Settings.brew.selectedSteamPitcher))
            var announcement = presets.length + " " + TranslationManager.translate("idle.accessible.presets", "presets") + ": " + names.join(", ")
            if (selectedName !== "") {
                announcement += ". " + selectedName + " " + TranslationManager.translate("idle.accessible.isSelected", "is selected")
            }
            AccessibilityManager.announce(announcement)
        }

        width: {
            var win = root.appWindow
            var w = Theme.scaled(600) + 2 * padding
            return win ? Math.min(w, win.width) : w
        }

        y: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.appWindow
            if (win) {
                var globalY = root.mapToItem(null, 0, 0).y
                var spaceBelow = win.height - globalY - root.height - Theme.spacingSmall
                var spaceAbove = globalY - Theme.spacingSmall
                if (height > spaceBelow && spaceAbove > spaceBelow)
                    return -height - Theme.spacingSmall
            }
            return parent.height + Theme.spacingSmall
        }

        x: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.appWindow
            if (win) {
                var globalX = root.mapToItem(null, 0, 0).x
                var centered = -width / 2 + parent.width / 2
                if (globalX + centered + width > win.width)
                    centered = win.width - globalX - width
                if (globalX + centered < 0)
                    centered = -globalX
                return centered
            }
            return -width / 2 + parent.width / 2
        }

        background: Rectangle {
            // With the glass chrome on, float the pills directly (matching
            // the center inline preset rows) instead of showing a panel; keep the
            // opaque surface panel when the glass chrome is off.
            readonly property bool hasGlassChrome: Theme.glassChrome
            color: hasGlassChrome ? "transparent" : Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: hasGlassChrome ? 0 : 1
        }

        contentItem: Item {
            id: popupContent

            implicitWidth: popupPillRow.implicitWidth
            implicitHeight: popupPillRow.implicitHeight

            // Track scale weight to refresh pill suffix
            property int popupSuffixVersion: 0
            Connections {
                target: MachineState
                function onScaleWeightChanged() {
                    if (presetPopup.visible) popupContent.popupSuffixVersion++
                }
            }

            PresetPillRow {
                id: popupPillRow
                maxWidth: Theme.scaled(600)
                presets: Settings.brew.steamPitcherPresets
                selectedIndex: SteamLabels.pitcherDisplayIndex()
                pillSuffixMaxWidth: Theme.scaled(60)
                pillSuffixVersion: parent.popupSuffixVersion

                pillSuffixFn: function(index) { return SteamLabels.pitcherPillSuffix(index) }
                pillLabelFn: function(index, name) { return SteamLabels.pitcherPillLabel(index, name) }

                onPresetSelected: function(index) {
                    var wasAlreadySelected = (index === Settings.brew.selectedSteamPitcher)
                    var preset = Settings.brew.getSteamPitcherPreset(index)
                    // Shared with the idle pill row, the Steam page and MCP; the
                    // only per-surface difference is the milk fallback, which is
                    // why that is the argument. Handles "Heater off" itself.
                    MainController.selectSteamPitcher(index, AppShell.sessionMeasuredMilkG)
                    if (preset && preset.disabled) {
                        presetPopup.close()
                        return   // never start steam by re-tapping Heater off
                    }

                    if (wasAlreadySelected) {
                        if (MachineState.isReady && root.canStartOperations) {
                            DE1Device.startSteam()
                        } else {
                            console.log("Cannot start steam - machine not ready, phase:", MachineState.phase)
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager !== null && AccessibilityManager.enabled)
                                AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                        }
                    }
                    presetPopup.close()
                }
            }
        }
    }
}
