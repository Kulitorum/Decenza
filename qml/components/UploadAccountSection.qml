import QtQuick
import QtQuick.Layouts
import Decenza

// Sign-in for an upload account (Visualizer, the Decent account): the fields and
// Connect while not connected, Disconnect once connected. Connect checks the
// credentials before anything is saved, so there is no separate test.
ColumnLayout {
    id: root

    property string identityLabel
    property bool connected
    property bool needsSignIn
    property string connectedName
    property bool busy
    property string accessibleAccountName

    // For the page's KeyboardAwareContainer.
    property alias emailField: identityInput
    property alias passwordField: passwordInput

    readonly property string statusText: root.connected
        ? TranslationManager.translate("settings.upload.account.connectedAs", "Connected as %1").arg(root.connectedName)
        : root.needsSignIn
            ? TranslationManager.translate("settings.upload.account.signInAgain", "Sign in again — the saved login no longer works")
            : TranslationManager.translate("settings.upload.account.notConnected", "Not connected")
    readonly property color statusColor: root.connected ? Theme.successColor
        : root.needsSignIn ? Theme.warningColor : Theme.textSecondaryColor

    property int linkError: AccountLink.Error.None

    signal connectRequested(string identity, string password)
    signal disconnectRequested()

    // The owner calls this when its connect attempt answers.
    function connectFinished(error) {
        root.linkError = error
        passwordInput.text = ""
    }

    spacing: Theme.scaled(12)

    ColumnLayout {
        Layout.fillWidth: true
        visible: !root.connected
        spacing: Theme.scaled(4)

        Text {
            text: root.identityLabel
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(12)
            Accessible.ignored: true
        }
        StyledTextField {
            id: identityInput
            Layout.fillWidth: true
            text: root.connectedName
            placeholder: root.identityLabel
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            Keys.onReturnPressed: function(event) { passwordInput.forceActiveFocus() }
            Keys.onEnterPressed: function(event) { passwordInput.forceActiveFocus() }
        }

        Tr {
            key: "settings.upload.account.password"
            fallback: "Password"
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(12)
            Accessible.ignored: true
        }
        StyledTextField {
            id: passwordInput
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholder: TranslationManager.translate("settings.upload.account.password", "Password")
            inputMethodHints: Qt.ImhNoAutoUppercase
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(10)

        AccessibleButton {
            visible: !root.connected
            primary: true
            enabled: !root.busy && identityInput.text.length > 0 && passwordInput.text.length > 0
            text: root.busy
                  ? TranslationManager.translate("settings.upload.account.connecting", "Connecting…")
                  : TranslationManager.translate("settings.upload.account.connect", "Connect")
            accessibleName: TranslationManager.translate("settings.upload.account.connectAccessible", "Connect your %1 account")
                                .arg(root.accessibleAccountName)
            onClicked: {
                Keyboard.commit()
                root.linkError = AccountLink.Error.None
                root.connectRequested(identityInput.text.trim(), passwordInput.text)
            }
        }

        AccessibleButton {
            visible: root.connected || root.needsSignIn
            text: TranslationManager.translate("settings.upload.account.disconnect", "Disconnect")
            accessibleName: TranslationManager.translate("settings.upload.account.disconnectAccessible", "Disconnect your %1 account")
                                .arg(root.accessibleAccountName)
            onClicked: root.disconnectRequested()
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.linkError !== AccountLink.Error.None
        text: root.linkError === AccountLink.Error.Rejected
              ? TranslationManager.translate("settings.upload.account.rejected", "Email or password not accepted")
              : TranslationManager.translate("settings.upload.account.unreachable",
                    "Could not reach the server — check your connection")
        color: Theme.errorColor
        font.pixelSize: Theme.scaled(12)
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
