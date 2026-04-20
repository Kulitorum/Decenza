import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

// DE1 firmware update tab. Surfaces the FirmwareUpdater state machine
// from MainController (mainController.firmwareUpdater) — current vs.
// available version, "Check now" + "Update now" buttons, a progress
// bar, and an inline error / retry strip when the last attempt failed.
//
// Mount under SettingsMachineTab as a sub-section, or wire as its own
// top-level tab in SettingsPage.qml.

Item {
    id: firmwareTab

    readonly property var fw: typeof mainController !== "undefined" && mainController
                              ? mainController.firmwareUpdater : null

    // FirmwareUpdater::State enum values (kept in sync with the C++ side
    // — Q_ENUM exposes them but using the integer is the simplest binding)
    readonly property int stateIdle:        0
    readonly property int stateChecking:    1
    readonly property int stateDownloading: 2
    readonly property int stateReady:       3
    readonly property int stateErasing:     4
    readonly property int stateUploading:   5
    readonly property int stateVerifying:   6
    readonly property int stateSucceeded:   7
    readonly property int stateFailed:      8

    readonly property bool isWorking: fw && (fw.state === stateChecking ||
                                             fw.state === stateDownloading ||
                                             fw.state === stateErasing ||
                                             fw.state === stateUploading ||
                                             fw.state === stateVerifying)
    readonly property bool isFlashing: fw && (fw.state === stateErasing ||
                                              fw.state === stateUploading ||
                                              fw.state === stateVerifying)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // ----- Version card ---------------------------------------

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(110)
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingLarge

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Tr {
                        key: "firmware.tab.title"
                        fallback: "DE1 Firmware"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }
                    Text {
                        text: TranslationManager.translate(
                                  "firmware.tab.installed", "Installed: v%1")
                              .arg(fw && fw.installedVersion > 0
                                   ? fw.installedVersion : "—")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                    }
                    Text {
                        visible: fw && fw.availableVersion > 0
                        text: TranslationManager.translate(
                                  "firmware.tab.available", "Available: v%1")
                              .arg(fw ? fw.availableVersion : "—")
                        color: fw && fw.updateAvailable
                               ? Theme.accentColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        font.bold: fw && fw.updateAvailable
                    }
                }

                AccessibleButton {
                    Layout.preferredWidth: Theme.scaled(140)
                    Layout.preferredHeight: Theme.scaled(40)
                    text: TranslationManager.translate(
                              "firmware.tab.checkNow", "Check now")
                    accessibleName: text
                    enabled: fw && !firmwareTab.isWorking
                    onClicked: if (fw) fw.checkForUpdate()
                }

                AccessibleButton {
                    Layout.preferredWidth: Theme.scaled(140)
                    Layout.preferredHeight: Theme.scaled(40)
                    text: TranslationManager.translate(
                              "firmware.tab.updateNow", "Update now")
                    accessibleName: text
                    enabled: fw && fw.updateAvailable && !firmwareTab.isWorking
                    onClicked: if (fw) fw.startUpdate()
                }
            }
        }

        // ----- Status + progress strip (visible while working) -----

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(110)
            visible: firmwareTab.isWorking
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.accentColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Text {
                    text: fw ? fw.stateText : ""
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(15)
                    font.bold: true
                }

                ProgressBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(10)
                    from: 0
                    to: 1
                    value: fw ? fw.progress : 0
                }

                Text {
                    Layout.fillWidth: true
                    visible: firmwareTab.isFlashing
                    color: Theme.warningColor
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                    text: TranslationManager.translate(
                              "firmware.tab.doNotDisconnect",
                              "Do not disconnect the DE1 — flashing in progress")
                }
            }
        }

        // ----- Error strip with Retry (visible after a failure) ----

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(80)
            visible: fw && fw.state === firmwareTab.stateFailed
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.errorColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Tr {
                        key: "firmware.tab.failedHeader"
                        fallback: "Update failed"
                        color: Theme.errorColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: fw ? fw.errorMessage : ""
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }
                }

                AccessibleButton {
                    Layout.preferredWidth: Theme.scaled(120)
                    Layout.preferredHeight: Theme.scaled(36)
                    text: TranslationManager.translate(
                              "firmware.tab.retry", "Retry")
                    accessibleName: text
                    enabled: fw && fw.retryAvailable
                    onClicked: if (fw) fw.retry()
                }
            }
        }

        // ----- Success strip (visible briefly after Succeeded) -----

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(60)
            visible: fw && fw.state === firmwareTab.stateSucceeded
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.accentColor
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate(
                          "firmware.tab.success",
                          "Update complete — DE1 is on v%1")
                      .arg(fw ? fw.installedVersion : "")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
            }
        }

        // ----- Source / explanatory note --------------------------

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            text: TranslationManager.translate(
                      "firmware.tab.sourceNote",
                      "Firmware comes from Decent's official GitHub repository " +
                      "(decentespresso/de1app). Auto-checks run weekly. " +
                      "A typical update takes about 45 seconds.")
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }
    }
}
