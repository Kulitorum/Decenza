import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Dialog {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 0
    topPadding: 0
    bottomPadding: 0
    closePolicy: Dialog.CloseOnEscape
    header: null
    width: Math.min(parent ? parent.width - Theme.scaled(40) : Theme.scaled(400), Theme.scaled(500))
    height: Math.min(dialogContent.implicitHeight + Theme.scaled(32),
                     parent ? parent.height - Theme.scaled(40) : Theme.scaled(600))

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    onOpened: {
        if (SteamCalibrator.state === 0 || SteamCalibrator.state === 5)
            SteamCalibrator.startCalibration()
    }

    onClosed: {
        if (SteamCalibrator.state !== 0 && SteamCalibrator.state !== 5)
            SteamCalibrator.cancelCalibration()
    }

    contentItem: Flickable {
        contentHeight: dialogContent.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: dialogContent
            width: parent.width
            spacing: Theme.spacingMedium

            // Title
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(16)
                Layout.bottomMargin: 0

                Text {
                    text: TranslationManager.translate("steamCal.title", "Steam Calibration")
                    color: Theme.textColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(18)
                    font.bold: true
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                AccessibleButton {
                    text: "\u00D7"
                    accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Close dialog")
                    flat: true
                    onClicked: root.close()
                }
            }

            // Beta notice
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                implicitHeight: betaText.implicitHeight + Theme.scaled(12)
                color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.1)
                radius: Theme.scaled(4)

                Text {
                    id: betaText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(8)
                    text: TranslationManager.translate("steamCal.beta",
                        "Beta: Results may vary. Please open an issue on GitHub if you get results you don't believe.")
                    color: Theme.warningColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }
            }

            // Step indicator
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                visible: SteamCalibrator.state > 0 && SteamCalibrator.state < 5
                spacing: Theme.scaled(4)

                Repeater {
                    model: SteamCalibrator.totalSteps
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(4)
                        radius: Theme.scaled(2)
                        color: index < SteamCalibrator.currentStep ? Theme.primaryColor
                             : index === SteamCalibrator.currentStep ? Theme.warningColor
                             : Theme.backgroundColor
                    }
                }
            }

            // Status message
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                text: SteamCalibrator.statusMessage
                color: Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(14)
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            // Heater recovery indicator
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                visible: SteamCalibrator.state === 2 /* WaitingToStart */
                spacing: Theme.scaled(8)

                Text {
                    text: SteamCalibrator.heaterReady
                        ? TranslationManager.translate("steamCal.heaterReady", "Heater ready — start steaming now")
                        : TranslationManager.translate("steamCal.heaterRecovering", "Waiting for heater to recover...")
                    color: SteamCalibrator.heaterReady ? Theme.primaryColor : Theme.warningColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(14)
                    font.bold: true
                    Accessible.ignored: true
                }

                Text {
                    text: SteamCalibrator.currentHeaterTemp.toFixed(0) + "°C"
                    color: Theme.textSecondaryColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(14)
                    Accessible.ignored: true
                }
            }

            // Steaming progress
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                visible: SteamCalibrator.state === 3 /* Steaming */
                spacing: Theme.scaled(8)

                Text {
                    Layout.fillWidth: true
                    text: {
                        var remaining = Math.max(0, 22 - SteamCalibrator.steamingElapsed)
                        if (SteamCalibrator.hasEnoughData)
                            return TranslationManager.translate("steamCal.stopping", "Stopping...")
                        else if (remaining > 0)
                            return TranslationManager.translate("steamCal.countdown", "Collecting data: %1s remaining")
                                .arg(Math.ceil(remaining))
                        else
                            return TranslationManager.translate("steamCal.stopping", "Stopping...")
                    }
                    color: SteamCalibrator.hasEnoughData ? Theme.primaryColor : Theme.textColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    Accessible.ignored: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    color: Theme.backgroundColor

                    Rectangle {
                        width: parent.width * Math.min(1, SteamCalibrator.steamingElapsed / 22)
                        height: parent.height
                        radius: Theme.scaled(4)
                        color: SteamCalibrator.hasEnoughData ? Theme.primaryColor : Theme.warningColor
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }

            // Instructions
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                visible: SteamCalibrator.state === 1
                text: TranslationManager.translate("steamCal.instructions",
                    "This tool finds the optimal steam flow rate for your machine by measuring pressure stability at different settings.\n\n" +
                    "Steam into the air (no water needed). Each step auto-stops after ~20 seconds. " +
                    "The tool will wait for the heater to recover between steps.")
                color: Theme.textSecondaryColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            // Results view
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(16)
                Layout.rightMargin: Theme.scaled(16)
                visible: SteamCalibrator.state === 5
                spacing: Theme.scaled(8)

                // Recommendation banner
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: recLayout.implicitHeight + Theme.scaled(16)
                    color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1)
                    radius: Theme.cardRadius
                    visible: SteamCalibrator.hasCalibration

                    ColumnLayout {
                        id: recLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(4)

                        Text {
                            text: TranslationManager.translate("steamCal.recommended", "Recommended Flow Rate")
                            color: Theme.primaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        Text {
                            text: (SteamCalibrator.recommendedFlow / 100).toFixed(2) + " mL/s"
                            color: Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(24)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        Text {
                            text: TranslationManager.translate("steamCal.recDetail",
                                "CV: %1  ·  Est. dilution: ~%2%")
                                .arg(SteamCalibrator.bestCV.toFixed(3))
                                .arg(SteamCalibrator.recommendedDilution.toFixed(1))
                            color: Theme.textSecondaryColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            Accessible.ignored: true
                        }
                    }
                }

                // Results table header
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Text {
                        Layout.preferredWidth: Theme.scaled(55)
                        text: TranslationManager.translate("steamCal.flowHeader", "Flow")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(11)
                        font.bold: true
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.preferredWidth: Theme.scaled(55)
                        text: TranslationManager.translate("steamCal.pressureHeader", "Pressure")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(11)
                        font.bold: true
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.preferredWidth: Theme.scaled(45)
                        text: "CV"
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(11)
                        font.bold: true
                        Accessible.ignored: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("steamCal.stabilityHeader", "Stability")
                        color: Theme.textSecondaryColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.scaled(11)
                        font.bold: true
                        Accessible.ignored: true
                    }
                }

                // Result rows
                Repeater {
                    model: SteamCalibrator.results

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        readonly property bool isRecommended:
                            modelData.flowRate === SteamCalibrator.recommendedFlow

                        // Find min CV for bar scaling
                        readonly property double minCV: {
                            var min = 999
                            var res = SteamCalibrator.results
                            for (var i = 0; i < res.length; i++)
                                if (res[i].pressureCV < min) min = res[i].pressureCV
                            return min
                        }

                        Text {
                            Layout.preferredWidth: Theme.scaled(55)
                            text: (modelData.flowRate / 100).toFixed(2)
                            color: parent.isRecommended ? Theme.primaryColor : Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            font.bold: parent.isRecommended
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.preferredWidth: Theme.scaled(55)
                            text: modelData.avgPressure.toFixed(1) + " bar"
                            color: parent.isRecommended ? Theme.primaryColor : Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.preferredWidth: Theme.scaled(45)
                            text: modelData.pressureCV.toFixed(3)
                            color: parent.isRecommended ? Theme.primaryColor : Theme.textColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(12)
                            Accessible.ignored: true
                        }

                        // CV bar — lower is better, so bar shows inverse
                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.scaled(16)
                            radius: Theme.scaled(3)
                            color: Theme.backgroundColor

                            Rectangle {
                                // Scale: minCV gets full bar, 2x minCV gets half bar
                                width: parent.width * Math.min(1, Math.max(0.1,
                                    parent.parent.minCV / Math.max(0.001, modelData.pressureCV)))
                                height: parent.height
                                radius: Theme.scaled(3)
                                color: parent.parent.isRecommended ? Theme.primaryColor
                                     : modelData.pressureCV <= parent.parent.minCV * 1.2 ? Theme.primaryColor
                                     : modelData.pressureCV <= parent.parent.minCV * 1.5 ? Theme.warningColor
                                     : Theme.errorColor
                            }
                        }

                        Image {
                            visible: parent.isRecommended
                            source: "qrc:/icons/tick.svg"
                            width: Theme.scaled(14)
                            height: Theme.scaled(14)
                            Accessible.ignored: true
                        }
                    }
                }
            }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(16)
                Layout.topMargin: Theme.spacingSmall
                spacing: Theme.spacingMedium

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    visible: SteamCalibrator.state > 0 && SteamCalibrator.state < 5
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: {
                        SteamCalibrator.cancelCalibration()
                        root.close()
                    }
                }

                AccessibleButton {
                    visible: SteamCalibrator.state === 1
                    text: TranslationManager.translate("steamCal.begin", "Begin")
                    accessibleName: TranslationManager.translate("steamCal.begin", "Begin")
                    primary: true
                    onClicked: SteamCalibrator.advanceToNextStep()
                }

                AccessibleButton {
                    visible: SteamCalibrator.state === 5 && SteamCalibrator.hasCalibration
                    text: TranslationManager.translate("steamCal.apply", "Apply")
                    accessibleName: TranslationManager.translate("steamCal.applyAccessible",
                        "Apply recommended steam flow rate")
                    primary: true
                    onClicked: {
                        SteamCalibrator.applyRecommendation()
                        root.close()
                    }
                }

                AccessibleButton {
                    visible: SteamCalibrator.state === 5
                    text: TranslationManager.translate("common.button.close", "Close")
                    accessibleName: TranslationManager.translate("common.button.close", "Close")
                    onClicked: root.close()
                }
            }
        }
    }
}
