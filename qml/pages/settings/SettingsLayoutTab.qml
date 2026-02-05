import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"
import "../../components/layout"

Item {
    id: layoutTab

    // Currently selected item for move operations
    property string selectedItemId: ""
    property string selectedFromZone: ""

    // Helper to get zone items from settings
    // Reading layoutConfiguration establishes a QML binding dependency
    // so that callers re-evaluate when the layout changes
    function getZoneItems(zoneName) {
        var _dep = Settings.layoutConfiguration
        return Settings.getZoneItems(zoneName)
    }

    // Helper to get zone Y offset (also depends on layoutConfiguration)
    function getZoneYOffset(zoneName) {
        var _dep = Settings.layoutConfiguration
        return Settings.getZoneYOffset(zoneName)
    }

    // Handle item tap: select or deselect
    function onItemTapped(itemId, zoneName) {
        if (selectedItemId === itemId) {
            // Deselect
            selectedItemId = ""
            selectedFromZone = ""
        } else {
            selectedItemId = itemId
            selectedFromZone = zoneName
        }
    }

    // Handle zone tap: move selected item to this zone
    function onZoneTapped(targetZone) {
        if (selectedItemId !== "" && selectedFromZone !== targetZone) {
            Settings.moveItem(selectedItemId, selectedFromZone, targetZone, -1)
        }
        selectedItemId = ""
        selectedFromZone = ""
    }

    // Handle item removal
    function onItemRemoved(itemId, zoneName) {
        Settings.removeItem(itemId, zoneName)
        if (selectedItemId === itemId) {
            selectedItemId = ""
            selectedFromZone = ""
        }
    }

    // Handle move left within zone
    function onMoveLeft(itemId, zoneName) {
        var items = Settings.getZoneItems(zoneName)
        for (var i = 0; i < items.length; i++) {
            if (items[i].id === itemId && i > 0) {
                Settings.reorderItem(zoneName, i, i - 1)
                break
            }
        }
    }

    // Handle move right within zone
    function onMoveRight(itemId, zoneName) {
        var items = Settings.getZoneItems(zoneName)
        for (var i = 0; i < items.length; i++) {
            if (items[i].id === itemId && i < items.length - 1) {
                Settings.reorderItem(zoneName, i, i + 1)
                break
            }
        }
    }

    function openTextEditor(itemId, zoneName) {
        var props = Settings.getItemProperties(itemId)
        textEditorPopup.openForItem(itemId, zoneName, props)
    }

    // Ensure there's always a way to reach Settings from the home screen
    function ensureSettingsAccessible() {
        var zones = ["statusBar", "topLeft", "topRight", "centerStatus", "centerTop",
                     "centerMiddle", "bottomLeft", "bottomRight"]
        for (var z = 0; z < zones.length; z++) {
            var items = Settings.getZoneItems(zones[z])
            for (var i = 0; i < items.length; i++) {
                if (items[i].type === "settings") return
                if (items[i].type === "text") {
                    var props = Settings.getItemProperties(items[i].id)
                    if (props.action === "navigate:settings") return
                }
            }
        }
        // No settings access found — add a settings widget to bottom right
        Settings.addItem("settings", "bottomRight")
        console.log("SettingsLayoutTab: Added settings widget to bottomRight (no settings access found)")
    }

    onVisibleChanged: {
        if (!visible)
            ensureSettingsAccessible()
    }

    TextEditorPopup {
        id: textEditorPopup
        pageContext: "idle"
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            // Title + Reset button
            RowLayout {
                Layout.fillWidth: true

                Tr {
                    key: "settings.layout.title"
                    fallback: "Home Screen Layout"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("settings.layout.reset", "Reset to Default")
                    accessibleName: TranslationManager.translate("settings.layout.reset", "Reset to Default")
                    onClicked: {
                        Settings.resetLayoutToDefault()
                        layoutTab.selectedItemId = ""
                        layoutTab.selectedFromZone = ""
                    }
                }
            }

            // Instructions
            Tr {
                key: "settings.layout.instructions"
                fallback: "Tap + to add widgets. Tap a widget to select it for moving or reordering. Long-press Text items to edit."
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            // Status Bar zone (visible on all pages)
            LayoutEditorZone {
                Layout.fillWidth: true
                zoneName: "statusBar"
                zoneLabel: TranslationManager.translate("settings.layout.zone.statusbar", "Status Bar (All Pages)")
                items: layoutTab.getZoneItems("statusBar")
                selectedItemId: layoutTab.selectedItemId

                onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "statusBar") }
                onZoneTapped: layoutTab.onZoneTapped("statusBar")
                onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "statusBar") }
                onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "statusBar") }
                onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "statusBar") }
                onAddItemRequested: function(type) { Settings.addItem(type, "statusBar") }
                onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
            }

            // Zone cards - paired top zones
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "topLeft"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.topleft", "Top Bar (Left)")
                    items: layoutTab.getZoneItems("topLeft")
                    selectedItemId: layoutTab.selectedItemId

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "topLeft") }
                    onZoneTapped: layoutTab.onZoneTapped("topLeft")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "topLeft") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "topLeft") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "topLeft") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "topLeft") }
                    onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                }

                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "topRight"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.topright", "Top Bar (Right)")
                    items: layoutTab.getZoneItems("topRight")
                    selectedItemId: layoutTab.selectedItemId

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "topRight") }
                    onZoneTapped: layoutTab.onZoneTapped("topRight")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "topRight") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "topRight") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "topRight") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "topRight") }
                    onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                }
            }

            // Center Status zone (readouts)
            LayoutEditorZone {
                Layout.fillWidth: true
                zoneName: "centerStatus"
                zoneLabel: TranslationManager.translate("settings.layout.zone.centerstatus", "Center - Top")
                items: layoutTab.getZoneItems("centerStatus")
                selectedItemId: layoutTab.selectedItemId
                showPositionControls: true
                yOffset: layoutTab.getZoneYOffset("centerStatus")

                onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "centerStatus") }
                onZoneTapped: layoutTab.onZoneTapped("centerStatus")
                onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "centerStatus") }
                onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "centerStatus") }
                onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "centerStatus") }
                onAddItemRequested: function(type) { Settings.addItem(type, "centerStatus") }
                onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                onMoveUp: Settings.setZoneYOffset("centerStatus", yOffset - 5)
                onMoveDown: Settings.setZoneYOffset("centerStatus", yOffset + 5)
            }

            // Center Top zone
            LayoutEditorZone {
                Layout.fillWidth: true
                zoneName: "centerTop"
                zoneLabel: TranslationManager.translate("settings.layout.zone.centertop", "Center - Action Buttons")
                items: layoutTab.getZoneItems("centerTop")
                selectedItemId: layoutTab.selectedItemId
                showPositionControls: true
                yOffset: layoutTab.getZoneYOffset("centerTop")

                onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "centerTop") }
                onZoneTapped: layoutTab.onZoneTapped("centerTop")
                onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "centerTop") }
                onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "centerTop") }
                onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "centerTop") }
                onAddItemRequested: function(type) { Settings.addItem(type, "centerTop") }
                onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                onMoveUp: Settings.setZoneYOffset("centerTop", yOffset - 5)
                onMoveDown: Settings.setZoneYOffset("centerTop", yOffset + 5)
            }

            // Center Middle zone
            LayoutEditorZone {
                Layout.fillWidth: true
                zoneName: "centerMiddle"
                zoneLabel: TranslationManager.translate("settings.layout.zone.centermiddle", "Center - Info")
                items: layoutTab.getZoneItems("centerMiddle")
                selectedItemId: layoutTab.selectedItemId
                showPositionControls: true
                yOffset: layoutTab.getZoneYOffset("centerMiddle")

                onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "centerMiddle") }
                onZoneTapped: layoutTab.onZoneTapped("centerMiddle")
                onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "centerMiddle") }
                onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "centerMiddle") }
                onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "centerMiddle") }
                onAddItemRequested: function(type) { Settings.addItem(type, "centerMiddle") }
                onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                onMoveUp: Settings.setZoneYOffset("centerMiddle", yOffset - 5)
                onMoveDown: Settings.setZoneYOffset("centerMiddle", yOffset + 5)
            }

            // Bottom bar zones
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "bottomLeft"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.bottomleft", "Bottom Bar (Left)")
                    items: layoutTab.getZoneItems("bottomLeft")
                    selectedItemId: layoutTab.selectedItemId

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "bottomLeft") }
                    onZoneTapped: layoutTab.onZoneTapped("bottomLeft")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "bottomLeft") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "bottomLeft") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "bottomLeft") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "bottomLeft") }
                    onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                }

                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "bottomRight"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.bottomright", "Bottom Bar (Right)")
                    items: layoutTab.getZoneItems("bottomRight")
                    selectedItemId: layoutTab.selectedItemId

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "bottomRight") }
                    onZoneTapped: layoutTab.onZoneTapped("bottomRight")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "bottomRight") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "bottomRight") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "bottomRight") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "bottomRight") }
                    onEditTextRequested: function(itemId, zoneName) { layoutTab.openTextEditor(itemId, zoneName) }
                }
            }
        }
    }
}
