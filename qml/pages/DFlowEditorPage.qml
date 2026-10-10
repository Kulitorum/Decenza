import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

/**
 * DFlowEditorPage - Simplified D-Flow style profile editor
 *
 * Users edit intuitive "coffee concept" parameters like infuse pressure
 * and pour flow, and the app automatically generates DE1 frames.
 */
T.Page {
    id: dflowEditorPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: ProfileManager.currentProfileName || TranslationManager.translate("profileEditor.title", "Profile Editor")

    // Persisted as the pageScale/<objectName> settings key (main.qml), so it keeps its stored spelling.
    objectName: "recipeEditorPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    property var profile: null
    property var params: ProfileManager.getOrConvertProfileParams()
    property bool profileModified: ProfileManager.profileModified
    property string originalProfileName: ProfileManager.baseProfileName

    function handleBack() {
        flushPendingEdits()
        if (profileModified) {
            exitDialog.open()
        } else {
            AppShell.backRequested()
        }
    }

    // Intercept Android system back button / Escape key
    focus: true
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true
            handleBack()
        }
    }

    // Helper: get value with fallback, safe for 0 values (avoids JS falsy coercion)
    function val(v, fallback) {
        return (v !== undefined && v !== null) ? v : fallback
    }

    // Commit any text fields that use onEditingFinished (which won't fire on navigation)
    function flushPendingEdits() {
        if (profile && profileNotesField.text !== (profile.profile_notes || "")) {
            profile.profile_notes = profileNotesField.text
            ProfileManager.uploadProfile(profile)
        }
    }

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
        if (name === "pre fill") return "infuse"  // Pre Fill workaround frame
        if (name.indexOf("fill") !== -1 && name.indexOf("2nd") === -1) return "infuse"  // Fill maps to infuse section
        if (name.indexOf("infuse") !== -1 || name.indexOf("preinfuse") !== -1) return "infuse"
        if (name.indexOf("2nd fill") !== -1) return "aflowToggles"
        if (name.indexOf("pause") !== -1) return "aflowToggles"
        if (name.indexOf("ramp") !== -1 || name.indexOf("transition") !== -1) return "pour"
        if (name.indexOf("pressure up") !== -1) return "pour"
        if (name.indexOf("pressure decline") !== -1) return "pour"
        if (name.indexOf("flow start") !== -1) return "pour"
        if (name.indexOf("flow extraction") !== -1) return "pour"
        if (name.indexOf("pour") !== -1 || name.indexOf("extraction") !== -1) return "pour"

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
            case "pour": targetY = pourSection.y; break
            default: return
        }

        scrollingFromSelection = true
        // Center the section in the view
        var flick = editorScrollView.contentItem as Flickable
        flick.contentY = Math.max(0, targetY - editorScrollView.height / 4)
        // Clear flag after synchronous binding updates have propagated
        Qt.callLater(function() { scrollingFromSelection = false })
    }

    // Find which section is most centered in the scroll view
    function findCenteredSection() {
        var viewCenter = (editorScrollView.contentItem as Flickable).contentY + editorScrollView.height / 2
        var sections = [
            { name: "core", item: coreSection },
            { name: "infuse", item: infuseSection },
            { name: "aflowToggles", item: aflowTogglesSection },
            { name: "pour", item: pourSection }
        ]

        var closest = "infuse"  // Default to infuse if nothing found
        var closestDist = 999999

        for (let i = 0; i < sections.length; i++) {
            let s = sections[i]
            // Skip invisible or disabled sections
            if (!s.item.visible || s.item.height === 0) continue

            let sectionCenter = s.item.y + s.item.height / 2
            let dist = Math.abs(viewCenter - sectionCenter)
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

        for (let i = 0; i < profile.steps.length; i++) {
            if (frameToSection(i) === sectionName) return i
        }

        return -1
    }


    // Load profile data from ProfileManager
    function loadCurrentProfile() {
        params = ProfileManager.getOrConvertProfileParams()

        // Regenerate profile from params to ensure frames match.
        // Preserve modified state — this is just syncing, not a user edit.
        var wasModified = ProfileManager.profileModified
        ProfileManager.uploadProfileFromParams(params)
        if (!wasModified) {
            ProfileManager.markProfileClean()
        }

        var loadedProfile = ProfileManager.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps && loadedProfile.steps.length > 0) {
            profile = loadedProfile
            profileGraph.frames = []
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Update a param and upload to machine
    function updateParam(key, value) {
        var newParams = Object.assign({}, params)
        newParams[key] = value
        params = newParams

        ProfileManager.uploadProfileFromParams(params)

        // Reload profile to get regenerated frames
        var loadedProfile = ProfileManager.getCurrentProfile()
        if (loadedProfile && loadedProfile.steps) {
            profile = loadedProfile
            profileGraph.frames = profile.steps.slice()
        }
    }

    KeyboardAwareContainer {
        id: keyboardContainer
        anchors.fill: parent
        textFields: [profileNotesField.textField]

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
                text: (dflowEditorPage.params.editorType === "aflow")
                    ? TranslationManager.translate("recipeEditor.aFlowEditorTitle", "A-Flow Editor")
                    : TranslationManager.translate("recipeEditor.dFlowEditorTitle", "D-Flow Editor")
                font.family: Theme.titleFont.family
                font.pixelSize: Theme.titleFont.pixelSize
                font.bold: true
                color: Theme.primaryContrastColor
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

                // Profile visualization
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Theme.scaled(120)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ProfileGraph {
                        id: profileGraph
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        frames: []  // Loaded via loadCurrentProfile()
                        selectedFrameIndex: dflowEditorPage.selectedFrameIndex
                        targetWeight: dflowEditorPage.profile ? (dflowEditorPage.profile.target_weight || 0) : 0
                        targetVolume: dflowEditorPage.profile ? (dflowEditorPage.profile.target_volume || 0) : 0

                        onFrameSelected: function(index) {
                            dflowEditorPage.selectedFrameIndex = index
                            var section = dflowEditorPage.frameToSection(index)
                            dflowEditorPage.scrollToSection(section)
                        }
                    }
                }

                // Profile description
                ExpandableTextArea {
                    id: profileNotesField
                    inlineHeight: Theme.scaled(80)
                    text: dflowEditorPage.profile ? (dflowEditorPage.profile.profile_notes || "") : ""
                    accessibleName: TranslationManager.translate("profileEditor.accessible.profileDescription", "Profile description")
                    textFont: Theme.labelFont
                    onEditingFinished: {
                        if (dflowEditorPage.profile) {
                            dflowEditorPage.profile.profile_notes = text
                            ProfileManager.uploadProfile(dflowEditorPage.profile)
                        }
                    }
                }
            }

            // Right side: parameter controls
            Rectangle {
                Layout.preferredWidth: Theme.scaled(320)
                Layout.fillHeight: true
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ScrollView {
                    id: editorScrollView
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(15)
                    clip: true
                    contentWidth: availableWidth

                    // Monitor scroll position to update selected frame
                    // Use onMovingChanged instead of onMovementEnded because
                    // movementEnded does not fire for mouse wheel scrolling on desktop
                    Connections {
                        target: editorScrollView.contentItem
                        function onMovingChanged() {
                            if (!(editorScrollView.contentItem as Flickable).moving && !dflowEditorPage.scrollingFromSelection) {
                                let section = dflowEditorPage.findCenteredSection()
                                let frameIdx = dflowEditorPage.sectionToFrame(section)
                                if (frameIdx >= 0 && frameIdx !== dflowEditorPage.selectedFrameIndex) {
                                    dflowEditorPage.selectedFrameIndex = frameIdx
                                }
                            }
                        }
                        function onDraggingChanged() {
                            if ((editorScrollView.contentItem as Flickable).dragging) {
                                dflowEditorPage.scrollingFromSelection = false
                            }
                        }
                    }

                    ColumnLayout {
                        id: sectionsColumn
                        width: editorScrollView.width - Theme.scaled(14)
                        spacing: Theme.scaled(18)

                        // === Core Settings ===
                        ProfileEditorSection {
                            id: coreSection
                            Layout.fillWidth: true

                            // Dose
                            Text { text: TranslationManager.translate("recipeEditor.dose", "Dose"); font: Theme.captionFont; color: Theme.weightColor }
                            ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: TranslationManager.translate("recipeEditor.dose", "Dose"); from: 3; to: 40; stepSize: 0.1; suffix: " g"; value: ProfileManager.profileRecommendedDose; onValueModified: function(newValue) { ProfileManager.setCurrentProfileRecommendedDose(Math.round(newValue * 10) / 10) } }

                            // Display ratio (weight is set in Pour section)
                            Text {
                                Layout.fillWidth: true
                                text: { var d = ProfileManager.profileRecommendedDose; return TranslationManager.translate("recipeEditor.ratio", "Ratio: 1:") + (d > 0 ? (dflowEditorPage.val(dflowEditorPage.params.targetWeight, 36) / d).toFixed(1) : "--") }
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        // === A-Flow Options ===
                        ProfileEditorSection {
                            id: aflowTogglesSection
                            title: TranslationManager.translate("recipeEditor.aflowTogglesTitle", "A-Flow Options")
                            visible: dflowEditorPage.params.editorType === "aflow"
                            Layout.fillWidth: true

                            // Ramp Down
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)
                                Text {
                                    text: TranslationManager.translate("recipeEditor.rampDown", "Ramp Down")
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                                StyledSwitch {
                                    checked: dflowEditorPage.val(dflowEditorPage.params.rampDownEnabled, false)
                                    accessibleName: TranslationManager.translate("recipeEditor.rampDown", "Ramp Down")
                                    onClicked: {
                                        var newParams = Object.assign({}, dflowEditorPage.params)
                                        if (!dflowEditorPage.params.rampDownEnabled) {
                                            newParams.rampTime = Math.round(dflowEditorPage.params.rampTime * 2)
                                            newParams.rampDownEnabled = true
                                        } else {
                                            newParams.rampTime = Math.round(dflowEditorPage.params.rampTime / 2)
                                            newParams.rampDownEnabled = false
                                        }
                                        dflowEditorPage.params = newParams
                                        ProfileManager.uploadProfileFromParams(dflowEditorPage.params)
                                        var loadedProfile = ProfileManager.getCurrentProfile()
                                        if (loadedProfile && loadedProfile.steps) {
                                            dflowEditorPage.profile = loadedProfile
                                            profileGraph.frames = dflowEditorPage.profile.steps.slice()
                                        }
                                    }
                                }
                            }

                            // Flow Up
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)
                                Text {
                                    text: TranslationManager.translate("recipeEditor.flowUp", "Flow Up")
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                                StyledSwitch {
                                    checked: dflowEditorPage.val(dflowEditorPage.params.flowExtractionUp, true)
                                    accessibleName: TranslationManager.translate("recipeEditor.flowUp", "Flow Up")
                                    onClicked: dflowEditorPage.updateParam("flowExtractionUp", !dflowEditorPage.params.flowExtractionUp)
                                }
                            }

                            // 2nd Fill
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(8)
                                Text {
                                    text: TranslationManager.translate("recipeEditor.secondFill", "2nd Fill")
                                    font: Theme.captionFont
                                    color: Theme.textSecondaryColor
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                                StyledSwitch {
                                    checked: dflowEditorPage.val(dflowEditorPage.params.secondFillEnabled, false)
                                    accessibleName: TranslationManager.translate("recipeEditor.secondFill", "2nd Fill")
                                    onClicked: dflowEditorPage.updateParam("secondFillEnabled", !dflowEditorPage.params.secondFillEnabled)
                                }
                            }
                        }

                        // === Infuse Phase ===
                        ProfileEditorSection {
                            id: infuseSection
                            title: TranslationManager.translate("recipeEditor.infuseTitle", "Infuse")
                            Layout.fillWidth: true

                            // Temp
                            Text { text: TranslationManager.translate("recipeEditor.infuseTemp", "Temp"); font: Theme.captionFont; color: Theme.temperatureColor }
                            ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("recipeEditor.infuseTemperature", "Infuse temperature"); from: Theme.cToDisplay(80); to: Theme.cToDisplay(100); stepSize: 0.1; suffix: Theme.tempUnitSuffix(); value: Theme.cToDisplay(dflowEditorPage.val(dflowEditorPage.params.fillTemperature, 88)); onValueModified: function(newValue) { dflowEditorPage.updateParam("fillTemperature", Math.round(Theme.displayToC(newValue) * 10) / 10) } }

                            // Pressure
                            Text { text: TranslationManager.translate("recipeEditor.infusePressureLabel", "Pressure"); font: Theme.captionFont; color: Theme.pressureColor }
                            ValueInput { Layout.fillWidth: true; valueColor: Theme.pressureColor; accessibleName: TranslationManager.translate("recipeEditor.infusePressure", "Infuse pressure"); from: 0; to: 6; stepSize: 0.01; suffix: " bar"; value: dflowEditorPage.params.infusePressure !== undefined ? dflowEditorPage.params.infusePressure : 3.0; onValueModified: function(newValue) { dflowEditorPage.updateParam("infusePressure", Math.round(newValue * 100) / 100) } }

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
                                    Text { text: TranslationManager.translate("recipeEditor.infuseTimeLabel", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                    ValueInput { Layout.fillWidth: true; accessibleName: TranslationManager.translate("recipeEditor.infuseTime", "Infuse time"); from: 0; to: 60; stepSize: 1; suffix: " s"; displayText: dflowEditorPage.val(dflowEditorPage.params.infuseTime, 20) === 0 ? TranslationManager.translate("profileEditor.off", "off") : ""; value: dflowEditorPage.val(dflowEditorPage.params.infuseTime, 20); onValueModified: function(newValue) { dflowEditorPage.updateParam("infuseTime", Math.round(newValue)) } }

                                    // Volume
                                    Text { text: TranslationManager.translate("recipeEditor.infuseVolumeLabel", "Volume"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                                    ValueInput { Layout.fillWidth: true; accessibleName: TranslationManager.translate("recipeEditor.infuseVolume", "Infuse volume"); from: 10; to: 200; stepSize: 1; suffix: " mL"; value: dflowEditorPage.val(dflowEditorPage.params.infuseVolume, 100); onValueModified: function(newValue) { dflowEditorPage.updateParam("infuseVolume", Math.round(newValue)) } }

                                    // Weight
                                    Text { text: TranslationManager.translate("recipeEditor.infuseWeightLabel", "Weight"); font: Theme.captionFont; color: Theme.weightColor }
                                    ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: TranslationManager.translate("recipeEditor.infuseWeight", "Infuse weight"); from: 0; to: 20; stepSize: 0.1; suffix: " g"; value: dflowEditorPage.val(dflowEditorPage.params.infuseWeight, 4.0); onValueModified: function(newValue) { dflowEditorPage.updateParam("infuseWeight", Math.round(newValue * 10) / 10) } }
                                }
                            }
                        }

                        // === Pour Phase ===
                        ProfileEditorSection {
                            id: pourSection
                            title: TranslationManager.translate("recipeEditor.pourTitle", "Pour")
                            Layout.fillWidth: true

                            // Temp
                            Text { text: TranslationManager.translate("recipeEditor.pourTemp", "Temp"); font: Theme.captionFont; color: Theme.temperatureColor }
                            ValueInput { Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("recipeEditor.pourTemperature", "Pour temperature"); from: Theme.cToDisplay(80); to: Theme.cToDisplay(100); stepSize: 0.1; suffix: Theme.tempUnitSuffix(); value: Theme.cToDisplay(dflowEditorPage.val(dflowEditorPage.params.pourTemperature, 93)); onValueModified: function(newValue) { dflowEditorPage.updateParam("pourTemperature", Math.round(Theme.displayToC(newValue) * 10) / 10) } }

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
                                    Text { text: TranslationManager.translate("recipeEditor.pourFlowLabel", "Flow"); font: Theme.captionFont; color: Theme.flowColor }
                                    ValueInput { Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("recipeEditor.pourFlow", "Pour flow"); from: 0.1; to: ProfileManager.maxSettableFlow; stepSize: 0.01; suffix: " mL/s"; value: dflowEditorPage.val(dflowEditorPage.params.pourFlow, 2.0); onValueModified: function(newValue) { dflowEditorPage.updateParam("pourFlow", Math.round(newValue * 100) / 100) } }

                                    // Pressure limit
                                    Text { text: TranslationManager.translate("recipeEditor.pourPressureLabel", "Pressure"); font: Theme.captionFont; color: Theme.pressureColor }
                                    ValueInput { Layout.fillWidth: true; valueColor: Theme.pressureColor; accessibleName: TranslationManager.translate("recipeEditor.pourPressureLimit", "Pour pressure limit"); from: 1; to: 12; stepSize: 0.01; suffix: " bar"; value: dflowEditorPage.val(dflowEditorPage.params.pourPressure, 9.0); onValueModified: function(newValue) { dflowEditorPage.updateParam("pourPressure", Math.round(newValue * 100) / 100) } }

                                    // Ramp time (A-Flow only — pressure ramp up duration)
                                    Text { text: TranslationManager.translate("recipeEditor.pourTimeLabel", "Time"); font: Theme.captionFont; color: Theme.textSecondaryColor; visible: dflowEditorPage.params.editorType === "aflow" }
                                    ValueInput { Layout.fillWidth: true; accessibleName: TranslationManager.translate("recipeEditor.rampTime", "Ramp time"); visible: dflowEditorPage.params.editorType === "aflow"; from: 0; to: 30; stepSize: 1; suffix: " s"; value: dflowEditorPage.val(dflowEditorPage.params.rampTime, 5); onValueModified: function(newValue) { dflowEditorPage.updateParam("rampTime", Math.round(newValue)) } }
                                }
                            }

                            // Weight stop condition
                            Text { text: TranslationManager.translate("recipeEditor.pourWeightLabel", "Stop at weight"); font: Theme.captionFont; color: Theme.weightColor }
                            ValueInput { Layout.fillWidth: true; valueColor: Theme.weightColor; accessibleName: TranslationManager.translate("recipeEditor.targetWeight", "Target weight"); from: 0; to: 500; stepSize: 0.1; suffix: " g"; displayText: dflowEditorPage.val(dflowEditorPage.params.targetWeight, 36) <= 0 ? TranslationManager.translate("profileEditor.off", "off") : ""; value: dflowEditorPage.val(dflowEditorPage.params.targetWeight, 36); onValueModified: function(newValue) { dflowEditorPage.updateParam("targetWeight", Math.round(newValue * 10) / 10) } }

                            // Volume stop condition (D-Flow only)
                            Text { text: TranslationManager.translate("recipeEditor.pourVolumeLabel", "Stop at volume"); font: Theme.captionFont; color: Theme.textSecondaryColor; visible: dflowEditorPage.params.editorType !== "aflow" }
                            ValueInput { Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("recipeEditor.targetVolume", "Target volume"); visible: dflowEditorPage.params.editorType !== "aflow"; from: 0; to: 500; stepSize: 1; suffix: " mL"; displayText: dflowEditorPage.val(dflowEditorPage.params.targetVolume, 0) <= 0 ? TranslationManager.translate("profileEditor.off", "off") : ""; value: dflowEditorPage.val(dflowEditorPage.params.targetVolume, 0); onValueModified: function(newValue) { dflowEditorPage.updateParam("targetVolume", Math.round(newValue)) } }
                        }

                        // Spacer
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
        title: ProfileManager.currentProfileName || TranslationManager.translate("profileEditor.profile", "Profile")
        onBackClicked: dflowEditorPage.handleBack()

        // Read-only indicator
        Text {
            text: TranslationManager.translate("profileEditor.readOnly", "Read-Only")
            color: Theme.warningColor
            font: Theme.bodyFont
            visible: ProfileManager.isCurrentProfileReadOnly
        }

        // Modified indicator
        Text {
            text: "\u2022 " + TranslationManager.translate("recipeEditor.modified", "Modified")
            color: Theme.warningColor
            font: Theme.bodyFont
            visible: dflowEditorPage.profileModified && !ProfileManager.isCurrentProfileReadOnly
        }

        Rectangle { width: 1; height: Theme.scaled(30); color: bottomBar.contentColor; opacity: 0.3 }

        Text {
            text: ProfileManager.frameCount() + " " + TranslationManager.translate("recipeEditor.frames", "frames")
            color: bottomBar.contentColor
            font: Theme.bodyFont
        }

        Rectangle { width: 1; height: Theme.scaled(30); color: bottomBar.contentColor; opacity: 0.3 }

        Text {
            text: {
                var parts = []
                var w = dflowEditorPage.val(dflowEditorPage.params.targetWeight, 36)
                var v = dflowEditorPage.val(dflowEditorPage.params.targetVolume, 0)
                if (w > 0) parts.push(w.toFixed(0) + TranslationManager.translate("units.grams", "g"))
                if (v > 0) parts.push(v.toFixed(0) + TranslationManager.translate("units.ml", "ml"))
                return parts.length > 0 ? parts.join(" / ") : TranslationManager.translate("profileEditor.off", "off")
            }
            color: bottomBar.contentColor
            font: Theme.bodyFont
        }

        AccessibleButton {
            id: doneButton
            text: TranslationManager.translate("recipeEditor.done", "Done")
            accessibleName: TranslationManager.translate("profileEditor.finishEditing", "Finish editing profile")
            onClicked: {
                dflowEditorPage.flushPendingEdits()
                if (dflowEditorPage.profileModified) {
                    exitDialog.open()
                } else {
                    AppShell.backRequested()
                }
            }
            // White button with primary text for bottom bar
            background: Rectangle {
                implicitWidth: Math.max(Theme.scaled(80), doneText.implicitWidth + Theme.scaled(32))
                implicitHeight: Theme.scaled(36)
                radius: Theme.scaled(6)
                color: doneButton.down || doneButton.isPressed ? Qt.darker(Theme.primaryContrastColor, 1.1) : Theme.primaryContrastColor
            }
            contentItem: Text {
                id: doneText
                text: doneButton.text
                font.pixelSize: Theme.scaled(14)
                font.family: Theme.bodyFont.family
                color: Theme.primaryColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    } // KeyboardAwareContainer

    // Save error dialog
    DecenzaDialog {
        id: saveErrorDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Theme.scaled(350)
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Text {
                text: TranslationManager.translate("recipeEditor.saveError", "Save Failed")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("recipeEditor.saveErrorMessage", "Could not save the profile. Please try again or use Save As with a different name.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            AccessibleButton {
                text: TranslationManager.translate("recipeEditor.ok", "OK")
                accessibleName: TranslationManager.translate("recipeEditor.dismissError", "Dismiss error")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: saveErrorDialog.close()
            }
        }
    }

    // Exit dialog for unsaved changes
    UnsavedChangesDialog {
        id: exitDialog
        itemType: "profile"
        canSave: dflowEditorPage.originalProfileName !== "" && !ProfileManager.isCurrentProfileReadOnly
        showTry: true
        onDiscardClicked: {
            if (dflowEditorPage.originalProfileName) {
                ProfileManager.loadProfile(dflowEditorPage.originalProfileName)
            }
            AppShell.backRequested()
        }
        onTryClicked: {
            ProfileManager.uploadCurrentProfile()
            AppShell.backRequested()
        }
        onSaveAsClicked: saveAsDialog.open()
        onSaveClicked: {
            if (ProfileManager.saveProfile(dflowEditorPage.originalProfileName)) {
                AccessibilityManager.announce(TranslationManager.translate("recipeEditor.profileSaved", "Profile saved"))
                AppShell.backRequested()
            } else {
                AccessibilityManager.announce(TranslationManager.translate("recipeEditor.saveFailed", "Save failed"))
                saveErrorDialog.open()
            }
        }
    }

    // Helper: get the prefix for the current editor type
    function editorPrefix() {
        return (params.editorType === "aflow") ? "A-Flow / " : "D-Flow / "
    }

    // Helper: strip known prefix from a title
    // Handles leading * (modified indicator from imports, e.g. "*D-Flow / myprofile")
    function stripPrefix(title) {
        var t = title.startsWith("*") ? title.substring(1) : title
        if (t.indexOf("D-Flow / ") === 0) return t.substring(9)
        if (t.indexOf("A-Flow / ") === 0) return t.substring(9)
        if (t.indexOf("D-Flow /") === 0) return t.substring(8).trim()
        if (t.indexOf("A-Flow /") === 0) return t.substring(8).trim()
        return t
    }

    // Save As dialog
    DecenzaDialog {
        id: saveAsDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        modal: true
        padding: 0

        property string pendingFilename: ""

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Text {
                text: TranslationManager.translate("profileEditor.saveProfileAs", "Save Profile As")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("profileeditor.label.profiletitle", "Profile Title")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(6)
                spacing: Theme.scaled(4)

                Text {
                    text: dflowEditorPage.editorPrefix()
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    verticalAlignment: Text.AlignVCenter
                    Accessible.ignored: true
                }

                StyledTextField {
                    id: saveAsTitleField
                    Accessible.name: TranslationManager.translate("recipeEditor.profileName", "Profile name")
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileselector.newProfile.title", "New Profile")
                    font: Theme.bodyFont
                    color: Theme.textColor
                    placeholder: TranslationManager.translate("profileEditor.enterProfileName", "Enter profile name")
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
                    onAccepted: saveAsDialog.doSave()
                }
            }

            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("recipeEditor.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("recipeEditor.cancelSave", "Cancel save")
                    Layout.fillWidth: true
                    onClicked: saveAsDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("recipeEditor.save", "Save")
                    accessibleName: TranslationManager.translate("profileEditor.saveProfile", "Save profile")
                    Layout.fillWidth: true
                    onClicked: saveAsDialog.doSave()
                }
            }
        }

        function doSave() {
            Keyboard.commit()
            if (saveAsTitleField.text.length > 0) {
                let fullTitle = dflowEditorPage.editorPrefix() + saveAsTitleField.text
                let filename = ProfileManager.titleToFilename(fullTitle)
                if (ProfileManager.isBuiltInFilename(filename)) {
                    saveAsDialog.close()
                    builtInNameDialog.open()
                    return
                }
                if (ProfileManager.profileExists(filename) && filename !== dflowEditorPage.originalProfileName) {
                    saveAsDialog.pendingFilename = filename
                    saveAsDialog.close()
                    overwriteDialog.open()
                    return
                }
                if (ProfileManager.saveProfileAs(filename, fullTitle)) {
                    AppShell.backRequested()
                } else {
                    saveErrorDialog.open()
                }
            }
            saveAsDialog.close()
        }

        onOpened: {
            var currentName = ProfileManager.currentProfileName || "New Profile"
            saveAsTitleField.text = dflowEditorPage.stripPrefix(currentName)
            saveAsTitleField.forceActiveFocus()
        }
    }

    // Overwrite confirmation dialog
    DecenzaDialog {
        id: overwriteDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Text {
                text: TranslationManager.translate("recipeEditor.profileExists", "Profile Exists")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("recipeEditor.overwriteConfirm", "A profile with this name already exists.\nDo you want to overwrite it?")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            RowLayout {
                spacing: Theme.scaled(10)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)

                AccessibleButton {
                    text: TranslationManager.translate("recipeEditor.no", "No")
                    accessibleName: TranslationManager.translate("recipeEditor.cancelOverwrite", "Cancel overwrite")
                    Layout.fillWidth: true
                    onClicked: overwriteDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("recipeEditor.yes", "Yes")
                    accessibleName: TranslationManager.translate("recipeEditor.confirmOverwrite", "Confirm overwrite")
                    destructive: true
                    Layout.fillWidth: true
                    onClicked: {
                        overwriteDialog.close()
                        var fullTitle = dflowEditorPage.editorPrefix() + saveAsTitleField.text
                        if (ProfileManager.saveProfileAs(saveAsDialog.pendingFilename, fullTitle)) {
                            AppShell.backRequested()
                        } else {
                            saveErrorDialog.open()
                        }
                    }
                }
            }
        }
    }

    // Built-in profile name collision dialog
    DecenzaDialog {
        id: builtInNameDialog
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: Theme.scaled(80)
        width: Math.min(parent.width - Theme.scaled(40), Theme.scaled(400))
        modal: true
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Text {
                text: TranslationManager.translate("profileEditor.reservedName", "Reserved Name")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
            }

            Text {
                text: TranslationManager.translate("profileEditor.reservedNameMessage", "This name is reserved for a built-in profile. Please choose a different name.")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(10)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
            }

            AccessibleButton {
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(20)
                onClicked: {
                    builtInNameDialog.close()
                    saveAsDialog.open()
                }
            }
        }
    }

    // Deferred graph refresh function (replaces timer guard per CLAUDE.md)
    function deferredGraphRefresh() {
        if (profile && profile.steps) {
            profileGraph.frames = profile.steps.slice()
        }
    }

    // Load params when page is actually navigated to (not just instantiated)
    Component.onCompleted: {
        // Don't create a profile here - wait for StackView.onActivated
        // Component.onCompleted fires during instantiation which may happen at app startup
    }

    StackView.onActivated: {
        // Capture the original profile name BEFORE conversion (createNewDFlowProfile clears baseProfileName)
        originalProfileName = ProfileManager.baseProfileName || ""

        // If not already in params mode, create a new D-Flow profile from current profile settings
        var freshConversion = false
        if (!ProfileManager.isCurrentProfileParamsBased) {
            freshConversion = true
            ProfileManager.createNewDFlowProfile(ProfileManager.currentProfileName || "New Profile")
        }
        loadCurrentProfile()
        // Fresh conversion is editor initialization, not a user edit — start clean
        if (freshConversion) {
            ProfileManager.markProfileClean()
        }
        // Deferred refresh to ensure chart is ready (per CLAUDE.md: no timer guards)
        Qt.callLater(deferredGraphRefresh)
    }
}
