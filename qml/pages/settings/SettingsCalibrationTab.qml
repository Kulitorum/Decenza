import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

Item {
    id: calibrationTab

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
                    color: Theme.surfaceColor
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
                                text: TranslationManager.translate("settings.preferences.autoCalibration", "Auto calibration — learns from your scale after each shot")
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            StyledSwitch {
                                checked: Settings.autoFlowCalibration
                                accessibleName: TranslationManager.translate("settings.preferences.autoCalibration", "Auto calibration")
                                onToggled: Settings.autoFlowCalibration = checked
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            property int _calVersion: Settings.perProfileFlowCalVersion
                            property double effectiveCal: {
                                void(_calVersion);
                                void(Settings.autoFlowCalibration);
                                void(Settings.flowCalibrationMultiplier);
                                return Settings.effectiveFlowCalibration(ProfileManager.baseProfileName);
                            }
                            property bool isPerProfile: {
                                void(_calVersion);
                                void(Settings.autoFlowCalibration);
                                void(Settings.flowCalibrationMultiplier);
                                return Settings.hasProfileFlowCalibration(ProfileManager.baseProfileName);
                            }

                            Text {
                                property string calSuffix: {
                                    if (!Settings.autoFlowCalibration) return "";
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
                                onClicked: Settings.clearProfileFlowCalibration(ProfileManager.baseProfileName)
                            }

                            AccessibleButton {
                                accessibleName: TranslationManager.translate("settings.preferences.openFlowCalibration", "Open Flow Calibration")
                                text: TranslationManager.translate("settings.preferences.calibrate", "Calibrate")
                                primary: true
                                enabled: !Settings.autoFlowCalibration
                                onClicked: pageStack.push(Qt.resolvedUrl("../FlowCalibrationPage.qml"))
                            }
                        }
                    }
                }

                // Weight Stop Timing (was Stop-at-Weight Calibration)
                Rectangle {
                    objectName: "weightStopTiming"
                    Layout.fillWidth: true
                    implicitHeight: sawContent.implicitHeight + Theme.scaled(24)
                    color: Theme.surfaceColor
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
                                text: Settings.sawLearnedLag.toFixed(2) + TranslationManager.translate("common.unit.seconds", "s")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: (Settings.scaleType || TranslationManager.translate("settings.options.none", "none")) + " · " + TranslationManager.translate("settings.options.autoLearns", "learns when to stop so your cup hits target weight")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                id: resetText
                                text: TranslationManager.translate("settings.options.reset", "Reset")
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -Theme.scaled(4)
                                    accessibleName: TranslationManager.translate("settings.calibration.resetWeightStopTiming", "Reset weight stop timing")
                                    accessibleItem: resetText
                                    onAccessibleClicked: Settings.resetSawLearning()
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
                    color: Theme.surfaceColor
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
                    color: Theme.surfaceColor
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
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: ignoreVolumeContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(15)
                        spacing: Theme.spacingSmall

                        Text {
                            text: TranslationManager.translate("settings.calibration.preferWeightOverVolume", "Prefer Weight over Volume")
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
                                checked: Settings.ignoreVolumeWithScale
                                accessibleName: TranslationManager.translate("settings.calibration.preferWeightOverVolume", "Prefer weight over volume")
                                onToggled: Settings.ignoreVolumeWithScale = checked
                            }
                        }
                    }
                }

                // Steam Health Monitor
                Rectangle {
                    objectName: "steamHealth"
                    Layout.fillWidth: true
                    implicitHeight: steamHealthContent.implicitHeight + Theme.scaled(30)
                    color: Theme.surfaceColor
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
                                "Rising pressure or temperature over time can indicate scale buildup. The bars show how far your latest session has drifted from your clean-machine baseline toward the warning level.")
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }

                        // Status: no data yet
                        Text {
                            visible: !SteamHealthTracker.hasData
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.calibration.steamHealthNoData",
                                "Not enough data yet. At least 5 steam sessions with the same settings are needed to establish a baseline.")
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
                                height: Theme.scaled(6)
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
                                          ": " + SteamHealthTracker.currentTemperature.toFixed(0) + "°C"
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
                                    text: SteamHealthTracker.baselineTemperature.toFixed(0) + " — " +
                                          SteamHealthTracker.temperatureThreshold.toFixed(0) + "°C"
                                    color: Theme.textSecondaryColor
                                    font.family: Theme.bodyFont.family
                                    font.pixelSize: Theme.scaled(11)
                                    Accessible.ignored: true
                                }
                            }

                            // Progress bar
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(6)
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
                                width: resetBaselineText.implicitWidth + Theme.spacingMedium * 2
                                height: Theme.scaled(28)
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

                        // Calibrate Steam button — hidden until recommendation algorithm is improved
                        // AccessibleButton {
                        //     Layout.fillWidth: true
                        //     Layout.topMargin: Theme.spacingSmall
                        //     text: SteamCalibrator.hasCalibration
                        //           ? TranslationManager.translate("settings.calibration.steamCalRecommended",
                        //                 "Calibrated: %1 mL/s (CV %2)")
                        //                 .arg((SteamCalibrator.recommendedFlow / 100).toFixed(2))
                        //                 .arg(SteamCalibrator.bestCV.toFixed(3))
                        //           : TranslationManager.translate("settings.calibration.steamCalibrate", "Calibrate Steam")
                        //     accessibleName: TranslationManager.translate("settings.calibration.steamCalibrateAccessible", "Calibrate steam settings")
                        //     primary: !SteamCalibrator.hasCalibration
                        //     onClicked: steamCalibrationDialog.open()
                        // }
                    }
                }
            }

            // Spacer
            Item { Layout.fillWidth: true }
        }
    }

    // Heater Calibration Warning Dialog
    Dialog {
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
    Dialog {
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

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Heater idle temperature
                Text { text: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); font: Theme.captionFont; color: Theme.temperatureColor }
                ValueInput { id: heaterIdleTempSlider; Layout.fillWidth: true; valueColor: Theme.temperatureColor; accessibleName: TranslationManager.translate("settings.calibration.heaterIdleTemp", "Heater idle temperature"); from: 0; to: 990; stepSize: 5; displayText: (value / 10).toFixed(1) + "\u00B0C"; rangeText: "0.0\u00B0C \u2014 99.0\u00B0C"; value: Settings.heaterIdleTemp; onValueModified: function(newValue) { Settings.heaterIdleTemp = Math.round(newValue) }; KeyNavigation.tab: heaterWarmupFlowSlider; KeyNavigation.backtab: doneButton }

                // Heater warmup flow rate
                Text { text: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); font: Theme.captionFont; color: Theme.flowColor }
                ValueInput { id: heaterWarmupFlowSlider; Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterWarmupFlow", "Heater warmup flow rate"); from: 5; to: 60; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 6.0 mL/s"; value: Settings.heaterWarmupFlow; onValueModified: function(newValue) { Settings.heaterWarmupFlow = Math.round(newValue) }; KeyNavigation.tab: heaterTestFlowSlider; KeyNavigation.backtab: heaterIdleTempSlider }

                // Heater test flow rate
                Text { text: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); font: Theme.captionFont; color: Theme.flowColor }
                ValueInput { id: heaterTestFlowSlider; Layout.fillWidth: true; valueColor: Theme.flowColor; accessibleName: TranslationManager.translate("settings.calibration.heaterTestFlow", "Heater test flow rate"); from: 5; to: 80; stepSize: 1; displayText: (value / 10).toFixed(1) + " mL/s"; rangeText: "0.5 \u2014 8.0 mL/s"; value: Settings.heaterTestFlow; onValueModified: function(newValue) { Settings.heaterTestFlow = Math.round(newValue) }; KeyNavigation.tab: heaterTestTimeoutSlider; KeyNavigation.backtab: heaterWarmupFlowSlider }

                // Heater test time-out
                Text { text: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                ValueInput { id: heaterTestTimeoutSlider; Layout.fillWidth: true; accessibleName: TranslationManager.translate("settings.calibration.heaterTestTimeout", "Heater test time-out"); from: 10; to: 300; stepSize: 1; displayText: (value / 10).toFixed(1) + " s"; rangeText: "1.0 — 30.0 s"; value: Settings.heaterWarmupTimeout; onValueModified: function(newValue) { Settings.heaterWarmupTimeout = Math.round(newValue) }; KeyNavigation.tab: steamTwoTapSwitch; KeyNavigation.backtab: heaterTestFlowSlider }

                // Steam two-tap stop
                RowLayout { Layout.fillWidth: true
                    Text { text: TranslationManager.translate("settings.calibration.steamTwoTapStop", "Steam two-tap stop"); font: Theme.captionFont; color: Theme.textSecondaryColor }
                    Item { Layout.fillWidth: true }
                    StyledSwitch {
                        id: steamTwoTapSwitch
                        accessibleName: TranslationManager.translate("settings.calibration.steamTwoTapStop", "Steam two-tap stop")
                        checked: Settings.steamTwoTapStop
                        onToggled: Settings.steamTwoTapStop = checked
                        KeyNavigation.tab: defaultsButton
                        KeyNavigation.backtab: heaterTestTimeoutSlider
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.calibration.steamTwoTapStopDesc", "First tap goes to puffs, second tap stops steam")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.WordWrap
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                // Defaults for cafe button
                AccessibleButton {
                    id: defaultsButton
                    Layout.fillWidth: true
                    primary: true
                    text: TranslationManager.translate("settings.calibration.defaultsForCafe", "Defaults for cafe")
                    accessibleName: TranslationManager.translate("settings.calibration.defaultsForCafeAccessible", "Reset heater calibration to cafe defaults")
                    onClicked: {
                        Settings.heaterIdleTemp = 990
                        Settings.heaterWarmupFlow = 20
                        Settings.heaterTestFlow = 40
                        Settings.heaterWarmupTimeout = 10
                        Settings.steamTwoTapStop = true
                    }
                    KeyNavigation.tab: doneButton
                    KeyNavigation.backtab: steamTwoTapSwitch
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

    // Steam Calibration Dialog
    SteamCalibrationDialog {
        id: steamCalibrationDialog
    }
}
