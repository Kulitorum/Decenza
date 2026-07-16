import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: visualizerTab
    textFields: [usernameField, passwordField]
    targetFlickable: visualizerLeftFlick

    // Connection test result message
    property string testResultMessage: ""
    property bool testResultSuccess: false

    // --- Recover shots from Visualizer (date-range history import) ---
    // Visualizer is "connected" when both credentials are present.
    readonly property bool visualizerConnected:
        Settings.visualizer.visualizerUsername.length > 0 &&
        Settings.visualizer.visualizerPassword.length > 0

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
            visualizerTab.recoverStatusError = false
            if (total === 0) {
                visualizerTab.recoverStatus = TranslationManager.translate(
                    "settings.visualizer.recoverNothing",
                    "No shots found in that date range.")
            } else {
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

    RowLayout {
        width: parent.width
        height: parent.height
        spacing: Theme.scaled(15)

        // Account settings
        Rectangle {
            objectName: "visualizer"
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius

            // Scrollable so the account card stays usable on short screens —
            // same pattern as SettingsAITab.
            Flickable {
                id: visualizerLeftFlick
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                contentHeight: visualizerLeftColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: visualizerLeftColumn
                    width: visualizerLeftFlick.width
                    spacing: Theme.scaled(12)

                    Tr {
                        key: "settings.visualizer.account"
                        fallback: "Visualizer.coffee Account"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.visualizer.accountDesc"
                        fallback: "Upload your shots to visualizer.coffee for tracking and analysis"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Item { height: 5 }

                    // Username
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.visualizer.username"
                            fallback: "Username / Email"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        StyledTextField {
                            id: usernameField
                            Layout.fillWidth: true
                            text: Settings.visualizer.visualizerUsername
                            placeholder: TranslationManager.translate("settings.visualizer.username", "Username / Email")
                            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                            onTextChanged: Settings.visualizer.visualizerUsername = text
                            // Enter jumps to password field
                            Keys.onReturnPressed: function(event) { passwordField.forceActiveFocus() }
                            Keys.onEnterPressed: function(event) { passwordField.forceActiveFocus() }
                        }
                    }

                    // Password
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.visualizer.password"
                            fallback: "Password"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        StyledTextField {
                            id: passwordField
                            Layout.fillWidth: true
                            text: Settings.visualizer.visualizerPassword
                            echoMode: TextInput.Password
                            placeholder: TranslationManager.translate("settings.visualizer.password", "Password")
                            inputMethodHints: Qt.ImhNoAutoUppercase
                            onTextChanged: Settings.visualizer.visualizerPassword = text
                        }
                    }

                    Item { height: 5 }

                    // Test connection button
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        AccessibleButton {
                            text: TranslationManager.translate("settings.visualizer.testConnection", "Test Connection")
                            accessibleName: TranslationManager.translate("visualizer.testConnection", "Test Visualizer connection")
                            primary: true
                            enabled: usernameField.text.length > 0 && passwordField.text.length > 0
                            onClicked: {
                                visualizerTab.testResultMessage = TranslationManager.translate("settings.visualizer.testing", "Testing...")
                                MainController.visualizer.testConnection()
                            }
                        }

                        Text {
                            text: visualizerTab.testResultMessage
                            color: visualizerTab.testResultSuccess ? Theme.successColor : Theme.errorColor
                            font.pixelSize: Theme.scaled(12)
                            visible: visualizerTab.testResultMessage.length > 0
                        }
                    }

                    Connections {
                        target: MainController.visualizer
                        function onConnectionTestResult(success, message) {
                            visualizerTab.testResultSuccess = success
                            visualizerTab.testResultMessage = message
                        }
                    }

                    // Sign up link — kept with the Visualizer account block.
                    Tr {
                        id: signUpLink
                        key: "settings.visualizer.signUp"
                        fallback: "Don't have an account? Sign up at visualizer.coffee"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: "Sign up at visualizer.coffee. Opens web browser"
                            accessibleItem: signUpLink
                            onAccessibleClicked: Qt.openUrlExternally("https://visualizer.coffee/users/sign_up")
                        }
                    }

                }
            }
        }

        // Upload settings
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.cardBackgroundColor
            radius: Theme.cardRadius

            // Scrollable: this card holds upload + backup + the Recover-shots
            // section, which together overflow a tablet's height. Without a
            // Flickable the bottom (recovery) was clipped and unreachable —
            // matches the left account card's scroll pattern.
            Flickable {
                id: visualizerRightFlick
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                contentWidth: width
                contentHeight: visualizerRightColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: visualizerRightColumn
                    width: visualizerRightFlick.width
                    spacing: Theme.scaled(12)

                Tr {
                    key: "settings.visualizer.uploadSettings"
                    fallback: "Upload Settings"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                // Auto-upload toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.autoUpload"
                            fallback: "Auto-Upload Shots"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.autoUploadDesc"
                            fallback: "Automatically upload espresso shots after completion"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizer.visualizerAutoUpload
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpload", "Auto-upload shots")
                        onToggled: Settings.visualizer.visualizerAutoUpload = checked
                    }
                }

                // Auto-update shots toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    enabled: Settings.visualizer.visualizerAutoUpload
                    opacity: Settings.visualizer.visualizerAutoUpload ? 1.0 : 0.4

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.autoUpdate"
                            fallback: "Auto-Update Shots"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.autoUpdateDesc"
                            fallback: "Automatically sync shot edits back to Visualizer"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizer.visualizerAutoUpdate
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpdate", "Auto-update shots")
                        onToggled: Settings.visualizer.visualizerAutoUpdate = checked
                    }
                }

                // Minimum duration
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.minDuration"
                            fallback: "Minimum Duration"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.minDurationDesc"
                            fallback: "Only upload shots longer than this (skip aborted shots)"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: minDurationInput
                        value: Settings.visualizer.visualizerMinDuration
                        from: 0
                        to: 30
                        stepSize: 1
                        suffix: " sec"
                        accessibleName: TranslationManager.translate("settings.visualizer.minUploadDuration", "Minimum upload duration")

                        onValueModified: function(newValue) {
                            Settings.visualizer.visualizerMinDuration = newValue
                        }
                    }
                }

                Item { height: 10 }

                // Show after shot toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.editAfterShot"
                            fallback: "Edit After Shot"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.editAfterShotDesc"
                            fallback: "Open Shot Info page after each espresso extraction"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizer.visualizerShowAfterShot
                        accessibleName: TranslationManager.translate("settings.visualizer.editAfterShot", "Edit After Shot")
                        onToggled: Settings.visualizer.visualizerShowAfterShot = checked
                    }
                }

                // Clear notes on shot start toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.clearNotesOnStart"
                            fallback: "Clear Notes on Start"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.clearNotesOnStartDesc"
                            fallback: "Clear shot notes when starting a new shot"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizer.visualizerClearNotesOnStart
                        accessibleName: TranslationManager.translate("settings.visualizer.clearNotesOnStart", "Clear Notes on Start")
                        onToggled: Settings.visualizer.visualizerClearNotesOnStart = checked
                    }
                }

                // Default shot rating
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        spacing: Theme.scaled(2)
                        Layout.fillWidth: true

                        Tr {
                            key: "settings.visualizer.defaultRating"
                            fallback: "Default Shot Rating"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "settings.visualizer.defaultRatingDesc"
                            fallback: "Starting rating for new shots (0 = unrated)"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                            wrapMode: Text.WordWrap
                        }
                    }

                    RatingInput {
                        id: defaultRatingInput
                        Layout.preferredWidth: Theme.scaled(220)
                        height: Theme.scaled(40)
                        compact: true
                        value: Settings.visualizer.defaultShotRating
                        accessibleName: TranslationManager.translate("settings.visualizer.defaultRating", "Default Shot Rating")

                        onValueModified: function(newValue) {
                            Settings.visualizer.defaultShotRating = newValue
                        }
                    }
                }

                // Divider before the recovery section
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(6)
                    height: 1
                    color: Theme.borderColor
                }

                // --- Recover shots from Visualizer ---
                Tr {
                    key: "settings.visualizer.recoverTitle"
                    fallback: "Recover Shots from Visualizer"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.visualizer.recoverDesc"
                    fallback: "Import your uploaded shot history back into this device for a date range. Shots you already have are skipped."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                // From / To date pickers
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.visualizer.recoverFrom"
                            fallback: "From"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            enabled: !MainController.visualizerImporter.recovering
                            text: visualizerTab.recoverFromDate
                            accessibleName: TranslationManager.translate(
                                "settings.visualizer.recoverFromPick",
                                "Recovery start date. Currently %1")
                                .arg(visualizerTab.recoverFromDate)
                            onClicked: recoverFromPicker.openWithDate(visualizerTab.recoverFromDate)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.visualizer.recoverTo"
                            fallback: "To"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        AccessibleButton {
                            Layout.fillWidth: true
                            enabled: !MainController.visualizerImporter.recovering
                            text: visualizerTab.recoverToDate
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
                    spacing: Theme.scaled(10)

                    AccessibleButton {
                        text: MainController.visualizerImporter.recovering
                            ? TranslationManager.translate("settings.visualizer.recovering", "Retrieving…")
                            : TranslationManager.translate("settings.visualizer.recoverButton", "Retrieve")
                        accessibleName: TranslationManager.translate(
                            "settings.visualizer.recoverButton", "Retrieve shots from Visualizer")
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
                        font.pixelSize: Theme.scaled(12)
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
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                    visible: !visualizerTab.visualizerConnected
                }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
