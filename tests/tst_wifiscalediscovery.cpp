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

    // Both selectors are PROCESS-WIDE, so a test that pins one and then fails an
    // assertion never reaches its own restore — QVERIFY returns from the function —
    // and every later test in this binary inherits the pin. That turns one red
    // assertion into a cascade of unrelated failures pointing at the wrong code.
    // cleanup() runs after each test whatever its outcome, which is the only
    // placement an early return cannot skip.
    void cleanup() {
        MdnsResolver::setHostnameResolver(MdnsResolver::HostnameResolver::Auto);
        MdnsResolver::setBrowseBackend(MdnsResolver::BrowseBackend::Auto);
    }

    // The default has to stay what each platform SHIPS. Making the resolver
    // runtime-selectable is only safe if `auto` still means mjansson on Android
    // (its getaddrinfo returns NXDOMAIN for ".local", so the direct query is not
    // a preference there, it is the only thing that works) and the system
    // resolver everywhere else. A default flipped by accident would be invisible
    // on a desktop and fatal on a device.
    void hostnameResolverAutoIsWhatThisPlatformShips() {
        MdnsResolver::setHostnameResolver(MdnsResolver::HostnameResolver::Auto);
#if defined(Q_OS_ANDROID)
        QVERIFY(MdnsResolver::useDirectHostnameResolver());
        QCOMPARE(MdnsResolver::activeHostnameResolverName(), QStringLiteral("mjansson"));
#else
        QVERIFY(!MdnsResolver::useDirectHostnameResolver());
        QCOMPARE(MdnsResolver::activeHostnameResolverName(), QStringLiteral("system"));
#endif
    }

    // Same contract as activeBrowseBackendName(): report what RAN, not what was
    // asked for. Requesting a resolver that is not compiled here yields the
    // substitute, and reporting the request instead would let a comparison of one
    // implementation against itself read as agreement between two.
    void hostnameResolverReportsWhatActuallyRan() {
        MdnsResolver::setHostnameResolver(MdnsResolver::HostnameResolver::System);
        QVERIFY(!MdnsResolver::useDirectHostnameResolver());
        QCOMPARE(MdnsResolver::activeHostnameResolverName(), QStringLiteral("system"));

        MdnsResolver::setHostnameResolver(MdnsResolver::HostnameResolver::Mjansson);
#ifdef Q_OS_IOS
        // Not compiled on iOS — the request must degrade, and say so.
        QVERIFY(!MdnsResolver::useDirectHostnameResolver());
        QCOMPARE(MdnsResolver::activeHostnameResolverName(), QStringLiteral("system"));
#else
        QVERIFY(MdnsResolver::useDirectHostnameResolver());
        QCOMPARE(MdnsResolver::activeHostnameResolverName(), QStringLiteral("mjansson"));
#endif

        // The stored request survives independently of what it resolves to, so a
        // caller can read back what it set.
        QCOMPARE(MdnsResolver::hostnameResolver(), MdnsResolver::HostnameResolver::Mjansson);
    }

    // The selector has to reach the LOOKUP, not just the accessor. Pinning System
    // and resolving a name only a unicast resolver can answer proves probe()
    // routes on it: on Android, where `auto` is mjansson, a probe that ignored the
    // selector would send an mDNS query for "localhost" and find nothing.
    //
    // The mirror case — pinning Mjansson and asserting localhost does NOT resolve —
    // is deliberately absent. The direct path needs a multicast socket, a CI
    // sandbox may refuse one, and that refusal is logged with qWarning, which
    // init()'s failOnWarning() turns into a failure indistinguishable from the
    // defect. Same reason the browse itself is not covered here.
    void probeHonoursAPinnedSystemResolver() {
        MdnsResolver::setHostnameResolver(MdnsResolver::HostnameResolver::System);

        WifiScaleDiscovery disc;
        QSignalSpy foundSpy(&disc, &WifiScaleDiscovery::resultFound);
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::probeFinished);

        disc.probe(QStringLiteral("localhost"), 3000);
        QVERIFY(doneSpy.wait(5000));
        QCOMPARE(foundSpy.count(), 1);
        QCOMPARE(foundSpy.last().at(0).value<WifiScaleResult>().hostname,
                 QStringLiteral("localhost"));
    }

    // macOS deliberately defaults to the backend it does NOT prefer, so this is
    // pinned: Bonjour is faster there and is what an Apple platform would
    // normally pick, and the default is mjansson anyway because macOS is the
    // development platform and the shipped populations are Android and iOS. A
    // well-meaning "fix" restoring Bonjour would silently remove daily coverage
    // of the browse three platforms ship, and nothing else would notice.
    //
    // Also pinned: both backends stay reachable on macOS. The default moved, the
    // compile guards did not.
    void browseBackendAutoIsTheDevelopmentChoiceOnMac() {
        MdnsResolver::setBrowseBackend(MdnsResolver::BrowseBackend::Auto);
#if defined(Q_OS_IOS)
        QCOMPARE(MdnsResolver::activeBrowseBackendName(), QStringLiteral("bonjour"));
#else
        QCOMPARE(MdnsResolver::activeBrowseBackendName(), QStringLiteral("mjansson"));
#endif

#if defined(Q_OS_MACOS)
        // Still selectable — this is a default, not a compile-out.
        MdnsResolver::setBrowseBackend(MdnsResolver::BrowseBackend::Bonjour);
        QCOMPARE(MdnsResolver::activeBrowseBackendName(), QStringLiteral("bonjour"));
        MdnsResolver::setBrowseBackend(MdnsResolver::BrowseBackend::Mjansson);
        QCOMPARE(MdnsResolver::activeBrowseBackendName(), QStringLiteral("mjansson"));
#endif
    }

    // The two selectors are NOT symmetrical, and the asymmetry is load-bearing.
    // The mjansson BROWSE is what three shipped platforms use; the mjansson
    // RESOLVER is Android-only, and QHostInfo is what iOS ships. Flipping the
    // resolver default on macOS too would leave both iOS paths with no dev
    // coverage at once.
    void hostnameResolverDefaultDoesNotFollowTheBrowseBackend() {
        MdnsResolver::setBrowseBackend(MdnsResolver::BrowseBackend::Auto);
        MdnsResolver::setHostnameResolver(MdnsResolver::HostnameResolver::Auto);
#if defined(Q_OS_MACOS)
        QCOMPARE(MdnsResolver::activeBrowseBackendName(), QStringLiteral("mjansson"));
        QCOMPARE(MdnsResolver::activeHostnameResolverName(), QStringLiteral("system"));
#endif
    }

    // The query source port decides whether the responder answers by multicast or
    // has to unicast back to this specific host, so the DEFAULT is behaviour, not
    // preference — and on ANDROID the default must be EPHEMERAL.
    //
    // This is the invariant that shipped broken. Binding 5353 on Android loses
    // every inbound packet to the system mDNS daemon that already owns the port:
    // measured on-device, records=0 for EVERY host, including the MQTT broker's
    // ".local" name, while an ephemeral socket resolved normally. A prior comment
    // recording exactly that was deleted on the theory it was an artifact of a
    // missing MulticastLock; the lock was later held and 5353 was still blind.
    //
    // So this asserts the platform split directly. A future "simplification" that
    // makes Auto mean 5353 everywhere would break every .local lookup on the
    // platform with the most users, and nothing else in the suite would notice.
    void queryPortAutoAvoids5353OnAndroid() {
        MdnsResolver::setQueryPort(MdnsResolver::QueryPort::Auto);
        QCOMPARE(MdnsResolver::queryPort(), MdnsResolver::QueryPort::Auto);
        QCOMPARE(MdnsResolver::queryPortName(), QStringLiteral("auto"));

        // The platform split itself. openQuerySocket() branches on this exact
        // predicate, so the assertion cannot drift from the behaviour.
#if defined(Q_OS_ANDROID)
        QVERIFY(!MdnsResolver::queryPortUsesMdnsPort());
#else
        QVERIFY(MdnsResolver::queryPortUsesMdnsPort());
#endif

        // The explicit overrides still reach the socket decision on every
        // platform — they are how the A/B that found this gets run at all.
        MdnsResolver::setQueryPort(MdnsResolver::QueryPort::Mdns);
        QVERIFY(MdnsResolver::queryPortUsesMdnsPort());
        MdnsResolver::setQueryPort(MdnsResolver::QueryPort::Ephemeral);
        QVERIFY(!MdnsResolver::queryPortUsesMdnsPort());
        MdnsResolver::setQueryPort(MdnsResolver::QueryPort::Auto);
    }

    // The whole point of the explicit values is settling an A/B, so a forced
    // policy has to be readable back — a comparison that silently ran the same
    // policy twice is worse than not running it.
    void queryPortReportsWhatWasRequested_data() {
        QTest::addColumn<int>("port");
        QTest::addColumn<QString>("name");
        QTest::newRow("auto") << int(MdnsResolver::QueryPort::Auto) << QStringLiteral("auto");
        QTest::newRow("mdns") << int(MdnsResolver::QueryPort::Mdns) << QStringLiteral("mdns");
        QTest::newRow("ephemeral")
            << int(MdnsResolver::QueryPort::Ephemeral) << QStringLiteral("ephemeral");
    }

    void queryPortReportsWhatWasRequested() {
        QFETCH(int, port);
        QFETCH(QString, name);
        MdnsResolver::setQueryPort(static_cast<MdnsResolver::QueryPort>(port));
        QCOMPARE(MdnsResolver::queryPortName(), name);
        MdnsResolver::setQueryPort(MdnsResolver::QueryPort::Auto);
    }

    // The NsdManager browse only ever runs on a tablet, so its parser is the one
    // piece of that path that can be held to account anywhere. What is being
    // guarded is field ALIGNMENT: `host` is legitimately empty below API 36, and a
    // split that dropped empty parts would slide the IPv4 address into the
    // hostname slot. That failure is silent — the row is stored under an identity
    // key of "192.168.10.145", never matches the saved "wifi:hds.local" primary,
    // and looks like a second scale rather than like a bug.
    void nsdLineParsesFieldsPositionally_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("instanceName");
        QTest::addColumn<QString>("hostname");
        QTest::addColumn<QString>("address");
        QTest::addColumn<int>("port");
        QTest::addColumn<QString>("firmware");

        QTest::newRow("resolved")
            << QStringLiteral("Half Decent Scale (hdstest)\thdstest.local\t192.168.10.242\t80\tfw=3.1.13|name=hdstest")
            << true << QStringLiteral("Half Decent Scale (hdstest)")
            << QStringLiteral("hdstest.local") << QStringLiteral("192.168.10.242")
            << 80 << QStringLiteral("3.1.13");
        // Below API 36 Android exposes no SRV target. The address must stay in the
        // address field regardless.
        QTest::newRow("no hostname")
            << QStringLiteral("Half Decent Scale\t\t192.168.10.145\t80\tfw=3.1.13")
            << true << QStringLiteral("Half Decent Scale")
            << QString() << QStringLiteral("192.168.10.145")
            << 80 << QStringLiteral("3.1.13");
        // A scale that publishes no TXT at all (firmware 3.1.12 does this) still
        // has a trailing empty field, so the line is complete and usable.
        QTest::newRow("no txt")
            << QStringLiteral("Half Decent Scale\ths.local\t192.168.10.145\t80\t")
            << true << QStringLiteral("Half Decent Scale")
            << QStringLiteral("hs.local") << QStringLiteral("192.168.10.145")
            << 80 << QString();
        QTest::newRow("truncated")
            << QStringLiteral("Half Decent Scale\ths.local\t192.168.10.145")
            << false << QString() << QString() << QString() << 0 << QString();
        // No address means the instance never resolved, whatever else it carries —
        // the other fields still parse, they just do not add up to a reachable
        // scale, which is what `valid` says and nothing else does.
        QTest::newRow("no address")
            << QStringLiteral("Half Decent Scale\ths.local\t\t80\tfw=3.1.13")
            << false << QStringLiteral("Half Decent Scale")
            << QStringLiteral("hs.local") << QString() << 80 << QStringLiteral("3.1.13");
    }

    void nsdLineParsesFieldsPositionally() {
        QFETCH(QString, line);
        QFETCH(bool, valid);
        QFETCH(QString, instanceName);
        QFETCH(QString, hostname);
        QFETCH(QString, address);
        QFETCH(int, port);
        QFETCH(QString, firmware);

        const WifiScaleDiscovery::NsdLine p = WifiScaleDiscovery::parseNsdLine(line);
        QVERIFY(!p.startFailure);
        QCOMPARE(p.valid, valid);
        QCOMPARE(p.instanceName, instanceName);
        QCOMPARE(p.hostname, hostname);
        QCOMPARE(p.address, address);
        QCOMPARE(static_cast<int>(p.port), port);
        QCOMPARE(p.txt.value(QStringLiteral("fw")), firmware);
    }

    // "Discovery never started" and "discovery ran and nothing answered" show a
    // user the same empty list and have completely different fixes, so the
    // sentinel must never be mistaken for a truncated instance line.
    void nsdStartFailureIsNotAnInstance() {
        const WifiScaleDiscovery::NsdLine p =
            WifiScaleDiscovery::parseNsdLine(QStringLiteral("!fail\t3"));
        QVERIFY(p.startFailure);
        QVERIFY(!p.valid);
        QCOMPARE(p.failureCode, QStringLiteral("3"));
        QVERIFY(p.address.isEmpty());
    }

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

    // browseFinished() is TERMINAL: callers stop waiting when it arrives, so it
    // must be emitted exactly once per browse. A browse can have two independent
    // workers in flight (mjansson, plus NsdManager on Android) finishing in either
    // order, and stopBrowse() can end the browse while both are still running —
    // three chances to emit twice, or to emit and then keep reporting results.
    //
    // Only one path is compiled here, so this covers the stopBrowse() race rather
    // than the two-path ordering; the counter it exercises is the same one. It
    // fails if stopBrowse() stops clearing m_browsePathsOutstanding, or if a
    // worker's completion stops checking the generation.
    void stopBrowseEmitsExactlyOneBrowseFinished() {
        WifiScaleDiscovery disc;
        QSignalSpy doneSpy(&disc, &WifiScaleDiscovery::browseFinished);

        // Short deadline on purpose: browse() does REAL multicast I/O, and this
        // test stops it immediately anyway. An earlier 3000 ms version added ~25 s
        // to the suite and put network-sensitive tests elsewhere under contention.
        disc.browse(600);
        QVERIFY(disc.isBrowsing());

        disc.stopBrowse();
        QCOMPARE(doneSpy.count(), 1);
        QCOMPARE(doneSpy.takeFirst().at(0).toBool(), false);  // did not run to completion
        QVERIFY(!disc.isBrowsing());

        // The worker is still winding down and will post its own completion. It
        // must be swallowed by the generation check rather than emitting a second
        // terminal signal — the defect this guards is silent, because a caller
        // that already moved on simply gets a stray signal later.
        QTest::qWait(900);
        QCOMPARE(doneSpy.count(), 0);
        QVERIFY(!disc.isBrowsing());

        // And a second stopBrowse() on an already-stopped browse stays silent.
        disc.stopBrowse();
        QCOMPARE(doneSpy.count(), 0);
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

    // The browse's SUCCESS path used to leave the reconnect machinery armed
    // forever. It dials the primary while the WiFi->BLE fallback scan it raced
    // is still running, so the recovery connect arrives with the fallback flag
    // set and looks exactly like a fallback — and the old rule (clear unless
    // this was a fallback connect) therefore did not clear. Nothing cleared it
    // afterwards while that scale stayed connected, so every later browse hit
    // saw a failed direct attempt that had in fact succeeded.
    //
    // This asserts the RULE, not the lifecycle: whether connectedScaleIsWifiPrimary()
    // actually reports true on that path depends on live objects and is not
    // reachable from here (no test links blemanager.cpp).
    void primaryConnectClearsTheFlagEvenDuringAFallback() {
        // The regression. Fallback active AND the primary is what connected:
        // must clear. Returned false before the fix.
        QVERIFY(BLEManager::connectClearsDirectAttemptFailed(true, true));
        // A genuine backup connect during a fallback: must NOT clear, or the
        // browse is disarmed exactly when it is needed.
        QVERIFY(!BLEManager::connectClearsDirectAttemptFailed(true, false));
        // No fallback in play — an ordinary connect always clears.
        QVERIFY(BLEManager::connectClearsDirectAttemptFailed(false, false));
        QVERIFY(BLEManager::connectClearsDirectAttemptFailed(false, true));
    }
};

QTEST_GUILESS_MAIN(tst_WifiScaleDiscovery)
#include "tst_wifiscalediscovery.moc"
