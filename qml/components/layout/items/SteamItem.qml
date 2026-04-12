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

    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

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
                    colorizationColor: Theme.textColor
                }
            }
            Tr {
                key: "idle.button.steam"
                fallback: "Steam"
                font: Theme.bodyFont
                color: DE1Device.guiEnabled ? Theme.textColor : Theme.textSecondaryColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            enabled: DE1Device.guiEnabled
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.steam", "Steam")
            accessibleDescription: TranslationManager.translate("idle.accessible.steam.hint", "Tap to toggle presets. Double-tap or long-press to configure steam.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToSteam()
            onAccessibleLongPressed: root.goToSteam()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(150)
        implicitHeight: Theme.scaled(120)

        ActionButton {
            anchors.fill: parent
            translationKey: "idle.button.steam"
            translationFallback: "Steam"
            iconSource: "qrc:/icons/steam.svg"
            enabled: DE1Device.guiEnabled
            supportDoubleClick: true
            onClicked: root.togglePresets()
            onPressAndHold: root.goToSteam()
            onDoubleClicked: root.goToSteam()

            Accessible.description: TranslationManager.translate("idle.accessible.steam.description", "Start steaming milk. Double-tap or long-press to configure.")
        }
    }

    // --- PRESET POPUP ---
    Popup {
        id: presetPopup
        modal: false
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        onOpened: {
            if (typeof MachineState !== "undefined") MachineState.tareScale()

            // Full-mode steam path runs IdlePage.onActivePresetFunctionChanged which
            // announces the preset list to TalkBack. The compact-mode popup bypasses
            // that path, so announce here directly to keep feature parity.
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var presets = Settings.steamPitcherPresets
            if (presets.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < presets.length; ++i) {
                names.push(presets[i].name)
            }
            if (Settings.selectedSteamPitcher >= 0 && Settings.selectedSteamPitcher < presets.length) {
                selectedName = presets[Settings.selectedSteamPitcher].name
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
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
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
                presets: Settings.steamPitcherPresets
                selectedIndex: Settings.selectedSteamPitcher
                pillSuffixMaxWidth: Theme.scaled(60)
                pillSuffixVersion: parent.popupSuffixVersion

                pillSuffixFn: function(index) {
                    if (!ScaleDevice.connected || ScaleDevice.isFlowScale) return ""
                    var preset = Settings.steamPitcherPresets[index]
                    if (!preset) return ""
                    var pitcherWeight = preset.pitcherWeightG ?? 0
                    if (pitcherWeight <= 0) return ""
                    var milkWeight = Math.max(0, MachineState.scaleWeight - pitcherWeight)
                    return " (" + Math.round(milkWeight) + "g)"
                }

                onPresetSelected: function(index) {
                    var wasAlreadySelected = (index === Settings.selectedSteamPitcher)
                    Settings.selectedSteamPitcher = index
                    var preset = Settings.getSteamPitcherPreset(index)
                    if (preset) {
                        Settings.steamTimeout = preset.duration
                        Settings.steamFlow = preset.flow !== undefined ? preset.flow : 150
                    }
                    MainController.applySteamSettings()

                    if (wasAlreadySelected) {
                        if (MachineState.isReady) {
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
