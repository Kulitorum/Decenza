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

    // Inventory bags shown as pills (bean-bag-inventory: pills are bags now,
    // not presets — visibility criterion is inInventory, selection is
    // activeBagId, and there is no dirty state because bag edits write
    // through). Capped to the 5 most recently used (inventoryReady is
    // MRU-ordered); the full inventory lives on the Beans page.
    property var inventoryBags: []

    function bagLabel(bag) {
        if (!bag) return ""
        var coffee = bag.coffeeName || ""
        return coffee.length > 0 ? coffee : (bag.roasterName || "")
    }

    Component.onCompleted: MainController.bagStorage.requestInventory()

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            root.inventoryBags = bags.slice(0, 5)
        }
        function onBagsChanged() {
            MainController.bagStorage.requestInventory()
        }
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

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
                    colorizationColor: Theme.textColor
                }
            }
            Tr {
                key: "idle.button.beaninfo"
                fallback: "Beans"
                font: Theme.bodyFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.beaninfo", "Beans")
            accessibleDescription: TranslationManager.translate("idle.accessible.beaninfo.hint", "Tap to toggle bag pills. Double-tap or long-press for the bag inventory.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToBeanInfo()
            onAccessibleLongPressed: root.goToBeanInfo()
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
            translationKey: "idle.button.beaninfo"
            translationFallback: "Beans"
            iconSource: "qrc:/icons/coffeebeans.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Settings.dye.activeBagId <= 0 ? Theme.highlightColor : Theme.primaryColor
            supportDoubleClick: true
            onClicked: root.togglePresets()
            onPressAndHold: root.goToBeanInfo()
            onDoubleClicked: root.goToBeanInfo()

            Accessible.description: TranslationManager.translate("idle.accessible.beaninfo.description", "Select the bag of beans for your next shot. Double-tap or long-press for the bag inventory.")
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
        onOpened: {
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var bags = root.inventoryBags
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
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: PresetPillRow {
            id: beansPillRow
            maxWidth: Theme.scaled(600)
            presets: root.inventoryBags.map(function(b) { return { name: root.bagLabel(b) } })
            selectedIndex: {
                var list = root.inventoryBags
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === Settings.dye.activeBagId) return i
                }
                return -1
            }

            onPresetSelected: function(index) {
                var bag = root.inventoryBags[index]
                if (!bag) return
                Settings.dye.activeBagId = bag.id
                presetPopup.close()
            }
        }
    }
}
