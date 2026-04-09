import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

Page {
    id: multiImportPage
    objectName: "visualizerMultiImportPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("visualizerImport.pageTitle", "Import Shared Profiles")
        // Auto-fetch shared profiles on page load
        MainController.visualizerImporter.fetchSharedShots()
    }
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("visualizerImport.pageTitle", "Import Shared Profiles")

    property var selectedShot: null
    property bool showCodeInput: false
    property int selectionVersion: 0  // Increment to force binding updates
    property var importedIds: ({})  // Track profiles imported in this session

    // Rename dialog for D profiles
    property bool showRenameDialog: false
    property string renameProfileId: ""
    property string renameProfileTitle: ""

    function showRenameForBuiltIn(shot) {
        renameProfileId = shot.id
        renameProfileTitle = shot.profile_title + " " + TranslationManager.translate("visualizerImport.copySuffix", "(copy)")
        showRenameDialog = true
        renameInput.text = renameProfileTitle
        renameInput.forceActiveFocus()
    }

    function importProfile(shot) {
        // Import immediately by shot ID
        renameProfileId = shot.id  // Track for marking as imported on success
        MainController.visualizerImporter.importFromShotId(shot.id)
    }

    // Source badge colors (matching ProfileSelectorPage)
    function sourceColor(source) {
        if (source === "B") return Theme.sourceBadgeBlueColor  // Blue for Built-in/Decent
        if (source === "D") return Theme.sourceBadgeGreenColor  // Green for Downloaded/Visualizer
        return Theme.sourceBadgeOrangeColor  // Orange for User
    }

    function sourceLetter(source) {
        if (source === "B") return "D"  // Decent
        if (source === "D") return "V"  // Visualizer
        return "U"  // User
    }

    Connections {
        target: MainController.visualizerImporter

        function onSharedShotsChanged() {
            selectedShot = null
            // Don't clear importedIds - keep tracking what was imported this session
            selectionVersion++  // Force UI rebind to show updated checkmarks
        }

        function onImportSuccess(profileTitle) {
            // Mark the profile as imported
            if (renameProfileId !== "") {
                importedIds[renameProfileId] = true
                selectionVersion++
                renameProfileId = ""
            }
            resultText.text = TranslationManager.translate("visualizerImport.imported", "Imported:") + " " + profileTitle
            resultText.color = Theme.successColor
            resultText.visible = true
            resultTimer.restart()
        }

        function onImportFailed(error) {
            resultText.text = TranslationManager.translate("visualizerImport.error", "Error:") + " " + error
            resultText.color = Theme.errorColor
            resultText.visible = true
            resultTimer.restart()
        }
    }

    Timer {
        id: resultTimer
        interval: 5000
        onTriggered: resultText.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Result message
        Text {
            id: resultText
            Layout.fillWidth: true
            visible: false
            color: Theme.successColor
            font: Theme.bodyFont
            horizontalAlignment: Text.AlignHCenter
        }

        // Instructions (shown when no shots loaded)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: instructionsCol.height + Theme.scaled(30)
            color: Theme.surfaceColor
            radius: Theme.scaled(8)
            visible: MainController.visualizerImporter.sharedShots.length === 0 &&
                     !MainController.visualizerImporter.fetching

            Column {
                id: instructionsCol
                anchors.centerIn: parent
                width: parent.width - Theme.scaled(40)
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("visualizerImport.heading", "Import Multiple Profiles from Visualizer")
                    color: Theme.textColor
                    font: Theme.headingFont
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    text: TranslationManager.translate("visualizerImport.description", "This feature lets you import profiles from shots you've shared on visualizer.coffee in the last hour.")
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Rectangle {
                    width: parent.width
                    height: Theme.scaled(1)
                    color: Theme.textSecondaryColor
                    opacity: 0.3
                }

                Text {
                    text: TranslationManager.translate("visualizerImport.howToUse", "How to use:")
                    color: Theme.textColor
                    font.bold: true
                    font.pixelSize: Theme.bodyFont.pixelSize
                }

                Text {
                    text: TranslationManager.translate("visualizerImport.step1", "1. Go to visualizer.coffee and find shots with profiles you want")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Text {
                    text: TranslationManager.translate("visualizerImport.step2", "2. Click the 'Share' button on each shot (creates a temporary share link)")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Text {
                    text: TranslationManager.translate("visualizerImport.step3", "3. Come back here and tap 'Fetch Shared Profiles'")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Text {
                    text: TranslationManager.translate("visualizerImport.step4", "4. Select which profiles to import and tap 'Import Selected'")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }
        }

        // Action buttons row
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)
            spacing: Theme.spacingMedium

            // Loading indicator
            BusyIndicator {
                running: MainController.visualizerImporter.fetching
                Layout.preferredWidth: Theme.scaled(30)
                Layout.preferredHeight: Theme.scaled(30)
                visible: running
            }

            Text {
                text: MainController.visualizerImporter.fetching ? TranslationManager.translate("visualizerImport.loading", "Loading profiles...") : ""
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                visible: MainController.visualizerImporter.fetching
            }

            Item { Layout.fillWidth: true }

            // Refresh button
            AccessibleButton {
                id: refreshButton
                text: TranslationManager.translate("visualizerImport.refresh", "Refresh")
                accessibleName: TranslationManager.translate("visualizerMultiImport.refreshSharedShots", "Refresh shared shots list from visualizer")
                Layout.preferredWidth: Theme.scaled(100)
                Layout.preferredHeight: Theme.scaled(40)
                enabled: !MainController.visualizerImporter.fetching &&
                         !MainController.visualizerImporter.importing
                visible: !showCodeInput

                onClicked: MainController.visualizerImporter.fetchSharedShots()

                background: Rectangle {
                    radius: Theme.scaled(6)
                    color: Theme.surfaceColor
                    border.color: refreshButton.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                    border.width: 1
                }

                contentItem: Text {
                    text: refreshButton.text
                    color: refreshButton.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                    font: Theme.captionFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // Add by code button / input
            Row {
                spacing: Theme.spacingSmall
                visible: !MainController.visualizerImporter.fetching

                AccessibleButton {
                    id: addByCodeButton
                    text: TranslationManager.translate("visualizerImport.addByCode", "Add by Code")
                    accessibleName: TranslationManager.translate("visualizerMultiImport.enterShareCode", "Enter a 4-character share code to import a profile")
                    primary: true
                    width: Theme.scaled(120)
                    height: Theme.scaled(40)
                    visible: !showCodeInput

                    onClicked: {
                        showCodeInput = true
                        codeInput.text = ""
                        codeInput.forceActiveFocus()
                    }
                }

                // Code input field (shown when Add by Code is clicked)
                Row {
                    spacing: Theme.spacingSmall
                    visible: showCodeInput

                    StyledTextField {
                        id: codeInput
                        width: Theme.scaled(80)
                        height: Theme.scaled(40)
                        font.pixelSize: Theme.scaled(16)
                        horizontalAlignment: Text.AlignHCenter
                        maximumLength: 4
                        placeholder: TranslationManager.translate("visualizerImport.codePlaceholder", "CODE")
                        accessibleName: TranslationManager.translate("visualizerImport.shareCode", "Share code")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

                        onTextChanged: {
                            var upper = text.toUpperCase()
                            if (text !== upper) text = upper
                        }

                        Keys.onReturnPressed: {
                            if (text.length === 4) {
                                MainController.visualizerImporter.importFromShareCode(text)
                                showCodeInput = false
                            }
                        }
                        Keys.onEscapePressed: showCodeInput = false
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("visualizerImport.add", "Add")
                        accessibleName: TranslationManager.translate("visualizerMultiImport.importByCode", "Import profile using entered share code")
                        primary: true
                        width: Theme.scaled(60)
                        height: Theme.scaled(40)
                        enabled: codeInput.text.length === 4

                        onClicked: {
                            MainController.visualizerImporter.importFromShareCode(codeInput.text)
                            showCodeInput = false
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("visualizerImport.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("visualizerMultiImport.cancelShareCode", "Cancel entering share code")
                        width: Theme.scaled(60)
                        height: Theme.scaled(40)

                        onClicked: showCodeInput = false
                    }
                }
            }
        }

        // Main content: list on left, details on right
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: MainController.visualizerImporter.sharedShots.length > 0

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingMedium

                // Profile list
                Rectangle {
                    Layout.preferredWidth: parent.width * 0.45
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.scaled(8)

                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                        spacing: Theme.scaled(0)

                        // Header with select all
                        RowLayout {
                            width: parent.width
                            height: Theme.scaled(40)

                            Text {
                                text: TranslationManager.translate("visualizerImport.sharedProfiles", "Shared Profiles") + " (" + MainController.visualizerImporter.sharedShots.length + ")"
                                color: Theme.textColor
                                font: Theme.bodyFont
                                Layout.fillWidth: true
                            }

                            Text {
                                text: TranslationManager.translate("visualizerImport.tapToImport", "Tap ☆ to import")
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        ListView {
                            id: shotList
                            width: parent.width
                            height: parent.height - Theme.scaled(45)
                            clip: true
                            model: MainController.visualizerImporter.sharedShots

                            delegate: Rectangle {
                                id: shotDelegate
                                width: shotList.width
                                height: Math.max(Theme.scaled(60), vizContentRow.implicitHeight + Theme.spacingSmall * 2)
                                color: selectedShot === modelData ? Theme.primaryColor.darker(1.5) :
                                       (index % 2 === 0 ? "transparent" : Theme.backgroundColor)

                                // Include selectionVersion to force re-evaluation when imports change
                                property bool isImported: selectionVersion >= 0 && importedIds[modelData.id] === true

                                RowLayout {
                                    id: vizContentRow
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    spacing: Theme.spacingSmall

                                    // Import star or status indicator
                                    Rectangle {
                                        Layout.preferredWidth: Theme.scaled(36)
                                        Layout.preferredHeight: Theme.scaled(36)
                                        color: "transparent"

                                        property bool isBuiltIn: modelData.source === "B"
                                        property bool isIdentical: modelData.identical === true
                                        property bool isInvalid: modelData.invalid === true

                                        // Can click to import:
                                        // - Invalid profiles: no
                                        // - New profiles: yes (import immediately)
                                        // - Built-in with different frames: yes (shows rename dialog)
                                        // - Already imported this session: no
                                        // - Identical: no
                                        property bool canImport: {
                                            if (isInvalid) return false  // Invalid profile
                                            if (shotDelegate.isImported) return false  // Already imported
                                            if (isIdentical) return false  // Already have same frames
                                            return true  // New or different = can import
                                        }

                                        Accessible.role: Accessible.Button
                                        Accessible.name: {
                                            if (isInvalid) return TranslationManager.translate("visualizer.import.invalid", "Invalid profile")
                                            if (!canImport) return TranslationManager.translate("visualizer.import.alreadyimported", "Already imported")
                                            return TranslationManager.translate("visualizer.import.importprofile", "Import profile")
                                        }
                                        Accessible.focusable: true
                                        Accessible.onPressAction: importStarArea.clicked(null)

                                        // Red X for invalid profiles
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: Theme.scaled(24)
                                            height: Theme.scaled(24)
                                            radius: Theme.scaled(12)
                                            color: Theme.errorColor
                                            visible: parent.isInvalid

                                            Text {
                                                anchors.centerIn: parent
                                                text: "✕"
                                                color: Theme.primaryContrastColor
                                                font.pixelSize: Theme.scaled(14)
                                                font.bold: true
                                            }
                                        }

                                        // Checkmark for identical or already imported
                                        ColoredIcon {
                                            anchors.centerIn: parent
                                            source: "qrc:/icons/tick.svg"
                                            iconWidth: Theme.scaled(18)
                                            iconHeight: Theme.scaled(18)
                                            iconColor: Theme.successColor
                                            visible: !parent.isInvalid && !parent.canImport
                                        }

                                        // Star for importable profiles
                                        ColoredIcon {
                                            anchors.centerIn: parent
                                            source: "qrc:/icons/star-outline.svg"
                                            iconWidth: Theme.scaled(20)
                                            iconHeight: Theme.scaled(20)
                                            iconColor: Theme.textSecondaryColor
                                            visible: parent.canImport && !MainController.visualizerImporter.importing
                                        }

                                        // Loading indicator when importing
                                        BusyIndicator {
                                            anchors.centerIn: parent
                                            width: Theme.scaled(24)
                                            height: Theme.scaled(24)
                                            running: MainController.visualizerImporter.importing && renameProfileId === modelData.id
                                            visible: running
                                        }

                                        MouseArea {
                                            id: importStarArea
                                            anchors.fill: parent
                                            enabled: parent.canImport && !MainController.visualizerImporter.importing
                                            onClicked: {
                                                if (parent.isBuiltIn && modelData.exists) {
                                                    // D profile with different frames - show rename dialog
                                                    showRenameForBuiltIn(modelData)
                                                } else {
                                                    // Import immediately
                                                    importProfile(modelData)
                                                }
                                            }
                                        }
                                    }

                                    // Profile info
                                    Column {
                                        Layout.fillWidth: true
                                        spacing: Theme.scaled(2)

                                        Text {
                                            text: modelData.profile_title || TranslationManager.translate("visualizerImport.unknownProfile", "Unknown Profile")
                                            color: Theme.textColor
                                            font: Theme.bodyFont
                                            elide: Text.ElideRight
                                            width: parent.width
                                        }

                                        Text {
                                            text: (modelData.bean_brand ? modelData.bean_brand + " " : "") +
                                                  (modelData.bean_type || "")
                                            color: Theme.textSecondaryColor
                                            font: Theme.captionFont
                                            elide: Text.ElideRight
                                            width: parent.width
                                            visible: modelData.bean_brand || modelData.bean_type
                                        }
                                    }

                                    // Status badge
                                    Rectangle {
                                        visible: modelData.exists
                                        Layout.preferredWidth: Theme.scaled(24)
                                        Layout.preferredHeight: Theme.scaled(24)
                                        radius: Theme.scaled(12)
                                        color: sourceColor(modelData.source)

                                        Text {
                                            anchors.centerIn: parent
                                            text: sourceLetter(modelData.source)
                                            color: Theme.primaryContrastColor
                                            font.pixelSize: Theme.scaled(12)
                                            font.bold: true
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.scaled(40)  // Don't overlap star
                                    onClicked: {
                                        // Toggle selection - clicking again deselects
                                        if (selectedShot === modelData) {
                                            selectedShot = null
                                        } else {
                                            selectedShot = modelData
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Details panel
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.surfaceColor
                    radius: Theme.scaled(8)

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("visualizer.import.deselect", "Deselect shot")
                    Accessible.focusable: true
                    Accessible.onPressAction: detailsDeselectArea.clicked(null)

                    // Click anywhere in details panel to deselect and show legend
                    MouseArea {
                        id: detailsDeselectArea
                        anchors.fill: parent
                        onClicked: selectedShot = null
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingMedium
                        visible: selectedShot !== null

                        Text {
                            text: selectedShot ? selectedShot.profile_title : ""
                            color: Theme.textColor
                            font: Theme.headingFont
                            wrapMode: Text.Wrap
                            width: parent.width
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: Theme.spacingMedium
                            rowSpacing: Theme.spacingSmall
                            width: parent.width

                            Text { text: TranslationManager.translate("visualizerImport.status", "Status:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: {
                                    if (!selectedShot) return ""
                                    if (selectedShot.invalid) return selectedShot.invalidReason || TranslationManager.translate("visualizerImport.invalidProfile", "Invalid profile")
                                    if (selectedShot.identical) return TranslationManager.translate("visualizerImport.identicalProfile", "You have this profile already, with the same frames")
                                    if (!selectedShot.exists) return TranslationManager.translate("visualizerImport.newProfile", "New profile")
                                    if (selectedShot.source === "B") return TranslationManager.translate("visualizerImport.builtInDifferent", "Built-in with different frames (will import as copy)")
                                    return TranslationManager.translate("visualizerImport.alreadyDownloaded", "Already downloaded (different frames)")
                                }
                                color: {
                                    if (!selectedShot) return Theme.textColor
                                    if (selectedShot.invalid) return Theme.errorColor
                                    if (selectedShot.identical) return Theme.successColor
                                    if (!selectedShot.exists) return Theme.successColor
                                    return sourceColor(selectedShot.source)
                                }
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            Text { text: TranslationManager.translate("visualizerImport.author", "Author:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot ? selectedShot.user_name : ""
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: TranslationManager.translate("visualizerImport.beans", "Beans:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot ? ((selectedShot.bean_brand || "") + " " + (selectedShot.bean_type || "")).trim() : ""
                                color: Theme.textColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            Text { text: TranslationManager.translate("visualizerImport.dose", "Dose:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && selectedShot.bean_weight ? selectedShot.bean_weight + "g" : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: TranslationManager.translate("visualizerImport.yield", "Yield:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && selectedShot.drink_weight ? selectedShot.drink_weight + "g" : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: TranslationManager.translate("visualizerImport.duration", "Duration:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot ? Math.round(selectedShot.duration) + "s" : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: TranslationManager.translate("visualizerImport.grinder", "Grinder:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && (selectedShot.grinder_model || selectedShot.grinder_setting) ?
                                      (selectedShot.grinder_model || "") +
                                      (selectedShot.grinder_model && selectedShot.grinder_setting ? " @ " : "") +
                                      (selectedShot.grinder_setting || "") : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            Text { text: TranslationManager.translate("visualizerImport.shotTime", "Shot time:"); color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && selectedShot.start_time ?
                                      new Date(selectedShot.start_time).toLocaleString(Qt.locale(), Settings.use12HourTime ? "MMM d, yyyy h:mm AP" : "MMM d, yyyy HH:mm") : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }
                        }
                    }

                    // Empty state - show icon legend
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - Theme.scaled(40)
                        spacing: Theme.spacingMedium
                        visible: selectedShot === null

                        Text {
                            text: TranslationManager.translate("visualizerImport.selectProfile", "Select a profile to see details")
                            color: Theme.textSecondaryColor
                            font: Theme.bodyFont
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Rectangle {
                            width: parent.width * 0.6
                            height: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Text {
                            text: TranslationManager.translate("visualizerImport.iconLegend", "Icon Legend")
                            color: Theme.textColor
                            font.bold: true
                            font.pixelSize: Theme.bodyFont.pixelSize
                            anchors.horizontalCenter: parent.horizontalCenter
                        }

                        Column {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: Theme.spacingSmall

                            Row {
                                spacing: Theme.spacingSmall
                                Rectangle {
                                    width: Theme.scaled(24)
                                    height: Theme.scaled(24)
                                    radius: Theme.scaled(12)
                                    color: Theme.sourceBadgeBlueColor
                                    Text {
                                        anchors.centerIn: parent
                                        text: "D"
                                        color: Theme.primaryContrastColor
                                        font.pixelSize: Theme.scaled(12)
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: TranslationManager.translate("visualizerImport.legendBuiltIn", "Built-in with different frames (tap to rename & import)")
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.spacingSmall
                                Rectangle {
                                    width: Theme.scaled(24)
                                    height: Theme.scaled(24)
                                    radius: Theme.scaled(12)
                                    color: Theme.sourceBadgeGreenColor
                                    Text {
                                        anchors.centerIn: parent
                                        text: "V"
                                        color: Theme.primaryContrastColor
                                        font.pixelSize: Theme.scaled(12)
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: TranslationManager.translate("visualizerImport.legendDownloaded", "Downloaded profile (tap to update)")
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.spacingSmall
                                ColoredIcon {
                                    source: "qrc:/icons/tick.svg"
                                    iconWidth: Theme.scaled(18)
                                    iconHeight: Theme.scaled(18)
                                    iconColor: Theme.successColor
                                    width: Theme.scaled(24)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: TranslationManager.translate("visualizer.legend.alreadyimported", "Already imported (same frames)")
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.spacingSmall
                                ColoredIcon {
                                    source: "qrc:/icons/star-outline.svg"
                                    iconWidth: Theme.scaled(18)
                                    iconHeight: Theme.scaled(18)
                                    iconColor: Theme.textSecondaryColor
                                    width: Theme.scaled(24)
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: TranslationManager.translate("visualizer.legend.taptoimport", "Tap to import")
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.spacingSmall
                                Rectangle {
                                    width: Theme.scaled(24)
                                    height: Theme.scaled(24)
                                    radius: Theme.scaled(12)
                                    color: Theme.errorColor
                                    Text {
                                        anchors.centerIn: parent
                                        text: "✕"
                                        color: Theme.primaryContrastColor
                                        font.pixelSize: Theme.scaled(14)
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: TranslationManager.translate("visualizerImport.legendInvalid", "Invalid profile (cannot import)")
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        Text {
                            text: TranslationManager.translate("visualizerImport.overwriteNote", "V profiles will be overwritten when re-imported")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            anchors.horizontalCenter: parent.horizontalCenter
                            topPadding: Theme.spacingSmall
                            wrapMode: Text.Wrap
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

    }

    // Rename dialog - flat bar at top for keyboard compatibility
    Rectangle {
        id: renameBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
        height: visible ? Theme.scaled(60) : 0
        color: Theme.surfaceColor
        visible: showRenameDialog
        z: 100

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMedium
            anchors.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Text {
                text: TranslationManager.translate("visualizerImport.saveAs", "Save as:")
                color: Theme.textColor
                font: Theme.bodyFont
            }

            StyledTextField {
                id: renameInput
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)
                selectByMouse: true
                inputMethodHints: Qt.ImhNoAutoUppercase
                placeholder: TranslationManager.translate("visualizerImport.profileName", "Profile name")
                accessibleName: TranslationManager.translate("visualizerImport.profileName", "Profile name")

                Keys.onReturnPressed: {
                    if (text.trim().length > 0) {
                        Qt.inputMethod.hide()
                        renameInput.focus = false
                        MainController.visualizerImporter.importFromShotIdWithName(renameProfileId, text.trim())
                        showRenameDialog = false
                    }
                }
                Keys.onEscapePressed: {
                    Qt.inputMethod.hide()
                    showRenameDialog = false
                }
            }

            AccessibleButton {
                text: TranslationManager.translate("visualizerImport.import", "Import")
                accessibleName: TranslationManager.translate("visualizerMultiImport.importWithNewName", "Import profile with the new name")
                primary: true
                Layout.preferredWidth: Theme.scaled(80)
                Layout.preferredHeight: Theme.scaled(44)
                enabled: renameInput.text.trim().length > 0

                onClicked: {
                    Qt.inputMethod.hide()
                    renameInput.focus = false
                    MainController.visualizerImporter.importFromShotIdWithName(renameProfileId, renameInput.text.trim())
                    showRenameDialog = false
                }
            }

            AccessibleButton {
                text: TranslationManager.translate("visualizerImport.cancel", "Cancel")
                accessibleName: TranslationManager.translate("visualizerMultiImport.cancelRenaming", "Cancel renaming and close dialog")
                Layout.preferredWidth: Theme.scaled(80)
                Layout.preferredHeight: Theme.scaled(44)

                onClicked: {
                    Qt.inputMethod.hide()
                    renameInput.focus = false
                    showRenameDialog = false
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("visualizerImport.visualizer", "Visualizer")
        rightText: TranslationManager.translate("visualizerImport.tapToImport", "Tap ☆ to import")
        onBackClicked: root.goBack()
    }
}
