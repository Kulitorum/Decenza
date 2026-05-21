#include <QtTest>
#include <QSignalSpy>

#include "network/wifiscalediscovery.h"

// Exercises the on-demand mDNS probe. We can't reliably mock QHostInfo on all
// platforms, so we resolve a known-good name ("localhost") for the success
// path and a clearly-invalid name (a UUID-ish label that cannot exist on a
// real LAN) for the failure path. Both paths must complete within the probe
// timeout.
class tst_WifiScaleDiscovery : public QObject {
    Q_OBJECT

private slots:
    // localhost resolves on every platform — exercises the success edge.
    void resolvedHostnameEmitsScaleFound() {
        WifiScaleDiscovery disc;
        QSignalSpy foundSpy(&disc, &WifiScaleDiscovery::scaleFound);
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        disc.probe(QStringLiteral("localhost"), 3000);
        QVERIFY(doneSpy.wait(5000));
        QCOMPARE(foundSpy.count(), 1);
        QCOMPARE(foundSpy.last().at(0).toString(), QStringLiteral("localhost"));
        QVERIFY(!foundSpy.last().at(1).toString().isEmpty());
    }

    // A nonexistent hostname must reach probeFinished without firing
    // scaleFound, regardless of whether the OS resolver returns NotFound
    // quickly or we hit the timeout.
    void nonexistentHostnameEmitsProbeFinishedOnly() {
        WifiScaleDiscovery disc;
        QSignalSpy foundSpy(&disc, &WifiScaleDiscovery::scaleFound);
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        // Use a label that's structurally valid but cannot exist on a normal LAN.
        disc.probe(QStringLiteral("decenza-test-no-such-host-XYZ.invalid"), 2000);
        QVERIFY(doneSpy.wait(5000));
        QCOMPARE(foundSpy.count(), 0);
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
};

QTEST_GUILESS_MAIN(tst_WifiScaleDiscovery)
#include "tst_wifiscalediscovery.moc"
