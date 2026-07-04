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
// stretch at the start, roll/texture through the middle, and an "almost" heads-up
// near the end. The end-of-steam "done" cue is NOT time-predicted: it fires from
// MachineState::steamFlowStopped() — the actual machine event that ends steam
// flow — so clock/firmware drift within the completion window can't cause a
// missed cue (drift beyond it classifies as an abort; see COMPLETION_WINDOW_SEC).
// An early manual stop (well before the target) is a deliberate abort and stays
// silent.
//
// Gating: two independent user settings, both off by default.
//   - Settings.app.steamCoachVisualEnabled — the on-screen banner (QML binds the
//     cue surface below).
//   - Settings.app.steamCoachAudioEnabled — spoken cues. The coach itself emits
//     speakRequested() (wired to AccessibilityManager::announceCoaching in
//     main.cpp), so audio works with the banner disabled and is independent of
//     the accessibility master switch.
// When both are off the coach does no per-tick evaluation work (the tick slot
// still runs, returning after two cached-bool checks).
//
// Milk-derived duration requirement: coaching only happens when the session's
// steam duration was derived from the ACTUAL milk weight (weight-timed steaming
// captured the pitcher on the scale — SteamPage binds `durationMilkDerived`
// from its steamTimeoutScaled state). A fixed preset duration says nothing
// about the milk in the pitcher, so pacing cues off it would confidently
// endorse ruining the milk (200 mL against a 60 s preset is destroyed long
// before "Almost there"). Without a milk-derived duration the coach emits a
// single informational pill ("No coaching — milk weight not captured", visual
// only) and otherwise behaves as if the feature were off.
//
// Rate control is one-shot latching only — each milestone fires at most once per
// steam operation (at most four cues total), so no spacing governor is needed and
// no distinct milestone can ever be rate-limited away.
//
// Cues are exposed to QML as individual marshalable Q_PROPERTYs (QString / bool)
// — never a struct return — so LiveCoachingBanner (steam page mount) can bind
// directly. The banner is purely visual; it does not speak.
class LiveSteamCoach : public QObject {
    Q_OBJECT

    // QML-marshalable cue surface. The banner binds to these directly.
    // cueSeverity is one of the banner's "positive" | "info" | "caution".
    Q_PROPERTY(QString cueText READ cueText NOTIFY cueChanged)
    Q_PROPERTY(QString cueSeverity READ cueSeverity NOTIFY cueChanged)
    Q_PROPERTY(bool cueActive READ cueActive NOTIFY cueChanged)
    // Whether the session's steam duration is derived from the actual milk
    // weight (weight-timed steaming captured this session). Written by
    // SteamPage (bound to its steamTimeoutScaled state); steam is page-bound,
    // so the page-fed property is authoritative. Gates ALL coaching cues.
    Q_PROPERTY(bool durationMilkDerived READ durationMilkDerived WRITE setDurationMilkDerived NOTIFY durationMilkDerivedChanged)

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
    bool durationMilkDerived() const { return m_durationMilkDerived; }
    void setDurationMilkDerived(bool derived) {
        if (m_durationMilkDerived == derived)
            return;
        m_durationMilkDerived = derived;
        emit durationMilkDerivedChanged();
    }

    // --- Milestone thresholds (fractions of the target steam duration) ---
    // Switch the barista from stretching (folding in air) to rolling (texturing).
    static constexpr double ROLL_FRACTION = 0.35;
    // "Almost there" heads-up: this far through, OR this many seconds from the end
    // (whichever comes first, so short steams still get a warning).
    static constexpr double ALMOST_FRACTION = 0.80;
    static constexpr double ALMOST_REMAINING_SEC = 3.0;
    // Below this target duration, skip the mid "roll" cue — too short to switch.
    static constexpr double MIN_DURATION_FOR_ROLL_SEC = 8.0;
    // Completion-vs-abort classifier for the steam-end event: flow stopping with
    // more than this many seconds remaining is a deliberate early stop (silent);
    // within it, natural completion — which also absorbs local-clock/firmware
    // drift (the firmware may stop slightly "early" by our clock). Reuses the
    // "almost" window so the anticipation cue and the done cue agree on what
    // counts as "the end".
    static constexpr double COMPLETION_WINDOW_SEC = ALMOST_REMAINING_SEC;

signals:
    // Single change signal for the whole cue surface — cueText / cueSeverity /
    // cueActive always change together (one emitCue / clearCue call).
    void cueChanged();
    void durationMilkDerivedChanged();
    // Emitted when a cue should be spoken (steamCoachAudioEnabled is on).
    // Wired once (main.cpp) to AccessibilityManager::announceCoaching, which
    // bypasses the accessibility master switch — the coach's audio toggle is
    // its own opt-in. `interrupt` = assertive (only the completion cue).
    void speakRequested(const QString& text, bool interrupt);

private slots:
    void onPhaseChanged();
    void onShotTimeChanged();
    void onSteamFlowStopped();
    void refreshEnabledFlags();

private:
    // Either output (banner or voice) wants cues. When false the coach does no
    // per-tick evaluation work at all.
    bool coachingActive() const { return m_visualEnabled || m_audioEnabled; }
    // Run the milestone checks against the current elapsed steam time. Picks at
    // most one cue (priority order) and publishes it via emitCue().
    void evaluate();
    // Publish a cue. Deduped by `id` (won't re-emit the same cue id while it's
    // still the active cue). `speak` requests voice; it is honored only when the
    // audio setting is on. `interrupt` = assertive announcement.
    void emitCue(const QString& id, const QString& text,
                 const QString& severity, bool speak, bool interrupt = false);
    // Clear the active cue and all latch state. Called on steam start and steam
    // end (phase transitions to/from Steaming).
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

    // Dedupe key: the id of the currently-published cue.
    QString m_activeCueId;

    // Cached toggle values, refreshed by the SettingsApp NOTIFY signals
    // (event-based — no per-tick QSettings reads, no polling).
    bool m_visualEnabled = false;
    bool m_audioEnabled = false;

    // See the Q_PROPERTY: page-fed, gates all coaching. Deliberately NOT reset
    // in resetState() — its lifecycle is owned by SteamPage's binding.
    bool m_durationMilkDerived = false;

    // True while the machine is in the Steaming phase.
    bool m_steaming = false;

    // One-shot latches so each milestone cue fires at most once per steam.
    // Latching is the only rate control (max four cues per operation).
    bool m_firedStretch = false;
    bool m_firedRoll = false;
    bool m_firedAlmost = false;
    bool m_firedCompletion = false;
    // Steam flow has stopped (steamFlowStopped seen) while the phase may still
    // linger in Steaming (Puffing/Ending). Blocks all further milestone
    // evaluation — never coach a stopped flow.
    bool m_flowStopped = false;

    // The elapsed clock ticked during THIS operation. Until then shotTime()
    // may still hold the previous operation's final value (missed BLE substate
    // → shot timer never restarted), so duration-paced cues and the completion
    // classifier refuse to trust it.
    bool m_sawShotTick = false;

    // Once-per-steam latch for the "No coaching — milk weight not captured"
    // informational pill (visual only, never spoken).
    bool m_firedNoCoaching = false;

    // Once-per-steam breadcrumb for the untimed-steam degradation path.
    bool m_loggedUntimed = false;
};
