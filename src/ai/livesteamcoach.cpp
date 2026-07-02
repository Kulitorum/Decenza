#include "livesteamcoach.h"

#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/translationmanager.h"
#include "../machine/machinestate.h"

// All user-visible / spoken strings go through TranslationManager so they are
// translatable (CLAUDE.md i18n rule). Falls back to the English source when no
// TranslationManager has been injected.
QString LiveSteamCoach::tr_(const char* key, const char* fallback) const {
    if (m_translationManager)
        return m_translationManager->translate(QString::fromUtf8(key),
                                               QString::fromUtf8(fallback));
    return QString::fromUtf8(fallback);
}

LiveSteamCoach::LiveSteamCoach(MachineState* machineState, Settings* settings,
                               QObject* parent)
    : QObject(parent)
    , m_machineState(machineState)
    , m_settings(settings)
{
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged,
                this, &LiveSteamCoach::onPhaseChanged);
        connect(m_machineState, &MachineState::shotTimeChanged,
                this, &LiveSteamCoach::onShotTimeChanged);
    }
}

void LiveSteamCoach::onPhaseChanged() {
    if (!m_machineState)
        return;
    const bool nowSteaming = m_machineState->phase() == MachineState::Phase::Steaming;

    if (nowSteaming && !m_steaming) {
        // Steam just started: clear any stale state from a previous steam.
        resetState();
        m_steaming = true;
        evaluate();  // fire the opening "stretch" cue right away
    } else if (!nowSteaming && m_steaming) {
        // Steam ended: silence and reset.
        m_steaming = false;
        resetState();
    }
}

void LiveSteamCoach::onShotTimeChanged() {
    // During steaming, shotTime is the elapsed steam time. Re-check milestones
    // as it advances.
    if (!m_steaming)
        return;
    evaluate();
}

void LiveSteamCoach::evaluate() {
    if (!m_steaming || !m_machineState || !m_settings)
        return;

    const double elapsed = m_machineState->shotTime();

    // Stretch: at the very start, keep the tip near the surface to fold in air.
    // Not gated on the target duration so it always fires, even for a manual
    // (untimed) steam.
    if (!m_firedStretch) {
        m_firedStretch = true;
        emitCue(QStringLiteral("steam-stretch"),
                tr_("steamCoach.cue.stretch",
                    "Steaming — keep the tip near the surface to stretch"),
                QStringLiteral("info"), /*speak=*/true, elapsed);
        return;
    }

    // The remaining milestones need a known target duration to pace against.
    // A manual/untimed steam (steamTimeout <= 0) gets only the stretch cue.
    const int timeout = m_settings->brew() ? m_settings->brew()->steamTimeout() : 0;
    if (timeout <= 0)
        return;
    const double remaining = static_cast<double>(timeout) - elapsed;

    // Stop (highest priority near the end): the machine is about to auto-stop.
    // Spoken and assertive — this is the cue you steam by, eyes off the screen.
    if (!m_firedStop && remaining <= STOP_REMAINING_SEC) {
        m_firedStop = true;
        emitCue(QStringLiteral("steam-stop"),
                tr_("steamCoach.cue.stop", "Stop — steam finishing"),
                QStringLiteral("caution"), /*speak=*/true, elapsed);
        return;
    }

    // Almost: a heads-up before the stop, by fraction or by seconds-remaining
    // (whichever comes first, so short steams still get a warning).
    if (!m_firedAlmost
        && (remaining <= ALMOST_REMAINING_SEC
            || elapsed >= ALMOST_FRACTION * timeout)) {
        m_firedAlmost = true;
        emitCue(QStringLiteral("steam-almost"),
                tr_("steamCoach.cue.almost", "Almost there — get ready"),
                QStringLiteral("info"), /*speak=*/true, elapsed);
        return;
    }

    // Roll: past the stretch window, submerge the tip to texture. Skipped on very
    // short steams (no time to switch technique).
    if (!m_firedRoll && timeout >= MIN_DURATION_FOR_ROLL_SEC
        && elapsed >= ROLL_FRACTION * timeout) {
        m_firedRoll = true;
        emitCue(QStringLiteral("steam-roll"),
                tr_("steamCoach.cue.roll", "Submerge the tip — roll and texture"),
                QStringLiteral("info"), /*speak=*/true, elapsed);
        return;
    }
}

void LiveSteamCoach::emitCue(const QString& id, const QString& text,
                             const QString& severity, bool speak, double clock) {
    // Dedupe: don't re-publish a cue that is already the active one.
    if (m_cueActive && id == m_activeCueId)
        return;

    // Spoken-cue spacing: gate voice on steam-time elapsed since the last spoken
    // cue (clock-based, not a guard Timer). Visual cue still shows. The "stop"
    // caution is the safety cue you steam by, so it is exempt — the spacing gate
    // must never silence it on a very short steam.
    bool willSpeak = speak;
    if (willSpeak && severity != QStringLiteral("caution")
        && (clock - m_lastSpokenClock) < MIN_SPOKEN_SPACING_SEC)
        willSpeak = false;
    if (willSpeak)
        m_lastSpokenClock = clock;

    m_activeCueId = id;
    m_cueText = text;
    m_cueSeverity = severity;
    m_cueSpeak = willSpeak;
    m_cueActive = true;
    emit cueChanged();
}

void LiveSteamCoach::clearCue() {
    if (!m_cueActive && m_cueText.isEmpty())
        return;
    m_cueActive = false;
    m_cueSpeak = false;
    m_cueText.clear();
    m_cueSeverity.clear();
    m_activeCueId.clear();
    emit cueChanged();
}

void LiveSteamCoach::resetState() {
    m_firedStretch = false;
    m_firedRoll = false;
    m_firedAlmost = false;
    m_firedStop = false;
    m_lastSpokenClock = -1000.0;
    clearCue();
}
