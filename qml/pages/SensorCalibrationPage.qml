import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

// Guided sensor calibration for ONE sensor, chosen by the caller.
//
// The whole point of this page is that the user never types the machine's half
// of the correction. SensorCalibration watches the run and reports what the
// machine's own sensor read; the only number entered here is the external
// instrument's. That is why there is no "what did the app show?" field anywhere
// on this page, and why the Apply step does not exist until a run has been
// measured.
//
// Page grammar follows TransportPage: a guided full-screen operation driven by
// MachineState.phase, which never starts the shot itself (the machine has a GHC;
// the user starts it) and never treats a dropped link as a completion.
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

    // Mirrors the controller's state machine. Named rather than compared inline
    // so the views below read as prose.
    readonly property bool armed: SensorCalibration.state === SensorCalibration.Armed
    readonly property bool observing: SensorCalibration.state === SensorCalibration.Observing
    readonly property bool measured: SensorCalibration.state === SensorCalibration.Measured
    readonly property bool noHold: SensorCalibration.state === SensorCalibration.NoHold
    readonly property bool aborted: SensorCalibration.state === SensorCalibration.Aborted

    // The machine's stored and factory offsets, re-read after every write. Both
    // are ABSENT until the machine answers — never shown as 0, which would read
    // as "no correction" and is the one wrong answer that looks plausible.
    readonly property int _calVersion: DE1Device.calibrationVersion
    readonly property bool hasStored: {
        void(calibrationPage._calVersion)
        return DE1Device.hasStoredCalibration(calibrationPage.calTarget)
    }
    readonly property bool hasFactory: {
        void(calibrationPage._calVersion)
        return DE1Device.hasFactoryCalibration(calibrationPage.calTarget)
    }
    readonly property double storedOffset: {
        void(calibrationPage._calVersion)
        return DE1Device.storedCalibration(calibrationPage.calTarget)
    }
    readonly property double factoryOffset: {
        void(calibrationPage._calVersion)
        return DE1Device.factoryCalibration(calibrationPage.calTarget)
    }

    // The previous cycle's gap between machine and instrument, so a second run
    // shows convergence rather than just another pair of numbers. NaN until a
    // first correction has been written.
    property double previousGap: NaN
    property bool wroteThisSession: false

    // The profile that was active before this page loaded its own, so leaving
    // puts the machine back where it was.
    //
    // This matters more than it looks: ProfileManager.loadProfile() UPLOADS to
    // the machine (profilemanager.cpp:1945), so opening this page really does
    // change what the next shot runs — a 60-second calibration hold. Without a
    // restore, backing out of the wizard and pulling a shot gives you that.
    property string previousProfile: ""
    // The test profile could not be loaded. The page then does nothing at all —
    // no arming, no reads — and says so, rather than measuring whatever profile
    // the machine was given instead.
    property bool profileMissing: false
    // A write was refused and nothing reached the machine.
    property bool writeFailed: false

    function _readBothCalibrations() {
        DE1Device.readCalibration(calibrationPage.calTarget, false)
        DE1Device.readCalibration(calibrationPage.calTarget, true)
    }

    Component.onCompleted: {
        // currentProfileTITLE, not currentProfileName. The latter is a DISPLAY
        // string — it becomes "*My Espresso" the moment the profile is modified,
        // and a dose nudge is enough — and its own declaration says it must
        // never be used as a query term (profilemanager.h:85-90). Feeding it back
        // to loadProfile matches nothing, and the not-found path loads and
        // UPLOADS the default profile.
        calibrationPage.previousProfile = ProfileManager.currentProfileTitle

        // Make this sensor's test profile active. If it cannot be loaded,
        // loadProfile substitutes the DEFAULT profile and uploads it
        // (profilemanager.cpp:1754-1759) — so arming anyway would measure an
        // ordinary espresso shot and compute a firmware correction from it,
        // which is precisely the defect this feature exists to prevent.
        if (!ProfileManager.loadProfile(SensorCalibration.profileFilename(calibrationPage.sensor))) {
            calibrationPage.profileMissing = true
            return
        }
        // The shot is NOT started here — the machine has a GHC and the user
        // starts it.
        SensorCalibration.arm(calibrationPage.sensor)
        // Four reads; the shared GATT queue orders them, so no pacing here.
        calibrationPage._readBothCalibrations()
    }

    Component.onDestruction: {
        SensorCalibration.reset()
        // Nothing to restore if we never replaced anything.
        if (!calibrationPage.profileMissing && calibrationPage.previousProfile.length > 0)
            ProfileManager.loadProfile(calibrationPage.previousProfile)
    }

    // The instrument field sits mid-page inside the scroll area, so a soft
    // keyboard would cover it without this (CLAUDE.md: wrap pages with text
    // inputs). targetFlickable is set so Android can scroll it into view, which
    // adjustPan cannot do inside a Flickable.
    KeyboardAwareContainer {
        anchors.fill: parent
        textFields: [instrumentField]
        targetFlickable: calibrationScroll.contentItem as Flickable

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
            spacing: Theme.scaled(16)

            // ===== What the machine currently holds =====
            Rectangle {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.scaled(600)
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: currentColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: currentColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(6)

                    Tr {
                        key: "sensorCalibration.current.title"
                        fallback: "What your machine holds now"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Tr {
                            key: "sensorCalibration.current.stored"
                            fallback: "Saved"
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

                    RowLayout {
                        Layout.fillWidth: true
                        Tr {
                            key: "sensorCalibration.current.factory"
                            fallback: "Factory"
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: calibrationPage.hasFactory
                                  ? calibrationPage._signed(calibrationPage.factoryOffset)
                                  : TranslationManager.translate("sensorCalibration.unavailable", "Not read yet")
                            color: calibrationPage.hasFactory ? Theme.textColor : Theme.textSecondaryColor
                            font: Theme.bodyFont
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !calibrationPage.hasStored
                        text: TranslationManager.translate(
                                  "sensorCalibration.current.unanswered",
                                  "Your machine has not answered yet. You can still run the test, "
                                  + "but a correction cannot be applied until it does.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ===== The test profile could not be loaded =====
            Rectangle {
                visible: calibrationPage.profileMissing
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.scaled(600)
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: missingColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: missingColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "sensorCalibration.profileMissing.title"
                        fallback: "The test profile is missing"
                        font: Theme.subtitleFont
                        color: Theme.warningColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.profileMissing.body",
                                  "Decenza could not load the %1 test profile, so this calibration "
                                  + "cannot run. Nothing has been changed on your machine.")
                              .arg(calibrationPage.sensorLabel)
                        color: Theme.textColor
                        font: Theme.bodyFont
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ===== Prepare =====
            Rectangle {
                visible: !calibrationPage.profileMissing
                         && (calibrationPage.armed || calibrationPage.noHold || calibrationPage.aborted)
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
                        fallback: "Before you start"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: calibrationPage.instrumentText
                        color: Theme.textColor
                        font: Theme.bodyFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.prepare.profileLoaded",
                                  "The %1 test profile is loaded. Start the shot as you normally would "
                                  + "and let it hold, then come back here.").arg(calibrationPage.sensorLabel)
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    // Precise about what has and has not happened. The test
                    // profile IS already uploaded — saying "nothing has been
                    // changed" would be false, and it is exactly the sentence a
                    // user who finds they lack the instrument would rely on.
                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.prepare.nothingWritten",
                                  "No calibration has been changed. Leaving this page puts your "
                                  + "previous profile back.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    // Two different answers, because they need opposite advice.
                    // "Run it again more carefully" is wrong for a run that never
                    // got far enough to hold at all.
                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.noHold && !SensorCalibration.neverPoured
                        text: TranslationManager.translate(
                                  "sensorCalibration.noHold",
                                  "That run never held steady long enough to measure. Run it again "
                                  + "and let it settle before stopping.")
                        color: Theme.warningColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.noHold && SensorCalibration.neverPoured
                        text: TranslationManager.translate(
                                  "sensorCalibration.neverPoured",
                                  "That run ended before any water flowed, so there was nothing to "
                                  + "measure. Let it run through to the hold.")
                        color: Theme.warningColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.aborted
                        text: TranslationManager.translate(
                                  "sensorCalibration.aborted",
                                  "That run was interrupted, so nothing was measured. Try again once "
                                  + "the machine is back.")
                        color: Theme.warningColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    AccessibleButton {
                        Layout.alignment: Qt.AlignRight
                        visible: calibrationPage.noHold || calibrationPage.aborted
                        text: TranslationManager.translate("sensorCalibration.runAgain", "Run again")
                        accessibleName: TranslationManager.translate("sensorCalibration.runAgain", "Run again")
                        primary: true
                        onClicked: SensorCalibration.arm(calibrationPage.sensor)
                    }
                }
            }

            // ===== Observing =====
            Rectangle {
                visible: calibrationPage.observing
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.scaled(600)
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: observeColumn.implicitHeight + Theme.scaled(24)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: observeColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(10)

                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        key: "sensorCalibration.observing.title"
                        fallback: "Watching the run"
                        font: Theme.titleFont
                        color: Theme.textColor
                    }

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: calibrationPage.observing
                        Accessible.ignored: true
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: TranslationManager.translate(
                                  "sensorCalibration.observing.hint",
                                  "Read your gauge while the machine holds. "
                                  + "You will enter that number when the run ends.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ===== Entry and confirm. Present ONLY once a run has been measured =====
            Rectangle {
                visible: calibrationPage.measured
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
                            text: TranslationManager.translate("sensorCalibration.apply.machineRead",
                                                               "Your machine read")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            // Measured by the app, never typed — the whole reason
                            // this page exists rather than a settings field.
                            text: SensorCalibration.measuredValue.toFixed(2) + " " + calibrationPage.unit
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        StyledTextField {
                            id: instrumentField
                            Layout.fillWidth: true
                            // Commit before anything READS the text. The Apply
                            // button's enabled state and the summary below both
                            // read it, so committing only inside the write
                            // handler would leave the in-progress word out of
                            // the gate on mobile (CLAUDE.md's IME rule).
                            onEditingFinished: Keyboard.commit()
                            placeholderText: TranslationManager.translate(
                                                 "sensorCalibration.apply.placeholder",
                                                 "Reading from your gauge")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            Accessible.name: TranslationManager.translate(
                                                 "sensorCalibration.apply.placeholder",
                                                 "Reading from your gauge")
                        }

                        // Always the sensor's own unit. For temperature that is
                        // Celsius whatever the display preference says, because the
                        // machine register is Celsius and converting silently is how
                        // a Fahrenheit number gets written into it.
                        Text {
                            text: calibrationPage.unit
                            color: Theme.textSecondaryColor
                            font: Theme.bodyFont
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.rejection.length > 0
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
                        // %4 comes from _signed(), which already appends the unit —
                        // so it is NOT followed by another %3.
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.summary",
                                  "Machine %1 %3, gauge %2 %3 — correction %4")
                              .arg(SensorCalibration.measuredValue.toFixed(2))
                              .arg(calibrationPage.entryValue.toFixed(2))
                              .arg(calibrationPage.unit)
                              .arg(calibrationPage._signed(calibrationPage.entryValue
                                                           - SensorCalibration.measuredValue))
                        color: Theme.textColor
                        font: Theme.bodyFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.writeFailed
                        text: TranslationManager.translate(
                                  "sensorCalibration.apply.writeFailed",
                                  "Your machine did not accept that — it may have disconnected. "
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
                        // DE1Device.connected too: a BLE drop while the state is
                        // Measured does not abort the session (the measurement is
                        // still valid), so without this the Apply card stays live
                        // with a working button after the machine has gone.
                        enabled: calibrationPage.entryValid && calibrationPage.hasStored
                                 && DE1Device.connected
                        onClicked: {
                            // The commit can change the text, so re-check rather
                            // than trusting the gate that opened this.
                            Keyboard.commit()
                            if (calibrationPage.entryValid && calibrationPage.hasStored
                                    && DE1Device.connected)
                                confirmDialog.open()
                        }
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
                        fallback: "Now run it again"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.verify.body",
                                  "Run the test profile again and compare. Repeat until the two agree.")
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
    }

    // Parsed entry state. Kept as page properties so the summary, the guard
    // message and the button all read the same values.
    readonly property double entryValue: parseFloat(instrumentField.text)
    readonly property string rejection: {
        void(calibrationPage._trVersion)
        // rejectionReason() reads hasMeasurement()/measuredValue() internally,
        // both NOTIFY stateChanged — and a C++ invokable records no dependency,
        // the same trap as the translating ones above. Without this the verdict
        // can be left over from a superseded measurement: start a second run
        // without touching the page and the summary re-renders from the new
        // value while the guard that allowed it was computed against the old.
        void(SensorCalibration.state)
        if (instrumentField.text.length === 0) return ""
        return SensorCalibration.rejectionReason(calibrationPage.sensor, calibrationPage.entryValue)
    }
    readonly property bool entryValid: instrumentField.text.length > 0
                                       && !isNaN(calibrationPage.entryValue)
                                       && calibrationPage.rejection.length === 0

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
                          "Machine %1 %3, gauge %2 %3. This changes your machine's calibration.")
                      .arg(SensorCalibration.measuredValue.toFixed(2))
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
                        Keyboard.commit()
                        // Refuse a value the commit turned invalid — the summary
                        // above and this write must never disagree.
                        if (!calibrationPage.entryValid || !calibrationPage.hasStored) {
                            confirmDialog.close()
                            return
                        }
                        var gap = calibrationPage.entryValue - SensorCalibration.measuredValue
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
                        calibrationPage._readBothCalibrations()
                        calibrationPage.wroteThisSession = true
                        instrumentField.text = ""
                        SensorCalibration.arm(calibrationPage.sensor)
                        confirmDialog.close()
                    }
                }
            }
        }
    }
}
