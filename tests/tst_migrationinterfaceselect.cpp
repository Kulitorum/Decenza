// Tests for DataMigrationClient::orderedSubnetCandidates — the pure
// interface-selection helper behind multi-homing-robust migration connects.
// Exercises the ordering logic directly (no sockets, no live interfaces), so it
// is deterministic across machines. See openspec change
// add-migration-multihoming-robustness.

#include <QtTest>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QNetworkReply>

#include "core/datamigrationclient.h"

using AddrPrefix = QPair<QHostAddress, int>;

class TestMigrationInterfaceSelect : public QObject {
    Q_OBJECT

private slots:
    // Project rule: every test fails on any unexpected qWarning (see
    // docs/CLAUDE_MD/TESTING.md "Handling Warnings").
    void init() { QTest::failOnWarning(); }

    // Single interface on the target's subnet -> exactly that one candidate.
    void singleHomed()
    {
        const QHostAddress target("192.168.10.163");
        const QList<AddrPrefix> local{{QHostAddress("192.168.10.183"), 24}};

        const auto candidates = DataMigrationClient::orderedSubnetCandidates(
            target, local, QHostAddress("192.168.10.183"));

        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first(), QHostAddress("192.168.10.183"));
    }

    // Two interfaces on the same subnet -> both are candidates, and the OS
    // preferred (default-route) source is placed first.
    void dualHomedSameSubnetPreferredFirst()
    {
        const QHostAddress target("192.168.10.163");
        const QList<AddrPrefix> local{
            {QHostAddress("192.168.10.183"), 24},   // e.g. Wi-Fi
            {QHostAddress("192.168.10.195"), 24},   // e.g. USB Ethernet (default route)
        };

        const auto candidates = DataMigrationClient::orderedSubnetCandidates(
            target, local, QHostAddress("192.168.10.195"));

        QCOMPARE(candidates.size(), 2);
        QCOMPARE(candidates.at(0), QHostAddress("192.168.10.195"));  // preferred moved to front
        QCOMPARE(candidates.at(1), QHostAddress("192.168.10.183"));
    }

    // Addresses off the target's subnet are excluded entirely.
    void offSubnetExcluded()
    {
        const QHostAddress target("192.168.10.163");
        const QList<AddrPrefix> local{
            {QHostAddress("192.168.10.183"), 24},   // on-subnet
            {QHostAddress("10.0.0.5"), 24},         // different subnet
            {QHostAddress("172.16.4.9"), 16},       // different subnet
        };

        const auto candidates = DataMigrationClient::orderedSubnetCandidates(
            target, local, QHostAddress());

        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first(), QHostAddress("192.168.10.183"));
    }

    // No local address on the target's subnet (routed target) -> empty list,
    // which tells the caller to connect unbound via the default route.
    void routedTargetYieldsNoCandidates()
    {
        const QHostAddress target("8.8.8.8");
        const QList<AddrPrefix> local{
            {QHostAddress("192.168.10.183"), 24},
            {QHostAddress("192.168.10.195"), 24},
        };

        const auto candidates = DataMigrationClient::orderedSubnetCandidates(
            target, local, QHostAddress());

        QVERIFY(candidates.isEmpty());
    }

    // A preferred address that is not itself a candidate must not be injected,
    // and must not disturb the surviving order.
    void preferredNotAmongCandidatesIgnored()
    {
        const QHostAddress target("192.168.10.163");
        const QList<AddrPrefix> local{
            {QHostAddress("192.168.10.183"), 24},
            {QHostAddress("192.168.10.195"), 24},
        };

        // Preferred is on a different subnet (stale/irrelevant) -> ignored.
        const auto candidates = DataMigrationClient::orderedSubnetCandidates(
            target, local, QHostAddress("10.0.0.5"));

        QCOMPARE(candidates.size(), 2);
        QCOMPARE(candidates.at(0), QHostAddress("192.168.10.183"));  // original order kept
        QCOMPARE(candidates.at(1), QHostAddress("192.168.10.195"));
    }

    // A zero/invalid prefix length is skipped rather than matching everything.
    void zeroPrefixSkipped()
    {
        const QHostAddress target("192.168.10.163");
        const QList<AddrPrefix> local{
            {QHostAddress("192.168.10.183"), 0},    // bogus prefix -> skip
            {QHostAddress("192.168.10.195"), 24},   // valid
        };

        const auto candidates = DataMigrationClient::orderedSubnetCandidates(
            target, local, QHostAddress());

        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first(), QHostAddress("192.168.10.195"));
    }

    // isTransientNetworkError drives BOTH the manifest retry decision and the
    // reachable-vs-unreachable classification, so its mapping is contractual:
    //  - EHOSTUNREACH (the reported bug) reaches Qt as UnknownNetworkError and
    //    MUST be treated as transient/unreachable.
    //  - ConnectionRefused/RemoteHostClosed mean the host answered -> NOT
    //    transient (must not be relabelled "unreachable" or retried).
    void transientErrorClassification_data()
    {
        QTest::addColumn<int>("error");
        QTest::addColumn<bool>("transient");

        QTest::newRow("unknown (EHOSTUNREACH)") << int(QNetworkReply::UnknownNetworkError) << true;
        QTest::newRow("host-not-found")         << int(QNetworkReply::HostNotFoundError) << true;
        QTest::newRow("timeout")                << int(QNetworkReply::TimeoutError) << true;
        QTest::newRow("temporary-failure")      << int(QNetworkReply::TemporaryNetworkFailureError) << true;
        QTest::newRow("session-failed")         << int(QNetworkReply::NetworkSessionFailedError) << true;

        QTest::newRow("connection-refused")     << int(QNetworkReply::ConnectionRefusedError) << false;
        QTest::newRow("remote-host-closed")     << int(QNetworkReply::RemoteHostClosedError) << false;
        QTest::newRow("content-not-found")      << int(QNetworkReply::ContentNotFoundError) << false;
        QTest::newRow("auth-required")          << int(QNetworkReply::AuthenticationRequiredError) << false;
        QTest::newRow("no-error")               << int(QNetworkReply::NoError) << false;
    }

    void transientErrorClassification()
    {
        QFETCH(int, error);
        QFETCH(bool, transient);
        QCOMPARE(DataMigrationClient::isTransientNetworkError(
                     static_cast<QNetworkReply::NetworkError>(error)),
                 transient);
    }
};

QTEST_GUILESS_MAIN(TestMigrationInterfaceSelect)
#include "tst_migrationinterfaceselect.moc"
