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

    // Load profile data from MainController
    function loadCurrentProfile() {
        var loadedProfile = MainController.getCurrentProfile()
        console.log("RecipeEditorPage: Loading profile with", loadedProfile ? loadedProfile.steps.length : 0, "steps")
        if (loadedProfile && loadedProfile.steps && loadedProfile.steps.length > 0) {
            profile = loadedProfile
            profileGraph.frames = profile.steps.slice()
            profileGraph.refresh()
        }
        recipe = MainController.getCurrentRecipeParams()
        console.log("RecipeEditorPage: Recipe params - fillTemp:", recipe.fillTemperature, "pourTemp:", recipe.pourTemperature, "fillPressure:", recipe.fillPressure)
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

    // Main content area
    Item {
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin
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
                        selectedFrameIndex: -1
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

                        // Switch to advanced editor
                        Button {
                            text: qsTr("Advanced")
                            onClicked: {
                                root.pageStack.replace("qml/pages/ProfileEditorPage.qml")
                            }
                            background: Rectangle {
                                implicitWidth: Theme.scaled(80)
                                implicitHeight: Theme.scaled(36)
                                radius: Theme.scaled(8)
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.textSecondaryColor
                            }
                            contentItem: Text {
                                text: parent.text
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
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
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.scaled(18)

                        // === Core Settings ===
                        RecipeSection {
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
                            title: qsTr("Bloom")
                            Layout.fillWidth: true

                            CheckBox {
                                id: bloomEnabledCheck
                                text: qsTr("Enable bloom pause")
                                checked: recipe.bloomEnabled || false
                                onToggled: updateRecipe("bloomEnabled", checked)
                                contentItem: Text {
                                    text: parent.text
                                    font: Theme.captionFont
                                    color: Theme.textColor
                                    leftPadding: parent.indicator.width + parent.spacing
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            RecipeRow {
                                visible: bloomEnabledCheck.checked
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
                                visible: bloomEnabledCheck.checked
                                text: qsTr("Zero-flow pause for CO2 release")
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // === Infuse Phase ===
                        RecipeSection {
                            title: qsTr("Infuse")
                            Layout.fillWidth: true

                            RecipeRow {
                                label: qsTr("Pressure")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.infusePressure || 3.0
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

                        // === Pour Phase ===
                        RecipeSection {
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

                            RecipeRow {
                                label: qsTr("Ramp")
                                ValueInput {
                                    Layout.fillWidth: true
                                    value: recipe.rampTime || 5
                                    from: 0; to: 15; stepSize: 0.5
                                    suffix: "s"
                                    onValueModified: function(newValue) {
                                        updateRecipe("rampTime", newValue)
                                    }
                                }
                            }
                        }

                        // === Decline Phase ===
                        RecipeSection {
                            title: qsTr("Decline")
                            Layout.fillWidth: true
                            visible: (recipe.pourStyle || "pressure") === "pressure"

                            CheckBox {
                                id: declineEnabledCheck
                                text: qsTr("Enable pressure decline")
                                checked: recipe.declineEnabled || false
                                onToggled: updateRecipe("declineEnabled", checked)
                                contentItem: Text {
                                    text: parent.text
                                    font: Theme.captionFont
                                    color: Theme.textColor
                                    leftPadding: parent.indicator.width + parent.spacing
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            RecipeRow {
                                visible: declineEnabledCheck.checked
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
                                visible: declineEnabledCheck.checked
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
            text: (MainController.currentProfilePtr ? MainController.currentProfilePtr.steps.length : 0) + " " + qsTr("frames")
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

    // Load recipe when page opens
    Component.onCompleted: {
        // If not already in recipe mode, create a new recipe from current profile settings
        if (!MainController.isCurrentProfileRecipe) {
            MainController.createNewRecipe(MainController.currentProfileName || "New Recipe")
        }
        loadCurrentProfile()
    }

    StackView.onActivated: {
        loadCurrentProfile()
        root.currentPageTitle = MainController.currentProfileName || qsTr("Recipe Editor")
    }
}
