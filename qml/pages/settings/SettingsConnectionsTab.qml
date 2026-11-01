// Bound so the scale-picker ComboBox delegate resolves this file's `scalePicker` id
// instead of reading it as unqualified access. All three delegates in this file declare
// every injected model role they use as a required property in the same edit: without
// that, ComponentBehavior: Bound stops role injection and breaks them at RUNTIME,
// silently. (`connectionsTab` and `discoveredDevicesList` are read only from sibling
// scopes, which resolve without the pragma.)
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Item {
    id: connectionsTab

    // The facade deliberately returns an empty map until its QtRO replica is
    // ready (and again after disconnect). Keep that transient state in the
    // same shape as the host snapshot so boolean bindings below never receive
    // `undefined` while the page is opening or reconnecting.
    readonly property var connectionSnapshot: SettingsConnectionsService.ready
        ? SettingsConnectionsService.snapshot
        : ({
            bluetoothAvailable: false,
            scanning: false,
            scaleConnecting: false,
            scaleConnectionFailed: false,
            usb: ({
                available: Qt.platform.os !== "ios",
                de1Connected: false,
                portName: "",
                serialNumber: ""
            })
        })
    readonly property var knownScales: connectionSnapshot.knownScales || []
    readonly property string primaryScaleAddress: connectionSnapshot.primaryScaleAddress || ""
    readonly property string savedRefractometerAddress: connectionSnapshot.savedRefractometerAddress || ""
    readonly property string savedRefractometerName: connectionSnapshot.savedRefractometerName || ""
    readonly property var discoveredDevices: connectionSnapshot.discoveredDevices || []
    readonly property var discoveredScales: connectionSnapshot.discoveredScales || []
    readonly property var discoveredRefractometers: connectionSnapshot.discoveredRefractometers || []
    readonly property var de1State: connectionSnapshot.de1 || ({
        connected: false,
        connecting: false,
        simulationMode: false,
        connectionType: "",
        firmwareVersion: "",
        stateString: ""
    })
    readonly property var usbState: connectionSnapshot.usb || ({
        available: Qt.platform.os !== "ios",
        de1Connected: false,
        portName: "",
        serialNumber: ""
    })
    readonly property var scaleState: connectionSnapshot.scale || ({})
    readonly property var portalState: connectionSnapshot.portal || ({
        state: "disconnected", active: false, owned: false, machineBusy: false, devices: [],
        savedAddress: "", savedName: "", errorMessage: "", syncDisplay: false,
        canControlDisplay: false, displayCommandStatus: "", lastPacket: "", packetCount: 0,
        hasReading: false, ecRaw: 0, temperatureC: 0
    })
    readonly property var firmwareState: connectionSnapshot.hdsFirmware || ({
        checking: false, updateAvailable: false, installedVersion: "", availableVersion: "",
        releaseNotes: "", releaseNotesLoading: false, updateStarted: false, updateError: ""
    })
    readonly property bool usbAvailable: usbState.available === true
    readonly property var preferences: SettingsPreferencesService.preferences
    property var pendingActions: ({})
    property string lastManualWifiHost: ""
    property string manualWifiProbeRequestId: ""
    property string manualWifiConnectRequestId: ""
    property string supportEmailClipboardRequestId: ""

    function preference(key: string, fallback: var): var {
        var value = preferences[key]
        return value === undefined ? fallback : value
    }

    function request(operation: string, args: var): string {
        var id = SettingsConnectionsService.execute(operation, args || ({}))
        if (id.length) {
            var updated = Object.assign({}, pendingActions)
            updated[id] = operation
            pendingActions = updated
        }
        return id
    }

    function completeRequest(requestId: string, operation: string,
                             success: bool, result: var, reason: string): void {
        if (pendingActions[requestId] !== operation) return
        var updated = Object.assign({}, pendingActions)
        delete updated[requestId]
        pendingActions = updated
        if (!success) {
            if (operation === "probeManualWifi" && requestId === manualWifiProbeRequestId) {
                manualWifiProbeRequestId = ""
                addWifiScaleDialog.mdnsSearching = false
            }
            if (operation === "connectManualWifi" && requestId === manualWifiConnectRequestId) {
                manualWifiConnectRequestId = ""
                // A probe can finish between the UI click and host dispatch.
                // Keep the address visible so the user can retry without
                // retyping it after the explicit stale-probe refusal.
                addWifiScaleDialog.open()
                wifiScaleHostField.text = lastManualWifiHost
                wifiScaleHostField.forceActiveFocus()
            }
            connectionOperationError.show(TranslationManager.translate(
                "settings.connections.operationFailed", "The connection action could not be completed (%1).")
                .arg(reason))
            return
        }
    }

    PreferenceWrite {
        id: connectionPreferenceWrite
        domain: "connections"
        onFailed: function(message) { connectionOperationError.show(message) }
    }
    PreferenceWrite {
        id: appPreferenceWrite
        domain: "app"
        onFailed: function(message) { connectionOperationError.show(message) }
    }
    StatusToast { id: connectionOperationError; error: true }
    StatusToast { id: logDownloadResult }
    SystemLogDownload {
        id: connectionLogDownload
        active: connectionsTab.visible
        onFinished: function(success, reason) {
            shareLogDialog.close()
            logDownloadResult.error = !success
            const message = success
                ? TranslationManager.translate("settings.debug.logSaved", "The log file was saved.")
                : TranslationManager.translate("settings.debug.logSaveFailed", "The log file could not be saved (%1).").arg(reason)
            logDownloadResult.show(message)
            AccessibilityManager.announce(message)
        }
    }

    Connections {
        target: SettingsConnectionsService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            connectionsTab.completeRequest(requestId, operation, success, result, reason)
        }
    }

    Connections {
        target: LocalClipboard
        function onOperationCompleted(requestId, operation, success, text, reason) {
            if (requestId !== connectionsTab.supportEmailClipboardRequestId
                    || operation !== "writeText") return
            connectionsTab.supportEmailClipboardRequestId = ""
            if (!success) {
                connectionOperationError.show(TranslationManager.translate(
                    "common.copyFailed", "Could not copy to the clipboard."))
                return
            }
            emailBox.copied = true
            copyResetTimer.restart()
        }
    }

    function copySupportEmail(): void {
        if (supportEmailClipboardRequestId.length > 0)
            return
        emailBox.copied = false
        const requestId = LocalClipboard.newRequestId()
        supportEmailClipboardRequestId = requestId
        if (!LocalClipboard.writeText(requestId, "decenzalogs@kulitorum.com")) {
            supportEmailClipboardRequestId = ""
            connectionOperationError.show(TranslationManager.translate(
                "common.copyFailed", "Could not copy to the clipboard."))
        }
    }

    // PORTAL discovery runs continuously in the background (for a saved
    // device's own reconnect), independent of this tab's "Scan for Devices"
    // button. An ambient, unpaired PORTAL merely broadcasting nearby must not
    // change this screen for a user who never pressed that button, so the
    // discovered-devices list only reacts to connectionsTab.portalState.devices once THIS
    // tab has actually triggered a scan.
    property bool portalScanTriggered: false

    // ─── Network card state ───
    readonly property var shotServerSnapshot: ShotServerSettingsService.ready
                                                ? ShotServerSettingsService.snapshot
                                                : ({ running: false, url: "", webSecurityEnabled: false,
                                                     hasTotpSecret: false, remoteWebEnabled: false,
                                                     tunnelAvailable: false })
    readonly property bool networkEnabled: preference("network.shotServerEnabled", false) === true
    readonly property bool remoteWebEnabled: preference("network.remoteWebEnabled", false) === true
    // The Tailscale node runs for either remote route, and both need Enable Network.
    readonly property bool tailscaleInUse: networkEnabled && (remoteWebEnabled
        || (preference("mcp.remoteMcpEnabled", false) === true
            && preference("mcp.remoteMcpMode", "custom") === "tailscale"))
    property string remoteWebSafeUrl: ""
    property string tailscaleLoginUrl: ""
    property string remoteUrlsRequestId: ""
    property string lastRemoteUrlsSignature: ""
    property string shotServerPrivateRequestId: ""
    property string shotServerPrivateOperation: ""
    property string networkClipboardRequestId: ""
    property bool copyingTotpSecret: false

    PreferenceWrite {
        id: networkPreferenceWrite
        domain: "network"
        onFailed: function(message) { connectionOperationError.show(message) }
    }
    ExternalUrlOpener { id: networkUrlOpener }

    function openExternal(url: string): void {
        const reservation = networkUrlOpener.reserve()
        if (!reservation || !networkUrlOpener.openReserved(reservation, url))
            connectionOperationError.show(TranslationManager.translate("common.openLinkFailed", "Could not open this link."))
    }

    function copyText(value: string, isTotpSecret: bool): void {
        if (networkClipboardRequestId.length > 0 || value.length === 0)
            return
        const requestId = LocalClipboard.newRequestId()
        networkClipboardRequestId = requestId
        copyingTotpSecret = isTotpSecret
        if (isTotpSecret)
            totpSetupDialog.totpSecretCopied = false
        if (!LocalClipboard.writeText(requestId, value)) {
            networkClipboardRequestId = ""
            connectionOperationError.show(TranslationManager.translate("common.copyFailed", "Could not copy to the clipboard."))
        }
    }

    function executeShotServer(operation: string, args: var): void {
        shotServerPrivateOperation = operation
        shotServerPrivateRequestId = ShotServerSettingsService.execute(operation, args || ({}))
    }

    // URL material is never in the snapshot; ask for it when the snapshot says
    // one of the URLs became (un)available.
    function refreshRemoteUrls(): void {
        if (!ShotServerSettingsService.ready || !tailscaleInUse) {
            remoteWebSafeUrl = ""
            tailscaleLoginUrl = ""
            lastRemoteUrlsSignature = ""
            return
        }
        const signature = [shotServerSnapshot.remoteWebReady === true,
                           shotServerSnapshot.tailscaleLoginPending === true,
                           shotServerSnapshot.remoteAccessStatus || "",
                           shotServerSnapshot.remoteUrlRevision || 0].join("|")
        if (signature === lastRemoteUrlsSignature || remoteUrlsRequestId.length > 0)
            return
        lastRemoteUrlsSignature = signature
        remoteUrlsRequestId = ShotServerSettingsService.execute("requestRemoteWebUrls", {})
    }

    function remoteAccessStatusText(): string {
        switch (shotServerSnapshot.remoteAccessStatus || "off") {
        case "active":
            return TranslationManager.translate("settings.ai.remoteMcp.status.activeTailscale", "Active — public Funnel URL ready")
        case "starting":
            return TranslationManager.translate("settings.ai.remoteMcp.status.starting", "Starting…")
        case "publishing":
            return TranslationManager.translate("settings.ai.remoteMcp.status.publishing", "Funnel is on — waiting for Tailscale to bring up the public address. This can take a few minutes after starting.")
        case "reconnecting":
            return TranslationManager.translate("settings.ai.remoteMcp.status.reconnecting", "Reconnecting…")
        case "error":
            return TranslationManager.translate("settings.ai.remoteMcp.status.error", "Error: %1").arg(shotServerSnapshot.remoteAccessStatusDetail || "")
        default:
            return TranslationManager.translate("settings.ai.remoteMcp.status.off", "Off")
        }
    }

    onTailscaleInUseChanged: refreshRemoteUrls()
    Component.onCompleted: Qt.callLater(connectionsTab.refreshRemoteUrls)

    Connections {
        target: ShotServerSettingsService
        function onStateChanged() { connectionsTab.refreshRemoteUrls() }
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === connectionsTab.remoteUrlsRequestId && operation === "requestRemoteWebUrls") {
                connectionsTab.remoteUrlsRequestId = ""
                connectionsTab.remoteWebSafeUrl = success ? (result.safeUrl || "") : ""
                connectionsTab.tailscaleLoginUrl = success ? (result.loginUrl || "") : ""
                Qt.callLater(connectionsTab.refreshRemoteUrls)
                return
            }
            if (requestId !== connectionsTab.shotServerPrivateRequestId
                    || operation !== connectionsTab.shotServerPrivateOperation) return
            connectionsTab.shotServerPrivateRequestId = ""
            connectionsTab.shotServerPrivateOperation = ""
            if (operation === "generateTotpSetup") {
                if (!success) { connectionOperationError.show(reason); return }
                totpSetupDialog.totpSecret = result.secret || ""
                totpSetupDialog.totpUri = result.uri || ""
                totpSetupDialog.open()
            } else if (operation === "completeTotpSetup") {
                totpSetupDialog.verifying = false
                if (success) totpSetupDialog.close()
                else {
                    totpSetupDialog.verifyError = TranslationManager.translate(
                        "settings.data.totpverifyfailed", "Invalid code. Please try again.")
                    totpCodeField.text = ""
                    totpCodeField.forceActiveFocus()
                }
            } else if (operation === "resetTotpSecret") {
                if (success) totpResetDialog.close()
                else connectionOperationError.show(reason)
            } else if (operation === "rotateRemoteWebToken") {
                if (success) {
                    connectionsTab.remoteWebSafeUrl = result.safeUrl || ""
                    connectionsTab.lastRemoteUrlsSignature = ""
                    AccessibilityManager.announce(TranslationManager.translate("settings.network.remoteWeb.rotated",
                        "New Remote Web Access link created. Browsers using the old one are signed out."))
                } else connectionOperationError.show(TranslationManager.translate("settings.network.remoteWeb.rotateFailed",
                    "Could not create a new Remote Web Access link."))
            } else if (operation === "forgetTailscale") {
                if (success) AccessibilityManager.announce(TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.done", "Signed out of Tailscale. Sign in again to use remote access."))
                else connectionOperationError.show(TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.failed", "Could not clear the Tailscale identity. Please try again."))
            }
        }
    }
    Connections {
        target: LocalClipboard
        function onOperationCompleted(requestId, operation, success, text, reason) {
            if (requestId !== connectionsTab.networkClipboardRequestId || operation !== "writeText") return
            connectionsTab.networkClipboardRequestId = ""
            if (!success) {
                connectionOperationError.show(TranslationManager.translate("common.copyFailed", "Could not copy to the clipboard."))
                return
            }
            if (connectionsTab.copyingTotpSecret) {
                totpSetupDialog.totpSecretCopied = true
                secretCopyTimer.restart()
            } else {
                AccessibilityManager.announce(TranslationManager.translate("settings.network.copied", "Copied"))
            }
        }
    }

    // TOTP Setup Dialog
    DecenzaDialog {
        id: totpSetupDialog
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        x: Math.round((parent.width - width) / 2)
        // qmllint enable Quick.layout-positioning
        y: {
            if (totpCodeField.activeFocus) {
                // Center in the visible area above the keyboard
                let kbHeight = Keyboard.rectangle.height;
                if (kbHeight <= 0 && (Qt.platform.os === "android" || Qt.platform.os === "ios"))
                    kbHeight = parent.height * 0.45;
                let availableHeight = parent.height - kbHeight;
                return Math.round(Math.max(Theme.scaled(10), (availableHeight - height) / 2));
            }
            return Math.round((parent.height - height) / 2);
        }
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        width: Theme.scaled(380)
        padding: 0
        modal: true

        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
        // qmllint enable Quick.layout-positioning

        property string totpSecret: ""
        property bool totpSecretCopied: false
        property string totpUri: ""
        property string verifyError: ""
        property bool verifying: false

        onOpened: {
            totpSecretCopied = false
            secretCopyTimer.stop()
        }

        onClosed: {
            totpCodeField.text = "";
            verifyError = "";
            verifying = false;
            totpSecretCopied = false
            secretCopyTimer.stop()
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.totpsetuptitle", "Set Up Authenticator")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Body
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(12)

                Text {
                    Layout.fillWidth: true
                    visible: !totpCodeField.activeFocus
                    text: TranslationManager.translate("settings.data.totpsetupinstructions",
                        "Scan this QR code with your authenticator app (Apple Passwords, Google Authenticator, Microsoft Authenticator, or similar).")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.captionFont.pixelSize
                    wrapMode: Text.WordWrap
                }

                // QR code — hidden when keyboard is open (user already scanned it)
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Theme.scaled(200)
                    Layout.preferredHeight: Theme.scaled(200)
                    visible: !totpCodeField.activeFocus
                    color: "#ffffff"
                    radius: Theme.scaled(8)
                    Accessible.role: Accessible.Graphic
                    Accessible.name: TranslationManager.translate("settings.data.qrcodeAccessible",
                        "QR code for authenticator app setup. Use the manual code below if you cannot scan.")

                    QrCode {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        value: totpSetupDialog.totpUri
                        Accessible.ignored: true
                    }
                }

                // Manual entry secret — hidden when keyboard is open
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !totpCodeField.activeFocus
                    spacing: Theme.scaled(4)

                    Text {
                        text: TranslationManager.translate("settings.data.totpmanualentry", "Or enter this code manually:")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.captionFont.pixelSize
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(36)
                            color: Theme.backgroundColor
                            radius: Theme.scaled(4)
                            border.color: Theme.borderColor
                            border.width: 1

                            TextInput {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(6)
                                text: totpSetupDialog.totpSecret
                                readOnly: true
                                selectByMouse: true
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                clip: true
                                color: Theme.textColor
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.captionFont.pixelSize
                                font.bold: true

                                Accessible.role: Accessible.StaticText
                                Accessible.name: TranslationManager.translate("settings.data.totpsecretAccessible",
                                    "Secret code for manual entry: ") + totpSetupDialog.totpSecret
                            }
                        }

                        AccessibleButton {
                            text: totpSetupDialog.totpSecretCopied && secretCopyTimer.running ?
                                  TranslationManager.translate("settings.data.copied", "Copied") :
                                  TranslationManager.translate("settings.data.copy", "Copy")
                            accessibleName: TranslationManager.translate("settings.data.copySecretAccessible",
                                "Copy secret code to clipboard")
                            enabled: connectionsTab.networkClipboardRequestId.length === 0
                            onClicked: connectionsTab.copyText(totpSetupDialog.totpSecret, true)

                            Timer {
                                id: secretCopyTimer
                                interval: 2000
                            }
                        }
                    }
                }

                // Verification code input
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Text {
                        text: TranslationManager.translate("settings.data.totpverify",
                            "Enter a code from your authenticator to verify:")
                        color: Theme.textColor
                        font.pixelSize: Theme.captionFont.pixelSize
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        StyledTextField {
                            id: totpCodeField
                            Layout.fillWidth: true
                            maximumLength: 6
                            inputMethodHints: Qt.ImhDigitsOnly
                            horizontalAlignment: TextInput.AlignHCenter
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                            font.letterSpacing: Theme.scaled(4)
                            enabled: !totpSetupDialog.verifying
                            accessibleName: TranslationManager.translate("settings.data.totpCodeFieldAccessible",
                                "Six digit verification code from authenticator app")

                            onTextChanged: {
                                totpSetupDialog.verifyError = "";
                            }

                            Keys.onReturnPressed: {
                                if (text.length === 6) totpVerifyButton.clicked();
                            }
                        }

                        AccessibleButton {
                            id: totpVerifyButton
                            primary: true
                            text: totpSetupDialog.verifying ?
                                  TranslationManager.translate("settings.data.verifying", "Verifying...") :
                                  TranslationManager.translate("settings.data.verify", "Verify")
                            accessibleName: TranslationManager.translate("settings.data.verifyAccessible",
                                "Verify authenticator code to complete setup")
                            enabled: totpCodeField.text.length === 6 && !totpSetupDialog.verifying
                                     && ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                            onClicked: {
                                Keyboard.commit()
                                totpSetupDialog.verifying = true;
                                connectionsTab.executeShotServer("completeTotpSetup",
                                    {secret: totpSetupDialog.totpSecret, code: totpCodeField.text});
                            }
                        }
                    }

                    Text {
                        visible: totpSetupDialog.verifyError !== ""
                        text: totpSetupDialog.verifyError
                        color: Theme.errorColor
                        font.pixelSize: Theme.captionFont.pixelSize
                    }
                }

                // Cancel button
                AccessibleButton {
                    Layout.alignment: Qt.AlignRight
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("settings.data.cancelTotpSetup",
                        "Cancel authenticator setup")
                    onClicked: totpSetupDialog.close()
                }
            }
        }
    }

    // TOTP Reset Confirmation Dialog
    DecenzaDialog {
        id: totpResetDialog
        parent: Overlay.overlay
        // qmllint disable Quick.layout-positioning
        // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
        // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
        // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
        // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
        anchors.centerIn: parent
        width: Theme.scaled(380)
        // qmllint enable Quick.layout-positioning
        padding: 0
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.data.resetsecuritytitle", "Reset Security?")
                    font: Theme.titleFont
                    color: Theme.warningColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Body
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.data.resetsecuritybody",
                        "This will remove your authenticator setup and sign out all web sessions. You will need to set up your authenticator app again.")
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("settings.data.cancelResetSecurity",
                            "Cancel security reset")
                        onClicked: totpResetDialog.close()
                    }

                    AccessibleButton {
                        destructive: true
                        text: TranslationManager.translate("settings.data.confirmreset", "Reset")
                        enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                        accessibleName: TranslationManager.translate("settings.data.confirmResetAccessible",
                            "Confirm: remove authenticator and sign out all sessions")
                        onClicked: {
                            connectionsTab.executeShotServer("resetTotpSecret", {});
                        }
                    }
                }
            }
        }
    }

    // Confirm signing out of Tailscale (wipes the embedded node's identity).
    DecenzaDialog {
        id: tailscaleSignoutDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(360), Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(tailscaleSignoutTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                id: tailscaleSignoutTitle
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.confirmTitle", "Sign out of Tailscale?")
                color: Theme.textColor
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.confirmBody", "This clears this device's Tailscale identity. Remote access stops working until you sign in again, and you'll get a fresh login link. Use this to recover from a login that keeps failing.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.captionFont.pixelSize
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: tailscaleSignoutDialog.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.button", "Sign out")
                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.accessible", "Sign out of Tailscale and clear this device's tailnet identity")
                    destructive: true
                    onClicked: {
                        connectionsTab.executeShotServer("forgetTailscale", {})
                        tailscaleSignoutDialog.close()
                    }
                }
            }
        }
    }

    // Step-by-step Tailscale Funnel setup instructions.
    DecenzaDialog {
        id: tailscaleSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(480), Theme.scaled(520))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        readonly property string aclSnippet:
            '"nodeAttrs": [\n  { "target": ["autogroup:member"], "attr": ["funnel"] },\n],'

        onOpened: AccessibilityManager.announce(tsSetupTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Flickable {
            implicitHeight: Math.min(tsSetupCol.implicitHeight,
                tailscaleSetupDialog.parent ? tailscaleSetupDialog.parent.height - Theme.scaled(120) : Theme.scaled(560))
            contentHeight: tsSetupCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: tsSetupCol
                width: parent.width
                spacing: Theme.scaled(12)

                Text {
                    id: tsSetupTitle
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.title", "Set up Tailscale Funnel")
                    color: Theme.textColor
                    font.family: Theme.subtitleFont.family
                    font.pixelSize: Theme.subtitleFont.pixelSize
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.network.tailscaleSetup.intro", "A one-time setup on your Tailscale account lets Decenza publish a public address for Remote Web Access and AI connectors. Do both steps in the Tailscale admin console, then come back: Decenza connects automatically, with no restart.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.captionFont.pixelSize
                    wrapMode: Text.WordWrap
                }

                // Step 1 — HTTPS certificates
                Tr {
                    key: "settings.ai.remoteMcp.setup.step1"
                    fallback: "1.  Enable HTTPS certificates"
                    color: Theme.textColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.step1body", "On the DNS page, scroll to the bottom and turn on “HTTPS Certificates”.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.captionFont.pixelSize
                    wrapMode: Text.WordWrap
                }
                AccessibleButton {
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.openDns", "Open DNS settings ↗")
                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.setup.openDnsAccessible", "Open Tailscale DNS settings in browser")
                    onClicked: connectionsTab.openExternal("https://login.tailscale.com/admin/dns")
                }

                // Step 2 — Funnel node attribute
                Tr {
                    key: "settings.ai.remoteMcp.setup.step2"
                    fallback: "2.  Allow Funnel for this device"
                    color: Theme.textColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.step2body", "Open Access controls and click “JSON editor” at the top. On the right, open the “Funnel” panel and click “Add Funnel to policy”, then Save (the red button). No typing needed.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.captionFont.pixelSize
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.ai.remoteMcp.setup.step2fallback", "Don’t see it? Paste this rule after the first “{” in the JSON editor instead, then Save:")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.captionFont.pixelSize
                    font.italic: true
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.backgroundColor
                    border.color: Theme.borderColor
                    border.width: 1
                    radius: Theme.scaled(6)
                    Layout.preferredHeight: tsSnippetText.implicitHeight + Theme.scaled(16)
                    Text {
                        id: tsSnippetText
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        text: tailscaleSetupDialog.aclSnippet
                        color: Theme.textColor
                        // Real family names, not the generic "monospace" alias:
                        // no platform provides a family by that name, so Qt ran
                        // a full font-alias sweep (~66 ms, with a warning) and
                        // fell back to a host font anyway. Ordered macOS /
                        // Windows / Linux; Qt takes the first that exists.
                        //
                        // Built with Qt.font() rather than `font.families:`,
                        // which is not assignable as a grouped property — that
                        // spelling fails at load with "Cannot assign to
                        // non-existent property". Same pattern as Theme.qml.
                        font: Qt.font({
                            families: ["Menlo", "Consolas", "DejaVu Sans Mono", "Courier New"],
                            pixelSize: Theme.captionFont.pixelSize
                        })
                        wrapMode: Text.WrapAnywhere
                        Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.setup.snippetAccessible", "Tailscale ACL rule to grant Funnel")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.remoteMcp.setup.copyRule", "Copy rule")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.setup.copyRuleAccessible", "Copy the Tailscale Funnel ACL rule to clipboard")
                        onClicked: {
                            connectionsTab.copyText(tailscaleSetupDialog.aclSnippet, false)
                        }
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.remoteMcp.setup.openAcls", "Open Access controls ↗")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.setup.openAclsAccessible", "Open Tailscale access controls in browser")
                        onClicked: connectionsTab.openExternal("https://login.tailscale.com/admin/acls")
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: Theme.scaled(4) }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    AccessibleButton {
                        text: TranslationManager.translate("common.button.done", "Done")
                        accessibleName: TranslationManager.translate("common.button.done", "Done")
                        primary: true
                        onClicked: tailscaleSetupDialog.close()
                    }
                }
            }
        }
    }

    // Confirm replacing the Remote Web Access link (signs out every browser on it).
    DecenzaDialog {
        id: rotateRemoteWebDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(360), Theme.scaled(420))
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        onOpened: AccessibilityManager.announce(rotateRemoteWebTitle.text)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                id: rotateRemoteWebTitle
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.network.remoteWeb.rotateConfirmTitle", "Create a new link?")
                color: Theme.textColor
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.network.remoteWeb.rotateConfirmBody", "The current Remote Web Access link stops working at once, and every browser using it is signed out. Scan the new code on each phone. The AI connector is not affected.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.captionFont.pixelSize
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)
                Item { Layout.fillWidth: true }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: rotateRemoteWebDialog.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("settings.ai.remoteMcp.rotateButton", "Rotate")
                    accessibleName: TranslationManager.translate("settings.network.remoteWeb.rotateAccessible", "Rotate the Remote Web Access link")
                    destructive: true
                    onClicked: {
                        connectionsTab.executeShotServer("rotateRemoteWebToken", {})
                        rotateRemoteWebDialog.close()
                    }
                }
            }
        }
    }

    DecenzaDialog {
        id: hdsFirmwareUpdateDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: TranslationManager.translate("connections.hdsUpdateTitle", "Update Half Decent Scale")
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min((parent ? parent.width : Theme.scaled(460)) * 0.9, Theme.scaled(460))
        padding: Theme.scaled(20)
        onOpened: connectionsTab.request("loadHdsReleaseNotes", {})

        // Single definition so the visible Text and the dialog's accessible
        // name (below) can never drift onto two different wordings of the
        // same refusal.
        readonly property string rejectionText: connectionsTab.firmwareState.updateError !== ""
            ? TranslationManager.translate("connections.hdsUpdateRejected",
                                           "The scale refused this update: %1")
                  .arg(connectionsTab.firmwareState.updateError)
            : ""

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)
            // DecenzaDialog is a Popup, so attach the dialog semantics to its Item content.
            // Every Text below is Accessible.ignored, relying on this name to
            // summarise the dialog per ACCESSIBILITY.md — so the one piece of
            // dynamic, actionable content this dialog produces (a refusal) has
            // to be folded in here too, not just painted in Theme.errorColor.
            Accessible.role: Accessible.Dialog
            Accessible.name: hdsFirmwareUpdateDialog.rejectionText !== ""
                ? hdsFirmwareUpdateDialog.title + ". " + hdsFirmwareUpdateDialog.rejectionText
                : hdsFirmwareUpdateDialog.title

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("connections.hdsUpdateVersions",
                                                   "Version %1 is available (installed: %2).")
                    .arg(connectionsTab.firmwareState.availableVersion)
                    .arg(connectionsTab.firmwareState.installedVersion)
                color: Theme.textColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                // Only WiFi can ever populate this — the one transport whose
                // protocol replies to a command in-band (see
                // ScaleDevice::firmwareUpdateRejected). Bluetooth and USB give
                // no such signal, so this stays empty there even on refusal.
                visible: hdsFirmwareUpdateDialog.rejectionText !== ""
                text: hdsFirmwareUpdateDialog.rejectionText
                color: Theme.errorColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                visible: !connectionsTab.firmwareState.updateStarted
                text: TranslationManager.translate("connections.hdsUpdateExplain",
                                                   "The scale will install this version on its own. Keep it powered and within WiFi range until it restarts.")
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                visible: connectionsTab.firmwareState.updateStarted
                // Says only what a transport can support. Bluetooth and USB carry no
                // acknowledgement at all, so "the scale accepted this" is not knowable
                // there however much machinery is added — and a restart is the only
                // positive evidence any transport offers.
                text: TranslationManager.translate("connections.hdsUpdateRequested",
                                                   "Update requested. The scale restarts if it accepts, and shows any problem on its own display.")
                color: Theme.textSecondaryColor
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("connections.hdsReleaseNotes", "Release notes")
                color: Theme.textColor
                font.bold: true
                Accessible.ignored: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(160)
                clip: true

                TextArea {
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    text: connectionsTab.firmwareState.releaseNotesLoading
                        ? TranslationManager.translate("connections.hdsReleaseNotesLoading", "Loading release notes…")
                        : (connectionsTab.firmwareState.releaseNotes ||
                           TranslationManager.translate("connections.hdsReleaseNotesUnavailable", "Release notes are unavailable right now."))
                    color: Theme.textColor
                    Accessible.role: Accessible.EditableText
                    Accessible.name: TranslationManager.translate("connections.hdsReleaseNotes", "Release notes")
                    Accessible.focusable: true
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.scaled(8)

                AccessibleButton {
                    text: connectionsTab.firmwareState.updateStarted
                        ? TranslationManager.translate("common.button.close", "Close")
                        : TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: text
                    subtle: true
                    onClicked: hdsFirmwareUpdateDialog.close()
                }

                AccessibleButton {
                    visible: !connectionsTab.firmwareState.updateStarted
                    text: TranslationManager.translate("connections.hdsStartUpdate", "Start update")
                    accessibleName: TranslationManager.translate("connections.hdsStartUpdate", "Start update")
                    primary: true
                    onClicked: connectionsTab.request("startHdsFirmwareUpdate", {})
                }
            }
        }
    }

    Connections {
        target: SettingsConnectionsService
        function onStateChanged() {
            if (!SettingsConnectionsService.ready) {
                connectionsTab.manualWifiProbeRequestId = ""
                connectionsTab.manualWifiConnectRequestId = ""
                addWifiScaleDialog.mdnsSearching = false
            }
            if (!connectionsTab.firmwareState.updateAvailable)
                hdsFirmwareUpdateDialog.close()
        }
        function onActiveHdsScaleChanged() {
            hdsFirmwareUpdateDialog.close()
        }
    }

    // Share Log Dialog
    DecenzaDialog {
        id: shareLogDialog
        onClosed: connectionLogDownload.cancel()
        modal: true
        anchors.centerIn: parent
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min(parent.width * 0.85, Theme.scaled(400))
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.primaryContrastColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Title
            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(15)
                text: TranslationManager.translate("settings.bluetooth.shareLogTitle", "Share Scale Debug Log")
                color: Theme.textColor
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            // Content
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                text: TranslationManager.translate("settings.bluetooth.shareLogInstructions", "Send the debug log to:")
                color: Theme.textColor
                font.pixelSize: Theme.labelFont.pixelSize
            }

            // Email address box
            Rectangle {
                id: emailBox
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(10)
                Layout.preferredHeight: Theme.scaled(40)
                color: emailMouseArea.containsMouse
                       ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.25)
                       : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.15)
                radius: Theme.scaled(6)
                border.color: Theme.accentColor
                border.width: 1

                property bool copied: false

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("settings.bluetooth.copyemail", "Copy email address") + " decenzalogs@kulitorum.com"
                Accessible.focusable: true
                Accessible.onPressAction: emailMouseArea.clicked(null)

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(8)

                    Text {
                        text: emailBox.copied
                              ? TranslationManager.translate("common.copied", "✓ Copied!")
                              : "decenzalogs@kulitorum.com"
                        color: Theme.accentColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                        Accessible.ignored: true
                    }

                    Text {
                        text: "⧉"
                        color: Theme.accentColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        visible: !emailBox.copied
                        Accessible.ignored: true
                    }
                }

                MouseArea {
                    id: emailMouseArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    enabled: connectionsTab.supportEmailClipboardRequestId.length === 0
                    onClicked: connectionsTab.copySupportEmail()
                }

                Timer {
                    id: copyResetTimer
                    interval: 2000
                    onTriggered: emailBox.copied = false
                }
            }

            // Instructions
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(12)
                text: TranslationManager.translate("settings.bluetooth.shareLogInclude", "Please include your scale model and describe the issue.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.captionFont.pixelSize
                wrapMode: Text.Wrap
            }

            ProgressBar {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                visible: connectionLogDownload.busy
                indeterminate: true
                Accessible.name: TranslationManager.translate("settings.debug.preparingLog", "Preparing log download")
            }

            // Buttons row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(15)
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                spacing: Theme.scaled(12)

                // Cancel button
                Text {
                    id: shareLogCancel
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    color: Theme.accentColor
                    font.pixelSize: Theme.labelFont.pixelSize

                    Accessible.role: Accessible.Button
                    Accessible.name: shareLogCancel.text
                    Accessible.focusable: true
                    Accessible.onPressAction: shareLogDialog.close()

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shareLogDialog.close()
                    }
                }

                Item { Layout.fillWidth: true }

                // Share button
                Rectangle {
                    Layout.preferredWidth: Theme.scaled(140)
                    Layout.preferredHeight: Theme.scaled(36)
                    enabled: connectionLogDownload.ready && !connectionLogDownload.busy
                    opacity: enabled ? 1 : 0.5
                    color: Theme.accentColor
                    radius: Theme.scaled(6)

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("settings.bluetooth.shareNow", "Share Log File")
                    Accessible.focusable: true
                    Accessible.onPressAction: shareLogArea.clicked(null)

                    Text {
                        anchors.centerIn: parent
                        text: TranslationManager.translate("settings.bluetooth.shareNow", "Share Log File")
                        color: Theme.primaryContrastColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        font.bold: true
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: shareLogArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // The SYSTEM log, not the scale log. Users have been
                            // asked for the system log for a while now, and the
                            // scale-only file was a subset that omitted the DE1 and
                            // everything else that ran alongside the failure.
                            if (!connectionLogDownload.start("zip"))
                                connectionOperationError.show(TranslationManager.translate(
                                    "settings.connections.logDownloadUnavailable", "The debug log download is unavailable."))
                        }
                    }
                }
            }
        }
    }

    // Add WiFi Scale Dialog — enter an IP address or mDNS name to connect a
    // WiFi scale that isn't being advertised/discovered right now.
    DecenzaDialog {
        id: addWifiScaleDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min((parent ? parent.width : Theme.scaled(420)) * 0.85, Theme.scaled(420))
        padding: 0

        // mDNS lookup state while the dialog is open. Set by the BLEManager
        // signals below. The hostname is shown as a one-tap shortcut so the
        // user doesn't have to type it (or guess between "hds.local" and an IP).
        property bool mdnsSearching: false
        property string mdnsHostname: ""
        property string mdnsIp: ""
        readonly property bool mdnsFound: mdnsHostname.length > 0

        onOpened: {
            wifiScaleHostField.text = connectionsTab.lastManualWifiHost
            wifiScaleHostField.forceActiveFocus()
            // Reset prior probe state and kick off a fresh mDNS lookup. If the
            // scale is on the LAN we offer it as a one-tap option above the
            // text field, sidestepping the typo-and-bad-IP failure mode entirely.
            if (connectionsTab.manualWifiProbeRequestId.length === 0) {
                mdnsHostname = ""
                mdnsIp = ""
                mdnsSearching = true
                connectionsTab.manualWifiProbeRequestId =
                    connectionsTab.request("probeManualWifi", {})
            } else {
                mdnsSearching = mdnsHostname.length === 0
            }
        }

        function submitWifiScale() {
            Keyboard.commit()  // flush in-progress IME word before reading text
            var host = wifiScaleHostField.text.trim()
            if (host.length === 0)
                return
            connectionsTab.lastManualWifiHost = host
            var probeRequestId = connectionsTab.manualWifiProbeRequestId
            connectionsTab.manualWifiProbeRequestId = ""
            addWifiScaleDialog.close()
            connectionsTab.manualWifiConnectRequestId =
                connectionsTab.request("connectManualWifi", probeRequestId.length
                    ? { hostname: host, probeRequestId: probeRequestId }
                    : { hostname: host })
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.primaryContrastColor
            border.width: 1
        }

        contentItem: KeyboardAwareContainer {
            id: wifiScaleKbContainer
            inOverlay: true
            textFields: [wifiScaleHostField]
            targetFlickable: wifiScaleFlick
            implicitWidth: addWifiScaleDialog.availableWidth
            implicitHeight: Math.min(wifiScaleCol.implicitHeight,
                                     addWifiScaleDialog.parent ? addWifiScaleDialog.parent.height * 0.9
                                                               : wifiScaleCol.implicitHeight)

            Flickable {
                id: wifiScaleFlick
                anchors.fill: parent
                contentHeight: wifiScaleCol.implicitHeight + wifiScaleKbContainer.estimatedKeyboardHeight
                contentWidth: parent.width
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                ColumnLayout {
                    id: wifiScaleCol
                    width: parent.width
                    spacing: 0

                    // Title
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(15)
                        text: TranslationManager.translate("settings.bluetooth.addWifiScaleTitle", "Add WiFi Scale")
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // Instructions
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        text: TranslationManager.translate("settings.bluetooth.addWifiScaleInstructions",
                              "Enter the scale's IP address or name.")
                        color: Theme.textColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.Wrap
                    }

                    // "Searching the network..." indicator shown while the
                    // mDNS probe is in flight and we haven't found anything
                    // yet. Replaced by the discovered-scale banner below once
                    // mdnsFound flips true; hidden entirely once the probe
                    // finishes without a result.
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(12)
                        visible: addWifiScaleDialog.mdnsSearching && !addWifiScaleDialog.mdnsFound
                        text: TranslationManager.translate("settings.bluetooth.mdnsSearching",
                                                           "Searching the network for a scale...")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.italic: true
                    }

                    // mDNS-discovered scale suggestion. Saves the user from
                    // typing (and from typos that would fail validation) when
                    // the scale is responding to mDNS on the LAN.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(12)
                        Layout.preferredHeight: visible ? mdnsRow.implicitHeight + Theme.scaled(16) : 0
                        visible: addWifiScaleDialog.mdnsFound
                        color: Qt.darker(Theme.accentColor, 1.4)
                        radius: Theme.scaled(6)
                        border.color: Theme.accentColor
                        border.width: 1

                        RowLayout {
                            id: mdnsRow
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)
                            spacing: Theme.scaled(8)

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(2)

                                Text {
                                    Layout.fillWidth: true
                                    text: TranslationManager.translate("settings.bluetooth.mdnsFoundTitle",
                                                                       "Scale found on this network")
                                    color: Theme.textColor
                                    font.pixelSize: Theme.labelFont.pixelSize
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: addWifiScaleDialog.mdnsHostname
                                        + (addWifiScaleDialog.mdnsIp.length > 0
                                           ? " (" + addWifiScaleDialog.mdnsIp + ")"
                                           : "")
                                    color: Theme.textSecondaryColor
                                    font.pixelSize: Theme.captionFont.pixelSize
                                    wrapMode: Text.Wrap
                                }
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.mdnsFoundUse", "Use")
                                accessibleName: TranslationManager.translate("settings.bluetooth.mdnsFoundUseAccessible",
                                                                              "Use the discovered WiFi scale")
                                primary: true
                                onClicked: {
                                    var host = addWifiScaleDialog.mdnsHostname
                                    var ip = addWifiScaleDialog.mdnsIp
                                    connectionsTab.lastManualWifiHost = host
                                    var probeRequestId = connectionsTab.manualWifiProbeRequestId
                                    connectionsTab.manualWifiProbeRequestId = ""
                                    addWifiScaleDialog.close()
                                    var args = { hostname: host, resolvedIp: ip }
                                    if (probeRequestId.length)
                                        args.probeRequestId = probeRequestId
                                    connectionsTab.manualWifiConnectRequestId =
                                        connectionsTab.request("connectManualWifi", args)
                                }
                            }
                        }
                    }

                    // IP / name input
                    StyledTextField {
                        id: wifiScaleHostField
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        Layout.topMargin: Theme.scaled(12)
                        placeholder: TranslationManager.translate("settings.bluetooth.addWifiScalePlaceholder", "192.168.1.50 or hds.local")
                        accessibleName: TranslationManager.translate("settings.bluetooth.addWifiScaleField", "WiFi scale IP address or name")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        Keys.onReturnPressed: addWifiScaleDialog.submitWifiScale()
                        Keys.onEnterPressed: addWifiScaleDialog.submitWifiScale()
                    }

                    // Buttons row
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(20)
                        Layout.bottomMargin: Theme.scaled(15)
                        Layout.leftMargin: Theme.scaled(20)
                        Layout.rightMargin: Theme.scaled(20)
                        spacing: Theme.scaled(12)

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: TranslationManager.translate("common.cancel", "Cancel")
                            accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                            subtle: true
                            onClicked: addWifiScaleDialog.close()
                        }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.bluetooth.connect", "Connect")
                            accessibleName: TranslationManager.translate("settings.bluetooth.addWifiScaleConnect", "Connect to WiFi scale")
                            primary: true
                            enabled: wifiScaleHostField.text.trim().length > 0
                            onClicked: addWifiScaleDialog.submitWifiScale()
                        }
                    }
                }
            }
        }
    }

    // Validation-result dialog for the manual "Add WiFi Scale" flow. Shown when
    // BLEManager fails to verify that the typed address actually hosts an HDS
    // scale (timeout without HDS recognition, or socket error). Without this
    // the user would silently end up with a poisoned saved primary that the
    // app keeps redialing on every reconnect cycle (see #1281).
    DecenzaDialog {
        id: manualWifiFailedDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        width: Math.min((parent ? parent.width : Theme.scaled(420)) * 0.85, Theme.scaled(420))
        padding: Theme.scaled(20)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.primaryContrastColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.bluetooth.manualWifiFailed.title",
                                                   "No scale found")
                color: Theme.textColor
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.bluetooth.manualWifiFailed.body",
                                                   "Couldn't verify a Decent WiFi scale at %1. Check the address and that the scale is on the same network, then try again.").arg(connectionsTab.lastManualWifiHost)
                color: Theme.textColor
                font.pixelSize: Theme.labelFont.pixelSize
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.ok", "OK")
                    accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                    primary: true
                    onClicked: manualWifiFailedDialog.close()
                }
            }
        }
    }

    Connections {
        target: SettingsConnectionsService
        function onManualWifiValidationFailed(requestId, hostnameOrIp) {
            if (requestId !== connectionsTab.manualWifiConnectRequestId) return
            connectionsTab.manualWifiConnectRequestId = ""
            connectionsTab.lastManualWifiHost = hostnameOrIp
            // Don't stack on top of the entry dialog if the user reopened it
            if (addWifiScaleDialog.opened) addWifiScaleDialog.close()
            manualWifiFailedDialog.open()
        }
        function onManualWifiValidationSucceeded(requestId, hostnameOrIp) {
            if (requestId !== connectionsTab.manualWifiConnectRequestId) return
            connectionsTab.manualWifiConnectRequestId = ""
            // No UX needed — the normal scale-connected indicator handles it.
            // Close any open entry dialog defensively in case the user reopened it.
            if (addWifiScaleDialog.opened) addWifiScaleDialog.close()
        }
        function onManualWifiValidationCancelled(requestId) {
            if (requestId !== connectionsTab.manualWifiConnectRequestId) return
            connectionsTab.manualWifiConnectRequestId = ""
        }
        function onManualWifiMdnsDiscovered(requestId, hostname, ip) {
            if (requestId !== connectionsTab.manualWifiProbeRequestId) return
            // Only surface the suggestion if the user still has the dialog
            // open AND hasn't started typing — otherwise the banner would
            // pop in under their fingers, distracting from the address they
            // were entering. (The field is empty on open, so the common
            // "opened-and-waiting" case still gets the banner.)
            if (!addWifiScaleDialog.opened) {
                addWifiScaleDialog.mdnsHostname = hostname
                addWifiScaleDialog.mdnsIp = ip
                addWifiScaleDialog.mdnsSearching = false
                return
            }
            if (wifiScaleHostField.text.length > 0) {
                addWifiScaleDialog.mdnsSearching = false
                return
            }
            addWifiScaleDialog.mdnsHostname = hostname
            addWifiScaleDialog.mdnsIp = ip
            addWifiScaleDialog.mdnsSearching = false
        }
        function onManualWifiMdnsProbeFinished(requestId) {
            if (requestId !== connectionsTab.manualWifiProbeRequestId) return
            connectionsTab.manualWifiProbeRequestId = ""
            addWifiScaleDialog.mdnsSearching = false
        }
    }

    GridLayout {
        anchors.fill: parent
        columns: connectionsTab.width >= connectionsTab.height ? 2 : 1
        columnSpacing: Theme.spacingMedium
        rowSpacing: Theme.spacingMedium

        // Left column: Network on top, Machine below it. It scrolls when both do not
        // fit, rather than squeezing Machine below what its content needs.
        Flickable {
            id: leftColumn
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitWidth: leftColumnLayout.implicitWidth
            contentHeight: leftColumnLayout.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: leftColumnLayout
            width: leftColumn.width
            height: Math.max(leftColumn.height,
                             networkCard.implicitHeight + spacing + machineCard.contentImplicitHeight)
            spacing: Theme.spacingMedium

            SettingsCard {
                id: networkCard
                searchId: "networkCard"
                title: TranslationManager.translate("settings.network.title", "Network")
                description: TranslationManager.translate("settings.network.description", "Web access, sign-in security and Remote Web Access through Tailscale")
                keywords: ["network", "server", "http", "https", "web", "browser", "remote", "share", "security", "totp", "authenticator", "password", "encryption", "tailscale", "funnel", "qr", "phone"]
                showHeader: false
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.network.title"
                    fallback: "Network"
                    color: Theme.textColor
                    font.pixelSize: Theme.bodyFont.pixelSize
                    font.bold: true
                    Accessible.role: Accessible.Heading
                }

                // Enable Network (stored as shotServerEnabled)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "settings.network.enable"
                            fallback: "Enable Network"
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }
                        Tr {
                            key: "settings.data.enableserverdesc"
                            fallback: "Access shot data, layout editor, and AI from your browser"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        SettingsSearch.keywords: ["server"]
                        checked: networkPreferenceWrite.displayedValue("shotServerEnabled", connectionsTab.networkEnabled)
                        accessibleName: TranslationManager.translate("settings.network.enable", "Enable Network")
                        enabled: networkPreferenceWrite.ready && !BrowserDisplay.remoteWebAccess
                        onToggled: networkPreferenceWrite.submit("shotServerEnabled", checked)
                    }
                }

                // Each of these would end this browser's own connection, with no
                // way back from here.
                Tr {
                    Layout.fillWidth: true
                    visible: BrowserDisplay.remoteWebAccess
                    key: "settings.network.remoteHere"
                    fallback: "You're connected through Remote Web Access. Turning the network or Remote Web Access off, signing out of Tailscale and making a new link are done on the device or from home."
                    color: Theme.warningColor
                    font.pixelSize: Theme.labelFont.pixelSize
                    wrapMode: Text.WordWrap
                }

                // LAN address
                RowLayout {
                    id: serverStatusRow
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    visible: connectionsTab.networkEnabled

                    property bool serverRunning: connectionsTab.shotServerSnapshot.running === true
                    property bool secured: serverStatusRow.serverRunning && connectionsTab.shotServerSnapshot.webSecurityEnabled === true &&
                                           connectionsTab.shotServerSnapshot.hasTotpSecret === true

                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(8)
                        Layout.preferredHeight: Theme.scaled(8)
                        radius: Theme.scaled(4)
                        color: !serverStatusRow.serverRunning ? Theme.errorColor :
                               serverStatusRow.secured ? Theme.successColor : Theme.textSecondaryColor
                        Accessible.ignored: true
                    }

                    Text {
                        text: {
                            if (!ShotServerSettingsService.ready)
                                return TranslationManager.translate("settings.data.serverUnavailable",
                                    "Host server status is unavailable while disconnected.");
                            if (!serverStatusRow.serverRunning)
                                return TranslationManager.translate("settings.data.serverstarting", "Starting...");
                            var url = connectionsTab.shotServerSnapshot.url || "";
                            if (serverStatusRow.secured)
                                return url + " • " + TranslationManager.translate("settings.data.secured", "Secured");
                            return url;
                        }
                        color: serverStatusRow.secured ? Theme.successColor :
                               serverStatusRow.serverRunning ? Theme.textColor : Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        font.underline: serverStatusRow.serverRunning
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle

                        AccessibleMouseArea {
                            anchors.fill: parent
                            enabled: serverStatusRow.serverRunning
                            accessibleName: TranslationManager.translate("settings.search.serverAddress", "Open web server address")
                            accessibleRole: Accessible.Link
                            cursorShape: Qt.PointingHandCursor
                            onAccessibleClicked: connectionsTab.openExternal(connectionsTab.shotServerSnapshot.url || "")
                        }
                    }
                }

                // Require authenticator code
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: connectionsTab.networkEnabled

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "settings.data.requireAuthenticator"
                            fallback: "Require authenticator code"
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }
                        Tr {
                            key: "settings.data.requireAuthenticatorDescription"
                            fallback: "Require a code from your authenticator app to sign in."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        SettingsSearch.keywords: ["enable", "security"]
                        checked: networkPreferenceWrite.displayedValue("webSecurityEnabled", connectionsTab.preference("network.webSecurityEnabled", false))
                        accessibleName: TranslationManager.translate("settings.data.requireAuthenticator", "Require authenticator code")
                        enabled: networkPreferenceWrite.ready
                        onToggled: networkPreferenceWrite.submit("webSecurityEnabled", checked)
                    }
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    primary: true
                    visible: connectionsTab.networkEnabled && connectionsTab.shotServerSnapshot.webSecurityEnabled === true
                             && connectionsTab.shotServerSnapshot.hasTotpSecret !== true
                    text: TranslationManager.translate("settings.data.setuptotp", "Set Up Authenticator")
                    accessibleName: TranslationManager.translate("settings.data.setuptotpAccessible",
                        "Set up authenticator app for web access security")
                    enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                    onClicked: connectionsTab.executeShotServer("generateTotpSetup", {})
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    destructive: true
                    visible: connectionsTab.networkEnabled && connectionsTab.shotServerSnapshot.webSecurityEnabled === true
                             && connectionsTab.shotServerSnapshot.hasTotpSecret === true
                    text: TranslationManager.translate("settings.data.resettotp", "Reset Security")
                    accessibleName: TranslationManager.translate("settings.data.resettotpAccessible",
                        "Remove authenticator and all web sessions")
                    enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                    onClicked: totpResetDialog.open()
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.borderColor
                    Accessible.ignored: true
                }

                // Remote Web Access
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "settings.network.remoteWeb"
                            fallback: "Remote Web Access"
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }
                        Text {
                            Layout.fillWidth: true
                            text: connectionsTab.shotServerSnapshot.tunnelAvailable !== true
                                ? TranslationManager.translate("settings.ai.remoteMcp.mode.tailscaleUnavailable", "Not included in this build.")
                                : !connectionsTab.networkEnabled
                                ? TranslationManager.translate("settings.network.remoteWeb.needsNetwork", "Turn on Enable Network first.")
                                : TranslationManager.translate("settings.network.remoteWeb.description",
                                    "Open the web interface from anywhere, such as your phone, through a private Tailscale link.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                    }

                    StyledSwitch {
                        checked: networkPreferenceWrite.displayedValue("remoteWebEnabled", connectionsTab.remoteWebEnabled)
                        accessibleName: TranslationManager.translate("settings.network.remoteWeb", "Remote Web Access")
                        enabled: networkPreferenceWrite.ready && connectionsTab.networkEnabled
                                 && connectionsTab.shotServerSnapshot.tunnelAvailable === true
                                 && !BrowserDisplay.remoteWebAccess
                        onToggled: networkPreferenceWrite.submit("remoteWebEnabled", checked)
                    }
                }

                // Tailscale node: shared by Remote Web Access and the remote MCP connector.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: connectionsTab.tailscaleInUse && connectionsTab.shotServerSnapshot.tunnelAvailable === true

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.network.tailscaleStatus", "Tailscale: %1")
                            .arg(connectionsTab.remoteAccessStatusText())
                        color: (connectionsTab.shotServerSnapshot.remoteAccessStatus || "off") === "error" ? Theme.errorColor
                             : (connectionsTab.shotServerSnapshot.remoteAccessStatus || "off") === "active" ? Theme.successColor
                             : Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }

                    // Sign-in, while the node waits for the user to authorize it.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)
                        visible: connectionsTab.tailscaleLoginUrl.length > 0

                        Tr {
                            key: "settings.ai.remoteMcp.tailscaleLoginLabel"
                            fallback: "Sign in to Tailscale"
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.bold: true
                        }
                        Tr {
                            key: "settings.ai.remoteMcp.tailscaleLoginHint"
                            fallback: "Open this link (or scan the code) to authorize this device on your tailnet. You may also need to approve Funnel for it in the Tailscale admin console."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)
                            Text {
                                Layout.fillWidth: true
                                text: connectionsTab.tailscaleLoginUrl
                                color: Theme.accentColor
                                font.pixelSize: Theme.labelFont.pixelSize
                                wrapMode: Text.WrapAnywhere
                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleLoginAccessible", "Open Tailscale login page")
                                    accessibleRole: Accessible.Link
                                    cursorShape: Qt.PointingHandCursor
                                    onAccessibleClicked: connectionsTab.openExternal(connectionsTab.tailscaleLoginUrl)
                                }
                            }
                            AccessibleButton {
                                text: TranslationManager.translate("common.button.copy", "Copy")
                                accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleLoginCopy", "Copy Tailscale login URL")
                                onClicked: connectionsTab.copyText(connectionsTab.tailscaleLoginUrl, false)
                            }
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Theme.scaled(150)
                            Layout.preferredHeight: Theme.scaled(150)
                            color: "#ffffff"
                            radius: Theme.scaled(8)
                            Accessible.role: Accessible.Graphic
                            Accessible.name: TranslationManager.translate("settings.ai.remoteMcp.tailscaleLoginQr", "QR code of the Tailscale login URL")
                            QrCode {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(8)
                                value: connectionsTab.tailscaleLoginUrl
                                Accessible.ignored: true
                            }
                        }
                    }

                    // One-time setup, offered only while something it covers is missing.
                    AccessibleButton {
                        Layout.fillWidth: true
                        visible: connectionsTab.shotServerSnapshot.tailscaleSetupNeeded === true
                        text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSetupButton", "Set up Tailscale Funnel (one-time)…")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSetupAccessible", "Open Tailscale Funnel setup instructions")
                        warning: (connectionsTab.shotServerSnapshot.remoteAccessStatus || "off") === "error"
                        onClicked: tailscaleSetupDialog.open()
                    }
                }

                // The safe URL, once the public address answers.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)
                    visible: connectionsTab.networkEnabled && connectionsTab.remoteWebEnabled
                             && connectionsTab.remoteWebSafeUrl.length > 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        Text {
                            Layout.fillWidth: true
                            text: connectionsTab.remoteWebSafeUrl
                            color: Theme.accentColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            font.underline: true
                            wrapMode: Text.WrapAnywhere
                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("settings.network.remoteWeb.openAccessible",
                                    "Open the Remote Web Access link in your browser")
                                accessibleRole: Accessible.Link
                                cursorShape: Qt.PointingHandCursor
                                onAccessibleClicked: connectionsTab.openExternal(connectionsTab.remoteWebSafeUrl)
                            }
                        }
                        AccessibleButton {
                            text: TranslationManager.translate("common.button.copy", "Copy")
                            accessibleName: TranslationManager.translate("settings.network.remoteWeb.copyAccessible", "Copy Remote Web Access link")
                            onClicked: connectionsTab.copyText(connectionsTab.remoteWebSafeUrl, false)
                        }
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Theme.scaled(150)
                        Layout.preferredHeight: Theme.scaled(150)
                        color: "#ffffff"
                        radius: Theme.scaled(8)
                        Accessible.role: Accessible.Graphic
                        Accessible.name: TranslationManager.translate("settings.network.remoteWeb.qrAccessible",
                            "QR code of the Remote Web Access link. Use the Copy button if you cannot scan it.")
                        QrCode {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)
                            value: connectionsTab.remoteWebSafeUrl
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: connectionsTab.shotServerSnapshot.webSecurityEnabled === true
                            ? TranslationManager.translate("settings.network.remoteWeb.secured",
                                "Scan with your phone. You'll be asked for your authenticator code.")
                            : TranslationManager.translate("settings.network.remoteWeb.unsecured",
                                "Anyone with this link can control your machine. Turn on “Require authenticator code” to also ask for a code.")
                        color: connectionsTab.shotServerSnapshot.webSecurityEnabled === true ? Theme.textSecondaryColor : Theme.warningColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)
                        Tr {
                            Layout.fillWidth: true
                            key: "settings.network.remoteWeb.rotateHint"
                            fallback: "New link: signs out every browser using this one. The AI connector keeps working."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                        }
                        AccessibleButton {
                            text: TranslationManager.translate("settings.ai.remoteMcp.rotateButton", "Rotate")
                            accessibleName: TranslationManager.translate("settings.network.remoteWeb.rotateAccessible", "Rotate the Remote Web Access link")
                            destructive: true
                            enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                                     && !BrowserDisplay.remoteWebAccess
                            onClicked: rotateRemoteWebDialog.open()
                        }
                    }
                }

                // Sign out of Tailscale: clears this device's tailnet identity, the
                // recovery for a login that keeps failing. Reachable while the
                // node is still waiting for login.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)
                    visible: connectionsTab.tailscaleInUse && connectionsTab.shotServerSnapshot.tunnelAvailable === true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "settings.ai.remoteMcp.tailscaleSignout.label"
                            fallback: "Sign out of Tailscale"
                            color: Theme.textColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }
                        Tr {
                            key: "settings.ai.remoteMcp.tailscaleSignout.hint"
                            fallback: "Clears this device's tailnet identity. Use this if login keeps failing (\"device already exists\" or a 403 error) — then sign in again with the account you want."
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.button", "Sign out")
                        accessibleName: TranslationManager.translate("settings.ai.remoteMcp.tailscaleSignout.accessible", "Sign out of Tailscale and clear this device's tailnet identity")
                        destructive: true
                        enabled: ShotServerSettingsService.ready && !ShotServerSettingsService.busy
                                 && !BrowserDisplay.remoteWebAccess
                        onClicked: tailscaleSignoutDialog.open()
                    }
                }
            }

            // Machine Connection
            SettingsCard {
                id: machineCard
                searchId: "machineConnection"
                title: TranslationManager.translate("settings.bluetooth.machine", "Machine")
                description: TranslationManager.translate("settings.search.machineConnectionDesc", "Connect to DE1 via Bluetooth or USB")
                keywords: ["ble", "bluetooth", "usb", "pair", "connect", "device", "machine"]
                showHeader: false
                fillContent: true
                spacing: Theme.scaled(10)

                // === USB-C view (shown when USB connected, not available on iOS) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: connectionsTab.usbAvailable && connectionsTab.usbState.de1Connected
                    spacing: Theme.scaled(10)

                    // Title row with status badge
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: TranslationManager.translate("connections.usb.de1Title", "DE1 Machine (USB)")
                            color: Theme.textColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: usbStatusText.implicitWidth + Theme.scaled(16)
                            Layout.preferredHeight: Theme.scaled(24)
                            radius: Theme.scaled(12)
                            color: connectionsTab.de1State.connected
                                   ? Qt.rgba(Theme.successColor.r, Theme.successColor.g, Theme.successColor.b, 0.2)
                                   : Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.2)

                            Text {
                                id: usbStatusText
                                anchors.centerIn: parent
                                text: connectionsTab.de1State.connected ? TranslationManager.translate("connections.connected", "Connected") : TranslationManager.translate("connections.connecting", "Connecting...")
                                color: connectionsTab.de1State.connected ? Theme.successColor : Theme.warningColor
                                font.pixelSize: Theme.captionFont.pixelSize
                                font.bold: true
                            }
                        }
                    }

                    // Connection type indicator
                    Text {
                        text: TranslationManager.translate("settings.connections.transport", "Transport:") + " " + (connectionsTab.de1State.connectionType || "USB-C")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                    }

                    // Port info
                    Text {
                        text: TranslationManager.translate("settings.connections.port", "Port:") + " " + (connectionsTab.usbAvailable ? connectionsTab.usbState.portName : "")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                    }

                    // Serial number
                    Text {
                        text: TranslationManager.translate("settings.connections.serial", "Serial:") + " " + (connectionsTab.usbAvailable ? connectionsTab.usbState.serialNumber : "")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        visible: connectionsTab.usbAvailable && connectionsTab.usbState.serialNumber !== ""
                    }

                    // Firmware version
                    Text {
                        text: TranslationManager.translate("settings.bluetooth.firmware", "Firmware:") + " " + (connectionsTab.de1State.firmwareVersion || TranslationManager.translate("settings.bluetooth.unknown", "Unknown"))
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        visible: connectionsTab.de1State.connected
                    }

                    // Machine state
                    Text {
                        text: TranslationManager.translate("settings.connections.state", "State:") + " " + connectionsTab.de1State.stateString
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        visible: connectionsTab.de1State.connected
                    }

                    // Disconnect button
                    AccessibleButton {
                        text: TranslationManager.translate("settings.connections.disconnect", "Disconnect USB")
                        accessibleName: TranslationManager.translate("settings.connections.accessible.disconnectUsb", "Disconnect DE1 USB connection")
                        Layout.alignment: Qt.AlignLeft
                        onClicked: connectionsTab.request("disconnectUsb", {})
                    }

                    // USB-C connection log. Reads [DE1] out of the system log, exactly
                    // like the BLE branch below — this panel and that one are the same
                    // machine, reached over a different wire, so they must not disagree
                    // about what happened to it.
                    //
                    // This replaced two hand-rolled `Connections` handlers appending to
                    // an uncapped `text +=`, and one of them had gone dead: it listened
                    // for BLEManager's `de1LogMessage`, which this change deleted. QML
                    // resolves signal handlers at runtime, so nothing failed at build
                    // time and the panel simply stopped showing DE1Device and
                    // SerialTransport lines — including this change's own new inbound
                    // stall warning, on the one screen a user with a silent USB machine
                    // would be looking at. Reading the log by marker cannot rot that
                    // way: there is no per-signal wiring left to go stale.
                    //
                    // [Bluetooth] rides along because the radio sits beneath the whole
                    // page. A machine on USB does not need it, but the scale paired in
                    // the panel below does, and an adapter that is off or wedged is a
                    // fault neither device's marker reports.
                    SubsystemLogView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: Theme.scaled(120)
                        markers: ["[DE1]", "[Bluetooth]"]
                        accessibleName: TranslationManager.translate("settings.connections.usbLog", "USB connection log")
                    }
                }

                // === BLE view (shown when no USB connection, or always on iOS) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !connectionsTab.usbAvailable || !connectionsTab.usbState.de1Connected
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.bluetooth.machine"
                        fallback: "Machine"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    // Bluetooth off warning
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: btOffRow.implicitHeight + Theme.scaled(16)
                        color: Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.15)
                        radius: Theme.scaled(6)
                        border.color: Theme.errorColor
                        border.width: 1
                        visible: !connectionsTab.connectionSnapshot.bluetoothAvailable

                        RowLayout {
                            id: btOffRow
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                margins: Theme.scaled(10)
                            }
                            spacing: Theme.scaled(8)

                            Image {
                                source: "qrc:/icons/bluetooth.svg"
                                // sourceSize is load-bearing here: bluetooth.svg has a
                                // 649×649 viewBox, so without it the Image's implicit
                                // size (what RowLayout uses) is 649×649, which on Linux
                                // renders as a giant icon overflowing the banner (#830).
                                sourceSize.width: Theme.scaled(18)
                                sourceSize.height: Theme.scaled(18)
                                Layout.preferredWidth: Theme.scaled(18)
                                Layout.preferredHeight: Theme.scaled(18)
                                fillMode: Image.PreserveAspectFit
                            }

                            Tr {
                                Layout.fillWidth: true
                                key: "settings.bluetooth.btOff"
                                fallback: "Bluetooth is turned off. Enable Bluetooth to connect to your DE1."
                                color: Theme.errorColor
                                font.pixelSize: Theme.labelFont.pixelSize
                                wrapMode: Text.Wrap
                                Accessible.ignored: true
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.openSettings", "Open Settings")
                                accessibleName: TranslationManager.translate("settings.bluetooth.btOff", "Bluetooth is turned off. Enable Bluetooth to connect to your DE1.") + " " + TranslationManager.translate("settings.bluetooth.openBtSettings", "Open Bluetooth settings")
                                onClicked: connectionsTab.request("openBluetoothSettings", {})
                            }
                        }

                        Accessible.ignored: true
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Tr {
                            key: "settings.bluetooth.status"
                            fallback: "Status:"
                            color: Theme.textSecondaryColor
                        }

                        Tr {
                            key: "settings.bluetooth.connected"
                            fallback: "Connected"
                            visible: connectionsTab.de1State.connected && !connectionsTab.de1State.simulationMode
                            color: Theme.successColor
                        }

                        Tr {
                            key: "settings.bluetooth.simulated"
                            fallback: "Simulated"
                            visible: connectionsTab.de1State.connected && connectionsTab.de1State.simulationMode
                            color: Theme.warningColor
                        }

                        Tr {
                            key: "settings.bluetooth.disconnected"
                            fallback: "Disconnected"
                            visible: !connectionsTab.de1State.connected
                            color: Theme.errorColor
                        }

                    }

                    Text {
                        text: TranslationManager.translate("settings.bluetooth.firmware", "Firmware:") + " " + (connectionsTab.de1State.firmwareVersion || TranslationManager.translate("settings.bluetooth.unknown", "Unknown"))
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: connectionsTab.de1State.connected && !connectionsTab.de1State.simulationMode
                    }

                    ListView {
                        id: de1DeviceList
                        SettingsSearch.title: TranslationManager.translate("settings.search.discoveredMachines", "Discovered machines")
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(60)
                        clip: true
                        visible: !connectionsTab.de1State.connected
                        model: connectionsTab.discoveredDevices

                        delegate: ItemDelegate {
                            id: delegate
                            required property var modelData

                            width: ListView.view.width
                            contentItem: Text {
                                text: delegate.modelData.name + " (" + delegate.modelData.address + ")"
                                color: Theme.textColor
                            }
                            background: Rectangle {
                                color: delegate.hovered ? Theme.accentColor : "transparent"
                                radius: Theme.scaled(4)
                            }

                            // The row sets contentItem rather than `text`, so Qt has no
                            // string to derive a default accessible name from.
                            Accessible.role: Accessible.Button
                            Accessible.name: delegate.modelData.name + " (" + delegate.modelData.address + ")"
                            Accessible.focusable: true
                            Accessible.onPressAction: delegate.clicked()

                            onClicked: connectionsTab.request("connectDe1", { address: delegate.modelData.address })
                        }

                        // A visual child of a ListView is reparented to its
                        // contentItem (qquickflickable.cpp:2462), so `parent`
                        // here is NOT the ListView: `parent.count` was undefined
                        // and this placeholder never once appeared. Read count
                        // off the list by id, and centre against the list's own
                        // height — contentItem is zero-high when the list is empty.
                        Tr {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: (de1DeviceList.height - height) / 2
                            key: "settings.bluetooth.noDevices"
                            fallback: "No devices found"
                            visible: de1DeviceList.count === 0
                            color: Theme.textSecondaryColor
                        }
                    }

                    // DE1 connection log — the machine's story at INFO and above,
                    // read from the system log rather than from a private signal.
                    //
                    // This used to follow BLEManager::de1LogMessage, which reached
                    // ONLY this window: none of it was in a submitted log, so the
                    // machine's discovery story could not be read after the fact.
                    // Now the same [DE1] lines a user's log contains are the ones on
                    // screen, and the view gains the two things it never had — a cap
                    // and a Clear button.
                    // [Bluetooth] is here because the radio sits BENEATH the machine:
                    // when the adapter is off or wedged the DE1 cannot connect, and
                    // that fault carries neither device's marker. Without it this view
                    // says "the machine never appeared" and cannot say why — the one
                    // answer a log reader must never be given.
                    SubsystemLogView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: Theme.scaled(120)
                        markers: ["[DE1]", "[Bluetooth]"]
                        accessibleName: TranslationManager.translate("settings.connections.de1Log", "DE1 connection log")
                    }
                }

                // Serial USB toggle — not available on iOS
                RowLayout {
                    visible: connectionsTab.usbAvailable
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: TranslationManager.translate("connections.usb.serialLabel", "Scan for USB devices")
                            font.pixelSize: Theme.labelFont.pixelSize
                            color: Theme.textColor
                            Accessible.ignored: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate("connections.usb.serialDesc", "Look for a DE1 or scale connected by USB cable. Disable to save battery; you can still find one with Scan for Devices.")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.captionFont.pixelSize
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: connectionPreferenceWrite.displayedValue("usbSerialEnabled", connectionsTab.preference("connections.usbSerialEnabled", false))
                        accessibleName: TranslationManager.translate("settings.connections.accessible.enableSerialUsb", "Scan for USB-connected devices")
                        onToggled: connectionPreferenceWrite.submit("usbSerialEnabled", checked)
                    }
                }
            }

            // Scale Connection
        }
        }

        SettingsCard {
            searchId: "scaleConnection"
            title: TranslationManager.translate("settings.bluetooth.scalesRefractometer", "Scales / Refractometer")
            description: TranslationManager.translate("settings.search.scaleConnectionDesc", "Pair a Bluetooth scale or refractometer")
            keywords: ["scale", "acaia", "decent", "felicita", "weight", "pair", "tds", "refractometer", "atago"]
            // The header scrolls with the content, inside the Flickable.
            showHeader: false
            fillContent: true
            clip: true

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: scaleColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: scaleColumn
                width: parent.width
                spacing: Theme.scaled(10)

                // === Scale view (BLE / WiFi / USB — all transports share this
                // unified view; USB shows up as the connected/selected scale and
                // stays switchable to other transports via Known Devices) ===
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.bluetooth.scalesRefractometer"
                        fallback: "Scales / Refractometer"
                        color: Theme.textColor
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Tr {
                            key: "settings.bluetooth.status"
                            fallback: "Status:"
                            color: Theme.textSecondaryColor
                        }

                        QtObject {
                            id: scaleStatusHelper
                            property bool isFlowScale: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.isFlowScale && connectionsTab.preference("connections.useFlowScale", false)
                            property bool isSimulated: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.isSimulated
                            // FlowScale fallback after physical disconnect — treat as disconnected
                            property bool isDisconnectedFallback: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.isFlowScale && !connectionsTab.preference("connections.useFlowScale", false)
                            property bool isConnected: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.connected && !isDisconnectedFallback
                        }

                        Tr {
                            key: "settings.bluetooth.connected"
                            fallback: "Connected"
                            visible: scaleStatusHelper.isConnected && !scaleStatusHelper.isFlowScale && !scaleStatusHelper.isSimulated
                            color: Theme.successColor
                        }

                        Tr {
                            key: "settings.bluetooth.virtualScale"
                            fallback: "Virtual Scale"
                            visible: scaleStatusHelper.isConnected && scaleStatusHelper.isFlowScale
                            color: Theme.warningColor
                        }

                        Tr {
                            key: "settings.bluetooth.simulated"
                            fallback: "Simulated"
                            visible: scaleStatusHelper.isConnected && scaleStatusHelper.isSimulated && !scaleStatusHelper.isFlowScale
                            color: Theme.warningColor
                        }

                        Tr {
                            key: "settings.bluetooth.connecting"
                            fallback: "Connecting..."
                            visible: !scaleStatusHelper.isConnected && connectionsTab.connectionSnapshot.scaleConnecting
                            color: Theme.textSecondaryColor
                        }

                        Tr {
                            key: "settings.bluetooth.notFound"
                            fallback: "Not found"
                            visible: !scaleStatusHelper.isConnected && !connectionsTab.connectionSnapshot.scaleConnecting && connectionsTab.connectionSnapshot.scaleConnectionFailed
                            color: Theme.errorColor
                        }

                        Tr {
                            key: "settings.bluetooth.disconnected"
                            fallback: "Disconnected"
                            visible: !scaleStatusHelper.isConnected && !connectionsTab.connectionSnapshot.scaleConnecting && !connectionsTab.connectionSnapshot.scaleConnectionFailed
                            color: Theme.textSecondaryColor
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: connectionsTab.connectionSnapshot.scanning ? TranslationManager.translate("settings.bluetooth.scanning", "Scanning...") : TranslationManager.translate("settings.bluetooth.scanForDevices", "Scan for Devices")
                            SettingsSearch.title: TranslationManager.translate("settings.bluetooth.scanForDevices", "Scan for Devices")
                            accessibleName: connectionsTab.connectionSnapshot.scanning
                                ? TranslationManager.translate("settings.bluetooth.accessible.scanning", "Scanning for devices")
                                : TranslationManager.translate("settings.bluetooth.accessible.scan", "Scan for Bluetooth DE1, scales, and refractometers")
                            enabled: !connectionsTab.connectionSnapshot.scanning
                            onClicked: {
                                connectionsTab.portalScanTriggered = true
                                MachineControlService.scanForDevices()
                            }
                        }
                    }

                    // Connected scale name + battery (transport-agnostic; the
                    // header above already routes BLE/WiFi/USB through this same row).
                    RowLayout {
                        Layout.fillWidth: true
                        visible: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.connected && !connectionsTab.scaleState.isFlowScale

                        Tr {
                            key: "settings.bluetooth.connectedScale"
                            fallback: "Connected:"
                            color: Theme.textSecondaryColor
                        }

                        Text {
                            text: connectionsTab.connectionSnapshot.scale !== undefined ? connectionsTab.scaleState.name : ""
                            color: Theme.textColor
                            font.pixelSize: Theme.bodyFont.pixelSize
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }

                        Row {
                            spacing: Theme.scaled(4)
                            visible: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.batteryLevel >= 0 && connectionsTab.scaleState.batteryLevel <= 100

                            // While charging, the BT path reports a 0xFF sentinel that our parser
                            // maps to batteryLevel=100, and the WiFi percent is similarly unreliable
                            // when plugged in. Mirror ScaleBatteryItem.qml's treatment: swap the
                            // icon to battery-charging.svg and replace the percent with "Charging".
                            readonly property bool charging: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.charging

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                source: {
                                    if (parent.charging) return "qrc:/icons/battery-charging.svg"
                                    var level = connectionsTab.connectionSnapshot.scale !== undefined ? connectionsTab.scaleState.batteryLevel : 0
                                    if (level <= 10) return "qrc:/icons/battery-0.svg"
                                    if (level <= 37) return "qrc:/icons/battery-25.svg"
                                    if (level <= 62) return "qrc:/icons/battery-50.svg"
                                    if (level <= 87) return "qrc:/icons/battery-75.svg"
                                    return "qrc:/icons/battery-100.svg"
                                }
                                sourceSize.width: Theme.scaled(14)
                                sourceSize.height: Theme.scaled(14)
                                Accessible.ignored: true
                            }

                            Text {
                                text: parent.charging
                                      ? TranslationManager.translate("scaleBattery.display.charging", "Charging")
                                      : (connectionsTab.connectionSnapshot.scale !== undefined ? connectionsTab.scaleState.batteryLevel : 0) + "%"
                                color: {
                                    if (parent.charging) return Theme.successColor
                                    var level = connectionsTab.connectionSnapshot.scale !== undefined ? connectionsTab.scaleState.batteryLevel : 0
                                    if (level > 50) return Theme.successColor
                                    if (level > 20) return Theme.warningColor
                                    return Theme.errorColor
                                }
                                font.pixelSize: Theme.labelFont.pixelSize
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Virtual scale notice (FlowScale active)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: flowScaleNotice.implicitHeight + 16
                        radius: Theme.scaled(6)
                        color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                        border.color: Theme.primaryColor
                        border.width: 1
                        visible: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.isFlowScale && connectionsTab.preference("connections.useFlowScale", false)

                        Text {
                            id: flowScaleNotice
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)
                            text: TranslationManager.translate("settings.bluetooth.flowScaleNotice",
                                  "Using Virtual Scale — estimating cup weight from flow data. Set your dose weight for best accuracy.")
                            color: Theme.primaryColor
                            font.pixelSize: Theme.captionFont.pixelSize
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Simulated scale notice
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: simScaleNotice.implicitHeight + 16
                        radius: Theme.scaled(6)
                        color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                        border.color: Theme.warningColor
                        border.width: 1
                        visible: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.isSimulated

                        Text {
                            id: simScaleNotice
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(8)
                            text: TranslationManager.translate("settings.bluetooth.simulatedScaleNotice", "Using Simulated Scale (simulator mode)")
                            color: Theme.warningColor
                            font.pixelSize: Theme.captionFont.pixelSize
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Known Devices picker (scales + refractometer)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: connectionsTab.knownScales.length > 0 || connectionsTab.savedRefractometerAddress !== ""
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.bluetooth.knownDevices"
                            fallback: "Known Devices"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.labelFont.pixelSize
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            // Primary scale picker \u2014 proper Qt ComboBox so the
                            // affordance always matches the behavior. The popup
                            // is capped at 3 visible rows; longer lists scroll
                            // inline instead of opening a separate dialog.
                            ComboBox {
                                id: scalePicker
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(36)

                                readonly property int rowHeight: Theme.scaled(44)
                                readonly property int visibleRows: 3
                                // More entries than fit means the dropdown has to
                                // SAY so. A transient ScrollIndicator does not —
                                // it only appears once you are already flicking,
                                // so a list of four reads as a list of three and
                                // the fourth scale looks like it was never saved.
                                readonly property bool listScrollable:
                                    connectionsTab.knownScales.length > visibleRows
                                readonly property int scrollBarWidth: Theme.scaled(6)

                                function indexOfPrimary() {
                                    var scales = connectionsTab.knownScales
                                    for (let i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary) return i
                                    }
                                    return -1
                                }
                                function primaryLabel() {
                                    var scales = connectionsTab.knownScales
                                    for (let i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary)
                                            return scales[i].name || scales[i].type
                                    }
                                    return TranslationManager.translate("settings.bluetooth.noScale", "No scale selected")
                                }
                                function transportLabel(typeId) {
                                    if (!typeId) return ""
                                    if (typeId === "decent-wifi") return "WiFi"
                                    if (typeId === "decent-usb") return "USB"
                                    return "BLE"
                                }

                                model: connectionsTab.knownScales
                                currentIndex: indexOfPrimary()

                                // Re-sync when the underlying list (or which one is primary) changes.
                                Connections {
                                    target: SettingsConnectionsService
                                    function onStateChanged() {
                                        scalePicker.currentIndex = scalePicker.indexOfPrimary()
                                    }
                                }

                                Accessible.role: Accessible.ComboBox
                                Accessible.name: primaryLabel()
                                SettingsSearch.title: TranslationManager.translate("settings.bluetooth.knownDevices", "Known Devices")
                                Accessible.focusable: true
                                // TalkBack double-tap "activate" → open the
                                // dropdown. Mirrors the StyledComboBox pattern.
                                Accessible.onPressAction: if (!scalePicker.popup.visible) scalePicker.popup.open()

                                onActivated: function(index) {
                                    var scales = connectionsTab.knownScales
                                    if (index < 0 || index >= scales.length) {
                                        // TOCTOU: scan/forget mutated the model between
                                        // the user's tap and this handler. Log so we can
                                        // tell this apart from a no-op re-select.
                                        WebDebugLogger.warn("Scale", "SettingsConnectionsTab", ["scalePicker: stale activated index", index,
                                                     "model length", scales.length].map(String).join(" "))
                                        return
                                    }
                                    var scale = scales[index]
                                    if (scale.isPrimary) return
                                    connectionsTab.request("selectPrimaryScale", {
                                        address: scale.address,
                                        expectedRevision: connectionsTab.connectionSnapshot.knownScalesRevision
                                    })
                                }

                                background: Rectangle {
                                    radius: Theme.scaled(6)
                                    color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.12)
                                    border.color: Theme.accentColor
                                    border.width: 1
                                }

                                // Suppress the default style indicator entirely
                                // and draw a single chevron at the end of the
                                // contentItem layout, so there's only one arrow.
                                indicator: Item { width: 0; height: 0 }

                                // Closed-state content: live status dot + name + transport badge + chevron.
                                contentItem: RowLayout {
                                    spacing: Theme.scaled(6)

                                    Rectangle {
                                        Layout.leftMargin: Theme.scaled(10)
                                        Layout.preferredWidth: Theme.scaled(8)
                                        Layout.preferredHeight: Theme.scaled(8)
                                        radius: Theme.scaled(4)
                                        // scaleStatusHelper, not connectionsTab.scaleState.connected: after a
                                        // physical scale is released the app falls back to
                                        // FlowScale, which reports connected — the raw check lit
                                        // this dot green next to a device just unplugged.
                                        color: scaleStatusHelper.isConnected
                                               ? Theme.successColor
                                               : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: scalePicker.primaryLabel()
                                        color: Theme.textColor
                                        font.pixelSize: Theme.labelFont.pixelSize
                                        font.bold: true
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        Accessible.ignored: true
                                    }

                                    Rectangle {
                                        property string badge: {
                                            var scales = connectionsTab.knownScales
                                            for (let i = 0; i < scales.length; i++) {
                                                if (scales[i].isPrimary)
                                                    return scalePicker.transportLabel(scales[i].type)
                                            }
                                            return ""
                                        }
                                        visible: badge.length > 0
                                        Layout.preferredWidth: badgeText.implicitWidth + Theme.scaled(10)
                                        Layout.preferredHeight: Theme.scaled(18)
                                        radius: Theme.scaled(9)
                                        color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.22)
                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: parent.badge
                                            color: Theme.accentColor
                                            font.pixelSize: Theme.captionFont.pixelSize
                                            font.bold: true
                                        }
                                    }

                                    Text {
                                        Layout.rightMargin: Theme.scaled(10)
                                        text: "\u25BC"
                                        font.pixelSize: Theme.captionFont.pixelSize
                                        color: Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }
                                }

                                // Per-row delegate: star (filled on primary) + name + transport badge.
                                delegate: ItemDelegate {
                                    id: scaleRowDelegate
                                    required property var modelData
                                    required property int index

                                    // Yield the scrollbar's strip when one is
                                    // showing — the bar overlays the flickable,
                                    // and a full-width row puts it on top of the
                                    // transport badge.
                                    width: scalePicker.width
                                           - (scalePicker.listScrollable ? scalePicker.scrollBarWidth : 0)
                                    height: scalePicker.rowHeight
                                    highlighted: scalePicker.highlightedIndex === index

                                    // `modelData["name"]` (bracket form) bypasses
                                    // QML's reserved-property lookup on `name`
                                    // — defensive per docs/CLAUDE_MD/QML_GOTCHAS.md
                                    // even though QVariantMap-backed models work
                                    // with dot notation in practice.
                                    readonly property string _label:
                                        modelData["name"] || modelData.type ||
                                        TranslationManager.translate("settings.bluetooth.unknownScale", "Unknown scale")

                                    Accessible.role: Accessible.Button
                                    Accessible.name: _label
                                                     + " " + scalePicker.transportLabel(modelData.type)
                                                     + (modelData.isPrimary
                                                        ? ", " + TranslationManager.translate("connections.primary", "primary")
                                                        : "")
                                    Accessible.focusable: true
                                    Accessible.onPressAction: scaleRowDelegate.clicked()

                                    contentItem: RowLayout {
                                        spacing: Theme.scaled(8)

                                        Image {
                                            source: "qrc:/icons/star.svg"
                                            sourceSize.width: Theme.scaled(14)
                                            sourceSize.height: Theme.scaled(14)
                                            opacity: scaleRowDelegate.modelData.isPrimary ? 1.0 : 0.25
                                            Accessible.ignored: true
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: scaleRowDelegate._label
                                            color: Theme.textColor
                                            font.pixelSize: Theme.labelFont.pixelSize
                                            font.bold: scaleRowDelegate.modelData.isPrimary
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            Accessible.ignored: true
                                        }

                                        Rectangle {
                                            property string badge: scalePicker.transportLabel(scaleRowDelegate.modelData.type)
                                            visible: badge.length > 0
                                            Layout.preferredWidth: rowBadgeText.implicitWidth + Theme.scaled(10)
                                            Layout.preferredHeight: Theme.scaled(18)
                                            radius: Theme.scaled(9)
                                            color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.2)
                                            Accessible.ignored: true
                                            Text {
                                                id: rowBadgeText
                                                anchors.centerIn: parent
                                                text: parent.badge
                                                color: Theme.accentColor
                                                font.pixelSize: Theme.captionFont.pixelSize
                                                font.bold: true
                                            }
                                        }
                                    }

                                    background: Rectangle {
                                        color: scaleRowDelegate.highlighted
                                               ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.18)
                                               : "transparent"
                                    }
                                }

                                // Use a Dialog (not Popup) so TalkBack can trap
                                // focus inside the dropdown list and navigate
                                // rows. Dialog inherits from Popup, so it's a
                                // drop-in for ComboBox.popup. We position it
                                // exactly where the Popup used to render, with
                                // no header/footer/buttons, so the visual is
                                // unchanged — only the accessibility tree
                                // changes (TalkBack sees a modal it can enter).
                                // See ACCESSIBILITY.md "Popup for selection lists".
                                popup: Dialog {
                                    y: scalePicker.height
                                    width: scalePicker.width
                                    modal: true
                                    header: null
                                    footer: null
                                    standardButtons: Dialog.NoButton
                                    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
                                    // Pin paddings so the style can't override them
                                    // and silently eat into the visible row count.
                                    leftPadding: 0
                                    rightPadding: 0
                                    topPadding: Theme.scaled(4)
                                    bottomPadding: Theme.scaled(4)
                                    // Cap visible height at `visibleRows`; longer lists scroll.
                                    // Drive sizing off the model length directly — ComboBox.count
                                    // can lag behind a QVariantList model after refresh.
                                    //
                                    // When there IS more, show half of the next row
                                    // rather than a clean edge at `visibleRows`. A
                                    // list cut exactly on a row boundary looks
                                    // complete; a sliced row cannot be mistaken for
                                    // the end of the list, and it works before any
                                    // interaction, unlike the scrollbar alone.
                                    height: (Math.min(connectionsTab.knownScales.length, scalePicker.visibleRows)
                                             + (scalePicker.listScrollable ? 0.5 : 0)) * scalePicker.rowHeight
                                            + topPadding + bottomPadding

                                    // Move keyboard focus into the list when the
                                    // dropdown opens so TalkBack/VoiceOver lands
                                    // on the highlighted row (the primary on
                                    // first open) instead of leaving focus on
                                    // the ComboBox. Defer via Qt.callLater so
                                    // the delegate items are guaranteed to be
                                    // realized before focus is requested.
                                    onOpened: Qt.callLater(scaleDropdownList.forceActiveFocus)

                                    contentItem: ListView {
                                        id: scaleDropdownList
                                        clip: true
                                        focus: true
                                        keyNavigationEnabled: true
                                        model: scalePicker.popup.visible ? scalePicker.delegateModel : null
                                        currentIndex: scalePicker.highlightedIndex
                                        // AlwaysOn, not AsNeeded: AsNeeded still
                                        // fades to zero opacity until the list is
                                        // active, which is exactly the case that
                                        // needs the cue.
                                        ScrollBar.vertical: ScrollBar {
                                            policy: scalePicker.listScrollable ? ScrollBar.AlwaysOn
                                                                               : ScrollBar.AlwaysOff
                                            width: scalePicker.scrollBarWidth
                                        }
                                    }

                                    background: Rectangle {
                                        radius: Theme.scaled(6)
                                        color: Theme.surfaceColor
                                        border.color: Theme.accentColor
                                        border.width: 1
                                    }
                                }
                            }

                            // Forget button — forgets the currently selected (primary) scale
                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.forget", "Forget")
                                SettingsSearch.title: TranslationManager.translate("connections.forgetScale", "Forget scale")
                                accessibleName: {
                                    var scales = connectionsTab.knownScales
                                    for (let i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary)
                                            return TranslationManager.translate("connections.forgetScale", "Forget scale") + " " + scales[i].name
                                    }
                                    return TranslationManager.translate("settings.bluetooth.forget", "Forget")
                                }
                                onClicked: {
                                    var scales = connectionsTab.knownScales
                                    for (let i = 0; i < scales.length; i++) {
                                        if (scales[i].isPrimary) {
                                            connectionsTab.request("removeScale", {
                                                address: scales[i].address,
                                                expectedRevision: connectionsTab.connectionSnapshot.knownScalesRevision
                                            })
                                            return
                                        }
                                    }
                                }
                            }

                            AccessibleButton {
                                visible: connectionsTab.firmwareState.updateAvailable
                                text: TranslationManager.translate("connections.hdsUpdate", "Update")
                                accessibleName: TranslationManager.translate("connections.hdsUpdateScale",
                                                                             "Update Half Decent Scale")
                                primary: true
                                onClicked: hdsFirmwareUpdateDialog.open()
                            }
                        }

                        // Saved refractometer display (below scale picker)
                        RowLayout {
                            Layout.fillWidth: true
                            visible: connectionsTab.savedRefractometerAddress !== ""
                            spacing: Theme.scaled(8)

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(36)
                                radius: Theme.scaled(6)
                                color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12)
                                border.color: Theme.primaryColor
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.scaled(10)
                                    anchors.rightMargin: Theme.scaled(10)
                                    spacing: Theme.scaled(6)

                                    // Status dot
                                    Rectangle {
                                        Layout.preferredWidth: Theme.scaled(8)
                                        Layout.preferredHeight: Theme.scaled(8)
                                        radius: Theme.scaled(4)
                                        color: connectionsTab.connectionSnapshot.refractometerConnected ? Theme.successColor : Theme.textSecondaryColor
                                        Accessible.ignored: true
                                    }

                                    Text {
                                        text: connectionsTab.savedRefractometerName || TranslationManager.translate("connections.refractometer", "Refractometer")
                                        color: Theme.textColor
                                        font.pixelSize: Theme.labelFont.pixelSize
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        Accessible.ignored: true
                                    }

                                    // Type badge
                                    Rectangle {
                                        Layout.preferredWidth: refBadgeText.implicitWidth + Theme.scaled(8)
                                        Layout.preferredHeight: Theme.scaled(18)
                                        radius: Theme.scaled(9)
                                        color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2)

                                        Text {
                                            id: refBadgeText
                                            anchors.centerIn: parent
                                            text: TranslationManager.translate("connections.refractometer", "Refractometer")
                                            color: Theme.primaryColor
                                            font.pixelSize: Theme.captionFont.pixelSize
                                            font.bold: true
                                            Accessible.ignored: true
                                        }
                                    }
                                }
                            }

                            // Auto Test toggle. Bound to the setting, not to the device:
                            // the R2 is only connected while the post-shot review page is
                            // open, so a device-only control would be greyed out almost
                            // everywhere the user would want to reach it. The setting is
                            // pushed to the refractometer on every connect (main.cpp), so
                            // changing it with the device away still takes effect.
                            AccessibleButton {
                                id: autoTestButton
                                readonly property bool autoTestOn: appPreferenceWrite.displayedValue("refractometerAutoTest", connectionsTab.preference("app.refractometerAutoTest", false))
                                // An R1 has no Auto Test. Offering the toggle anyway
                                // persisted a setting that the device never honoured and
                                // that main.cpp never even wired up, with no feedback.
                                // While disconnected the saved name is the only evidence
                                // of which model is paired.
                                readonly property bool deviceSupports:
                                    (connectionsTab.savedRefractometerName || "")
                                        .toLowerCase().indexOf("dft_tdj") !== 0

                                visible: deviceSupports
                                text: autoTestOn
                                    ? TranslationManager.translate("connections.autoTestOn", "Auto Test: On")
                                    : TranslationManager.translate("connections.autoTestOff", "Auto Test: Off")
                                accessibleName: autoTestOn
                                    ? TranslationManager.translate("connections.autoTestTurnOff",
                                        "Turn off Auto Test — the refractometer will no longer measure by itself")
                                    : TranslationManager.translate("connections.autoTestTurnOn",
                                        "Turn on Auto Test — the refractometer measures by itself when a sample is loaded")
                                onClicked: appPreferenceWrite.submit("refractometerAutoTest", !autoTestOn)
                                SettingsSearch.title: TranslationManager.translate("settings.search.refractometerAutoTest", "Refractometer Auto Test")
                            }

                            AccessibleButton {
                                text: TranslationManager.translate("settings.bluetooth.forget", "Forget")
                                accessibleName: TranslationManager.translate("connections.forgetRefractometer", "Forget refractometer")
                                onClicked: {
                                    connectionsTab.request("clearRefractometer", {
                                        expectedRevision: connectionsTab.connectionSnapshot.refractometerRevision
                                    })
                                }
                            }
                        }
                    }

                    // Reading TDS lives on the post-shot review page (where the
                    // R2 is used); Settings only manages pairing (status + Forget
                    // above) and discovery (list below), so no Read-TDS control here.

                    // Scale connection alert toggle
                    RowLayout {
                        Layout.fillWidth: true
                        visible: connectionsTab.knownScales.length > 0
                        spacing: Theme.scaled(15)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Tr {
                                key: "settings.bluetooth.scaleDialogs"
                                fallback: "Scale connection alerts"
                                font.pixelSize: Theme.labelFont.pixelSize
                                color: Theme.textColor
                                Accessible.ignored: true
                            }
                            Tr {
                                Layout.fillWidth: true
                                key: "settings.bluetooth.scaleDialogsDesc"
                                fallback: "Show popup when scale disconnects or is not found"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.captionFont.pixelSize
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            checked: connectionPreferenceWrite.displayedValue("showScaleDialogs", connectionsTab.preference("connections.showScaleDialogs", true))
                            accessibleName: TranslationManager.translate("connections.scaleConnectionAlerts", "Scale connection alerts")
                            onToggled: connectionPreferenceWrite.submit("showScaleDialogs", checked)
                        }
                    }

                    // Keep scale connected when DE1 sleeps
                    RowLayout {
                        Layout.fillWidth: true
                        visible: connectionsTab.knownScales.length > 0
                        spacing: Theme.scaled(15)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: TranslationManager.translate("settings.bluetooth.keepScaleOn",
                                                                   "Keep scale connected when DE1 sleeps")
                                font.pixelSize: Theme.labelFont.pixelSize
                                color: Theme.textColor
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: TranslationManager.translate(
                                    "settings.bluetooth.keepScaleOnDesc",
                                    "Turn off to power down and disconnect the scale when the DE1 sleeps. Recommended for battery-only scales; reconnects automatically when the DE1 wakes.")
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.captionFont.pixelSize
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }

                        Item { Layout.fillWidth: true }

                        StyledSwitch {
                            id: keepScaleOnSwitch
                            checked: connectionPreferenceWrite.displayedValue("keepScaleOn", connectionsTab.preference("connections.keepScaleOn", true))
                            accessibleName: TranslationManager.translate(
                                "settings.bluetooth.keepScaleOn",
                                "Keep scale connected when DE1 sleeps")
                            onToggled: connectionPreferenceWrite.submit("keepScaleOn", checked)
                        }
                    }

                    // Show weight when connected
                    RowLayout {
                        Layout.fillWidth: true
                        visible: connectionsTab.connectionSnapshot.scale !== undefined && connectionsTab.scaleState.connected

                        Tr {
                            key: "settings.bluetooth.weight"
                            fallback: "Weight:"
                            color: Theme.textSecondaryColor
                        }

                        Text {
                            text: MachineState.scaleWeight.toFixed(1) + " g"
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: TranslationManager.translate("settings.bluetooth.tare", "Tare")
                            accessibleName: TranslationManager.translate("connections.tareScaleToZero", "Tare scale to zero")
                            onClicked: {
                                connectionsTab.request("tareScale", {})
                            }
                        }
                    }

                    Loader {
                        id: portalDevicePanelLoader
                        Layout.fillWidth: true
                        SettingsSearch.title: TranslationManager.translate("settings.search.portalDevice", "PORTAL device")
                        // Not just visible: false — a user who never discovers
                        // a PORTAL must not pay for constructing this panel's
                        // ~15 items and ~18 translated bindings either.
                        active: connectionsTab.portalState.owned || connectionsTab.portalState.state !== "disconnected"
                        visible: active
                        sourceComponent: PortalDevicePanel {
                            width: portalDevicePanelLoader.width
                            requestAction: function(operation, args) { connectionsTab.request(operation, args) }
                        }
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.bluetooth.availableDevices"
                        fallback: "Available Devices"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        visible: discoveredDevicesList.visible && discoveredDevicesList.count > 0
                            && !discoveredDevicesList.needsScaleSelection
                    }

                    Tr {
                        Layout.fillWidth: true
                        key: "settings.bluetooth.selectDiscoveredScale"
                        fallback: "Scale found, select your scale:"
                        color: Theme.textColor
                        font.pixelSize: Theme.labelFont.pixelSize
                        wrapMode: Text.WordWrap
                        visible: discoveredDevicesList.visible && discoveredDevicesList.needsScaleSelection
                    }

                    // Unified discovered devices list (scales + refractometers).
                    // Height scales with the number of items so rows aren't
                    // clipped below the fold (notably the WiFi-scale row when
                    // both BLE and WiFi entries are present after a scan).
                    ListView {
                        id: discoveredDevicesList
                        SettingsSearch.title: TranslationManager.translate("settings.bluetooth.availableDevices", "Available Devices")
                        Layout.fillWidth: true
                        // Min 80px, grow up to ~160px so 3-4 rows fit without scrolling.
                        Layout.preferredHeight: Math.max(Theme.scaled(80),
                                                          Math.min(count, 4) * Theme.scaled(40))
                        clip: true
                        visible: connectionsTab.connectionSnapshot.scale === undefined || !connectionsTab.scaleState.connected || connectionsTab.scaleState.isFlowScale || !connectionsTab.connectionSnapshot.refractometerConnected
                            || (connectionsTab.portalScanTriggered && connectionsTab.portalState.devices.length > 0)
                        readonly property bool needsScaleSelection: connectionsTab.primaryScaleAddress === ""
                            && combinedModel.some(function(device) { return device.deviceClass === "scale" })

                        // Combine scales + refractometers. The model is rebuilt
                        // explicitly via Connections handlers below — relying on
                        // QML binding tracking (`property var foo: { ...read... }`
                        // or `: helper(connectionsTab.discoveredScales)`) proved
                        // unreliable for QVariantList properties in Qt 6.11:
                        // scalesChanged fired (verified via appendScaleLog) but
                        // the binding didn't re-evaluate and the list stayed
                        // empty in the UI. Explicit assignment from a Connections
                        // signal handler is the bulletproof path.
                        function buildCombinedModel(scales, refractometers) {
                            // Dedup against Known Devices: a scale or refractometer
                            // that's already saved as known shouldn't appear in the
                            // newly-discovered list — the user manages it from the
                            // Known Devices section above. Keys by address (which
                            // for WiFi is "wifi:hostname" — matches what we save).
                            var known = {}
                            var knownScales = connectionsTab.knownScales
                            for (let k = 0; k < knownScales.length; k++) {
                                known[knownScales[k].address] = true
                            }
                            var savedRefAddr = connectionsTab.savedRefractometerAddress || ""

                            var items = []
                            var skippedScales = 0
                            for (let i = 0; i < scales.length; i++) {
                                if (known[scales[i].address]) { skippedScales++; continue }
                                items.push({ deviceName: scales[i].name, address: scales[i].address,
                                             deviceType: scales[i].type, deviceClass: "scale" })
                            }
                            var skippedRefs = 0
                            for (let j = 0; j < refractometers.length; j++) {
                                if (savedRefAddr && refractometers[j].address === savedRefAddr) {
                                    skippedRefs++; continue
                                }
                                items.push({ deviceName: refractometers[j].name, address: refractometers[j].address,
                                             deviceType: refractometers[j].type, deviceClass: "refractometer" })
                            }
                            var portals = connectionsTab.portalState.devices
                            for (let p = 0; p < portals.length; p++) {
                                if (portals[p].identifier === connectionsTab.portalState.savedAddress) continue
                                items.push({ deviceName: portals[p].label, address: portals[p].identifier,
                                             deviceType: "PORTAL", deviceClass: "portal" })
                            }
                            WebDebugLogger.debug("Scale", "SettingsConnectionsTab", ["discoveredDevicesList combinedModel rebuilt:",
                                        "scales=" + scales.length + "(-" + skippedScales + " known)",
                                        "refractometers=" + refractometers.length + "(-" + skippedRefs + " known)",
                                        "→ items=" + items.length].map(String).join(" "))
                            return items
                        }
                        property var combinedModel: []
                        model: combinedModel

                        Component.onCompleted: {
                            combinedModel = buildCombinedModel(connectionsTab.discoveredScales,
                                                               connectionsTab.discoveredRefractometers)
                        }

                        // Rebuild when the Known Devices set changes — a scale
                        // moving from discovered → known should disappear from
                        // this list since it's now managed above.
                        Connections {
                            target: SettingsConnectionsService
                            function onStateChanged() {
                                discoveredDevicesList.combinedModel =
                                    discoveredDevicesList.buildCombinedModel(connectionsTab.discoveredScales,
                                                                              connectionsTab.discoveredRefractometers)
                            }
                        }

                        delegate: ItemDelegate {
                            id: delegate2
                            required property var modelData
                            enabled: modelData.deviceClass !== "portal"
                                || (!connectionsTab.portalState.active && connectionsTab.portalState.state !== "disconnecting" && !connectionsTab.portalState.machineBusy)

                            width: ListView.view.width

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.deviceName + ", " + (modelData.deviceClass === "refractometer"
                                ? TranslationManager.translate("connections.refractometer", "Refractometer")
                                : modelData.deviceType)
                            Accessible.focusable: true
                            Accessible.onPressAction: delegate2.clicked()

                            contentItem: RowLayout {
                                Text {
                                    text: delegate2.modelData.deviceName
                                    color: Theme.textColor
                                    Layout.fillWidth: true
                                    Accessible.ignored: true
                                }
                                // Type badge
                                Rectangle {
                                    Layout.preferredWidth: discoveredBadgeText.implicitWidth + Theme.scaled(8)
                                    Layout.preferredHeight: Theme.scaled(18)
                                    radius: Theme.scaled(9)
                                    color: delegate2.modelData.deviceClass === "refractometer"
                                        ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2)
                                        : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.2)

                                    Text {
                                        id: discoveredBadgeText
                                        anchors.centerIn: parent
                                        text: delegate2.modelData.deviceClass === "refractometer"
                                            ? TranslationManager.translate("connections.refractometer", "Refractometer")
                                            : delegate2.modelData.deviceType
                                        color: delegate2.modelData.deviceClass === "refractometer" ? Theme.primaryColor : Theme.accentColor
                                        font.pixelSize: Theme.captionFont.pixelSize
                                        font.bold: true
                                        Accessible.ignored: true
                                    }
                                }
                            }
                            background: Rectangle {
                                color: delegate2.hovered ? Theme.accentColor : "transparent"
                                radius: Theme.scaled(4)
                            }
                            onClicked: {
                                if (modelData.deviceClass === "portal")
                                    connectionsTab.request("connectPortal", { identifier: modelData.address })
                                else if (modelData.deviceClass === "refractometer")
                                    connectionsTab.request("connectRefractometer", { address: modelData.address })
                                else
                                    connectionsTab.request("connectScale", { address: modelData.address })
                            }
                        }

                        // Same contentItem-reparenting trap as the DE1 list above:
                        // `parent` is the contentItem, so `parent.count` was
                        // undefined and this never showed.
                        Tr {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: (discoveredDevicesList.height - height) / 2
                            key: "settings.bluetooth.noDevices"
                            fallback: "No devices found"
                            visible: discoveredDevicesList.count === 0
                                && connectionsTab.knownScales.length === 0
                                && connectionsTab.savedRefractometerAddress === ""
                            color: Theme.textSecondaryColor
                        }
                    }

                    // Manual WiFi-scale entry — a quiet fallback for when mDNS
                    // discovery doesn't surface the scale. Rarely needed, so it's
                    // a low-emphasis link rather than a button next to Scan.
                    Tr {
                        Layout.fillWidth: true
                        key: "settings.bluetooth.addWifiScaleLink"
                        fallback: "Scale not found? Add a WiFi scale by IP…"
                        color: Theme.accentColor
                        font.pixelSize: Theme.captionFont.pixelSize
                        horizontalAlignment: Text.AlignHCenter
                        topPadding: Theme.scaled(8)
                        bottomPadding: Theme.scaled(4)
                        Accessible.ignored: true  // AccessibleMouseArea carries the a11y node

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("settings.bluetooth.addWifiScaleAccessible", "Add a WiFi scale by IP address or name")
                            onAccessibleClicked: addWifiScaleDialog.open()
                        }
                    }

                    // Scale connection log, at INFO and above.
                    //
                    // Matches [Refractometer] as well as [Scale]. They are separate
                    // subsystems for QUERYING — a TDS problem and a weight problem
                    // are different questions — but they share this screen, so the
                    // view asks for both. That is the whole reason the filter takes a
                    // list rather than one marker.
                    //
                    // [Bluetooth] joins them for the same reason it appears on the
                    // machine panel: the adapter is beneath every device paired here,
                    // and "the scale never appeared" with no radio line is a dead end.
                    //
                    // [PORTAL] is here because the panel above sends its user to this
                    // log; a marker that is not listed never reaches the view. A user
                    // without a PORTAL emits no such line, so nothing changes for them.
                    SubsystemLogView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(150)
                        markers: ["[Scale]", "[Refractometer]", "[Bluetooth]", "[PORTAL]"]
                        showShare: true
                        accessibleName: TranslationManager.translate("settings.connections.bleScaleLog", "Bluetooth scale connection log")
                        onShareRequested: shareLogDialog.open()
                    }
                }

            }
            }
        }
    }

}
