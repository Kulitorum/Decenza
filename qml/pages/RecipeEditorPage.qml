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
    property bool recipeModified: false
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
        if (name.indexOf("fill") !== -1) return "fill"
        if (name.indexOf("bloom") !== -1) return "bloom"
        if (name.indexOf("infuse") !== -1 || name.indexOf("preinfuse") !== -1) return "infuse"
        if (name.indexOf("ramp") !== -1 || name.indexOf("transition") !== -1) return "ramp"
        if (name.indexOf("pour") !== -1 || name.indexOf("extraction") !== -1) return "pour"
        if (name.indexOf("decline") !== -1 || name.indexOf("pressure decline") !== -1) return "decline"

        // Fallback: use frame position heuristic
        var totalFrames = profile.steps.length
        if (frameIndex === 0) return "fill"
        if (frameIndex === totalFrames - 1 && recipe.declineEnabled) return "decline"
        if (frameIndex >= totalFrames - 2) return "pour"

        return "infuse"  // Default middle frames to infuse
    }

    // Scroll to section when frame is selected
    function scrollToSection(sectionName) {
        var targetY = 0
        switch (sectionName) {
            case "core": targetY = coreSection.y; break
            case "fill": targetY = fillSection.y; break
            case "bloom": targetY = bloomSection.y; break
            case "infuse": targetY = infuseSection.y; break
            case "ramp": targetY = rampSection.y; break
            case "pour": targetY = pourSection.y; break
            case "decline": targetY = declineSection.y; break
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
            { name: "fill", item: fillSection },
            { name: "bloom", item: bloomSection },
            { name: "infuse", item: infuseSection },
            { name: "ramp", item: rampSection },
            { name: "pour", item: pourSection },
            { name: "decline", item: declineSection }
        ]

        var closest = "fill"  // Default to fill if nothing found
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

        // Always regenerate profile from recipe params to ensure frames match
        MainController.uploadRecipeProfile(recipe)

        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps && loadedProfile.steps.length > 0) {
            profile = loadedProfile
            profileGraph.frames = []
            profileGraph.frames = profile.steps.slice()
        }

        originalProfileName = MainController.baseProfileName || ""
        recipeModified = false
    }

    // Update recipe and upload to machine
    function updateRecipe(key, value) {
        var newRecipe = Object.assign({}, recipe)
        newRecipe[key] = value
        recipe = newRecipe
        recipeModified = true

        MainController.uploadRecipeProfile(recipe)

        // Reload profile to get regenerated frames
        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps) {
            profile = loadedProfile
            profileGraph.frames = profile.steps.slice()
            profileGraph.refresh()
        }
    }

    // Apply a preset
    function applyPreset(name) {
        MainController.applyRecipePreset(name)
        recipe = MainController.getCurrentRecipeParams()
        recipeModified = true
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
                text: qsTr("D-Flow Editor")
                font.family: Theme.titleFont.family
                font.pixelSize: Theme.titleFont.pixelSize
                font.bold: true
                color: "white"
            }

            Text {
                text: qsTr("Simplified profile editing with Fill → Infuse → Pour phases")
                font: Theme.captionFont
                color: Qt.rgba(1, 1, 1, 0.8)
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Switch to Advanced Editor")
                onClicked: switchToAdvancedDialog.open()
                background: Rectangle {
                    implicitWidth: Theme.scaled(180)
                    implicitHeight: Theme.scaled(32)
                    radius: Theme.scaled(6)
                    color: Qt.rgba(1, 1, 1, 0.2)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.5)
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.captionFont
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Switch to Advanced Editor confirmation dialog
    Dialog {
        id: switchToAdvancedDialog
        anchors.centerIn: parent
        width: Theme.scaled(450)
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: "white"
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Switch to Advanced Editor")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(12)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("This will convert the profile to Advanced mode with full frame-by-frame control.")
                    font: Theme.bodyFont
                    color: Theme.textColor
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("You will gain access to advanced settings like custom exit conditions, per-frame limiters, popup messages, and arbitrary frame structures.")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Note: Once converted, this profile can no longer be edited in the D-Flow editor.")
                    font: Theme.captionFont
                    color: Theme.warningColor
                    wrapMode: Text.WordWrap
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                spacing: Theme.scaled(10)

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: qsTr("Cancel")
                    accessibleName: qsTr("Cancel and stay in D-Flow Editor")
                    onClicked: switchToAdvancedDialog.close()
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: Theme.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(50)
                    text: qsTr("Convert")
                    accessibleName: qsTr("Convert to Advanced format")
                    onClicked: {
                        switchToAdvancedDialog.close()
                        MainController.convertCurrentProfileToAdvanced()
                        root.switchToAdvancedEditor()
                    }
                    background: Rectangle {
                        radius: Theme.buttonRadius
                        color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.bodyFont
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
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

            // Left side: Profile graph + Presets
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.scaled(12)

                // Profile visualization
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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

                // Presets row
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(60)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Text {
                            text: qsTr("Presets:")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }

                        PresetButton {
                            text: qsTr("Classic")
                            onClicked: applyPreset("classic")
                        }

                        PresetButton {
                            text: qsTr("Londinium")
                            onClicked: applyPreset("londinium")
                        }

                        PresetButton {
                            text: qsTr("Turbo")
                            onClicked: applyPreset("turbo")
                        }

                        PresetButton {
                            text: qsTr("Blooming")
                            onClicked: applyPreset("blooming")
                        }

                        PresetButton {
                            text: qsTr("D-Flow")
                            onClicked: applyPreset("dflowDefault")
                        }

                        Item { Layout.fillWidth: true }
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
                                label: qsTr("Dose")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.dose || 18
                                    from: 10; to: 30; stepSize: 0.5
                                    suffix: "g"
                                    valueColor: Theme.weightColor
                                    accentColor: Theme.weightColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("dose", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Stop at")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.targetWeight || 36
                                    from: 0; to: 100; stepSize: 1
                                    suffix: "g"
                                    valueColor: Theme.weightColor
                                    accentColor: Theme.weightColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("targetWeight", newValue)
                                    }
                                }
                            }

                            // Display ratio
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Ratio: 1:") + ((recipe.targetWeight || 36) / (recipe.dose || 18)).toFixed(1)
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        // === Fill Phase ===
                        RecipeSection {
                            id: fillSection
                            title: qsTr("Fill")
                            Layout.fillWidth: true

                            RecipeRow {
                                label: qsTr("Temp")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.fillTemperature || 88
                                    from: 80; to: 100; stepSize: 0.5
                                    suffix: "\u00B0C"
                                    valueColor: Theme.temperatureColor
                                    accentColor: Theme.temperatureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("fillTemperature", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Pressure")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.fillPressure || 3.0
                                    from: 1; to: 6; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: Theme.pressureColor
                                    accentColor: Theme.pressureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("fillPressure", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Flow")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.fillFlow || 8.0
                                    from: 2; to: 10; stepSize: 0.5
                                    suffix: " mL/s"
                                    valueColor: Theme.flowColor
                                    accentColor: Theme.flowGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("fillFlow", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Exit at")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.fillExitPressure || 3.0
                                    from: 0.5; to: 6; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: Theme.pressureColor
                                    accentColor: Theme.pressureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("fillExitPressure", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Timeout")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.fillTimeout || 25
                                    from: 5; to: 60; stepSize: 1
                                    suffix: "s"
                                    decimals: 0
                                    onValueModified: function(newValue) {
                                        updateRecipe("fillTimeout", newValue)
                                    }
                                }
                            }
                        }

                        // === Bloom Phase (Optional) ===
                        RecipeSection {
                            id: bloomSection
                            title: qsTr("Bloom")
                            Layout.fillWidth: true
                            canEnable: true
                            sectionEnabled: recipe.bloomEnabled || false
                            onSectionToggled: function(enabled) { updateRecipe("bloomEnabled", enabled) }

                            RecipeRow {
                                label: qsTr("Time")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.bloomTime || 10
                                    from: 1; to: 30; stepSize: 1
                                    suffix: "s"
                                    decimals: 0
                                    onValueModified: function(newValue) {
                                        updateRecipe("bloomTime", newValue)
                                    }
                                }
                            }

                            Text {
                                text: qsTr("Zero-flow pause for CO2 release")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // === Infuse Phase ===
                        RecipeSection {
                            id: infuseSection
                            title: qsTr("Infuse")
                            Layout.fillWidth: true
                            canEnable: true
                            sectionEnabled: recipe.infuseEnabled !== false  // Default true
                            onSectionToggled: function(enabled) { updateRecipe("infuseEnabled", enabled) }

                            RecipeRow {
                                label: qsTr("Pressure")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.infusePressure !== undefined ? recipe.infusePressure : 3.0
                                    from: 0; to: 6; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: Theme.pressureColor
                                    accentColor: Theme.pressureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("infusePressure", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Time")
                                visible: !recipe.infuseByWeight
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.infuseTime || 20
                                    from: 0; to: 60; stepSize: 1
                                    suffix: "s"
                                    decimals: 0
                                    onValueModified: function(newValue) {
                                        updateRecipe("infuseTime", newValue)
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)

                                CheckBox {
                                    id: infuseByWeightCheck
                                    text: qsTr("By weight")
                                    checked: recipe.infuseByWeight || false
                                    onToggled: updateRecipe("infuseByWeight", checked)
                                    contentItem: Text {
                                        text: parent.text
                                        font: Theme.captionFont
                                        color: Theme.textColor
                                        leftPadding: parent.indicator.width + parent.spacing
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                ValueInput {
                                    visible: infuseByWeightCheck.checked
                                    Layout.fillWidth: true
                                    value: recipe.infuseWeight || 4.0
                                    from: 0; to: 20; stepSize: 0.5
                                    suffix: "g"
                                    valueColor: Theme.weightColor
                                    accentColor: Theme.weightColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("infuseWeight", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Volume")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.infuseVolume || 100
                                    from: 10; to: 200; stepSize: 10
                                    suffix: " mL"
                                    decimals: 0
                                    onValueModified: function(newValue) {
                                        updateRecipe("infuseVolume", newValue)
                                    }
                                }
                            }
                        }

                        // === Ramp Phase ===
                        RecipeSection {
                            id: rampSection
                            title: qsTr("Ramp")
                            Layout.fillWidth: true
                            canEnable: true
                            sectionEnabled: recipe.rampEnabled !== false  // Default true
                            onSectionToggled: function(enabled) { updateRecipe("rampEnabled", enabled) }

                            RecipeRow {
                                label: qsTr("Time")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.rampTime || 5
                                    from: 0.5; to: 15; stepSize: 0.5
                                    suffix: "s"
                                    onValueModified: function(newValue) {
                                        updateRecipe("rampTime", newValue)
                                    }
                                }
                            }

                            Text {
                                text: qsTr("Smooth transition from infuse to pour")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // === Pour Phase ===
                        RecipeSection {
                            id: pourSection
                            title: qsTr("Pour")
                            Layout.fillWidth: true

                            RecipeRow {
                                label: qsTr("Temp")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.pourTemperature || 93
                                    from: 80; to: 100; stepSize: 0.5
                                    suffix: "\u00B0C"
                                    valueColor: Theme.temperatureColor
                                    accentColor: Theme.temperatureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("pourTemperature", newValue)
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(20)

                                RadioButton {
                                    text: qsTr("Pressure")
                                    checked: (recipe.pourStyle || "pressure") === "pressure"
                                    onToggled: if (checked) updateRecipe("pourStyle", "pressure")
                                    contentItem: Text {
                                        text: parent.text
                                        font: Theme.captionFont
                                        color: Theme.textColor
                                        leftPadding: parent.indicator.width + parent.spacing
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                RadioButton {
                                    text: qsTr("Flow")
                                    checked: recipe.pourStyle === "flow"
                                    onToggled: if (checked) updateRecipe("pourStyle", "flow")
                                    contentItem: Text {
                                        text: parent.text
                                        font: Theme.captionFont
                                        color: Theme.textColor
                                        leftPadding: parent.indicator.width + parent.spacing
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }

                            RecipeRow {
                                visible: (recipe.pourStyle || "pressure") === "pressure"
                                label: qsTr("Pressure")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.pourPressure || 9.0
                                    from: 3; to: 12; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: Theme.pressureColor
                                    accentColor: Theme.pressureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("pourPressure", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                visible: recipe.pourStyle === "flow"
                                label: qsTr("Flow")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.pourFlow || 2.0
                                    from: 0.5; to: 6; stepSize: 0.1
                                    suffix: " mL/s"
                                    valueColor: Theme.flowColor
                                    accentColor: Theme.flowGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("pourFlow", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                visible: (recipe.pourStyle || "pressure") === "pressure"
                                label: qsTr("Flow limit")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.flowLimit || 0
                                    from: 0; to: 6; stepSize: 0.1
                                    suffix: " mL/s"
                                    valueColor: (recipe.flowLimit || 0) > 0 ? Theme.flowColor : Theme.textSecondaryColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("flowLimit", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                visible: recipe.pourStyle === "flow"
                                label: qsTr("P limit")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.pressureLimit || 0
                                    from: 0; to: 12; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: (recipe.pressureLimit || 0) > 0 ? Theme.pressureColor : Theme.textSecondaryColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("pressureLimit", newValue)
                                    }
                                }
                            }
                        }

                        // === Decline Phase ===
                        RecipeSection {
                            id: declineSection
                            title: qsTr("Decline")
                            Layout.fillWidth: true
                            visible: (recipe.pourStyle || "pressure") === "pressure"
                            canEnable: true
                            sectionEnabled: recipe.declineEnabled || false
                            onSectionToggled: function(enabled) { updateRecipe("declineEnabled", enabled) }

                            RecipeRow {
                                label: qsTr("To")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.declineTo || 6.0
                                    from: 1; to: (recipe.pourPressure || 9) - 1; stepSize: 0.1
                                    suffix: " bar"
                                    valueColor: Theme.pressureColor
                                    accentColor: Theme.pressureGoalColor
                                    onValueModified: function(newValue) {
                                        updateRecipe("declineTo", newValue)
                                    }
                                }
                            }

                            RecipeRow {
                                label: qsTr("Over")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.declineTime || 30
                                    from: 5; to: 60; stepSize: 1
                                    suffix: "s"
                                    decimals: 0
                                    onValueModified: function(newValue) {
                                        updateRecipe("declineTime", newValue)
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
        title: MainController.currentProfileName || qsTr("Recipe")
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
            text: (MainController.currentProfilePtr && MainController.currentProfilePtr.steps ? MainController.currentProfilePtr.steps.length : 0) + " " + qsTr("frames")
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
            text: qsTr("Done")
            accessibleName: recipeModified ? qsTr("Done. Unsaved changes") : qsTr("Done")
            onClicked: {
                if (recipeModified) {
                    exitDialog.open()
                } else {
                    root.goBack()
                }
            }
            background: Rectangle {
                implicitWidth: Theme.scaled(100)
                implicitHeight: Theme.scaled(40)
                radius: Theme.scaled(8)
                color: parent.down ? Qt.darker("white", 1.2) : "white"
            }
            contentItem: Text {
                text: parent.text
                font: Theme.bodyFont
                color: Theme.primaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
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
            MainController.saveProfile(originalProfileName)
            MainController.markProfileClean()
            root.goBack()
        }
    }

    // Save As dialog
    Dialog {
        id: saveAsDialog
        title: qsTr("Save Recipe As")
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(400)
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(10)

            Text {
                text: qsTr("Recipe Title")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
            }

            TextField {
                id: saveAsTitleField
                Layout.fillWidth: true
                text: MainController.currentProfileName || "New Recipe"
                font: Theme.bodyFont
                color: Theme.textColor
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

        onAccepted: {
            if (saveAsTitleField.text.length > 0) {
                var filename = MainController.titleToFilename(saveAsTitleField.text)
                MainController.saveProfileAs(filename, saveAsTitleField.text)
                MainController.markProfileClean()
                root.goBack()
            }
        }

        onOpened: {
            saveAsTitleField.text = MainController.currentProfileName || "New Recipe"
            saveAsTitleField.forceActiveFocus()
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

    // Load recipe when page opens
    Component.onCompleted: {
        // If not already in recipe mode, create a new recipe from current profile settings
        if (!MainController.isCurrentProfileRecipe) {
            MainController.createNewRecipe(MainController.currentProfileName || "New Recipe")
        }
        loadCurrentProfile()
        // Schedule a delayed refresh to ensure chart is ready
        delayedRefreshTimer.start()
    }

    StackView.onActivated: {
        loadCurrentProfile()
        root.currentPageTitle = MainController.currentProfileName || qsTr("Recipe Editor")
        // Schedule a delayed refresh
        delayedRefreshTimer.start()
    }
}
