// The tab, save-menu and entry-card Repeater/ListView delegates and the `layer.effect`
// blocks read this file's `libraryPanel` id; Bound makes it statically resolvable.
// Each delegate declares its one injected role, `modelData`, required in the same
// edit -- without that, Bound stops role injection and the tabs, the save menu and the
// whole library grid render blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

Rectangle {
    id: libraryPanel

    // Interface with layout editor
    property string selectedItemId: ""
    property string selectedFromZone: ""
    property string selectedZoneName: ""

    // Display state
    property int displayMode: 0  // 0=full, 1=compact
    property string activeTab: "local"  // "local", "community"

    // Type filters (all on by default)
    property bool showItems: true
    property bool showZones: true
    property bool showLayouts: true
    property bool showThemes: true

    // Type of the currently selected entry ("item", "zone", "layout", or "")
    readonly property string selectedEntryType: {
        var id = WidgetLibrary.selectedEntryId
        if (!id) return ""
        var entry = WidgetLibrary.getEntry(id)
        if (entry && entry.type) return entry.type
        if (activeTab !== "community") return ""
        var entries = LibrarySharing.communityEntries
        for (var i = 0; i < entries.length; i++) {
            if (entries[i].id === id) return entries[i].type || ""
        }
        return ""
    }

    // Whether the selected entry can be deleted
    readonly property bool canDeleteSelected: {
        if (WidgetLibrary.selectedEntryId === "") return false
        if (activeTab === "local") return true
        // Community tab: only allow deleting own entries
        var entries = LibrarySharing.communityEntries
        for (var i = 0; i < entries.length; i++) {
            if (entries[i].id === WidgetLibrary.selectedEntryId)
                return entries[i].deviceId === Settings.app.deviceId()
        }
        return false
    }

    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.color: Theme.borderColor
    border.width: 1

    onActiveTabChanged: {
        if (activeTab === "community")
            LibrarySharing.browseCommunity("", "", "", "", "newest", 1)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(8)
        spacing: Theme.scaled(6)

        // Header with display mode toggles
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)

            Text {
                text: TranslationManager.translate("library.title", "Library")
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.fillWidth: true
            }

            // Full preview (grid) mode button
            StyledIconButton {
                implicitWidth: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                icon.source: "qrc:/icons/grid.svg"
                icon.width: Theme.scaled(14)
                icon.height: Theme.scaled(14)
                active: libraryPanel.displayMode === 0
                accessibleName: TranslationManager.translate("library.accessibility.gridView", "Grid view") + (libraryPanel.displayMode === 0 ? ", " + TranslationManager.translate("library.accessibility.selected", "selected") : "")
                onClicked: libraryPanel.displayMode = 0
            }

            // Compact list mode button
            StyledIconButton {
                implicitWidth: Theme.scaled(28)
                implicitHeight: Theme.scaled(28)
                icon.source: "qrc:/icons/list.svg"
                icon.width: Theme.scaled(14)
                icon.height: Theme.scaled(14)
                active: libraryPanel.displayMode === 1
                accessibleName: TranslationManager.translate("library.accessibility.listView", "List view") + (libraryPanel.displayMode === 1 ? ", " + TranslationManager.translate("library.accessibility.selected", "selected") : "")
                onClicked: libraryPanel.displayMode = 1
            }
        }

        // Tab row
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(28)

            // Bottom border line (active tab covers its portion)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.borderColor
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: [
                        { key: "local", label: TranslationManager.translate("library.tab.myLibrary", "My Library") },
                        { key: "community", label: TranslationManager.translate("library.tab.community", "Community") }
                    ]

                    Item {
                        id: tabButton
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        // Active tab shape
                        Rectangle {
                            visible: libraryPanel.activeTab === tabButton.modelData.key
                            anchors.fill: parent
                            anchors.bottomMargin: -1
                            color: Theme.backgroundColor
                            border.color: Theme.borderColor
                            border.width: 1
                            radius: Theme.scaled(4)

                            // Cover bottom rounded corners and bottom border
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 1
                                anchors.rightMargin: 1
                                height: Theme.scaled(5)
                                color: Theme.backgroundColor
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: tabButton.modelData.label + " " + TranslationManager.translate("library.accessibility.tab", "tab") + (libraryPanel.activeTab === tabButton.modelData.key ? ", " + TranslationManager.translate("library.accessibility.selected", "selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: tabMa.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: tabButton.modelData.label
                            color: libraryPanel.activeTab === tabButton.modelData.key ? Theme.textColor : Theme.textSecondaryColor
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.scaled(11)
                            font.bold: libraryPanel.activeTab === tabButton.modelData.key
                            Accessible.ignored: true
                        }

                        MouseArea {
                            id: tabMa
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: libraryPanel.activeTab = tabButton.modelData.key
                        }
                    }
                }
            }
        }

        // Action buttons row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(4)

            // Add button with dropdown
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30)
                Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: addMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"
                border.color: Theme.primaryColor
                border.width: 1

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.addToLibrary", "Add to library")
                Accessible.focusable: true
                Accessible.onPressAction: addMa.clicked(null)

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/plus.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }

                MouseArea {
                    id: addMa
                    anchors.fill: parent
                    onClicked: addMenu.open()
                }

                Dialog {
                    id: addMenu
                    modal: true
                    parent: Overlay.overlay
                    anchors.centerIn: parent
                    padding: Theme.scaled(4)
                    closePolicy: Dialog.CloseOnPressOutside | Dialog.CloseOnEscape

                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }

                    contentItem: ColumnLayout {
                        spacing: Theme.scaled(2)

                        Repeater {
                            model: [
                                { label: TranslationManager.translate("library.menu.saveItem", "Save Item"), type: "item", enabled: libraryPanel.selectedItemId !== "" },
                                { label: TranslationManager.translate("library.menu.saveZone", "Save Zone"), type: "zone", enabled: libraryPanel.selectedZoneName !== "" },
                                { label: TranslationManager.translate("library.menu.saveLayout", "Save Layout"), type: "layout", enabled: true }
                            ]

                            Rectangle {
                                id: saveMenuRow
                                required property var modelData

                                Layout.fillWidth: true
                                implicitWidth: Theme.scaled(140)
                                height: Theme.scaled(32)
                                radius: Theme.scaled(4)
                                color: menuItemMa.containsMouse && saveMenuRow.modelData.enabled
                                    ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12)
                                    : "transparent"
                                opacity: saveMenuRow.modelData.enabled ? 1.0 : 0.4

                                Accessible.role: Accessible.Button
                                Accessible.name: saveMenuRow.modelData.label
                                Accessible.focusable: true
                                Accessible.onPressAction: menuItemMa.clicked(null)

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.scaled(10)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: saveMenuRow.modelData.label
                                    color: Theme.textColor
                                    font: Theme.bodyFont
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: menuItemMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: saveMenuRow.modelData.enabled
                                    onClicked: {
                                        addMenu.close()
                                        switch (saveMenuRow.modelData.type) {
                                            case "item":
                                                WidgetLibrary.addItemFromLayout(libraryPanel.selectedItemId)
                                                break
                                            case "zone":
                                                WidgetLibrary.addZoneFromLayout(libraryPanel.selectedZoneName)
                                                break
                                            case "layout":
                                                WidgetLibrary.addCurrentLayout(false)
                                                break
                                        }
                                        libraryPanel.activeTab = "local"
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Apply to zone button
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30)
                Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: applyMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"
                border.color: applyEnabled ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                opacity: applyEnabled ? 1.0 : 0.4

                property bool applyEnabled: WidgetLibrary.selectedEntryId !== "" && (libraryPanel.selectedEntryType === "layout" || libraryPanel.selectedEntryType === "theme" || libraryPanel.selectedZoneName !== "")

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.applySelected", "Apply selected entry")
                Accessible.focusable: true
                Accessible.onPressAction: applyMa.clicked(null)

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/ArrowLeft.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }

                MouseArea {
                    id: applyMa
                    anchors.fill: parent
                    enabled: parent.applyEnabled
                    onClicked: libraryPanel.applySelected()
                }
            }

            // Delete button
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30)
                Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: delMa.pressed ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.3) : "transparent"
                border.color: libraryPanel.canDeleteSelected ? Theme.errorColor : Theme.borderColor
                border.width: 1
                opacity: libraryPanel.canDeleteSelected ? 1.0 : 0.4

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.deleteSelected", "Delete selected entry")
                Accessible.focusable: true
                Accessible.onPressAction: delMa.clicked(null)

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/cross-filled.svg"  // Delete
                    sourceSize.width: Theme.scaled(14)
                    sourceSize.height: Theme.scaled(14)

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.errorColor
                    }
                }

                MouseArea {
                    id: delMa
                    anchors.fill: parent
                    enabled: libraryPanel.canDeleteSelected
                    onClicked: deleteConfirm.open()
                }
            }

            // Share button (local tab only)
            Rectangle {
                visible: libraryPanel.activeTab === "local"
                Layout.preferredWidth: Theme.scaled(30)
                Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: shareMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"
                border.color: WidgetLibrary.selectedEntryId !== "" ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                opacity: WidgetLibrary.selectedEntryId !== "" ? 1.0 : 0.4

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.shareToCommunity", "Share to community")
                Accessible.focusable: true
                Accessible.onPressAction: shareMa.clicked(null)

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/Upload.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }

                MouseArea {
                    id: shareMa
                    anchors.fill: parent
                    enabled: WidgetLibrary.selectedEntryId !== ""
                    onClicked: libraryPanel.captureAndUpload()
                }
            }

            Item { Layout.fillWidth: true }

            // Type filter: Items
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30); Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: libraryPanel.showItems ? Theme.primaryColor : "transparent"
                border.color: libraryPanel.showItems ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.filterItems", "Filter items") + (libraryPanel.showItems ? ", " + TranslationManager.translate("library.accessibility.on", "on") : ", " + TranslationManager.translate("library.accessibility.off", "off"))
                Accessible.focusable: true
                Accessible.onPressAction: filterItemsMa.clicked(null)
                Text {
                    anchors.centerIn: parent; text: "I"
                    color: libraryPanel.showItems ? Theme.primaryContrastColor : Theme.textSecondaryColor
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.scaled(12); font.bold: true
                    Accessible.ignored: true
                }
                MouseArea { id: filterItemsMa; anchors.fill: parent; onClicked: libraryPanel.showItems = !libraryPanel.showItems }
            }

            // Type filter: Zones
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30); Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: libraryPanel.showZones ? Theme.primaryColor : "transparent"
                border.color: libraryPanel.showZones ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.filterZones", "Filter zones") + (libraryPanel.showZones ? ", " + TranslationManager.translate("library.accessibility.on", "on") : ", " + TranslationManager.translate("library.accessibility.off", "off"))
                Accessible.focusable: true
                Accessible.onPressAction: filterZonesMa.clicked(null)
                Text {
                    anchors.centerIn: parent; text: "Z"
                    color: libraryPanel.showZones ? Theme.primaryContrastColor : Theme.textSecondaryColor
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.scaled(12); font.bold: true
                    Accessible.ignored: true
                }
                MouseArea { id: filterZonesMa; anchors.fill: parent; onClicked: libraryPanel.showZones = !libraryPanel.showZones }
            }

            // Type filter: Layouts
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30); Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: libraryPanel.showLayouts ? Theme.primaryColor : "transparent"
                border.color: libraryPanel.showLayouts ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.filterLayouts", "Filter layouts") + (libraryPanel.showLayouts ? ", " + TranslationManager.translate("library.accessibility.on", "on") : ", " + TranslationManager.translate("library.accessibility.off", "off"))
                Accessible.focusable: true
                Accessible.onPressAction: filterLayoutsMa.clicked(null)
                Text {
                    anchors.centerIn: parent; text: "L"
                    color: libraryPanel.showLayouts ? Theme.primaryContrastColor : Theme.textSecondaryColor
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.scaled(12); font.bold: true
                    Accessible.ignored: true
                }
                MouseArea { id: filterLayoutsMa; anchors.fill: parent; onClicked: libraryPanel.showLayouts = !libraryPanel.showLayouts }
            }

            // Type filter: Themes
            Rectangle {
                Layout.preferredWidth: Theme.scaled(30); Layout.preferredHeight: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: libraryPanel.showThemes ? Theme.primaryColor : "transparent"
                border.color: libraryPanel.showThemes ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("library.accessibility.filterThemes", "Filter themes") + (libraryPanel.showThemes ? ", " + TranslationManager.translate("library.accessibility.on", "on") : ", " + TranslationManager.translate("library.accessibility.off", "off"))
                Accessible.focusable: true
                Accessible.onPressAction: filterThemesMa.clicked(null)
                Text {
                    anchors.centerIn: parent; text: "T"
                    color: libraryPanel.showThemes ? Theme.primaryContrastColor : Theme.textSecondaryColor
                    font.family: Theme.captionFont.family
                    font.pixelSize: Theme.scaled(12); font.bold: true
                    Accessible.ignored: true
                }
                MouseArea { id: filterThemesMa; anchors.fill: parent; onClicked: libraryPanel.showThemes = !libraryPanel.showThemes }
            }
        }

        // Library entries list
        ListView {
            id: libraryList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(4)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            model: {
                var entries = libraryPanel.activeTab === "local" ? WidgetLibrary.entries
                            : libraryPanel.activeTab === "community" ? LibrarySharing.communityEntries
                            : []
                if (libraryPanel.showItems && libraryPanel.showZones && libraryPanel.showLayouts && libraryPanel.showThemes) return entries
                var result = []
                for (var i = 0; i < entries.length; i++) {
                    var t = entries[i].type || ""
                    if ((t === "item" && libraryPanel.showItems) ||
                        (t === "zone" && libraryPanel.showZones) ||
                        (t === "layout" && libraryPanel.showLayouts) ||
                        (t === "theme" && libraryPanel.showThemes))
                        result.push(entries[i])
                }
                return result
            }

            delegate: LibraryItemCard {
                id: entryCard
                required property var modelData

                entryData: entryCard.modelData
                displayMode: libraryPanel.displayMode
                isSelected: WidgetLibrary.selectedEntryId === (entryCard.modelData.id || "")

                onClicked: {
                    WidgetLibrary.selectedEntryId = entryCard.modelData.id || ""
                }
                onDoubleClicked: {
                    // TODO: Open apply dialog (zone picker for items/zones)
                    console.log("Apply entry:", entryCard.modelData.id)
                }
            }

            // Empty state
            Text {
                visible: libraryList.count === 0 && !LibrarySharing.browsing
                anchors.centerIn: parent
                text: libraryPanel.activeTab === "local"
                    ? TranslationManager.translate("library.emptyState.local", "No items in library.\nSelect a widget and click Add.")
                    : TranslationManager.translate("library.emptyState.community", "No entries found.")
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // Status indicators
        Text {
            visible: LibrarySharing.uploading
            text: TranslationManager.translate("library.status.uploading", "Uploading...")
            color: Theme.primaryColor
            font: Theme.captionFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            visible: LibrarySharing.browsing
            text: TranslationManager.translate("library.status.loading", "Loading...")
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Browse All button (for community/featured tabs)
        AccessibleButton {
            visible: libraryPanel.activeTab === "community"
            Layout.fillWidth: true
            text: TranslationManager.translate("library.button.browseAll", "Browse All")
            accessibleName: TranslationManager.translate("library.accessibility.browseAllCommunity", "Browse all community items")
            onClicked: {
                AppShell.communityBrowserRequested()
            }
        }
    }

    // Off-screen thumbnail renderers for upload (full + compact)
    // Uses layer.enabled to force FBO rendering (Android GPU skips off-screen items)
    Item {
        id: thumbContainer
        visible: false
        width: Theme.scaled(280)
        height: Math.max(thumbCardFull.height, thumbCardCompact.height)
        layer.enabled: visible  // Force offscreen framebuffer rendering for grabToImage

        LibraryItemCard {
            id: thumbCardFull
            width: parent.width
            displayMode: 0
            entryData: ({})
            isSelected: false
            showBadge: false
            livePreview: true
        }

        LibraryItemCard {
            id: thumbCardCompact
            y: thumbCardFull.height + Theme.scaled(4)
            width: parent.width
            displayMode: 1
            entryData: ({})
            isSelected: false
            showBadge: false
            livePreview: true
        }
    }

    // Timer to allow thumbnail components to fully render before capture (community upload)
    Timer {
        id: captureTimer
        interval: 200
        repeat: false
        property string captureEntryId: ""
        onTriggered: {
            thumbCardFull.grabToImage(function(fullResult) {
                thumbCardCompact.grabToImage(function(compactResult) {
                    thumbContainer.visible = false
                    // Save locally as well (caches for web editor + library panel)
                    WidgetLibrary.saveThumbnail(captureEntryId, fullResult.image)
                    WidgetLibrary.saveThumbnailCompact(captureEntryId, compactResult.image)
                    LibrarySharing.uploadEntryWithThumbnails(captureEntryId,
                        fullResult.image, compactResult.image)
                }, Qt.size(Theme.scaled(280), thumbCardCompact.height))
            }, Qt.size(Theme.scaled(280), thumbCardFull.height))
        }
    }

    // Track pending apply-after-download
    property string pendingApplyZone: ""

    function applySelected() {
        var entryId = WidgetLibrary.selectedEntryId
        if (!entryId) return

        // Local entry - apply directly
        var entry = WidgetLibrary.getEntry(entryId)
        if (entry && entry.type) {
            if (entry.type !== "layout" && entry.type !== "theme" && !selectedZoneName) {
                showToast(TranslationManager.translate("library.toast.selectZone", "Select a zone to apply to"), Theme.warningColor)
                return
            }
            applyEntry(entryId, entry.type, selectedZoneName)
            return
        }

        // Community entry - need to find the type from community data, then download first
        var entries = LibrarySharing.communityEntries
        var type = ""
        for (var i = 0; i < entries.length; i++) {
            if (entries[i].id === entryId) {
                type = entries[i].type || ""
                break
            }
        }
        if (!type) return
        if (type !== "layout" && type !== "theme" && !selectedZoneName) {
            showToast(TranslationManager.translate("library.toast.selectZone", "Select a zone to apply to"), Theme.warningColor)
            return
        }

        pendingApplyZone = selectedZoneName
        showToast(TranslationManager.translate("library.toast.downloading", "Downloading..."), Theme.primaryColor)
        LibrarySharing.downloadEntry(entryId)
    }

    function applyEntry(entryId, type, zoneName) {
        switch (type) {
            case "item":
                WidgetLibrary.applyItem(entryId, zoneName)
                break
            case "zone":
                WidgetLibrary.applyZone(entryId, zoneName)
                break
            case "layout":
                WidgetLibrary.applyLayout(entryId)
                break
            case "theme":
                WidgetLibrary.applyThemeEntry(entryId)
                break
        }
    }

    function captureAndUpload() {
        var entryId = WidgetLibrary.selectedEntryId
        if (!entryId) return

        var fullEntry = WidgetLibrary.getEntryData(entryId)
        if (!fullEntry || !fullEntry.type) {
            LibrarySharing.uploadEntry(entryId)
            return
        }
        thumbCardFull.entryData = fullEntry
        thumbCardCompact.entryData = fullEntry

        // Show container behind content (z: -1 keeps it invisible to user)
        // Using layer.enabled forces FBO rendering so grabToImage works on Android
        thumbContainer.z = -1
        thumbContainer.visible = true

        // Wait for complex components (CustomItem, zones) to render before capture
        captureTimer.captureEntryId = entryId
        captureTimer.start()
    }

    // Upload/sharing feedback
    Connections {
        target: LibrarySharing
        function onUploadSuccess(serverId) {
            libraryPanel.showToast(TranslationManager.translate("library.toast.sharedSuccess", "Shared successfully!"), Theme.successColor)
        }
        function onUploadFailed(error) {
            if (error === "Already shared")
                libraryPanel.showToast(TranslationManager.translate("library.toast.alreadyShared", "Already shared"), Theme.warningColor)
            else
                libraryPanel.showToast(TranslationManager.translate("library.toast.uploadFailed", "Upload failed: ") + error, Theme.errorColor)
        }
        function onDeleteSuccess() {
            libraryPanel.showToast(TranslationManager.translate("library.toast.deletedFromServer", "Deleted from server"), Theme.successColor)
            // Refresh community list
            LibrarySharing.browseCommunity("", "", "", "", "newest", 1)
        }
        function onDeleteFailed(error) {
            libraryPanel.showToast(TranslationManager.translate("library.toast.deleteFailed", "Delete failed: ") + error, Theme.errorColor)
        }
        function onDownloadComplete(localEntryId) {
            if (libraryPanel.pendingApplyZone) {
                var entry = WidgetLibrary.getEntry(localEntryId)
                if (entry && entry.type) {
                    libraryPanel.applyEntry(localEntryId, entry.type, libraryPanel.pendingApplyZone)
                    libraryPanel.showToast(TranslationManager.translate("library.toast.applied", "Applied!"), Theme.successColor)
                }
                libraryPanel.pendingApplyZone = ""
            }
        }
        function onDownloadFailed(error) {
            if (libraryPanel.pendingApplyZone) {
                libraryPanel.showToast(TranslationManager.translate("library.toast.downloadFailed", "Download failed: ") + error, Theme.errorColor)
                libraryPanel.pendingApplyZone = ""
            }
        }
    }

    function showToast(message, color) {
        toastText.text = message
        toastBg.border.color = color || Theme.borderColor
        toastBg.visible = true
        toastTimer.restart()
    }

    Rectangle {
        id: toastBg
        visible: false
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(8)
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(toastText.implicitWidth + Theme.scaled(24), libraryPanel.width - Theme.scaled(16))
        height: Theme.scaled(32)
        radius: Theme.scaled(16)
        color: Theme.surfaceColor
        border.width: 1
        z: 10

        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.textColor
            font: Theme.captionFont
            elide: Text.ElideRight
            width: parent.width - Theme.scaled(16)
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Timer {
        id: toastTimer
        interval: 3000
        onTriggered: toastBg.visible = false
    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirm
        anchors.centerIn: parent
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
                text: libraryPanel.activeTab === "community"
                    ? TranslationManager.translate("library.dialog.deleteFromServer", "Delete from server?")
                    : TranslationManager.translate("library.dialog.deleteEntry", "Delete this library entry?")
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("library.accessibility.cancelDeletion", "Cancel deletion")
                    onClicked: deleteConfirm.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.delete", "Delete")
                    accessibleName: TranslationManager.translate("library.accessibility.confirmDelete", "Confirm delete")
                    onClicked: {
                        if (libraryPanel.activeTab === "community") {
                            LibrarySharing.deleteFromServer(WidgetLibrary.selectedEntryId)
                        } else {
                            WidgetLibrary.removeEntry(WidgetLibrary.selectedEntryId)
                        }
                        WidgetLibrary.selectedEntryId = ""
                        deleteConfirm.close()
                    }
                }
            }
        }
    }
}
