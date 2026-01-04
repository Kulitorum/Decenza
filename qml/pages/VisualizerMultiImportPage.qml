import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: multiImportPage
    objectName: "visualizerMultiImportPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = "Import Shared Profiles"
        // Auto-fetch shared profiles on page load
        MainController.visualizerImporter.fetchSharedShots()
    }
    StackView.onActivated: root.currentPageTitle = "Import Shared Profiles"

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
        renameProfileTitle = shot.profile_title + " (copy)"
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
        if (source === "B") return "#4a90d9"  // Blue for Built-in/Decent
        if (source === "D") return "#4ad94a"  // Green for Downloaded/Visualizer
        return "#d9a04a"  // Orange for User
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
            resultText.text = "Imported: " + profileTitle
            resultText.color = Theme.successColor
            resultText.visible = true
            resultTimer.restart()
        }

        function onImportFailed(error) {
            resultText.text = "Error: " + error
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
                    text: "Import Multiple Profiles from Visualizer"
                    color: Theme.textColor
                    font: Theme.headingFont
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    text: "This feature lets you import profiles from shots you've shared on visualizer.coffee in the last hour."
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
                    text: "How to use:"
                    color: Theme.textColor
                    font.bold: true
                    font.pixelSize: Theme.bodyFont.pixelSize
                }

                Text {
                    text: "1. Go to visualizer.coffee and find shots with profiles you want"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Text {
                    text: "2. Click the 'Share' button on each shot (creates a temporary share link)"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Text {
                    text: "3. Come back here and tap 'Fetch Shared Profiles'"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                    width: parent.width
                }

                Text {
                    text: "4. Select which profiles to import and tap 'Import Selected'"
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
                text: MainController.visualizerImporter.fetching ? "Loading profiles..." : ""
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                visible: MainController.visualizerImporter.fetching
            }

            Item { Layout.fillWidth: true }

            // Refresh button
            Button {
                id: refreshButton
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
                    border.width: Theme.scaled(1)
                }

                contentItem: Text {
                    text: "Refresh"
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

                Button {
                    id: addByCodeButton
                    width: Theme.scaled(120)
                    height: Theme.scaled(40)
                    visible: !showCodeInput

                    onClicked: {
                        showCodeInput = true
                        codeInput.text = ""
                        codeInput.forceActiveFocus()
                    }

                    background: Rectangle {
                        radius: Theme.scaled(6)
                        color: Theme.primaryColor
                    }

                    contentItem: Text {
                        text: "Add by Code"
                        color: "white"
                        font: Theme.captionFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Code input field (shown when Add by Code is clicked)
                Row {
                    spacing: Theme.spacingSmall
                    visible: showCodeInput

                    TextField {
                        id: codeInput
                        width: Theme.scaled(80)
                        height: Theme.scaled(40)
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        horizontalAlignment: Text.AlignHCenter
                        maximumLength: 4
                        placeholderText: "CODE"
                        placeholderTextColor: Theme.textSecondaryColor
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

                        background: Rectangle {
                            color: Theme.surfaceColor
                            border.color: codeInput.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: Theme.scaled(1)
                            radius: Theme.scaled(4)
                        }

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

                    Button {
                        width: Theme.scaled(60)
                        height: Theme.scaled(40)
                        enabled: codeInput.text.length === 4

                        onClicked: {
                            MainController.visualizerImporter.importFromShareCode(codeInput.text)
                            showCodeInput = false
                        }

                        background: Rectangle {
                            radius: Theme.scaled(4)
                            color: parent.enabled ? Theme.primaryColor : Theme.surfaceColor
                        }

                        contentItem: Text {
                            text: "Add"
                            color: parent.enabled ? "white" : Theme.textSecondaryColor
                            font: Theme.captionFont
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Button {
                        width: Theme.scaled(60)
                        height: Theme.scaled(40)

                        onClicked: showCodeInput = false

                        background: Rectangle {
                            radius: Theme.scaled(4)
                            color: Theme.surfaceColor
                        }

                        contentItem: Text {
                            text: "Cancel"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
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
                                text: "Shared Profiles (" + MainController.visualizerImporter.sharedShots.length + ")"
                                color: Theme.textColor
                                font: Theme.bodyFont
                                Layout.fillWidth: true
                            }

                            Text {
                                text: "Tap ☆ to import"
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
                                height: Theme.scaled(60)
                                color: selectedShot === modelData ? Theme.primaryColor.darker(1.5) :
                                       (index % 2 === 0 ? "transparent" : Theme.backgroundColor)

                                // Include selectionVersion to force re-evaluation when imports change
                                property bool isImported: selectionVersion >= 0 && importedIds[modelData.id] === true

                                RowLayout {
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
                                                color: "white"
                                                font.pixelSize: Theme.scaled(14)
                                                font.bold: true
                                            }
                                        }

                                        // Checkmark for identical or already imported
                                        Text {
                                            anchors.centerIn: parent
                                            text: "\u2713"
                                            color: Theme.successColor
                                            font.pixelSize: Theme.scaled(20)
                                            font.bold: true
                                            visible: !parent.isInvalid && !parent.canImport
                                        }

                                        // Star for importable profiles
                                        Text {
                                            anchors.centerIn: parent
                                            text: "\u2606"
                                            color: Theme.textSecondaryColor
                                            font.pixelSize: Theme.scaled(24)
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
                                            text: modelData.profile_title || "Unknown Profile"
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
                                            color: "white"
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

                    // Click anywhere in details panel to deselect and show legend
                    MouseArea {
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

                            Text { text: "Status:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: {
                                    if (!selectedShot) return ""
                                    if (selectedShot.invalid) return selectedShot.invalidReason || "Invalid profile"
                                    if (selectedShot.identical) return "You have this profile already, with the same frames"
                                    if (!selectedShot.exists) return "New profile"
                                    if (selectedShot.source === "B") return "Built-in with different frames (will import as copy)"
                                    return "Already downloaded (different frames)"
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

                            Text { text: "Author:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot ? selectedShot.user_name : ""
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: "Beans:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot ? ((selectedShot.bean_brand || "") + " " + (selectedShot.bean_type || "")).trim() : ""
                                color: Theme.textColor
                                font: Theme.captionFont
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            Text { text: "Dose:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && selectedShot.bean_weight ? selectedShot.bean_weight + "g" : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: "Yield:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && selectedShot.drink_weight ? selectedShot.drink_weight + "g" : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: "Duration:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot ? Math.round(selectedShot.duration) + "s" : "-"
                                color: Theme.textColor
                                font: Theme.captionFont
                            }

                            Text { text: "Grinder:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
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

                            Text { text: "Shot time:"; color: Theme.textSecondaryColor; font: Theme.captionFont }
                            Text {
                                text: selectedShot && selectedShot.start_time ?
                                      new Date(selectedShot.start_time).toLocaleString(Qt.locale(), "MMM d, yyyy h:mm AP") : "-"
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
                            text: "Select a profile to see details"
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
                            text: "Icon Legend"
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
                                    color: "#4a90d9"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "D"
                                        color: "white"
                                        font.pixelSize: Theme.scaled(12)
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: "Built-in with different frames (tap to rename & import)"
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
                                    color: "#4ad94a"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "V"
                                        color: "white"
                                        font.pixelSize: Theme.scaled(12)
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: "Downloaded profile (tap to update)"
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.spacingSmall
                                Text {
                                    text: "\u2713"
                                    color: Theme.successColor
                                    font.pixelSize: Theme.scaled(20)
                                    font.bold: true
                                    width: Theme.scaled(24)
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Text {
                                    text: "Already imported (same frames)"
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            Row {
                                spacing: Theme.spacingSmall
                                Text {
                                    text: "\u2606"
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.scaled(24)
                                    width: Theme.scaled(24)
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Text {
                                    text: "Tap to import"
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
                                        color: "white"
                                        font.pixelSize: Theme.scaled(14)
                                        font.bold: true
                                    }
                                }
                                Text {
                                    text: "Invalid profile (cannot import)"
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        Text {
                            text: "V profiles will be overwritten when re-imported"
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
                text: "Save as:"
                color: Theme.textColor
                font: Theme.bodyFont
            }

            TextField {
                id: renameInput
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)
                color: Theme.textColor
                font: Theme.bodyFont
                selectByMouse: true
                inputMethodHints: Qt.ImhNoAutoUppercase

                background: Rectangle {
                    color: Theme.backgroundColor
                    border.color: renameInput.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                    border.width: Theme.scaled(2)
                    radius: Theme.scaled(4)
                }

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

            Button {
                Layout.preferredWidth: Theme.scaled(80)
                Layout.preferredHeight: Theme.scaled(44)
                enabled: renameInput.text.trim().length > 0

                onClicked: {
                    Qt.inputMethod.hide()
                    renameInput.focus = false
                    MainController.visualizerImporter.importFromShotIdWithName(renameProfileId, renameInput.text.trim())
                    showRenameDialog = false
                }

                background: Rectangle {
                    radius: Theme.scaled(6)
                    color: parent.enabled ? Theme.primaryColor : Theme.surfaceColor
                }

                contentItem: Text {
                    text: "Import"
                    color: parent.enabled ? "white" : Theme.textSecondaryColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                Layout.preferredWidth: Theme.scaled(80)
                Layout.preferredHeight: Theme.scaled(44)

                onClicked: {
                    Qt.inputMethod.hide()
                    renameInput.focus = false
                    showRenameDialog = false
                }

                background: Rectangle {
                    radius: Theme.scaled(6)
                    color: Theme.backgroundColor
                    border.color: Theme.textSecondaryColor
                    border.width: Theme.scaled(1)
                }

                contentItem: Text {
                    text: "Cancel"
                    color: Theme.textColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        title: "Visualizer"
        rightText: "Tap ☆ to import"
        onBackClicked: root.goBack()
    }
}
