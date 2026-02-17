import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

/**
 * SimpleProfileEditorPage - Shared simple profile editor for flow (settings_2b) and pressure (settings_2a)
 *
 * Matches de1app's settings_2a/2b editor layout:
 * Profile Temp → 1: Preinfuse → 2: Hold/Rise and Hold → 3: Decline → 4: Stop at Weight
 *
 * Set profileType to "flow" or "pressure" to switch behavior.
 */
Page {
    id: editorPage
    objectName: isFlow ? "flowEditorPage" : "pressureEditorPage"
    background: Rectangle { color: Theme.backgroundColor }

    required property string profileType
    readonly property bool isFlow: profileType === "flow"

    property var profile: null
    property var recipe: MainController.getOrConvertRecipeParams()
    property bool recipeModified: MainController.profileModified
    property string originalProfileName: MainController.baseProfileName

    property int selectedFrameIndex: -1
    property bool scrollingFromSelection: false

    // Translation helper to avoid repeating ternaries
    function tr(key, fallback) {
        var prefix = isFlow ? "flowEditor" : "pressureEditor"
        return TranslationManager.translate(prefix + "." + key, fallback)
    }

    // Helper: get value with fallback, safe for 0 values (avoids JS falsy coercion)
    function val(v, fallback) {
        return (v !== undefined && v !== null) ? v : fallback
    }

    // Commit any text fields that use onEditingFinished (which won't fire on navigation)
    function flushPendingEdits() {
        if (profile && notesField.text !== (profile.profile_notes || "")) {
            profile.profile_notes = notesField.text
            MainController.uploadProfile(profile)
        }
    }

    // Helper: get display temp for a step
    function stepTemp(stepKey) {
        return val(recipe[stepKey], val(recipe.pourTemperature, 90))
    }

    function frameToSection(frameIndex) {
        if (!profile || !profile.steps || frameIndex < 0 || frameIndex >= profile.steps.length)
            return "preinfusion"
        var name = (profile.steps[frameIndex].name || "").toLowerCase()
        if (name.indexOf("preinfusion") !== -1 || name.indexOf("temp boost") !== -1) return "preinfusion"
        if (isFlow) {
            if (name.indexOf("hold") !== -1) return "hold"
        } else {
            if (name.indexOf("forced rise") !== -1) return "hold"
            if (name.indexOf("rise and hold") !== -1) return "hold"
        }
        if (name.indexOf("decline") !== -1) return "decline"
        // Position-based fallback for profiles with non-standard frame names
        var totalFrames = profile.steps.length
        if (frameIndex === 0) return "preinfusion"
        if (frameIndex >= totalFrames - 1) return "decline"
        return "hold"
    }

    function scrollToSection(sectionName) {
        var targetY = 0
        switch (sectionName) {
            case "preinfusion": targetY = preinfusionSection.y; break
            case "hold": targetY = holdSection.y; break
            case "decline": targetY = declineSection.y; break
            default: return
        }
        scrollingFromSelection = true
        var scrollTarget = Math.max(0, targetY - editorScrollView.height / 4)
        editorScrollView.contentItem.contentY = scrollTarget
        // Clear flag after synchronous binding updates have propagated
        Qt.callLater(function() { scrollingFromSelection = false })
    }

    function findCenteredSection() {
        var viewCenter = editorScrollView.contentItem.contentY + editorScrollView.height / 2
        var sections = [
            { name: "preinfusion", item: preinfusionSection },
            { name: "hold", item: holdSection },
            { name: "decline", item: declineSection }
        ]
        var closest = "hold"
        var closestDist = 999999
        for (var i = 0; i < sections.length; i++) {
            var s = sections[i]
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

    function sectionToFrame(sectionName) {
        if (!profile || !profile.steps) return -1
        for (var i = 0; i < profile.steps.length; i++) {
            if (frameToSection(i) === sectionName) return i
        }
        return -1
    }


    function loadCurrentProfile() {
        recipe = MainController.getOrConvertRecipeParams()
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
        } else {
            console.warn("SimpleProfileEditorPage: loadCurrentProfile failed to get valid profile")
        }
    }

    function updateRecipe(key, value) {
        var newRecipe = Object.assign({}, recipe)
        newRecipe[key] = value
        recipe = newRecipe
        MainController.uploadRecipeProfile(recipe)
        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps) {
            profile = loadedProfile
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Update profile temp — sets all 4 step temps at once
    function updateProfileTemp(newTemp) {
        var newRecipe = Object.assign({}, recipe)
        newRecipe.pourTemperature = newTemp
        newRecipe.tempStart = newTemp
        newRecipe.tempPreinfuse = newTemp
        newRecipe.tempHold = newTemp
        newRecipe.tempDecline = newTemp
        recipe = newRecipe
        MainController.uploadRecipeProfile(recipe)
        var loadedProfile = MainController.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps) {
            profile = loadedProfile
            profileGraph.frames = profile.steps.slice()
        }
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        textFields: [notesField, saveAsTitleField]

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
                text: isFlow ? tr("title", "Flow Profile Editor") : tr("title", "Pressure Profile Editor")
                font.family: Theme.titleFont.family
                font.pixelSize: Theme.titleFont.pixelSize
                font.bold: true
                color: "white"
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Accessible.focusable: true
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
                        frames: []
                        selectedFrameIndex: editorPage.selectedFrameIndex
                        targetWeight: profile ? (profile.target_weight || 0) : 0
                        targetVolume: profile ? (profile.target_volume || 0) : 0

                        onFrameSelected: function(index) {
                            editorPage.selectedFrameIndex = index
                            var section = frameToSection(index)
                            scrollToSection(section)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(80)
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(6)
                        contentWidth: availableWidth
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded

                        TextArea {
                            id: notesField
                            Accessible.role: Accessible.EditableText
                            Accessible.name: TranslationManager.translate("profileEditor.accessible.profileDescription", "Profile description")
                            Accessible.description: text
                            text: profile ? (profile.profile_notes || "") : ""
                            font: Theme.labelFont
                            color: Theme.textColor
                            wrapMode: TextArea.Wrap
                            leftPadding: Theme.scaled(8)
                            rightPadding: Theme.scaled(8)
                            topPadding: Theme.scaled(4)
                            bottomPadding: Theme.scaled(4)
                            background: Rectangle { color: "transparent" }
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

            // Right side: Editor controls (de1app style)
            Rectangle {
                Layout.preferredWidth: Theme.scaled(320)
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ScrollView {
                    id: editorScrollView
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    clip: true
                    contentWidth: availableWidth

                    // Use onMovingChanged instead of onMovementEnded because
                    // movementEnded does not fire for mouse wheel scrolling on desktop
                    Connections {
                        target: editorScrollView.contentItem
                        function onMovingChanged() {
                            if (!editorScrollView.contentItem.moving && !scrollingFromSelection) {
                                var section = findCenteredSection()
                                var frameIdx = sectionToFrame(section)
                                if (frameIdx >= 0 && frameIdx !== selectedFrameIndex) {
                                    selectedFrameIndex = frameIdx
                                }
                            }
                        }
                        function onDraggingChanged() {
                            if (editorScrollView.contentItem.dragging) {
                                scrollingFromSelection = false
                            }
                        }
                    }

                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.scaled(14)

                        // === Profile Temperature ===
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)

                                Text {
                                    text: tr("profileTemp", "Profile temp")
                                    font.family: Theme.captionFont.family
                                    font.pixelSize: Theme.captionFont.pixelSize
                                    font.bold: true
                                    color: Theme.textColor
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: val(recipe.pourTemperature, 90).toFixed(1) + "\u00B0C"
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.bodyFont.pixelSize
                                    font.bold: true
                                    color: Theme.temperatureColor
                                }

                                // Temp Steps button
                                Rectangle {
                                    Layout.preferredWidth: tempStepsText.implicitWidth + Theme.scaled(16)
                                    Layout.preferredHeight: Theme.scaled(28)
                                    radius: Theme.scaled(6)
                                    color: Theme.backgroundColor
                                    border.color: Theme.temperatureColor
                                    border.width: 1

                                    Text {
                                        id: tempStepsText
                                        anchors.centerIn: parent
                                        text: tr("tempSteps", "Steps")
                                        font.family: Theme.captionFont.family
                                        font.pixelSize: Theme.captionFont.pixelSize
                                        color: Theme.temperatureColor
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Edit temperature steps"
                                        Accessible.focusable: true
                                        onClicked: tempStepsDialog.open()
                                    }
                                }
                            }

                            ValueInput {
                                id: profileTempSlider
                                Layout.fillWidth: true; valueColor: Theme.temperatureColor
                                accessibleName: "Profile temperature"
                                from: 70; to: 100; stepSize: 0.1
                                value: val(recipe.pourTemperature, 90)
                                onValueModified: function(newValue) { updateProfileTemp(Math.round(newValue * 10) / 10) }
                            }
                        }

                        // Separator
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.borderColor
                        }

                        // === 1: Preinfuse ===
                        Item {
                            id: preinfusionSection
                            Layout.fillWidth: true
                            implicitHeight: preinfuseCol.implicitHeight

                            ColumnLayout {
                                id: preinfuseCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: Theme.scaled(4)

                                // Step header with temp badge
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(6)
                                    Text { text: "1:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                                    Text { text: tr("preinfuse", "Preinfuse"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                                    Item { Layout.fillWidth: true }
                                    Rectangle {
                                        Layout.preferredWidth: tempPreinfuseLabel.implicitWidth + Theme.scaled(12); Layout.preferredHeight: Theme.scaled(32)
                                        radius: Theme.scaled(12); color: Qt.rgba(Theme.temperatureColor.r, Theme.temperatureColor.g, Theme.temperatureColor.b, 0.15)
                                        Text { id: tempPreinfuseLabel; anchors.centerIn: parent; text: stepTemp("tempStart").toFixed(1) + "/" + stepTemp("tempPreinfuse").toFixed(1) + "\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; color: Theme.temperatureColor }
                                        MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Edit preinfuse temperature"; Accessible.focusable: true; onClicked: tempStepsDialog.open() }
                                    }
                                }

                                // Time
                                Text { text: TranslationManager.translate("simpleProfile.time", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                ValueInput { Layout.fillWidth: true; accessibleName: "Preinfusion time"; from: 0; to: 60; stepSize: 1; value: val(recipe.preinfusionTime, 20); onValueModified: function(newValue) { updateRecipe("preinfusionTime", Math.round(newValue)) } }

                                // Flow rate
                                Text { text: TranslationManager.translate("simpleProfile.flowRate", "Flow rate"); font: Theme.captionFont; color: Theme.flowColor }
                                ValueInput { Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: "Preinfusion flow rate"; from: 1; to: 10; stepSize: 0.1; value: val(recipe.preinfusionFlowRate, 8.0); onValueModified: function(newValue) { updateRecipe("preinfusionFlowRate", Math.round(newValue * 10) / 10) } }

                                // Pressure
                                Text { text: TranslationManager.translate("simpleProfile.pressure", "Pressure"); font: Theme.captionFont; color: Theme.pressureColor }
                                ValueInput { Layout.fillWidth: true; valueColor: Theme.pressureColor; accessibleName: "Preinfusion stop pressure"; from: 0.5; to: 8; stepSize: 0.1; value: val(recipe.preinfusionStopPressure, 4.0); onValueModified: function(newValue) { updateRecipe("preinfusionStopPressure", Math.round(newValue * 10) / 10) } }
                            }
                        }

                        // Separator
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                        // === 2: Hold (flow) / Rise and Hold (pressure) ===
                        Item {
                            id: holdSection
                            Layout.fillWidth: true
                            implicitHeight: holdCol.implicitHeight

                            ColumnLayout {
                                id: holdCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: Theme.scaled(4)

                                // Step header with temp badge
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(6)
                                    Text { text: "2:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                                    Text { text: isFlow ? tr("hold", "Hold") : tr("riseAndHold", "Rise and Hold"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                                    Item { Layout.fillWidth: true }
                                    Rectangle {
                                        Layout.preferredWidth: tempHoldLabel.implicitWidth + Theme.scaled(12); Layout.preferredHeight: Theme.scaled(32)
                                        radius: Theme.scaled(12); color: Qt.rgba(Theme.temperatureColor.r, Theme.temperatureColor.g, Theme.temperatureColor.b, 0.15)
                                        Text { id: tempHoldLabel; anchors.centerIn: parent; text: stepTemp("tempHold").toFixed(1) + "\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; color: Theme.temperatureColor }
                                        MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Edit hold temperature"; Accessible.focusable: true; onClicked: tempStepsDialog.open() }
                                    }
                                }

                                // Time
                                Text { text: TranslationManager.translate("simpleProfile.holdTime", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                ValueInput { Layout.fillWidth: true; accessibleName: "Hold time"; from: 0; to: 60; stepSize: 1; value: val(recipe.holdTime, 10); onValueModified: function(newValue) { updateRecipe("holdTime", Math.round(newValue)) } }

                                // Flow: holdFlow + limit pressure
                                // Pressure: limit flow + espressoPressure
                                // First slider: flow has holdFlow, pressure has limiterValue (limit flow)
                                Text { text: isFlow ? TranslationManager.translate("simpleProfile.flow", "Flow") : TranslationManager.translate("simpleProfile.limitFlow", "Limit flow"); font: Theme.captionFont; color: Theme.flowColor }
                                ValueInput {
                                    Layout.fillWidth: true; valueColor: Theme.flowColor
                                    accessibleName: isFlow ? "Hold flow" : "Flow limiter"
                                    from: isFlow ? 0.1 : 0; to: 8; stepSize: 0.1
                                    value: isFlow ? val(recipe.holdFlow, 2.2) : val(recipe.limiterValue, 3.5)
                                    onValueModified: function(newValue) { isFlow
                                        ? updateRecipe("holdFlow", Math.round(newValue * 10) / 10)
                                        : updateRecipe("limiterValue", Math.round(newValue * 10) / 10) }
                                }

                                // Second slider: flow has limiterValue (limit pressure), pressure has espressoPressure
                                Text { text: isFlow ? TranslationManager.translate("simpleProfile.limitPressure", "Limit pressure") : TranslationManager.translate("simpleProfile.pressure2", "Pressure"); font: Theme.captionFont; color: Theme.pressureColor }
                                ValueInput {
                                    Layout.fillWidth: true; valueColor: Theme.pressureColor
                                    accessibleName: isFlow ? "Pressure limiter" : "Hold pressure"
                                    from: isFlow ? 0 : 1; to: 12; stepSize: 0.1
                                    value: isFlow ? val(recipe.limiterValue, 3.5) : val(recipe.espressoPressure, 8.4)
                                    onValueModified: function(newValue) { isFlow
                                        ? updateRecipe("limiterValue", Math.round(newValue * 10) / 10)
                                        : updateRecipe("espressoPressure", Math.round(newValue * 10) / 10) }
                                }
                            }
                        }

                        // Separator
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                        // === 3: Decline ===
                        Item {
                            id: declineSection
                            Layout.fillWidth: true
                            implicitHeight: declineCol.implicitHeight

                            ColumnLayout {
                                id: declineCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: Theme.scaled(4)

                                // Step header with temp badge
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(6)
                                    Text { text: "3:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                                    Text { text: tr("decline", "Decline"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                                    Item { Layout.fillWidth: true }
                                    Rectangle {
                                        Layout.preferredWidth: tempDeclineLabel.implicitWidth + Theme.scaled(12); Layout.preferredHeight: Theme.scaled(32)
                                        radius: Theme.scaled(12); color: Qt.rgba(Theme.temperatureColor.r, Theme.temperatureColor.g, Theme.temperatureColor.b, 0.15)
                                        Text { id: tempDeclineLabel; anchors.centerIn: parent; text: stepTemp("tempDecline").toFixed(1) + "\u00B0C"; font.family: Theme.captionFont.family; font.pixelSize: Theme.captionFont.pixelSize; color: Theme.temperatureColor }
                                        MouseArea { anchors.fill: parent; Accessible.role: Accessible.Button; Accessible.name: "Edit decline temperature"; Accessible.focusable: true; onClicked: tempStepsDialog.open() }
                                    }
                                }

                                // Time
                                Text { text: TranslationManager.translate("simpleProfile.declineTime", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                ValueInput { Layout.fillWidth: true; accessibleName: "Decline time"; from: 0; to: 60; stepSize: 1; value: val(recipe.simpleDeclineTime, 30); onValueModified: function(newValue) { updateRecipe("simpleDeclineTime", Math.round(newValue)) } }

                                // End value: flow has flowEnd (mL/s), pressure has pressureEnd (bar)
                                Text { text: isFlow ? TranslationManager.translate("simpleProfile.endFlow", "Flow") : TranslationManager.translate("simpleProfile.endPressure", "Pressure"); font: Theme.captionFont; color: isFlow ? Theme.flowColor : Theme.pressureColor }
                                ValueInput {
                                    Layout.fillWidth: true; valueColor: isFlow ? Theme.flowColor : Theme.pressureColor
                                    accessibleName: isFlow ? "Decline end flow" : "Decline pressure"
                                    from: 0; to: isFlow ? 8 : 12; stepSize: 0.1
                                    value: isFlow ? val(recipe.flowEnd, 1.8) : val(recipe.pressureEnd, 6.0)
                                    onValueModified: function(newValue) { isFlow
                                        ? updateRecipe("flowEnd", Math.round(newValue * 10) / 10)
                                        : updateRecipe("pressureEnd", Math.round(newValue * 10) / 10) }
                                }
                            }
                        }

                        // Separator
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                        // === 4: Stop at Weight ===
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: stopCol.implicitHeight

                            ColumnLayout {
                                id: stopCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: Theme.scaled(4)

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.scaled(6)
                                    Text { text: "4:"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.primaryColor }
                                    Text { text: tr("stopAtWeight", "Stop at Weight"); font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.textColor }
                                }

                                // Dose
                                Text { text: TranslationManager.translate("simpleProfile.dose", "Dose"); font: Theme.captionFont; color: Theme.weightColor }
                                ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: "Dose"; from: 3; to: 40; stepSize: 0.1; value: val(recipe.dose, 18); onValueModified: function(newValue) { updateRecipe("dose", Math.round(newValue * 10) / 10) } }

                                // Weight
                                Text { text: TranslationManager.translate("simpleProfile.weight", "Weight"); font: Theme.captionFont; color: Theme.weightColor }
                                ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: "Target weight"; from: 0; to: 100; stepSize: 0.1; value: val(recipe.targetWeight, 36); onValueModified: function(newValue) { updateRecipe("targetWeight", Math.round(newValue * 10) / 10) } }

                                Text {
                                    Layout.fillWidth: true
                                    text: { var d = val(recipe.dose, 18); return tr("ratio", "Ratio: 1:") + (d > 0 ? (val(recipe.targetWeight, 36) / d).toFixed(1) : "--") }
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // Bottom bar — counteract keyboard shift so it stays at screen bottom (behind keyboard)
    BottomBar {
        id: bottomBar
        transform: Translate { y: keyboardContainer.keyboardOffset }
        title: MainController.currentProfileName || (isFlow ? tr("flow", "Flow") : tr("pressure", "Pressure"))
        onBackClicked: {
            flushPendingEdits()
            if (recipeModified) {
                exitDialog.open()
            } else {
                root.goBack()
            }
        }

        Text {
            text: "\u2022 Modified"
            color: Theme.warningColor
            font: Theme.bodyFont
            visible: recipeModified
        }

        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

        Text {
            text: MainController.frameCount() + " " + tr("frames", "frames")
            color: "white"
            font: Theme.bodyFont
        }

        Rectangle { width: 1; height: Theme.scaled(30); color: "white"; opacity: 0.3 }

        Text {
            text: val(recipe.targetWeight, 36).toFixed(0) + "g"
            color: "white"
            font: Theme.bodyFont
        }

        AccessibleButton {
            text: tr("done", "Done")
            accessibleName: isFlow ? tr("finishEditing", "Finish editing flow profile") : tr("finishEditing", "Finish editing pressure profile")
            onClicked: {
                flushPendingEdits()
                if (recipeModified) {
                    exitDialog.open()
                } else {
                    root.goBack()
                }
            }
            background: Rectangle {
                implicitWidth: Math.max(Theme.scaled(80), doneText.implicitWidth + Theme.scaled(32))
                implicitHeight: Theme.scaled(36)
                radius: Theme.scaled(6)
                color: parent.down ? Qt.darker("white", 1.1) : "white"
            }
            contentItem: Text {
                id: doneText
                text: parent.text
                font.pixelSize: Theme.scaled(14)
                font.family: Theme.bodyFont.family
                color: Theme.primaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    } // KeyboardAwareContainer

    // === Temperature Steps Dialog ===
    Dialog {
        id: tempStepsDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
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
                    text: tr("tempStepsTitle", "Temperature Steps")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(12)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scaled(32); height: Theme.scaled(32)
                    radius: Theme.scaled(16)
                    color: tempCloseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "\u2715"
                        font.pixelSize: Theme.scaled(16)
                        color: Theme.textSecondaryColor
                    }
                    MouseArea {
                        id: tempCloseArea
                        anchors.fill: parent
                        Accessible.role: Accessible.Button
                        Accessible.name: "Close temperature steps"
                        Accessible.focusable: true
                        onClicked: tempStepsDialog.close()
                    }
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
                spacing: Theme.scaled(14)

                // Start
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: tr("start", "Start"); font: Theme.bodyFont; color: Theme.textColor }
                        Item { Layout.fillWidth: true }
                        Text { text: val(recipe.tempStart, 90).toFixed(1) + "\u00B0C"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                    }
                    ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: "Start temperature"; from: 70; to: 100; stepSize: 0.1; value: val(recipe.tempStart, 90); onValueModified: function(newValue) { updateRecipe("tempStart", Math.round(newValue * 10) / 10) } }
                }

                // 1: Preinfuse
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "1: " + tr("preinfuse", "Preinfuse"); font: Theme.bodyFont; color: Theme.textColor }
                        Item { Layout.fillWidth: true }
                        Text { text: val(recipe.tempPreinfuse, 90).toFixed(1) + "\u00B0C"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                    }
                    ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: "Preinfuse temperature"; from: 70; to: 100; stepSize: 0.1; value: val(recipe.tempPreinfuse, 90); onValueModified: function(newValue) { updateRecipe("tempPreinfuse", Math.round(newValue * 10) / 10) } }
                }

                // 2: Hold / Rise and Hold
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: isFlow ? ("2: " + tr("hold", "Hold")) : ("2: " + tr("riseAndHold", "Rise and Hold")); font: Theme.bodyFont; color: Theme.textColor }
                        Item { Layout.fillWidth: true }
                        Text { text: val(recipe.tempHold, 90).toFixed(1) + "\u00B0C"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                    }
                    ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: isFlow ? "Hold temperature" : "Rise and hold temperature"; from: 70; to: 100; stepSize: 0.1; value: val(recipe.tempHold, 90); onValueModified: function(newValue) { updateRecipe("tempHold", Math.round(newValue * 10) / 10) } }
                }

                // 3: Decline
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "3: " + tr("decline", "Decline"); font: Theme.bodyFont; color: Theme.textColor }
                        Item { Layout.fillWidth: true }
                        Text { text: val(recipe.tempDecline, 90).toFixed(1) + "\u00B0C"; font.family: Theme.bodyFont.family; font.pixelSize: Theme.bodyFont.pixelSize; font.bold: true; color: Theme.temperatureColor }
                    }
                    ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: "Decline temperature"; from: 70; to: 100; stepSize: 0.1; value: val(recipe.tempDecline, 90); onValueModified: function(newValue) { updateRecipe("tempDecline", Math.round(newValue * 10) / 10) } }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(6)
                    text: TranslationManager.translate("common.done", "Done")
                    accessibleName: TranslationManager.translate("common.done", "Done")
                    onClicked: tempStepsDialog.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(44)
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

    // Save error dialog
    Dialog {
        id: saveErrorDialog
        anchors.centerIn: parent
        width: Theme.scaled(350)
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

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: tr("saveError", "Save Failed")
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

            Text {
                text: tr("saveErrorMessage", "Could not save the profile. Please try again or use Save As with a different name.")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                text: "OK"
                accessibleName: "OK"
                onClicked: saveErrorDialog.close()
                background: Rectangle {
                    implicitHeight: Theme.scaled(44)
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

    // Exit dialog for unsaved changes
    UnsavedChangesDialog {
        id: exitDialog
        itemType: "profile"
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

    // Save As dialog
    Dialog {
        id: saveAsDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        modal: true
        padding: 0

        property string pendingFilename: ""

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: "white"
        }

        contentItem: ColumnLayout {
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: tr("saveAs", "Save Profile As")
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

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(10)

                Text {
                    text: tr("profileTitle", "Profile Title")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                }

                StyledTextField {
                    id: saveAsTitleField
                    Accessible.name: "Profile name"
                    Layout.fillWidth: true
                    text: isFlow ? "New Flow Profile" : "New Pressure Profile"
                    font: Theme.bodyFont
                    color: Theme.textColor
                    placeholder: tr("namePlaceholder", "Enter profile name")
                    leftPadding: Theme.scaled(12)
                    rightPadding: Theme.scaled(12)
                    topPadding: Theme.scaled(12)
                    bottomPadding: Theme.scaled(12)
                    background: Rectangle {
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                        border.color: saveAsTitleField.activeFocus ? Theme.primaryColor : Theme.borderColor
                        border.width: 1
                    }
                    onAccepted: saveAsDialog.doSave()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                        onClicked: saveAsDialog.close()
                        background: Rectangle {
                            implicitHeight: Theme.scaled(44)
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
                        text: TranslationManager.translate("common.save", "Save")
                        accessibleName: TranslationManager.translate("common.save", "Save")
                        onClicked: saveAsDialog.doSave()
                        background: Rectangle {
                            implicitHeight: Theme.scaled(44)
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

        function doSave() {
            if (saveAsTitleField.text.length > 0) {
                var filename = MainController.titleToFilename(saveAsTitleField.text)
                if (MainController.profileExists(filename) && filename !== originalProfileName) {
                    saveAsDialog.pendingFilename = filename
                    overwriteDialog.open()
                } else {
                    if (MainController.saveProfileAs(filename, saveAsTitleField.text)) {
                        root.goBack()
                    } else {
                        saveErrorDialog.open()
                    }
                }
            }
        }

        onOpened: {
            var defaultName = isFlow ? "New Flow Profile" : "New Pressure Profile"
            saveAsTitleField.text = MainController.currentProfileName || defaultName
            saveAsTitleField.forceActiveFocus()
        }
    }

    // Overwrite confirmation dialog
    Dialog {
        id: overwriteDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
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

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Layout.topMargin: Theme.scaled(10)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: tr("profileExists", "Profile Exists")
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

            Text {
                text: tr("overwriteConfirm", "A profile with this name already exists.\nDo you want to overwrite it?")
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                spacing: Theme.scaled(10)

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("common.no", "No")
                    accessibleName: TranslationManager.translate("common.no", "No")
                    onClicked: overwriteDialog.close()
                    background: Rectangle {
                        implicitHeight: Theme.scaled(44)
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
                    text: TranslationManager.translate("common.yes", "Yes")
                    accessibleName: TranslationManager.translate("common.yes", "Yes")
                    onClicked: {
                        overwriteDialog.close()
                        if (MainController.saveProfileAs(saveAsDialog.pendingFilename, saveAsTitleField.text)) {
                            root.goBack()
                        } else {
                            saveErrorDialog.open()
                        }
                    }
                    background: Rectangle {
                        implicitHeight: Theme.scaled(44)
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


    StackView.onActivated: {
        // Capture BEFORE conversion (createNew*Profile clears baseProfileName)
        originalProfileName = MainController.baseProfileName || ""
        var freshConversion = false
        if (!MainController.isCurrentProfileRecipe) {
            freshConversion = true
            console.warn("SimpleProfileEditorPage: Converting non-recipe profile to",
                         isFlow ? "flow" : "pressure", "- original:", MainController.currentProfileName)
            var defaultName = isFlow ? "New Flow Profile" : "New Pressure Profile"
            if (isFlow) {
                MainController.createNewFlowProfile(MainController.currentProfileName || defaultName)
            } else {
                MainController.createNewPressureProfile(MainController.currentProfileName || defaultName)
            }
        }
        loadCurrentProfile()
        // Fresh conversion is editor initialization, not a user edit — start clean
        if (freshConversion) {
            MainController.markProfileClean()
        }
        var editorTitle = isFlow ? tr("title", "Flow Profile Editor") : tr("title", "Pressure Profile Editor")
        root.currentPageTitle = MainController.currentProfileName || editorTitle
        Qt.callLater(function() {
            if (profile && profile.steps) {
                profileGraph.frames = profile.steps.slice()
            }
        })
    }
}
