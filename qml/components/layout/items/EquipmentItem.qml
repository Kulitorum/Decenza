import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

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

    // Recently-used equipment packages shown as pills. Capped to the 5 most
    // recently used (inventoryReady is MRU-ordered); the full inventory lives on
    // the Equipment page.
    property var inventoryEquipment: []

    function equipmentLabel(pkg) {
        if (!pkg) return ""
        if (pkg.name && String(pkg.name).length > 0) return String(pkg.name)
        return [pkg.grinderBrand || "", pkg.grinderModel || ""]
                .filter(function(s) { return s.length > 0 }).join(" ")
    }

    Component.onCompleted: MainController.equipmentStorage.requestInventory()

    Connections {
        target: MainController.equipmentStorage
        function onInventoryReady(packages) {
            root.inventoryEquipment = packages.slice(0, 5)
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

        onOpened: {
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var pkgs = root.inventoryEquipment
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
            id: equipmentPillRow
            maxWidth: Theme.scaled(600)
            presets: root.inventoryEquipment.map(function(p) { return { name: root.equipmentLabel(p) } })
            selectedIndex: {
                var list = root.inventoryEquipment
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === Settings.dye.activeEquipmentId) return i
                }
                return -1
            }

            onPresetSelected: function(index) {
                var pkg = root.inventoryEquipment[index]
                if (!pkg) return
                Settings.dye.switchToEquipment(pkg)
                presetPopup.close()
            }
        }
    }
}
