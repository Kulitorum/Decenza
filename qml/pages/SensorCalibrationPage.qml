import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

// Guided sensor calibration for ONE sensor, chosen by the caller.
//
// The only number entered here is the external instrument's. The machine's half
// is the loaded profile's declared hold, supplied by SensorCalibration — so
// there is no "what did the app show?" field to get wrong.
//
// The page does NOT start the shot. On a machine with a GHC — most of them — the
// firmware refuses an app-initiated start outright (DE1Device::isHeadless is
// exactly "the app may start operations"), so the user starts it at the machine.
// The shot then shows on the espresso page, PUSHED over this page rather than
// replacing it, so the run ending is a plain pop back here with the page and its
// entered state intact.
T.Page {
    id: calibrationPage

    // Index into the SensorCalibration table. Set by main.qml when pushing.
    property int sensor: 0

    // The service snapshot is a reactive typed read model; all operator inputs
    // and wizard state remain local to this page instance.
    readonly property var sensorInfo: SensorCalibrationService.sensors[calibrationPage.sensor] ?? ({})
    readonly property string sensorLabel: TranslationManager.translate(
        sensorInfo.labelKey ?? "settings.sensorCalibration.pressure",
        sensorInfo.labelFallback ?? "Pressure")
    readonly property string unit: sensorInfo.unitLabel ?? ""
    readonly property string instrumentText: TranslationManager.translate(
        sensorInfo.instrumentKey ?? "settings.sensorCalibration.instrument",
        sensorInfo.instrumentFallback ?? "External instrument")

    readonly property string pageTitle: calibrationPage.sensorLabel

    objectName: "sensorCalibrationPage"
    background: ThemedPageBackground {}

    // Plain state refreshed by explicit handlers, NOT bindings over a version
    // counter. That idiom is used elsewhere in this codebase and does not work
    // here: twice now the log and the MCP showed the value in hand — a stored
    // offset, then an active test profile — while the card still said it was
    // missing. A handler on the signal has no dependency capture to get wrong.
    property bool testProfileActive: false
    // What the machine holds to and shows during the test: the profile's declared
    // hold, the number the user compares against their gauge. NaN when this
    // sensor's test profile is not loaded.
    property double declaredHold: NaN
    readonly property bool haveDeclaredHold: !isNaN(calibrationPage.declaredHold)

    property bool hasStored: false
    property double storedOffset: 0
    property bool lastConnected: false
    property double previousGap: NaN
    property bool wroteThisSession: false
    property bool writeFailed: false
    property string profileLoadRequestId: ""
    property string readRequestId: ""
    property string validationRequestId: ""
    property string applyRequestId: ""
    property bool validationPending: false
    property string previewRejection: ""
    property string validationError: ""
    property double validationReading: NaN
    property double validatedContextRevision: -1
    property double validationCalibrationRevision: -1
    property double validatedCalibrationRevision: -1
    property bool calibrationChangedNotice: false
    property string writeError: ""
    property string profileLoadError: ""
    property string readError: ""
    property double pendingGap: NaN
    property var _restoreRecipeId: 0
    property string _restoreProfileName: ""
    property double _lastContextRevision: -1
    property double _lastCalibrationRevision: -1

    function _refreshContext() {
        var snapshot = SensorCalibrationService.snapshot
        if (!SensorCalibrationService.ready || !snapshot.sensors || !snapshot.sensors[calibrationPage.sensor])
            return
        var state = snapshot.sensors[calibrationPage.sensor]
        calibrationPage.testProfileActive = state.testProfileActive
        calibrationPage.declaredHold = state.hasDeclaredHold ? state.declaredHoldValue : NaN
        calibrationPage.hasStored = state.hasStored
        calibrationPage.storedOffset = state.storedCalibration
        var revalidate = false
        if (snapshot.contextRevision !== calibrationPage._lastContextRevision) {
            calibrationPage._lastContextRevision = snapshot.contextRevision
            calibrationPage.validationRequestId = ""
            calibrationPage.validationPending = false
            calibrationPage.validatedContextRevision = -1
            calibrationPage.validatedCalibrationRevision = -1
            revalidate = true
        }
        if (snapshot.calibrationRevision !== calibrationPage._lastCalibrationRevision) {
            calibrationPage._lastCalibrationRevision = snapshot.calibrationRevision
            calibrationPage.validationRequestId = ""
            calibrationPage.validationPending = false
            calibrationPage.validationCalibrationRevision = -1
            calibrationPage.validatedCalibrationRevision = -1
            if (calibrationPage.entryTouched || confirmDialog.visible) {
                // A reading confirmed against the old calibration must not be
                // silently rebased. Require the user to review and re-enter it.
                calibrationPage.entryTouched = false
                calibrationPage.validatedContextRevision = -1
                calibrationPage.previewRejection = ""
                calibrationPage.validationError = ""
                calibrationPage.calibrationChangedNotice = calibrationPage.applyRequestId === ""
                confirmDialog.close()
            }
            revalidate = false
        }
        if (revalidate && calibrationPage.entryTouched)
            calibrationPage._requestValidation()
        var connectedNow = snapshot.connected === true
        if (connectedNow && !calibrationPage.lastConnected)
            calibrationPage._readCalibration()
        calibrationPage.lastConnected = connectedNow
    }

    function _readCalibration() {
        if (SensorCalibrationService.connected)
            readRequestId = SensorCalibrationService.readCalibration(calibrationPage.sensor)
    }

    function _requestValidation() {
        if (!calibrationPage.entryTouched || !calibrationPage.haveDeclaredHold
                || !calibrationPage.testProfileActive || !SensorCalibrationService.ready) {
            calibrationPage.validationRequestId = ""
            calibrationPage.validationPending = false
            calibrationPage.previewRejection = ""
            calibrationPage.validatedContextRevision = -1
            calibrationPage.validatedCalibrationRevision = -1
            calibrationPage.validationCalibrationRevision = -1
            return
        }
        calibrationPage.validationError = ""
        calibrationPage.previewRejection = ""
        calibrationPage.validationReading = calibrationPage.entryValue
        calibrationPage.validationCalibrationRevision = SensorCalibrationService.snapshot.calibrationRevision
        calibrationPage.validationPending = true
        calibrationPage.validationRequestId = SensorCalibrationService.validateCorrection(
            calibrationPage.sensor, calibrationPage.entryValue,
            SensorCalibrationService.snapshot.contextRevision)
    }

    function _availableProfile(filename) {
        return ProfileService.availableProfiles.some(function(row) { return row.name === filename })
    }

    Connections {
        target: SensorCalibrationService
        function onStateChanged() { calibrationPage._refreshContext() }
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === calibrationPage.readRequestId && operation === "readCalibration") {
                calibrationPage.readRequestId = ""
                calibrationPage.readError = success ? "" : calibrationPage.displayReason(reason)
                calibrationPage._refreshContext()
                return
            }
            if (requestId === calibrationPage.validationRequestId && operation === "validateCorrection") {
                calibrationPage.validationRequestId = ""
                calibrationPage.validationPending = false
                if (!success) {
                    calibrationPage.validationError = calibrationPage.displayReason(reason)
                    return
                }
                if (result.reading !== calibrationPage.entryValue
                        || result.contextRevision !== SensorCalibrationService.snapshot.contextRevision
                        || calibrationPage.validationCalibrationRevision
                           !== SensorCalibrationService.snapshot.calibrationRevision)
                    return
                calibrationPage.previewRejection = result.rejection ?? ""
                calibrationPage.validatedContextRevision = result.contextRevision
                calibrationPage.validatedCalibrationRevision = calibrationPage.validationCalibrationRevision
                return
            }
            if (requestId !== calibrationPage.applyRequestId || operation !== "applyCorrection")
                return
            calibrationPage.applyRequestId = ""
            if (!success) {
                calibrationPage.writeFailed = true
                calibrationPage.writeError = calibrationPage.displayReason(reason)
                if (reason === "conflict") {
                    calibrationPage.entryTouched = false
                    calibrationPage.validatedContextRevision = -1
                    calibrationPage.validatedCalibrationRevision = -1
                    SensorCalibrationService.refresh()
                }
                confirmDialog.close()
                return
            }
            calibrationPage.writeFailed = false
            calibrationPage.writeError = ""
            calibrationPage.previousGap = calibrationPage.pendingGap
            calibrationPage.wroteThisSession = true
            calibrationPage._readCalibration()
            calibrationPage.entryValue = calibrationPage.declaredHold
            calibrationPage.entryTouched = false
            calibrationPage.calibrationChangedNotice = false
            calibrationPage.previewRejection = ""
            calibrationPage.validationError = ""
            calibrationPage.validatedContextRevision = -1
            calibrationPage.validatedCalibrationRevision = -1
            confirmDialog.close()
            calibrationPage._requestValidation()
        }
    }

    Connections {
        target: ProfileService
        function onOperationCompleted(requestId, operation, filename, success, reason) {
            if (requestId !== calibrationPage.profileLoadRequestId || operation !== "load")
                return
            calibrationPage.profileLoadRequestId = ""
            if (!success) {
                calibrationPage.profileLoadError = calibrationPage.displayReason(reason)
                return
            }
            calibrationPage.profileLoadError = ""
            calibrationPage._refreshContext()
            calibrationPage._readCalibration()
        }
    }

    Component.onCompleted: {
        calibrationPage._restoreRecipeId = RecipeService.activeRecipeId
        calibrationPage._restoreProfileName = ProfileService.previousProfileName
        calibrationPage.profileLoadRequestId = ProfileService.loadProfile(
            calibrationPage.sensorInfo.profileFilename ?? "")
        calibrationPage._refreshContext()
        if (SensorCalibrationService.connected)
            calibrationPage._readCalibration()
    }

    StackView.onRemoved: {
        if (!calibrationPage.testProfileActive)
            return
        if (calibrationPage._restoreRecipeId > 0) {
            RecipeService.requestActivateRecipe(calibrationPage._restoreRecipeId)
        } else if (calibrationPage._restoreProfileName !== ""
                   && calibrationPage._availableProfile(calibrationPage._restoreProfileName)) {
            ProfileService.loadProfile(calibrationPage._restoreProfileName)
        }
    }

    ScrollView {
        id: calibrationScroll
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.scaled(80)
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(10)

            // ===== Prepare =====
            Rectangle {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.scaled(600)
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: prepareColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: prepareColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "sensorCalibration.prepare.title"
                        fallback: "Run the test"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: calibrationPage.instrumentText
                        color: Theme.textColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.haveDeclaredHold
                        text: TranslationManager.translate(
                                  "sensorCalibration.prepare.body",
                                  "Start the shot as usual and read your gauge while the machine holds.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.prepare.restores",
                                  "Your usual profile or recipe comes back when you leave.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.profileLoadError !== ""
                        text: calibrationPage.profileLoadError
                        color: Theme.errorColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !calibrationPage.testProfileActive
                        text: TranslationManager.translate(
                                  "sensorCalibration.prepare.wrongProfile",
                                  "The test profile is not loaded, so there is nothing to compare "
                                  + "against. Leave and open this again.")
                        color: Theme.warningColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ===== Enter what your gauge read =====
            Rectangle {
                visible: calibrationPage.haveDeclaredHold
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.scaled(600)
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: applyColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: applyColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "sensorCalibration.apply.title"
                        fallback: "What did your gauge read?"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: TranslationManager.translate("sensorCalibration.apply.machineHolds",
                                                               "Your machine holds")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: calibrationPage.declaredHold.toFixed(2) + " " + calibrationPage.unit
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Tr {
                            key: "sensorCalibration.current.stored"
                            fallback: "Correction now stored"
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            // Unavailable, not zero: an unanswered read must not
                            // read as "no correction".
                            text: calibrationPage.hasStored
                                  ? calibrationPage._signed(calibrationPage.storedOffset)
                                  : TranslationManager.translate("sensorCalibration.unavailable", "Not read yet")
                            color: calibrationPage.hasStored ? Theme.textColor : Theme.textSecondaryColor
                            font: Theme.bodyFont
                        }
                    }

                    // A stepper, not a keyboard field. Typing a decimal on a
                    // tablet is awkward, and it was also the only way a locale
                    // comma could reach parseFloat and silently truncate "88,5"
                    // to 88. Nudging from the declared hold removes both.
                    //
                    // The range is the largest accepted correction either side of
                    // that hold, clamped to what the sensor could read — so the
                    // guard is enforced by the control rather than only by
                    // rejecting afterwards, and the user cannot dial in a value
                    // that would be refused.
                    ValueInput {
                        id: instrumentInput
                        Layout.fillWidth: true
                        from: Math.max(calibrationPage.sensorInfo.minValue,
                                       calibrationPage.declaredHold - calibrationPage._entrySwing)
                        to: Math.min(calibrationPage.sensorInfo.maxValue,
                                     calibrationPage.declaredHold + calibrationPage._entrySwing)
                        stepSize: calibrationPage.sensorInfo.entryStep
                        value: calibrationPage.entryValue
                        displayText: calibrationPage.entryValue.toFixed(2) + " " + calibrationPage.unit
                        rangeText: from.toFixed(1) + " \u2014 " + to.toFixed(1) + " " + calibrationPage.unit
                        accessibleName: TranslationManager.translate(
                                            "sensorCalibration.apply.placeholder",
                                            "Reading from your gauge")
                        onValueModified: function(newValue) {
                            calibrationPage.entryValue = newValue
                            calibrationPage.entryTouched = true
                            calibrationPage.calibrationChangedNotice = false
                            calibrationPage.writeFailed = false
                            calibrationPage.validationError = ""
                            calibrationPage._requestValidation()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.entryTouched
                                 && calibrationPage.rejection.length > 0
                        text: calibrationPage.rejection
                        color: Theme.errorColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    // The pair and the resulting correction, together, before
                    // anything is written.
                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.entryValid
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.summary",
                                  "Machine %1 %3, gauge %2 %3 \u2014 correction %4")
                              .arg(calibrationPage.declaredHold.toFixed(2))
                              .arg(calibrationPage.entryValue.toFixed(2))
                              .arg(calibrationPage.unit)
                              .arg(calibrationPage._signed(calibrationPage.entryValue
                                                           - calibrationPage.declaredHold))
                        color: Theme.textColor
                        font: Theme.bodyFont
                        wrapMode: Text.WordWrap
                    }

                    // The machine applies a TENTH of each correction (measured on
                    // hardware). Without saying so, a user who enters the right
                    // number sees the gap barely move and concludes it is broken
                    // — when it is working exactly as the vendor intends.
                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.entryValid
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.gradual",
                                  "Applied about a tenth at a time — expect several runs.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.calibrationChangedNotice
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.changedReview",
                                  "Calibration changed on this machine. Review the gauge reading and enter it again.")
                        color: Theme.warningColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.validationPending
                        text: TranslationManager.translate("sensorCalibration.validation.pending", "Checking this reading...")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.readError !== ""
                        text: calibrationPage.readError
                        color: Theme.errorColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.writeError !== "" || calibrationPage.writeFailed
                        text: calibrationPage.writeError !== "" ? calibrationPage.writeError
                            : TranslationManager.translate("sensorCalibration.apply.writeFailed",
                                  "Your machine did not accept that — nothing was changed.")
                        color: Theme.errorColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !calibrationPage.hasStored
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.noBaseline",
                                  "Your machine has not reported its current calibration, so a "
                                  + "correction cannot be applied yet.")
                        color: Theme.warningColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    AccessibleButton {
                        Layout.alignment: Qt.AlignRight
                        text: TranslationManager.translate("sensorCalibration.apply.button", "Apply correction")
                        accessibleName: TranslationManager.translate("sensorCalibration.apply.button", "Apply correction")
                        primary: true
                        // SensorCalibrationService.connected too: without it the button stays
                        // live after the machine has gone.
                        enabled: calibrationPage.entryValid && calibrationPage.hasStored
                                 && SensorCalibrationService.connected && !SensorCalibrationService.busy
                        onClicked: confirmDialog.open()
                    }
                }
            }

            // ===== Convergence, after a write =====
            Rectangle {
                visible: calibrationPage.wroteThisSession
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.scaled(600)
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: convergeColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: convergeColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "sensorCalibration.verify.title"
                        fallback: "Run it again"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.verify.body",
                                  "Run the test profile again and compare, until the two agree.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !isNaN(calibrationPage.previousGap)
                        text: TranslationManager.translate(
                                  "sensorCalibration.verify.previousGap",
                                  "Last time, machine and gauge differed by %1 %2.")
                              .arg(Math.abs(calibrationPage.previousGap).toFixed(2))
                              .arg(calibrationPage.unit)
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // Parsed entry state. Kept as page properties so the summary, the guard
    // message and the button all read the same values.
    // The instrument reading, nudged from the declared hold. Starting equal to it
    // means an untouched control is "they agree", which rejectionReason refuses —
    // so Apply is inert until the user actually moves it.
    property double entryValue: 0
    // The stepper opens sitting on the declared hold, so its very first state is
    // "the two agree" — which rejectionReason correctly refuses. Showing that
    // refusal on arrival paints an error before the user has done anything, so
    // the guard message waits until they have moved the control.
    property bool entryTouched: false
    // How far either side of the declared hold the stepper may go. One step
    // inside maxCorrection, because the guard refuses AT the limit.
    readonly property double _entrySwing: calibrationPage.sensorInfo.maxCorrection
                                          - calibrationPage.sensorInfo.entryStep

    onDeclaredHoldChanged: {
        if (!isNaN(calibrationPage.declaredHold)) {
            calibrationPage.entryValue = calibrationPage.declaredHold
            calibrationPage.entryTouched = false
        }
    }

    readonly property string rejection: calibrationPage.validationError !== ""
        ? calibrationPage.validationError : calibrationPage.previewRejection
    readonly property bool entryValid: calibrationPage.entryTouched
        && !calibrationPage.validationPending
        && calibrationPage.validatedContextRevision === SensorCalibrationService.snapshot.contextRevision
        && calibrationPage.validatedCalibrationRevision === SensorCalibrationService.snapshot.calibrationRevision
        && calibrationPage.validationReading === calibrationPage.entryValue
        && calibrationPage.previewRejection === ""

    function displayReason(reason) {
        if (reason === "conflict")
            return TranslationManager.translate("sensorCalibration.conflict", "The machine or test profile changed. Review the current reading and try again.")
        if (reason === "unavailable" || reason === "disconnected")
            return TranslationManager.translate("sensorCalibration.unavailable", "Sensor calibration is unavailable. Reconnect and try again.")
        if (reason === "readRefused")
            return TranslationManager.translate("sensorCalibration.readRefused", "The machine did not accept the calibration read request.")
        return reason
    }

    function _signed(v) {
        return (v >= 0 ? "+" : "") + v.toFixed(2) + " " + calibrationPage.unit
    }

    DecenzaDialog {
        id: confirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(420))
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Dialog.CloseOnEscape

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Tr {
                key: "sensorCalibration.confirm.title"
                fallback: "Write this to your machine?"
                font: Theme.subtitleFont
                color: Theme.textColor
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate(
                          "sensorCalibration.confirm.body",
                          "Machine holds %1 %3, gauge read %2 %3. This changes your machine's calibration.")
                      .arg(calibrationPage.declaredHold.toFixed(2))
                      .arg(calibrationPage.entryValue.toFixed(2))
                      .arg(calibrationPage.unit)
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.scaled(10)

                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: confirmDialog.close()
                }

                AccessibleButton {
                    text: TranslationManager.translate("sensorCalibration.confirm.write", "Write")
                    accessibleName: TranslationManager.translate("sensorCalibration.confirm.write", "Write")
                    primary: true
                    enabled: calibrationPage.entryValid && SensorCalibrationService.ready
                             && SensorCalibrationService.connected && !SensorCalibrationService.busy
                    onClicked: {
                        var gap = calibrationPage.entryValue - calibrationPage.declaredHold
                        // ONE call, and it carries only the instrument's reading.
                        // The controller supplies what the machine read, because
                        // it is the object that watched the run — there is no way
                        // from here to name that half, which is the point.
                        calibrationPage.pendingGap = gap
                        calibrationPage.writeError = ""
                        calibrationPage.applyRequestId = SensorCalibrationService.applyCorrection(
                            calibrationPage.sensor, calibrationPage.entryValue,
                            calibrationPage.validatedContextRevision,
                            calibrationPage.validatedCalibrationRevision)
                    }
                }
            }
        }
    }

    // Back is otherwise only the top-bar arrow, the Android hardware button and
    // Escape — all of which pop through main.qml's global handler. This gives the
    // page its own affordance; it goes through AppShell like every other page, so
    // StackView.onRemoved still runs the profile restore.
    BottomBar {
        title: calibrationPage.pageTitle
        onBackClicked: AppShell.backRequested()
    }
}
