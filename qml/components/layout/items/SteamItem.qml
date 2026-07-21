import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

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
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/SteamPage.qml"))
        }
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
            onAccessibleDoubleClicked: root.goToSteam()
            onAccessibleLongPressed: root.goToSteam()
        }
    }

    // --- PRESET POPUP ---
    Popup {
        id: presetPopup
        modal: true
        dim: false
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        onClosed: { if (root.idlePage) root.idlePage.releasePanelClearance() }
        onOpened: {
            if (root.idlePage) {
                var rootTopInPage = root.mapToItem(root.idlePage, 0, 0).y
                root.idlePage.requestPanelClearance(rootTopInPage + presetPopup.y, presetPopup.height)
            }
            if (typeof MachineState !== "undefined") MachineState.tareScale()

            // Full-mode steam path runs IdlePage.onActivePresetFunctionChanged which
            // announces the preset list to TalkBack. The compact-mode popup bypasses
            // that path, so announce here directly to keep feature parity.
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var presets = Settings.brew.steamPitcherPresets
            if (presets.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < presets.length; ++i) {
                names.push(presets[i].name)
            }
            if (Settings.brew.selectedSteamPitcher >= 0 && Settings.brew.selectedSteamPitcher < presets.length) {
                selectedName = presets[Settings.brew.selectedSteamPitcher].name
            }
            var announcement = presets.length + " " + TranslationManager.translate("idle.accessible.presets", "presets") + ": " + names.join(", ")
            if (selectedName !== "") {
                announcement += ". " + selectedName + " " + TranslationManager.translate("idle.accessible.isSelected", "is selected")
            }
            AccessibilityManager.announce(announcement)
        }

        width: {
            var win = root.Window.window
            var w = Theme.scaled(600) + 2 * padding
            return win ? Math.min(w, win.width) : w
        }

        y: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
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
            var win = root.Window.window
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
            // Over a custom background image, float the pills directly (matching
            // the center inline preset rows) instead of showing a panel; keep the
            // opaque surface panel when no background image is set.
            readonly property bool hasBackgroundImage: Settings.theme.backgroundImagePath.length > 0
            color: hasBackgroundImage ? "transparent" : Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: hasBackgroundImage ? 0 : 1
        }

        contentItem: Item {
            implicitWidth: popupPillRow.implicitWidth
            implicitHeight: popupPillRow.implicitHeight

            // Track scale weight to refresh pill suffix
            property int popupSuffixVersion: 0
            Connections {
                target: MachineState
                function onScaleWeightChanged() {
                    if (presetPopup.visible) popupPillRow.parent.popupSuffixVersion++
                }
            }

            PresetPillRow {
                id: popupPillRow
                maxWidth: Theme.scaled(600)
                presets: Settings.brew.steamPitcherPresets
                selectedIndex: Settings.brew.selectedSteamPitcher
                pillSuffixMaxWidth: Theme.scaled(60)
                pillSuffixVersion: parent.popupSuffixVersion

                // Live net-milk suffix — twin of the idle steam pill row's pillSuffixFn
                // in IdlePage.qml (rationale documented there); keep in sync.
                pillSuffixFn: function(index) {
                    if (!ScaleDevice.connected || ScaleDevice.isFlowScale) return ""
                    var preset = Settings.brew.steamPitcherPresets[index]
                    if (!preset || preset.disabled) return ""
                    var pitcherWeight = preset.pitcherWeightG ?? 0
                    if (pitcherWeight <= 0) return ""
                    var milkWeight = Math.max(0, MachineState.scaleWeight - pitcherWeight)
                    return " (" + Math.round(milkWeight) + "g)"
                }

                onPresetSelected: function(index) {
                    var wasAlreadySelected = (index === Settings.brew.selectedSteamPitcher)
                    Settings.brew.selectedSteamPitcher = index
                    var preset = Settings.brew.getSteamPitcherPreset(index)
                    if (preset && preset.disabled) {
                        // "Off" preset — disable the steam heater. Don't write
                        // undefined preset.duration/flow into int Settings, and
                        // don't start steam on re-tap.
                        MainController.turnOffSteamHeater()
                        presetPopup.close()
                        return
                    }
                    if (preset) {
                        // Scaled-or-base resolved by the shared SettingsBrew helper — the
                        // same helper the idle pill tap and steam-plan display use (their
                        // milk FALLBACKS differ per surface) — so this popup can't program
                        // an unscaled duration while the plan shows a scaled one. Net milk
                        // on the scale now, else this session's captured milk. (If the
                        // window's sessionMeasuredMilkG is ever renamed this silently reads
                        // 0 → base duration; SteamPlanText's one-time warn is the canary.)
                        var milk = (ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale)
                                   ? Settings.brew.netMilkForPitcher(index, MachineState.scaleWeight) : 0
                        if (milk <= 0 && root.Window.window)
                            milk = root.Window.window.sessionMeasuredMilkG || 0
                        Settings.brew.steamTimeout = Settings.brew.effectiveSteamDurationSec(index, milk)
                        Settings.brew.steamFlow = preset.flow !== undefined ? preset.flow : 150
                        Settings.brew.steamTemperature = (preset.temperature !== undefined) ? preset.temperature : Settings.brew.steamTemperature
                    }
                    MainController.applySteamSettings()

                    if (wasAlreadySelected) {
                        if (MachineState.isReady && root.canStartOperations) {
                            DE1Device.startSteam()
                        } else {
                            console.log("Cannot start steam - machine not ready, phase:", MachineState.phase)
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                        }
                    }
                    presetPopup.close()
                }
            }
        }
    }
}
