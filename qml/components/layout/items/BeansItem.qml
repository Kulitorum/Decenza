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

    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    // Inventory bags shown as pills (bean-bag-inventory: pills are bags now,
    // not presets — visibility criterion is inInventory, selection is
    // activeBagId, and there is no dirty state because bag edits write
    // through). The full MRU inventory (inventoryReady is MRU-ordered) is kept
    // and paged so each page occupies at most two rows (descriptive-recipe-
    // names — mirrors RecipesItem); the full inventory also lives on the Beans
    // page.
    property var inventoryBags: []
    property int beanPageIndex: 0

    // Live two-row fit (descriptive-recipe-names) — same approach as
    // RecipesItem: pack the MRU list into pages of AT MOST TWO ROWS at the pill
    // row's real available width, per-page count varying with name length.
    // Widths MIRROR PresetPillRow's pill metrics (font 16 bold, padding 40,
    // spacing 12); bean pills carry no icon, so no icon term. Keep in sync with
    // PresetPillRow.qml / PillFit.js.
    readonly property real _pillFitAvail: beansPillRow ? beansPillRow.effectiveMaxWidth : Theme.scaled(600)
    // FontMetrics.advanceWidth() (not a mutated TextMetrics.text/.width) so
    // measuring inside a reactive binding doesn't self-trigger a binding loop.
    FontMetrics { id: beanPillMetrics; font.pixelSize: Theme.scaled(16); font.bold: true }
    function _beanPillWidths() {
        var out = []
        for (var i = 0; i < inventoryBags.length; ++i)
            out.push(beanPillMetrics.advanceWidth(root.bagLabel(inventoryBags[i])) + Theme.scaled(40))
        return out
    }
    readonly property var _beanPageSizes: {
        var widths = _beanPillWidths()
        var sizes = PillFit.packPageSizes(widths, Theme.scaled(12), _pillFitAvail, 2)
        if (sizes.length <= 1)
            return sizes
        return PillFit.packPageSizes(widths, Theme.scaled(12),
                                     Math.max(0, _pillFitAvail - 2 * Theme.scaled(48)), 2)
    }
    readonly property int beanPageCount: Math.max(1, _beanPageSizes.length)
    readonly property var visibleBags: {
        if (inventoryBags.length === 0)
            return []
        var idx = Math.max(0, Math.min(beanPageIndex, _beanPageSizes.length - 1))
        var start = 0
        for (var p = 0; p < idx; ++p)
            start += _beanPageSizes[p]
        return inventoryBags.slice(start, start + (_beanPageSizes[idx] || 0))
    }

    function bagLabel(bag) {
        if (!bag) return ""
        var coffee = bag.coffeeName || ""
        return coffee.length > 0 ? coffee : (bag.roasterName || "")
    }

    Component.onCompleted: MainController.bagStorage.requestInventory()

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            root.inventoryBags = bags
            root.beanPageIndex = Math.max(0, Math.min(root.beanPageIndex, root.beanPageCount - 1))
        }
        function onBagsChanged() {
            MainController.bagStorage.requestInventory()
        }
    }

    // Highlight this button while its mode is selected on the home screen (the
    // centre preset row is expanded), or — in compact mode, where tapping opens
    // presetPopup instead of setting activePresetFunction — while its popup is open.
    readonly property bool isActive:
        (idlePage ? idlePage.activePresetFunction : "") === "beans" || presetPopup.visible

    // Compact (bar) rendering only: full-size placements of this type compile to
    // CustomItem in LayoutItemDelegate (isCompiled), so this item never loads
    // non-compact and carries no full-mode rendering.
    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function togglePresets() {
        if (root.isCompact) {
            if (root.inventoryBags.length === 0) {
                goToBeanInfo()
            } else {
                presetPopup.visible ? presetPopup.close() : presetPopup.open()
            }
        } else if (root.inventoryBags.length === 0) {
            goToBeanInfo()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "beans") ? "" : "beans"
        }
    }

    function goToBeanInfo() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/BeanInfoPage.qml"))
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
                source: "qrc:/icons/coffeebeans.svg"
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
                key: "idle.button.beaninfo"
                fallback: "Beans"
                font: Theme.bodyFont
                color: root.isActive ? Theme.accentColor : Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.beaninfo", "Beans")
                            + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: TranslationManager.translate("idle.accessible.beaninfo.hint", "Tap to toggle bag pills. Double-tap or long-press for the bag inventory.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToBeanInfo()
            onAccessibleLongPressed: root.goToBeanInfo()
        }
    }

    // --- BAG PILL POPUP ---
    // Dialog (not Popup) so TalkBack can trap focus inside the pill list — a Qt
    // Popup leaks focus and the pills are unreachable by swipe. modal traps
    // focus; dim:false keeps it looking like a dropdown, header/footer:null
    // strip the Dialog chrome so it renders like the old bare popup.
    Dialog {
        id: presetPopup
        modal: true
        dim: false
        header: null
        footer: null
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        // Full-mode beans path runs IdlePage.onActivePresetFunctionChanged which announces
        // the bag list to TalkBack. The compact-mode popup bypasses that path, so
        // announce here directly to keep feature parity for screen-reader users.
        onAboutToShow: root.beanPageIndex = 0  // Always open on the most-recent five.

        onClosed: { if (root.idlePage) root.idlePage.releasePanelClearance() }
        onOpened: {
            if (root.idlePage) {
                var rootTopInPage = root.mapToItem(root.idlePage, 0, 0).y
                root.idlePage.requestPanelClearance(rootTopInPage + presetPopup.y, presetPopup.height)
            }
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var bags = root.visibleBags
            if (bags.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < bags.length; ++i) {
                names.push(root.bagLabel(bags[i]))
                if (bags[i].id === Settings.dye.activeBagId) selectedName = root.bagLabel(bags[i])
            }
            var announcement = bags.length + " " + TranslationManager.translate("idle.accessible.bags", "bags") + ": " + names.join(", ")
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
            id: beansPillRow
            maxWidth: Theme.scaled(600)
            presets: root.visibleBags.map(function(b) { return { name: root.bagLabel(b) } })
            selectedIndex: {
                var list = root.visibleBags
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === Settings.dye.activeBagId) return i
                }
                return -1
            }

            pageCount: root.beanPageCount
            pageIndex: root.beanPageIndex
            prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousBeans", "Previous beans")
            nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextBeans", "Next beans")
            onPageChangeRequested: function(delta) {
                root.beanPageIndex = Math.max(0, Math.min(root.beanPageIndex + delta, root.beanPageCount - 1))
            }

            onPresetSelected: function(index) {
                var bag = root.visibleBags[index]
                if (!bag) return
                Settings.dye.activeBagId = bag.id
                presetPopup.close()
            }
        }
    }
}
