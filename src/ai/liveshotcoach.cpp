#include "liveshotcoach.h"

#include <QtGlobal>
#include <cmath>

#include "../ble/de1device.h"
#include "../core/translationmanager.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"

// All user-visible / spoken strings go through TranslationManager so they are
// translatable (CLAUDE.md i18n rule). Falls back to the English source when no
// TranslationManager has been injected.
QString LiveShotCoach::tr_(const char* key, const char* fallback) const {
    if (m_translationManager)
        return m_translationManager->translate(QString::fromUtf8(key),
                                               QString::fromUtf8(fallback));
    return QString::fromUtf8(fallback);
}

LiveShotCoach::LiveShotCoach(DE1Device* device, MachineState* machineState,
                             ShotDataModel* shotDataModel, QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
    , m_shotDataModel(shotDataModel)
{
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &LiveShotCoach::onShotSampleReceived);
    }
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged,
                this, &LiveShotCoach::onPhaseChanged);
        connect(m_machineState, &MachineState::scaleWeightChanged,
                this, &LiveShotCoach::onScaleWeightChanged);
    }
}

bool LiveShotCoach::isFlowPhase(MachineState::Phase phase) {
    return phase == MachineState::Phase::Preinfusion
        || phase == MachineState::Phase::Pouring
        || phase == MachineState::Phase::Ending;
}

void LiveShotCoach::onPhaseChanged() {
    if (!m_machineState)
        return;
    const MachineState::Phase phase = m_machineState->phase();
    const bool nowInShot = isFlowPhase(phase);

    if (nowInShot && !m_shotActive) {
        // Shot just started: clear any stale state from a previous shot.
        resetState();
        m_shotActive = true;
    } else if (!nowInShot && m_shotActive) {
        // Shot ended (idle / steaming / etc.): silence and reset.
        m_shotActive = false;
        resetState();
        return;
    }

    // Phase milestone: announce the preinfusion -> pour transition once. Calm,
    // informational, not spoken (avoids competing with the more useful cues).
    if (m_shotActive && phase == MachineState::Phase::Pouring && !m_firedPourStarted) {
        m_firedPourStarted = true;
        const double shotTime = m_machineState->shotTime();
        emitCue(QStringLiteral("pour-started"),
                tr_("liveCoach.cue.pourStarted", "Pouring — flow building"),
                QStringLiteral("info"), /*speak=*/false, shotTime);
    }
}

void LiveShotCoach::onShotSampleReceived(const ShotSample& sample) {
    if (!m_shotActive || !m_machineState)
        return;

    const double shotTime = m_machineState->shotTime();
    if (sample.groupPressure > m_peakPressure)
        m_peakPressure = sample.groupPressure;

    pushSample(shotTime, sample.groupPressure, sample.groupFlow,
               sample.setFlowGoal, sample.setPressureGoal);
    evaluate(shotTime);
}

void LiveShotCoach::onScaleWeightChanged() {
    // The on-pace cue is driven off the scale; re-evaluate when weight updates
    // so it can fire even between BLE shot samples. Cheap: evaluate() is O(1).
    if (!m_shotActive || !m_machineState)
        return;
    evaluate(m_machineState->shotTime());
}

void LiveShotCoach::pushSample(double shotTime, double pressure, double flow,
                               double goalFlow, double goalPressure) {
    m_window.append(Sample{shotTime, pressure, flow, goalFlow, goalPressure});
    // Drop samples older than WINDOW_SEC. O(1) amortized.
    const double cutoff = shotTime - WINDOW_SEC;
    qsizetype drop = 0;
    while (drop < m_window.size() && m_window[drop].t < cutoff)
        ++drop;
    if (drop > 0)
        m_window.remove(0, drop);
}

void LiveShotCoach::evaluate(double shotTime) {
    if (!m_machineState)
        return;
    const MachineState::Phase phase = m_machineState->phase();

    // --- Detector 1 (highest priority): low pressure / no puck. ---
    // Well into the pour, if peak pressure never crossed the floor the puck
    // likely never seated. Mirrors ShotAnalysis::detectPourTruncated's concept
    // (pressure-only, PRESSURE_FLOOR_BAR) but on the partial live curve.
    if (!m_firedNoPuck && shotTime > POUR_SETTLE_SEC
        && (phase == MachineState::Phase::Pouring
            || phase == MachineState::Phase::Ending)
        && m_peakPressure < PRESSURE_FLOOR_BAR) {
        m_firedNoPuck = true;
        emitCue(QStringLiteral("no-puck"),
                tr_("liveCoach.cue.noPuck", "Low pressure — did the puck seat?"),
                QStringLiteral("caution"), /*speak=*/true, shotTime);
        return;
    }

    // --- Detector 2: channeling proxy. ---
    // A sharp flow rise while pressure stays flat/falling during a pressure-
    // controlled phase. Conservative live PROXY (the real dC/dt detector is
    // post-shot only). Requires a clear jump across the rolling window.
    if (!m_firedChanneling && shotTime > POUR_SETTLE_SEC
        && phase == MachineState::Phase::Pouring
        && m_window.size() >= 3) {
        const Sample& first = m_window.first();
        const Sample& last = m_window.last();
        const bool pressureControlled = last.goalPressure > PRESSURE_FLOOR_BAR;
        const double flowRise = last.flow - first.flow;
        const double pressureChange = last.pressure - first.pressure;
        if (pressureControlled
            && flowRise > CHANNELING_FLOW_JUMP
            && pressureChange < CHANNELING_PRESSURE_FLAT) {
            m_firedChanneling = true;
            emitCue(QStringLiteral("channeling"),
                    tr_("liveCoach.cue.channeling", "Possible channeling"),
                    QStringLiteral("caution"), /*speak=*/true, shotTime);
            return;
        }
    }

    // --- Detector 3: fast/slow flow vs goal. ---
    // Rolling avg of (flow - goalFlow) over the window during the pour.
    // |delta| > threshold -> running fast (bright) or slow (tight).
    // Fires at most once per shot to stay calm. Restricted to Pouring:
    // flow into a dry puck during Preinfusion routinely deviates from goal,
    // and because this is a one-shot latch, firing there would suppress the
    // legitimate pour-phase cue for the rest of the shot (same reason
    // detector 2 restricts to Pouring).
    if (!m_firedFlow && shotTime > POUR_SETTLE_SEC
        && phase == MachineState::Phase::Pouring
        && !m_window.isEmpty()) {
        double sumDelta = 0.0;
        qsizetype n = 0;
        for (const Sample& s : m_window) {
            if (s.goalFlow > 0.1) {  // only meaningful when there's a flow target
                sumDelta += (s.flow - s.goalFlow);
                ++n;
            }
        }
        if (n >= 3) {
            const double avgDelta = sumDelta / static_cast<double>(n);
            if (avgDelta > FLOW_DEVIATION_THRESHOLD) {
                m_firedFlow = true;
                emitCue(QStringLiteral("flow-fast"),
                        tr_("liveCoach.cue.flowFast",
                            "Running fast — this'll come in bright"),
                        QStringLiteral("info"), /*speak=*/true, shotTime);
                return;
            }
            if (avgDelta < -FLOW_DEVIATION_THRESHOLD) {
                m_firedFlow = true;
                emitCue(QStringLiteral("flow-slow"),
                        tr_("liveCoach.cue.flowSlow", "Running slow — tight puck"),
                        QStringLiteral("info"), /*speak=*/true, shotTime);
                return;
            }
        }
    }

    // --- Detector 4 (positive): on pace to target weight. ---
    // From scale flow rate + scale weight vs target. Fires once, only when the
    // scale is actually flowing and we're not basically done.
    if (!m_firedOnPace && shotTime > POUR_SETTLE_SEC
        && phase == MachineState::Phase::Pouring) {
        const double target = m_machineState->targetWeight();
        const double weight = m_machineState->scaleWeight();
        const double flowGps = m_machineState->scaleFlowRate();
        const double remaining = target - weight;
        if (target > 0.0 && flowGps > ONPACE_MIN_FLOW_GPS
            && remaining > ONPACE_MIN_REMAINING_G) {
            const int secsToGo = static_cast<int>(std::round(remaining / flowGps));
            if (secsToGo >= 1 && secsToGo <= 30) {
                m_firedOnPace = true;
                const QString text =
                    tr_("liveCoach.cue.onPace", "On pace — about %1s to %2g")
                        .arg(secsToGo)
                        .arg(QString::number(target, 'f', 0));
                emitCue(QStringLiteral("on-pace"), text,
                        QStringLiteral("positive"), /*speak=*/false, shotTime);
                return;
            }
        }
    }
}

void LiveShotCoach::emitCue(const QString& id, const QString& text,
                            const QString& severity, bool speak, double shotTime) {
    // Dedupe: don't re-publish a cue that is already the active one.
    if (m_cueActive && id == m_activeCueId)
        return;

    // Spoken-cue spacing: gate voice on shot-time elapsed since the last spoken
    // cue (clock-based, not a guard Timer). Visual cue still shows.
    bool willSpeak = speak;
    if (willSpeak && (shotTime - m_lastSpokenShotTime) < MIN_SPOKEN_SPACING_SEC)
        willSpeak = false;
    if (willSpeak)
        m_lastSpokenShotTime = shotTime;

    m_activeCueId = id;
    m_cueText = text;
    m_cueSeverity = severity;
    m_cueSpeak = willSpeak;
    m_cueActive = true;
    emit cueChanged();
}

void LiveShotCoach::clearCue() {
    if (!m_cueActive && m_cueText.isEmpty())
        return;
    m_cueActive = false;
    m_cueSpeak = false;
    m_cueText.clear();
    m_cueSeverity.clear();
    m_activeCueId.clear();
    emit cueChanged();
}

void LiveShotCoach::resetState() {
    m_window.clear();
    m_peakPressure = 0.0;
    m_firedNoPuck = false;
    m_firedChanneling = false;
    m_firedFlow = false;
    m_firedOnPace = false;
    m_firedPourStarted = false;
    m_lastSpokenShotTime = -1000.0;
    clearCue();
}
