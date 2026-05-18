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
    if (connected == m_scaleConnected) return;
    m_scaleConnected = connected;
    // Each connect is a fresh probe opportunity (the streaming baseline must
    // be re-established). A backoff sets the app-run latch, so a post-backoff
    // reconnect will fail the latch gate and not re-probe.
    m_streamConfirmed = false;
    m_probedThisConnect = false;
    m_streamRun = 0;
    m_lastSampleMs = 0;
    if (!connected && m_probing)
        endProbe("scale disconnected");
}

void ScalePriorityProbe::onScaleWeight()
{
    const qint64 now = m_clock.elapsed();
    const qint64 gap = (m_lastSampleMs > 0) ? (now - m_lastSampleMs) : 0;
    m_lastSampleMs = now;

    if (m_probing) return;  // detection is via WeightProcessor while probing

    if (m_streamConfirmed) return;
    // A too-large gap means the feed is not yet healthily streaming — restart
    // the run so we only confirm on a genuine cadence-stable baseline.
    if (gap > kStreamMaxGapMs) { m_streamRun = 1; return; }
    if (++m_streamRun < kStreamConfirmSamples) return;

    m_streamConfirmed = true;
    maybeStartProbe();
}

void ScalePriorityProbe::onMachinePhaseChanged()
{
    // Yield-on-shot: an espresso cycle (or any non-idle phase) started while
    // probing → end immediately, do NOT trip the backoff ourselves; normal
    // preheat/extraction liveness governs from here.
    if (m_probing && !machineIdle())
        endProbe("machine left idle (shot started)");
}

void ScalePriorityProbe::maybeStartProbe()
{
    if (m_probing || m_probedThisConnect) return;
    if (probeDisabled()) return;
    if (!m_de1 || !m_de1->isConnected()) return;
    if (!m_scaleConnected || !m_streamConfirmed) return;
    if (!machineIdle()) return;
    // Latch already set this run (e.g. a prior backoff): nothing to provoke,
    // the scale is already at BALANCED and detection is disarmed.
    if (m_ble && m_ble->scaleSkipHighPriority()) return;

    startProbe();
}

void ScalePriorityProbe::startProbe()
{
    m_probing = true;
    m_probedThisConnect = true;
    m_latchedAtStart = m_ble && m_ble->scaleSkipHighPriority();
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
    if (!m_probing) return;

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
    if (!m_probing) return;
    m_probing = false;
    m_tickTimer.stop();

    if (m_wp) {
        WeightProcessor* wp = m_wp;
        QMetaObject::invokeMethod(wp, [wp]() { wp->setProbeActive(false); },
                                  Qt::QueuedConnection);
    }

    const bool provoked = m_ble && m_ble->scaleSkipHighPriority()
                          && !m_latchedAtStart;
    // WARN so the outcome is self-evidencing in one debug.log (D6).
    qWarning().noquote()
        << QStringLiteral("[BLE] Scale connection-priority PROBE ended "
                          "(provoked=%1) — %2")
               .arg(provoked ? "true" : "false")
               .arg(QString::fromUtf8(why));
}
