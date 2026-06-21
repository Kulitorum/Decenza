import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"
import "../../components/layout"
import "../../components/library"

Item {
    objectName: "layoutEditor"
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
        var _dep = Settings.network.layoutConfiguration
        return Settings.network.getZoneItems(zoneName)
    }

    // Helper to get zone Y offset (also depends on layoutConfiguration)
    function getZoneYOffset(zoneName) {
        var _dep = Settings.network.layoutConfiguration
        return Settings.network.getZoneYOffset(zoneName)
    }

    // Helper to get zone scale (also depends on layoutConfiguration)
    function getZoneScale(zoneName) {
        var _dep = Settings.network.layoutConfiguration
        return Settings.network.getZoneScale(zoneName)
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

    // Pending removal awaiting confirmation (configured widgets only)
    property string pendingRemoveId: ""
    property string pendingRemoveZone: ""

    // Handle item removal. A configured widget (one with options or non-default
    // settings) asks for confirmation so an accidental tap can't discard a
    // set-up widget; a bare widget is removed directly.
    function onItemRemoved(itemId, zoneName) {
        if (Settings.network.itemIsConfigured(itemId)) {
            pendingRemoveId = itemId
            pendingRemoveZone = zoneName
            removeConfirm.open()
            return
        }
        doRemoveItem(itemId, zoneName)
    }

    function doRemoveItem(itemId, zoneName) {
        Settings.network.removeItem(itemId, zoneName)
        if (selectedItemId === itemId) {
            selectedItemId = ""
            selectedFromZone = ""
        }
    }

    // Handle move left within zone
    function onMoveLeft(itemId, zoneName) {
        var items = Settings.network.getZoneItems(zoneName)
        for (var i = 0; i < items.length; i++) {
            if (items[i].id === itemId && i > 0) {
                Settings.network.reorderItem(zoneName, i, i - 1)
                break
            }
        }
    }

    // Handle move right within zone
    function onMoveRight(itemId, zoneName) {
        var items = Settings.network.getZoneItems(zoneName)
        for (var i = 0; i < items.length; i++) {
            if (items[i].id === itemId && i < items.length - 1) {
                Settings.network.reorderItem(zoneName, i, i + 1)
                break
            }
        }
    }

    // Close any open widget-options editor so only one is ever active at a time.
    function closeOptionEditors() {
        customEditorPopup.close()
        screensaverEditorPopup.close()
        scaleWeightEditorPopup.close()
        displayModeEditorPopup.close()
        sleepEditorPopup.close()
    }

    function openCustomEditor(itemId, zoneName) {
        var props = Settings.network.getItemProperties(itemId)
        var type = props.type || ""
        // Single source of truth: nothing to open for non-configurable types.
        if (!Settings.network.typeHasOptions(type))
            return
        closeOptionEditors()
        if (type.startsWith("screensaver") || type === "lastShot" || type === "shotPlan") {
            screensaverEditorPopup.openForItem(itemId, zoneName, props)
        } else if (type === "scaleWeight") {
            scaleWeightEditorPopup.openForItem(itemId, props.dataMode || "", props.displayMode || "")
        } else if (type === "machineStatus" || type === "temperature" || type === "steamTemperature" || type === "waterLevel" || type === "clock") {
            displayModeEditorPopup.openForItem(itemId, props.displayMode || "")
        } else if (type === "sleep") {
            sleepEditorPopup.openForItem(itemId,
                props.allowQuit !== undefined ? props.allowQuit : true,
                props.showIcon !== undefined ? props.showIcon : true)
        } else {
            customEditorPopup.openForItem(itemId, zoneName, props)
        }
    }

    function openZoneOptions(zoneName, zoneLabel) {
        zoneOptionsPopup.openForZone(zoneName, zoneLabel)
    }

    // Ensure there's always a way to reach Settings from the home screen
    function ensureSettingsAccessible() {
        var zones = ["statusBar", "topLeft", "topRight", "centerStatus", "centerTop",
                     "centerMiddle", "lowerMidBar", "bottomLeft", "bottomRight"]
        for (var z = 0; z < zones.length; z++) {
            var items = Settings.network.getZoneItems(zones[z])
            for (var i = 0; i < items.length; i++) {
                if (items[i].type === "settings") return
                if (items[i].type === "custom") {
                    var props = Settings.network.getItemProperties(items[i].id)
                    if (props.action === "navigate:settings") return
                }
            }
        }
        // No settings access found — add a settings widget to bottom right
        Settings.network.addItem("settings", "bottomRight")
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

    ZoneOptionsPopup {
        id: zoneOptionsPopup
    }

    ScaleWeightEditorPopup {
        id: scaleWeightEditorPopup
    }

    DisplayModeEditorPopup {
        id: displayModeEditorPopup
    }

    SleepEditorPopup {
        id: sleepEditorPopup
    }

    // Confirm wiping the whole custom layout — a single tap is otherwise
    // irreversible.
    Dialog {
        id: resetConfirm
        anchors.centerIn: Overlay.overlay
        modal: true
        closePolicy: Dialog.CloseOnPressOutside | Dialog.CloseOnEscape
        padding: Theme.scaled(16)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: TranslationManager.translate("settings.layout.resetConfirm", "Reset the layout to default? Your customizations will be lost.")
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.maximumWidth: Theme.scaled(360)
                wrapMode: Text.Wrap
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: resetConfirm.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.layout.reset", "Reset to Default")
                    accessibleName: TranslationManager.translate("settings.layout.reset", "Reset to Default")
                    onClicked: {
                        Settings.network.resetLayoutToDefault()
                        layoutTab.selectedItemId = ""
                        layoutTab.selectedFromZone = ""
                        layoutTab.selectedZoneName = ""
                        resetConfirm.close()
                    }
                }
            }
        }
    }

    // Confirm removing a configured widget so a set-up widget isn't lost by an
    // accidental tap.
    Dialog {
        id: removeConfirm
        anchors.centerIn: Overlay.overlay
        modal: true
        closePolicy: Dialog.CloseOnPressOutside | Dialog.CloseOnEscape
        padding: Theme.scaled(16)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: TranslationManager.translate("settings.layout.removeConfirm", "Remove this widget and its settings?")
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.maximumWidth: Theme.scaled(360)
                wrapMode: Text.Wrap
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: removeConfirm.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.remove", "Remove")
                    accessibleName: TranslationManager.translate("layoutEditor.removeWidget", "Remove widget")
                    onClicked: {
                        layoutTab.doRemoveItem(layoutTab.pendingRemoveId, layoutTab.pendingRemoveZone)
                        layoutTab.pendingRemoveId = ""
                        layoutTab.pendingRemoveZone = ""
                        removeConfirm.close()
                    }
                }
            }
        }
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
                        onClicked: resetConfirm.open()
                    }
                }

                // Instructions
                Tr {
                    key: "settings.layout.instructions"
                    fallback: "Tap + to add widgets. Drag a widget to reorder it. Tap a widget to select it, then tap its gear icon (or long-press) to change options."
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
                    onReorder: function(from, to) { Settings.network.reorderItem("statusBar", from, to) }
                    onAddItemRequested: function(type) { Settings.network.addItem(type, "statusBar") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)

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
                        onReorder: function(from, to) { Settings.network.reorderItem("topLeft", from, to) }
                        onAddItemRequested: function(type) { Settings.network.addItem(type, "topLeft") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)
    
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
                        onReorder: function(from, to) { Settings.network.reorderItem("topRight", from, to) }
                        onAddItemRequested: function(type) { Settings.network.addItem(type, "topRight") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)
    
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
                    onReorder: function(from, to) { Settings.network.reorderItem("centerStatus", from, to) }
                    onAddItemRequested: function(type) { Settings.network.addItem(type, "centerStatus") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)

                    onMoveUp: Settings.network.setZoneYOffset("centerStatus", yOffset - 5)
                    onMoveDown: Settings.network.setZoneYOffset("centerStatus", yOffset + 5)
                    onScaleUp: Settings.network.setZoneScale("centerStatus", zoneScale + 0.05)
                    onScaleDown: Settings.network.setZoneScale("centerStatus", zoneScale - 0.05)
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
                    onReorder: function(from, to) { Settings.network.reorderItem("centerTop", from, to) }
                    onAddItemRequested: function(type) { Settings.network.addItem(type, "centerTop") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)

                    onMoveUp: Settings.network.setZoneYOffset("centerTop", yOffset - 5)
                    onMoveDown: Settings.network.setZoneYOffset("centerTop", yOffset + 5)
                    onScaleUp: Settings.network.setZoneScale("centerTop", zoneScale + 0.05)
                    onScaleDown: Settings.network.setZoneScale("centerTop", zoneScale - 0.05)
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
                    onReorder: function(from, to) { Settings.network.reorderItem("centerMiddle", from, to) }
                    onAddItemRequested: function(type) { Settings.network.addItem(type, "centerMiddle") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)

                    onMoveUp: Settings.network.setZoneYOffset("centerMiddle", yOffset - 5)
                    onMoveDown: Settings.network.setZoneYOffset("centerMiddle", yOffset + 5)
                    onScaleUp: Settings.network.setZoneScale("centerMiddle", zoneScale + 0.05)
                    onScaleDown: Settings.network.setZoneScale("centerMiddle", zoneScale - 0.05)
                }

                // Lower-mid bar (optional full-width band above the bottom bar).
                // Listed here, above the bottom-bar zones, to match its on-screen
                // position (it renders above the bottom action bar, not below it).
                LayoutEditorZone {
                    Layout.fillWidth: true
                    zoneName: "lowerMidBar"
                    zoneLabel: TranslationManager.translate("settings.layout.zone.lowermidbar", "Lower Mid Bar")
                    items: layoutTab.getZoneItems("lowerMidBar")
                    selectedItemId: layoutTab.selectedItemId
                    zoneSelected: layoutTab.selectedZoneName === "lowerMidBar"
                    showPositionControls: true
                    yOffset: layoutTab.getZoneYOffset("lowerMidBar")
                    zoneScale: layoutTab.getZoneScale("lowerMidBar")

                    onItemTapped: function(itemId) { layoutTab.onItemTapped(itemId, "lowerMidBar") }
                    onZoneTapped: layoutTab.onZoneTapped("lowerMidBar")
                    onItemRemoved: function(itemId) { layoutTab.onItemRemoved(itemId, "lowerMidBar") }
                    onMoveLeft: function(itemId) { layoutTab.onMoveLeft(itemId, "lowerMidBar") }
                    onMoveRight: function(itemId) { layoutTab.onMoveRight(itemId, "lowerMidBar") }
                    onReorder: function(from, to) { Settings.network.reorderItem("lowerMidBar", from, to) }
                    onAddItemRequested: function(type) { Settings.network.addItem(type, "lowerMidBar") }
                    onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)

                    onMoveUp: Settings.network.setZoneYOffset("lowerMidBar", yOffset - 5)
                    onMoveDown: Settings.network.setZoneYOffset("lowerMidBar", yOffset + 5)
                    onScaleUp: Settings.network.setZoneScale("lowerMidBar", zoneScale + 0.05)
                    onScaleDown: Settings.network.setZoneScale("lowerMidBar", zoneScale - 0.05)
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
                        onReorder: function(from, to) { Settings.network.reorderItem("bottomLeft", from, to) }
                        onAddItemRequested: function(type) { Settings.network.addItem(type, "bottomLeft") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)
    
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
                        onReorder: function(from, to) { Settings.network.reorderItem("bottomRight", from, to) }
                        onAddItemRequested: function(type) { Settings.network.addItem(type, "bottomRight") }
                        onEditCustomRequested: function(itemId, zoneName) { layoutTab.openCustomEditor(itemId, zoneName) }
                    onZoneOptionsRequested: layoutTab.openZoneOptions(zoneName, zoneLabel)

                    }
                }
            }
        }

        // Right column: live preview + Library. Scrollable so the Library stays
        // reachable even when the (large) preview would otherwise push it off.
        ScrollView {
            id: rightScroll
            // Roughly an even split so the preview is large enough to be useful.
            Layout.preferredWidth: Math.max(Theme.scaled(380), layoutTab.width * 0.42)
            Layout.minimumWidth: Theme.scaled(340)
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: rightScroll.availableWidth
                spacing: Theme.spacingMedium

                Tr {
                    key: "settings.layout.preview"
                    fallback: "Preview"
                    color: Theme.textColor
                    font: Theme.subtitleFont
                    Layout.fillWidth: true
                }

                // Live home-screen preview (5:3, matches the device reference aspect)
                Rectangle {
                    id: previewBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: width / 1.6
                    color: Theme.backgroundColor
                    radius: Theme.cardRadius
                    border.color: Theme.borderColor
                    border.width: 1
                    clip: true

                    LayoutPreview {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(4)
                    }
                }

                LibraryPanel {
                    Layout.fillWidth: true
                    // Fill the leftover viewport height when there's room; clamp to
                    // a usable minimum so the whole column scrolls (rather than
                    // hiding the Library) when the preview is tall.
                    Layout.preferredHeight: Math.max(Theme.scaled(340),
                        rightScroll.height - previewBox.height - Theme.scaled(56))

                    selectedItemId: layoutTab.selectedItemId
                    selectedFromZone: layoutTab.selectedFromZone
                    selectedZoneName: layoutTab.selectedZoneName
                }
            }
        }
    }
}
