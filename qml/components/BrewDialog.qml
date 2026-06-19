import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    property real dialogScale: 0.75
    width: Theme.scaled(520) * dialogScale
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    // Accessibility: Dialog is announced via onAboutToShow below
    // Note: Don't set 'title' property - it creates a built-in header frame
    // Note: Don't set Accessible properties on Dialog - it doesn't derive from Item

    // Temperature override
    property double temperatureValue: ProfileManager.profileTargetTemperature
    property double profileTemperature: ProfileManager.profileTargetTemperature

    // Dose value (editable, default 18g)
    property double doseValue: 18.0
    property double ratio: Settings.brew.lastUsedRatio

    // Target (yield) value and tracking
    property double targetValue: doseValue * ratio
    property double profileTargetWeight: ProfileManager.profileTargetWeight
    property bool targetManuallySet: false

    // Equipment (read-only, resolved from the active package via SettingsDye) +
    // the dial-in fields that live in Brew Settings (grind setting + rpm).
    readonly property string equipmentBrand: Settings.dye.dyeGrinderBrand
    readonly property string equipmentModel: Settings.dye.dyeGrinderModel
    readonly property bool equipmentRpmCapable: Settings.dye.grinderRpmCapable(equipmentBrand, equipmentModel)
    // Show the package's display name (defaults to "{brand} {model}"), not the
    // raw grinder identity (add-equipment-packages).
    readonly property string equipmentLabel: Settings.dye.dyeEquipmentName
    property string grindSetting: ""
    property int grindRpm: 0

    // Profile
    property string selectedProfileTitle: ""
    property string originalProfileFilename: ""

    function getProfileSuggestions() {
        var profiles = ProfileManager.availableProfiles
        var titles = []
        for (var i = 0; i < profiles.length; i++)
            titles.push(profiles[i].title)
        return titles
    }

    function loadProfileByTitle(title) {
        var filename = ProfileManager.findProfileByTitle(title)
        if (filename.length > 0) {
            ProfileManager.loadProfile(filename)
            root.profileTemperature = ProfileManager.profileTargetTemperature
            root.temperatureValue = root.profileTemperature
            root.profileTargetWeight = ProfileManager.profileTargetWeight
            if (!root.targetManuallySet)
                root.targetValue = root.profileTargetWeight
        }
    }

    // Grinder identity is now chosen via the Switch Equipment dialog, so only the
    // grind-setting field keeps a suggestion list (scoped to the active grinder).
    function getGrinderSettingSuggestions() {
        var suggestions = MainController.shotHistory ? MainController.shotHistory.getDistinctGrinderSettingsForGrinder(root.equipmentModel) : []
        if (Settings.dye.dyeGrinderSetting.length > 0 && suggestions.indexOf(Settings.dye.dyeGrinderSetting) === -1) {
            suggestions.unshift(Settings.dye.dyeGrinderSetting)
        }
        return suggestions
    }

    // Incremented when async distinct cache refreshes; referenced in suggestion bindings
    // to force QML re-evaluation (the >= 0 condition is always true by design)
    property int _distinctCacheVersion: 0

    Connections {
        target: MainController.shotHistory
        function onDistinctCacheReady() {
            _distinctCacheVersion++
        }
    }

    // Low dose warning - shown when dose is low OR when scale read failed
    property bool showScaleWarning: false
    property bool lowDoseWarning: doseValue < 3 || showScaleWarning

    // Recalculate target when dose or ratio changes (unless manually overridden)
    onDoseValueChanged: {
        if (!targetManuallySet) {
            targetValue = doseValue * ratio
        }
    }

    onRatioChanged: {
        if (!targetManuallySet) {
            targetValue = doseValue * ratio
        }
    }

    onRejected: {
        // Restore the original profile if the user changed it via the profile picker
        if (originalProfileFilename.length > 0 && Settings.app.currentProfile !== originalProfileFilename) {
            ProfileManager.loadProfile(originalProfileFilename)
        }
    }

    onAboutToShow: {
        // Announce dialog for accessibility
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var announcement = TranslationManager.translate("brewDialog.dialogAnnouncement", "Brew Settings dialog. Profile: ") + ProfileManager.currentProfileName
            if (Settings.dye.dyeBeanBrand.length > 0)
                announcement += ". " + TranslationManager.translate("brewDialog.roasterAnnouncementLabel", "Roaster: ") + Settings.dye.dyeBeanBrand
            if (Settings.dye.dyeBeanType.length > 0)
                announcement += ". " + TranslationManager.translate("brewDialog.coffeeAnnouncementLabel", "Coffee: ") + Settings.dye.dyeBeanType
            AccessibilityManager.announce(announcement)
        }

        // Update profile temperature, use override if active
        profileTemperature = ProfileManager.profileTargetTemperature
        profileTargetWeight = ProfileManager.profileTargetWeight
        temperatureValue = Settings.brew.hasTemperatureOverride ? Settings.brew.temperatureOverride : profileTemperature

        // Use DYE fields for dose and grind (source of truth). Grinder identity
        // is read-only here (resolved from the active package); only the dial-in
        // (grind setting + rpm) is editable.
        doseValue = Settings.dye.dyeBeanWeight > 0 ? Settings.dye.dyeBeanWeight : 18.0
        grindSetting = Settings.dye.dyeGrinderSetting
        grindRpm = Settings.dye.dyeGrinderRpm
        selectedProfileTitle = ProfileManager.currentProfileName
        originalProfileFilename = Settings.app.currentProfile
        showScaleWarning = false

        // Yield: use override if active, otherwise use profile default
        targetValue = Settings.brew.hasBrewYieldOverride ? Settings.brew.brewYieldOverride : profileTargetWeight
        ratio = doseValue > 0 ? targetValue / doseValue : Settings.brew.lastUsedRatio
        targetManuallySet = Settings.brew.hasBrewYieldOverride
    }

    SwitchEquipmentDialog {
        id: switchEquipmentDialog
    }

    EquipmentInfoDialog {
        id: equipmentInfoDialog
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: KeyboardAwareContainer {
        id: keyboardContainer
        implicitHeight: Math.min(mainColumn.implicitHeight * root.dialogScale,
                                 root.parent ? root.parent.height * 0.9 : mainColumn.implicitHeight * root.dialogScale)
        implicitWidth: Theme.scaled(520) * root.dialogScale
        inOverlay: true
        textFields: [
            profileInput.textField,
            grindInput.textField, rpmInput
        ]
        targetFlickable: brewFlickable

        Flickable {
            id: brewFlickable
            anchors.fill: parent
            contentHeight: mainColumn.implicitHeight * root.dialogScale
                           + keyboardContainer.estimatedKeyboardHeight
            contentWidth: parent.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: mainColumn
            width: Theme.scaled(520)
            scale: root.dialogScale
            transformOrigin: Item.TopLeft
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
                text: TranslationManager.translate("brewDialog.title", "Brew Settings")
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.ignored: true  // Dialog title announced on open
            }

            HideKeyboardButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(8)
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        // Profile selector
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(12)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.profileLabel", "Profile:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Theme.scaled(75)
                Accessible.ignored: true
            }

            SuggestionField {
                id: profileInput
                Layout.fillWidth: true
                label: ""
                accessibleName: TranslationManager.translate("brewDialog.profile", "Profile")
                text: root.selectedProfileTitle
                suggestions: root.getProfileSuggestions()
                onTextEdited: function(t) {
                    root.selectedProfileTitle = t
                    // Only load profile on exact match (e.g. suggestion selection),
                    // not partial typing that might accidentally match a short title
                    if (root.getProfileSuggestions().indexOf(t) >= 0)
                        root.loadProfileByTitle(t)
                }
            }
        }

        // Beans: read-only summary of the active bag + Change Beans
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(8)
            spacing: Theme.scaled(4)

            Text {
                text: TranslationManager.translate("brewDialog.beansLabel", "Beans:")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: Theme.scaled(75)
                Accessible.ignored: true
            }

            BeanSummary {
                id: brewBeanSummary
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }

            AccessibleButton {
                Layout.preferredHeight: Theme.scaled(44)
                Layout.alignment: Qt.AlignVCenter
                text: brewBeanSummary.hasBeans
                    ? TranslationManager.translate("beans.button.change", "Change Beans")
                    : TranslationManager.translate("beans.button.select", "Select Beans")
                accessibleName: TranslationManager.translate("beans.button.accessible.change", "Change the selected beans")
                onClicked: brewChangeBeansDialog.open()
            }

            ChangeBeansDialog {
                id: brewChangeBeansDialog
                context: "brew"
            }
        }

        // Content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(20)
            Layout.topMargin: Theme.scaled(8)
            spacing: Theme.scaled(8)

            // Low dose warning
            Rectangle {
                Layout.fillWidth: true
                visible: root.lowDoseWarning
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.warningColor
                radius: Theme.scaled(8)
                implicitHeight: warningText.implicitHeight + Theme.scaled(24)

                // Accessibility: announce warning and make it focusable
                Accessible.role: Accessible.AlertMessage
                Accessible.name: warningText.text
                Accessible.focusable: true

                // Announce warning when it becomes visible
                onVisibleChanged: {
                    if (visible && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                        AccessibilityManager.announce(TranslationManager.translate("brewDialog.warningPrefix", "Warning: ") + warningText.text)
                    }
                }

                Text {
                    id: warningText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(12)
                    text: TranslationManager.translate("brewDialog.scaleWarning", "Please put the portafilter with coffee on the scale")
                    font: Theme.bodyFont
                    color: Theme.warningColor
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true  // Parent handles accessibility
                }
            }

            // Temperature input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: TranslationManager.translate("brewDialog.tempLabel", "Temp:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Theme.scaled(75)
                        Accessible.ignored: true  // Label for sighted users; input has accessibleName
                    }

                    ValueInput {
                        id: tempInput
                        Layout.fillWidth: true
                        value: root.temperatureValue
                        from: 70
                        to: 100
                        stepSize: 1
                        decimals: 0
                        suffix: "°C"
                        valueColor: Math.abs(root.temperatureValue - root.profileTemperature) > 0.1 ? Theme.temperatureColor : Theme.textSecondaryColor
                        accentColor: Theme.temperatureColor
                        accessibleName: TranslationManager.translate("brewDialog.brewTemperature", "Brew temperature")
                        onValueModified: function(newValue) {
                            root.temperatureValue = newValue
                        }
                    }

                    // Save to profile button
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(44)
                        text: TranslationManager.translate("brewDialog.updateProfile", "Update Profile")
                        accessibleName: TranslationManager.translate("brewDialog.saveTemperatureToProfile", "Save temperature to profile")
                        primary: true
                        enabled: Math.abs(root.temperatureValue - root.profileTemperature) > 0.1
                        onClicked: {
                            var profile = ProfileManager.getCurrentProfile()
                            if (profile && profile.steps.length > 0) {
                                var delta = root.temperatureValue - profile.steps[0].temperature
                                for (var i = 0; i < profile.steps.length; i++) {
                                    profile.steps[i].temperature += delta
                                }
                                profile.espresso_temperature = root.temperatureValue
                                ProfileManager.uploadProfile(profile)
                            }
                            root.profileTemperature = root.temperatureValue
                            if (ProfileManager.baseProfileName.length > 0) {
                                ProfileManager.saveProfile(ProfileManager.baseProfileName)
                            }
                        }
                    }
                }

                // Visual indicator showing profile default
                Text {
                    visible: Math.abs(root.temperatureValue - root.profileTemperature) > 0.1
                    text: TranslationManager.translate("brewDialog.profileTempIndicator", "Profile: %1°C").arg(root.profileTemperature.toFixed(1))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("brewDialog.profileDefaultTemp", "Profile default temperature: %1 degrees").arg(root.profileTemperature.toFixed(1))
                }
            }

            // Dose input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.doseLabel", "Dose:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.scaled(75)
                    Accessible.ignored: true  // Label for sighted users; input has accessibleName
                }

                ValueInput {
                    id: doseInput
                    Layout.fillWidth: true
                    value: root.doseValue
                    from: 1
                    to: 50
                    stepSize: 0.1
                    decimals: 1
                    suffix: "g"
                    valueColor: Theme.weightColor
                    accentColor: Theme.weightColor
                    accessibleName: TranslationManager.translate("brewDialog.doseWeight", "Dose weight")
                    onValueModified: function(newValue) {
                        root.targetManuallySet = false  // Reset manual flag when dose changes
                        root.doseValue = newValue
                        if (newValue >= 3) {
                            root.showScaleWarning = false
                        }
                    }
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(44)
                    text: TranslationManager.translate("brewDialog.getFromScale", "Get from scale")
                    accessibleName: TranslationManager.translate("brewDialog.getDoseFromScale", "Get dose from scale")
                    primary: true
                    onClicked: {
                        var scaleWeight = MachineState.scaleWeight
                        if (scaleWeight >= 3) {
                            root.showScaleWarning = false
                            root.targetManuallySet = false  // Reset manual flag
                            root.doseValue = scaleWeight
                        } else {
                            // Show warning but don't change dose
                            root.showScaleWarning = true
                        }
                    }
                }
            }

            // Profile recommended dose indicator
            Text {
                visible: ProfileManager.profileHasRecommendedDose && Math.abs(root.doseValue - ProfileManager.profileRecommendedDose) > 0.05
                text: TranslationManager.translate("brewDialog.profileDoseIndicator", "Profile: %1g").arg(ProfileManager.profileRecommendedDose.toFixed(1))
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(11)
                font.italic: true
                color: Theme.textSecondaryColor
                Layout.alignment: Qt.AlignHCenter
                Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("brewDialog.profileRecommendedDose", "Profile recommended dose: %1 grams").arg(ProfileManager.profileRecommendedDose.toFixed(1))
            }

            // Ratio input
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("brewDialog.ratioLabel", "Ratio: 1:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.preferredWidth: Theme.scaled(75)
                    Accessible.ignored: true  // Label for sighted users; input has accessibleName
                }

                ValueInput {
                    id: ratioInput
                    Layout.fillWidth: true
                    value: root.ratio
                    from: 0.5
                    to: 20.0
                    stepSize: 0.1
                    decimals: 1
                    valueColor: Theme.primaryColor
                    accentColor: Theme.primaryColor
                    accessibleName: TranslationManager.translate("brewDialog.brewRatio", "Brew ratio")
                    onValueModified: function(newValue) {
                        root.targetManuallySet = false  // Reset manual flag when ratio changes
                        root.ratio = newValue
                    }
                }
            }

            // Yield input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: TranslationManager.translate("brewDialog.stopAtWeightLabel", "Stop at:")
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Theme.scaled(75)
                        Accessible.ignored: true  // Label for sighted users; input has accessibleName
                    }

                    ValueInput {
                        id: targetInput
                        Layout.fillWidth: true
                        value: root.targetValue
                        from: 1
                        to: 500
                        stepSize: 1
                        decimals: 0
                        suffix: "g"
                        // Color changes based on whether value is auto-calculated or manually set
                        valueColor: root.targetManuallySet ? Theme.primaryColor : Theme.weightColor
                        accentColor: root.targetManuallySet ? Theme.primaryColor : Theme.weightColor
                        accessibleName: TranslationManager.translate("brewDialog.stopAtWeight", "Stop at weight") + (root.targetManuallySet ? TranslationManager.translate("brewDialog.manual", " (manual)") : TranslationManager.translate("brewDialog.calculated", " (calculated)"))
                        onValueModified: function(newValue) {
                            root.targetManuallySet = true  // Mark as manually set
                            root.targetValue = newValue
                            // Update ratio to match (yield / dose)
                            if (root.doseValue > 0) {
                                root.ratio = newValue / root.doseValue
                            }
                        }
                    }

                    // Save to profile button
                    AccessibleButton {
                        Layout.preferredHeight: Theme.scaled(44)
                        text: TranslationManager.translate("brewDialog.updateProfile", "Update Profile")
                        accessibleName: TranslationManager.translate("brewDialog.saveStopWeightToProfile", "Save stop-at-weight to profile")
                        primary: true
                        enabled: root.targetValue !== root.profileTargetWeight
                        onClicked: {
                            var profile = ProfileManager.getCurrentProfile()
                            if (profile) {
                                profile.target_weight = root.targetValue
                                ProfileManager.uploadProfile(profile)
                            }
                            root.profileTargetWeight = root.targetValue
                            if (ProfileManager.baseProfileName.length > 0) {
                                ProfileManager.saveProfile(ProfileManager.baseProfileName)
                            }
                        }
                    }
                }

                // Visual indicator showing profile default
                Text {
                    visible: Math.abs(root.targetValue - root.profileTargetWeight) > 0.1
                    text: TranslationManager.translate("brewDialog.profileDefault", "Profile: %1g").arg(root.profileTargetWeight.toFixed(0))
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    font.italic: true
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: Theme.scaled(75) + Theme.scaled(8)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("brewDialog.profileDefaultStopWeight", "Profile default stop-at-weight: %1 grams").arg(root.profileTargetWeight.toFixed(0))
                }
            }

            // Equipment package (read-only) + Switch Equipment button. Identity
            // is managed in the Equipment window / Switch dialog, not edited here.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Text {
                    text: TranslationManager.translate("brewDialog.equipmentLabel", "Equipment:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.scaled(75)
                    Accessible.ignored: true
                }

                Text {
                    Layout.fillWidth: true
                    text: root.equipmentLabel.length > 0
                          ? root.equipmentLabel
                          : TranslationManager.translate("brewDialog.equipmentNotSet", "Not set")
                    font: Theme.bodyFont
                    color: root.equipmentLabel.length > 0 ? Theme.textColor : Theme.textSecondaryColor
                    elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("brewDialog.equipmentAccessible", "Equipment: %1").arg(
                        root.equipmentLabel.length > 0 ? root.equipmentLabel
                        : TranslationManager.translate("brewDialog.equipmentNotSet", "Not set"))
                }

                // Info: show the active package's full contents.
                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(40)
                    visible: Settings.dye.activeEquipmentId > 0
                    icon.source: "qrc:/icons/info.svg"
                    accessibleName: TranslationManager.translate("equipment.info.button", "Equipment details")
                    onClicked: equipmentInfoDialog.openFor(Settings.dye.activeEquipmentId)
                }

                AccessibleButton {
                    Layout.preferredHeight: Theme.scaled(40)
                    text: root.equipmentLabel.length > 0
                          ? TranslationManager.translate("brewDialog.switchEquipment", "Switch")
                          : TranslationManager.translate("brewDialog.addEquipment", "Add")
                    accessibleName: TranslationManager.translate("brewDialog.switchEquipmentAccessible", "Switch equipment package")
                    onClicked: switchEquipmentDialog.openPicker()
                }
            }

            // Grind setting (+ rpm when the grinder is rpm-adjustable) — dial-in
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Text {
                    text: TranslationManager.translate("brewDialog.grindLabel", "Grind:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Theme.scaled(75)
                    Accessible.ignored: true
                }

                SuggestionField {
                    id: grindInput
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.scaled(110)
                    label: ""
                    accessibleName: TranslationManager.translate("brewDialog.grinderSetting", "Grinder setting")
                    text: root.grindSetting
                    suggestions: _distinctCacheVersion >= 0 ? root.getGrinderSettingSuggestions() : []
                    onTextEdited: function(t) { root.grindSetting = t }
                }

                Text {
                    visible: root.equipmentRpmCapable
                    text: TranslationManager.translate("brewDialog.rpmLabel", "RPM:")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }

                StyledTextField {
                    id: rpmInput
                    visible: root.equipmentRpmCapable
                    Layout.preferredWidth: Theme.scaled(80)
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: root.grindRpm > 0 ? String(root.grindRpm) : ""
                    Accessible.name: TranslationManager.translate("brewDialog.rpmAccessible", "Grinder rpm")
                    onTextEdited: root.grindRpm = parseInt(text) || 0
                }
            }
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            spacing: Theme.scaled(10)

            // Clear All button
            AccessibleButton {
                Layout.preferredHeight: Theme.scaled(50)
                text: TranslationManager.translate("brewDialog.clear", "Clear")
                accessibleName: TranslationManager.translate("brewDialog.clearAllOverrides", "Clear all overrides")
                onClicked: {
                    // Reset to current profile and bean preset values (not cached values from dialog open)
                    root.profileTemperature = ProfileManager.profileTargetTemperature
                    root.temperatureValue = root.profileTemperature
                    root.profileTargetWeight = ProfileManager.profileTargetWeight

                    // Use the active bag's dose if available, otherwise default 18g
                    root.doseValue = Settings.dye.dyeBeanWeight > 0 ? Settings.dye.dyeBeanWeight : 18.0
                    root.selectedProfileTitle = ProfileManager.currentProfileName
                    root.grindSetting = Settings.dye.dyeGrinderSetting
                    root.grindRpm = Settings.dye.dyeGrinderRpm

                    // Calculate ratio from profile target weight / dose
                    var profileTarget = ProfileManager.profileTargetWeight
                    root.ratio = (profileTarget > 0 && root.doseValue > 0) ? profileTarget / root.doseValue : Settings.brew.lastUsedRatio
                    root.targetManuallySet = false
                    root.targetValue = root.doseValue * root.ratio
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
                    radius: Theme.buttonRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.warningColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: Theme.warningColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                text: TranslationManager.translate("brewDialog.cancel", "Cancel")
                accessibleName: TranslationManager.translate("brewDialog.cancelBrewSettings", "Cancel brew settings")
                onClicked: root.reject()
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
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
                text: TranslationManager.translate("brewDialog.ok", "OK")
                accessibleName: TranslationManager.translate("brewDialog.confirmBrewSettings", "Confirm brew settings")
                onClicked: {
                    Qt.inputMethod.commit()
                    Settings.brew.lastUsedRatio = root.ratio
                    // Grinder identity is managed via Switch Equipment; only the
                    // dial-in (grind setting + rpm) is saved from here.
                    Settings.dye.dyeGrinderSetting = root.grindSetting
                    Settings.dye.dyeGrinderRpm = root.grindRpm
                    // Use the new activateBrewWithOverrides method
                    ProfileManager.activateBrewWithOverrides(
                        root.doseValue,
                        root.targetValue,
                        root.temperatureValue,
                        root.grindSetting
                    )
                    root.accept()
                }
                background: Rectangle {
                    implicitHeight: Theme.scaled(50)
                    radius: Theme.buttonRadius
                    color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.bodyFont
                    color: Theme.primaryContrastColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        } // ColumnLayout (mainColumn)
        } // Flickable
    } // KeyboardAwareContainer (contentItem)
}
