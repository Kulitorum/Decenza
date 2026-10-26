// The window-resolution ComboBox delegate reads this file's `resolutionCombo` id; Bound
// makes it statically resolvable. It declares both injected roles it uses, `modelData`
// and `index`, required in the same edit -- without that, Bound stops role injection
// and every resolution row renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import Decenza

Item {
    id: debugTab
    property string localApkPhase: ""
    property url localApkFile: ""
    property string localApkRequestId: ""
    property string localApkTransferId: ""
    readonly property var localApkApp: UpdateService.snapshot.app || ({})
    readonly property bool localApkBusy: localApkPhase !== ""
    readonly property bool localApkHostAvailable: UpdateService.ready
        && localApkApp.canInstallLocalApk === true && !localApkApp.checking
        && !localApkApp.downloading && !localApkApp.installing && !localApkApp.preparingLocalApk
    property bool profileConversionBusy: false
    property string profileConversionRequestId: ""
    property var convertedBundle: ({})
    property var profileProgress: ({})
    readonly property var debugPreferences: SettingsPreferencesService.preferences || ({})
    readonly property var languageSnapshot: LanguageService.snapshot || ({})

    StatusToast { id: localApkResult }
    ConversationArchiveDialog {
        id: conversationArchive
        onClosed: archiveBrowseButton.forceActiveFocus()
    }

    function canSelectLocalApk() {
        return localApkHostAvailable && !localApkBusy
    }

    function selectLocalApk() {
        if (!canSelectLocalApk()) return false
        localApkPhase = "picker"
        localApkPicker.open()
        return true
    }

    function acceptLocalApk(file) {
        if (localApkPhase !== "picker") return
        localApkFile = file
        localApkPhase = "confirm"
        localApkConfirmation.open()
    }

    function cancelLocalApkSelection() {
        if (localApkPhase !== "picker" && localApkPhase !== "confirm") return
        localApkFile = ""
        localApkPhase = ""
    }

    function uploadLocalApk() {
        if (localApkPhase !== "confirm") return
        localApkPhase = "transfer"
        localApkConfirmation.close()
        if (!localApkHostAvailable || !localApkTransfer.start([localApkFile], "apkFile", ({})))
            finishLocalApk(false, "unavailable")
    }

    function localApkTransferred(success, result, reason) {
        if (localApkPhase !== "transfer") return
        if (!success) { finishLocalApk(false, reason); return }
        localApkTransferId = result.transferId || ""
        if (localApkTransferId === "") { finishLocalApk(false, "invalidTransfer"); return }
        localApkPhase = "request"
        localApkRequestId = UpdateService.execute("installLocalApk", {transferId: localApkTransferId})
        if (localApkRequestId === "") finishLocalApk(false, "unavailable")
    }

    function completeLocalApk(requestId, operation, success, reason) {
        if (localApkPhase !== "request" || requestId !== localApkRequestId || operation !== "installLocalApk") return
        finishLocalApk(success, reason)
    }

    function finishLocalApk(success, reason) {
        if (localApkTransferId !== "")
            ImportService.execute("cancelTransfer", {transferId: localApkTransferId})
        localApkTransferId = ""
        localApkRequestId = ""
        localApkFile = ""
        localApkPhase = ""
        const message = success
            ? TranslationManager.translate("update.localApk.dispatched", "Installer requested. Check the Android host for the installation prompt.")
            : reason === "disconnected"
            ? TranslationManager.translate("update.localApk.disconnected", "The host disconnected during installation handoff. Check its screen before retrying; restart it if installation was cancelled.")
            : reason === "machineBusy"
            ? TranslationManager.translate("update.localApk.machineBusy", "Finish the current machine operation before installing an APK.")
            : reason === "invalidApk"
            ? TranslationManager.translate("update.localApk.invalid", "This file is not a valid APK upload. Select an APK file and try again.")
            : TranslationManager.translate("update.localApk.failed", "Could not install the APK. Check the host update status and try again.")
        localApkResult.error = !success
        localApkResult.show(message)
        AccessibilityManager.announce(message, !success)
    }

    FileDialog {
        id: localApkPicker
        title: TranslationManager.translate("update.localApk.select", "Select APK file")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Android packages (*.apk)"]
        onAccepted: debugTab.acceptLocalApk(selectedFile)
        onRejected: debugTab.cancelLocalApkSelection()
    }

    LocalFileTransfer {
        id: localApkTransfer
        onFinished: function(success, result, reason) { debugTab.localApkTransferred(success, result, reason) }
    }

    DecenzaDialog {
        id: localApkConfirmation
        title: TranslationManager.translate("update.localApk.title", "Install APK on host?")
        modal: true
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(480), parent ? parent.width - Theme.scaled(32) : Theme.scaled(480))
        onClosed: debugTab.cancelLocalApkSelection()
        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("update.localApk.confirm", "Upload and install this APK on the Android host? The host will disconnect during handoff. Complete the installation prompt on its screen. Maximum file size: 500 MB.")
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AccessibleButton {
                    accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                    text: accessibleName
                    onClicked: localApkConfirmation.close()
                }
                AccessibleButton {
                    accessibleName: TranslationManager.translate("update.localApk.uploadInstall", "Upload & Install APK")
                    text: accessibleName
                    primary: true
                    onClicked: debugTab.uploadLocalApk()
                }
            }
        }
    }

    Connections {
        target: UpdateService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            debugTab.completeLocalApk(requestId, operation, success, reason)
        }
    }

    function writeDebugPreference(writer, name, value) {
        if (!writer.submit(name, value))
            debugActionError.show(TranslationManager.translate(
                "common.settingsNotReady", "Settings are not connected yet."))
    }

    function setHeadless(value) {
        if (!MaintenanceService.ready) {
            debugActionError.show(TranslationManager.translate(
                "common.settingsNotReady", "Settings are not connected yet."))
            return
        }
        MaintenanceService.execute("setHeadless", { "headless": value })
    }

    PreferenceWrite {
        id: hideGhcSimulatorWrite
        domain: "app"
        onFailed: function(message) { debugActionError.show(message) }
    }
    PreferenceWrite {
        id: simulatedScaleWrite
        domain: "app"
        onFailed: function(message) { debugActionError.show(message) }
    }
    PreferenceWrite {
        id: developerTranslationUploadWrite
        domain: "app"
        onFailed: function(message) { debugActionError.show(message) }
    }
    StatusToast { id: debugActionError; error: true }
    StatusToast { id: logDownloadResult }
    SystemLogDownload {
        id: diagnosticLogDownload
        active: debugTab.visible
        onFinished: function(success, reason) {
            logDownloadResult.error = !success
            const message = success
                ? TranslationManager.translate("settings.debug.logSaved", "The log file was saved.")
                : TranslationManager.translate("settings.debug.logSaveFailed", "The log file could not be saved (%1).").arg(reason)
            logDownloadResult.show(message)
            AccessibilityManager.announce(message)
        }
    }

    Connections {
        target: MaintenanceService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (operation === "setHeadless" && !success)
                debugActionError.show(TranslationManager.translate(
                    "common.settingsChangedRetry", "The setting could not be saved. Review the current value and try again."))
        }
    }
    Connections {
        target: LanguageService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (operation === "batchTranslateUpload" && !success)
                debugActionError.show(TranslationManager.translate(
                    "settings.debug.batchFailed", "Batch translation and upload could not be completed."))
        }
    }

    Flickable {
        id: debugScroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: debugColumns.height
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
    }

    RowLayout {
        id: debugColumns
        parent: debugScroll.contentItem
        width: debugScroll.width
        height: Math.max(implicitHeight, debugScroll.height)
        spacing: Theme.scaled(15)

        // Left column: Window Resolution
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            SettingsCard {
                searchId: "debugLogDownload"
                title: TranslationManager.translate("settings.debug.diagnosticLog", "Diagnostic log")
                spacing: Theme.scaled(12)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)
                    AccessibleButton {
                        accessibleName: TranslationManager.translate("settings.debug.saveLog", "Save log file")
                        text: accessibleName
                        enabled: diagnosticLogDownload.ready && !diagnosticLogDownload.busy
                        onClicked: diagnosticLogDownload.start("log")
                    }
                    AccessibleButton {
                        accessibleName: TranslationManager.translate("settings.debug.saveLogZip", "Save log ZIP")
                        text: accessibleName
                        enabled: diagnosticLogDownload.ready && !diagnosticLogDownload.busy
                        onClicked: diagnosticLogDownload.start("zip")
                    }
                    AccessibleButton {
                        accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                        text: accessibleName
                        visible: diagnosticLogDownload.busy
                        onClicked: diagnosticLogDownload.cancel()
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: diagnosticLogDownload.busy
                    indeterminate: true
                    Accessible.name: TranslationManager.translate("settings.debug.preparingLog", "Preparing log download")
                }
            }

            SettingsCard {
                searchId: "aiConversationArchive"
                title: TranslationManager.translate("settings.debug.aiArchive", "AI conversation archive")
                AccessibleButton {
                    id: archiveBrowseButton
                    accessibleName: TranslationManager.translate("settings.debug.browseConversations", "Browse saved AI conversations")
                    text: accessibleName
                    enabled: ConversationSession.ready
                    onClicked: {
                        archiveBrowseButton.forceActiveFocus()
                        conversationArchive.open()
                    }
                }
            }

            SettingsCard {
                searchId: "localApkInstaller"
                availability: "debug"
                title: TranslationManager.translate("settings.debug.localApk", "Local APK installation")
                shown: debugTab.localApkApp.canInstallLocalApk === true
                RowLayout {
                    Layout.fillWidth: true
                    AccessibleButton {
                        accessibleName: TranslationManager.translate("update.localApk.uploadInstall", "Upload & Install APK")
                        text: accessibleName
                        enabled: debugTab.canSelectLocalApk()
                        onClicked: debugTab.selectLocalApk()
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: debugTab.localApkPhase === "transfer" || debugTab.localApkPhase === "request"
                            || debugTab.localApkApp.preparingLocalApk === true
                        text: debugTab.localApkPhase === "transfer"
                            ? TranslationManager.translate("update.localApk.uploading", "Uploading APK…")
                            : TranslationManager.translate("update.localApk.preparing", "Preparing installer…")
                        color: Theme.textSecondaryColor
                        font: Theme.bodyFont
                        wrapMode: Text.Wrap
                    }
                }

            }

            // Window Resolution section (Windows/desktop only)
            SettingsCard {
                id: resolutionSection
                searchId: "debugWindowResolution"
                title: TranslationManager.translate("settings.debug.windowResolution", "Window resolution")
                availability: "debugWindows"
                spacing: Theme.scaled(12)
                Layout.preferredHeight: Theme.scaled(120)
                // Resolution presets (Decent tablet first as default, landscape only)
                property var resolutions: [
                    { name: "Decent Tablet", width: 1200, height: 800 },
                    { name: "Tablet 7\"", width: 1024, height: 600 },
                    { name: "Tablet 10\"", width: 1280, height: 800 },
                    { name: "iPad 10.2\"", width: 1080, height: 810 },
                    { name: "iPad Pro 11\"", width: 1194, height: 834 },
                    { name: "iPad Pro 12.9\"", width: 1366, height: 1024 },
                    { name: "Desktop HD", width: 1280, height: 720 },
                    { name: "Desktop Full HD", width: 1920, height: 1080 }
                ]

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "settings.debug.resizeWindow"
                        fallback: "Resize window to test UI scaling"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Item { Layout.fillWidth: true }

                    StyledComboBox {
                        id: resolutionCombo
                        Layout.preferredWidth: Theme.scaled(200)
                        accessibleLabel: TranslationManager.translate("settings.debug.windowResolution", "Window resolution")
                        model: resolutionSection.resolutions
                        textRole: "name"
                        displayText: Window.window ? (Window.window.width + " x " + Window.window.height) : "Select..."

                        delegate: ItemDelegate {
                            id: resolutionRow
                            required property var modelData
                            required property int index

                            width: resolutionCombo.width
                            contentItem: Text {
                                text: resolutionRow.modelData.name + " (" + resolutionRow.modelData.width + "x" + resolutionRow.modelData.height + ")"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                                verticalAlignment: Text.AlignVCenter
                            }
                            highlighted: resolutionCombo.highlightedIndex === index
                            background: Rectangle {
                                color: resolutionRow.highlighted ? Theme.accentColor : Theme.surfaceColor
                            }
                        }

                        background: Rectangle {
                            color: Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1
                            radius: Theme.scaled(4)
                        }

                        contentItem: Text {
                            text: resolutionCombo.displayText
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(8)
                        }

                        onActivated: function(index) {
                            var res = resolutionSection.resolutions[index]
                            if (Window.window && res) {
                                Window.window.width = res.width
                                Window.window.height = res.height
                            }
                        }
                    }
                }
            }

            // Simulation toggles
            SettingsCard {
                searchId: "debugSimulationToggles"
                availability: "debug"
                title: TranslationManager.translate("settings.debug.simulationToggles", "Simulation Toggles")
                spacing: Theme.scaled(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "debug.headlessMachine"
                        fallback: "Headless machine"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: MaintenanceService.snapshot.isHeadless === true
                        accessibleName: TranslationManager.translate("debug.headlessMachine", "Headless machine")
                        onToggled: debugTab.setHeadless(checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "debug.hideGhcSimulator"
                        fallback: "Hide GHC Simulator"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: hideGhcSimulatorWrite.displayedValue("hideGhcSimulator",
                            debugTab.debugPreferences["app.hideGhcSimulator"] === true)
                        accessibleName: TranslationManager.translate("debug.hideGhcSimulator", "Hide GHC Simulator")
                        onToggled: debugTab.writeDebugPreference(hideGhcSimulatorWrite, "hideGhcSimulator", checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)
                    visible: debugTab.debugPreferences["app.simulationMode"] === true

                    Tr {
                        key: "debug.simulatedScaleEnabled"
                        fallback: "Simulated Scale"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                        Accessible.ignored: true
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: simulatedScaleWrite.displayedValue("simulatedScaleEnabled",
                            debugTab.debugPreferences["app.simulatedScaleEnabled"] !== false)
                        accessibleName: TranslationManager.translate("debug.simulatedScaleEnabled", "Simulated Scale")
                        onToggled: debugTab.writeDebugPreference(simulatedScaleWrite, "simulatedScaleEnabled", checked)
                    }
                }
            }

            // Profile Converter section
            SettingsCard {
                id: profileConverterCard
                searchId: "debugProfileConverter"
                availability: "debug"
                title: TranslationManager.translate("debug.profileConverter", "Profile Converter")
                spacing: Theme.scaled(10)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.debug.convertTclDesc", "Convert DE1 app TCL profiles to native JSON format. Preserves all fields including popup messages, per-frame weight, etc.")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(5)
                    visible: profileTransfer.busy || debugTab.profileConversionBusy

                    Text {
                        text: profileTransfer.busy
                              ? TranslationManager.translate("settings.debug.uploadingProfiles", "Preparing selected profiles…")
                              : (debugTab.profileProgress.statusMessage || TranslationManager.translate(
                                  "settings.debug.convertingProfiles", "Converting selected profiles…"))
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        visible: debugTab.profileConversionBusy
                        from: 0
                        to: Math.max(1, Number(debugTab.profileProgress.totalFiles || 1))
                        value: Number(debugTab.profileProgress.processedFiles || 0)
                    }

                    Text {
                        visible: debugTab.profileConversionBusy
                                 && Boolean(debugTab.profileProgress.currentFile)
                        text: debugTab.profileProgress.currentFile || ""
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: TranslationManager.translate("settings.debug.tclSelectionHint", "Select one or more DE1 app .tcl profiles to convert.")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                        wrapMode: Text.WordWrap
                    }

                    Tr {
                        key: "debug.overwrite"
                        fallback: "Overwrite"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    StyledSwitch {
                        id: overwriteSwitch
                        checked: false
                        accessibleName: TranslationManager.translate("debug.overwriteExistingProfiles", "Overwrite existing profiles")
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("debug.convertProfiles", "Convert Profiles")
                        accessibleName: TranslationManager.translate("debug.convertProfilesAccessible", "Convert DE1 app profiles to native format")
                        enabled: !profileTransfer.busy && !debugTab.profileConversionBusy
                                 && !profileBundleDownload.busy
                        onClicked: profileSourceDialog.open()
                    }
                }
            }


            FileDialog {
                id: profileSourceDialog
                title: TranslationManager.translate("settings.debug.selectTclProfiles", "Select DE1 app .tcl profiles")
                fileMode: FileDialog.OpenFiles
                nameFilters: ["DE1 profiles (*.tcl)", "All files (*)"]
                onAccepted: {
                    const started = profileTransfer.start(selectedFiles, "tclFiles", {
                            "overwrite": overwriteSwitch.checked
                        })
                    if (!started) {
                        debugActionError.show(TranslationManager.translate(
                            "settings.debug.importBusy", "A file transfer is already in progress."))
                    } else {
                        const oldBundleId = debugTab.convertedBundle.bundleId || ""
                        debugTab.convertedBundle = ({})
                        if (oldBundleId.length > 0)
                            DebugConversionService.execute("releaseBundle", { "bundleId": oldBundleId })
                    }
                }
            }

            LocalFileTransfer {
                id: profileTransfer
                onFinished: function(success, result, reason) {
                    if (!success) {
                        profileConvertResultDialog.title = TranslationManager.translate(
                            "settings.debug.convertFailed", "Conversion Failed")
                        profileConvertResultDialog.resultMessage = TranslationManager.translate(
                            "settings.debug.profileTransferFailed", "The selected profile files could not be prepared.")
                        profileConvertResultDialog.isError = true
                        profileConvertResultDialog.open()
                        return
                    }
                    debugTab.profileConversionBusy = true
                    debugTab.profileProgress = ({})
                    debugTab.profileConversionRequestId = DebugConversionService.execute(
                        "convertProfiles", {
                            "transferId": result.transferId,
                            "overwrite": overwriteSwitch.checked
                        })
                }
            }

            LocalFileDownload {
                id: profileBundleDownload
                executeOperation: function(operation, args) {
                    return DebugConversionService.execute(operation, args)
                }
                onFinished: function(success, reason) {
                    profileConvertResultDialog.title = success
                        ? TranslationManager.translate("settings.debug.convertDone", "Conversion Complete")
                        : TranslationManager.translate("settings.debug.convertFailed", "Conversion Failed")
                    profileConvertResultDialog.resultMessage = success
                        ? TranslationManager.translate("settings.debug.bundleSaved", "Converted profiles were saved.")
                        : TranslationManager.translate("settings.debug.bundleSaveFailed", "The converted profile bundle could not be saved.")
                    profileConvertResultDialog.isError = !success
                    profileConvertResultDialog.open()
                }
            }

            Connections {
                target: DebugConversionService
                function onOperationProgress(requestId, progress) {
                    if (requestId === debugTab.profileConversionRequestId)
                        debugTab.profileProgress = progress
                }
                function onOperationCompleted(requestId, operation, success, result, reason) {
                    profileBundleDownload.completeOperation(requestId, operation, success, result, reason)
                    if (requestId !== debugTab.profileConversionRequestId || operation !== "convertProfiles") return
                    debugTab.profileConversionBusy = false
                    debugTab.profileConversionRequestId = ""
                    if (!success) {
                        profileConvertResultDialog.title = TranslationManager.translate(
                            "settings.debug.convertFailed", "Conversion Failed")
                        profileConvertResultDialog.resultMessage = TranslationManager.translate(
                            "settings.debug.convertFailedSafe", "The profiles could not be converted. Check the selected files and try again.")
                        profileConvertResultDialog.isError = true
                    } else {
                        debugTab.convertedBundle = result
                        const errors = Number(result.errors || 0)
                        profileConvertResultDialog.title = errors > 0
                            ? TranslationManager.translate("settings.debug.convertDoneErrors", "Conversion Complete (with errors)")
                            : TranslationManager.translate("settings.debug.convertDone", "Conversion Complete")
                        profileConvertResultDialog.resultMessage = TranslationManager.translate(
                            "settings.debug.convertCount", "Successfully converted: %1 profiles").arg(result.converted || 0)
                        if (Number(result.skipped || 0) > 0)
                            profileConvertResultDialog.resultMessage += "\n" + TranslationManager.translate(
                                "settings.debug.convertSkipped", "Skipped (already exist): %1").arg(result.skipped)
                        if (errors > 0)
                            profileConvertResultDialog.resultMessage += "\n" + TranslationManager.translate(
                                "settings.debug.convertErrors", "Errors: %1").arg(errors)
                        profileConvertResultDialog.isError = errors > 0
                    }
                    profileConvertResultDialog.open()
                }
            }

            // Profile conversion result dialog
            DecenzaDialog {
                id: profileConvertResultDialog
                modal: true
                dim: true
                closePolicy: Dialog.CloseOnEscape
                // qmllint disable Quick.layout-positioning
                // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
                // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
                // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
                // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
                anchors.centerIn: Overlay.overlay
                // qmllint enable Quick.layout-positioning
                padding: Theme.scaled(24)

                property string resultMessage: ""
                property bool isError: false

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 2
                    border.color: profileConvertResultDialog.isError ? Theme.errorColor : Theme.primaryColor
                }

                contentItem: Column {
                    spacing: Theme.spacingMedium
                    width: Theme.scaled(300)

                    Text {
                        text: profileConvertResultDialog.title
                        font: Theme.subtitleFont
                        color: profileConvertResultDialog.isError ? Theme.errorColor : Theme.textColor
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: profileConvertResultDialog.resultMessage
                        wrapMode: Text.Wrap
                        width: parent.width
                        font: Theme.bodyFont
                        color: Theme.textColor
                    }

                    AccessibleButton {
                        visible: Boolean(debugTab.convertedBundle.bundleId)
                        enabled: !profileBundleDownload.busy
                        text: TranslationManager.translate("settings.debug.saveConvertedBundle", "Save converted profiles…")
                        accessibleName: TranslationManager.translate("settings.debug.saveConvertedBundleAccessible", "Save converted profile bundle")
                        anchors.horizontalCenter: parent.horizontalCenter
                        onClicked: {
                            profileConvertResultDialog.close()
                            if (!profileBundleDownload.start(debugTab.convertedBundle))
                                debugActionError.show(TranslationManager.translate(
                                    "settings.debug.bundleSaveFailed", "The converted profile bundle could not be saved."))
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("common.button.ok", "OK")
                        accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                        anchors.horizontalCenter: parent.horizontalCenter
                        onClicked: profileConvertResultDialog.close()
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }
        }

        // Right column: Shot Database and Translation
        ColumnLayout {
            visible: SettingsSearchRegistry.isAvailable("debug")
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.scaled(15)

            // Database Import section
            SettingsCard {
                searchId: "debugShotDatabase"
                availability: "debug"
                title: TranslationManager.translate("settings.debug.shotDatabase", "Shot Database")
                spacing: Theme.scaled(12)
                Layout.preferredHeight: Theme.scaled(160)

                Tr {
                    Layout.fillWidth: true
                    key: "settings.debug.shotDatabaseDesc"
                    fallback: "Import shots from another device. Merge adds new shots, Replace overwrites all data."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    Text {
                        text: TranslationManager.translate("settings.debug.currentShots", "Current shots:") + " " + HistoryService.totalShots
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.debug.merge", "Merge...")
                        accessibleName: TranslationManager.translate("debug.importMergeDatabase", "Import and merge database")
                        enabled: !databaseTransfer.busy
                        onClicked: {
                            importDialog.mergeMode = true
                            importDialog.open()
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.debug.replace", "Replace...")
                        accessibleName: TranslationManager.translate("debug.importReplaceDatabase", "Import and replace database")
                        onClicked: {
                            importDialog.mergeMode = false
                            importDialog.open()
                        }
                    }
                }
            }

            FileDialog {
                id: importDialog
                title: mergeMode ? TranslationManager.translate("settings.debug.selectMerge", "Select database to merge") : TranslationManager.translate("settings.debug.selectReplace", "Select database to replace with")
                nameFilters: ["SQLite databases (*.db *.sqlite *.sqlite3)", "All files (*)"]
                property bool mergeMode: true

                onAccepted: {
                    if (!databaseTransfer.start([selectedFile], "database",
                                                { "merge": mergeMode }))
                        debugActionError.show(TranslationManager.translate(
                            "settings.debug.importBusy", "A file transfer is already in progress."))
                }
            }

            LocalFileTransfer {
                id: databaseTransfer
                onFinished: function(success, result, reason) {
                    if (success) {
                        importResultDialog.title = TranslationManager.translate("settings.debug.importOk", "Import Successful")
                        const totalShots = result.totalShots
                        importResultDialog.resultMessage = totalShots === undefined
                            ? TranslationManager.translate("settings.debug.importOkGeneric", "Database imported successfully.")
                            : TranslationManager.translate("settings.debug.importOkDetail", "Database imported successfully.\nTotal shots: %1").arg(totalShots)
                        importResultDialog.isError = false
                    } else {
                        importResultDialog.title = TranslationManager.translate("shotimporter.title.importFailed", "Import Failed")
                        importResultDialog.resultMessage = TranslationManager.translate(
                            "settings.debug.importFailedGeneric", "The database could not be imported. Check the file and available storage.")
                        importResultDialog.isError = true
                    }
                    importResultDialog.open()
                }
            }

            // Import result feedback dialog
            DecenzaDialog {
                id: importResultDialog
                modal: true
                dim: true
                closePolicy: Dialog.CloseOnEscape
                // qmllint disable Quick.layout-positioning
                // False positive, verified: qmllint's ForbiddenChildrenPropertyValidatorPass checks only
                // whether an object is DECLARED lexically inside a Layout, never whether a Layout actually
                // manages it. This object is not layout-managed — Dialog/Popup derive from QObject rather
                // than Item, and anything with `parent: Overlay.overlay` is reparented out at runtime.
                anchors.centerIn: Overlay.overlay
                // qmllint enable Quick.layout-positioning
                padding: Theme.scaled(24)

                property string resultMessage: ""
                property bool isError: false

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.cardRadius
                    border.width: 2
                    border.color: importResultDialog.isError ? Theme.errorColor : Theme.primaryColor
                }

                contentItem: Column {
                    spacing: Theme.spacingMedium
                    width: Theme.scaled(300)

                    Text {
                        text: importResultDialog.title
                        font: Theme.subtitleFont
                        color: importResultDialog.isError ? Theme.errorColor : Theme.textColor
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: importResultDialog.resultMessage
                        wrapMode: Text.Wrap
                        width: parent.width
                        font: Theme.bodyFont
                        color: Theme.textColor
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("common.button.ok", "OK")
                        accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                        anchors.horizontalCenter: parent.horizontalCenter
                        onClicked: importResultDialog.close()
                    }
                }
            }

            // The transfer service owns the database path and returns a correlated result.

            // Translation Developer Tools section
            SettingsCard {
                searchId: "debugTranslationTools"
                availability: "debug"
                title: TranslationManager.translate("settings.debug.translationTools", "Translation Developer Tools")
                spacing: Theme.scaled(12)
                Layout.preferredHeight: Theme.scaled(180)

                Tr {
                    Layout.fillWidth: true
                    key: "settings.debug.translationToolsDesc"
                    fallback: "Tools for managing community translations. Enable upload to allow submitting translations from the Language settings."
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)

                    Tr {
                        key: "settings.debug.enableUpload"
                        fallback: "Enable translation upload"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: developerTranslationUploadWrite.displayedValue("developerTranslationUpload",
                            debugTab.debugPreferences["app.developerTranslationUpload"] === true)
                        accessibleName: TranslationManager.translate("settings.debug.enableUpload", "Enable translation upload")
                        onToggled: debugTab.writeDebugPreference(developerTranslationUploadWrite,
                                                                 "developerTranslationUpload", checked)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    Text {
                        text: debugTab.languageSnapshot.autoTranslating === true ?
                              TranslationManager.translate("settings.debug.translating", "Translating...") :
                              (debugTab.languageSnapshot.uploading === true ?
                               TranslationManager.translate("settings.debug.uploading", "Uploading...") :
                               (debugTab.languageSnapshot.batchTranslateUploadActive === true ?
                                TranslationManager.translate("settings.debug.translating", "Translating...") :
                                TranslationManager.translate("settings.debug.batchProcess", "Batch process all languages")))
                        color: debugTab.languageSnapshot.autoTranslating === true
                               || debugTab.languageSnapshot.uploading === true
                               || debugTab.languageSnapshot.batchTranslateUploadActive === true
                               ? Theme.primaryColor : Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: debugTab.languageSnapshot.autoTranslating === true ?
                              TranslationManager.translate("settings.debug.cancel", "Cancel") :
                              TranslationManager.translate("settings.debug.translateUploadAll", "Translate & Upload All")
                        accessibleName: TranslationManager.translate("debug.translateUploadAll", "Translate and upload all languages")
                        enabled: LanguageService.ready && debugTab.languageSnapshot.uploading !== true
                                 && (debugTab.languageSnapshot.batchTranslateUploadActive !== true
                                     || debugTab.languageSnapshot.autoTranslating === true)
                        onClicked: {
                            if (debugTab.languageSnapshot.autoTranslating === true) {
                                LanguageService.execute("cancelAutoTranslate", {})
                            } else {
                                LanguageService.execute("batchTranslateUpload", {})
                            }
                        }
                    }
                }
            }

            // Spacer
            Item { Layout.fillHeight: true }
        }
    }
    ScrollDownIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(8)
        flickable: debugScroll
    }
}
