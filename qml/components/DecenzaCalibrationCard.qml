import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Decenza Scale calibration card. Lives on the Calibration tab.
//
// Flow per firmware coordination brief (firmware Decision 1 + 2 shipped):
//   1. User picks reference weight (custom value, or DE1 drip tray grate
//      preset = 295.5 g). Live readout shows current weight while empty.
//   2. User clicks "Tare empty scale" — sends standard tare frame. After
//      ~500 ms the live readout should be near 0; otherwise we tell the
//      user to clean the platter and try again.
//   3. User places the reference weight. Card waits for stability
//      (variance < 0.1 g over 500 ms) before enabling Calibrate.
//   4. User clicks "Calibrate". Card sends 0x10 frame with the reference
//      weight in decigrams via the active transport (BLE or Wi-Fi —
//      identical wire format on both per firmware brief).
//   5. After ~300 ms grace, card samples 1 s of weight readings and
//      checks |mean - reference| < 0.5 g. Persists a success ISO
//      timestamp to Settings.connections so future launches show the
//      "calibrated" state.
//
// Step 2 is critical — skipping the tare bakes whatever stale offset the
// scale had into the cps factor, producing a permanent constant error.
// The card refuses to advance to Calibrate until the tare step has run
// at least once in this session and the live reading was confirmed
// near zero.
Rectangle {
    id: card
    color: Theme.surfaceColor
    radius: Theme.cardRadius
    Layout.fillWidth: true
    implicitHeight: cardContent.implicitHeight + Theme.scaled(30)

    // Visible only when a Decenza scale is the active scale. Anything
    // else (FlowScale, Acaia, Bookoo, etc.) hides the card — its
    // calibration command is Decent-Scale-protocol-specific, and only
    // the user's DecenzaScale firmware honors the 0x10 byte.
    visible: ScaleDevice && ScaleDevice.connected
             && ScaleDevice.type === "decent"
             && ScaleDevice.name
             && ScaleDevice.name.toLowerCase().indexOf("decenza") >= 0

    // ─── State ─────────────────────────────────────────────────
    // Phase machine — drives which buttons are enabled and what label
    // the status row shows. "idle" is the resting state; "succeeded"
    // and "failed" are terminal until the user hits Restart.
    property string phase: "idle"  // "idle" | "tareSent" | "tareConfirmed" | "calibrateSent" | "verifying" | "succeeded" | "failed"

    // Reference weight in grams. 100.0 g is a sensible default — many
    // calibration weights ship at this value, and the DE1 drip tray
    // grate preset (295.5 g) is one tap away.
    property real referenceGrams: 100.0
    property bool useGratePreset: false

    // Drip tray grate weight constant. Sourced from user-provided
    // measurement of the OEM grate; if Decent ever ships a revised
    // grate weight this constant becomes the single edit point.
    readonly property real gratePresetGrams: 295.5

    // Stability detection: track the last few weight samples and only
    // enable Calibrate when the spread is small. Without this the user
    // can press Calibrate while the weight is still settling and bake
    // a transient mid-reading into the scale factor.
    property var recentSamples: []
    property bool isStable: false
    readonly property real kStabilityThresholdG: 0.1
    readonly property int kStabilitySampleCount: 5  // 500 ms at 10 Hz

    // Post-calibration verification: collect ~10 samples (1 s at 10 Hz),
    // compute the mean, compare to reference within 0.5 g tolerance.
    property var verificationSamples: []
    property real verifiedWeightG: 0.0
    readonly property real kTolerancePassG: 0.5

    // Failure / status messages bubbled up to the user.
    property string statusMessage: ""

    // ─── Live weight tracking ───────────────────────────────────
    Connections {
        target: ScaleDevice
        function onWeightChanged(w) {
            // Stability rolling buffer for the calibrate-enable gate.
            card.recentSamples.push(w)
            while (card.recentSamples.length > card.kStabilitySampleCount) {
                card.recentSamples.shift()
            }
            if (card.recentSamples.length === card.kStabilitySampleCount) {
                let min = card.recentSamples[0], max = card.recentSamples[0]
                for (let i = 1; i < card.recentSamples.length; i++) {
                    if (card.recentSamples[i] < min) min = card.recentSamples[i]
                    if (card.recentSamples[i] > max) max = card.recentSamples[i]
                }
                card.isStable = (max - min) < card.kStabilityThresholdG
            } else {
                card.isStable = false
            }

            // Post-calibrate verification window: collect samples until
            // the verifyTimer stops the run.
            if (card.phase === "verifying") {
                card.verificationSamples.push(w)
            }
        }
    }

    // ─── Tare-confirm timer ─────────────────────────────────────
    Timer {
        id: tareConfirmTimer
        interval: 700  // tare command + heartbeat lag + a sample-or-two settle
        onTriggered: {
            const w = ScaleDevice ? ScaleDevice.weight : 999
            if (Math.abs(w) < 1.0) {
                card.phase = "tareConfirmed"
                card.statusMessage = ""
            } else {
                card.phase = "idle"
                card.statusMessage = qsTr("Tare didn't take (live reads %1 g). Make sure the platter is clean and empty, then try again.")
                                       .arg(w.toFixed(1))
            }
        }
    }

    // ─── Verification timer ─────────────────────────────────────
    Timer {
        id: verifyStartTimer
        interval: 300  // grace period after sending the calibrate frame
        onTriggered: {
            card.verificationSamples = []
            card.phase = "verifying"
            verifySampleTimer.start()
        }
    }
    Timer {
        id: verifySampleTimer
        interval: 1000  // 1 s sampling window
        onTriggered: {
            if (card.verificationSamples.length === 0) {
                card.phase = "failed"
                card.statusMessage = qsTr("No weight readings during verification. Is the scale still connected?")
                return
            }
            let sum = 0
            for (let i = 0; i < card.verificationSamples.length; i++) {
                sum += card.verificationSamples[i]
            }
            card.verifiedWeightG = sum / card.verificationSamples.length
            const error = Math.abs(card.verifiedWeightG - card.referenceGrams)
            if (error < card.kTolerancePassG) {
                card.phase = "succeeded"
                card.statusMessage = qsTr("Calibration successful — %1 g reads as %2 g.")
                                       .arg(card.referenceGrams.toFixed(1))
                                       .arg(card.verifiedWeightG.toFixed(1))
                Settings.connections.decenzaScaleLastCalibrationIso =
                    new Date().toISOString()
            } else {
                card.phase = "failed"
                card.statusMessage = qsTr("Calibration may have failed: expected %1 g, scale reads %2 g. Try again with the platter clean and the weight centred.")
                                       .arg(card.referenceGrams.toFixed(1))
                                       .arg(card.verifiedWeightG.toFixed(1))
            }
        }
    }

    function startTare() {
        if (!ScaleDevice || !ScaleDevice.connected) return
        card.phase = "tareSent"
        card.statusMessage = ""
        ScaleDevice.tare()
        tareConfirmTimer.restart()
    }
    function startCalibrate() {
        if (!ScaleDevice || !ScaleDevice.connected) return
        if (!ScaleDevice.calibrateToKnownWeight) return
        card.phase = "calibrateSent"
        card.statusMessage = ""
        ScaleDevice.calibrateToKnownWeight(card.referenceGrams)
        verifyStartTimer.restart()
    }
    function restart() {
        card.phase = "idle"
        card.statusMessage = ""
        card.verifiedWeightG = 0.0
        card.verificationSamples = []
    }

    Component.onCompleted: {
        console.log("[decenzaCal] card constructed; visible="
                    + visible + " phase=" + phase
                    + " referenceGrams=" + referenceGrams
                    + " ScaleDevice=" + (ScaleDevice ? "yes" : "null")
                    + " connected=" + (ScaleDevice ? ScaleDevice.connected : "n/a")
                    + " type=" + (ScaleDevice ? ScaleDevice.type : "n/a")
                    + " name=" + (ScaleDevice ? ScaleDevice.name : "n/a"))
    }

    // ─── Layout ─────────────────────────────────────────────────
    ColumnLayout {
        id: cardContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.scaled(15)
        spacing: Theme.scaled(10)

        // Title + calibration-status badge
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: TranslationManager.translate("decenzaCal.title", "Decenza Scale Calibration")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
                Layout.fillWidth: true
            }
            Rectangle {
                visible: Settings.connections.decenzaScaleLastCalibrationIso !== ""
                width: calStatusText.implicitWidth + Theme.scaled(12)
                height: Theme.scaled(20)
                radius: Theme.scaled(10)
                color: Qt.rgba(0.3, 0.7, 0.4, 0.25)
                Text {
                    id: calStatusText
                    anchors.centerIn: parent
                    text: TranslationManager.translate("decenzaCal.calibratedPill", "Calibrated")
                    color: "#3aa055"
                    font.pixelSize: Theme.scaled(10)
                    font.bold: true
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: Settings.connections.decenzaScaleLastCalibrationIso !== ""
                ? TranslationManager.translate("decenzaCal.calibratedDesc",
                    "Last calibrated: ") + Settings.connections.decenzaScaleLastCalibrationIso.substring(0, 10)
                : TranslationManager.translate("decenzaCal.uncalibratedDesc",
                    "This scale is not yet calibrated. Calibration sets the zero offset and a known-weight reference; the result is saved on the scale and survives reboots.")
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WordWrap
        }

        // ─── Weight selection ──────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(4)
            spacing: Theme.scaled(6)
            enabled: card.phase === "idle" || card.phase === "tareConfirmed"

            Text {
                text: TranslationManager.translate("decenzaCal.weightLabel", "Reference weight")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                font.bold: true
            }

            // Custom-value option
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)
                RadioButton {
                    id: customRadio
                    checked: !card.useGratePreset
                    onClicked: {
                        card.useGratePreset = false
                        card.referenceGrams = parseFloat(customWeightField.text)
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: TranslationManager.translate("decenzaCal.customWeight", "Use custom weight value")
                }
                Text {
                    text: TranslationManager.translate("decenzaCal.customLabel", "Custom weight:")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                }
                StyledTextField {
                    id: customWeightField
                    Layout.preferredWidth: Theme.scaled(90)
                    text: "100.0"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    enabled: customRadio.checked
                    validator: DoubleValidator {
                        bottom: 0.1
                        top: 3276.7  // int16-decigrams ceiling enforced by DecentScale::calibrateToKnownWeight
                        decimals: 1
                        notation: DoubleValidator.StandardNotation
                    }
                    onTextChanged: {
                        if (customRadio.checked && acceptableInput) {
                            card.referenceGrams = parseFloat(text)
                        }
                    }
                }
                Text {
                    text: "g"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                }
                Item { Layout.fillWidth: true }
            }

            // Drip-tray-grate preset option
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)
                RadioButton {
                    id: grateRadio
                    checked: card.useGratePreset
                    onClicked: {
                        card.useGratePreset = true
                        card.referenceGrams = card.gratePresetGrams
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: TranslationManager.translate("decenzaCal.useGrate",
                        "Use the DE1 drip tray grate as the reference weight")
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("decenzaCal.gratePreset",
                        "Use DE1 drip tray grate (") + card.gratePresetGrams.toFixed(1) + " g)"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ─── Live weight readout ───────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(6)
            color: Qt.darker(Theme.surfaceColor, 1.15)
            radius: Theme.scaled(4)
            height: Theme.scaled(50)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(10)
                anchors.rightMargin: Theme.scaled(10)
                Text {
                    text: TranslationManager.translate("decenzaCal.live", "Live: ")
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(13)
                }
                Text {
                    text: ScaleDevice ? ScaleDevice.weight.toFixed(1) + " g" : "—"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(20)
                    font.bold: true
                    Layout.fillWidth: true
                }
                // Stability indicator — only meaningful between tare-confirmed
                // and calibrate-sent, but visible at all times so the user
                // gets familiar with the signal.
                Rectangle {
                    width: stabilityText.implicitWidth + Theme.scaled(10)
                    height: Theme.scaled(18)
                    radius: Theme.scaled(9)
                    color: card.isStable
                        ? Qt.rgba(0.3, 0.7, 0.4, 0.22)
                        : Qt.rgba(0.7, 0.5, 0.2, 0.22)
                    Text {
                        id: stabilityText
                        anchors.centerIn: parent
                        text: card.isStable
                            ? TranslationManager.translate("decenzaCal.stable", "Stable")
                            : TranslationManager.translate("decenzaCal.settling", "Settling…")
                        color: card.isStable ? "#3aa055" : "#a07020"
                        font.pixelSize: Theme.scaled(10)
                        font.bold: true
                    }
                }
            }
        }

        // ─── Status message line ───────────────────────────────
        Text {
            Layout.fillWidth: true
            visible: card.statusMessage !== ""
            text: card.statusMessage
            color: card.phase === "failed" ? "#cc4444"
                 : card.phase === "succeeded" ? "#3aa055"
                 : Theme.textColor
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WordWrap
        }

        // ─── Step-instruction line ─────────────────────────────
        Text {
            Layout.fillWidth: true
            visible: card.statusMessage === ""
            text: {
                if (card.phase === "idle") {
                    return TranslationManager.translate("decenzaCal.step1Hint",
                        "Step 1 — Remove anything from the scale, then tap Tare empty scale.")
                }
                if (card.phase === "tareSent") {
                    return TranslationManager.translate("decenzaCal.step1Wait",
                        "Taring…")
                }
                if (card.phase === "tareConfirmed") {
                    return TranslationManager.translate("decenzaCal.step2Hint",
                        "Step 2 — Place the reference weight on the scale and wait for the reading to stabilise, then tap Calibrate.")
                }
                if (card.phase === "calibrateSent") {
                    return TranslationManager.translate("decenzaCal.calibrating",
                        "Sending calibration command…")
                }
                if (card.phase === "verifying") {
                    return TranslationManager.translate("decenzaCal.verifying",
                        "Verifying calibration (1 s)…")
                }
                return ""
            }
            color: Theme.textColor
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WordWrap
        }

        // ─── Action buttons ─────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(6)
            spacing: Theme.scaled(8)

            AccessibleButton {
                text: TranslationManager.translate("decenzaCal.tareBtn", "Tare empty scale")
                accessibleName: TranslationManager.translate("decenzaCal.tareBtnA11y",
                    "Tare the scale to set the zero offset")
                // The card itself only renders when the scale is connected
                // (see `visible:` chain at the root), so this button only
                // needs to gate on the wizard's phase.
                enabled: card.phase === "idle"
                         || card.phase === "failed"
                         || card.phase === "succeeded"
                onClicked: card.startTare()
            }

            AccessibleButton {
                text: TranslationManager.translate("decenzaCal.calibrateBtn", "Calibrate")
                accessibleName: TranslationManager.translate("decenzaCal.calibrateBtnA11y",
                    "Send the calibration command to the scale")
                enabled: card.phase === "tareConfirmed"
                         && card.isStable
                         && card.referenceGrams > 0
                onClicked: card.startCalibrate()
            }

            Item { Layout.fillWidth: true }

            AccessibleButton {
                visible: card.phase === "succeeded" || card.phase === "failed"
                text: TranslationManager.translate("decenzaCal.restart", "Restart")
                accessibleName: TranslationManager.translate("decenzaCal.restartA11y",
                    "Restart the calibration flow")
                onClicked: card.restart()
            }
        }
    }
}
