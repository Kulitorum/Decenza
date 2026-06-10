import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: visualizerTab
    textFields: [usernameField, passwordField, beanBaseKeyField]
    targetFlickable: visualizerLeftFlick

    // Connection test result message
    property string testResultMessage: ""
    property bool testResultSuccess: false

    // Bean Base (Loffee Labs) API-key test result
    property string beanBaseTestMessage: ""
    property bool beanBaseTestSuccess: false
    property bool showBeanBaseKey: false

    RowLayout {
        width: parent.width
        height: parent.height
        spacing: Theme.scaled(15)

        // Account settings
        Rectangle {
            objectName: "visualizer"
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            // Scrollable: the card grew past short screens when the Bean Base
            // section was added — same pattern as SettingsAITab.
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

                    // Sign up link — kept with the Visualizer account block,
                    // above the Bean Base divider.
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

                    // ───────────────────────────────────────────
                    // Bean Base (Loffee Labs) section
                    // ───────────────────────────────────────────
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(8)
                        height: 1
                        color: Theme.borderColor
                    }

                    Tr {
                        key: "settings.beanbase.section"
                        fallback: "Bean Base"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.beanbase.description"
                        fallback: "Look up coffee details and link your shots to a global coffee database."
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Item { height: 5 }

                    // API key
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        RowLayout {
                            Layout.fillWidth: true

                            Tr {
                                key: "settings.beanbase.apiKey"
                                fallback: "API Key"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                            }

                            Item { Layout.fillWidth: true }

                            // Show / hide toggle (text, not a glyph icon — CLAUDE.md rule)
                            Tr {
                                id: beanBaseShowToggle
                                key: visualizerTab.showBeanBaseKey ? "common.button.hide" : "common.button.show"
                                fallback: visualizerTab.showBeanBaseKey ? "Hide" : "Show"
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(12)
                                visible: beanBaseKeyField.text.length > 0

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: visualizerTab.showBeanBaseKey
                                        ? TranslationManager.translate("settings.beanbase.hideKey", "Hide API key")
                                        : TranslationManager.translate("settings.beanbase.showKey", "Show API key")
                                    accessibleItem: beanBaseShowToggle
                                    onAccessibleClicked: visualizerTab.showBeanBaseKey = !visualizerTab.showBeanBaseKey
                                }
                            }
                        }

                        StyledTextField {
                            id: beanBaseKeyField
                            Layout.fillWidth: true
                            text: Settings.beanbase.beanBaseApiKey
                            echoMode: visualizerTab.showBeanBaseKey ? TextInput.Normal : TextInput.Password
                            placeholder: TranslationManager.translate("settings.beanbase.apiKeyPlaceholder", "Paste your API key")
                            accessibleName: TranslationManager.translate("settings.beanbase.apiKey", "API Key")
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                            onTextChanged: {
                                if (Settings.beanbase.beanBaseApiKey !== text)
                                    Settings.beanbase.beanBaseApiKey = text
                                // Clear a stale test result once the key is edited.
                                visualizerTab.beanBaseTestMessage = ""
                            }
                        }
                    }

                    Item { height: 5 }

                    // Test key button + result
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(10)

                        AccessibleButton {
                            text: TranslationManager.translate("settings.beanbase.testKey", "Test Key")
                            accessibleName: TranslationManager.translate("settings.beanbase.testKey", "Test Bean Base API key")
                            primary: true
                            enabled: beanBaseKeyField.text.length > 0
                            onClicked: {
                                visualizerTab.beanBaseTestSuccess = false
                                visualizerTab.beanBaseTestMessage =
                                    TranslationManager.translate("settings.beanbase.testing", "Testing...")
                                MainController.beanbase.testApiKey()
                            }
                        }

                        Text {
                            text: visualizerTab.beanBaseTestMessage
                            color: visualizerTab.beanBaseTestSuccess ? Theme.successColor : Theme.errorColor
                            font.pixelSize: Theme.scaled(12)
                            visible: visualizerTab.beanBaseTestMessage.length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    Connections {
                        target: MainController.beanbase
                        function onApiKeyTestResult(success, message) {
                            visualizerTab.beanBaseTestSuccess = success
                            if (success) {
                                visualizerTab.beanBaseTestMessage =
                                    TranslationManager.translate("settings.beanbase.testSuccess", "API key is valid")
                            } else if (message === "invalid") {
                                visualizerTab.beanBaseTestMessage =
                                    TranslationManager.translate("settings.beanbase.testInvalid", "Invalid API key")
                            } else if (message === "ratelimited") {
                                visualizerTab.beanBaseTestMessage =
                                    TranslationManager.translate("settings.beanbase.testRateLimited", "Rate limit reached — try again shortly")
                            } else {
                                visualizerTab.beanBaseTestMessage =
                                    TranslationManager.translate("settings.beanbase.testNetworkError", "Could not reach Bean Base")
                            }
                        }
                    }

                    // Signup link
                    Tr {
                        id: beanBaseSignupLink
                        key: "settings.beanbase.signupLink"
                        fallback: "Get a free API key from loffeelabs.com"
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(12)
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: "Get a free API key from loffeelabs.com. Opens web browser"
                            accessibleItem: beanBaseSignupLink
                            onAccessibleClicked: Qt.openUrlExternally("https://loffeelabs.com/developers")
                        }
                    }

                }
            }
        }

        // Upload settings
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
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

                Item { Layout.fillHeight: true }
            }
        }
    }
}
