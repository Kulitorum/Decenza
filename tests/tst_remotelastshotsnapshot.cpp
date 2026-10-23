#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "../src/network/remotelastshotsnapshot.h"

class tst_RemoteLastShotSnapshot : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void serializesEmptyAndFullSnapshotsWithIdentityAndRevision()
    {
        const auto empty = RemoteLastShotSnapshot::serialize(
            QStringLiteral("package-A"), QStringLiteral("host-A"), 4, std::nullopt);
        QCOMPARE(empty.httpStatus, 200);
        const QJsonObject emptyRoot = QJsonDocument::fromJson(empty.body).object();
        QCOMPARE(emptyRoot.value(QStringLiteral("type")).toString(), QStringLiteral("lastShotSnapshot"));
        QCOMPARE(emptyRoot.value(QStringLiteral("protocolVersion")).toInt(), 1);
        QCOMPARE(emptyRoot.value(QStringLiteral("packageId")).toString(), QStringLiteral("package-A"));
        QCOMPARE(emptyRoot.value(QStringLiteral("hostId")).toString(), QStringLiteral("host-A"));
        QCOMPARE(emptyRoot.value(QStringLiteral("revision")).toInteger(), qint64(4));
        QVERIFY(emptyRoot.value(QStringLiteral("shot")).isNull());

        ShotProjection shot;
        shot.id = 81;
        shot.profileName = QStringLiteral("Snapshot test");
        shot.durationSec = 27.5;
        shot.pressure = QVariantList{QVariantMap{{QStringLiteral("x"), 0.0},
                                                 {QStringLiteral("y"), 1.2}}};
        const auto full = RemoteLastShotSnapshot::serialize(
            QStringLiteral("package-A"), QStringLiteral("host-A"), 5, shot);
        QCOMPARE(full.httpStatus, 200);
        const QJsonObject fullRoot = QJsonDocument::fromJson(full.body).object();
        QCOMPARE(fullRoot.value(QStringLiteral("revision")).toInteger(), qint64(5));
        const QJsonObject fullShot = fullRoot.value(QStringLiteral("shot")).toObject();
        QCOMPARE(fullShot.value(QStringLiteral("id")).toInteger(), qint64(81));
        QCOMPARE(fullShot.value(QStringLiteral("profileName")).toString(), QStringLiteral("Snapshot test"));
        QCOMPARE(fullShot.value(QStringLiteral("durationSec")).toDouble(), 27.5);
        QCOMPARE(fullShot.value(QStringLiteral("pressure")).toArray().size(), 1);
    }

    void rejectsExcessiveCurveAndMarkerCountsWithoutTruncating()
    {
        ShotProjection tooManyPoints;
        tooManyPoints.id = 1;
        tooManyPoints.flow.reserve(RemoteLastShotSnapshot::MaximumEntriesPerSeries + 1);
        for (qsizetype i = 0; i <= RemoteLastShotSnapshot::MaximumEntriesPerSeries; ++i)
            tooManyPoints.flow.append(QVariantMap{{QStringLiteral("x"), i},
                                                  {QStringLiteral("y"), 1.0}});
        const auto curveResult = RemoteLastShotSnapshot::serialize(
            QStringLiteral("pkg"), QStringLiteral("host"), 7, tooManyPoints);
        QCOMPARE(curveResult.httpStatus, 413);
        QCOMPARE(QJsonDocument::fromJson(curveResult.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(), QStringLiteral("tooLarge"));

        ShotProjection tooManyMarkers;
        tooManyMarkers.id = 2;
        tooManyMarkers.phases.reserve(RemoteLastShotSnapshot::MaximumPhaseMarkers + 1);
        for (qsizetype i = 0; i <= RemoteLastShotSnapshot::MaximumPhaseMarkers; ++i)
            tooManyMarkers.phases.append(QVariantMap{{QStringLiteral("time"), i}});
        const auto markerResult = RemoteLastShotSnapshot::serialize(
            QStringLiteral("pkg"), QStringLiteral("host"), 8, tooManyMarkers);
        QCOMPARE(markerResult.httpStatus, 413);
    }

    void rejectsOversizedSerializedProjectionAndInvalidIdentity()
    {
        ShotProjection large;
        large.id = 9;
        large.debugLog = QString(RemoteLastShotSnapshot::MaximumResponseBytes, QLatin1Char('x'));
        const auto oversized = RemoteLastShotSnapshot::serialize(
            QStringLiteral("pkg"), QStringLiteral("host"), 1, large);
        QCOMPARE(oversized.httpStatus, 413);

        ShotProjection invalid;
        const auto unavailable = RemoteLastShotSnapshot::serialize(
            QStringLiteral("pkg"), QStringLiteral("host"), 1, invalid);
        QCOMPARE(unavailable.httpStatus, 500);
        QCOMPARE(QJsonDocument::fromJson(unavailable.body).object()
                     .value(QStringLiteral("error")).toObject()
                     .value(QStringLiteral("code")).toString(), QStringLiteral("unavailable"));
    }
};

QTEST_GUILESS_MAIN(tst_RemoteLastShotSnapshot)
#include "tst_remotelastshotsnapshot.moc"
