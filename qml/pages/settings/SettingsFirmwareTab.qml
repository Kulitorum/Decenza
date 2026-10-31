import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// DE1 firmware update tab. Surfaces the FirmwareUpdater state machine
// from the shared UpdateService host projection — current vs.
// available version, "Check now" + "Update now" buttons, a progress
// bar, and an inline error / retry strip when the last attempt failed.
//
// Mount under SettingsMachineTab as a sub-section, or wire as its own
// top-level tab in SettingsPage.qml.

Item {
    id: firmwareTab

    readonly property var fw: UpdateService.ready ? UpdateService.snapshot.firmware : null
    property string pendingRequest: ""
    property string pendingOperation: ""

    function requestFirmware(operation) {
        if (!UpdateService.ready || pendingRequest !== "") return false
        pendingOperation = operation
        pendingRequest = UpdateService.execute(operation)
        return pendingRequest !== ""
    }

    Connections {
        target: UpdateService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId !== firmwareTab.pendingRequest || operation !== firmwareTab.pendingOperation) return
            firmwareTab.pendingRequest = ""
            firmwareTab.pendingOperation = ""
            if (!success) {
                const message = reason === "simulatedDevice"
                    ? TranslationManager.translate("firmware.tab.simulatedRefused", "Firmware installation is unavailable in simulation mode.")
                    : TranslationManager.translate("firmware.tab.commandFailed", "The firmware request could not start. Check the machine connection and firmware status, then try again.")
                firmwareRequestError.show(message)
                AccessibilityManager.announce(message, true)
            }
        }
    }
    StatusToast { id: firmwareRequestError; error: true }

    PreferenceWrite {
        id: firmwareChannelPreference
        domain: "app"
        onFailed: function(message) { AccessibilityManager.announce(message) }
    }

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
            Layout.preferredHeight: Theme.scaled(120)
            color: Theme.cardBackgroundColor
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
                              .arg(firmwareTab.fw && firmwareTab.fw.installedVersion > 0
                                   ? firmwareTab.fw.installedVersion : "—")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(13)
                    }
                    Text {
                        visible: firmwareTab.fw && firmwareTab.fw.availableVersion > 0
                        text: {
                            if (!firmwareTab.fw) return ""
                            if (firmwareTab.fw.isDowngrade) {
                                return TranslationManager.translate(
                                    "firmware.tab.availableDowngrade",
                                    "Available: v%1 (downgrade)")
                                    .arg(firmwareTab.fw.availableVersion)
                            }
                            return TranslationManager.translate(
                                "firmware.tab.available", "Available: v%1")
                                .arg(firmwareTab.fw.availableVersion)
                        }
                        color: firmwareTab.fw && firmwareTab.fw.updateAvailable
                               ? (firmwareTab.fw.isDowngrade ? Theme.warningColor : Theme.accentColor)
                               : Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(13)
                        font.bold: firmwareTab.fw && firmwareTab.fw.updateAvailable
                    }
                }

                AccessibleButton {
                    Layout.preferredWidth: Theme.scaled(140)
                    Layout.preferredHeight: Theme.scaled(40)
                    text: TranslationManager.translate(
                              "firmware.tab.checkNow", "Check now")
                    accessibleName: text
                    enabled: UpdateService.ready && firmwareTab.fw && !firmwareTab.isWorking && firmwareTab.pendingRequest === ""
                    onClicked: firmwareTab.requestFirmware("checkFirmware")
                }

                AccessibleButton {
                    Layout.preferredWidth: Theme.scaled(140)
                    Layout.preferredHeight: Theme.scaled(40)
                    text: {
                        if (!firmwareTab.fw) return TranslationManager.translate(
                                "firmware.tab.updateNow", "Update now")
                        if (firmwareTab.fw.isDowngrade)
                            return TranslationManager.translate(
                                "firmware.tab.downgradeNow", "Downgrade now")
                        if (firmwareTab.fw.isReflash)
                            return TranslationManager.translate(
                                "firmware.tab.reflashNow", "Reflash")
                        return TranslationManager.translate(
                                "firmware.tab.updateNow", "Update now")
                    }
                    accessibleName: text
                    // No version gate — matches de1app, which flashes whatever
                    // is in bootfwupdate.dat regardless of the installed
                    // build. Same-build re-flash is warned about in the strip
                    // below, not blocked: it is the only recovery for a bank
                    // that verified but did not take. The requirement is a
                    // known bundled version (i.e. a check has succeeded), not a
                    // newer one.
                    enabled: UpdateService.ready && firmwareTab.fw && firmwareTab.fw.availableVersion > 0 && !firmwareTab.isWorking && !firmwareTab.fw.isSimulated && firmwareTab.pendingRequest === ""
                    onClicked: firmwareTab.requestFirmware("startFirmware")
                }
            }
        }

        // ----- Bundled release notes -------------------------------

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(86)
            visible: firmwareTab.fw && firmwareTab.fw.availableVersion > 0
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.scaled(3)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate(
                              "firmware.tab.releaseNotesHeader",
                              "%1 firmware v%2")
                          .arg(firmwareTab.fw && firmwareTab.fw.availableChannelLabel
                               ? firmwareTab.fw.availableChannelLabel
                               : TranslationManager.translate("firmware.tab.stableChannel", "Stable"))
                          .arg(firmwareTab.fw && firmwareTab.fw.availableVersionLabel
                               ? firmwareTab.fw.availableVersionLabel
                               : (firmwareTab.fw ? firmwareTab.fw.availableVersion : "—"))
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: firmwareTab.fw && firmwareTab.fw.availableReleaseNotes
                          ? firmwareTab.fw.availableReleaseNotes
                          : TranslationManager.translate(
                                "firmware.tab.noReleaseNotes",
                                "No release notes are included for this firmware.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }

        // ----- Downgrade warning strip -----------------------------
        // Surfaced whenever the available blob is older than what's
        // installed, so a user flipping the channel toggle (e.g. Early access
        // → Stable) sees clearly that flashing will roll the DE1 back.

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(70)
            visible: firmwareTab.fw
                     && ((firmwareTab.fw.updateAvailable && firmwareTab.fw.isDowngrade)
                         || firmwareTab.fw.isReflash)
            color: Theme.cardBackgroundColor
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

                    Text {
                        text: firmwareTab.fw && firmwareTab.fw.isReflash
                              ? TranslationManager.translate(
                                    "firmware.tab.reflashHeader",
                                    "Already on this version")
                              : TranslationManager.translate(
                                    "firmware.tab.downgradeHeader",
                                    "This is a downgrade")
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(14)
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: firmwareTab.fw && firmwareTab.fw.isReflash
                              ? TranslationManager.translate(
                                    "firmware.tab.reflashDetail",
                                    "The DE1 already reports v%1. " +
                                    "Flashing it again takes the full upload time " +
                                    "and is only useful if the last update did not take.")
                                .arg(firmwareTab.fw.availableVersion)
                              : TranslationManager.translate(
                                    "firmware.tab.downgradeDetail",
                                    "Installed v%1 → available v%2. " +
                                    "Flashing will roll the DE1 back. " +
                                    "Continue only if you know why.")
                                .arg(firmwareTab.fw && firmwareTab.fw.installedVersion > 0 ? firmwareTab.fw.installedVersion : "—")
                                .arg(firmwareTab.fw ? firmwareTab.fw.availableVersion : "—")
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
            visible: firmwareTab.fw && firmwareTab.fw.isSimulated
            color: Theme.cardBackgroundColor
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
            Layout.preferredHeight: firmwareProgressContent.implicitHeight + 2 * Theme.spacingMedium
            visible: firmwareTab.isFlashing
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius
            border.color: Theme.accentColor
            border.width: 1

            ColumnLayout {
                id: firmwareProgressContent
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Text {
                    text: firmwareTab.fw ? firmwareTab.fw.stateText : ""
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(15)
                    font.bold: true
                }

                ProgressBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(10)
                    from: 0
                    to: 1
                    value: firmwareTab.fw ? firmwareTab.fw.progress : 0
                    indeterminate: firmwareTab.fw && (firmwareTab.fw.state === firmwareTab.stateErasing
                                                     || firmwareTab.fw.state === firmwareTab.stateVerifying)
                }

                Text {
                    Layout.fillWidth: true
                    visible: firmwareTab.fw && firmwareTab.fw.state === firmwareTab.stateUploading
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.Wrap
                    text: {
                        if (!firmwareTab.fw || firmwareTab.fw.state !== firmwareTab.stateUploading) return ""
                        const percent = Math.floor(firmwareTab.fw.progress * 100)
                        const seconds = firmwareTab.fw.estimatedUploadSecondsRemaining ?? -1
                        if (seconds < 0)
                            return TranslationManager.translate("firmware.tab.uploadEstimating", "%1% · Estimating upload time…").arg(percent)
                        if (seconds === 0)
                            return TranslationManager.translate("firmware.tab.uploadFinishing", "%1% · Finishing upload…").arg(percent)
                        if (seconds < 60)
                            return TranslationManager.translate("firmware.tab.uploadUnderMinute", "%1% · Less than 1 min of upload remaining").arg(percent)
                        return TranslationManager.translate("firmware.tab.uploadMinutesRemaining", "%1% · About %2 min of upload remaining")
                            .arg(percent).arg(Math.ceil(seconds / 60))
                    }
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
            visible: firmwareTab.fw && firmwareTab.fw.state === firmwareTab.stateFailed
            color: Theme.cardBackgroundColor
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
                        text: firmwareTab.fw ? firmwareTab.fw.errorMessage : ""
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
                    enabled: UpdateService.ready && firmwareTab.fw && firmwareTab.fw.retryAvailable
                        && !firmwareTab.fw.isSimulated && firmwareTab.pendingRequest === ""
                    onClicked: firmwareTab.requestFirmware("retryFirmware")
                }
            }
        }

        // ----- Success strip (visible briefly after Succeeded) -----

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(60)
            visible: firmwareTab.fw && firmwareTab.fw.state === firmwareTab.stateSucceeded
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius
            border.color: Theme.accentColor
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate(
                          "firmware.tab.success",
                          "Update complete — DE1 is on v%1")
                      .arg(firmwareTab.fw ? firmwareTab.fw.installedVersion : "")
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
            visible: firmwareTab.fw && firmwareTab.fw.state === firmwareTab.stateAwaitingReboot
            color: Theme.cardBackgroundColor
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
                    key: "firmware.tab.earlyAccessChannel"
                    fallback: "Use early access firmware"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate(
                              "firmware.tab.earlyAccessChannelNote",
                              "Off: bundled Stable firmware. On: bundled Early access firmware. " +
                              "Release notes for the selected version are shown above.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    wrapMode: Text.Wrap
                }
            }

            Switch {
                id: earlyAccessSwitch
                checked: firmwareChannelPreference.displayedValue("firmwareEarlyAccess",
                                                                   SettingsPreferencesService.preferences["app.firmwareEarlyAccess"])
                enabled: !firmwareTab.isWorking && firmwareChannelPreference.ready
                onToggled: firmwareChannelPreference.submit("firmwareEarlyAccess", checked)
                Accessible.role: Accessible.CheckBox
                Accessible.name: TranslationManager.translate(
                                    "firmware.tab.earlyAccessChannel",
                                    "Use early access firmware")
                Accessible.focusable: true
                // No press handler: Qt's own calls click(), which emits the toggled this
                // switch saves on; toggle() would not (see StyledSwitch).
            }
        }

        // ----- Source / explanatory note --------------------------

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingMedium
            text: TranslationManager.translate(
                      "firmware.tab.sourceNoteConnection",
                      "Firmware is bundled with Decenza. Auto-checks run weekly. " +
                      "Upload time depends on the Bluetooth connection. " +
                      "Keep the app open and the DE1 connected " +
                      "until the update completes.")
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
            wrapMode: Text.Wrap
        }

        Item { Layout.fillHeight: true }
    }
}
