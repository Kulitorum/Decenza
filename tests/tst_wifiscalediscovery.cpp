#include <QtTest>
#include <QMetaMethod>
#include <QSignalSpy>

#include <algorithm>

#include "network/mdnsresolver.h"
#include "network/wifiscalediscovery.h"
// For the reconnect-browse gating predicates. Header-inline statics only — no
// BLEManager is constructed and blemanager.cpp is not linked.
#include "ble/blemanager.h"

// Exercises the on-demand mDNS probe. We can't reliably mock QHostInfo on all
// platforms, so we resolve a known-good name ("localhost") for the success
// path and a clearly-invalid name (a UUID-ish label that cannot exist on a
// real LAN) for the failure path. Both paths must complete within the probe
// timeout.
//
// Running a DNS-SD browse is deliberately NOT covered here: it needs real
// responders on a real LAN (see mdnsresolver.h). What IS covered is the browse
// logic that holds without packets — the join predicate that decides whether an
// instance becomes a row, and the add-only guarantee that keeps a row present
// across a mid-cycle withdrawal. TXT parsing and labelling live in
// WifiScaleResultUtil and are covered by tst_wifiscaleresult.
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

    // --- Add-only within a scan (design D4c) -------------------------------
    //
    // Two halves to the rule. An instance that never resolves must never become
    // a row, and a row the user can already see must not be retracted under
    // their finger by a withdrawal arriving mid-browse.

    void ghostInstanceIsNotARow_data() {
        QTest::addColumn<QByteArray>("srvTarget");
        QTest::addColumn<quint16>("port");
        QTest::addColumn<bool>("haveAddress");
        QTest::addColumn<bool>("expected");

        // The only shape that qualifies.
        QTest::newRow("srv + port + address")
            << QByteArray("hds.local") << quint16(80) << true << true;

        // A stale PTR with no SRV behind it — the commonest ghost. Observed at
        // half the instances on the reference network.
        QTest::newRow("no srv")
            << QByteArray() << quint16(0) << false << false;
        // SRV present but its target has no A record: the name is advertised,
        // the host is gone.
        QTest::newRow("srv but no address")
            << QByteArray("hds.local") << quint16(80) << false << false;
        // Port 0 is not a connectable endpoint even with an address, so it must
        // not pass — a row built from it would offer a connection that cannot
        // be made.
        QTest::newRow("zero port")
            << QByteArray("hds.local") << quint16(0) << true << false;
        // Guards the AND: two out of three is still a ghost.
        QTest::newRow("port and address but no srv target")
            << QByteArray() << quint16(80) << true << false;
    }

    void ghostInstanceIsNotARow() {
        QFETCH(QByteArray, srvTarget);
        QFETCH(quint16, port);
        QFETCH(bool, haveAddress);
        QFETCH(bool, expected);
        QCOMPARE(MdnsResolver::browseInstanceResolved(srvTarget, port, haveAddress), expected);
    }

    // A withdrawal must be incapable of removing a row, not merely ignored by
    // today's callers. The guarantee is structural: discovery emits no signal
    // that retracts a result, so there is nothing for a caller to act on. If
    // someone later adds one, this fails and they have to revisit D4c rather
    // than discovering the flicker on a device.
    void discoveryExposesNoResultRetraction() {
        const QMetaObject* mo = &WifiScaleDiscovery::staticMetaObject;
        QStringList signalNames;
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            if (m.methodType() == QMetaMethod::Signal)
                signalNames << QString::fromLatin1(m.name());
        }
        QCOMPARE(signalNames, QStringList({QStringLiteral("resultFound"),
                                           QStringLiteral("probeFinished"),
                                           QStringLiteral("browseFinished"),
                                           QStringLiteral("logMessage")}));
    }

    // The row set itself is add-only within a scan: re-seeing a scale updates
    // its entry in place and never drops another one. This is what keeps a row
    // present across a mid-cycle withdrawal — a later result for a DIFFERENT
    // scale cannot evict it.
    void repeatedResultsNeverShrinkTheRowSet() {
        QVector<WifiScaleResult> set;

        WifiScaleResult a;
        a.foundBy = WifiScaleResult::Source::Browse;
        a.hostname = QStringLiteral("hds.local");
        a.address = QStringLiteral("192.168.1.50");
        QVERIFY(WifiScaleResultUtil::upsertByHostname(set, a));

        WifiScaleResult b;
        b.foundBy = WifiScaleResult::Source::Browse;
        b.hostname = QStringLiteral("hdstest.local");
        b.address = QStringLiteral("192.168.1.51");
        QVERIFY(WifiScaleResultUtil::upsertByHostname(set, b));
        QCOMPARE(set.size(), 2);

        // Same scale again on a new DHCP address: updates in place, evicts
        // nothing.
        WifiScaleResult aMoved = a;
        aMoved.address = QStringLiteral("192.168.1.77");
        QVERIFY(WifiScaleResultUtil::upsertByHostname(set, aMoved));
        QCOMPARE(set.size(), 2);

        // An unresolved result is rejected outright rather than replacing a
        // good row with an unconnectable one.
        WifiScaleResult ghost;
        ghost.foundBy = WifiScaleResult::Source::Browse;
        ghost.hostname = QStringLiteral("hds.local");
        ghost.address.clear();
        QVERIFY(!WifiScaleResultUtil::upsertByHostname(set, ghost));
        QCOMPARE(set.size(), 2);

        const auto it = std::find_if(set.cbegin(), set.cend(), [](const WifiScaleResult& r) {
            return r.hostname == QStringLiteral("hds.local");
        });
        QVERIFY(it != set.cend());
        QCOMPARE(it->address, QStringLiteral("192.168.1.77"));
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

    // --- Reconnect browse gating (BLEManager, header-inline predicates) ---
    //
    // The reconnect ladder browses for a saved WiFi scale whose direct connect
    // has failed, because this responder answers a DNS-SD browse but not a bare
    // A-query for its hostname. These assert the two rules that decide whether
    // that browse runs and whether its result may be connected. BLEManager
    // cannot be constructed here (it owns the BLE stack), which is why both are
    // static predicates rather than behaviour reachable through the object.

    // The healthy path must stay silent: a saved WiFi scale that connects on its
    // cached IP never sets directAttemptFailed, so no multicast is generated.
    // This is the gate that keeps a background browse from running on every
    // reconnect tick forever.
    void reconnectBrowseOnlyAfterADirectAttemptFailed() {
        const QString saved = QStringLiteral("wifi:hds.local");
        QVERIFY(!BLEManager::shouldBrowseOnReconnect(saved, false, false));
        QVERIFY(BLEManager::shouldBrowseOnReconnect(saved, true, false));
    }

    // With no WiFi scale saved, the no-background-discovery rule is absolute —
    // a failed attempt on some other transport must not open a browse.
    void reconnectBrowseNeverRunsWithoutASavedWifiScale() {
        QVERIFY(!BLEManager::shouldBrowseOnReconnect(QString(), true, false));
        QVERIFY(!BLEManager::shouldBrowseOnReconnect(
            QStringLiteral("AA:BB:CC:DD:EE:FF"), true, false));
        QVERIFY(!BLEManager::shouldBrowseOnReconnect(
            QStringLiteral("usb:decent"), true, false));
    }

    // A ladder tick arriving while a browse is still open must not restart it:
    // browse() cancels the in-flight one, which would reset the very window the
    // browse needs in order to hear a reply.
    void reconnectBrowseDoesNotRestartAnOpenBrowse() {
        const QString saved = QStringLiteral("wifi:hds.local");
        QVERIFY(!BLEManager::shouldBrowseOnReconnect(saved, true, true));
    }

    // Anti-substitution: a browse sees every scale on the LAN, and only the
    // saved one may be auto-connected. A second scale on the network must never
    // be silently swapped in for the user's.
    void onlyTheSavedScaleIsAutoConnected() {
        const QString saved = QStringLiteral("wifi:hds.local");
        QVERIFY(BLEManager::browsedScaleIsSavedPrimary(QStringLiteral("hds.local"), saved));
        QVERIFY(!BLEManager::browsedScaleIsSavedPrimary(QStringLiteral("hds-2.local"), saved));
        QVERIFY(!BLEManager::browsedScaleIsSavedPrimary(QStringLiteral("hdstest.local"), saved));
        // Nothing saved, or nothing found: never a match.
        QVERIFY(!BLEManager::browsedScaleIsSavedPrimary(QStringLiteral("hds.local"), QString()));
        QVERIFY(!BLEManager::browsedScaleIsSavedPrimary(QString(), saved));
    }

    // mDNS names are case-insensitive, and the saved address is stored as typed.
    // A scale whose advertised name differs only in case is still the user's
    // scale and must match — the same rule the BLE saved-address path uses.
    void savedScaleMatchIsCaseInsensitive() {
        QVERIFY(BLEManager::browsedScaleIsSavedPrimary(
            QStringLiteral("HDS.local"), QStringLiteral("wifi:hds.local")));
        QVERIFY(BLEManager::browsedScaleIsSavedPrimary(
            QStringLiteral("hds.local"), QStringLiteral("WIFI:HDS.LOCAL")));
    }
};

QTEST_GUILESS_MAIN(tst_WifiScaleDiscovery)
#include "tst_wifiscalediscovery.moc"
