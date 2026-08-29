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
// The shot then shows on the espresso page like any other, and main.qml brings
// the wizard back when it ends (saveReturnToPage/finishCompletion).
T.Page {
    id: calibrationPage

    // Index into the SensorCalibration table. Set by main.qml when pushing.
    property int sensor: 0

    // SensorCalibration's string accessors are C++ Q_INVOKABLEs that translate
    // internally, so a binding calling one records NO dependency on
    // TranslationManager and would freeze on the language in force when the page
    // was built. The Q_PROPERTY(translate) fix that made plain translate() calls
    // reactive does not reach through a C++ invokable.
    //
    // This is the one shape where translationVersion is still required, which is
    // why it appears here despite CLAUDE.md telling new code not to use it —
    // that advice is scoped to bindings over translate() itself.
    readonly property int _trVersion: TranslationManager.translationVersion
    readonly property string sensorLabel: {
        void(calibrationPage._trVersion)
        return SensorCalibration.label(calibrationPage.sensor)
    }
    readonly property string unit: {
        void(calibrationPage._trVersion)
        return SensorCalibration.unitLabel(calibrationPage.sensor)
    }
    readonly property string instrumentText: {
        void(calibrationPage._trVersion)
        return SensorCalibration.instrumentText(calibrationPage.sensor)
    }
    readonly property int calTarget: SensorCalibration.calibrationTarget(calibrationPage.sensor)

    readonly property string pageTitle: calibrationPage.sensorLabel

    objectName: "sensorCalibrationPage"
    background: ThemedPageBackground {}

    // Everything below re-reads when the active profile changes.
    readonly property int _ctxVersion: SensorCalibration.contextVersion
    readonly property bool testProfileActive: {
        void(calibrationPage._ctxVersion)
        return SensorCalibration.isTestProfileActive(calibrationPage.sensor)
    }
    // What the machine holds to and shows on screen during the test — the
    // profile's declared hold, which is the number the user compares against
    // their gauge. NaN when the test profile is not loaded.
    readonly property double declaredHold: {
        void(calibrationPage._ctxVersion)
        return SensorCalibration.declaredHoldValue(calibrationPage.sensor)
    }
    readonly property bool haveDeclaredHold: !isNaN(calibrationPage.declaredHold)

    // Plain state driven by an explicit handler, NOT a binding over a version
    // counter. That idiom is used elsewhere here, but in this page it did not
    // re-evaluate — the log showed the value arriving while the card still read
    // "not read yet". A Connections handler fires on the signal with no
    // dependency-capture subtlety to get wrong.
    property bool hasStored: false
    property double storedOffset: 0

    function _refreshStored() {
        calibrationPage.hasStored = DE1Device.hasStoredCalibration(calibrationPage.calTarget)
        calibrationPage.storedOffset = DE1Device.storedCalibration(calibrationPage.calTarget)
    }

    // The previous cycle's gap between machine and instrument, so a second run
    // shows convergence rather than just another pair of numbers.
    property double previousGap: NaN
    property bool wroteThisSession: false

    // A write was refused and nothing reached the machine.
    property bool writeFailed: false

    function _readCalibration() {
        DE1Device.readCalibration(calibrationPage.calTarget)
    }

    // What the user had before the wizard replaced it. Captured BEFORE the load,
    // because the load itself destroys both: MainController watches
    // currentProfileChanged and deactivates a recipe whose profile has diverged
    // (maincontroller.cpp:1559), so by the time the profile is loaded the recipe
    // selection is already gone.
    //
    // A recipe is restored as a recipe, never as its profile filename: activating
    // one also reapplies the linked bag, equipment, dose, yield, temperature,
    // grind and steam block, none of which loadProfile knows about. Restoring the
    // profile alone would leave the user's next shot on their profile with
    // somebody else's grind.
    // Read from Settings.app.currentProfile rather than ProfileManager.baseProfileName,
    // and captured rather than bound. The page is destroyed when the calibration shot
    // takes the screen and rebuilt when it ends, so baseProfileName would by then be
    // the test profile — but the persisted key still names the user's profile, because
    // the load below is the one profile change this page never lets persist. A binding
    // would not survive either: loadProfile writes that key before we read it back.
    property string _restoreProfile: ""
    // var, not int: a recipe id is a database rowid and QML's int is 32-bit.
    property var _restoreRecipeId: 0

    Component.onCompleted: {
        calibrationPage._restoreProfile = Settings.app.currentProfile
        calibrationPage._restoreRecipeId = MainController.selectedRecipeId
        // Load this sensor's test profile, the way tapping it in the profile list
        // would. It stays loaded for as long as the page lives — that is what
        // keeps isTestProfileActive true across the run-read-apply loop, which the
        // user repeats without leaving.
        ProfileManager.loadProfile(SensorCalibration.profileFilename(calibrationPage.sensor))
        // loadProfile persists its choice as the startup profile
        // (profilemanager.cpp:1923). Writing the user's name straight back keeps the
        // test profile to this session — so a quit here does not come back on a
        // hidden profile that declares espresso_temperature 1.00, and _restoreProfile
        // above stays true across the shot that destroys this page.
        if (calibrationPage._restoreProfile.length > 0)
            Settings.app.currentProfile = calibrationPage._restoreProfile
        // Deferred, because the reply can be SYNCHRONOUS. The simulated machine
        // answers inside this call, so calibrationChanged would fire while the
        // page is still completing and the hasStored binding would never see it —
        // the card sat on "not read yet" with the value already in hand. Real
        // hardware replies over BLE and does not hit this, which is exactly why
        // it only showed in the simulator.
        Qt.callLater(calibrationPage._readCalibration)
        // And pick up anything already cached: a value carried over from an
        // earlier visit fires no signal.
        calibrationPage._refreshStored()
    }

    // Every way out converges here, whichever back path was used — the top-bar
    // arrow, the Android hardware back button and Escape all pop the page, and
    // main.qml's global handler does it without consulting the page (which is why
    // DescalingPage had to intercept the key itself). StackView.onRemoved fires
    // for all of them; ScreensaverPage restores brightness the same way.
    //
    // Auto-sleep also removes the page, and restoring then is correct rather than
    // premature: the wizard reloads the test profile when the user opens it again,
    // and the alternative is a sleeping machine holding a calibration profile
    // ready for the next shot.
    StackView.onRemoved: {
        // Only if the test profile is still the active one. The profile can change
        // under the wizard — a Sleep->Idle wake runs loadAutoLoadRecipeIfNeeded()
        // with no page check (main.qml:511) — and replaying a capture from minutes
        // ago would throw that away.
        if (!SensorCalibration.isTestProfileActive(calibrationPage.sensor))
            return
        if (calibrationPage._restoreRecipeId > 0) {
            MainController.activateRecipe(calibrationPage._restoreRecipeId)
        // Not the test profile itself — arriving with it already loaded means it IS
        // what they had. And only a name loadProfile can resolve: baseProfileName is
        // a filename only when the profile came from a file, since loadProfileFromJson
        // sets it to the TITLE (profilemanager.cpp:1984), on which loadProfile falls
        // through to the default profile. Leaving the test profile loaded beats
        // silently dropping the user on "default".
        } else if (calibrationPage._restoreProfile
                       !== SensorCalibration.profileFilename(calibrationPage.sensor)
                   && ProfileManager.profileExists(calibrationPage._restoreProfile)) {
            ProfileManager.loadProfile(calibrationPage._restoreProfile)
        }
    }

    Connections {
        target: DE1Device
        function onCalibrationChanged() { calibrationPage._refreshStored() }
        // The stored offset belongs to one machine, so DE1Device clears it on
        // disconnect; without the re-read the page would sit on "not read yet"
        // for the rest of its life after a reconnect.
        function onConnectedChanged() {
            if (DE1Device.connected)
                calibrationPage._readCalibration()
            else
                calibrationPage._refreshStored()
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
                visible: true
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
                        from: Math.max(SensorCalibration.minValue(calibrationPage.sensor),
                                       calibrationPage.declaredHold - calibrationPage._entrySwing)
                        to: Math.min(SensorCalibration.maxValue(calibrationPage.sensor),
                                     calibrationPage.declaredHold + calibrationPage._entrySwing)
                        stepSize: SensorCalibration.entryStep(calibrationPage.sensor)
                        value: calibrationPage.entryValue
                        displayText: calibrationPage.entryValue.toFixed(2) + " " + calibrationPage.unit
                        rangeText: from.toFixed(1) + " \u2014 " + to.toFixed(1) + " " + calibrationPage.unit
                        accessibleName: TranslationManager.translate(
                                            "sensorCalibration.apply.placeholder",
                                            "Reading from your gauge")
                        onValueModified: function(newValue) {
                            calibrationPage.entryValue = newValue
                            calibrationPage.entryTouched = true
                            calibrationPage.writeFailed = false
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
                        visible: calibrationPage.writeFailed
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.writeFailed",
                                  "Your machine did not accept that \u2014 it may have disconnected. "
                                  + "Nothing was changed.")
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
                        // DE1Device.connected too: without it the button stays
                        // live after the machine has gone.
                        enabled: calibrationPage.entryValid && calibrationPage.hasStored
                                 && DE1Device.connected
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
    readonly property double _entrySwing: SensorCalibration.maxCorrection(calibrationPage.sensor)
                                          - SensorCalibration.entryStep(calibrationPage.sensor)

    onDeclaredHoldChanged: {
        if (!isNaN(calibrationPage.declaredHold)) {
            calibrationPage.entryValue = calibrationPage.declaredHold
            calibrationPage.entryTouched = false
        }
    }

    readonly property string rejection: {
        void(calibrationPage._ctxVersion)
        return SensorCalibration.rejectionReason(calibrationPage.sensor, calibrationPage.entryValue)
    }
    readonly property bool entryValid: calibrationPage.rejection.length === 0

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
                    onClicked: {
                        var gap = calibrationPage.entryValue - calibrationPage.declaredHold
                        // ONE call, and it carries only the instrument's reading.
                        // The controller supplies what the machine read, because
                        // it is the object that watched the run — there is no way
                        // from here to name that half, which is the point.
                        if (!SensorCalibration.applyCorrection(calibrationPage.sensor,
                                                               calibrationPage.entryValue)) {
                            // Refused, and nothing reached the machine. Saying
                            // "applied" here would send the user off to re-run
                            // against a machine that never changed.
                            calibrationPage.writeFailed = true
                            confirmDialog.close()
                            return
                        }
                        calibrationPage.writeFailed = false
                        calibrationPage.previousGap = gap
                        // Read back rather than trusting what we sent.
                        calibrationPage._readCalibration()
                        calibrationPage.wroteThisSession = true
                        calibrationPage.entryValue = calibrationPage.declaredHold
                        // onDeclaredHoldChanged does not fire — the hold did not
                        // change — so clear the touched flag here or the guard
                        // message reappears in error red on a write that worked.
                        calibrationPage.entryTouched = false
                        confirmDialog.close()
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
