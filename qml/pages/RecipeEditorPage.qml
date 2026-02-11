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
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(90)

                    Text {
                        id: recipeDescLabel
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.scaled(4)
                        text: TranslationManager.translate("recipeEditor.descriptionLabel", "Description")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }

                    Rectangle {
                        anchors.top: recipeDescLabel.bottom
                        anchors.topMargin: Theme.scaled(2)
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(6)
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded

                            TextArea {
                                id: recipeNotesField
                                Accessible.name: "Profile description"
                                text: profile ? (profile.profile_notes || "") : ""
                                font.pixelSize: Theme.scaled(10)
                                color: Theme.textColor
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

                            // Dose
                            RowLayout { Layout.fillWidth: true
                                Text { text: TranslationManager.translate("recipeEditor.dose", "Dose"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Item { Layout.fillWidth: true }
                                Text { text: (recipe.dose || 18).toFixed(1) + "g"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.weightColor }
                            }
                            StepSlider { Layout.fillWidth: true; accessibleName: "Dose"; from: 3; to: 40; stepSize: 0.5; value: recipe.dose || 18; onMoved: updateRecipe("dose", Math.round(value * 10) / 10) }

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

                            // Temp
                            RowLayout { Layout.fillWidth: true
                                Text { text: TranslationManager.translate("recipeEditor.infuseTemp", "Temp"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Item { Layout.fillWidth: true }
                                Text { text: (recipe.fillTemperature || 88).toFixed(1) + "\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                            }
                            StepSlider { Layout.fillWidth: true; accessibleName: "Infuse temperature"; from: 80; to: 100; stepSize: 0.5; value: recipe.fillTemperature || 88; onMoved: updateRecipe("fillTemperature", Math.round(value * 10) / 10) }

                            // Pressure
                            RowLayout { Layout.fillWidth: true
                                Text { text: TranslationManager.translate("recipeEditor.infusePressureLabel", "Pressure"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Item { Layout.fillWidth: true }
                                Text { text: (recipe.infusePressure !== undefined ? recipe.infusePressure : 3.0).toFixed(1) + " bar"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.pressureColor }
                            }
                            StepSlider { Layout.fillWidth: true; accessibleName: "Infuse pressure"; from: 0; to: 6; stepSize: 0.1; value: recipe.infusePressure !== undefined ? recipe.infusePressure : 3.0; onMoved: updateRecipe("infusePressure", Math.round(value * 10) / 10) }

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

                                    // Time
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: TranslationManager.translate("recipeEditor.infuseTimeLabel", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                        Item { Layout.fillWidth: true }
                                        Text { text: Math.round(recipe.infuseTime || 20) + "s"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textColor }
                                    }
                                    StepSlider { Layout.fillWidth: true; accessibleName: "Infuse time"; from: 0; to: 60; stepSize: 1; value: recipe.infuseTime || 20; onMoved: updateRecipe("infuseTime", Math.round(value)) }

                                    // Volume
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: TranslationManager.translate("recipeEditor.infuseVolumeLabel", "Volume"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                        Item { Layout.fillWidth: true }
                                        Text { text: Math.round(recipe.infuseVolume || 100) + " mL"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textColor }
                                    }
                                    StepSlider { Layout.fillWidth: true; accessibleName: "Infuse volume"; from: 10; to: 200; stepSize: 10; value: recipe.infuseVolume || 100; onMoved: updateRecipe("infuseVolume", Math.round(value)) }

                                    // Weight
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: TranslationManager.translate("recipeEditor.infuseWeightLabel", "Weight"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (recipe.infuseWeight || 4.0).toFixed(1) + "g"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.weightColor }
                                    }
                                    StepSlider { Layout.fillWidth: true; accessibleName: "Infuse weight"; from: 0; to: 20; stepSize: 0.5; value: recipe.infuseWeight || 4.0; onMoved: updateRecipe("infuseWeight", Math.round(value * 10) / 10) }
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

                            // Temp
                            RowLayout { Layout.fillWidth: true
                                Text { text: TranslationManager.translate("recipeEditor.pourTemp", "Temp"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Item { Layout.fillWidth: true }
                                Text { text: (recipe.pourTemperature || 93).toFixed(1) + "\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                            }
                            StepSlider { Layout.fillWidth: true; accessibleName: "Pour temperature"; from: 80; to: 100; stepSize: 0.5; value: recipe.pourTemperature || 93; onMoved: updateRecipe("pourTemperature", Math.round(value * 10) / 10) }

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

                                    // Flow
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: TranslationManager.translate("recipeEditor.pourFlowLabel", "Flow"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (recipe.pourFlow || 2.0).toFixed(1) + " mL/s"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.flowColor }
                                    }
                                    StepSlider { Layout.fillWidth: true; accessibleName: "Pour flow"; from: 0.1; to: 8; stepSize: 0.1; value: recipe.pourFlow || 2.0; onMoved: updateRecipe("pourFlow", Math.round(value * 10) / 10) }

                                    // Pressure limit
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: TranslationManager.translate("recipeEditor.pourPressureLabel", "Pressure"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (recipe.pourPressure || 9.0).toFixed(1) + " bar"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.pressureColor }
                                    }
                                    StepSlider { Layout.fillWidth: true; accessibleName: "Pour pressure limit"; from: 1; to: 12; stepSize: 0.1; value: recipe.pourPressure || 9.0; onMoved: updateRecipe("pourPressure", Math.round(value * 10) / 10) }

                                    // Ramp time (A-Flow only — pressure ramp up duration)
                                    RowLayout { Layout.fillWidth: true; visible: recipe.editorType === "aflow"
                                        Text { text: TranslationManager.translate("recipeEditor.pourTimeLabel", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                        Item { Layout.fillWidth: true }
                                        Text { text: (recipe.rampTime || 5).toFixed(1) + "s"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textColor }
                                    }
                                    StepSlider { Layout.fillWidth: true; accessibleName: "Ramp time"; visible: recipe.editorType === "aflow"; from: 0; to: 30; stepSize: 0.5; value: recipe.rampTime || 5; onMoved: updateRecipe("rampTime", Math.round(value * 10) / 10) }
                                }
                            }

                            // Weight stop condition
                            RowLayout { Layout.fillWidth: true
                                Text { text: TranslationManager.translate("recipeEditor.pourWeightLabel", "Weight"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Item { Layout.fillWidth: true }
                                Text { text: Math.round(recipe.targetWeight || 36) + "g"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.weightColor }
                            }
                            StepSlider { Layout.fillWidth: true; accessibleName: "Target weight"; from: 0; to: 100; stepSize: 1; value: recipe.targetWeight || 36; onMoved: updateRecipe("targetWeight", Math.round(value)) }

                            // Volume stop condition (D-Flow only)
                            RowLayout { Layout.fillWidth: true; visible: recipe.editorType !== "aflow"
                                Text { text: TranslationManager.translate("recipeEditor.pourVolumeLabel", "Volume"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Item { Layout.fillWidth: true }
                                Text { text: Math.round(recipe.targetVolume || 0) + " mL"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; font.bold: true; color: Theme.textColor }
                            }
                            StepSlider { Layout.fillWidth: true; accessibleName: "Target volume"; visible: recipe.editorType !== "aflow"; from: 0; to: 200; stepSize: 5; value: recipe.targetVolume || 0; onMoved: updateRecipe("targetVolume", Math.round(value)) }
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
                AccessibilityManager.announce("Profile saved")
                root.goBack()
            } else {
                AccessibilityManager.announce("Save failed")
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
                    Accessible.name: "Profile name"
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
