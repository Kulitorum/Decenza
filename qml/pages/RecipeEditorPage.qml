import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

/**
 * RecipeEditorPage - Simplified D-Flow style profile editor
 *
 * Users edit intuitive "coffee concept" parameters like infuse pressure
 * and pour flow, and the app automatically generates DE1 frames.
 */
Page {
    id: recipeEditorPage
    objectName: "recipeEditorPage"
    background: Rectangle { color: Theme.backgroundColor }

    property var profile: null
    property var recipe: MainController.getCurrentRecipeParams()
    property bool recipeModified: MainController.profileModified
    property string originalProfileName: MainController.baseProfileName

    // Track selected frame for scroll synchronization
    property int selectedFrameIndex: -1
    property bool scrollingFromSelection: false  // Prevent feedback loop

    // Map frame index to section name based on enabled phases
    function frameToSection(frameIndex) {
        if (!profile || !profile.steps || frameIndex < 0 || frameIndex >= profile.steps.length)
            return "core"

        var frame = profile.steps[frameIndex]
        var name = (frame.name || "").toLowerCase()

        // Match frame name to section
        if (name.indexOf("fill") !== -1) return "infuse"  // Fill maps to infuse section
        if (name.indexOf("bloom") !== -1) return "infuse"
        if (name.indexOf("infuse") !== -1 || name.indexOf("preinfuse") !== -1) return "infuse"
        if (name.indexOf("ramp") !== -1 || name.indexOf("transition") !== -1) return "pour"
        if (name.indexOf("pressure up") !== -1) return "pour"
        if (name.indexOf("pressure decline") !== -1) return "pour"
        if (name.indexOf("flow start") !== -1) return "pour"
        if (name.indexOf("flow extraction") !== -1) return "pour"
        if (name.indexOf("pour") !== -1 || name.indexOf("extraction") !== -1) return "pour"
        if (name.indexOf("decline") !== -1) return "pour"

        // Fallback: use frame position heuristic
        var totalFrames = profile.steps.length
        if (frameIndex === 0) return "infuse"
        if (frameIndex >= totalFrames - 2) return "pour"

        return "infuse"  // Default middle frames to infuse
    }

    // Scroll to section when frame is selected
    function scrollToSection(sectionName) {
        var targetY = 0
        switch (sectionName) {
            case "core": targetY = coreSection.y; break
            case "infuse": targetY = infuseSection.y; break
            case "aflowToggles": targetY = aflowTogglesSection.y; break
            case "ramp": targetY = rampSection.y; break
            case "pour": targetY = pourSection.y; break
            default: return
        }

        scrollingFromSelection = true
        // Center the section in the view
        var scrollTarget = Math.max(0, targetY - recipeScrollView.height / 4)
        recipeScrollView.contentItem.contentY = scrollTarget
        scrollResetTimer.restart()
    }

    // Find which section is most centered in the scroll view
    function findCenteredSection() {
        var viewCenter = recipeScrollView.contentItem.contentY + recipeScrollView.height / 2
        var sections = [
            { name: "core", item: coreSection },
            { name: "infuse", item: infuseSection },
            { name: "aflowToggles", item: aflowTogglesSection },
            { name: "ramp", item: rampSection },
            { name: "pour", item: pourSection }
        ]

        var closest = "infuse"  // Default to infuse if nothing found
        var closestDist = 999999

        for (var i = 0; i < sections.length; i++) {
            var s = sections[i]
            // Skip invisible or disabled sections
            if (!s.item.visible || s.item.height === 0) continue

            var sectionCenter = s.item.y + s.item.height / 2
            var dist = Math.abs(viewCenter - sectionCenter)
            if (dist < closestDist) {
                closestDist = dist
                closest = s.name
            }
        }

        return closest
    }

    // Map section to first frame index
    function sectionToFrame(sectionName) {
        if (!profile || !profile.steps) return -1

        for (var i = 0; i < profile.steps.length; i++) {
            if (frameToSection(i) === sectionName) return i
        }

        return -1
    }

    Timer {
        id: scrollResetTimer
        interval: 300
        onTriggered: scrollingFromSelection = false
    }

    // Load profile data from MainController
    function loadCurrentProfile() {
        recipe = MainController.getCurrentRecipeParams()

        // Regenerate profile from recipe params to ensure frames match.
        // Preserve modified state — this is just syncing, not a user edit.
        var wasModified = MainController.profileModified
        MainController.uploadRecipeProfile(recipe)
        if (!wasModified) {
            MainController.markProfileClean()
        }

        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps && loadedProfile.steps.length > 0) {
            profile = loadedProfile
            profileGraph.frames = []
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Update recipe and upload to machine
    function updateRecipe(key, value) {
        var newRecipe = Object.assign({}, recipe)
        newRecipe[key] = value
        recipe = newRecipe

        MainController.uploadRecipeProfile(recipe)

        // Reload profile to get regenerated frames
        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps) {
            profile = loadedProfile
            profileGraph.frames = profile.steps.slice()
            profileGraph.refresh()
        }
    }

    // Editor mode header
    Rectangle {
        id: editorModeHeader
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        height: Theme.scaled(50)
        color: Theme.primaryColor
        radius: Theme.cardRadius

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.scaled(15)
            anchors.rightMargin: Theme.scaled(15)

            Text {
                text: (recipe.editorType === "aflow")
                    ? TranslationManager.translate("recipeEditor.aFlowEditorTitle", "A-Flow Editor")
                    : TranslationManager.translate("recipeEditor.dFlowEditorTitle", "D-Flow Editor")
                font.family: Theme.titleFont.family
                font.pixelSize: Theme.titleFont.pixelSize
                font.bold: true
                color: "white"
            }

            Item { Layout.fillWidth: true }
        }
    }

    // Main content area
    Item {
        anchors.top: editorModeHeader.bottom
        anchors.topMargin: Theme.scaled(10)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin

        RowLayout {
            anchors.fill: parent
            spacing: Theme.scaled(15)

            // Left side: Profile graph + Description
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(8)

                // Profile visualization
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Theme.scaled(120)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ProfileGraph {
                        id: profileGraph
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        frames: []  // Loaded via loadCurrentProfile()
                        selectedFrameIndex: recipeEditorPage.selectedFrameIndex

                        onFrameSelected: function(index) {
                            recipeEditorPage.selectedFrameIndex = index
                            var section = frameToSection(index)
                            scrollToSection(section)
                        }
                    }
                }

                // Profile description
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(70)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(6)

                        TextArea {
                            id: recipeNotesField
                            text: profile ? (profile.profile_notes || "") : ""
                            font: Theme.captionFont
                            color: Theme.textColor
                            placeholderText: TranslationManager.translate("recipeEditor.descriptionPlaceholder", "Profile description...")
                            placeholderTextColor: Theme.textSecondaryColor
                            wrapMode: TextArea.Wrap
                            leftPadding: Theme.scaled(8)
                            rightPadding: Theme.scaled(8)
                            topPadding: Theme.scaled(4)
                            bottomPadding: Theme.scaled(4)
                            background: Rectangle {
                                color: Theme.backgroundColor
                                radius: Theme.scaled(4)
                                border.color: recipeNotesField.activeFocus ? Theme.primaryColor : Theme.borderColor
                                border.width: 1
                            }
                            onEditingFinished: {
                                if (profile) {
                                    profile.profile_notes = text
                                    MainController.uploadProfile(profile)
                                }
                            }
                        }
                    }
                }
            }

            // Right side: Recipe controls
            Rectangle {
                Layout.preferredWidth: Theme.scaled(320)
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ScrollView {
                    id: recipeScrollView
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    clip: true
                    contentWidth: availableWidth

                    // Monitor scroll position to update selected frame
                    Connections {
                        target: recipeScrollView.contentItem
                        function onContentYChanged() {
                            if (!scrollingFromSelection) {
                                scrollUpdateTimer.restart()
                            }
                        }
                    }

                    Timer {
                        id: scrollUpdateTimer
                        interval: 150  // Debounce scroll events
                        onTriggered: {
                            if (!scrollingFromSelection) {
                                var section = findCenteredSection()
                                var frameIdx = sectionToFrame(section)
                                if (frameIdx >= 0 && frameIdx !== selectedFrameIndex) {
                                    selectedFrameIndex = frameIdx
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        id: sectionsColumn
                        width: parent.width
                        spacing: Theme.scaled(18)

                        // === Core Settings ===
                        RecipeSection {
                            id: coreSection
                            Layout.fillWidth: true

                            RecipeRow {
                                label: TranslationManager.translate("recipeEditor.dose", "Dose")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.dose || 18
                                    from: 3; to: 40; stepSize: 0.5
                                    suffix: "g"
                                    valueColor: Theme.weightColor
                                    accentColor: Theme.weightColor
                                    accessibleName: TranslationManager.translate("recipeEditor.doseWeight", "Dose weight")
                                    onValueModified: function(newValue) {
                                        updateRecipe("dose", newValue)
                                    }
                                }
                            }

                            // Display ratio (weight is set in Pour section)
                            Text {
                                Layout.fillWidth: true
                                text: TranslationManager.translate("recipeEditor.ratio", "Ratio: 1:") + ((recipe.targetWeight || 36) / (recipe.dose || 18)).toFixed(1)
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        // === Infuse Phase ===
                        RecipeSection {
                            id: infuseSection
                            title: TranslationManager.translate("recipeEditor.infuseTitle", "Infuse")
                            Layout.fillWidth: true
                            canEnable: recipe.editorType === "aflow"  // D-Flow always has infuse
                            sectionEnabled: recipe.infuseEnabled !== false  // Default true
                            onSectionToggled: function(enabled) { updateRecipe("infuseEnabled", enabled) }

                            RecipeRow {
                                label: TranslationManager.translate("recipeEditor.infuseTemp", "Temp")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.fillTemperature || 88
                                    from: 80; to: 100; stepSize: 0.5
                                    suffix: "\u00B0C"
                                    valueColor: Theme.temperatureColor
                                    accentColor: Theme.temperatureGoalColor
                                    accessibleName: TranslationManager.translate("recipeEditor.infuseTemperature", "Infuse temperature")
                                    onValueModified: function(newValue) {
                                        updateRecipe("fillTemperature", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: TranslationManager.translate("recipeEditor.infusePressureLabel", "Pressure")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.infusePressure !== undefined ? recipe.infusePressure : 3.0
                                    from: 0; to: 6; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: Theme.pressureColor
                                    accentColor: Theme.pressureGoalColor
                                    accessibleName: TranslationManager.translate("recipeEditor.infusePressure", "Infuse pressure")
                                    onValueModified: function(newValue) {
                                        updateRecipe("infusePressure", newValue)
                                    }
                                }
                            }

                            // Grouped: move to next step on first reached
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: infuseExitGroup.implicitHeight

                                // Left accent bar
                                Rectangle {
                                    id: infuseAccent
                                    width: Theme.scaled(3)
                                    height: parent.height
                                    radius: Theme.scaled(1.5)
                                    color: Theme.textSecondaryColor
                                    opacity: 0.4
                                }

                                ColumnLayout {
                                    id: infuseExitGroup
                                    anchors.left: infuseAccent.right
                                    anchors.leftMargin: Theme.scaled(8)
                                    anchors.right: parent.right
                                    spacing: Theme.scaled(8)

                                    Text {
                                        text: TranslationManager.translate("recipeEditor.infuseExitLabel", "Move to next step on first reached")
                                        font.family: Theme.captionFont.family
                                        font.pixelSize: Theme.captionFont.pixelSize
                                        font.italic: true
                                        color: Theme.textSecondaryColor
                                        opacity: 0.8
                                    }

                                    RecipeRow {
                                        label: TranslationManager.translate("recipeEditor.infuseTimeLabel", "Time")
                                        ValueInput {
                                            Layout.fillWidth: true
                                            value: recipe.infuseTime || 20
                                            from: 0; to: 60; stepSize: 1
                                            suffix: "s"
                                            decimals: 0
                                            accessibleName: TranslationManager.translate("recipeEditor.infuseTime", "Infuse time")
                                            onValueModified: function(newValue) {
                                                updateRecipe("infuseTime", newValue)
                                            }
                                        }
                                    }

                                    RecipeRow {
                                        label: TranslationManager.translate("recipeEditor.infuseVolumeLabel", "Volume")
                                        ValueInput {
                                            Layout.fillWidth: true
                                            value: recipe.infuseVolume || 100
                                            from: 10; to: 200; stepSize: 10
                                            suffix: " mL"
                                            decimals: 0
                                            accessibleName: TranslationManager.translate("recipeEditor.infuseVolume", "Infuse volume")
                                            onValueModified: function(newValue) {
                                                updateRecipe("infuseVolume", newValue)
                                            }
                                        }
                                    }

                                    RecipeRow {
                                        label: TranslationManager.translate("recipeEditor.infuseWeightLabel", "Weight")
                                        ValueInput {
                                            Layout.fillWidth: true
                                            value: recipe.infuseWeight || 4.0
                                            from: 0; to: 20; stepSize: 0.5
                                            suffix: "g"
                                            valueColor: Theme.weightColor
                                            accentColor: Theme.weightColor
                                            accessibleName: TranslationManager.translate("recipeEditor.infuseWeight", "Infuse weight")
                                            onValueModified: function(newValue) {
                                                updateRecipe("infuseWeight", newValue)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Ramp section anchor (for scroll sync compatibility)
                        Item {
                            id: rampSection
                            visible: false
                            Layout.fillWidth: true
                            implicitHeight: 0
                        }

                        // A-Flow toggles section anchor (for scroll sync compatibility)
                        Item {
                            id: aflowTogglesSection
                            visible: false
                            Layout.fillWidth: true
                            implicitHeight: 0
                        }

                        // === Pour Phase ===
                        RecipeSection {
                            id: pourSection
                            title: TranslationManager.translate("recipeEditor.pourTitle", "Pour")
                            Layout.fillWidth: true

                            RecipeRow {
                                label: TranslationManager.translate("recipeEditor.pourTemp", "Temp")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.pourTemperature || 93
                                    from: 80; to: 100; stepSize: 0.5
                                    suffix: "\u00B0C"
                                    valueColor: Theme.temperatureColor
                                    accentColor: Theme.temperatureGoalColor
                                    accessibleName: TranslationManager.translate("recipeEditor.pourTemperature", "Pour temperature")
                                    onValueModified: function(newValue) {
                                        updateRecipe("pourTemperature", newValue)
                                    }
                                }
                            }

                            // Grouped: flow, pressure, and time (ramp time for A-Flow)
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: pourExtractionGroup.implicitHeight

                                Rectangle {
                                    id: pourExtractionAccent
                                    width: Theme.scaled(3)
                                    height: parent.height
                                    radius: Theme.scaled(1.5)
                                    color: Theme.flowColor
                                    opacity: 0.5
                                }

                                ColumnLayout {
                                    id: pourExtractionGroup
                                    anchors.left: pourExtractionAccent.right
                                    anchors.leftMargin: Theme.scaled(8)
                                    anchors.right: parent.right
                                    spacing: Theme.scaled(8)

                                    Text {
                                        text: TranslationManager.translate("recipeEditor.pourExtractionLabel", "Flow control with pressure limit")
                                        font.family: Theme.captionFont.family
                                        font.pixelSize: Theme.captionFont.pixelSize
                                        font.italic: true
                                        color: Theme.textSecondaryColor
                                        opacity: 0.8
                                    }

                                    RecipeRow {
                                        label: TranslationManager.translate("recipeEditor.pourFlowLabel", "Flow")
                                        ValueInput {
                                            Layout.fillWidth: true
                                            value: recipe.pourFlow || 2.0
                                            from: 0.1; to: 8; stepSize: 0.1
                                            suffix: " mL/s"
                                            valueColor: Theme.flowColor
                                            accentColor: Theme.flowGoalColor
                                            accessibleName: TranslationManager.translate("recipeEditor.pourFlow", "Pour flow")
                                            onValueModified: function(newValue) {
                                                updateRecipe("pourFlow", newValue)
                                            }
                                        }
                                    }

                                    RecipeRow {
                                        label: TranslationManager.translate("recipeEditor.pourPressureLabel", "Pressure")
                                        ValueInput {
                                            Layout.fillWidth: true
                                            value: recipe.pourPressure || 9.0
                                            from: 1; to: 12; stepSize: 0.1
                                            suffix: " bar"
                                            valueColor: Theme.pressureColor
                                            accentColor: Theme.pressureGoalColor
                                            accessibleName: TranslationManager.translate("recipeEditor.pourPressure", "Pour pressure limit")
                                            onValueModified: function(newValue) {
                                                updateRecipe("pourPressure", newValue)
                                            }
                                        }
                                    }

                                    // Ramp time (A-Flow only — pressure ramp up duration)
                                    RecipeRow {
                                        visible: recipe.editorType === "aflow"
                                        label: TranslationManager.translate("recipeEditor.pourTimeLabel", "Time")
                                        ValueInput {
                                            Layout.fillWidth: true
                                            value: recipe.rampTime || 5
                                            from: 0; to: 30; stepSize: 0.5
                                            suffix: "s"
                                            accessibleName: TranslationManager.translate("recipeEditor.rampTime", "Ramp time")
                                            onValueModified: function(newValue) {
                                                updateRecipe("rampTime", newValue)
                                            }
                                        }
                                    }
                                }
                            }

                            // Weight stop condition
                            RecipeRow {
                                label: TranslationManager.translate("recipeEditor.pourWeightLabel", "Weight")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.targetWeight || 36
                                    from: 0; to: 100; stepSize: 1
                                    suffix: "g"
                                    valueColor: Theme.weightColor
                                    accentColor: Theme.weightColor
                                    accessibleName: TranslationManager.translate("recipeEditor.pourWeight", "Stop at weight")
                                    onValueModified: function(newValue) {
                                        updateRecipe("targetWeight", newValue)
                                    }
                                }
                            }

                            // Volume stop condition (D-Flow only)
                            RecipeRow {
                                visible: recipe.editorType !== "aflow"
                                label: TranslationManager.translate("recipeEditor.pourVolumeLabel", "Volume")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.targetVolume || 0
                                    from: 0; to: 200; stepSize: 5
                                    suffix: " mL"
                                    decimals: 0
                                    accessibleName: TranslationManager.translate("recipeEditor.pourVolume", "Stop at volume")
                                    onValueModified: function(newValue) {
                                        updateRecipe("targetVolume", newValue)
                                    }
                                }
                            }
                        }

                        // Spacer
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: MainController.currentProfileName || TranslationManager.translate("recipeEditor.recipe", "Recipe")
        onBackClicked: {
            if (recipeModified) {
                exitDialog.open()
            } else {
                root.goBack()
            }
        }

        // Modified indicator
        Text {
            text: "\u2022 Modified"
            color: "#FFCC00"
            font: Theme.bodyFont
            visible: recipeModified
        }

        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

        Text {
            text: MainController.frameCount() + " " + TranslationManager.translate("recipeEditor.frames", "frames")
            color: "white"
            font: Theme.bodyFont
        }

        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

        Text {
            text: (recipe.targetWeight || 36).toFixed(0) + "g"
            color: "white"
            font: Theme.bodyFont
        }

        AccessibleButton {
            text: TranslationManager.translate("recipeEditor.done", "Done")
            accessibleName: TranslationManager.translate("recipeEditor.finishEditing", "Finish editing recipe")
            onClicked: {
                if (recipeModified) {
                    exitDialog.open()
                } else {
                    root.goBack()
                }
            }
            // White button with primary text for bottom bar
            background: Rectangle {
                implicitWidth: Math.max(Theme.scaled(80), recipeDoneText.implicitWidth + Theme.scaled(32))
                implicitHeight: Theme.scaled(36)
                radius: Theme.scaled(6)
                color: parent.down ? Qt.darker("white", 1.1) : "white"
            }
            contentItem: Text {
                id: recipeDoneText
                text: parent.text
                font.pixelSize: Theme.scaled(14)
                font.family: Theme.bodyFont.family
                color: Theme.primaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Save error dialog
    Dialog {
        id: saveErrorDialog
        title: TranslationManager.translate("recipeEditor.saveError", "Save Failed")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(350)
        modal: true
        standardButtons: Dialog.Ok

        contentItem: Text {
            width: saveErrorDialog.availableWidth
            text: TranslationManager.translate("recipeEditor.saveErrorMessage", "Could not save the profile. Please try again or use Save As with a different name.")
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
        }
    }

    // Exit dialog for unsaved changes
    UnsavedChangesDialog {
        id: exitDialog
        itemType: "recipe"
        canSave: originalProfileName !== ""
        onDiscardClicked: {
            if (originalProfileName) {
                MainController.loadProfile(originalProfileName)
            }
            root.goBack()
        }
        onSaveAsClicked: saveAsDialog.open()
        onSaveClicked: {
            if (MainController.saveProfile(originalProfileName)) {
                root.goBack()
            } else {
                saveErrorDialog.open()
            }
        }
    }

    // Helper: get the prefix for the current editor type
    function editorPrefix() {
        return (recipe.editorType === "aflow") ? "A-Flow / " : "D-Flow / "
    }

    // Helper: strip known prefix from a title
    function stripPrefix(title) {
        if (title.indexOf("D-Flow / ") === 0) return title.substring(9)
        if (title.indexOf("A-Flow / ") === 0) return title.substring(9)
        if (title.indexOf("D-Flow /") === 0) return title.substring(8).trim()
        if (title.indexOf("A-Flow /") === 0) return title.substring(8).trim()
        return title
    }

    // Save As dialog
    Dialog {
        id: saveAsDialog
        title: TranslationManager.translate("recipeEditor.saveRecipeAs", "Save Recipe As")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel

        property string pendingFilename: ""

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(10)

            Text {
                text: TranslationManager.translate("recipeEditor.recipeTitle", "Recipe Title")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                // Fixed prefix label
                Text {
                    text: editorPrefix()
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    verticalAlignment: Text.AlignVCenter
                }

                TextField {
                    id: saveAsTitleField
                    Layout.fillWidth: true
                    text: "New Recipe"
                    font: Theme.bodyFont
                    color: Theme.textColor
                    placeholderText: TranslationManager.translate("recipeEditor.namePlaceholder", "Enter recipe name")
                    placeholderTextColor: Theme.textSecondaryColor
                    leftPadding: Theme.scaled(12)
                    rightPadding: Theme.scaled(12)
                    topPadding: Theme.scaled(12)
                    bottomPadding: Theme.scaled(12)
                    background: Rectangle {
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: saveAsTitleField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1
                    }
                    onAccepted: saveAsDialog.accept()
                }
            }
        }

        onAccepted: {
            if (saveAsTitleField.text.length > 0) {
                var fullTitle = editorPrefix() + saveAsTitleField.text
                var filename = MainController.titleToFilename(fullTitle)
                if (MainController.profileExists(filename) && filename !== originalProfileName) {
                    saveAsDialog.pendingFilename = filename
                    overwriteDialog.open()
                } else {
                    if (MainController.saveProfileAs(filename, fullTitle)) {
                        root.goBack()
                    } else {
                        saveErrorDialog.open()
                    }
                }
            }
        }

        onOpened: {
            // Strip prefix from current name to show only the suffix
            var currentName = MainController.currentProfileName || "New Recipe"
            saveAsTitleField.text = stripPrefix(currentName)
            saveAsTitleField.forceActiveFocus()
        }
    }

    // Overwrite confirmation dialog
    Dialog {
        id: overwriteDialog
        title: TranslationManager.translate("recipeEditor.profileExists", "Profile Exists")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No

        contentItem: Text {
            width: overwriteDialog.availableWidth
            text: TranslationManager.translate("recipeEditor.overwriteConfirm", "A profile with this name already exists.\nDo you want to overwrite it?")
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
        }

        onAccepted: {
            var fullTitle = editorPrefix() + saveAsTitleField.text
            if (MainController.saveProfileAs(saveAsDialog.pendingFilename, fullTitle)) {
                root.goBack()
            } else {
                saveErrorDialog.open()
            }
        }
    }

    // Timer for delayed graph refresh after page loads
    Timer {
        id: delayedRefreshTimer
        interval: 200
        onTriggered: {
            if (profile && profile.steps) {
                profileGraph.frames = profile.steps.slice()
                profileGraph.refresh()
            }
        }
    }

    // Load recipe when page is actually navigated to (not just instantiated)
    Component.onCompleted: {
        // Don't create recipe here - wait for StackView.onActivated
        // Component.onCompleted fires during instantiation which may happen at app startup
    }

    StackView.onActivated: {
        // Capture the original profile name BEFORE conversion (createNewRecipe clears baseProfileName)
        originalProfileName = MainController.baseProfileName || ""

        // If not already in recipe mode, create a new recipe from current profile settings
        if (!MainController.isCurrentProfileRecipe) {
            MainController.createNewRecipe(MainController.currentProfileName || "New Recipe")
        }
        loadCurrentProfile()
        root.currentPageTitle = MainController.currentProfileName || TranslationManager.translate("recipeEditor.title", "Recipe Editor")
        // Schedule a delayed refresh to ensure chart is ready
        delayedRefreshTimer.start()
    }
}
