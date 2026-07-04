#include "livesteamcoach.h"

#include <QDebug>

#include "../core/settings.h"
#include "../core/settings_app.h"
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
        // The authoritative end-of-steam event (never time-predicted).
        connect(m_machineState, &MachineState::steamFlowStopped,
                this, &LiveSteamCoach::onSteamFlowStopped);
    } else {
        // Breadcrumb: without a MachineState the coach is permanently inert.
        qDebug() << "[SteamCoach] no MachineState — coach inert";
    }

    // Cache the two toggle values and keep them fresh via NOTIFY signals
    // (event-based flags — no per-tick QSettings reads).
    if (m_settings && m_settings->app()) {
        connect(m_settings->app(), &SettingsApp::steamCoachVisualEnabledChanged,
                this, &LiveSteamCoach::refreshEnabledFlags);
        connect(m_settings->app(), &SettingsApp::steamCoachAudioEnabledChanged,
                this, &LiveSteamCoach::refreshEnabledFlags);
        refreshEnabledFlags();
    } else {
        qDebug() << "[SteamCoach] no Settings — coach disabled";
    }
}

void LiveSteamCoach::refreshEnabledFlags() {
    if (!m_settings || !m_settings->app())
        return;
    m_visualEnabled = m_settings->app()->steamCoachVisualEnabled();
    m_audioEnabled = m_settings->app()->steamCoachAudioEnabled();
}

void LiveSteamCoach::onPhaseChanged() {
    if (!m_machineState)
        return;
    const bool nowSteaming = m_machineState->phase() == MachineState::Phase::Steaming;

    if (nowSteaming && !m_steaming) {
        // Steam just started: clear any stale state from a previous steam.
        // Deliberately NO evaluate() here: SteamPage applies the weight-scaled
        // duration (and sets durationMilkDerived) in its OWN phaseChanged
        // handler, which runs after this C++ slot. The first shot-time tick
        // (~100ms) drives the opening cue instead, by which time the page
        // state has settled — event-ordering, not a timer.
        resetState();
        m_steaming = true;
    } else if (!nowSteaming && m_steaming) {
        // Steam ended: silence and reset.
        m_steaming = false;
        resetState();
    }
}

void LiveSteamCoach::onShotTimeChanged() {
    // During steaming, shotTime is the elapsed steam time. Re-check milestones
    // as it advances. A fully disabled coach does no per-tick work.
    if (!m_steaming)
        return;
    // The clock ticked during THIS operation — duration-paced cues may trust
    // it now (see the m_sawShotTick guard in evaluate()).
    m_sawShotTick = true;
    if (!coachingActive())
        return;
    evaluate();
}

void LiveSteamCoach::onSteamFlowStopped() {
    // The machine event that actually ends steam flow (auto-stop at the target,
    // or a manual stop). This — not a predicted time window — drives the "done"
    // cue, so it cannot be missed when the local 100ms clock lags the firmware's
    // own countdown (the shot timer freezes the instant flow stops).
    if (!m_steaming || !m_machineState)
        return;
    // Latch even when no cue results (early abort / untimed / disabled):
    // whatever happens after this, the flow has stopped and there is nothing
    // left to coach — evaluate() keys off this.
    const bool alreadyStopped = m_flowStopped;
    m_flowStopped = true;
    if (alreadyStopped || m_firedCompletion || !coachingActive())
        return;

    // Untimed/manual steam: there is no target to complete against, and the user
    // stopped it themselves — no announcement.
    const int timeout = (m_settings && m_settings->brew())
                            ? m_settings->brew()->steamTimeout() : 0;
    if (timeout <= 0)
        return;

    // No coaching without a milk-derived duration (see evaluate()) — a "done"
    // for a fixed preset timer would bless a duration that may have ruined
    // the milk long before it fired.
    if (!m_durationMilkDerived)
        return;

    // Early manual abort (flow stopped well before the target): deliberate,
    // needs no announcement. Within the window = natural completion — this also
    // absorbs firmware/local clock drift (firmware stopping slightly "early").
    // The m_sawShotTick guard is belt-and-suspenders against classifying off a
    // stale clock (the emitter already only fires for observed flow).
    const double remaining = static_cast<double>(timeout) - m_machineState->shotTime();
    if (remaining > COMPLETION_WINDOW_SEC || !m_sawShotTick)
        return;

    m_firedCompletion = true;
    // Notification wording (not an instruction — the machine has already
    // stopped). Spoken assertively so it lands promptly, eyes on the pitcher.
    // Known tradeoff: if the stop arrived via a direct Steaming->Idle phase
    // change (intermediate Puffing/Ending notifications dropped), the queued
    // phaseChanged delivers right after this and resetState() clears the cue
    // before QML paints — the VISUAL pill is lost on that rare path. The
    // spoken cue below is emitted synchronously and always survives. On the
    // normal auto-stop path the phase stays Steaming through the purge, so
    // the pill shows for its full auto-dismiss window.
    emitCue(QStringLiteral("steam-done"),
            tr_("steamCoach.cue.done", "Steam done"),
            QStringLiteral("positive"), /*speak=*/true, /*interrupt=*/true);
}

void LiveSteamCoach::evaluate() {
    if (!m_steaming || !m_machineState || !m_settings)
        return;
    // Never coach a stopped flow: once steamFlowStopped has fired, technique
    // milestones (roll/almost) must not surface on a late/frozen-clock tick —
    // the pour is over even while the phase lingers in Steaming (Puffing/Ending).
    if (m_flowStopped)
        return;

    // No coaching without a milk-derived duration. A fixed preset duration
    // says nothing about the milk actually in the pitcher — pacing cues off
    // it would confidently endorse ruining it (200 mL against a 60 s preset
    // is destroyed long before "Almost there"). Show one informational pill
    // (visual only — never spoken) so the silence is explained and actionable:
    // rest the pitcher on the scale next time.
    if (!m_durationMilkDerived) {
        if (!m_firedNoCoaching) {
            m_firedNoCoaching = true;
            emitCue(QStringLiteral("no-coaching"),
                    tr_("steamCoach.cue.noCoaching",
                        "No coaching — milk weight not captured"),
                    QStringLiteral("info"), /*speak=*/false);
        }
        return;
    }

    const double elapsed = m_machineState->shotTime();

    // Stretch: at the very start, keep the tip near the surface to fold in air.
    // Not gated on the target duration so it always fires, even for a manual
    // (untimed) steam.
    if (!m_firedStretch) {
        m_firedStretch = true;
        emitCue(QStringLiteral("steam-stretch"),
                tr_("steamCoach.cue.stretch",
                    "Steaming — keep the tip near the surface to stretch"),
                QStringLiteral("info"), /*speak=*/true);
        return;
    }

    // The remaining milestones need a known target duration to pace against.
    // A manual/untimed steam (steamTimeout <= 0) gets only the stretch cue
    // (its end is user-decided, so no completion cue either).
    const int timeout = m_settings->brew() ? m_settings->brew()->steamTimeout() : 0;
    if (timeout <= 0) {
        if (!m_loggedUntimed) {
            m_loggedUntimed = true;
            qDebug() << "[SteamCoach] no target duration (manual steam) — stretch cue only";
        }
        return;
    }
    // Trust the elapsed clock only after it has ticked during this operation:
    // if steam entered mid-sequence (missed BLE substate notification) the
    // shot timer never restarted and shotTime() still holds the PREVIOUS
    // operation's final value — pacing "almost"/"roll" off it would misfire
    // at steam entry. (The stretch cue above is not time-paced, so it's fine.)
    if (!m_sawShotTick)
        return;
    const double remaining = static_cast<double>(timeout) - elapsed;

    // Almost: a heads-up before the end, by fraction or by seconds-remaining
    // (whichever comes first, so short steams still get a warning). The "done"
    // cue itself is event-driven (onSteamFlowStopped), never predicted.
    if (!m_firedAlmost
        && (remaining <= ALMOST_REMAINING_SEC
            || elapsed >= ALMOST_FRACTION * timeout)) {
        m_firedAlmost = true;
        emitCue(QStringLiteral("steam-almost"),
                tr_("steamCoach.cue.almost", "Almost there — get ready"),
                QStringLiteral("info"), /*speak=*/true);
        return;
    }

    // Roll: past the stretch window, submerge the tip to texture. Skipped on very
    // short steams (no time to switch technique).
    if (!m_firedRoll && timeout >= MIN_DURATION_FOR_ROLL_SEC
        && elapsed >= ROLL_FRACTION * timeout) {
        m_firedRoll = true;
        emitCue(QStringLiteral("steam-roll"),
                tr_("steamCoach.cue.roll", "Submerge the tip — roll and texture"),
                QStringLiteral("info"), /*speak=*/true);
        return;
    }
}

void LiveSteamCoach::emitCue(const QString& id, const QString& text,
                             const QString& severity, bool speak, bool interrupt) {
    // Dedupe: don't re-publish a cue that is already the active one. (Belt and
    // suspenders — every caller is also one-shot latched.)
    if (m_cueActive && id == m_activeCueId)
        return;

    m_activeCueId = id;
    m_cueText = text;
    m_cueSeverity = severity;
    m_cueActive = true;
    emit cueChanged();

    // Voice is a service concern, gated only on the dedicated audio setting —
    // not on the banner, the page, or the accessibility master switch.
    if (speak && m_audioEnabled)
        emit speakRequested(text, interrupt);
}

void LiveSteamCoach::clearCue() {
    if (!m_cueActive && m_cueText.isEmpty())
        return;
    m_cueActive = false;
    m_cueText.clear();
    m_cueSeverity.clear();
    m_activeCueId.clear();
    emit cueChanged();
}

void LiveSteamCoach::resetState() {
    m_firedStretch = false;
    m_firedRoll = false;
    m_firedAlmost = false;
    m_firedCompletion = false;
    m_flowStopped = false;
    m_sawShotTick = false;
    m_firedNoCoaching = false;
    m_loggedUntimed = false;
    // m_durationMilkDerived is NOT reset here — SteamPage's binding owns it.
    clearCue();
}
