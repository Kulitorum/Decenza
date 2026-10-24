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
    readonly property var shotUploadSnapshot: ShotUploadService.snapshot || ({})
    readonly property var visualizerState: shotUploadSnapshot.visualizer || ({})
    readonly property var decentState: shotUploadSnapshot.decent || ({})
    readonly property var uploadPreferences: SettingsPreferencesService.preferences
    property string visualizerLinkRequestId: ""
    property string decentLinkRequestId: ""
    property string decentAccountUrlRequestId: ""
    property string decentAccountUrlReservationId: ""
    property string recoveryRequestId: ""
    property bool recoveryOutcomeUnknown: false
    readonly property bool visualizerConnected: visualizerState.connected === true

    function uploadPreference(name, fallback) {
        const value = uploadPreferences["upload." + name]
        return value === undefined ? fallback : value
    }

    function visualizerPreference(name, fallback) {
        const value = uploadPreferences["visualizer." + name]
        return value === undefined ? fallback : value
    }

    function preferenceWriter(name) {
        if (name === "visualizerEnabled") return visualizerEnabledWrite
        if (name === "decentEnabled") return decentEnabledWrite
        if (name === "autoUpload") return autoUploadWrite
        if (name === "autoUpdate") return autoUpdateWrite
        return minDurationWrite
    }

    function writeUploadPreference(name, value) {
        if (!preferenceWriter(name).submit(name, value))
            preferenceError.show(TranslationManager.translate(
                "common.settingsNotReady", "Settings are not connected yet."))
    }

    function writeVisualizerPreference(name, value) {
        const writer = name === "communitySearchEnabled"
                ? communitySearchWrite : communityMinEnjoymentWrite
        if (!writer.submit(name, value))
            preferenceError.show(TranslationManager.translate(
                "common.settingsNotReady", "Settings are not connected yet."))
    }

    function command(operation, args) {
        return ShotUploadService.execute(operation, args || ({}))
    }

    PreferenceWrite {
        id: visualizerEnabledWrite
        domain: "upload"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: decentEnabledWrite
        domain: "upload"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: autoUploadWrite
        domain: "upload"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: autoUpdateWrite
        domain: "upload"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: minDurationWrite
        domain: "upload"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: communitySearchWrite
        domain: "visualizer"
        onFailed: function(message) { preferenceError.show(message) }
    }
    PreferenceWrite {
        id: communityMinEnjoymentWrite
        domain: "visualizer"
        onFailed: function(message) { preferenceError.show(message) }
    }
    StatusToast { id: preferenceError; error: true }
    ExternalUrlOpener { id: externalUrlOpener }

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
        target: ShotUploadService
        function onStateChanged() {
            if (!visualizerTab.recoveryOutcomeUnknown || !ShotUploadService.ready) return
            const recovering = (ShotUploadService.snapshot.importer || {}).recovering === true
            visualizerTab.recoverStatusError = false
            visualizerTab.recoverStatus = recovering
                ? TranslationManager.translate("settings.visualizer.recoveryContinues",
                    "Recovery is still running on the host; its final result is unknown here.")
                : TranslationManager.translate("settings.visualizer.recoveryUnknownFinished",
                    "Recovery finished while disconnected. Check your history before retrying.")
        }
        function onRecoveryProgress(requestId, total, imported, skipped, failed) {
            if (requestId !== visualizerTab.recoveryRequestId) return
            visualizerTab.recoveryOutcomeUnknown = false
            visualizerTab.recoverStatusError = false
            visualizerTab.recoverStatus = TranslationManager.translate(
                "settings.visualizer.recoverProgress",
                "Imported %1 of %2 — %3 skipped (already have), %4 failed")
                .arg(imported).arg(total).arg(skipped).arg(failed)
        }
        function onRecoveryComplete(requestId, total, imported, skipped, failed) {
            if (requestId !== visualizerTab.recoveryRequestId) return
            visualizerTab.recoveryRequestId = ""
            visualizerTab.recoveryOutcomeUnknown = false
            if (total === 0) {
                visualizerTab.recoverStatusError = false
                visualizerTab.recoverStatus = TranslationManager.translate(
                    "settings.visualizer.recoverNothing", "No shots found in that date range.")
            } else {
                visualizerTab.recoverStatusError = failed > 0
                visualizerTab.recoverStatus = TranslationManager.translate(
                    "settings.visualizer.recoverDone",
                    "Done: %1 imported, %2 already present, %3 failed (of %4).")
                    .arg(imported).arg(skipped).arg(failed).arg(total)
            }
        }
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === visualizerTab.visualizerLinkRequestId) {
                visualizerTab.visualizerLinkRequestId = ""
                visualizerAccount.connectFinished(result.errorCode ?? AccountLink.Error.ServerError)
            } else if (requestId === visualizerTab.decentLinkRequestId) {
                visualizerTab.decentLinkRequestId = ""
                decentAccount.connectFinished(result.errorCode ?? AccountLink.Error.ServerError)
            } else if (requestId === visualizerTab.decentAccountUrlRequestId) {
                visualizerTab.decentAccountUrlRequestId = ""
                const reservationId = visualizerTab.decentAccountUrlReservationId
                visualizerTab.decentAccountUrlReservationId = ""
                if (success && result.url && externalUrlOpener.openReserved(reservationId, result.url)) {
                    // The window was reserved in the original click gesture and
                    // is now navigating to the host-validated authenticated URL.
                } else {
                    externalUrlOpener.cancel(reservationId)
                    preferenceError.show(TranslationManager.translate(
                        "decent.account.openFailed", "Could not open your Decent account page."))
                }
            } else if (requestId === visualizerTab.recoveryRequestId && !success) {
                visualizerTab.recoveryRequestId = ""
                if (reason === "disconnected") {
                    visualizerTab.recoveryOutcomeUnknown = true
                    visualizerTab.recoverStatusError = false
                    visualizerTab.recoverStatus = TranslationManager.translate(
                        "settings.visualizer.recoveryUnknown",
                        "Connection lost during recovery. The host may still be working; check history before retrying.")
                } else {
                    visualizerTab.recoveryOutcomeUnknown = false
                    visualizerTab.recoverStatusError = true
                    visualizerTab.recoverStatus = result.error || TranslationManager.translate(
                        "settings.visualizer.recoverFailed", "Could not retrieve shots.")
                }
            }
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
                switchedOn: visualizerEnabledWrite.displayedValue("visualizerEnabled", visualizerTab.uploadPreference("visualizerEnabled", false))
                onSwitchToggled: function(on) { visualizerTab.writeUploadPreference("visualizerEnabled", on) }
                statusText: visualizerAccount.statusText
                statusColor: visualizerAccount.statusColor

                UploadAccountSection {
                    id: visualizerAccount
                    SettingsSearch.title: TranslationManager.translate("settings.search.visualizerAccount", "Visualizer login")
                    Layout.fillWidth: true
                    identityLabel: TranslationManager.translate("settings.visualizer.username", "Username / Email")
                    accessibleAccountName: TranslationManager.translate("settings.upload.visualizerTitle", "Visualizer")
                    connected: visualizerTab.visualizerConnected
                    connectedName: visualizerTab.visualizerState.username || ""
                    busy: visualizerTab.visualizerState.busy === true
                    onConnectRequested: function(identity, password) {
                        visualizerTab.visualizerLinkRequestId = visualizerTab.command(
                            "connectVisualizer", { identity: identity, password: password })
                    }
                    onDisconnectRequested: visualizerTab.command("disconnectVisualizer")
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
                            enabled: (visualizerTab.shotUploadSnapshot.importer || {}).recovering !== true
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
                            enabled: (visualizerTab.shotUploadSnapshot.importer || {}).recovering !== true
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
                        text: (visualizerTab.shotUploadSnapshot.importer || {}).recovering === true
                            ? TranslationManager.translate("settings.visualizer.recovering", "Retrieving…")
                            : TranslationManager.translate("settings.visualizer.recoverButton", "Retrieve")
                        accessibleName: TranslationManager.translate(
                            "settings.visualizer.recoverButtonA11y", "Retrieve shots from Visualizer")
                        primary: true
                        enabled: visualizerTab.visualizerConnected &&
                                 (visualizerTab.shotUploadSnapshot.importer || {}).recovering !== true
                        onClicked: {
                            visualizerTab.recoveryOutcomeUnknown = false
                            visualizerTab.recoverStatusError = false
                            visualizerTab.recoverStatus = TranslationManager.translate(
                                "settings.visualizer.recoverStarting", "Looking up your shots…")
                            visualizerTab.recoveryRequestId = visualizerTab.command(
                                "recoverVisualizer", {
                                    fromEpoch: visualizerTab.recoverDateToEpoch(visualizerTab.recoverFromDate, false),
                                    toEpoch: visualizerTab.recoverDateToEpoch(visualizerTab.recoverToDate, true)
                                })
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
                switchedOn: decentEnabledWrite.displayedValue("decentEnabled", visualizerTab.uploadPreference("decentEnabled", false))
                onSwitchToggled: function(on) { visualizerTab.writeUploadPreference("decentEnabled", on) }
                statusText: decentAccount.statusText
                statusColor: decentAccount.statusColor

                UploadAccountSection {
                    id: decentAccount
                    SettingsSearch.title: TranslationManager.translate("settings.search.decentAccount", "Decent account login")
                    Layout.fillWidth: true
                    identityLabel: TranslationManager.translate("settings.upload.account.email", "Email")
                    accessibleAccountName: TranslationManager.translate("decent.account.title", "Decent Account")
                    connected: visualizerTab.decentState.state === "linked"
                    needsSignIn: visualizerTab.decentState.state === "needsSignIn"
                    connectedName: visualizerTab.decentState.email || ""
                    busy: visualizerTab.decentState.busy === true
                    onConnectRequested: function(identity, password) {
                        const requestId = visualizerTab.command(
                            "linkDecent", { email: identity, password: password })
                        visualizerTab.decentLinkRequestId = requestId
                    }
                    onDisconnectRequested: visualizerTab.command("unlinkDecent")
                }

                Tr {
                    id: viewShotsLink
                    Layout.fillWidth: true
                    visible: decentAccount.connected
                    enabled: visualizerTab.decentAccountUrlRequestId.length === 0
                    opacity: enabled ? 1 : 0.5
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
                        onAccessibleClicked: {
                            if (visualizerTab.decentAccountUrlRequestId.length > 0)
                                return
                            const reservationId = externalUrlOpener.reserve()
                            if (!reservationId) {
                                preferenceError.show(TranslationManager.translate(
                                    "common.popupBlocked",
                                    "Your browser blocked the new tab. Allow pop-ups for Decenza and try again."))
                                return
                            }
                            visualizerTab.decentAccountUrlReservationId = reservationId
                            visualizerTab.decentAccountUrlRequestId =
                                visualizerTab.command("openDecentAccount")
                        }
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
                        checked: autoUploadWrite.displayedValue("autoUpload", visualizerTab.uploadPreference("autoUpload", true))
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpload", "Auto-upload shots")
                        onToggled: visualizerTab.writeUploadPreference("autoUpload", checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    enabled: visualizerTab.uploadPreference("autoUpload", true)
                    opacity: visualizerTab.uploadPreference("autoUpload", true) ? 1.0 : 0.4

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
                        checked: autoUpdateWrite.displayedValue("autoUpdate", visualizerTab.uploadPreference("autoUpdate", true))
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpdate", "Auto-update shots")
                        onToggled: visualizerTab.writeUploadPreference("autoUpdate", checked)
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
                        value: minDurationWrite.displayedValue("minDuration", visualizerTab.uploadPreference("minDuration", 6.0))
                        from: 0
                        to: 30
                        stepSize: 1
                        suffix: " sec"
                        accessibleName: TranslationManager.translate("settings.visualizer.minUploadDuration", "Minimum upload duration")
                        onValueModified: function(newValue) { visualizerTab.writeUploadPreference("minDuration", newValue) }
                    }
                }
            }

            SettingsCard {
                searchId: "communitySuggestions"
                title: TranslationManager.translate("settings.visualizer.community.title", "Community suggestions")
                description: TranslationManager.translate("settings.visualizer.community.desc",
                    "What the bean page counts as a shot people liked")
                keywords: ["community", "bean", "suggestion", "enjoyment", "rating", "liked", "search"]
                showHeader: false
                contentMargins: Theme.spacingMedium
                spacing: Theme.spacingMedium
                Layout.columnSpan: uploadGrid.columns

                Tr {
                    key: "settings.visualizer.community.title"
                    fallback: "Community suggestions"
                    color: Theme.textColor
                    font: Theme.subtitleFont
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 4
                        Tr {
                            key: "settings.visualizer.community.search"
                            fallback: "Search the community"
                            color: Theme.textColor
                            font: Theme.labelFont
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.community.searchDesc"
                            fallback: "Look up Visualizer community shots for a coffee when you open its suggestions"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: communitySearchWrite.displayedValue(
                                     "communitySearchEnabled",
                                     visualizerTab.visualizerPreference("communitySearchEnabled", true))
                        accessibleName: TranslationManager.translate("settings.visualizer.community.search", "Search the community")
                        onToggled: visualizerTab.writeVisualizerPreference("communitySearchEnabled", checked)
                    }
                }

                RowLayout {
                    enabled: visualizerTab.visualizerPreference("communitySearchEnabled", true)
                    opacity: enabled ? 1.0 : 0.4
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall / 4
                        Tr {
                            key: "settings.visualizer.community.minEnjoyment"
                            fallback: "Minimum enjoyment"
                            color: Theme.textColor
                            font: Theme.labelFont
                        }
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.community.minEnjoymentDesc"
                            fallback: "Only community shots rated at least this pick the suggested profile"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    ValueInput {
                        value: communityMinEnjoymentWrite.displayedValue(
                                   "communityMinEnjoyment",
                                   visualizerTab.visualizerPreference("communityMinEnjoyment", 70))
                        from: 0
                        to: 100
                        stepSize: 5
                        accessibleName: TranslationManager.translate("settings.visualizer.community.minEnjoyment", "Minimum enjoyment")
                        onValueModified: function(newValue) {
                            visualizerTab.writeVisualizerPreference("communityMinEnjoyment", newValue)
                        }
                    }
                }
            }
        }
    }
}
