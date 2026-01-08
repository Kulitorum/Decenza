import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: visualizerTab

    // Connection test result message
    property string testResultMessage: ""
    property bool testResultSuccess: false

    // Keyboard offset for shifting content up
    property real keyboardOffset: 0

    // Delay timer to prevent jumpy behavior when switching fields
    Timer {
        id: keyboardResetTimer
        interval: 100
        onTriggered: {
            if (!Qt.inputMethod.visible) {
                visualizerTab.keyboardOffset = 0
            }
        }
    }

    // Function to update keyboard offset for a given field
    function updateKeyboardOffset(focusedField) {
        if (!focusedField) return

        // Qt doesn't report keyboard height on Android, so calculate based on field position
        // Keyboard typically covers bottom ~50% of screen, shift fields to top 30%
        var fieldPos = focusedField.mapToItem(visualizerTab, 0, 0)
        var fieldBottom = fieldPos.y + focusedField.height
        var safeZone = visualizerTab.height * 0.30

        keyboardOffset = Math.max(0, fieldBottom - safeZone)
    }

    // Track keyboard visibility and update offset
    Connections {
        target: Qt.inputMethod
        function onVisibleChanged() {
            if (Qt.inputMethod.visible) {
                keyboardResetTimer.stop()
                var focusedField = usernameField.activeFocus ? usernameField :
                                  (passwordField.activeFocus ? passwordField : null)
                visualizerTab.updateKeyboardOffset(focusedField)
            } else {
                keyboardResetTimer.restart()
            }
        }
    }

    RowLayout {
        width: parent.width
        height: parent.height
        y: -visualizerTab.keyboardOffset
        spacing: Theme.scaled(15)

        Behavior on y {
            NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
        }

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
                        onAccepted: passwordField.forceActiveFocus()
                        Keys.onReturnPressed: passwordField.forceActiveFocus()
                        Keys.onEnterPressed: passwordField.forceActiveFocus()
                        onActiveFocusChanged: if (activeFocus && Qt.inputMethod.visible) visualizerTab.updateKeyboardOffset(usernameField)
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
                        onAccepted: {
                            passwordField.focus = false
                            Qt.inputMethod.hide()
                        }
                        Keys.onReturnPressed: { passwordField.focus = false; Qt.inputMethod.hide() }
                        Keys.onEnterPressed: { passwordField.focus = false; Qt.inputMethod.hide() }
                        onActiveFocusChanged: if (activeFocus && Qt.inputMethod.visible) visualizerTab.updateKeyboardOffset(passwordField)
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

                        onValueModified: function(newValue) {
                            Settings.visualizerMinDuration = newValue
                        }
                    }
                }

                Item { height: 10 }

                // Extended metadata toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        spacing: Theme.scaled(2)

                        Tr {
                            key: "settings.visualizer.extendedMetadata"
                            fallback: "Extended Metadata"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Tr {
                            key: "settings.visualizer.extendedMetadataDesc"
                            fallback: "Include bean, grinder, and tasting notes with uploads"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizerExtendedMetadata
                        onCheckedChanged: Settings.visualizerExtendedMetadata = checked
                    }
                }

                // Show after shot toggle (only when extended metadata enabled)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    visible: Settings.visualizerExtendedMetadata

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
                        onCheckedChanged: Settings.visualizerShowAfterShot = checked
                    }
                }

                // Clear notes on shot start toggle (only when extended metadata enabled)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    visible: Settings.visualizerExtendedMetadata

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
                            fallback: "Clear espresso notes when starting a new shot"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: Settings.visualizerClearNotesOnStart
                        onCheckedChanged: Settings.visualizerClearNotesOnStart = checked
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
