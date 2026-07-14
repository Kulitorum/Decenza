#include <QtTest>
#include <QSignalSpy>
#include <QVector>
#include <QRegularExpression>

#include "ble/scaledevice.h"
#include "machine/weightprocessor.h"
#include "mocks/MockScaleDevice.h"

// Regression coverage for the #1176 / #1185 root cause.
//
// The scale-feed stall detector (and SAW de-jitter) must be driven by sample
// *arrival*, not value *change*. ScaleDevice::weightChanged is deduped (it
// backs the `weight` Q_PROPERTY / QML / MQTT, which must not churn on a
// constant reading). Before the fix, WeightProcessor::processWeight was wired
// to weightChanged, so a healthy scale reporting a constant weight through the
// EspressoPreheating dwell looked identical to a dead feed → false SUSPECTED →
// CONFIRMED stall → mid-shot connection-priority backoff (scale bounce /
// ruined shot). The fix adds ScaleDevice::weightSampleReceived (unconditional)
// and points the processing pipeline at it.
//
// Uses an injectable fake clock — no real-time waits.
class tst_ScaleFeedLiveness : public QObject {
    Q_OBJECT

    qint64 m_clock = 1000000;  // arbitrary fake-clock origin (ms)

    void configureActiveShot(WeightProcessor& wp) {
        const QVector<double> none;
        wp.setWallClock([this] { return m_clock; });
        wp.configure(36.0, 1, none, {}, none, none, false, 0.38);
        wp.startExtraction();          // sets m_active=true, m_tareComplete=false,
                                       // m_lastWallClockMs=0, resets stall tracking
        wp.setTareComplete(true);      // must follow startExtraction()
    }

private slots:

    void init() { QTest::failOnWarning(); m_clock = 1000000; }

    // The fix contract, at the ScaleDevice chokepoint: every accepted sample
    // emits weightSampleReceived; weightChanged stays deduped.
    void sampleSignalFiresEvenWhenValueUnchanged() {
        MockScaleDevice scale;
        QSignalSpy sampleSpy(&scale, &ScaleDevice::weightSampleReceived);
        QSignalSpy changeSpy(&scale, &ScaleDevice::weightChanged);

        scale.mockSetWeight(0.0);   // equals default m_weight → no value change
        scale.mockSetWeight(0.0);
        scale.mockSetWeight(0.0);
        QCOMPARE(sampleSpy.count(), 3);   // liveness: every sample
        QCOMPARE(changeSpy.count(), 0);   // dedup: nothing changed

        scale.mockSetWeight(1.5);   // genuine change
        scale.mockSetWeight(1.5);   // unchanged again
        QCOMPARE(sampleSpy.count(), 5);
        QCOMPARE(changeSpy.count(), 1);
    }

    // Regression: a constant weight stream through a 15 s preheat-style dwell,
    // wired the FIXED way (weightSampleReceived → processWeight), must NOT trip
    // any scale-feed stall — the scale is alive, it just isn't changing.
    void constantWeightOnSampleSignalDoesNotStall() {
        MockScaleDevice scale;
        WeightProcessor wp;
        configureActiveShot(wp);
        QObject::connect(&scale, &ScaleDevice::weightSampleReceived,
                         &wp, &WeightProcessor::processWeight);

        QSignalSpy suspected(&wp, &WeightProcessor::scaleFeedStalled);
        QSignalSpy confirmed(&wp, &WeightProcessor::scaleFeedStallConfirmed);

        // 15 s: scale at 10 Hz reporting a dead-static 0.0 g, DE1 telemetry
        // tick (setCurrentFrame) at ~5 Hz — exactly the #1176 EspressoPreheating
        // window. kScaleStaleMs=2000, kScaleStallConfirmMs=6000.
        for (int i = 0; i < 150; ++i) {
            scale.mockSetWeight(0.0);
            if (i % 2 == 0) wp.setCurrentFrame(0);
            m_clock += 100;
        }

        QCOMPARE(suspected.count(), 0);
        QCOMPARE(confirmed.count(), 0);
    }

    // Contrast: the SAME constant feed wired the OLD way (deduped
    // weightChanged) MUST still confirm a false stall. This keeps the exact
    // #1176 failure reproducible so a future rewiring to weightChanged can't
    // silently regress.
    void constantWeightOnChangedSignalStillStalls() {
        MockScaleDevice scale;
        WeightProcessor wp;
        configureActiveShot(wp);
        QObject::connect(&scale, &ScaleDevice::weightChanged,
                         &wp, &WeightProcessor::processWeight);

        QSignalSpy confirmed(&wp, &WeightProcessor::scaleFeedStallConfirmed);

        // Prime m_lastWallClockMs with one changing reading (mirrors the real
        // first/tare sample before the static dwell). The cup then sits
        // dead-static at 0.2 g: deduped weightChanged drops every identical
        // sample, so processWeight is never called again and the DE1 tick
        // (setCurrentFrame) is the only stall evaluator — the #1176 failure.
        scale.mockSetWeight(0.2);

        m_clock += 2500;                       // > kScaleStaleMs (2000)
        scale.mockSetWeight(0.2);              // unchanged → no weightChanged
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("Scale feed stalled")));
        wp.setCurrentFrame(0);                 // SUSPECTED (false stall)
        QCOMPARE(confirmed.count(), 0);

        m_clock += 4000;                       // total 6500 > kScaleStallConfirmMs
        scale.mockSetWeight(0.2);              // still unchanged
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("CONFIRMED")));
        wp.setCurrentFrame(0);                 // CONFIRMED — the #1176 false stall
        QVERIFY2(confirmed.count() >= 1,
                 "deduped weightChanged wiring must still reproduce the #1176 "
                 "false stall — otherwise this regression test proves nothing");
    }
};

QTEST_GUILESS_MAIN(tst_ScaleFeedLiveness)
#include "tst_scalefeedliveness.moc"
