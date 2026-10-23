#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>

#include "network/remotesteamgraphsnapshot.h"

class tst_RemoteSteamGraphSnapshot final : public QObject
{
    Q_OBJECT
private:
    static RemoteSteamGraphSnapshot::Snapshot sample()
    {
        RemoteSteamGraphSnapshot::Snapshot snapshot;
        snapshot.packageId = QStringLiteral("decenza-package");
        snapshot.hostId = QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1");
        snapshot.revision = 12;
        snapshot.rawTime = 5.25;
        snapshot.pressure = {{0.0, 0.4}, {5.25, 1.1}};
        snapshot.flow = {{0.0, 0.0}, {5.25, 2.4}};
        snapshot.temperature = {{0.0, 130.0}, {5.25, 136.0}};
        snapshot.flowGoalPoints = {{0.0, 0.0}, {30.0, 5.0}};
        return snapshot;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void roundTripsCompleteSnapshot()
    {
        const auto source = sample();
        const auto result = RemoteSteamGraphSnapshot::serialize(source);
        QVERIFY(result.ok);
        QCOMPARE(result.httpStatus, 200);

        RemoteSteamGraphSnapshot::Snapshot parsed;
        QString error;
        QVERIFY(RemoteSteamGraphSnapshot::parse(result.body, source.packageId,
                                                source.hostId, source.revision,
                                                &parsed, &error));
        QCOMPARE(parsed.revision, source.revision);
        QCOMPARE(parsed.rawTime, source.rawTime);
        QCOMPARE(parsed.pressure, source.pressure);
        QCOMPARE(parsed.flow, source.flow);
        QCOMPARE(parsed.temperature, source.temperature);
        QCOMPARE(parsed.flowGoalPoints, source.flowGoalPoints);
    }

    void rejectsIdentityAndRevisionMismatch()
    {
        const auto source = sample();
        const QByteArray body = RemoteSteamGraphSnapshot::serialize(source).body;
        RemoteSteamGraphSnapshot::Snapshot parsed;
        QString error;
        QVERIFY(!RemoteSteamGraphSnapshot::parse(body, QStringLiteral("other-package"),
                                                 source.hostId, source.revision, &parsed, &error));
        QVERIFY(!RemoteSteamGraphSnapshot::parse(body, source.packageId,
                                                 QStringLiteral("other-host"), source.revision,
                                                 &parsed, &error));
        QVERIFY(!RemoteSteamGraphSnapshot::parse(body, source.packageId, source.hostId,
                                                 source.revision + 1, &parsed, &error));
    }

    void rejectsUnalignedCurvesAndWrongCount()
    {
        const auto source = sample();
        const QByteArray body = RemoteSteamGraphSnapshot::serialize(source).body;
        RemoteSteamGraphSnapshot::Snapshot parsed;
        QString error;
        QJsonObject root = QJsonDocument::fromJson(body).object();
        QJsonObject graph = root.value(QStringLiteral("steamGraph")).toObject();
        graph.insert(QStringLiteral("sampleCount"), 7);
        root.insert(QStringLiteral("steamGraph"), graph);
        QVERIFY(!RemoteSteamGraphSnapshot::parse(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                                 source.packageId, source.hostId,
                                                 source.revision, &parsed, &error));

        graph.insert(QStringLiteral("sampleCount"), 2);
        graph.insert(QStringLiteral("flow"), QJsonArray{QJsonArray{0.0, 0.0}});
        root.insert(QStringLiteral("steamGraph"), graph);
        QVERIFY(!RemoteSteamGraphSnapshot::parse(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                                 source.packageId, source.hostId,
                                                 source.revision, &parsed, &error));
    }

    void rejectsMalformedNumbersAndLimits()
    {
        auto source = sample();
        source.rawTime = std::numeric_limits<double>::infinity();
        QVERIFY(!RemoteSteamGraphSnapshot::serialize(source).ok);

        source = sample();
        source.pressure[1].setX(0.0);
        QVERIFY(!RemoteSteamGraphSnapshot::serialize(source).ok);

        source = sample();
        source.pressure.resize(RemoteSteamGraphSnapshot::MaximumSamples + 1);
        QCOMPARE(RemoteSteamGraphSnapshot::serialize(source).httpStatus, 413);

        source = sample();
        source.flowGoalPoints.resize(RemoteSteamGraphSnapshot::MaximumGoalPoints + 1);
        QCOMPARE(RemoteSteamGraphSnapshot::serialize(source).httpStatus, 413);
    }
};

QTEST_GUILESS_MAIN(tst_RemoteSteamGraphSnapshot)
#include "tst_remotesteamgraphsnapshot.moc"
