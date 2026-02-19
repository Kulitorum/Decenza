import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

KeyboardAwareContainer {
    id: visualizerTab
    textFields: [usernameField, passwordField]

    // Connection test result message
    property string testResultMessage: ""
    property bool testResultSuccess: false

    RowLayout {
        width: parent.width
        height: parent.height
        spacing: Theme.scaled(15)

        // Account settings
        Rectangle {
            Layout.preferredWidth: Theme.scaled(350)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
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

                    TextField {
                        id: usernameField
                        Layout.fillWidth: true
                        text: Settings.visualizerUsername
                        font: Theme.bodyFont
                        color: Theme.textColor
                        placeholderTextColor: Theme.textSecondaryColor
                        inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                        topPadding: Theme.scaled(12)
                        bottomPadding: Theme.scaled(12)
                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: usernameField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1
                        }
                        onTextChanged: Settings.visualizerUsername = text
                        // Enter jumps to password field
                        onAccepted: passwordField.forceActiveFocus()
                        Keys.onReturnPressed: passwordField.forceActiveFocus()
                        Keys.onEnterPressed: passwordField.forceActiveFocus()
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

                    TextField {
                        id: passwordField
                        Layout.fillWidth: true
                        text: Settings.visualizerPassword
                        echoMode: TextInput.Password
                        font: Theme.bodyFont
                        color: Theme.textColor
                        placeholderTextColor: Theme.textSecondaryColor
                        inputMethodHints: Qt.ImhNoAutoUppercase
                        leftPadding: Theme.scaled(12)
                        rightPadding: Theme.scaled(12)
                        topPadding: Theme.scaled(12)
                        bottomPadding: Theme.scaled(12)
                        background: Rectangle {
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: passwordField.activeFocus ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1
                        }
                        onTextChanged: Settings.visualizerPassword = text
                        // Enter dismisses keyboard
                        onAccepted: {
                            passwordField.focus = false
                            Qt.inputMethod.hide()
                        }
                        Keys.onReturnPressed: { passwordField.focus = false; Qt.inputMethod.hide() }
                        Keys.onEnterPressed: { passwordField.focus = false; Qt.inputMethod.hide() }
                    }
                }

                Item { height: 5 }

                // Test connection button
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    AccessibleButton {
                        text: TranslationManager.translate("settings.visualizer.testConnection", "Test Connection")
                        accessibleName: "Test Visualizer connection"
                        enabled: usernameField.text.length > 0 && passwordField.text.length > 0
                        onClicked: {
                            visualizerTab.testResultMessage = TranslationManager.translate("settings.visualizer.testing", "Testing...")
                            MainController.visualizer.testConnection()
                        }
                        background: Rectangle {
                            implicitWidth: Theme.scaled(140)
                            implicitHeight: Theme.scaled(40)
                            radius: Theme.scaled(6)
                            color: parent.enabled ? Theme.primaryColor : Theme.backgroundColor
                            border.color: parent.enabled ? Theme.primaryColor : Theme.textSecondaryColor
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? "white" : Theme.textSecondaryColor
                            font: Theme.bodyFont
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
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

                Item { Layout.fillHeight: true }

                // Sign up link
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
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.autoUpload"
                            fallback: "Auto-Upload Shots"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            key: "settings.visualizer.autoUploadDesc"
                            fallback: "Automatically upload espresso shots after completion"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizerAutoUpload
                        accessibleName: TranslationManager.translate("settings.visualizer.autoUpload", "Auto-upload shots")
                        onCheckedChanged: Settings.visualizerAutoUpload = checked
                    }
                }

                // Minimum duration
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.minDuration"
                            fallback: "Minimum Duration"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            key: "settings.visualizer.minDurationDesc"
                            fallback: "Only upload shots longer than this (skip aborted shots)"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        id: minDurationInput
                        value: Settings.visualizerMinDuration
                        from: 0
                        to: 30
                        stepSize: 1
                        suffix: " sec"
                        accessibleName: TranslationManager.translate("settings.visualizer.minUploadDuration", "Minimum upload duration")

                        onValueModified: function(newValue) {
                            Settings.visualizerMinDuration = newValue
                        }
                    }
                }

                Item { height: 10 }

                // Show after shot toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.editAfterShot"
                            fallback: "Edit After Shot"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            key: "settings.visualizer.editAfterShotDesc"
                            fallback: "Open Shot Info page after each espresso extraction"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizerShowAfterShot
                        accessibleName: TranslationManager.translate("settings.visualizer.editAfterShot", "Edit After Shot")
                        onCheckedChanged: Settings.visualizerShowAfterShot = checked
                    }
                }

                // Clear notes on shot start toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.clearNotesOnStart"
                            fallback: "Clear Notes on Start"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            key: "settings.visualizer.clearNotesOnStartDesc"
                            fallback: "Clear shot notes when starting a new shot"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizerClearNotesOnStart
                        accessibleName: TranslationManager.translate("settings.visualizer.clearNotesOnStart", "Clear Notes on Start")
                        onCheckedChanged: Settings.visualizerClearNotesOnStart = checked
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
                        value: Settings.defaultShotRating
                        accessibleName: TranslationManager.translate("settings.visualizer.defaultRating", "Default Shot Rating")

                        onValueModified: function(newValue) {
                            Settings.defaultShotRating = newValue
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
