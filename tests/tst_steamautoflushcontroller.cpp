#include <QtTest>
#include <QSignalSpy>

#include <memory>

#include "ble/de1device.h"
#include "controllers/steamautoflushcontroller.h"
#include "core/settings.h"
#include "machine/machinestate.h"

class tst_SteamAutoFlushController : public QObject
{
    Q_OBJECT

    struct Fixture {
        DE1Device device;
        Settings settings;
        MachineState machine{&device};
        std::unique_ptr<SteamAutoFlushController> countdown;
        qint64 nowMs = 10000;

        Fixture()
        {
            device.setSimulationMode(true);
            machine.setSettings(&settings);
            settings.brew()->setSteamAutoFlushSeconds(2);
            device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);

            // Construct only after the fixture is in a non-Puffing state, then
            // install the fake clock before any test can move the machine.
            countdown = std::make_unique<SteamAutoFlushController>(
                settings.brew(), &device, &machine);
            countdown->setClockForTesting([this] { return nowMs; });
        }

        void setSteamSubstate(DE1::SubState substate)
        {
            device.setSimulatedState(DE1::State::Steam, substate);
            QTRY_COMPARE(machine.phase(), MachineState::Phase::Steaming);
            countdown->evaluateForTesting();
        }

        void advance(qint64 milliseconds)
        {
            nowMs += milliseconds;
            countdown->evaluateForTesting();
        }
    };

private slots:
    void init() { QTest::failOnWarning(); }

    void puffingArmsAndExpiresOnce()
    {
        Fixture f;
        QSignalSpy expired(f.countdown.get(), &SteamAutoFlushController::expired);

        f.setSteamSubstate(DE1::SubState::Puffing);
        QCOMPARE(f.countdown->remainingSeconds(), 2.0);
        f.advance(1999);
        QVERIFY(f.countdown->remainingSeconds() > 0.0);
        QCOMPARE(expired.count(), 0);

        f.advance(1);
        QCOMPARE(f.countdown->remainingSeconds(), 0.0);
        QCOMPARE(expired.count(), 1);
        f.advance(1000);
        QCOMPARE(expired.count(), 1);
    }

    void leavingPuffingCancelsAndCannotStopLaterOperation()
    {
        Fixture f;
        QSignalSpy expired(f.countdown.get(), &SteamAutoFlushController::expired);

        f.setSteamSubstate(DE1::SubState::Puffing);
        QCOMPARE(f.countdown->remainingSeconds(), 2.0);
        f.setSteamSubstate(DE1::SubState::Ending);
        QCOMPARE(f.countdown->remainingSeconds(), 0.0);

        f.device.setSimulatedState(DE1::State::Espresso, DE1::SubState::Pouring);
        QTRY_COMPARE(f.machine.phase(), MachineState::Phase::Pouring);
        f.advance(3000);
        QCOMPARE(expired.count(), 0);
        QCOMPARE(f.countdown->remainingSeconds(), 0.0);
    }

    void disconnectCancelsCountdown()
    {
        Fixture f;
        QSignalSpy expired(f.countdown.get(), &SteamAutoFlushController::expired);

        f.setSteamSubstate(DE1::SubState::Puffing);
        QCOMPARE(f.countdown->remainingSeconds(), 2.0);

        f.device.setSimulationMode(false);
        f.countdown->evaluateForTesting();
        QCOMPARE(f.countdown->remainingSeconds(), 0.0);
        f.advance(3000);
        QCOMPARE(expired.count(), 0);
    }

    void nextPuffingTransitionArmsFreshCountdown()
    {
        Fixture f;
        QSignalSpy expired(f.countdown.get(), &SteamAutoFlushController::expired);

        f.setSteamSubstate(DE1::SubState::Puffing);
        f.advance(500);
        f.setSteamSubstate(DE1::SubState::Ending);
        QCOMPARE(f.countdown->remainingSeconds(), 0.0);

        f.setSteamSubstate(DE1::SubState::Puffing);
        QCOMPARE(f.countdown->remainingSeconds(), 2.0);
        f.advance(1999);
        QCOMPARE(expired.count(), 0);
        QVERIFY(f.countdown->remainingSeconds() > 0.0);
        f.advance(1);
        QCOMPARE(expired.count(), 1);
    }
};

QTEST_GUILESS_MAIN(tst_SteamAutoFlushController)
#include "tst_steamautoflushcontroller.moc"
