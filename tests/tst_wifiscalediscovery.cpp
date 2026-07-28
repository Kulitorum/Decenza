#include <QtTest>
#include <QSignalSpy>

#include "network/wifiscalediscovery.h"

// Exercises the on-demand mDNS probe. We can't reliably mock QHostInfo on all
// platforms, so we resolve a known-good name ("localhost") for the success
// path and a clearly-invalid name (a UUID-ish label that cannot exist on a
// real LAN) for the failure path. Both paths must complete within the probe
// timeout.
//
// The DNS-SD browse is deliberately NOT covered here: it needs real responders
// on a real LAN (see mdnsresolver.h). The logic that can be tested without packets lives in
// WifiScaleResultUtil and is covered by tst_wifiscaleresult.
class tst_WifiScaleDiscovery : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<WifiScaleResult>("WifiScaleResult"); }
    void init() { QTest::failOnWarning(); }

    // localhost resolves on every platform — exercises the success edge.
    void resolvedHostnameEmitsResultFound() {
        WifiScaleDiscovery disc;
        QSignalSpy foundSpy(&disc, &WifiScaleDiscovery::resultFound);
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        disc.probe(QStringLiteral("localhost"), 3000);
        QVERIFY(doneSpy.wait(5000));
        QCOMPARE(foundSpy.count(), 1);

        const auto result = foundSpy.last().at(0).value<WifiScaleResult>();
        QCOMPARE(result.hostname, QStringLiteral("localhost"));
        QVERIFY(!result.address.isEmpty());
        // An A-record hit carries no DNS-SD metadata, and must not pretend to.
        QCOMPARE(result.foundBy, WifiScaleResult::Source::Fallback);
        QVERIFY(result.instanceName.isEmpty());
    }

    // A nonexistent hostname must reach probeFinished without firing
    // resultFound, regardless of whether the OS resolver returns NotFound
    // quickly or we hit the timeout.
    void nonexistentHostnameEmitsProbeFinishedOnly() {
        WifiScaleDiscovery disc;
        QSignalSpy foundSpy(&disc, &WifiScaleDiscovery::resultFound);
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        // Use a label that's structurally valid but cannot exist on a normal LAN.
        disc.probe(QStringLiteral("decenza-test-no-such-host-XYZ.invalid"), 2000);
        QVERIFY(doneSpy.wait(5000));
        QCOMPARE(foundSpy.count(), 0);
        // It ran and found nothing — distinct from never having run.
        QCOMPARE(doneSpy.last().at(0).toBool(), true);
    }

    // "Ran and found nothing" and "never ran" must be distinguishable, so the
    // log can say which happened instead of conflating a silent network with a
    // skipped step.
    void emptyHostnameListReportsDidNotRun() {
        WifiScaleDiscovery disc;
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        disc.probe(QStringList{}, 2000);
        QCOMPARE(doneSpy.count(), 1);
        QCOMPARE(doneSpy.last().at(0).toBool(), false);
    }

    // Several names probed together must yield ONE completion, not one per
    // name — the caller uses it to clear a "scanning" state.
    void multipleHostnamesFinishOnce() {
        WifiScaleDiscovery disc;
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        disc.probe({QStringLiteral("localhost"),
                    QStringLiteral("decenza-test-no-such-host-A.invalid"),
                    QStringLiteral("decenza-test-no-such-host-B.invalid")}, 3000);
        QVERIFY(doneSpy.wait(6000));
        QTest::qWait(200);
        QCOMPARE(doneSpy.count(), 1);
    }

    // Two probe() calls in quick succession must NOT yield two completions
    // for the first — the older lookup is cancelled.
    void rapidProbesCancelInFlight() {
        WifiScaleDiscovery disc;
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        disc.probe(QStringLiteral("localhost"), 3000);
        disc.probe(QStringLiteral("localhost"), 3000);
        // Wait for at least one to finish, then a bit more to confirm the
        // first didn't also emit (which would yield count >= 2 quickly).
        QVERIFY(doneSpy.wait(5000));
        QTest::qWait(200);
        // Exactly one completion (the second probe). The first was cancelled.
        QCOMPARE(doneSpy.count(), 1);
    }

    // The fallback name list is a user-habit heuristic, not protocol. Pinning it
    // so a later reader doesn't quietly extend it believing firmware generates
    // these names — nothing does.
    void fallbackHostnamesAreTheThreeExpected() {
        const QStringList names = WifiScaleDiscovery::defaultFallbackHostnames();
        QCOMPARE(names, QStringList({QStringLiteral("hds.local"),
                                     QStringLiteral("hds-2.local"),
                                     QStringLiteral("hds-3.local")}));
    }
};

QTEST_GUILESS_MAIN(tst_WifiScaleDiscovery)
#include "tst_wifiscalediscovery.moc"
