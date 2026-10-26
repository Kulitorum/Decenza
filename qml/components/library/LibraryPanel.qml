// The save-menu and entry-card Repeater/ListView delegates read this file's
// `libraryPanel` id; Bound makes it statically resolvable. Each declares its one
// injected role, `modelData`, required in the same edit -- without that, Bound stops
// role injection and the save menu and the whole library grid render blank at
// RUNTIME, silently. (The `layer.effect` blocks read only `Theme`, a singleton, so they
// need nothing from the pragma.)
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
    // Selection belongs to this panel instance. It must not be shared through the
    // host singleton when native and browser clients coexist.
    property string selectedEntryId: ""
    property var pendingOperations: ({})

    // Type filters (all on by default)
    property bool showItems: true
    property bool showZones: true
    property bool showLayouts: true
    property bool showThemes: true

    // Type of the currently selected entry ("item", "zone", "layout", or "")
    readonly property string selectedEntryType: {
        var entry = selectedEntryId ? findEntry(LibraryService.localEntries, selectedEntryId) : null
        return entry ? entry.type || "" : ""
    }

    readonly property bool canDeleteSelected: selectedEntryId !== ""

    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.color: Theme.borderColor
    border.width: 1

    function findEntry(entries, id) {
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].id === id) return entries[i]
        return null
    }

    function dispatch(operation, args, context) {
        const requestId = LibraryService.newRequestId()
        const pending = Object.assign({}, pendingOperations)
        pending[requestId] = { operation: operation, context: context || ({}) }
        pendingOperations = pending
        LibraryService.execute(operation, args || ({}), requestId)
        return requestId
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

                DecenzaDialog {
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
                                { label: TranslationManager.translate("library.menu.saveLayout", "Save Layout"), type: "layout", enabled: true },
                                { label: TranslationManager.translate("library.menu.saveTheme", "Save current theme"), type: "theme", enabled: true }
                            ]

                            Rectangle {
                                id: saveMenuRow
                                required property var modelData

                                Layout.fillWidth: true
                                implicitWidth: Math.max(Theme.scaled(140), saveMenuLabel.implicitWidth + Theme.scaled(24))
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
                                    id: saveMenuLabel
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
                                                libraryPanel.dispatch("addItem", { itemId: libraryPanel.selectedItemId })
                                                break
                                            case "zone":
                                                libraryPanel.dispatch("addZone", { zoneName: libraryPanel.selectedZoneName })
                                                break
                                            case "layout":
                                                libraryPanel.dispatch("addLayout", { includeTheme: false })
                                                break
                                            case "theme":
                                                libraryPanel.dispatch("addTheme", { name: ThemeManagementService.activeThemeName })
                                                break
                                        }
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

                property bool applyEnabled: libraryPanel.selectedEntryId !== "" && (libraryPanel.selectedEntryType === "layout" || libraryPanel.selectedEntryType === "theme" || libraryPanel.selectedZoneName !== "")

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
                var entries = LibraryService.localEntries
                if (libraryPanel.showItems && libraryPanel.showZones && libraryPanel.showLayouts && libraryPanel.showThemes) return entries
                var result = []
                for (let i = 0; i < entries.length; i++) {
                    let t = entries[i].type || ""
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
                isSelected: libraryPanel.selectedEntryId === (entryCard.modelData.id || "")

                onClicked: {
                    libraryPanel.selectedEntryId = entryCard.modelData.id || ""
                }
                onDoubleClicked: {
                    // Same selection and apply path as the toolbar, so zone validation
                    // and correlated host writes stay unified.
                    libraryPanel.selectedEntryId = entryCard.modelData.id || ""
                    libraryPanel.applySelected()
                }
            }

            // Empty state
            Text {
                visible: libraryList.count === 0 && LibraryService.ready
                anchors.centerIn: parent
                text: TranslationManager.translate("library.emptyState.local", "No items in library.\nSelect a widget and click Add.")
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Text {
            visible: !LibraryService.ready
            text: LibraryService.errorMessage || TranslationManager.translate("library.status.loading", "Loading...")
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }

    function applySelected() {
        const entryId = selectedEntryId
        if (!entryId) return
        const entry = findEntry(LibraryService.localEntries, entryId)
        if (!entry || !entry.type) return
        if (entry.type !== "layout" && entry.type !== "theme" && !selectedZoneName) {
            showToast(TranslationManager.translate("library.toast.selectZone", "Select a zone to apply to"), Theme.warningColor)
            return
        }
        applyEntry(entryId, entry.type, selectedZoneName)
    }

    function applyEntry(entryId, type, zoneName) {
        const args = { entryId: entryId }
        let operation = ""
        if (type === "item" || type === "zone") {
            operation = type === "item" ? "applyItem" : "applyZone"
            args.targetZone = zoneName
            args.expectedLayout = LayoutService.layoutConfiguration
        } else if (type === "layout") {
            operation = "applyLayout"
            args.applyTheme = false
            args.expectedLayout = LayoutService.layoutConfiguration
        } else if (type === "theme") {
            operation = "applyTheme"
            args.expectedTheme = Settings.theme.customThemeColors
        }
        if (operation !== "") dispatch(operation, args, { action: "apply" })
    }

    // Every async result is correlated to the operation issued by this panel.
    Connections {
        target: LibraryService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            const pending = libraryPanel.pendingOperations[requestId]
            if (!pending || pending.operation !== operation) return
            const updated = Object.assign({}, libraryPanel.pendingOperations)
            delete updated[requestId]
            libraryPanel.pendingOperations = updated
            const context = pending.context || ({})
            if (!success) {
                libraryPanel.showToast(reason || TranslationManager.translate("common.operationFailed", "Operation failed"), Theme.errorColor)
                return
            }
            if (context.action === "apply") {
                libraryPanel.showToast(TranslationManager.translate("library.toast.applied", "Applied!"), Theme.successColor)
            } else if (context.action === "deleteLocal") {
                libraryPanel.selectedEntryId = ""
                libraryPanel.showToast(TranslationManager.translate("library.toast.deleted", "Deleted"), Theme.successColor)
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
    DecenzaDialog {
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
                text: TranslationManager.translate("library.dialog.deleteEntry", "Delete this library entry?")
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
                        libraryPanel.dispatch("removeLocal", { entryId: libraryPanel.selectedEntryId }, { action: "deleteLocal" })
                        deleteConfirm.close()
                    }
                }
            }
        }
    }
}
