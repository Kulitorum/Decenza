#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

class DE1Device;
class MachineState;
class BLEManager;
class WeightProcessor;

// Startup connection-priority probe (the "early win", #1093/#1176).
//
// Strictly additive accelerator of the EXISTING scale-feed-liveness backoff:
// once a scale is confirmed correctly streaming weight (DE1 connected, machine
// idle), it runs a bounded burst of READ-ONLY DE1 traffic to provoke the
// dual-HIGH BLE contention while the liveness detector is armed — so a weak
// device is detected and backed off at idle, before any shot. It introduces
// NO new detection/backoff mechanism: it sets WeightProcessor::m_probeActive
// for the probe window and ticks WeightProcessor::pollScaleFeedLiveness() at
// the probe's own cadence (the DE1 shot-sample tick is silent at idle), so a
// provoked stall drives the same scaleFeedStalled → backoff → reconnect path.
//
// Safety (D5 invariants): the stressor is read-only by construction (see
// DE1Device::issueConnectionPriorityProbeReads — never writes/commands the
// machine); a no-op when it provokes nothing; runs at most once per scale
// connect; only while idle and only after streaming is confirmed; yields
// immediately if an espresso cycle starts; capable hardware sustains the read
// burst and never trips. Correctness never depends on it — the preheat /
// extraction liveness gate is the guaranteed net whether or not it provokes.
//
// Lives on the main thread (same as DE1Device / MachineState / BLEManager);
// WeightProcessor is on its worker thread so calls into it are queued.
class ScalePriorityProbe : public QObject {
    Q_OBJECT
public:
    ScalePriorityProbe(DE1Device* de1, MachineState* machine,
                       BLEManager* ble, WeightProcessor* wp,
                       QObject* parent = nullptr);
    // Guarantees an in-flight probe is ended (idempotent endProbe()) on every
    // teardown path, so m_probeActive cannot strand true on the worker.
    ~ScalePriorityProbe() override;

    // Non-UI kill-switch (cheap field insurance, D5 / open question). The
    // probe is disabled for the run if the env var is set at launch.
    static bool probeDisabled();

    // Probe lifecycle, per scale connect. An explicit state machine replaces
    // the prior bool soup so illegal combinations are unrepresentable (D7):
    //   Idle       — scale not connected; nothing to do.
    //   Confirming — connected; accumulating the streaming baseline.
    //   Armed      — baseline confirmed; waiting for the start gate
    //                (DE1 connected ∧ machine idle ∧ latch clear).
    //   Probing    — bounded read-only burst running.
    //   Done       — probe ran (or latch already set) this connect; no
    //                re-probe until the next (re)connect.
    enum class State { Idle, Confirming, Armed, Probing, Done };

public slots:
    // Wired in main.cpp to the active scale + machine. A scale-type change
    // builds a fresh scale object; re-wiring re-points these.
    void onScaleConnectionChanged(bool connected);
    void onScaleWeight();        // a streamed weight sample arrived
    void onMachinePhaseChanged(); // yield-on-shot / idle gating
    // Counted only while Probing — surfaced in the probe end-line so a single
    // debug.log attributes contention to the probe window (D6/D7-H2).
    void onDe1LinkFault(const QString& kind);

private:
    bool machineIdle() const;
    void maybeStartProbe();
    void startProbe();
    void endProbe(const char* why);
    void onProbeTick();

    DE1Device*       m_de1     = nullptr;
    MachineState*    m_machine = nullptr;
    BLEManager*      m_ble     = nullptr;
    WeightProcessor* m_wp      = nullptr;

    // Streaming-confirmed gate: N consecutive samples each within the healthy
    // gap establish the known-good baseline (never probe on bare connect).
    static constexpr int     kStreamConfirmSamples = 8;
    static constexpr qint64  kStreamMaxGapMs       = 1500;
    static constexpr int     kProbeTickMs          = 500;
    static constexpr qint64  kProbeDurationMs      = 6000;

    State        m_state           = State::Idle;
    int          m_streamRun       = 0;   // Confirming working set
    qint64       m_lastSampleMs    = 0;   // Confirming working set
    QElapsedTimer m_clock;                // free-running sample-gap clock
    bool         m_latchedAtStart  = false;  // Probing working set
    int          m_de1FaultsInWindow = 0;    // Probing working set
    QTimer       m_tickTimer;
    QElapsedTimer m_probeClock;           // per-probe stopwatch

#ifdef DECENZA_TESTING
public:
    State testState() const { return m_state; }
    int   testStreamRun() const { return m_streamRun; }
#endif
};
