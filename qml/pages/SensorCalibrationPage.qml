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
// The page does NOT start the shot: the machine has a GHC and the user starts it
// there. It does keep the screen while that shot runs, which main.qml's phase
// handler has an explicit exemption for — its instructions and entry field are
// no use on the espresso page.
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

    // The machine's stored and factory offsets, re-read after every write. Both
    // are ABSENT until the machine answers — never shown as 0, which would read
    // as "no correction" and is the one wrong answer that looks plausible.
    readonly property int _calVersion: DE1Device.calibrationVersion
    readonly property bool hasStored: {
        void(calibrationPage._calVersion)
        return DE1Device.hasStoredCalibration(calibrationPage.calTarget)
    }
    readonly property double storedOffset: {
        void(calibrationPage._calVersion)
        return DE1Device.storedCalibration(calibrationPage.calTarget)
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

    Component.onCompleted: {
        // Load this sensor's test profile, the way tapping it in the profile list
        // would. NOT restored on exit: leaving it loaded is what keeps
        // isTestProfileActive true when the user comes back to type their gauge
        // reading, and a restore would only have to load it again. The profile
        // name is on screen throughout, as with any other profile load.
        ProfileManager.loadProfile(SensorCalibration.profileFilename(calibrationPage.sensor))
        calibrationPage._readCalibration()
    }

    // The stored offset belongs to one machine, so DE1Device clears it on
    // disconnect. Without this the page would sit on "not read yet" for the rest
    // of its life after a reconnect, Apply disabled, nothing saying why.
    Connections {
        target: DE1Device
        function onConnectedChanged() {
            if (DE1Device.connected)
                calibrationPage._readCalibration()
        }
    }

    // The instrument field sits mid-page inside the scroll area, so a soft
    // keyboard would cover it without this (CLAUDE.md: wrap pages with text
    // inputs). targetFlickable lets Android scroll it into view, which adjustPan
    // cannot do inside a Flickable.
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
                        fallback: "Your machine's calibration"
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
                        font: Theme.bodyFont
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: calibrationPage.haveDeclaredHold
                        text: TranslationManager.translate(
                                  "sensorCalibration.prepare.body",
                                  "Start the shot as you normally would. The machine will hold at "
                                  + "%1 %2 — read your gauge while it holds, then enter that value below.")
                              .arg(calibrationPage.declaredHold.toFixed(2))
                              .arg(calibrationPage.unit)
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
                        spacing: Theme.scaled(8)

                        StyledTextField {
                            id: instrumentField
                            Layout.fillWidth: true
                            // Commit before anything READS the text. The Apply
                            // button's enabled state and the summary below both
                            // read it, so committing only in the write handler
                            // would leave the in-progress word out of the gate
                            // on mobile (CLAUDE.md's IME rule).
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
                        // Celsius whatever the display preference says, because
                        // the machine register is Celsius and converting
                        // silently is how a Fahrenheit number gets written.
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
                                  "Your machine applies about a tenth of a correction at a time, "
                                  + "so expect several runs before the two agree.")
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
                        onClicked: {
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
                        fallback: "Run it again"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate(
                                  "sensorCalibration.verify.body",
                                  "Run the test profile again and compare. Each run closes about a "
                                  + "tenth of the gap, so repeat until the two agree.")
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
    // Locale-aware. parseFloat is C-locale only, so on a comma-decimal locale
    // "88,5" silently became 88 — and 88 is a plausible reading, so nothing
    // downstream could tell it was wrong. The keyboard offers whatever separator
    // the system uses, which is why this cannot be left to the user.
    readonly property double entryValue: {
        var t = instrumentField.text.trim()
        if (t.length === 0) return NaN
        var viaLocale = Number.fromLocaleString(Qt.locale(), t)
        if (!isNaN(viaLocale)) return viaLocale
        return parseFloat(t.replace(",", "."))
    }
    readonly property string rejection: {
        void(calibrationPage._trVersion)
        // rejectionReason() reads the declared hold internally, which depends on
        // the active profile — and a C++ invokable records no dependency, the
        // same trap as the translating ones above.
        void(calibrationPage._ctxVersion)
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
                        Keyboard.commit()
                        // Refuse a value the commit turned invalid — the summary
                        // above and this write must never disagree.
                        if (!calibrationPage.entryValid || !calibrationPage.hasStored) {
                            confirmDialog.close()
                            return
                        }
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
                        instrumentField.text = ""
                        confirmDialog.close()
                    }
                }
            }
        }
    }
}
