#pragma once

#include <QObject>
#include <QString>

#include "../machine/machinestate.h"

class DE1Device;
class ShotDataModel;
class TranslationManager;
struct ShotSample;

// During-shot live coaching cues — a LOCAL, real-time "reflex" layer that
// comments on the espresso shot AS IT RUNS. This is deliberately NOT the AI
// advisor (which is post-shot, network-backed): LiveShotCoach runs cheap C++
// analysis on the live telemetry stream (~5 Hz) and emits at most one short,
// calm cue at a time. It favors silence over false alarms — most shots get
// zero or one cue.
//
// It reuses only the cheap, partial-curve-safe detector concepts from
// ShotAnalysis (the pressure-floor "no puck" check and the flow-vs-goal delta).
// ShotAnalysis::analyzeShot() is post-shot only (it needs the full curve and a
// Gaussian-smoothed conductance derivative) and is never called here; channeling
// uses a conservative live PROXY instead of the real dC/dt detector.
//
// Cues are exposed to QML as individual marshalable Q_PROPERTYs (QString / bool)
// — never a struct return — so a banner can bind directly.
class LiveShotCoach : public QObject {
    Q_OBJECT

    // QML-marshalable cue surface. The banner binds to these directly.
    // cueSeverity is one of "positive" | "info" | "caution".
    Q_PROPERTY(QString cueText READ cueText NOTIFY cueChanged)
    Q_PROPERTY(QString cueSeverity READ cueSeverity NOTIFY cueChanged)
    Q_PROPERTY(bool cueActive READ cueActive NOTIFY cueChanged)
    Q_PROPERTY(bool cueSpeak READ cueSpeak NOTIFY cueChanged)

public:
    explicit LiveShotCoach(DE1Device* device, MachineState* machineState,
                           ShotDataModel* shotDataModel, QObject* parent = nullptr);

    // Optional — used to translate cue text. Without it cues fall back to the
    // English source strings. Mirrors AccessibilityManager::setTranslationManager.
    void setTranslationManager(TranslationManager* translationManager) {
        m_translationManager = translationManager;
    }

    QString cueText() const { return m_cueText; }
    QString cueSeverity() const { return m_cueSeverity; }
    bool cueActive() const { return m_cueActive; }
    bool cueSpeak() const { return m_cueSpeak; }

    // --- Detector thresholds (mirror the post-shot ShotAnalysis concepts) ---
    // Flow-vs-goal deviation that flags a fast/slow pour. Same value as
    // ShotAnalysis::FLOW_DEVIATION_THRESHOLD (the post-shot grind detector).
    static constexpr double FLOW_DEVIATION_THRESHOLD = 0.4;   // mL/s
    // Peak pressure floor below which the puck likely never seated. Same value
    // as ShotAnalysis::PRESSURE_FLOOR_BAR.
    static constexpr double PRESSURE_FLOOR_BAR = 2.5;          // bar
    // Channeling proxy: a sharp flow rise while pressure is flat/declining
    // during a pressure-controlled phase. Conservative — requires a clear jump.
    static constexpr double CHANNELING_FLOW_JUMP = 1.0;        // mL/s over the window
    static constexpr double CHANNELING_PRESSURE_FLAT = 0.15;   // bar — pressure must be ~flat/falling
    // On-pace cue: only speak it once, well into the pour, when we have a real
    // scale flow rate and a target.
    static constexpr double ONPACE_MIN_FLOW_GPS = 0.5;        // g/s — scale must be actually flowing
    static constexpr double ONPACE_MIN_REMAINING_G = 3.0;     // don't fire when basically done

    // Rolling window length (seconds of shot time) for the flow-vs-goal and
    // channeling-proxy averages.
    static constexpr double WINDOW_SEC = 3.0;
    // How far into a flow-relevant phase before flow-vs-goal / no-puck fire.
    static constexpr double POUR_SETTLE_SEC = 3.0;
    // Minimum shot-time spacing between SPOKEN cues (clock-based, not a guard
    // timer — measured against the live sample clock / shotTime).
    static constexpr double MIN_SPOKEN_SPACING_SEC = 8.0;

signals:
    // Single change signal for the whole cue surface — cueText / cueSeverity /
    // cueActive / cueSpeak always change together (one emitCue / clearCue call).
    void cueChanged();

private slots:
    void onShotSampleReceived(const ShotSample& sample);
    void onPhaseChanged();
    void onScaleWeightChanged();

private:
    // Append one telemetry point into the rolling window and drop stale ones.
    void pushSample(double shotTime, double pressure, double flow,
                    double goalFlow, double goalPressure);
    // Run all detectors against the current rolling state. Picks at most one
    // cue (priority order) and publishes it via emitCue().
    void evaluate(double shotTime);
    // Publish a cue. Deduped by `id` (won't re-emit the same cue id while it's
    // still the active cue). `speak` is honored only if enough shot-time has
    // elapsed since the last spoken cue (event/clock-based spacing).
    void emitCue(const QString& id, const QString& text,
                 const QString& severity, bool speak, double shotTime);
    // Clear the active cue and all rolling/governor state. Called on shot
    // start and shot end (phase transitions to/from a flow phase / idle).
    void resetState();
    void clearCue();

    static bool isFlowPhase(MachineState::Phase phase);

    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    ShotDataModel* m_shotDataModel = nullptr;
    TranslationManager* m_translationManager = nullptr;

    // Translate a cue string via the injected TranslationManager, or fall back
    // to the English source when none is set.
    QString tr_(const char* key, const char* fallback) const;

    // Published cue state (mirrors the Q_PROPERTYs).
    QString m_cueText;
    QString m_cueSeverity;
    bool m_cueActive = false;
    bool m_cueSpeak = false;

    // Governor: the id of the currently-published cue (dedupe key) and the
    // shot-time of the last SPOKEN cue (spacing is measured against the live
    // sample clock, never a guard Timer).
    QString m_activeCueId;
    double m_lastSpokenShotTime = -1000.0;

    // Tracks whether we are currently inside a shot (a flow phase). Toggled by
    // phase transitions; used to reset cleanly at shot start/end.
    bool m_shotActive = false;

    // Rolling window over recent samples (kept tiny: ~WINDOW_SEC at 5 Hz).
    struct Sample {
        double t = 0.0;          // shot time (s)
        double pressure = 0.0;   // bar
        double flow = 0.0;       // mL/s (group flow)
        double goalFlow = 0.0;   // mL/s
        double goalPressure = 0.0; // bar
    };
    QVector<Sample> m_window;

    // Peak group pressure seen this shot (for the no-puck check).
    double m_peakPressure = 0.0;
    // One-shot latches so each detector fires at most once per shot.
    bool m_firedNoPuck = false;
    bool m_firedChanneling = false;
    bool m_firedFlow = false;
    bool m_firedOnPace = false;
    // Phase-milestone latch: announce the preinfusion->pour transition once.
    bool m_firedPourStarted = false;
};
