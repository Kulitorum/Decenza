#pragma once

#include <QObject>
#include <QString>

#include "../machine/machinestate.h"

class Settings;
class TranslationManager;

// During-steam live coaching cues — a LOCAL, real-time "reflex" layer that talks
// you through steaming milk AS IT HAPPENS. This is NOT the AI advisor (post-shot,
// network-backed): it runs cheap C++ checks on the live steam telemetry and emits
// at most one short, calm cue at a time, favouring silence.
//
// The DE1 does not measure milk temperature, so cues are NOT keyed off a milk
// thermometer. Instead they follow the elapsed steam time vs the target steam
// duration (Settings.brew.steamTimeout — weight-scaled for calibrated pitchers):
// stretch at the start, roll/texture through the middle, a heads-up near the end,
// and a spoken "stop" as the machine is about to finish. The spoken stop is the
// biggest accessibility win — you keep your eyes on the pitcher, not the screen.
//
// Cues are exposed to QML as individual marshalable Q_PROPERTYs (QString / bool)
// — never a struct return — so the shared LiveCoachingBanner can bind directly.
class LiveSteamCoach : public QObject {
    Q_OBJECT

    // QML-marshalable cue surface. The banner binds to these directly.
    // cueSeverity is one of "positive" | "info" | "caution".
    Q_PROPERTY(QString cueText READ cueText NOTIFY cueChanged)
    Q_PROPERTY(QString cueSeverity READ cueSeverity NOTIFY cueChanged)
    Q_PROPERTY(bool cueActive READ cueActive NOTIFY cueChanged)
    Q_PROPERTY(bool cueSpeak READ cueSpeak NOTIFY cueChanged)

public:
    explicit LiveSteamCoach(MachineState* machineState, Settings* settings,
                            QObject* parent = nullptr);

    // Optional — used to translate cue text. Without it cues fall back to the
    // English source strings. Mirrors AccessibilityManager::setTranslationManager.
    void setTranslationManager(TranslationManager* translationManager) {
        m_translationManager = translationManager;
    }

    QString cueText() const { return m_cueText; }
    QString cueSeverity() const { return m_cueSeverity; }
    bool cueActive() const { return m_cueActive; }
    bool cueSpeak() const { return m_cueSpeak; }

    // --- Milestone thresholds (fractions of the target steam duration) ---
    // Switch the barista from stretching (folding in air) to rolling (texturing).
    static constexpr double ROLL_FRACTION = 0.35;
    // "Almost there" heads-up: this far through, OR this many seconds from the end.
    static constexpr double ALMOST_FRACTION = 0.80;
    static constexpr double ALMOST_REMAINING_SEC = 3.0;
    // Spoken "stop" cue: this many seconds before the machine's auto-stop.
    static constexpr double STOP_REMAINING_SEC = 1.0;
    // Below this target duration, skip the mid "roll" cue — too short to switch.
    static constexpr double MIN_DURATION_FOR_ROLL_SEC = 8.0;
    // Minimum steam-time spacing between SPOKEN cues (clock-based, not a guard
    // timer — measured against the live steam clock / shotTime).
    static constexpr double MIN_SPOKEN_SPACING_SEC = 2.0;

signals:
    // Single change signal for the whole cue surface — cueText / cueSeverity /
    // cueActive / cueSpeak always change together (one emitCue / clearCue call).
    void cueChanged();

private slots:
    void onPhaseChanged();
    void onShotTimeChanged();

private:
    // Run the milestone checks against the current elapsed steam time. Picks at
    // most one cue (priority order) and publishes it via emitCue().
    void evaluate();
    // Publish a cue. Deduped by `id` (won't re-emit the same cue id while it's
    // still the active cue). `speak` is honored only if enough steam-time has
    // elapsed since the last spoken cue (event/clock-based spacing).
    void emitCue(const QString& id, const QString& text,
                 const QString& severity, bool speak, double clock);
    // Clear the active cue and all governor/latch state. Called on steam
    // start and steam end (phase transitions to/from Steaming).
    void resetState();
    void clearCue();

    MachineState* m_machineState = nullptr;
    Settings* m_settings = nullptr;
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
    // steam-time of the last SPOKEN cue (spacing is measured against the live
    // steam clock, never a guard Timer).
    QString m_activeCueId;
    double m_lastSpokenClock = -1000.0;

    // True while the machine is in the Steaming phase.
    bool m_steaming = false;

    // One-shot latches so each milestone cue fires at most once per steam.
    bool m_firedStretch = false;
    bool m_firedRoll = false;
    bool m_firedAlmost = false;
    bool m_firedStop = false;
};
