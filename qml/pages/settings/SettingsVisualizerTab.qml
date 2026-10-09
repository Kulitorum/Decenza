import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Settings → Shot Upload: a card per destination (switch + account), then one
// Upload settings card shared by both.
KeyboardAwareContainer {
    id: visualizerTab
    textFields: [visualizerAccount.emailField, visualizerAccount.passwordField,
                 decentAccount.emailField, decentAccount.passwordField]
    targetFlickable: uploadFlick

    readonly property bool wide: width >= Theme.scaled(700)

    // --- Recover shots from Visualizer (date-range history import) ---
    readonly property bool visualizerConnected: Settings.visualizer.visualizerConnected

    // Format a JS Date as a local YYYY-MM-DD string.
    function jsDateToIso(d) {
        var mm = String(d.getMonth() + 1).padStart(2, '0')
        var dd = String(d.getDate()).padStart(2, '0')
        return d.getFullYear() + "-" + mm + "-" + dd
    }

    // Selected range as YYYY-MM-DD strings (defaults: 30 days ago .. today).
    property string recoverFromDate: jsDateToIso(
        new Date(Date.now() - 30 * 24 * 60 * 60 * 1000))
    property string recoverToDate: jsDateToIso(new Date())

    // Progress line shown while / after a recovery runs.
    property string recoverStatus: ""
    property bool recoverStatusError: false

    // Convert a YYYY-MM-DD string to Unix seconds. `endOfDay` pushes to
    // 23:59:59 local so the "To" bound is inclusive of the whole day.
    function recoverDateToEpoch(dateStr, endOfDay) {
        var parts = dateStr.split("-")
        if (parts.length !== 3) return 0
        var d = new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]),
                         endOfDay ? 23 : 0, endOfDay ? 59 : 0, endOfDay ? 59 : 0)
        return Math.floor(d.getTime() / 1000)
    }

    Connections {
        target: MainController.visualizerImporter
        function onRecoveryProgress(total, imported, skipped, failed) {
            visualizerTab.recoverStatusError = false
            visualizerTab.recoverStatus = TranslationManager.translate(
                "settings.visualizer.recoverProgress",
                "Imported %1 of %2 — %3 skipped (already have), %4 failed")
                .arg(imported).arg(total).arg(skipped).arg(failed)
        }
        function onRecoveryComplete(total, imported, skipped, failed) {
            if (total === 0) {
                visualizerTab.recoverStatusError = false
                visualizerTab.recoverStatus = TranslationManager.translate(
                    "settings.visualizer.recoverNothing",
                    "No shots found in that date range.")
            } else {
                // Style as an error when nothing came through but shots were
                // found — otherwise a wholesale failure (schema change, a run of
                // server errors) reads identically to a clean success. Any
                // non-zero failure count is worth flagging too.
                visualizerTab.recoverStatusError = failed > 0
                visualizerTab.recoverStatus = TranslationManager.translate(
                    "settings.visualizer.recoverDone",
                    "Done: %1 imported, %2 already present, %3 failed (of %4).")
                    .arg(imported).arg(skipped).arg(failed).arg(total)
            }
        }
        function onRecoveryFailed(error) {
            visualizerTab.recoverStatusError = true
            visualizerTab.recoverStatus = error
        }
    }

    DatePickerDialog {
        id: recoverFromPicker
        onDateSelected: function(dateString) {
            if (dateString.length === 10) visualizerTab.recoverFromDate = dateString
        }
    }

    DatePickerDialog {
        id: recoverToPicker
        onDateSelected: function(dateString) {
            if (dateString.length === 10) visualizerTab.recoverToDate = dateString
        }
    }

    Flickable {
        id: uploadFlick
        anchors.fill: parent
        contentWidth: width
        contentHeight: uploadGrid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        GridLayout {
            id: uploadGrid
            width: uploadFlick.width
            columns: visualizerTab.wide ? 2 : 1
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingMedium

            UploadDestinationCard {
                searchId: "visualizer"
                keywords: ["visualizer", "coffee", "upload", "share", "account"]
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                title: TranslationManager.translate("settings.upload.visualizerTitle", "Visualizer")
                description: TranslationManager.translate("settings.visualizer.accountDesc",
                                                          "Upload your shots to visualizer.coffee for tracking and analysis")
                switchedOn: Settings.visualizer.visualizerEnabled
                onSwitchToggled: function(on) { Settings.visualizer.visualizerEnabled = on }
                statusText: visualizerAccount.statusText
                statusColor: visualizerAccount.statusColor

                UploadAccountSection {
                    id: visualizerAccount
                    SettingsSearch.title: TranslationManager.translate("settings.search.visualizerAccount", "Visualizer login")
                    Layout.fillWidth: true
                    identityLabel: TranslationManager.translate("settings.visualizer.username", "Username / Email")
                    accessibleAccountName: TranslationManager.translate("settings.upload.visualizerTitle", "Visualizer")
                    connected: visualizerTab.visualizerConnected
                    connectedName: Settings.visualizer.visualizerUsername
                    busy: MainController.visualizer.connecting
                    onConnectRequested: function(identity, password) { MainController.visualizer.connectAccount(identity, password) }
                    onDisconnectRequested: MainController.visualizer.disconnectAccount()

                    Connections {
                        target: MainController.visualizer
                        function onAccountConnectFinished(error) { visualizerAccount.connectFinished(error) }
                    }
                }

                // Sign up link — kept with the Visualizer account block.
                Tr {
                    id: signUpLink
                    key: "settings.visualizer.signUp"
                    fallback: "Don't have an account? Sign up at visualizer.coffee"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: TranslationManager.translate("settings.visualizer.accessible.signup", "Sign up at visualizer.coffee. Opens web browser")
                        accessibleItem: signUpLink
                        onAccessibleClicked: Qt.openUrlExternally("https://visualizer.coffee/users/sign_up")
                    }
                }

                UploadMissingShots {
                    Layout.fillWidth: true
                    SettingsSearch.title: TranslationManager.translate("settings.search.uploadMissingShots", "Upload missing shots")
                    destination: "visualizer"
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingSmall
                    Layout.preferredHeight: 1
                    color: Theme.borderColor
                }

                // --- Recover shots from Visualizer ---
                Tr {
                    key: "settings.visualizer.recoverTitle"
                    fallback: "Recover Shots from Visualizer"
                    color: Theme.textColor
                    font: Theme.subtitleFont
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.visualizer.recoverDesc"
                    fallback: "Import your uploaded shot history back into this device for a date range. Shots you already have are skipped."
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                }

                // From / To date pickers
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 2

                        Tr {
                            key: "settings.visualizer.recoverFrom"
                            fallback: "From"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            enabled: !MainController.visualizerImporter.recovering
                            text: visualizerTab.recoverFromDate
                            SettingsSearch.title: TranslationManager.translate("settings.search.recoverFrom", "Recovery start date")
                            accessibleName: TranslationManager.translate(
                                "settings.visualizer.recoverFromPick",
                                "Recovery start date. Currently %1")
                                .arg(visualizerTab.recoverFromDate)
                            onClicked: recoverFromPicker.openWithDate(visualizerTab.recoverFromDate)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 2

                        Tr {
                            key: "settings.visualizer.recoverTo"
                            fallback: "To"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            enabled: !MainController.visualizerImporter.recovering
                            text: visualizerTab.recoverToDate
                            SettingsSearch.title: TranslationManager.translate("settings.search.recoverTo", "Recovery end date")
                            accessibleName: TranslationManager.translate(
                                "settings.visualizer.recoverToPick",
                                "Recovery end date. Currently %1")
                                .arg(visualizerTab.recoverToDate)
                            onClicked: recoverToPicker.openWithDate(visualizerTab.recoverToDate)
                        }
                    }
                }

                // Retrieve button + progress line
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    AccessibleButton {
                        text: MainController.visualizerImporter.recovering
                            ? TranslationManager.translate("settings.visualizer.recovering", "Retrieving…")
                            : TranslationManager.translate("settings.visualizer.recoverButton", "Retrieve")
                        accessibleName: TranslationManager.translate(
                            "settings.visualizer.recoverButtonA11y", "Retrieve shots from Visualizer")
                        primary: true
                        enabled: visualizerTab.visualizerConnected &&
                                 !MainController.visualizerImporter.recovering
                        onClicked: {
                            visualizerTab.recoverStatusError = false
                            visualizerTab.recoverStatus = TranslationManager.translate(
                                "settings.visualizer.recoverStarting", "Looking up your shots…")
                            MainController.visualizerImporter.recoverShots(
                                visualizerTab.recoverDateToEpoch(visualizerTab.recoverFromDate, false),
                                visualizerTab.recoverDateToEpoch(visualizerTab.recoverToDate, true))
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: visualizerTab.recoverStatus
                        color: visualizerTab.recoverStatusError ? Theme.errorColor : Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                        visible: visualizerTab.recoverStatus.length > 0
                    }
                }

                // Hint shown when not connected
                Tr {
                    Layout.fillWidth: true
                    key: "settings.visualizer.recoverNotConnected"
                    fallback: "Connect your Visualizer account above to recover shots."
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                    visible: !visualizerTab.visualizerConnected
                }
            }

            UploadDestinationCard {
                searchId: "decentAccount"
                keywords: ["decent", "decentespresso", "account", "upload", "shots", "login"]
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                title: TranslationManager.translate("decent.account.title", "Decent Account")
                description: TranslationManager.translate("decent.account.desc",
                                                          "Upload your shots to your account at decentespresso.com")
                switchedOn: Settings.decent.enabled
                onSwitchToggled: function(on) { Settings.decent.enabled = on }
                statusText: decentAccount.statusText
                statusColor: decentAccount.statusColor

                UploadAccountSection {
                    id: decentAccount
                    SettingsSearch.title: TranslationManager.translate("settings.search.decentAccount", "Decent account login")
                    Layout.fillWidth: true
                    identityLabel: TranslationManager.translate("settings.upload.account.email", "Email")
                    accessibleAccountName: TranslationManager.translate("decent.account.title", "Decent Account")
                    connected: MainController.decentAccount.state === DecentAccount.State.Linked
                    needsSignIn: MainController.decentAccount.state === DecentAccount.State.NeedsSignIn
                    connectedName: MainController.decentAccount.email
                    busy: MainController.decentAccount.busy
                    onConnectRequested: function(identity, password) { MainController.decentAccount.link(identity, password) }
                    onDisconnectRequested: MainController.decentAccount.unlink()

                    Connections {
                        target: MainController.decentAccount
                        function onLinkFinished(error) { decentAccount.connectFinished(error) }
                    }
                }

                Tr {
                    id: viewShotsLink
                    Layout.fillWidth: true
                    visible: decentAccount.connected
                    key: "decent.account.viewShots"
                    fallback: "View my shots on decentespresso.com"
                    color: Theme.primaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: TranslationManager.translate("decent.account.viewShotsAccessible",
                                                                     "View my shots on decentespresso.com. Opens web browser")
                        accessibleItem: viewShotsLink
                        onAccessibleClicked: MainController.decentAccount.openAccountInBrowser()
                    }
                }

                DecentUploadStatus {
                    Layout.fillWidth: true
                }

                UploadMissingShots {
                    Layout.fillWidth: true
                    SettingsSearch.title: TranslationManager.translate("settings.search.uploadMissingShots", "Upload missing shots")
                    destination: "decent"
                    note: TranslationManager.translate("settings.upload.missing.decentNote",
                                                       "Shots go up under the DE1 that is connected when they are sent.")
                }
            }

            // Settings shared by every destination that is switched on.
            SettingsCard {
                searchId: "uploadSettings"
                title: TranslationManager.translate("settings.upload.settingsTitle", "Upload Settings")
                description: TranslationManager.translate("settings.upload.settingsDesc", "When shots are uploaded, for every destination that is switched on")
                keywords: ["upload", "auto", "automatic", "update", "minimum", "duration"]
                showHeader: false
                contentMargins: Theme.spacingMedium
                spacing: Theme.spacingMedium
                Layout.columnSpan: uploadGrid.columns

                Tr {
                    key: "settings.upload.settingsTitle"
                    fallback: "Upload Settings"
                    color: Theme.textColor
                    font: Theme.subtitleFont
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.upload.settingsDesc"
                    fallback: "When shots are uploaded, for every destination that is switched on"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 4
                        Tr {
                            key: "settings.visualizer.autoUpload"
                            fallback: "Auto-upload shots"
                            color: Theme.textColor
                            font: Theme.labelFont
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.autoUploadDesc"
                            fallback: "Automatically upload espresso shots after completion"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: Settings.upload.autoUpload
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpload", "Auto-upload shots")
                        onToggled: Settings.upload.autoUpload = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    enabled: Settings.upload.autoUpload
                    opacity: Settings.upload.autoUpload ? 1.0 : 0.4

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 4
                        Tr {
                            key: "settings.visualizer.autoUpdate"
                            fallback: "Auto-update shots"
                            color: Theme.textColor
                            font: Theme.labelFont
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.upload.autoUpdateTwoWayDesc"
                            fallback: "Re-send a shot after you edit it, and bring back edits made on Visualizer"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: Settings.upload.autoUpdate
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpdate", "Auto-update shots")
                        onToggled: Settings.upload.autoUpdate = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 4
                        Tr {
                            key: "settings.visualizer.minDuration"
                            fallback: "Minimum Duration"
                            color: Theme.textColor
                            font: Theme.labelFont
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.minDurationDesc"
                            fallback: "Only upload shots longer than this (skip aborted shots)"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    ValueInput {
                        value: Settings.upload.minDuration
                        from: 0
                        to: 30
                        stepSize: 1
                        suffix: " sec"
                        accessibleName: TranslationManager.translate("settings.visualizer.minUploadDuration", "Minimum upload duration")
                        onValueModified: function(newValue) { Settings.upload.minDuration = newValue }
                    }
                }
            }
        }
    }
}
