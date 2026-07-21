import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."
import "../PillFit.js" as PillFit

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

    // Live two-row fit for the flush pills (descriptive-recipe-names, mirrors
    // EspressoItem). selectedFlushPreset is an ABSOLUTE index → taps map through
    // _flushPageStart. No icon. Widths MIRROR PresetPillRow's metrics (font 16
    // bold, padding 40, spacing 12) — keep in sync (see PillFit.js).
    property int flushPageIndex: 0
    readonly property real _pillFitAvail: flushPillRow ? flushPillRow.effectiveMaxWidth : Theme.scaled(600)
    // FontMetrics.advanceWidth() (not a mutated TextMetrics.text/.width) so
    // measuring inside a reactive binding doesn't self-trigger a binding loop.
    FontMetrics { id: flushPillMetrics; font.pixelSize: Theme.scaled(16); font.bold: true }
    readonly property var _flushPageSizes: {
        var favs = Settings.brew.flushPresets
        var w = []
        for (var i = 0; i < favs.length; ++i)
            w.push(flushPillMetrics.advanceWidth((favs[i] && favs[i].name) || "") + Theme.scaled(40))
        var sizes = PillFit.packPageSizes(w, Theme.scaled(12), _pillFitAvail, 2)
        if (sizes.length <= 1)
            return sizes
        return PillFit.packPageSizes(w, Theme.scaled(12), Math.max(0, _pillFitAvail - 2 * Theme.scaled(48)), 2)
    }
    readonly property int flushPageCount: Math.max(1, _flushPageSizes.length)
    readonly property int _flushPageStart: {
        var idx = Math.max(0, Math.min(flushPageIndex, _flushPageSizes.length - 1))
        var start = 0
        for (var p = 0; p < idx; ++p)
            start += _flushPageSizes[p]
        return start
    }
    readonly property var visibleFlush: {
        var favs = Settings.brew.flushPresets
        if (!favs || favs.length === 0)
            return []
        var idx = Math.max(0, Math.min(flushPageIndex, _flushPageSizes.length - 1))
        return favs.slice(_flushPageStart, _flushPageStart + (_flushPageSizes[idx] || 0))
    }

    // Highlight this button while its mode is selected on the home screen (the
    // centre preset row is expanded), or — in compact mode, where tapping opens
    // presetPopup instead of setting activePresetFunction — while its popup is open.
    readonly property bool isActive:
        (idlePage ? idlePage.activePresetFunction : "") === "flush" || presetPopup.visible

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
                (root.idlePage.activePresetFunction === "flush") ? "" : "flush"
        }
    }

    function goToFlush() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/FlushPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/flush.svg"
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
                key: "idle.button.flush"
                fallback: "Flush"
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
            accessibleName: TranslationManager.translate("idle.button.flush", "Flush")
                            + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: TranslationManager.translate("idle.accessible.flush.hint", "Tap to toggle presets. Double-tap or long-press to configure flush.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToFlush()
            onAccessibleLongPressed: root.goToFlush()
        }
    }

    // --- PRESET POPUP ---
    Popup {
        id: presetPopup
        modal: true
        dim: false
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        // Reopen on the first (most-recent) page, matching RecipesItem/BeansItem.
        onAboutToShow: root.flushPageIndex = 0

        // Full-mode flush path runs IdlePage.onActivePresetFunctionChanged which
        // announces the preset list to TalkBack. The compact-mode popup bypasses that
        // path, so announce here directly to keep feature parity for screen-reader users.
        onClosed: { if (root.idlePage) root.idlePage.releasePanelClearance() }
        onOpened: {
            if (root.idlePage) {
                var rootTopInPage = root.mapToItem(root.idlePage, 0, 0).y
                root.idlePage.requestPanelClearance(rootTopInPage + presetPopup.y, presetPopup.height)
            }
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            // Announce the visible page (just reset to page 1), not the full list.
            var presets = root.visibleFlush
            if (presets.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < presets.length; ++i) {
                names.push(presets[i].name)
            }
            var selRel = Settings.brew.selectedFlushPreset - root._flushPageStart
            if (selRel >= 0 && selRel < presets.length) {
                selectedName = presets[selRel].name
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

        contentItem: PresetPillRow {
            id: flushPillRow
            maxWidth: Theme.scaled(600)
            presets: root.visibleFlush
            selectedIndex: {
                var rel = Settings.brew.selectedFlushPreset - root._flushPageStart
                return (rel >= 0 && rel < root.visibleFlush.length) ? rel : -1
            }

            pageCount: root.flushPageCount
            pageIndex: root.flushPageIndex
            prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousFlush", "Previous flushes")
            nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextFlush", "Next flushes")
            onPageChangeRequested: function(delta) {
                root.flushPageIndex = Math.max(0, Math.min(root.flushPageIndex + delta, root.flushPageCount - 1))
            }

            onPresetSelected: function(index) {
                var absIndex = root._flushPageStart + index
                var wasAlreadySelected = (absIndex === Settings.brew.selectedFlushPreset)
                Settings.brew.selectedFlushPreset = absIndex
                var preset = Settings.brew.getFlushPreset(absIndex)
                if (preset) {
                    Settings.brew.flushFlow = preset.flow
                    Settings.brew.flushSeconds = preset.seconds
                }
                MainController.applyFlushSettings()

                if (wasAlreadySelected) {
                    if (MachineState.isReady && root.canStartOperations) {
                        DE1Device.startFlush()
                    } else {
                        console.log("Cannot start flush - machine not ready, phase:", MachineState.phase)
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                            AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                    }
                }
                presetPopup.close()
            }
        }
    }
}
