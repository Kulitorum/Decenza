import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import ".."

Rectangle {
    id: libraryPanel

    // Interface with layout editor
    property string selectedItemId: ""
    property string selectedFromZone: ""
    property string selectedZoneName: ""

    // Display state
    property int displayMode: 0  // 0=full, 1=compact
    property string activeTab: "local"  // "local", "community"

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
                return entries[i].deviceId === Settings.deviceId()
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
                text: "Library"
                color: Theme.textColor
                font: Theme.subtitleFont
                Layout.fillWidth: true
            }

            // Full preview mode button
            Rectangle {
                width: Theme.scaled(28)
                height: Theme.scaled(28)
                radius: Theme.scaled(4)
                color: displayMode === 0 ? Theme.primaryColor : "transparent"
                border.color: displayMode === 0 ? Theme.primaryColor : Theme.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "\u25A3"  // Grid icon
                    color: displayMode === 0 ? "white" : Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(14)
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: displayMode = 0
                }
            }

            // Compact list mode button
            Rectangle {
                width: Theme.scaled(28)
                height: Theme.scaled(28)
                radius: Theme.scaled(4)
                color: displayMode === 1 ? Theme.primaryColor : "transparent"
                border.color: displayMode === 1 ? Theme.primaryColor : Theme.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "\u2630"  // List icon
                    color: displayMode === 1 ? "white" : Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(14)
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: displayMode = 1
                }
            }
        }

        // Tab row
        Item {
            Layout.fillWidth: true
            height: Theme.scaled(28)

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
                        { key: "local", label: "My Library" },
                        { key: "community", label: "Community" }
                    ]

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        // Active tab shape
                        Rectangle {
                            visible: activeTab === modelData.key
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

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: activeTab === modelData.key ? Theme.textColor : Theme.textSecondaryColor
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.scaled(11)
                            font.bold: activeTab === modelData.key
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: activeTab = modelData.key
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
                width: Theme.scaled(30)
                height: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: addMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"
                border.color: Theme.primaryColor
                border.width: 1

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/plus.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                }

                MouseArea {
                    id: addMa
                    anchors.fill: parent
                    onClicked: addMenu.open()
                }

                Popup {
                    id: addMenu
                    y: parent.height + Theme.scaled(4)
                    padding: Theme.scaled(4)
                    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

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
                                { label: "Save Item", type: "item", enabled: selectedItemId !== "" },
                                { label: "Save Zone", type: "zone", enabled: selectedZoneName !== "" },
                                { label: "Save Layout", type: "layout", enabled: true }
                            ]

                            Rectangle {
                                Layout.fillWidth: true
                                implicitWidth: Theme.scaled(140)
                                height: Theme.scaled(32)
                                radius: Theme.scaled(4)
                                color: menuItemMa.containsMouse && modelData.enabled
                                    ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12)
                                    : "transparent"
                                opacity: modelData.enabled ? 1.0 : 0.4

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.scaled(10)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: Theme.textColor
                                    font: Theme.bodyFont
                                }

                                MouseArea {
                                    id: menuItemMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: modelData.enabled
                                    onClicked: {
                                        addMenu.close()
                                        switch (modelData.type) {
                                            case "item":
                                                WidgetLibrary.addItemFromLayout(selectedItemId)
                                                break
                                            case "zone":
                                                WidgetLibrary.addZoneFromLayout(selectedZoneName)
                                                break
                                            case "layout":
                                                WidgetLibrary.addCurrentLayout(false)
                                                break
                                        }
                                        activeTab = "local"
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Apply to zone button
            Rectangle {
                width: Theme.scaled(30)
                height: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: applyMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"
                border.color: applyEnabled ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                opacity: applyEnabled ? 1.0 : 0.4

                property bool applyEnabled: WidgetLibrary.selectedEntryId !== "" && (selectedEntryType === "layout" || selectedZoneName !== "")

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/ArrowLeft.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                }

                MouseArea {
                    id: applyMa
                    anchors.fill: parent
                    enabled: parent.applyEnabled
                    onClicked: applySelected()
                }
            }

            // Delete button
            Rectangle {
                width: Theme.scaled(30)
                height: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: delMa.pressed ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.3) : "transparent"
                border.color: canDeleteSelected ? Theme.errorColor : Theme.borderColor
                border.width: 1
                opacity: canDeleteSelected ? 1.0 : 0.4

                Text {
                    anchors.centerIn: parent
                    text: "\uD83D\uDDD1"  // Wastebasket
                    font.pixelSize: Theme.scaled(14)
                }

                MouseArea {
                    id: delMa
                    anchors.fill: parent
                    enabled: canDeleteSelected
                    onClicked: deleteConfirm.open()
                }
            }

            // Share button (local tab only)
            Rectangle {
                visible: activeTab === "local"
                width: Theme.scaled(30)
                height: Theme.scaled(30)
                radius: Theme.scaled(4)
                color: shareMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"
                border.color: WidgetLibrary.selectedEntryId !== "" ? Theme.primaryColor : Theme.borderColor
                border.width: 1
                opacity: WidgetLibrary.selectedEntryId !== "" ? 1.0 : 0.4

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/Upload.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                }

                MouseArea {
                    id: shareMa
                    anchors.fill: parent
                    enabled: WidgetLibrary.selectedEntryId !== ""
                    onClicked: captureAndUpload()
                }
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
                if (activeTab === "local") return WidgetLibrary.entries
                if (activeTab === "community") return LibrarySharing.communityEntries
                return []
            }

            delegate: LibraryItemCard {
                entryData: modelData
                displayMode: libraryPanel.displayMode
                isSelected: WidgetLibrary.selectedEntryId === (modelData.id || "")

                onClicked: {
                    WidgetLibrary.selectedEntryId = modelData.id || ""
                }
                onDoubleClicked: {
                    // TODO: Open apply dialog (zone picker for items/zones)
                    console.log("Apply entry:", modelData.id)
                }
            }

            // Empty state
            Text {
                visible: libraryList.count === 0 && !LibrarySharing.browsing
                anchors.centerIn: parent
                text: activeTab === "local"
                    ? "No items in library.\nSelect a widget and click Add."
                    : "No entries found."
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // Status indicators
        Text {
            visible: LibrarySharing.uploading
            text: "Uploading..."
            color: Theme.primaryColor
            font: Theme.captionFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            visible: LibrarySharing.browsing
            text: "Loading..."
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Browse All button (for community/featured tabs)
        AccessibleButton {
            visible: activeTab === "community"
            Layout.fillWidth: true
            text: "Browse All"
            accessibleName: "Browse all community items"
            onClicked: {
                pageStack.push(Qt.resolvedUrl("../../pages/CommunityBrowserPage.qml"))
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
        }

        LibraryItemCard {
            id: thumbCardCompact
            y: thumbCardFull.height + Theme.scaled(4)
            width: parent.width
            displayMode: 1
            entryData: ({})
            isSelected: false
            showBadge: false
        }
    }

    // Timer to allow thumbnail components to fully render before capture
    Timer {
        id: captureTimer
        interval: 200
        repeat: false
        property string captureEntryId: ""
        onTriggered: {
            thumbCardFull.grabToImage(function(fullResult) {
                thumbCardCompact.grabToImage(function(compactResult) {
                    thumbContainer.visible = false
                    LibrarySharing.uploadEntryWithThumbnails(captureEntryId,
                        fullResult.image, compactResult.image)
                }, Qt.size(Theme.scaled(280), thumbCardCompact.height))
            }, Qt.size(Theme.scaled(280), thumbCardFull.height))
        }
    }

    // Timer for local-only thumbnail capture (triggered by C++ signal, e.g. from web editor)
    Timer {
        id: localCaptureTimer
        interval: 200
        repeat: false
        property string captureEntryId: ""
        onTriggered: {
            thumbCardFull.grabToImage(function(fullResult) {
                WidgetLibrary.saveThumbnail(captureEntryId, fullResult.image)
                thumbContainer.visible = false
            }, Qt.size(Theme.scaled(280), thumbCardFull.height))
        }
    }

    // Handle thumbnail capture requests from C++ (e.g. when web editor saves to library)
    Connections {
        target: WidgetLibrary
        function onRequestThumbnailCapture(entryId) {
            var entryData = WidgetLibrary.getEntryData(entryId)
            if (!entryData || !entryData.type) return

            thumbCardFull.entryData = entryData
            thumbCardCompact.entryData = entryData
            thumbContainer.z = -1
            thumbContainer.visible = true

            localCaptureTimer.captureEntryId = entryId
            localCaptureTimer.start()
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
            if (entry.type !== "layout" && !selectedZoneName) {
                showToast("Select a zone to apply to", Theme.warningColor)
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
        if (type !== "layout" && !selectedZoneName) {
            showToast("Select a zone to apply to", Theme.warningColor)
            return
        }

        pendingApplyZone = selectedZoneName
        showToast("Downloading...", Theme.primaryColor)
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
            showToast("Shared successfully!", Theme.successColor)
        }
        function onUploadFailed(error) {
            if (error === "Already shared")
                showToast("Already shared", Theme.warningColor)
            else
                showToast("Upload failed: " + error, Theme.errorColor)
        }
        function onDeleteSuccess() {
            showToast("Deleted from server", Theme.successColor)
            // Refresh community list
            LibrarySharing.browseCommunity("", "", "", "", "newest", 1)
        }
        function onDeleteFailed(error) {
            showToast("Delete failed: " + error, Theme.errorColor)
        }
        function onDownloadComplete(localEntryId) {
            if (pendingApplyZone) {
                var entry = WidgetLibrary.getEntry(localEntryId)
                if (entry && entry.type) {
                    applyEntry(localEntryId, entry.type, pendingApplyZone)
                    showToast("Applied!", Theme.successColor)
                }
                pendingApplyZone = ""
            }
        }
        function onDownloadFailed(error) {
            if (pendingApplyZone) {
                showToast("Download failed: " + error, Theme.errorColor)
                pendingApplyZone = ""
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
    Popup {
        id: deleteConfirm
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
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
                text: activeTab === "community"
                    ? "Delete from server?"
                    : "Delete this library entry?"
                color: Theme.textColor
                font: Theme.subtitleFont
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: "Cancel"
                    accessibleName: "Cancel deletion"
                    onClicked: deleteConfirm.close()
                }

                AccessibleButton {
                    text: "Delete"
                    accessibleName: "Confirm delete"
                    onClicked: {
                        if (activeTab === "community") {
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
