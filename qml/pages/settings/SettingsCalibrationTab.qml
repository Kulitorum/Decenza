// Bound so the Sensor Calibration Repeater's delegate can reach this file's root
// id. Safe here because the file has exactly ONE delegate and it already declares
// every model role it reads as a required property (`index`) — which is the
// condition CLAUDE.md attaches to this pragma. Re-check that if another Repeater
// is ever added to this file.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Item {
    id: calibrationTab

    // Carries the sensor index into the SensorCalibration table. Relayed by
    // SettingsPage to AppShell — a settings tab never touches pageStack.
    signal openSensorCalibration(int sensor)

    Flickable {
        id: calibrationFlickable
        anchors.fill: parent
        contentHeight: calibrationLayout.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        RowLayout {
            id: calibrationLayout
            width: parent.width
            spacing: Theme.scaled(15)

            // ========== LEFT COLUMN ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Flow Calibration
                Rectangle {
                    objectName: "flowCalibration"
                    Layout.fillWidth: true
                    implicitHeight: flowCalContent.implicitHeight + Theme.scaled(20)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: flowCalContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(6)

                        Text {
                            text: TranslationManager.translate("settings.preferences.flowCalibration", "Flow Calibration")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.autoCalibration.description", "Auto calibration — learns from your scale after each shot")
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.calibration.autoFlowCalibration
                                accessibleName: TranslationManager.translate("settings.preferences.autoCalibration", "Auto calibration")
                                onToggled: Settings.calibration.autoFlowCalibration = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            property int _calVersion: Settings.calibration.perProfileFlowCalVersion
                            property double effectiveCal: {
                                void(_calVersion);
                                void(Settings.calibration.autoFlowCalibration);
                                void(Settings.calibration.flowCalibrationMultiplier);
                                return Settings.calibration.effectiveFlowCalibration(ProfileManager.baseProfileName);
                            }
                            property bool isPerProfile: {
                                void(_calVersion);
                                void(Settings.calibration.autoFlowCalibration);
                                void(Settings.calibration.flowCalibrationMultiplier);
                                return Settings.calibration.hasProfileFlowCalibration(ProfileManager.baseProfileName);
                            }

                            Text {
                                property string calSuffix: {
                                    if (!Settings.calibration.autoFlowCalibration) return "";
                                    if (parent.isPerProfile)
                                        return " " + TranslationManager.translate("settings.preferences.calAuto", "(auto)");
                                    return " " + TranslationManager.translate("settings.preferences.calGlobal", "(global)");
                                }
                                text: TranslationManager.translate("settings.preferences.currentMultiplier", "Current:") + " " + parent.effectiveCal.toFixed(2) + calSuffix
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            AccessibleButton {
                                visible: parent.isPerProfile
                                accessibleName: TranslationManager.translate("settings.preferences.resetAutoCal", "Reset auto calibration for current profile")
                                text: TranslationManager.translate("settings.preferences.reset", "Reset")
                                onClicked: Settings.calibration.clearProfileFlowCalibration(ProfileManager.baseProfileName)
                            }

                            AccessibleButton {
                                accessibleName: TranslationManager.translate("settings.preferences.openFlowCalibration", "Open Flow Calibration")
                                text: TranslationManager.translate("settings.preferences.calibrate", "Calibrate")
                                primary: true
                                enabled: !Settings.calibration.autoFlowCalibration
                                onClicked: AppShell.flowCalibrationRequested()
                            }
                        }
                    }
                }

                // Weight Stop Timing (was Stop-at-Weight Calibration)
                Rectangle {
                    objectName: "weightStopTiming"
                    Layout.fillWidth: true
                    implicitHeight: sawContent.implicitHeight + Theme.scaled(24)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: sawContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.calibration.weightStopTiming", "Weight Stop Timing")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                // sawLearnedLagFor() picks per-(profile, scale) data when the
                                // pair has graduated, otherwise falls back to global bootstrap
                                // / global pool / scale default. void(Settings.calibration.sawLearnedLag)
                                // makes the binding depend on sawLearnedLagChanged so that
                                // commits/rejects/resets trigger a re-evaluation.
                                property string _profile: ProfileManager.baseProfileName
                                // The scale SERVING, not the saved primary — this must match the
                                // key the shot engine learns under, or the tab shows one pool
                                // while shots train another (WiFi primary, BLE actually serving).
                                property string _scale: MachineState.activeScaleType
                                property double _lagDep: Settings.calibration.sawLearnedLag
                                text: { void(_lagDep); void(sawSourceRow._basketDep);
                                    return Settings.calibration.sawLearnedLagFor(_profile, _scale).toFixed(2)
                                      + TranslationManager.translate("common.unit.seconds", "s"); }
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }
                        }

                        RowLayout {
                            id: sawSourceRow
                            Layout.fillWidth: true
                            property double _modelDep: Settings.calibration.sawLearnedLag  // dep tracker for rebind
                            // Second dep tracker, for the BASKET. sawLearnedLagChanged fires on
                            // calibration writes and resets — never on an equipment-package
                            // switch — so without this the label named the NEW basket beside the
                            // old basket's lag and tier, which is worse than saying nothing.
                            property string _basketDep: Settings.dye.dyeBasketBrand + "|"
                                                        + Settings.dye.dyeBasketModel
                            property string _modelSource: { void(_modelDep); void(_basketDep);
                                return Settings.calibration.sawModelSource(ProfileManager.baseProfileName, MachineState.activeScaleType); }
                            // The number is keyed on (profile, scale, basket), so the card names all
                            // three — otherwise switching any one of them changes the lag on screen
                            // with nothing to explain why. The profile was missing even before the
                            // basket existed, which made the per-profile tier unreadable: the label
                            // said "Decent Scale (per-profile)" without saying WHICH profile.
                            //
                            // currentProfileTitle, not baseProfileName: the title is the label a user
                            // recognises, the filename is the key. (Not currentProfileName either —
                            // that one prefixes "*" once the profile is edited.)
                            property string _profileLabel: {
                                var p = (ProfileManager.currentProfileTitle || "").trim();
                                return p === "" ? "" : p + " · ";
                            }
                            property string _basketLabel: {
                                var b = (Settings.dye.dyeBasketBrand + " " + Settings.dye.dyeBasketModel).trim();
                                return b === "" ? "" : " + " + b;
                            }
                            // void(_modelDep) first, like _modelSource above: this is a
                            // Q_INVOKABLE, so a binding that just calls it records no dependency
                            // and the button would stay visible after its own reset.
                            property bool _hasProfileData: { void(_modelDep); void(_basketDep);
                                return Settings.calibration.hasSawLearningForProfile(
                                    ProfileManager.baseProfileName, MachineState.activeScaleType); }
                            property string _sourceSuffix: {
                                if (_modelSource === "perProfile")
                                    return " " + TranslationManager.translate("settings.preferences.sawPerProfile", "(per-profile)");
                                if (_modelSource === "globalBootstrap")
                                    return " " + TranslationManager.translate("settings.preferences.sawBootstrap", "(global bootstrap)");
                                if (_modelSource === "globalPool")
                                    return " " + TranslationManager.translate("settings.preferences.sawGlobal", "(global)");
                                return " " + TranslationManager.translate("settings.preferences.sawDefault", "(default)");
                            }

                            Text {
                                // Show the human-readable scale name, not scaleType — the latter
                                // is now a canonical id ("decent", "bookoo"), not a display label.
                                // Same for the basket: the model is keyed on a normalized slug, so
                                // render the package's own brand/model instead.
                                text: sawSourceRow._profileLabel
                                      + (MachineState.activeScaleName || TranslationManager.translate("settings.options.none", "none"))
                                      + sawSourceRow._basketLabel
                                      + sawSourceRow._sourceSuffix
                                      + " · "
                                      + TranslationManager.translate("settings.options.autoLearns", "learns when to stop so your cup hits target weight")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }

                            Item { Layout.fillWidth: true }

                            // Two scopes. Visibility on the scoped one keys on whether there is
                            // DATA to clear, not on which tier is winning: a bucket can hold
                            // medians while the bootstrap or the global pool outranks it, and
                            // gating on the tier hid it exactly then — leaving the full wipe as
                            // the only button on the card.
                            Text {
                                id: resetThisProfileText
                                visible: sawSourceRow._hasProfileData
                                text: TranslationManager.translate("settings.options.resetThisProfileAllBaskets", "Reset profile")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    accessibleName: TranslationManager.translate("settings.calibration.resetWeightStopTimingProfileAllBaskets", "Reset weight stop timing for current profile, every basket")
                                    accessibleItem: resetThisProfileText
                                    onAccessibleClicked: Settings.calibration.resetSawLearningForProfile(ProfileManager.baseProfileName, MachineState.activeScaleType)
                                }
                            }

                            Text {
                                id: resetAllText
                                text: TranslationManager.translate("settings.options.resetAll", "Reset all")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    accessibleName: TranslationManager.translate("settings.calibration.resetWeightStopTimingAll", "Reset all weight stop timing")
                                    accessibleItem: resetAllText
                                    // Confirmed, unlike the scoped reset: this one discards
                                    // months of learning for EVERY profile and scale the user owns,
                                    // and it used to fire on a single tap.
                                    onAccessibleClicked: resetAllSawConfirmDialog.open()
                                }
                            }
                        }
                    }
                }

                // Heater Calibration Card
                Rectangle {
                    objectName: "heaterCalibration"
                    Layout.fillWidth: true
                    implicitHeight: calibrateContent.implicitHeight + Theme.scaled(24)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: calibrateContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.calibration.title", "Heater Calibration")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                id: calibrateText
                                text: TranslationManager.translate("settings.calibration.calibrate", "Calibrate...")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    accessibleName: TranslationManager.translate("settings.calibration.openCalibration", "Open heater calibration")
                                    accessibleItem: calibrateText
                                    onAccessibleClicked: calibrationWarningDialog.open()
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.calibration.description", "Configure steam heater warm-up behavior for consistent temperature")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Sensor Calibration — one row per sensor, both derived from the
                // SensorCalibration table rather than written out here, so a sensor's
                // label, required instrument and test profile have exactly one
                // definition (src/controllers/sensorcalibrationcontroller.cpp).
                //
                // Two rows rather than one combined entry on purpose: the sensors need
                // different equipment and a user commonly owns one and not the other,
                // so each names its instrument on the card — visible without opening
                // anything. Same row format as the Maintenance card's operations
                // (SettingsActionRow), which is the shape a guided full-screen
                // operation already has in this app.
                Rectangle {
                    objectName: "sensorCalibration"
                    Layout.fillWidth: true
                    implicitHeight: sensorCalContent.implicitHeight + Theme.scaled(24)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: sensorCalContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Text {
                            text: TranslationManager.translate("settings.sensorCalibration.title", "Sensor Calibration")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.sensorCalibration.description",
                                                               "Correct what the machine reads against an external gauge or thermometer")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: SensorCalibration.sensorCount()

                            delegate: SettingsActionRow {
                                // Declared because a delegate with any required property
                                // stops receiving model roles as context properties.
                                required property int index

                                // label()/instrumentText() are C++ invokables that
                                // translate internally, so a binding on them records
                                // no dependency on TranslationManager and would
                                // freeze on a language change. See the same note in
                                // SensorCalibrationPage.qml.
                                readonly property int trVersion: TranslationManager.translationVersion

                                emoji: "🎯"
                                title: { void(trVersion); return SensorCalibration.label(index) }
                                description: { void(trVersion); return SensorCalibration.instrumentText(index) }
                                actionEnabled: DE1Device.connected
                                disabledReason: TranslationManager.translate(
                                    "settings.sensorCalibration.needsMachine",
                                    "Connect your machine to calibrate")
                                onTriggered: calibrationTab.openSensorCalibration(index)
                            }
                        }
                    }
                }
            }

            // ========== RIGHT COLUMN ==========
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(350)
                Layout.alignment: Qt.AlignTop
                spacing: Theme.scaled(15)

                // Virtual Scale (FlowScale)
                Rectangle {
                    objectName: "virtualScale"
                    Layout.fillWidth: true
                    implicitHeight: flowScaleContent.implicitHeight + Theme.scaled(30)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: flowScaleContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.scaled(10)

                        Text {
                            text: TranslationManager.translate("settings.preferences.virtualScale", "Virtual Scale")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.preferences.virtualScaleDesc",
                                  "Estimate cup weight from the machine's flow sensor when no Bluetooth scale is connected. Accuracy depends on flow calibration.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.preferences.useVirtualScale", "Enable virtual scale")
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.useFlowScale
                                accessibleName: TranslationManager.translate("settings.preferences.useVirtualScale", "Enable virtual scale")
                                onClicked: Settings.useFlowScale = checked
                            }
                        }
                    }
                }

                // Prefer Weight over Volume (was Ignore Stop-at-Volume with Scale)
                Rectangle {
                    objectName: "preferWeight"
                    Layout.fillWidth: true
                    implicitHeight: ignoreVolumeContent.implicitHeight + Theme.scaled(30)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: ignoreVolumeContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.spacingSmall

                        Text {
                            text: TranslationManager.translate("settings.calibration.preferWeightOverVolume", "Prefer weight over volume")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: TranslationManager.translate("settings.calibration.preferWeightOverVolumeDesc",
                                    "When a Bluetooth scale is paired, stop by weight only instead of weight and volume")
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondaryColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                            }

                            StyledSwitch {
                                checked: Settings.brew.ignoreVolumeWithScale
                                accessibleName: TranslationManager.translate("settings.calibration.preferWeightOverVolume", "Prefer weight over volume")
                                onToggled: Settings.brew.ignoreVolumeWithScale = checked
                            }
                        }
                    }
                }

                // Steam Health Monitor
                Rectangle {
                    objectName: "steamHealth"
                    Layout.fillWidth: true
                    implicitHeight: steamHealthContent.implicitHeight + Theme.scaled(30)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: steamHealthContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.spacingSmall

                        Text {
                            text: TranslationManager.translate("settings.calibration.steamHealth", "Steam Health")
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.calibration.steamHealthDesc",
                                "Rising pressure or temperature over time can indicate milk residue or scale buildup. Try cleaning your steam wand with a milk cleaner first; descale if the issue persists. The bars show drift from your clean-machine baseline toward the warning level.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }

                        // Status: establishing baseline (fresh install or post-reset).
                        // SteamHealthTracker.baselineState drives the wording so the
                        // user always sees what's happening — never a silent empty
                        // panel. Covers the Empty / EstablishingInitial /
                        // EstablishingAfterReset states; hidden once Ready.
                        Text {
                            visible: !SteamHealthTracker.hasData
                            Layout.fillWidth: true
                            text: {
                                var state = SteamHealthTracker.baselineState
                                var count = SteamHealthTracker.sessionCount
                                var total = SteamHealthTracker.minSessionsForTrend
                                if (state === SteamHealthTracker.EstablishingAfterReset) {
                                    return TranslationManager.translate("settings.calibration.steamHealthEstablishingAfterReset",
                                        "Establishing new, improved baseline — we detected a significant pressure drop (likely a descale or steam-wand clean). Collecting %1 of %2 sessions to calibrate against your freshly-clean machine.")
                                        .arg(count).arg(total)
                                }
                                if (state === SteamHealthTracker.EstablishingInitial) {
                                    return TranslationManager.translate("settings.calibration.steamHealthEstablishingInitial",
                                        "Establishing baseline — %1 of %2 sessions collected. Steam your next drink as normal; trends will appear once we have enough data.")
                                        .arg(count).arg(total)
                                }
                                // Empty
                                return TranslationManager.translate("settings.calibration.steamHealthEmpty",
                                    "No steam sessions recorded yet. At least %1 sessions are needed before trend detection begins.")
                                    .arg(total)
                            }
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            font.italic: true
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }

                        // Pressure row
                        ColumnLayout {
                            visible: SteamHealthTracker.hasData
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium

                                Text {
                                    text: TranslationManager.translate("settings.calibration.steamPressure", "Pressure") +
                                          ": " + SteamHealthTracker.currentPressure.toFixed(1) + " bar"
                                    color: {
                                        var range = SteamHealthTracker.pressureThreshold - SteamHealthTracker.baselinePressure
                                        if (range <= 0) return Theme.textColor
                                        var progress = (SteamHealthTracker.currentPressure - SteamHealthTracker.baselinePressure) / range
                                        if (progress >= 0.6) return Theme.errorColor
                                        if (progress >= 0.3) return Theme.warningColor
                                        return Theme.textColor
                                    }
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.scaled(14)
                                    font.bold: true
                                    Accessible.ignored: true
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: SteamHealthTracker.baselinePressure.toFixed(1) + " — " +
                                          SteamHealthTracker.pressureThreshold.toFixed(1) + " bar"
                                    color: Theme.textSecondaryColor
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.scaled(11)
                                    Accessible.ignored: true
                                }
                            }

                            // Progress bar
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(6)
                                radius: Theme.scaled(3)
                                color: Theme.backgroundColor

                                Rectangle {
                                    width: {
                                        var range = SteamHealthTracker.pressureThreshold - SteamHealthTracker.baselinePressure
                                        if (range <= 0) return 0
                                        var progress = Math.max(0, Math.min(1,
                                            (SteamHealthTracker.currentPressure - SteamHealthTracker.baselinePressure) / range))
                                        return parent.width * progress
                                    }
                                    height: parent.height
                                    radius: Theme.scaled(3)
                                    color: {
                                        var range = SteamHealthTracker.pressureThreshold - SteamHealthTracker.baselinePressure
                                        if (range <= 0) return Theme.primaryColor
                                        var progress = (SteamHealthTracker.currentPressure - SteamHealthTracker.baselinePressure) / range
                                        if (progress >= 0.6) return Theme.errorColor
                                        if (progress >= 0.3) return Theme.warningColor
                                        return Theme.primaryColor
                                    }
                                }
                            }
                        }

                        // Temperature row
                        ColumnLayout {
                            visible: SteamHealthTracker.hasData
                            Layout.fillWidth: true
                            spacing: Theme.scaled(4)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingMedium

                                Text {
                                    text: TranslationManager.translate("settings.calibration.steamTemperature", "Temperature") +
                                          ": " + Theme.formatTemperature(SteamHealthTracker.currentTemperature, 0)
                                    color: {
                                        var range = SteamHealthTracker.temperatureThreshold - SteamHealthTracker.baselineTemperature
                                        if (range <= 0) return Theme.textColor
                                        var progress = (SteamHealthTracker.currentTemperature - SteamHealthTracker.baselineTemperature) / range
                                        if (progress >= 0.6) return Theme.errorColor
                                        if (progress >= 0.3) return Theme.warningColor
                                        return Theme.textColor
                                    }
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.scaled(14)
                                    font.bold: true
                                    Accessible.ignored: true
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: Theme.cToDisplay(SteamHealthTracker.baselineTemperature).toFixed(0) + " — " +
                                          Theme.formatTemperature(SteamHealthTracker.temperatureThreshold, 0)
                                    color: Theme.textSecondaryColor
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.scaled(11)
                                    Accessible.ignored: true
                                }
                            }

                            // Progress bar
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(6)
                                radius: Theme.scaled(3)
                                color: Theme.backgroundColor

                                Rectangle {
                                    width: {
                                        var range = SteamHealthTracker.temperatureThreshold - SteamHealthTracker.baselineTemperature
                                        if (range <= 0) return 0
                                        var progress = Math.max(0, Math.min(1,
                                            (SteamHealthTracker.currentTemperature - SteamHealthTracker.baselineTemperature) / range))
                                        return parent.width * progress
                                    }
                                    height: parent.height
                                    radius: Theme.scaled(3)
                                    color: {
                                        var range = SteamHealthTracker.temperatureThreshold - SteamHealthTracker.baselineTemperature
                                        if (range <= 0) return Theme.primaryColor
                                        var progress = (SteamHealthTracker.currentTemperature - SteamHealthTracker.baselineTemperature) / range
                                        if (progress >= 0.6) return Theme.errorColor
                                        if (progress >= 0.3) return Theme.warningColor
                                        return Theme.primaryColor
                                    }
                                }
                            }
                        }

                        // Session count + Reset button
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall

                            Text {
                                text: TranslationManager.translate("settings.calibration.steamSessions", "Sessions tracked") +
                                      ": " + SteamHealthTracker.sessionCount
                                color: Theme.textSecondaryColor
                                font.family: Theme.bodyFont.family
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                            }

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                id: resetBaselineBtn
                                visible: SteamHealthTracker.sessionCount > 0
                                Layout.preferredWidth: resetBaselineText.implicitWidth + Theme.spacingMedium * 2
                                Layout.preferredHeight: Theme.scaled(28)
                                radius: Theme.scaled(4)
                                color: resetBaselineMa.containsMouse ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"
                                border.color: Theme.textSecondaryColor
                                border.width: 1

                                Accessible.ignored: true

                                Text {
                                    id: resetBaselineText
                                    anchors.centerIn: parent
                                    text: TranslationManager.translate("settings.calibration.resetBaseline", "Reset Baseline")
                                    color: Theme.textColor
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.scaled(12)
                                    Accessible.ignored: true
                                }

                                AccessibleMouseArea {
                                    id: resetBaselineMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    accessibleName: TranslationManager.translate("settings.calibration.resetBaseline", "Reset Baseline")
                                    accessibleItem: resetBaselineBtn
                                    onAccessibleClicked: SteamHealthTracker.clearHistory()
                                }
                            }
                        }

                    }
                }
            }

            // Spacer
            Item { Layout.fillWidth: true }
        }
    }

    // Heater Calibration Warning Dialog
    // Confirmation for the full SAW wipe. Same shape as calibrationWarningDialog below —
    // this page's established confirm pattern rather than a fourth variant of one.
    DecenzaDialog {
        id: resetAllSawConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Dialog.CloseOnEscape

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                text: TranslationManager.translate("settings.calibration.resetAllSawTitle", "Reset all weight stop timing?")
                color: Theme.warningColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.calibration.resetAllSawMessage",
                    "This erases the learned stop timing for every profile, scale and basket — not just the one shown — and cannot be undone. Each combination needs about 3 shots to learn again. To clear only the profile shown, use Reset profile instead.")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.calibration.cancelResetAllSaw", "Keep weight stop timing")
                    onClicked: resetAllSawConfirmDialog.close()
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("settings.options.resetAll", "Reset all")
                    accessibleName: TranslationManager.translate("settings.calibration.confirmResetAllSaw", "Confirm resetting all weight stop timing")
                    onClicked: {
                        resetAllSawConfirmDialog.close()
                        Settings.calibration.resetSawLearning()
                    }
                }
            }
        }
    }

    DecenzaDialog {
        id: calibrationWarningDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Dialog.CloseOnEscape

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                text: TranslationManager.translate("settings.calibration.warningTitle", "Heater Calibration")
                color: Theme.warningColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.calibration.warningMessage", "Bad calibration settings might make your espresso machine unusable. Only proceed if you know what you are doing.")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.calibration.cancelWarning", "Cancel calibration")
                    onClicked: calibrationWarningDialog.close()
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("settings.calibration.proceed", "Proceed")
                    accessibleName: TranslationManager.translate("settings.calibration.proceedWarning", "Proceed to calibration")
                    onClicked: {
                        calibrationWarningDialog.close()
                        calibrationPopup.open()
                    }
                }
            }
        }
    }

    // Heater Calibration Popup
    DecenzaDialog {
        id: calibrationPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, Theme.scaled(500))
        height: Math.min(calibrationColumn.implicitHeight + 2 * padding, parent.height * 0.85)
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        onOpened: heaterIdleTempSlider.forceActiveFocus()

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Flickable {
            contentHeight: calibrationColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: calibrationColumn
                width: parent.width
                spacing: Theme.scaled(16)

                // Title
                Text {
                    text: TranslationManager.translate("settings.calibration.title", "Heater Calibration")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(18)
                    font.bold: true
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // Heater idle temperature
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); font: Theme.bodyFont; color: Theme.temperatureColor; wrapMode: Text.WordWrap }
                    // Slider mechanics stay in the internal scaled-integer space (value = \u00B0C \u00D7 10,
                    // 0\u2013990). Only the human-readable \u00B0C/\u00B0F label is unit-converted: value/10 is the
                    // Celsius reading, fed through cToDisplay; the stored scaled int is untouched.
                    ValueInput { id: heaterIdleTempSlider; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); from: 0; to: 990; stepSize: 5; displayText: Theme.cToDisplay(value / 10).toFixed(1) + Theme.tempUnitSuffix(); rangeText: Theme.formatTemperature(0, 1) + " \u2014 " + Theme.formatTemperature(99, 1); value: Settings.hardware.heaterIdleTemp; onValueModified: function(newValue) { Settings.hardware.heaterIdleTemp = Math.round(newValue) }; KeyNavigation.tab: heaterWarmupFlowSlider; KeyNavigation.backtab: doneButton }
                }

                // Heater warmup flow rate
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); font: Theme.bodyFont; color: Theme.flowColor; wrapMode: Text.WordWrap }
                    ValueInput { id: heaterWarmupFlowSlider; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); from: 5; to: 60; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 6.0 mL/s"; value: Settings.hardware.heaterWarmupFlow; onValueModified: function(newValue) { Settings.hardware.heaterWarmupFlow = Math.round(newValue) }; KeyNavigation.tab: heaterTestFlowSlider; KeyNavigation.backtab: heaterIdleTempSlider }
                }

                // Heater test flow rate
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); font: Theme.bodyFont; color: Theme.flowColor; wrapMode: Text.WordWrap }
                    ValueInput { id: heaterTestFlowSlider; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); from: 5; to: 80; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 8.0 mL/s"; value: Settings.hardware.heaterTestFlow; onValueModified: function(newValue) { Settings.hardware.heaterTestFlow = Math.round(newValue) }; KeyNavigation.tab: heaterTestTimeoutSlider; KeyNavigation.backtab: heaterWarmupFlowSlider }
                }

                // Heater test time-out
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); font: Theme.bodyFont; color: Theme.textSecondaryColor; wrapMode: Text.WordWrap }
                    ValueInput { id: heaterTestTimeoutSlider; accessibleName: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); from: 10; to: 300; stepSize: 1; displayText: (value / 10).toFixed(1) + " s"; rangeText: "1.0 — 30.0 s"; value: Settings.hardware.heaterWarmupTimeout; onValueModified: function(newValue) { Settings.hardware.heaterWarmupTimeout = Math.round(newValue) }; KeyNavigation.tab: fanThresholdSlider; KeyNavigation.backtab: heaterTestFlowSlider }
                }

                // Fan temperature threshold: 0 = "Always on" (fan runs continuously,
                // matching DE1 firmware default); 1–60 suppresses fan below that temp.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.fanThreshold", "Fan temperature threshold"); font: Theme.bodyFont; color: Theme.temperatureColor; wrapMode: Text.WordWrap }
                    // The slider value stays in Celsius: 0 is the "Always on" sentinel (0°C == 32°F,
                    // so a unit-converted from/to would make the sentinel unreachable in Fahrenheit).
                    // Only the displayed threshold label is unit-converted (cToDisplay(value)); the
                    // stored Celsius value and the 0 sentinel are untouched.
                    ValueInput { id: fanThresholdSlider; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("settings.calibration.fanThreshold", "Fan temperature threshold"); from: 0; to: 60; stepSize: 1; displayText: value === 0 ? TranslationManager.translate("settings.calibration.fanAlwaysOn", "Always on") : Theme.formatTemperature(value, 0); rangeText: TranslationManager.translate("settings.calibration.fanAlwaysOn", "Always on") + " — " + Theme.formatTemperature(60, 0); value: Settings.hardware.fanThreshold; onValueModified: function(newValue) { Settings.hardware.fanThreshold = Math.round(newValue) }; KeyNavigation.tab: heaterVoltage120; KeyNavigation.backtab: heaterTestTimeoutSlider }
                }

                // Nominal heater voltage. Deliberately in here rather than on the
                // Machine tab: it is set once at commissioning, almost never changed,
                // and getting it wrong runs the heater at the wrong duty — so it sits
                // behind this popup's destructive-change warning, among the other
                // expert parameters, and off any tab a user browses casually.
                //
                // NOT a Settings.hardware value. This is machine state read back over
                // MMR, which is why "Defaults for cafe" below leaves it alone.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.calibration.heaterVoltage", "Nominal heater voltage")
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            // Unknown stays unknown and preselects nothing — guessing
                            // here and being wrong is a heater at the wrong duty.
                            text: DE1Device.nominalHeaterVoltage === 0
                                  ? TranslationManager.translate("settings.calibration.heaterVoltageUnknown",
                                                                 "Your machine has not reported one")
                                  : TranslationManager.translate("settings.calibration.heaterVoltageReported",
                                                                 "Machine reports %1 V").arg(DE1Device.nominalHeaterVoltage)
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }
                    }

                    AccessibleButton {
                        id: heaterVoltage120
                        text: "120 V"
                        accessibleName: TranslationManager.translate("settings.calibration.heaterVoltageSet120", "Set nominal heater voltage to 120 volts")
                        primary: DE1Device.nominalHeaterVoltage === 120
                        enabled: DE1Device.connected
                        onClicked: DE1Device.setHeaterVoltage(120)
                        KeyNavigation.tab: heaterVoltage230
                        KeyNavigation.backtab: fanThresholdSlider
                    }

                    AccessibleButton {
                        id: heaterVoltage230
                        text: "230 V"
                        accessibleName: TranslationManager.translate("settings.calibration.heaterVoltageSet230", "Set nominal heater voltage to 230 volts")
                        primary: DE1Device.nominalHeaterVoltage === 230
                        enabled: DE1Device.connected
                        onClicked: DE1Device.setHeaterVoltage(230)
                        KeyNavigation.tab: defaultsButton
                        KeyNavigation.backtab: heaterVoltage120
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // Defaults for cafe button
                AccessibleButton {
                    id: defaultsButton
                    Layout.fillWidth: true
                    primary: true
                    text: TranslationManager.translate("settings.calibration.defaultsForCafe", "Defaults for cafe")
                    accessibleName: TranslationManager.translate("settings.calibration.defaultsForCafeAccessible", "Reset heater calibration to cafe defaults")
                    onClicked: {
                        Settings.hardware.heaterIdleTemp = 990
                        Settings.hardware.heaterWarmupFlow = 20
                        Settings.hardware.heaterTestFlow = 40
                        Settings.hardware.heaterWarmupTimeout = 10
                        Settings.hardware.fanThreshold = 60
                        // Cafe defaults force two-tap stop for safety: the first tap puts
                        // the firmware in Puffing, allowing the auto-flush timer to clear
                        // the wand reliably. Toggle UI lives in Steam Heater (Machine tab).
                        Settings.hardware.steamTwoTapStop = true
                    }
                    KeyNavigation.tab: doneButton
                    KeyNavigation.backtab: heaterVoltage230
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.calibration.cancelCalibration", "Cancel calibration")
                        onClicked: calibrationPopup.close()
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        id: doneButton
                        text: TranslationManager.translate("settings.calibration.done", "Done")
                        accessibleName: TranslationManager.translate("settings.calibration.closeCalibration", "Close heater calibration")
                        onClicked: calibrationPopup.close()
                        KeyNavigation.tab: heaterIdleTempSlider
                        KeyNavigation.backtab: defaultsButton
                    }
                }
            }
        }
    }

}
