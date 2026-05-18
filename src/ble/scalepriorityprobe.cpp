#include "scalepriorityprobe.h"

#include "de1device.h"
#include "blemanager.h"
#include "../machine/machinestate.h"
#include "../machine/weightprocessor.h"

#include <QDebug>

ScalePriorityProbe::ScalePriorityProbe(DE1Device* de1, MachineState* machine,
                                       BLEManager* ble, WeightProcessor* wp,
                                       QObject* parent)
    : QObject(parent), m_de1(de1), m_machine(machine), m_ble(ble), m_wp(wp)
{
    m_clock.start();
    m_tickTimer.setInterval(kProbeTickMs);
    // Genuinely periodic task (the probe's read cadence + liveness tick) —
    // not a guard/workaround timer (CLAUDE.md design rule).
    connect(&m_tickTimer, &QTimer::timeout, this, &ScalePriorityProbe::onProbeTick);
    // Attribute DE1 link faults observed during the probe window (D6/D7-H2).
    if (m_de1)
        connect(m_de1, &DE1Device::de1LinkFault,
                this, &ScalePriorityProbe::onDe1LinkFault);
}

ScalePriorityProbe::~ScalePriorityProbe()
{
    // endProbe() is idempotent (no-ops unless State::Probing) and enqueues the
    // setProbeActive(false) cleanup, so m_probeActive cannot strand true on
    // any teardown path. Note: at process exit the worker thread may already
    // be stopped, so a late queued lambda can be dropped — that is benign
    // (the process is exiting); the value here is the non-shutdown teardown
    // path and a self-documenting log line.
    endProbe("probe object destroyed");
}

bool ScalePriorityProbe::probeDisabled()
{
    return qEnvironmentVariableIsSet("DECENZA_DISABLE_SCALE_PRIORITY_PROBE");
}

bool ScalePriorityProbe::machineIdle() const
{
    if (!m_machine) return false;
    const auto p = m_machine->phase();
    return p == MachineState::Phase::Idle || p == MachineState::Phase::Ready;
}

void ScalePriorityProbe::onScaleConnectionChanged(bool connected)
{
    if (connected) {
        if (m_state != State::Idle) return;  // already tracking this connect
        // Fresh probe opportunity — the streaming baseline must be
        // re-established. A backoff sets the app-run latch, so a post-backoff
        // reconnect will fail the latch gate in maybeStartProbe().
        m_state = State::Confirming;
        m_streamRun = 0;
        m_lastSampleMs = 0;
    } else {
        if (m_state == State::Probing)
            endProbe("scale disconnected");
        m_state = State::Idle;
        m_streamRun = 0;
        m_lastSampleMs = 0;
    }
}

void ScalePriorityProbe::onScaleWeight()
{
    const qint64 now = m_clock.elapsed();
    const qint64 gap = (m_lastSampleMs > 0) ? (now - m_lastSampleMs) : 0;
    m_lastSampleMs = now;

    // Only the Confirming state counts samples. Probing detection is via
    // WeightProcessor; Armed/Done/Idle do not accumulate a baseline.
    if (m_state != State::Confirming) return;

    // A too-large gap means the feed is not yet healthily streaming — restart
    // the run so we only confirm on a genuine cadence-stable baseline.
    if (gap > kStreamMaxGapMs) { m_streamRun = 1; return; }
    if (++m_streamRun < kStreamConfirmSamples) return;

    m_state = State::Armed;
    maybeStartProbe();
}

void ScalePriorityProbe::onMachinePhaseChanged()
{
    // Yield-on-shot: an espresso cycle (or any non-idle phase) started while
    // probing → end immediately, do NOT trip the backoff ourselves; normal
    // preheat/extraction liveness governs from here.
    if (m_state == State::Probing && !machineIdle()) {
        endProbe("machine left idle (shot started)");
        return;
    }
    // Become-idle path: the baseline may have been confirmed (State::Armed)
    // while the machine was busy (e.g. scale connected during warm-up).
    // onScaleWeight() only reaches maybeStartProbe() on the single
    // confirm transition, so without this the probe would never run for that
    // whole connect. maybeStartProbe() is fully guarded + idempotent.
    if (m_state == State::Armed && machineIdle())
        maybeStartProbe();
}

void ScalePriorityProbe::onDe1LinkFault(const QString& /*kind*/)
{
    if (m_state == State::Probing) ++m_de1FaultsInWindow;
}

void ScalePriorityProbe::maybeStartProbe()
{
    if (m_state != State::Armed) return;
    if (probeDisabled()) return;
    if (!m_de1 || !m_de1->isConnected()) return;
    if (!machineIdle()) return;
    // Latch already set this run (e.g. a prior backoff): nothing to provoke,
    // the scale is already at BALANCED and detection is disarmed. Mark Done
    // so we don't keep re-evaluating this connect.
    if (m_ble && m_ble->scaleSkipHighPriority()) { m_state = State::Done; return; }

    startProbe();
}

void ScalePriorityProbe::startProbe()
{
    m_state = State::Probing;
    m_latchedAtStart = m_ble && m_ble->scaleSkipHighPriority();
    m_de1FaultsInWindow = 0;
    m_probeClock.start();

    // WARN so a single reporter debug.log shows the probe ran (D6).
    qWarning().noquote()
        << "[BLE] Scale connection-priority PROBE started — bounded read-only "
           "DE1 burst at idle to provoke dual-HIGH contention while the "
           "scale-feed-liveness detector is armed";

    if (m_wp) {
        WeightProcessor* wp = m_wp;
        QMetaObject::invokeMethod(wp, [wp]() { wp->setProbeActive(true); },
                                  Qt::QueuedConnection);
    }
    onProbeTick();          // first chunk immediately
    m_tickTimer.start();
}

void ScalePriorityProbe::onProbeTick()
{
    if (m_state != State::Probing) return;

    // A provoked stall trips WeightProcessor::scaleFeedStalled → the existing
    // backoff, which sets the app-run latch. Detect that and finish.
    if (m_ble && m_ble->scaleSkipHighPriority() && !m_latchedAtStart) {
        endProbe("provoked — backoff latch set");
        return;
    }
    if (m_de1 && !m_de1->isConnected()) { endProbe("DE1 disconnected"); return; }
    if (!machineIdle())                 { endProbe("machine left idle"); return; }
    if (m_probeClock.elapsed() >= kProbeDurationMs) {
        endProbe("duration elapsed — no provocation");
        return;
    }

    // Read-only DE1 burst (provokes contention) + tick the idle liveness
    // check on the worker (no DE1 shot-sample cadence at idle).
    if (m_de1) m_de1->issueConnectionPriorityProbeReads();
    if (m_wp) {
        WeightProcessor* wp = m_wp;
        QMetaObject::invokeMethod(wp, [wp]() { wp->pollScaleFeedLiveness(); },
                                  Qt::QueuedConnection);
    }
}

void ScalePriorityProbe::endProbe(const char* why)
{
    if (m_state != State::Probing) return;
    m_state = State::Done;          // no re-probe until the next (re)connect
    m_tickTimer.stop();

    if (m_wp) {
        WeightProcessor* wp = m_wp;
        QMetaObject::invokeMethod(wp, [wp]() { wp->setProbeActive(false); },
                                  Qt::QueuedConnection);
    }

    const bool provoked = m_ble && m_ble->scaleSkipHighPriority()
                          && !m_latchedAtStart;
    // WARN so the outcome is self-evidencing in one debug.log (D6). The DE1
    // link-fault count attributes any contention noise to the probe window
    // (D7-H2: DE1 errorOccurred has no UI surface, so this log line — not
    // error-channel suppression — is the correct resolution).
    qWarning().noquote()
        << QStringLiteral("[BLE] Scale connection-priority PROBE ended "
                          "(provoked=%1, de1LinkFaults=%2) — %3")
               .arg(provoked ? "true" : "false")
               .arg(m_de1FaultsInWindow)
               .arg(QString::fromUtf8(why));
}
