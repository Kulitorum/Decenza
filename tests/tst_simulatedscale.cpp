#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ble/de1device.h"
#include "controllers/shottimingcontroller.h"
#include "core/settings.h"
#include "machine/machinestate.h"
#include "profile/profile.h"
#include "simulator/de1simulator.h"
#include "simulator/simulatedscale.h"

// End-to-end cover for the simulator's scale channel: DE1Simulator emits a
// weight, SimulatedScale publishes it like any ScaleDevice, and MachineState
// (which every UI surface and MCP tool reads through) reports it.
//
// Regression guard for the "flow is live but weight reads 0.0 g" bug: the
// espresso readout and CupFillView both bind MachineState.scaleWeight, so a
// break anywhere in that chain shows as a permanently empty cup even though
// the shot is clearly extracting.
// Minimal concrete ScaleDevice for the disconnect-tier branch. SimulatedScale
// is the only concrete driver reachable from a test, and both members under test
// are protected, so a probe subclass is the least-machinery way in.
class DisconnectTierProbe : public SimulatedScale {
public:
    using SimulatedScale::setConnected;
    void markExpected() { markExpectedDisconnect(); }
};

// Captures messages so a test can ASSERT on their tier.
//
// QTest::ignoreMessage is a PERMISSION, not an assertion. An unmatched pattern is
// reported by printUnhandledIgnoreMessages() by calling addMessage() with
// QAbstractTestLogger::Info (qtbase/src/testlib/qtestlog.cpp:397-419) — a printed
// line, never a failure. So disconnect tests built on ignoreMessage alone still
// pass if the line under test is demoted a tier or deleted outright, which is
// exactly the regression they exist to catch: this whole change is about WHICH
// TIER a disconnect is reported at, and the one thing ignoreMessage cannot check
// is that.
//
// Deliberately does NOT chain to the previous handler. init() calls
// QTest::failOnWarning(), and here the WARN branch is a thing being asserted
// rather than a thing that went wrong.
class CapturedMessages {
public:
    struct Entry { QtMsgType type = QtDebugMsg; QString text; };

    CapturedMessages() {
        s_entries = &m_entries;
        m_previous = qInstallMessageHandler(&CapturedMessages::handler);
    }
    ~CapturedMessages() {
        qInstallMessageHandler(m_previous);
        s_entries = nullptr;
    }
    Q_DISABLE_COPY_MOVE(CapturedMessages)

    void clear() { m_entries.clear(); }

    // The one message containing `needle`, or false. Insists on EXACTLY one:
    // zero means the line vanished, more than one means the event was announced
    // twice, and a test that accepted either would not notice.
    bool single(const QString& needle, Entry* out) const {
        Entry found;
        int matches = 0;
        for (const Entry& e : m_entries) {
            if (e.text.contains(needle)) { found = e; ++matches; }
        }
        if (matches == 1 && out)
            *out = found;
        return matches == 1;
    }

private:
    static void handler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
        if (s_entries)
            s_entries->append({type, msg});
    }

    QList<Entry> m_entries;
    QtMessageHandler m_previous = nullptr;
    static inline QList<Entry>* s_entries = nullptr;
};

class tst_SimulatedScale : public QObject {
    Q_OBJECT

private:
    // Minimal two-frame espresso profile, enough for the simulator to run a shot.
    static Profile makeProfile() {
        QJsonObject obj;
        obj["title"] = "Sim Test";
        obj["author"] = "test";
        obj["beverage_type"] = "espresso";
        obj["version"] = "2";
        obj["legacy_profile_type"] = "settings_2c";
        obj["target_weight"] = 36.0;
        obj["target_volume"] = 0.0;
        obj["espresso_temperature"] = 93.0;
        obj["maximum_pressure"] = 12.0;
        obj["maximum_flow"] = 6.0;
        obj["minimum_pressure"] = 0.0;
        obj["number_of_preinfuse_frames"] = 1;

        QJsonObject preinfusion;
        preinfusion["name"] = "preinfusion";
        preinfusion["temperature"] = 93.0;
        preinfusion["sensor"] = "coffee";
        preinfusion["pump"] = "flow";
        preinfusion["transition"] = "fast";
        preinfusion["pressure"] = 1.0;
        preinfusion["flow"] = 4.0;
        preinfusion["seconds"] = 10.0;
        preinfusion["volume"] = 0.0;

        QJsonObject pour;
        pour["name"] = "pour";
        pour["temperature"] = 93.0;
        pour["sensor"] = "coffee";
        pour["pump"] = "pressure";
        pour["transition"] = "fast";
        pour["pressure"] = 9.0;
        pour["flow"] = 6.0;
        pour["seconds"] = 40.0;
        pour["volume"] = 0.0;

        obj["steps"] = QJsonArray{preinfusion, pour};
        return Profile::fromJson(QJsonDocument(obj));
    }

    // Mirrors the simulation-mode wiring in main.cpp.
    struct SimFixture {
        DE1Device device;
        Settings settings;
        DE1Simulator simulator;
        SimulatedScale scale;
        MachineState state{&device};
        ShotTimingController timing{&device};

        SimFixture() {
            device.setSimulationMode(true);
            device.setSimulator(&simulator);
            state.setSettings(&settings);
            state.setScale(&scale);
            timing.setScale(&scale);
            timing.setSettings(&settings);
            timing.setMachineState(&state);
            state.setTimingController(&timing);

            // MainController::onEspressoCycleStarted does exactly this.
            QObject::connect(&state, &MachineState::espressoCycleStarted, &timing, [this]() {
                timing.startShot();
                timing.tare();
            });

            QObject::connect(&simulator, &DE1Simulator::stateChanged, &device, [this]() {
                device.setSimulatedState(simulator.state(), simulator.subState());
            });
            QObject::connect(&simulator, &DE1Simulator::subStateChanged, &device, [this]() {
                device.setSimulatedState(simulator.state(), simulator.subState());
            });
            QObject::connect(&simulator, &DE1Simulator::scaleWeightChanged,
                             &scale, &SimulatedScale::setSimulatedWeight);

            simulator.setProfile(makeProfile());
            scale.simulateConnection();
        }
    };

private slots:
    void init() { QTest::failOnWarning(); }

    // A weight published by the simulator must be readable through the scale
    // itself AND through MachineState, which is what the UI and MCP read.
    void weightReachesMachineState() {
        SimFixture f;
        QVERIFY(f.scale.isConnected());

        f.scale.setSimulatedWeight(12.5);
        QCOMPARE(f.scale.weight(), 12.5);
        QCOMPARE(f.state.scaleWeight(), 12.5);

        f.scale.setSimulatedWeight(20.0);
        QCOMPARE(f.state.scaleWeight(), 20.0);
    }

    // A disconnect is a FAULT only when the link dropped on its own.
    //
    // Validated in both directions against real logs before being written this
    // way: a user's capture has eight disconnects, every one preceded by a
    // genuine "CONTROLLER ERROR: ConnectionError", where WARN is right. A
    // maintainer's capture has the opposite — "WebSocket disconnected (expected)
    // — scale power-off" followed immediately by the same line at WARN, i.e. a
    // deliberate DE1-sleep close reported as a fault. Neither a blanket WARN nor
    // a blanket demotion is correct, which is why the driver declares intent.
    void expectedDisconnectIsNarrativeNotAFault() {
        CapturedMessages log;
        DisconnectTierProbe probe;
        probe.setConnected(true);

        // Unmarked: the link went away by itself. WARN.
        log.clear();
        probe.setConnected(false);
        CapturedMessages::Entry drop;
        QVERIFY2(log.single(QStringLiteral("DISCONNECTED"), &drop),
                 "expected exactly one DISCONNECTED line");
        QCOMPARE(drop.type, QtWarningMsg);
        QVERIFY(drop.text.contains(QStringLiteral("[Scale][ScaleDevice]")));
        QVERIFY(!drop.text.contains(QStringLiteral("(expected)")));

        // Marked: we closed it. INFO, and it says so.
        probe.setConnected(true);
        probe.markExpected();
        log.clear();
        probe.setConnected(false);
        QVERIFY2(log.single(QStringLiteral("DISCONNECTED"), &drop),
                 "expected exactly one DISCONNECTED line");
        QCOMPARE(drop.type, QtInfoMsg);
        QVERIFY(drop.text.contains(QStringLiteral("(expected)")));
    }

    // The flag is ONE-SHOT. A stale flag would silently downgrade the next
    // genuine failure — turning the fix into a way to hide real faults.
    void expectedDisconnectDoesNotPersistToTheNextOne() {
        CapturedMessages log;
        DisconnectTierProbe probe;
        probe.setConnected(true);
        probe.markExpected();
        log.clear();
        probe.setConnected(false);
        CapturedMessages::Entry drop;
        QVERIFY(log.single(QStringLiteral("DISCONNECTED"), &drop));
        QCOMPARE(drop.type, QtInfoMsg);

        probe.setConnected(true);
        log.clear();
        probe.setConnected(false);
        QVERIFY(log.single(QStringLiteral("DISCONNECTED"), &drop));
        QCOMPARE(drop.type, QtWarningMsg);
        QVERIFY(!drop.text.contains(QStringLiteral("(expected)")));
    }

    // A mark orphaned by a reconnect must not survive it.
    //
    // This is the case the two tests above could NOT reach, and it shipped: the
    // WiFi driver set the flag while classifying a close, then the
    // hostname-fallback path returned WITHOUT calling setConnected(false) — the
    // only place the flag is consumed. It stayed set, so the next genuine drop
    // would have been reported as expected. The driver now records intent in a
    // local and hands it over immediately before the transition; this pins the
    // base class's half, that reconnecting discards a mark nothing used.
    void expectedDisconnectDoesNotSurviveAReconnect() {
        CapturedMessages log;
        DisconnectTierProbe probe;
        probe.setConnected(true);
        log.clear();
        probe.setConnected(false);
        CapturedMessages::Entry drop;
        QVERIFY(log.single(QStringLiteral("DISCONNECTED"), &drop));
        QCOMPARE(drop.type, QtWarningMsg);

        // Orphaned mark: nothing consumed it, and now we reconnect.
        probe.markExpected();
        probe.setConnected(true);

        // The next drop is genuine and must WARN, not inherit the stale mark.
        log.clear();
        probe.setConnected(false);
        QVERIFY(log.single(QStringLiteral("DISCONNECTED"), &drop));
        QCOMPARE(drop.type, QtWarningMsg);
        QVERIFY(!drop.text.contains(QStringLiteral("(expected)")));
    }

    // Every published sample must also go out on weightSampleReceived — that is
    // the liveness feed WeightProcessor (SAW, LSLR flow) listens to, and it is
    // emitted even when the value repeats.
    void everySampleIsPublished() {
        SimFixture f;
        QSignalSpy samples(&f.scale, &ScaleDevice::weightSampleReceived);

        f.scale.setSimulatedWeight(5.0);
        f.scale.setSimulatedWeight(5.0);   // unchanged repeat still counts
        f.scale.setSimulatedWeight(7.0);
        QCOMPARE(samples.count(), 3);
    }

    // Tare zeroes the reading and later samples stay relative to that zero.
    void tareRebasesSubsequentSamples() {
        SimFixture f;
        f.scale.setSimulatedWeight(30.0);
        f.scale.tare();
        QCOMPARE(f.state.scaleWeight(), 0.0);

        f.scale.setSimulatedWeight(35.0);
        QCOMPARE(f.state.scaleWeight(), 5.0);
    }

    // Drive an actual simulated shot and require the weight to arrive at
    // MachineState. This is the path the espresso screen and CupFillView read.
    void simulatedShotProducesWeight() {
        SimFixture f;
        f.simulator.wakeUp();
        f.simulator.startEspresso();

        // Preheat is 5 s, then the puck must saturate before the first drip.
        QTRY_VERIFY_WITH_TIMEOUT(f.state.scaleWeight() > 1.0, 25000);
        QVERIFY(f.scale.flowRate() >= 0.0);

        f.simulator.stop();
    }

    // An espresso that follows a hot-water pour (or a flush) must still report
    // weight: the simulator's yield accumulator and the scale's tare offset must
    // not be left holding the previous operation's numbers.
    void espressoAfterHotWaterStillReportsWeight() {
        SimFixture f;
        f.simulator.wakeUp();

        // The old "SAW-HotWater" hyphenated family collapsed into the registered
        // [SAW] marker with a [HotWater] source tag. Match the full new shape, not
        // a bare "HotWater" substring — that would also swallow an unrelated
        // warning from any other subsystem that mentions hot water, and
        // ignoreMessage silently passing the wrong line is indistinguishable from
        // it passing the right one.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(R"(\[SAW\]\[HotWater\] Tare not completed)"));
        f.simulator.startHotWater();
        QTRY_VERIFY_WITH_TIMEOUT(f.scale.weight() > 50.0, 8000);
        f.simulator.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!f.state.isFlowing(), 3000);

        f.simulator.startEspresso();
        QTRY_VERIFY_WITH_TIMEOUT(f.state.scaleWeight() > 1.0, 25000);
        f.simulator.stop();
    }

    // The app's tare and the simulator's per-operation reset are independent
    // events, so either can land first. When the tare lands first — capturing
    // the previous operation's yield as the zero — the reset must re-zero the
    // scale rather than push every later reading negative for the whole shot.
    void producerResetAfterTareRezeroes() {
        SimFixture f;
        f.scale.setSimulatedWeight(36.0);   // previous shot's yield still there
        f.scale.tare();                     // app tares against it -> offset 36
        QCOMPARE(f.state.scaleWeight(), 0.0);

        f.scale.setSimulatedWeight(0.0);    // simulator restarts its accumulator
        QCOMPARE(f.state.scaleWeight(), 0.0);

        f.scale.setSimulatedWeight(12.0);   // ...and the new shot pours
        QCOMPARE(f.state.scaleWeight(), 12.0);   // read -24.0 before the fix
    }

    // Each hot-water pour must start from zero rather than inheriting the yield
    // of whatever the simulator dispensed before it. Driven without MachineState
    // so the hot-water SAW path (and its pre-tare warning) stays out of the way.
    void hotWaterStartsFromZero() {
        DE1Simulator simulator;
        SimulatedScale scale;
        simulator.setProfile(makeProfile());
        scale.simulateConnection();
        QObject::connect(&simulator, &DE1Simulator::scaleWeightChanged,
                         &scale, &SimulatedScale::setSimulatedWeight);
        simulator.wakeUp();

        simulator.startHotWater();
        QTRY_VERIFY_WITH_TIMEOUT(scale.weight() > 20.0, 5000);
        simulator.stop();
        const double firstPourTotal = scale.weight();

        simulator.startHotWater();
        // The reset lands synchronously in startHotWater().
        QVERIFY(scale.weight() < firstPourTotal);
        QCOMPARE(scale.weight(), 0.0);
        simulator.stop();
    }
};

QTEST_MAIN(tst_SimulatedScale)
#include "tst_simulatedscale.moc"
