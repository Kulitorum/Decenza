import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

// DE1 firmware update tab. Surfaces the FirmwareUpdater state machine
// from MainController (MainController.firmwareUpdater) — current vs.
// available version, "Check now" + "Update now" buttons, a progress
// bar, and an inline error / retry strip when the last attempt failed.
//
// Mount under SettingsMachineTab as a sub-section, or wire as its own
// top-level tab in SettingsPage.qml.

Item {
    id: firmwareTab

    readonly property var fw: typeof MainController !== "undefined" && MainController
                              ? MainController.firmwareUpdater : null

    // FirmwareUpdater::State enum values (kept in sync with the C++ side
    // — Q_ENUM exposes them but using the integer is the simplest binding)
    readonly property int stateIdle:           0
    readonly property int stateChecking:       1
    readonly property int stateDownloading:    2
    readonly property int stateReady:          3
    readonly property int stateErasing:        4
    readonly property int stateUploading:      5
    readonly property int stateVerifying:      6
    readonly property int stateSucceeded:      7
    readonly property int stateFailed:         8
    readonly property int stateAwaitingReboot: 9

    readonly property bool isWorking: fw && (fw.state === stateChecking ||
                                             fw.state === stateDownloading ||
                                             fw.state === stateErasing ||
                                             fw.state === stateUploading ||
                                             fw.state === stateVerifying ||
                                             fw.state === stateAwaitingReboot)
    readonly property bool isFlashing: fw && (fw.state === stateErasing ||
                                              fw.state === stateUploading ||
                                              fw.state === stateVerifying ||
                                              fw.state === stateAwaitingReboot)

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
                        text: {
                            if (!fw) return ""
                            if (fw.isDowngrade) {
                                return TranslationManager.translate(
                                    "firmware.tab.availableDowngrade",
                                    "Available: v%1 (downgrade)")
                                    .arg(fw.availableVersion)
                            }
                            return TranslationManager.translate(
                                "firmware.tab.available", "Available: v%1")
                                .arg(fw.availableVersion)
                        }
                        color: fw && fw.updateAvailable
                               ? (fw.isDowngrade ? Theme.warningColor : Theme.accentColor)
                               : Theme.textSecondaryColor
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
                    text: fw && fw.isDowngrade
                          ? TranslationManager.translate(
                                "firmware.tab.downgradeNow", "Downgrade now")
                          : TranslationManager.translate(
                                "firmware.tab.updateNow", "Update now")
                    accessibleName: text
                    enabled: fw && fw.updateAvailable && !firmwareTab.isWorking && !fw.isSimulated
                    onClicked: if (fw) fw.startUpdate()
                }
            }
        }

        // ----- Downgrade warning strip -----------------------------
        // Surfaced whenever the available blob is older than what's
        // installed, so a user flipping the channel toggle (e.g. nightly
        // → stable) sees clearly that flashing will roll the DE1 back.

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(70)
            visible: fw && fw.updateAvailable && fw.isDowngrade
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(2)

                    Tr {
                        key: "firmware.tab.downgradeHeader"
                        fallback: "This is a downgrade"
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "firmware.tab.downgradeDetail",
                                  "Installed v%1 → available v%2. " +
                                  "Flashing will roll the DE1 back. " +
                                  "Continue only if you know why.")
                              .arg(fw && fw.installedVersion > 0 ? fw.installedVersion : "—")
                              .arg(fw ? fw.availableVersion : "—")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // ----- Simulator notice ------------------------------------
        // Shown whenever the app is wired to the DE1 simulator. The page
        // is still functional (check, channel toggle, version surfaces)
        // but flashing is disabled — no real DE1 to write to.

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)
            visible: fw && fw.isSimulated
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1

            Text {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                text: TranslationManager.translate(
                          "firmware.tab.simulatorNote",
                          "Simulator connected — flashing is disabled. " +
                          "Check and channel selection still work for testing.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
        }

        // ----- Status + progress strip (visible while working) -----

        Rectangle {
            // Show only during the long flash phases (Erasing/Uploading/
            // Verifying). Checking and Downloading can be sub-second,
            // which would otherwise fire this 110px accent-bordered card
            // every channel toggle and look like a red/pink flash.
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(110)
            visible: firmwareTab.isFlashing
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
                    wrapMode: Text.Wrap
                    text: TranslationManager.translate(
                              "firmware.tab.doNotDisconnect",
                              "Do not disconnect the DE1. Keep Decenza open and " +
                              "in the foreground until the update completes — " +
                              "backgrounding the app can suspend BLE.")
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

        // ----- Awaiting-reboot strip ------------------------------
        // Shown after verify succeeds, while we wait for the user to
        // power-cycle the DE1 so it boots into the new firmware.

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(80)
            visible: fw && fw.state === firmwareTab.stateAwaitingReboot
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1

            Text {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                text: TranslationManager.translate(
                          "firmware.tab.awaitingManualReboot",
                          "Firmware flashed — please power-cycle the DE1 to boot into the new version.")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(14)
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        // ----- Channel toggle --------------------------------------

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            spacing: Theme.spacingMedium

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Tr {
                    key: "firmware.tab.nightlyChannel"
                    fallback: "Use nightly firmware channel"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate(
                              "firmware.tab.nightlyChannelNote",
                              "Off: stable (de1plus). On: nightly (de1nightly). " +
                              "Nightly may include unreleased firmware that Decent " +
                              "has not yet promoted to the stable channel.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.Wrap
                }
            }

            Switch {
                id: nightlyChannelSwitch
                checked: Settings.firmwareNightlyChannel
                enabled: !firmwareTab.isWorking
                onToggled: Settings.firmwareNightlyChannel = checked
                Accessible.role: Accessible.CheckBox
                Accessible.name: TranslationManager.translate(
                                    "firmware.tab.nightlyChannel",
                                    "Use nightly firmware channel")
                Accessible.focusable: true
                Accessible.onPressAction: nightlyChannelSwitch.toggle()
            }
        }

        // ----- Source / explanatory note --------------------------

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            text: TranslationManager.translate(
                      "firmware.tab.sourceNote",
                      "Firmware is fetched from Decent's update CDN " +
                      "(fast.decentespresso.com). Auto-checks run weekly. " +
                      "The upload takes several minutes over Bluetooth — " +
                      "keep the app open and the DE1 connected until the " +
                      "update completes.")
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }
    }
}
