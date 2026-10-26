#include <QtTest>
#include <cmath>
#include <QSignalSpy>
#include "core/preparationstate.h"

class TestPreparation final : public PreparationState {
public:
    using PreparationState::applySnapshot;
    bool selectMode(const QString&) override { return false; }
    bool startMilkAttempt() override { return false; }
    bool cancelMilkAttempt() override { return false; }
    bool setMilkTimeLocked(bool) override { return false; }
};

class tst_PreparationState : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void strictSnapshot()
    {
        PreparationSnapshot state;
        state.mode = QStringLiteral("steam");
        state.milk.active = true;
        state.milk.cupWeight = 160;
        state.milk.maxNet = 1500;
        state.sessionMeasuredMilkG = 250;
        state.lastOnScaleMilkG = 130;
        state.steamTimeoutScaled = true;
        PreparationSnapshot parsed;
        QString error;
        QVERIFY(PreparationSnapshot::parse(state.toJson(), &parsed, &error));
        QCOMPARE(parsed.toJson(), state.toJson());
        const auto valid = state.toJson();
        for (auto it = valid.begin(); it != valid.end(); ++it) {
            auto bad = valid;
            bad.remove(it.key());
            QVERIFY(!PreparationSnapshot::parse(bad, &parsed, &error));
            QCOMPARE(parsed.toJson(), valid);
        }
        for (const auto& mode : {QStringLiteral("recipes"), QStringLiteral(""), QStringLiteral("Steam")}) {
            auto bad = valid;
            bad.insert(QStringLiteral("mode"), mode);
            QVERIFY(!PreparationSnapshot::parse(bad, &parsed, &error));
        }
        auto bad = valid;
        bad.insert(QStringLiteral("milkCaptureSequence"), 0.5);
        QVERIFY(!PreparationSnapshot::parse(bad, &parsed, &error));
        bad = valid;
        bad.insert(QStringLiteral("lastOnScaleMilkG"), -1.0);
        QVERIFY(!PreparationSnapshot::parse(bad, &parsed, &error));
        QCOMPARE(error, QStringLiteral("Invalid preparation field lastOnScaleMilkG"));
        bad = valid;
        bad.insert(QStringLiteral("steamTimeoutScaled"), 1);
        QVERIFY(!PreparationSnapshot::parse(bad, &parsed, &error));
        bad = valid;
        auto milk = state.milk.toJson();
        milk.insert(QStringLiteral("active"), 1);
        bad.insert(QStringLiteral("milk"), milk);
        QVERIFY(!PreparationSnapshot::parse(bad, &parsed, &error));
        QCOMPARE(error, QStringLiteral("milk: Invalid capture field active"));
    }
    void observersNeverReplayCaptureOnReconnect()
    {
        TestPreparation model;
        QSignalSpy beans(&model, &PreparationState::beanCaptured);
        QSignalSpy milk(&model, &PreparationState::milkCaptured);
        PreparationSnapshot snapshot;
        snapshot.beanCaptureSequence = 5;
        snapshot.milkCaptureSequence = 3;
        snapshot.lastBeanGrams = 18;
        snapshot.lastMilkGrams = 200;
        snapshot.lastMilkSeconds = 35;
        model.applySnapshot(snapshot, true, false);
        QCOMPARE(beans.size(), 0);
        QCOMPARE(milk.size(), 0);
        ++snapshot.milkCaptureSequence;
        model.applySnapshot(snapshot, true, true);
        QCOMPARE(milk.size(), 1);
        QCOMPARE(milk.first().at(0).toDouble(), 200);
        model.applySnapshot(snapshot, true, true);
        QCOMPARE(milk.size(), 1);
        model.applySnapshot({}, false, false);
        ++snapshot.milkCaptureSequence;
        model.applySnapshot(snapshot, true, true);
        QCOMPARE(milk.size(), 1);
        ++snapshot.beanCaptureSequence;
        model.applySnapshot(snapshot, true, true);
        QCOMPARE(beans.size(), 1);
    }
    void milkReadoutUsesHostBaseline()
    {
        TestPreparation model;
        PreparationSnapshot snapshot;
        snapshot.milk.active = true;
        snapshot.milk.rawWeight = 405;
        snapshot.milk.virtualZero = -5;
        model.applySnapshot(snapshot, true, false);
        QVERIFY(std::isnan(model.milkScaleLoadG()));
        QCOMPARE(model.loadOnScaleG(405), 405);
        snapshot.milk.seeded = true;
        snapshot.milk.loadPresent = true;
        model.applySnapshot(snapshot, true, false);
        QCOMPARE(model.milkScaleLoadG(), 410);
        snapshot.milk.loadPresent = false;
        model.applySnapshot(snapshot, true, false);
        QCOMPARE(model.milkScaleLoadG(), 0);
        // #2058: the empty reading is learned while capture is disarmed too, so the
        // baseline holds without an active capture.
        snapshot.milk.active = false;
        snapshot.milk.loadPresent = true;
        model.applySnapshot(snapshot, true, false);
        QCOMPARE(model.milkScaleLoadG(), 410);
        QCOMPARE(model.loadOnScaleG(405), 410);
        snapshot.milk.seeded = false;
        model.applySnapshot(snapshot, true, false);
        QVERIFY(std::isnan(model.milkScaleLoadG()));
    }

    void sharedSteamSessionValuesApplyToAllObservers()
    {
        TestPreparation model;
        PreparationSnapshot snapshot;
        snapshot.lastOnScaleMilkG = 120.0;
        snapshot.steamTimeoutScaled = true;
        model.applySnapshot(snapshot, true, false);
        QCOMPARE(model.lastOnScaleMilkG(), 120.0);
        QVERIFY(model.steamTimeoutScaled());
    }
};
QTEST_GUILESS_MAIN(tst_PreparationState)
#include "tst_preparationstate.moc"
