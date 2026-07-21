import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."
import "../PillFit.js" as PillFit

// Idle-page Equipment button (add-equipment-packages). Mirrors BeansItem: a tap
// shows the recently-used equipment packages as quick-switch pills (inline pills
// in center zones, a dropdown popup in compact bars), and a double-tap or
// long-press opens the full Equipment window. In center zones LayoutItemDelegate
// compiles "equipment" to a CustomItem (action togglePreset:equipment +
// navigate:equipment on long/double), so this file renders only in the compact
// (top/bottom/statusBar) zones.
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

    // Recently-used equipment packages shown as pills (MRU-ordered). The full
    // inventory is kept and paged into two-row pages (descriptive-recipe-names —
    // previously capped to 5 with no paging); the full inventory also lives on
    // the Equipment page.
    property var inventoryEquipment: []
    property int equipmentPageIndex: 0

    function equipmentLabel(pkg) {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""]
                .filter(function(s) { return s.length > 0 }).join(" ")
    }

    // Live two-row fit (descriptive-recipe-names, mirrors RecipesItem). Equipment
    // pills carry no icon. Widths MIRROR PresetPillRow's metrics — keep in sync
    // (see PillFit.js).
    readonly property real _pillFitAvail: equipmentPillRow ? equipmentPillRow.effectiveMaxWidth : Theme.scaled(600)
    // FontMetrics.advanceWidth() (not a mutated TextMetrics.text/.width) so
    // measuring inside a reactive binding doesn't self-trigger a binding loop.
    FontMetrics { id: equipmentPillMetrics; font.pixelSize: Theme.scaled(16); font.bold: true }
    readonly property var _equipmentPageSizes: {
        var w = []
        for (var i = 0; i < inventoryEquipment.length; ++i)
            w.push(equipmentPillMetrics.advanceWidth(equipmentLabel(inventoryEquipment[i])) + Theme.scaled(40))
        var sizes = PillFit.packPageSizes(w, Theme.scaled(12), _pillFitAvail, 2)
        if (sizes.length <= 1)
            return sizes
        return PillFit.packPageSizes(w, Theme.scaled(12), Math.max(0, _pillFitAvail - 2 * Theme.scaled(48)), 2)
    }
    readonly property int equipmentPageCount: Math.max(1, _equipmentPageSizes.length)
    readonly property var visibleEquipment: {
        if (inventoryEquipment.length === 0)
            return []
        var idx = Math.max(0, Math.min(equipmentPageIndex, _equipmentPageSizes.length - 1))
        var start = 0
        for (var p = 0; p < idx; ++p)
            start += _equipmentPageSizes[p]
        return inventoryEquipment.slice(start, start + (_equipmentPageSizes[idx] || 0))
    }

    Component.onCompleted: MainController.equipmentStorage.requestInventory()

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            root.inventoryEquipment = packages
            root.equipmentPageIndex = Math.max(0, Math.min(root.equipmentPageIndex, root.equipmentPageCount - 1))
        }
        function onPackagesChanged() {
            MainController.equipmentStorage.requestInventory()
        }
    }

    // Highlight this button while its mode is selected on the home screen (the
    // centre preset row is expanded), or — in compact mode, where tapping opens
    // presetPopup instead of setting activePresetFunction — while its popup is open.
    readonly property bool isActive:
        (idlePage ? idlePage.activePresetFunction : "") === "equipment" || presetPopup.visible

    // Compact (bar) rendering only — see the header comment; the full-size type
    // compiles to CustomItem in LayoutItemDelegate (isCompiled).
    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function togglePresets() {
        if (root.inventoryEquipment.length === 0) {
            goToEquipment()
        } else if (root.isCompact) {
            presetPopup.visible ? presetPopup.close() : presetPopup.open()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "equipment") ? "" : "equipment"
        }
    }

    function goToEquipment() {
        if (typeof pageStack !== "undefined")
            pageStack.push(Qt.resolvedUrl("../../../pages/EquipmentPage.qml"))
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
                source: "qrc:/icons/grind.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.isActive ? Theme.accentColor : Theme.textColor
                }
            }
            Tr {
                key: "idle.button.equipment"
                fallback: "Equipment"
                font: Theme.bodyFont
                color: root.isActive ? Theme.accentColor : Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.equipment", "Equipment")
                            + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: TranslationManager.translate("idle.accessible.equipment.hint", "Tap to switch equipment. Double-tap or long-press for the equipment inventory.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToEquipment()
            onAccessibleLongPressed: root.goToEquipment()
        }
    }

    // --- EQUIPMENT PILL POPUP (compact mode) ---
    // Dialog (not Popup) so TalkBack can trap focus inside the pill list, mirroring
    // BeansItem. modal traps focus; dim:false keeps the dropdown look; header/footer
    // null strip the Dialog chrome.
    Dialog {
        id: presetPopup
        modal: true
        dim: false
        header: null
        footer: null
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        // Reopen on the first (most-recent) page, matching BeansItem/RecipesItem.
        onAboutToShow: root.equipmentPageIndex = 0

        // Slide the OTHER idle content clear of this popup — up when the picker
        // sits in the page's lower half (e.g. the bottom bar, clearing the Shot
        // Plan sentence / lower-mid bar), down when in the upper half. The button
        // and popup stay put; only the other content yields.
        onOpened: {
            if (root.idlePage) {
                var rootTopInPage = root.mapToItem(root.idlePage, 0, 0).y
                root.idlePage.requestPanelClearance(rootTopInPage + presetPopup.y, presetPopup.height)
            }
            _announceOnOpen()
        }
        onClosed: {
            if (root.idlePage) root.idlePage.releasePanelClearance()
        }

        function _announceOnOpen() {
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            // Announce the visible page (just reset to page 1), not the full
            // inventory — matches every sibling pill row.
            var pkgs = root.visibleEquipment
            if (pkgs.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < pkgs.length; ++i) {
                names.push(root.equipmentLabel(pkgs[i]))
                if (pkgs[i].id === Settings.dye.activeEquipmentId) selectedName = root.equipmentLabel(pkgs[i])
            }
            var announcement = pkgs.length + " " + TranslationManager.translate("idle.accessible.equipmentPackages", "equipment packages") + ": " + names.join(", ")
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
            // With the glass chrome on, float the pills directly (matching
            // the center inline preset rows) instead of showing a panel; keep the
            // opaque surface panel when the glass chrome is off.
            readonly property bool hasGlassChrome: Theme.glassChrome
            color: hasGlassChrome ? "transparent" : Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: hasGlassChrome ? 0 : 1
        }

        contentItem: PresetPillRow {
            id: equipmentPillRow
            maxWidth: Theme.scaled(600)
            presets: root.visibleEquipment.map(function(p) { return { name: root.equipmentLabel(p) } })
            selectedIndex: {
                var list = root.visibleEquipment
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === Settings.dye.activeEquipmentId) return i
                }
                return -1
            }

            pageCount: root.equipmentPageCount
            pageIndex: root.equipmentPageIndex
            prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousEquipment", "Previous equipment")
            nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextEquipment", "Next equipment")
            onPageChangeRequested: function(delta) {
                root.equipmentPageIndex = Math.max(0, Math.min(root.equipmentPageIndex + delta, root.equipmentPageCount - 1))
            }

            onPresetSelected: function(index) {
                var pkg = root.visibleEquipment[index]
                if (!pkg) return
                Settings.dye.switchToEquipment(pkg)
                presetPopup.close()
            }
        }
    }
}
