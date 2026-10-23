// Bound so the Sensor Calibration Repeater's delegate can reach this file's root
// id. Safe here because the file has exactly ONE delegate and it already declares
// the delegate's `modelData` role as a required property — which is the
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
    property string pendingRequestId: ""
    property string operationError: ""

    function runOperation(operation, arguments) {
        if (pendingRequestId !== "") return
        operationError = ""
        pendingRequestId = CalibrationService.execute(operation, arguments)
        if (pendingRequestId === "")
            operationError = CalibrationService.errorMessage || TranslationManager.translate("common.operationStartFailed", "The operation could not be started")
    }

    function displayReason(reason: string): string {
        if (reason === "conflict" || reason === "contextChanged" || reason === "stalePreferences")
            return TranslationManager.translate("settings.calibration.conflict",
                "Calibration context changed elsewhere. Review the current settings and try again.")
        if (reason === "busy" || reason === "tooManyRequests")
            return TranslationManager.translate("settings.calibration.busy",
                "Another calibration operation is running. Try again shortly.")
        if (reason === "disconnected" || reason === "unavailable")
            return TranslationManager.translate("settings.calibration.unavailable",
                "Calibration settings are unavailable. Reconnect and try again.")
        if (reason === "invalidValue" || reason === "invalidArguments" || reason === "invalidRequestId")
            return TranslationManager.translate("settings.calibration.invalidValue",
                "The calibration value was not accepted. Review it and try again.")
        return TranslationManager.translate("settings.calibration.operationFailed",
            "The calibration setting could not be changed. Review the current value and try again.")
    }

    Connections {
        target: CalibrationService
        function onOperationCompleted(requestId, operation, success, result, reason, revision) {
            if (requestId === "" || requestId !== calibrationTab.pendingRequestId) return
            calibrationTab.pendingRequestId = ""
            calibrationTab.operationError = success ? "" : calibrationTab.displayReason(reason)
        }
    }

    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 2
        visible: calibrationTab.operationError !== ""
        text: calibrationTab.operationError
        color: Theme.errorColor
        wrapMode: Text.WordWrap
    }

    Component.onCompleted: CalibrationService.refresh()

    Flickable {
        id: calibrationFlickable
        anchors.fill: parent
        enabled: !CalibrationService.busy
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
                SettingsCard {
                    searchId: "flowCalibration"
                    title: TranslationManager.translate("settings.preferences.flowCalibration", "Flow Calibration")
                    description: TranslationManager.translate("settings.search.flowCalDesc", "Calibrate flow sensor accuracy")
                    keywords: ["flow", "calibration", "sensor", "auto", "multiplier"]
                    contentMargins: Theme.scaled(10)
                    spacing: Theme.scaled(6)

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
                            checked: CalibrationService.snapshot.autoFlowCalibration
                            accessibleName: TranslationManager.translate("settings.preferences.autoCalibration", "Auto calibration")
                            onToggled: calibrationTab.runOperation("setAutoFlowCalibration", { enabled: checked })
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        property int _calVersion: CalibrationService.snapshot.perProfileFlowCalVersion
                        property double effectiveCal: {
                            void(_calVersion);
                            void(CalibrationService.snapshot.autoFlowCalibration);
                            void(CalibrationService.snapshot.effectiveFlowCalibration);
                            return CalibrationService.snapshot.effectiveFlowCalibration;
                        }
                        property bool isPerProfile: {
                            void(_calVersion);
                            void(CalibrationService.snapshot.autoFlowCalibration);
                            void(CalibrationService.snapshot.effectiveFlowCalibration);
                            return CalibrationService.snapshot.hasProfileFlowCalibration;
                        }

                        Text {
                            property string calSuffix: {
                                if (!CalibrationService.snapshot.autoFlowCalibration) return "";
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
                            onClicked: calibrationTab.runOperation("clearProfileFlowCalibration", { filename: CalibrationService.snapshot.profileFilename })
                        }

                        AccessibleButton {
                            accessibleName: TranslationManager.translate("settings.preferences.openFlowCalibration", "Open Flow Calibration")
                            text: TranslationManager.translate("settings.preferences.calibrate", "Calibrate")
                            primary: true
                            enabled: !CalibrationService.snapshot.autoFlowCalibration
                            onClicked: AppShell.flowCalibrationRequested()
                        }
                    }
                }

                // Weight Stop Timing (was Stop-at-Weight Calibration)
                SettingsCard {
                    searchId: "weightStopTiming"
                    title: TranslationManager.translate("settings.calibration.weightStopTiming", "Weight Stop Timing")
                    description: TranslationManager.translate("settings.search.weightStopDesc", "Auto-learned stop-at-weight lag timing")
                    keywords: ["weight", "stop", "saw", "lag", "timing", "scale"]
                    showHeader: false
                    contentMargins: Theme.scaled(12)
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
                            // / global pool / scale default. void(CalibrationService.snapshot.sawLearnedLag)
                            // makes the binding depend on sawLearnedLagChanged so that
                            // commits/rejects/resets trigger a re-evaluation.
                            property string _profile: CalibrationService.snapshot.profileFilename
                            // The scale SERVING, not the saved primary — this must match the
                            // key the shot engine learns under, or the tab shows one pool
                            // while shots train another (WiFi primary, BLE actually serving).
                            property string _scale: CalibrationService.snapshot.activeScaleType
                            property double _lagDep: CalibrationService.snapshot.sawLearnedLag
                            text: { void(_lagDep); void(sawSourceRow._basketDep);
                                return CalibrationService.snapshot.sawLearnedLag.toFixed(2)
                                  + TranslationManager.translate("common.unit.seconds", "s"); }
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                        }
                    }

                    RowLayout {
                        id: sawSourceRow
                        Layout.fillWidth: true
                        property double _modelDep: CalibrationService.snapshot.sawLearnedLag  // dep tracker for rebind
                        // Second dep tracker, for the BASKET. sawLearnedLagChanged fires on
                        // calibration writes and resets — never on an equipment-package
                        // switch — so without this the label named the NEW basket beside the
                        // old basket's lag and tier, which is worse than saying nothing.
                        property string _basketDep: CalibrationService.snapshot.basketBrand + "|"
                                                    + CalibrationService.snapshot.basketModel
                        property string _modelSource: { void(_modelDep); void(_basketDep);
                            return CalibrationService.snapshot.sawModelSource; }
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
                            var p = (CalibrationService.snapshot.profileTitle || "").trim();
                            return p === "" ? "" : p + " · ";
                        }
                        property string _basketLabel: {
                            var b = (CalibrationService.snapshot.basketBrand + " " + CalibrationService.snapshot.basketModel).trim();
                            return b === "" ? "" : " + " + b;
                        }
                        // void(_modelDep) first, like _modelSource above: this is a
                        // Q_INVOKABLE, so a binding that just calls it records no dependency
                        // and the button would stay visible after its own reset.
                        property bool _hasProfileData: { void(_modelDep); void(_basketDep);
                            return CalibrationService.snapshot.hasSawLearningForProfile; }
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
                                  + (CalibrationService.snapshot.activeScaleName || TranslationManager.translate("settings.options.none", "none"))
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
                                onAccessibleClicked: calibrationTab.runOperation("resetSawLearningForProfile", { filename: CalibrationService.snapshot.profileFilename, scaleType: CalibrationService.snapshot.activeScaleType })
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

                // Heater Calibration Card
                SettingsCard {
                    searchId: "heaterCalibration"
                    title: TranslationManager.translate("settings.calibration.title", "Heater Calibration")
                    description: TranslationManager.translate("settings.search.heaterCalDesc", "Idle temp, warmup flow rates, timeout")
                    keywords: ["heater", "temperature", "warmup", "calibrate", "idle"]
                    showHeader: false
                    contentMargins: Theme.scaled(12)
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
                SettingsCard {
                    searchId: "sensorCalibration"
                    title: TranslationManager.translate("settings.sensorCalibration.title", "Sensor Calibration")
                    description: TranslationManager.translate("settings.sensorCalibration.description", "Correct what the machine reads against an external gauge or thermometer")
                    showDescription: true
                    keywords: ["sensor", "pressure", "temperature", "thermometer", "gauge", "offset", "calibrate", "accuracy"]
                    contentMargins: Theme.scaled(12)

                    Repeater {
                        SettingsSearch.title: TranslationManager.translate("settings.sensorCalibration.title", "Sensor Calibration")
                        model: SensorCalibrationService.sensors

                        delegate: SettingsActionRow {
                            // Keep the row's own sensor map. Looking it up again by
                            // index in the live service list can race a disconnect:
                            // the list clears before the repeater has removed its rows.
                            required property var modelData
                            readonly property var sensorData: modelData || ({})
                            readonly property int sensorIndex: Number(sensorData.sensor ?? -1)
                            readonly property bool sensorAvailable: sensorIndex >= 0
                                    && !!sensorData.labelKey && !!sensorData.instrumentKey

                            emoji: "🎯"
                            title: {
                                return TranslationManager.translate(
                                    sensorData.labelKey || "settings.sensorCalibration.unknownSensor",
                                    sensorData.labelFallback || "Sensor calibration")
                            }
                            description: {
                                return TranslationManager.translate(
                                    sensorData.instrumentKey || "settings.sensorCalibration.instrumentUnavailable",
                                    sensorData.instrumentFallback || "Required instrument information unavailable")
                            }
                            actionEnabled: SensorCalibrationService.ready
                                    && SensorCalibrationService.connected
                                    && sensorAvailable
                            disabledReason: TranslationManager.translate(
                                "settings.sensorCalibration.needsMachine",
                                "Connect your machine to calibrate")
                            onTriggered: {
                                if (!actionEnabled || !sensorAvailable) return
                                calibrationTab.openSensorCalibration(sensorIndex)
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
                SettingsCard {
                    searchId: "virtualScale"
                    title: TranslationManager.translate("settings.preferences.virtualScale", "Virtual Scale")
                    description: TranslationManager.translate("settings.search.virtualScaleDesc", "Estimate weight from flow sensor")
                    keywords: ["virtual", "scale", "flow", "weight", "estimate", "fallback"]
                    spacing: Theme.scaled(10)

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
                            checked: CalibrationService.snapshot.useFlowScale
                            accessibleName: TranslationManager.translate("settings.preferences.useVirtualScale", "Enable virtual scale")
                            onClicked: calibrationTab.runOperation("setUseFlowScale", { enabled: checked })
                        }
                    }
                }

                // Prefer Weight over Volume (was Ignore Stop-at-Volume with Scale)
                SettingsCard {
                    searchId: "preferWeight"
                    title: TranslationManager.translate("settings.calibration.preferWeightOverVolume", "Prefer weight over volume")
                    description: TranslationManager.translate("settings.search.preferWeightDesc", "Ignore volume limit when scale is paired")
                    keywords: ["weight", "volume", "sav", "ignore", "scale", "stop"]
                    spacing: Theme.spacingSmall

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
                            checked: CalibrationService.snapshot.ignoreVolumeWithScale
                            accessibleName: TranslationManager.translate("settings.calibration.preferWeightOverVolume", "Prefer weight over volume")
                            onToggled: calibrationTab.runOperation("setIgnoreVolumeWithScale", { enabled: checked })
                        }
                    }
                }

                // Steam Health Monitor
                SettingsCard {
                    searchId: "steamHealth"
                    title: TranslationManager.translate("settings.calibration.steamHealth", "Steam Health")
                    description: TranslationManager.translate("settings.search.steamHealthDesc", "Steam pressure and temperature drift from your clean-machine baseline")
                    keywords: ["steam", "health", "descale", "clean", "milk", "residue", "scale", "buildup", "drift", "baseline", "wand"]
                    spacing: Theme.spacingSmall

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
                    // CalibrationService.snapshot.steamBaselineState drives the wording so the
                    // user always sees what's happening — never a silent empty
                    // panel. Covers the Empty / EstablishingInitial /
                    // EstablishingAfterReset states; hidden once Ready.
                    Text {
                        visible: !CalibrationService.snapshot.steamHasData
                        Layout.fillWidth: true
                        text: {
                            var state = CalibrationService.snapshot.steamBaselineState
                            var count = CalibrationService.snapshot.steamSessionCount
                            var total = CalibrationService.snapshot.steamMinSessionsForTrend
                            if (state === 2) {
                                return TranslationManager.translate("settings.calibration.steamHealthEstablishingAfterReset",
                                    "Establishing new, improved baseline — we detected a significant pressure drop (likely a descale or steam-wand clean). Collecting %1 of %2 sessions to calibrate against your freshly-clean machine.")
                                    .arg(count).arg(total)
                            }
                            if (state === 1) {
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
                        visible: CalibrationService.snapshot.steamHasData
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium

                            Text {
                                text: TranslationManager.translate("settings.calibration.steamPressure", "Pressure") +
                                      ": " + CalibrationService.snapshot.steamCurrentPressure.toFixed(1) + " bar"
                                color: {
                                    var range = CalibrationService.snapshot.steamPressureThreshold - CalibrationService.snapshot.steamBaselinePressure
                                    if (range <= 0) return Theme.textColor
                                    var progress = (CalibrationService.snapshot.steamCurrentPressure - CalibrationService.snapshot.steamBaselinePressure) / range
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
                                text: CalibrationService.snapshot.steamBaselinePressure.toFixed(1) + " — " +
                                      CalibrationService.snapshot.steamPressureThreshold.toFixed(1) + " bar"
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
                                    var range = CalibrationService.snapshot.steamPressureThreshold - CalibrationService.snapshot.steamBaselinePressure
                                    if (range <= 0) return 0
                                    var progress = Math.max(0, Math.min(1,
                                        (CalibrationService.snapshot.steamCurrentPressure - CalibrationService.snapshot.steamBaselinePressure) / range))
                                    return parent.width * progress
                                }
                                height: parent.height
                                radius: Theme.scaled(3)
                                color: {
                                    var range = CalibrationService.snapshot.steamPressureThreshold - CalibrationService.snapshot.steamBaselinePressure
                                    if (range <= 0) return Theme.primaryColor
                                    var progress = (CalibrationService.snapshot.steamCurrentPressure - CalibrationService.snapshot.steamBaselinePressure) / range
                                    if (progress >= 0.6) return Theme.errorColor
                                    if (progress >= 0.3) return Theme.warningColor
                                    return Theme.primaryColor
                                }
                            }
                        }
                    }

                    // Temperature row
                    ColumnLayout {
                        visible: CalibrationService.snapshot.steamHasData
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium

                            Text {
                                text: TranslationManager.translate("settings.calibration.steamTemperature", "Temperature") +
                                      ": " + Theme.formatTemperature(CalibrationService.snapshot.steamCurrentTemperature, 0)
                                color: {
                                    var range = CalibrationService.snapshot.steamTemperatureThreshold - CalibrationService.snapshot.steamBaselineTemperature
                                    if (range <= 0) return Theme.textColor
                                    var progress = (CalibrationService.snapshot.steamCurrentTemperature - CalibrationService.snapshot.steamBaselineTemperature) / range
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
                                text: Theme.cToDisplay(CalibrationService.snapshot.steamBaselineTemperature).toFixed(0) + " — " +
                                      Theme.formatTemperature(CalibrationService.snapshot.steamTemperatureThreshold, 0)
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
                                    var range = CalibrationService.snapshot.steamTemperatureThreshold - CalibrationService.snapshot.steamBaselineTemperature
                                    if (range <= 0) return 0
                                    var progress = Math.max(0, Math.min(1,
                                        (CalibrationService.snapshot.steamCurrentTemperature - CalibrationService.snapshot.steamBaselineTemperature) / range))
                                    return parent.width * progress
                                }
                                height: parent.height
                                radius: Theme.scaled(3)
                                color: {
                                    var range = CalibrationService.snapshot.steamTemperatureThreshold - CalibrationService.snapshot.steamBaselineTemperature
                                    if (range <= 0) return Theme.primaryColor
                                    var progress = (CalibrationService.snapshot.steamCurrentTemperature - CalibrationService.snapshot.steamBaselineTemperature) / range
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
                                  ": " + CalibrationService.snapshot.steamSessionCount
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            Accessible.ignored: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            id: resetBaselineBtn
                            visible: CalibrationService.snapshot.steamSessionCount > 0
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
                                onAccessibleClicked: calibrationTab.runOperation("clearSteamHealthHistory", {})
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
                        calibrationTab.runOperation("resetSawLearning", {})
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
        // The scrolling parameters plus the pinned footer, capped at 85% of the
        // window. The footer is OUTSIDE the Flickable (see contentItem), so Done
        // stays reachable at any height rather than depending on the content
        // fitting.
        height: Math.min(calibrationColumn.implicitHeight + calibrationFooter.implicitHeight
                             + Theme.scaled(10) + 2 * padding,
                         parent.height * 0.85)
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

        // Parameters scroll; the actions do not.
        //
        // Everything used to live in one Flickable, so a sixth parameter row
        // pushed Cancel and Done below the fold — visible only if the user
        // thought to scroll a dialog that gives no sign it scrolls. Pinning the
        // footer makes "Done is reachable" independent of how many rows the
        // popup grows, which is what the layout spec's first scenario asks for.
        contentItem: ColumnLayout {
            spacing: Theme.scaled(10)

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: calibrationColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: calibrationColumn
                width: parent.width
                // Tightened from 16. Six parameter rows and a footer do not fit
                // at the old spacing on the 1280x800 reference.
                spacing: Theme.scaled(10)

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
                    ValueInput { id: heaterIdleTempSlider; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); from: 0; to: 990; stepSize: 5; displayText: Theme.cToDisplay(value / 10).toFixed(1) + Theme.tempUnitSuffix(); rangeText: Theme.formatTemperature(0, 1) + " \u2014 " + Theme.formatTemperature(99, 1); value: CalibrationService.snapshot.heaterIdleTemp; onValueModified: function(newValue) { calibrationTab.runOperation("setHeaterValue", { name: "heaterIdleTemp", value: Math.round(newValue) }) }; KeyNavigation.tab: heaterWarmupFlowSlider; KeyNavigation.backtab: doneButton }
                }

                // Heater warmup flow rate
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); font: Theme.bodyFont; color: Theme.flowColor; wrapMode: Text.WordWrap }
                    ValueInput { id: heaterWarmupFlowSlider; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); from: 5; to: 60; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 6.0 mL/s"; value: CalibrationService.snapshot.heaterWarmupFlow; onValueModified: function(newValue) { calibrationTab.runOperation("setHeaterValue", { name: "heaterWarmupFlow", value: Math.round(newValue) }) }; KeyNavigation.tab: heaterTestFlowSlider; KeyNavigation.backtab: heaterIdleTempSlider }
                }

                // Heater test flow rate
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); font: Theme.bodyFont; color: Theme.flowColor; wrapMode: Text.WordWrap }
                    ValueInput { id: heaterTestFlowSlider; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); from: 5; to: 80; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 8.0 mL/s"; value: CalibrationService.snapshot.heaterTestFlow; onValueModified: function(newValue) { calibrationTab.runOperation("setHeaterValue", { name: "heaterTestFlow", value: Math.round(newValue) }) }; KeyNavigation.tab: heaterTestTimeoutSlider; KeyNavigation.backtab: heaterWarmupFlowSlider }
                }

                // Heater test time-out
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    Text { Layout.fillWidth: true; text: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); font: Theme.bodyFont; color: Theme.textSecondaryColor; wrapMode: Text.WordWrap }
                    ValueInput { id: heaterTestTimeoutSlider; accessibleName: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); from: 10; to: 300; stepSize: 1; displayText: (value / 10).toFixed(1) + " s"; rangeText: "1.0 — 30.0 s"; value: CalibrationService.snapshot.heaterWarmupTimeout; onValueModified: function(newValue) { calibrationTab.runOperation("setHeaterValue", { name: "heaterWarmupTimeout", value: Math.round(newValue) }) }; KeyNavigation.tab: fanThresholdSlider; KeyNavigation.backtab: heaterTestFlowSlider }
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
                    ValueInput { id: fanThresholdSlider; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("settings.calibration.fanThreshold", "Fan temperature threshold"); from: 0; to: 60; stepSize: 1; displayText: value === 0 ? TranslationManager.translate("settings.calibration.fanAlwaysOn", "Always on") : Theme.formatTemperature(value, 0); rangeText: TranslationManager.translate("settings.calibration.fanAlwaysOn", "Always on") + " — " + Theme.formatTemperature(60, 0); value: CalibrationService.snapshot.fanThreshold; onValueModified: function(newValue) { calibrationTab.runOperation("setHeaterValue", { name: "fanThreshold", value: Math.round(newValue) }) }; KeyNavigation.tab: heaterVoltage120; KeyNavigation.backtab: heaterTestTimeoutSlider }
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
                            text: CalibrationService.snapshot.nominalHeaterVoltage === 0
                                  ? TranslationManager.translate("settings.calibration.heaterVoltageUnknown",
                                                                 "Your machine has not reported one")
                                  : TranslationManager.translate("settings.calibration.heaterVoltageReported",
                                                                 "Machine reports %1 V").arg(CalibrationService.snapshot.nominalHeaterVoltage)
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }
                    }

                    AccessibleButton {
                        id: heaterVoltage120
                        text: "120 V"
                        accessibleName: TranslationManager.translate("settings.calibration.heaterVoltageSet120", "Set nominal heater voltage to 120 volts")
                        primary: CalibrationService.snapshot.nominalHeaterVoltage === 120
                        enabled: CalibrationService.snapshot.connected
                        onClicked: calibrationTab.runOperation("setHeaterVoltage", { volts: 120 })
                        KeyNavigation.tab: heaterVoltage230
                        KeyNavigation.backtab: fanThresholdSlider
                    }

                    AccessibleButton {
                        id: heaterVoltage230
                        text: "230 V"
                        accessibleName: TranslationManager.translate("settings.calibration.heaterVoltageSet230", "Set nominal heater voltage to 230 volts")
                        primary: CalibrationService.snapshot.nominalHeaterVoltage === 230
                        enabled: CalibrationService.snapshot.connected
                        onClicked: calibrationTab.runOperation("setHeaterVoltage", { volts: 230 })
                        KeyNavigation.tab: defaultsButton
                        KeyNavigation.backtab: heaterVoltage120
                    }
                }

            }
            }

            ColumnLayout {
                id: calibrationFooter
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderColor }

                // Defaults for cafe button
                AccessibleButton {
                    id: defaultsButton
                    Layout.fillWidth: true
                    primary: true
                    text: TranslationManager.translate("settings.calibration.defaultsForCafe", "Defaults for cafe")
                    accessibleName: TranslationManager.translate("settings.calibration.defaultsForCafeAccessible", "Reset heater calibration to cafe defaults")
                    onClicked: {
                        // The host applies the complete, validated cafe preset atomically.
                        calibrationTab.runOperation("setHeaterDefaults", {})
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
