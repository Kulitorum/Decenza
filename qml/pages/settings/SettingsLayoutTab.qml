import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"
import "../../components/layout"
import "../../components/library"

Item {
    id: layoutTab

    // Currently selected item for move operations
    property string selectedItemId: ""
    property string selectedFromZone: ""

    // Currently selected zone for library operations
    property string selectedZoneName: ""

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

    // Helper to get zone scale (also depends on layoutConfiguration)
    function getZoneScale(zoneName) {
        var _dep = Settings.layoutConfiguration
        return Settings.getZoneScale(zoneName)
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
        // Clear zone selection when an item is selected
        selectedZoneName = ""
    }

    // Handle zone tap: toggle zone selection or clear item selection
    function onZoneTapped(targetZone) {
        selectedItemId = ""
        selectedFromZone = ""
        // Toggle zone selection
        selectedZoneName = (selectedZoneName === targetZone) ? "" : targetZone
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

    function openCustomEditor(itemId, zoneName) {
        var props = Settings.getItemProperties(itemId)
        var type = props.type || ""
        if (type.startsWith("screensaver") || type === "lastShot") {
            screensaverEditorPopup.openForItem(itemId, zoneName, props)
        } else {
            customEditorPopup.openForItem(itemId, zoneName, props)
        }
    }

    // Ensure there's always a way to reach Settings from the home screen
    function ensureSettingsAccessible() {
        var zones = ["statusBar", "topLeft", "topRight", "centerStatus", "centerTop",
                     "centerMiddle", "bottomLeft", "bottomRight"]
        for (var z = 0; z < zones.length; z++) {
            var items = Settings.getZoneItems(zones[z])
            for (var i = 0; i < items.length; i++) {
                if (items[i].type === "settings") return
                if (items[i].type === "custom") {
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

    CustomEditorPopup {
        id: customEditorPopup
        pageContext: "idle"
    }

    ScreensaverEditorPopup {
        id: screensaverEditorPopup
    }

    // Two-column layout: zone editors on left, library panel on right
    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Left column: zone editors
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                            layoutTab.selectedZoneName = ""
                        }
                    }
                }

                // Instructions
                Tr {
                    key: "settings.layout.instructions"
                    fallback: "Tap + to add widgets. Tap a widget to select it for moving or reordering. Long-press Custom items to edit."
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
                    zoneSelected: layoutTab.selectedZoneName === "statusBar"

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "statusBar") }
                    onZoneTapped: layoutTab.onZoneTapped("statusBar")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "statusBar") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "statusBar") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "statusBar") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "statusBar") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }

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
                        zoneSelected: layoutTab.selectedZoneName === "topLeft"

                        onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "topLeft") }
                        onZoneTapped: layoutTab.onZoneTapped("topLeft")
                        onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "topLeft") }
                        onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "topLeft") }
                        onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "topLeft") }
                        onAddItemRequested: function(type) { Settings.addItem(type, "topLeft") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
    
                    }

                    LayoutEditorZone {
                        Layout.fillWidth: true
                        zoneName: "topRight"
                        zoneLabel: TranslationManager.translate("settings.layout.zone.topright", "Top Bar (Right)")
                        items: layoutTab.getZoneItems("topRight")
                        selectedItemId: layoutTab.selectedItemId
                        zoneSelected: layoutTab.selectedZoneName === "topRight"

                        onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "topRight") }
                        onZoneTapped: layoutTab.onZoneTapped("topRight")
                        onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "topRight") }
                        onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "topRight") }
                        onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "topRight") }
                        onAddItemRequested: function(type) { Settings.addItem(type, "topRight") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
    
                    }
                }

                // Center Status zone (readouts)
                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "centerStatus"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.centerstatus", "Center - Top")
                    items: layoutTab.getZoneItems("centerStatus")
                    selectedItemId: layoutTab.selectedItemId
                    zoneSelected: layoutTab.selectedZoneName === "centerStatus"
                    showPositionControls: true
                    yOffset: layoutTab.getZoneYOffset("centerStatus")
                    zoneScale: layoutTab.getZoneScale("centerStatus")

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "centerStatus") }
                    onZoneTapped: layoutTab.onZoneTapped("centerStatus")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "centerStatus") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "centerStatus") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "centerStatus") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "centerStatus") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }

                    onMoveUp: Settings.setZoneYOffset("centerStatus", yOffset - 5)
                    onMoveDown: Settings.setZoneYOffset("centerStatus", yOffset + 5)
                    onScaleUp: Settings.setZoneScale("centerStatus", zoneScale + 0.05)
                    onScaleDown: Settings.setZoneScale("centerStatus", zoneScale - 0.05)
                }

                // Center Top zone
                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "centerTop"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.centertop", "Center - Action Buttons")
                    items: layoutTab.getZoneItems("centerTop")
                    selectedItemId: layoutTab.selectedItemId
                    zoneSelected: layoutTab.selectedZoneName === "centerTop"
                    showPositionControls: true
                    yOffset: layoutTab.getZoneYOffset("centerTop")
                    zoneScale: layoutTab.getZoneScale("centerTop")

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "centerTop") }
                    onZoneTapped: layoutTab.onZoneTapped("centerTop")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "centerTop") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "centerTop") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "centerTop") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "centerTop") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }

                    onMoveUp: Settings.setZoneYOffset("centerTop", yOffset - 5)
                    onMoveDown: Settings.setZoneYOffset("centerTop", yOffset + 5)
                    onScaleUp: Settings.setZoneScale("centerTop", zoneScale + 0.05)
                    onScaleDown: Settings.setZoneScale("centerTop", zoneScale - 0.05)
                }

                // Center Middle zone
                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "centerMiddle"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.centermiddle", "Center - Info")
                    items: layoutTab.getZoneItems("centerMiddle")
                    selectedItemId: layoutTab.selectedItemId
                    zoneSelected: layoutTab.selectedZoneName === "centerMiddle"
                    showPositionControls: true
                    yOffset: layoutTab.getZoneYOffset("centerMiddle")
                    zoneScale: layoutTab.getZoneScale("centerMiddle")

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "centerMiddle") }
                    onZoneTapped: layoutTab.onZoneTapped("centerMiddle")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "centerMiddle") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "centerMiddle") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "centerMiddle") }
                    onAddItemRequested: function(type) { Settings.addItem(type, "centerMiddle") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }

                    onMoveUp: Settings.setZoneYOffset("centerMiddle", yOffset - 5)
                    onMoveDown: Settings.setZoneYOffset("centerMiddle", yOffset + 5)
                    onScaleUp: Settings.setZoneScale("centerMiddle", zoneScale + 0.05)
                    onScaleDown: Settings.setZoneScale("centerMiddle", zoneScale - 0.05)
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
                        zoneSelected: layoutTab.selectedZoneName === "bottomLeft"

                        onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "bottomLeft") }
                        onZoneTapped: layoutTab.onZoneTapped("bottomLeft")
                        onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "bottomLeft") }
                        onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "bottomLeft") }
                        onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "bottomLeft") }
                        onAddItemRequested: function(type) { Settings.addItem(type, "bottomLeft") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
    
                    }

                    LayoutEditorZone {
                        Layout.fillWidth: true
                        zoneName: "bottomRight"
                        zoneLabel: TranslationManager.translate("settings.layout.zone.bottomright", "Bottom Bar (Right)")
                        items: layoutTab.getZoneItems("bottomRight")
                        selectedItemId: layoutTab.selectedItemId
                        zoneSelected: layoutTab.selectedZoneName === "bottomRight"

                        onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "bottomRight") }
                        onZoneTapped: layoutTab.onZoneTapped("bottomRight")
                        onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "bottomRight") }
                        onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "bottomRight") }
                        onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "bottomRight") }
                        onAddItemRequested: function(type) { Settings.addItem(type, "bottomRight") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
    
                    }
                }
            }
        }

        // Right column: Library panel
        LibraryPanel {
            Layout.preferredWidth: Theme.scaled(320)
            Layout.minimumWidth: Theme.scaled(280)
            Layout.fillHeight: true

            selectedItemId: layoutTab.selectedItemId
            selectedFromZone: layoutTab.selectedFromZone
            selectedZoneName: layoutTab.selectedZoneName
        }
    }
}
