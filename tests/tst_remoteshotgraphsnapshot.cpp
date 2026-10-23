#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>

#include "network/remoteshotgraphsnapshot.h"

#include <algorithm>
#include <limits>

namespace {
RemoteShotGraphSnapshot::Input fixture(int pointsPerSeries)
{
    RemoteShotGraphSnapshot::Input input;
    input.hostId = QStringLiteral("fixture-host");
    input.revision = 7;
    input.rawTime = 62.992;
    input.weightAtStop = 31.2;
    input.finalWeight = 33.4;
    input.profileTargetWeight = 32.0;
    input.machineTargetWeight = 32.5;
    input.hasProfileTargetWeight = true;
    input.hasMachineTargetWeight = true;
    input.phaseMarkers.append({0.0, QStringLiteral("Start"), 1, false,
                               QStringLiteral("time"), QStringLiteral(" [T]")});

    const auto series = [pointsPerSeries] {
        QVector<QPointF> points;
        points.reserve(pointsPerSeries);
        for (int i = 0; i < pointsPerSeries; ++i)
            points.append(QPointF(i * 62.992 / qMax(1, pointsPerSeries - 1),
                                  (i % 100) * 0.1037 + 0.12345));
        return points;
    };
    input.pressure = series();
    input.flow = series();
    input.temperature = series();
    input.temperatureMix = series();
    input.weight = series();
    input.weightFlowRate = series();
    input.resistance = series();
    input.conductance = series();
    input.darcyResistance = series();
    input.pressureGoal = {QPointF(0, 0), QPointF(5, 9), QPointF(qQNaN(), qQNaN()),
                          QPointF(6, 8), QPointF(10, 8)};
    input.flowGoal = {QPointF(0, 0), QPointF(10, 4)};
    input.temperatureGoal = {QPointF(0, 90), QPointF(10, 92)};
    input.temperatureMixGoal = {QPointF(0, 85), QPointF(10, 87)};
    return input;
}

QJsonObject parse(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}
} // namespace

class TestRemoteShotGraphSnapshot : public QObject
{
    Q_OBJECT

    void reportSerializationTiming(int pointsPerSeries)
    {
        constexpr int kRuns = 32;
        const auto input = fixture(pointsPerSeries);
        QVector<double> milliseconds;
        milliseconds.reserve(kRuns);
        qint64 serializedBytes = 0;
        bool accepted = false;
        int status = 0;
        for (int run = 0; run < kRuns; ++run) {
            QElapsedTimer timer;
            timer.start();
            const auto result = RemoteShotGraphSnapshot::serialize(input);
            milliseconds.append(timer.nsecsElapsed() / 1.0e6);
            serializedBytes = result.serializedBodyBytes;
            accepted = result.ok;
            status = result.httpStatus;
        }
        std::sort(milliseconds.begin(), milliseconds.end());
        const double median = (milliseconds.at(15) + milliseconds.at(16)) / 2.0;
        const double p95 = milliseconds.at(30);
        const double worst = milliseconds.last();
        qInfo().noquote()
            << "graph serialize points/curve=" << pointsPerSeries
            << "runs=" << kRuns
            << "payloadBytes=" << serializedBytes
            << "accepted=" << accepted
            << "httpStatus=" << status
            << "medianMs=" << median
            << "p95Ms=" << p95
            << "worstMs=" << worst;
        QVERIFY(serializedBytes > 0);
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void serializesAllSeriesAndGoalSeparators()
    {
        const auto result = RemoteShotGraphSnapshot::serialize(fixture(253));
        QVERIFY(result.ok);
        QCOMPARE(result.httpStatus, 200);
        const QJsonObject root = parse(result.body);
        QCOMPARE(root.value("type").toString(), QStringLiteral("graphSnapshot"));
        QCOMPARE(root.value("revision").toDouble(), 7.0);
        const QJsonObject graph = root.value("graph").toObject();
        QCOMPARE(graph.value("profileTargetWeight").toDouble(), 32.0);
        QCOMPARE(graph.value("machineTargetWeight").toDouble(), 32.5);
        const QJsonObject series = graph.value("series").toObject();
        const QStringList names = {"pressure", "flow", "temperature", "temperatureMix",
                                   "weight", "weightFlowRate", "resistance", "conductance",
                                   "darcyResistance"};
        for (const QString& name : names)
            QCOMPARE(series.value(name).toArray().size(), 253);
        const QJsonArray pressureGoal = graph.value("goals").toObject()
                                            .value("pressure").toArray();
        QCOMPARE(pressureGoal.size(), 5);
        QVERIFY(pressureGoal.at(2).isNull());
        QVERIFY(pressureGoal.at(1).isArray());
        QCOMPARE(graph.value("phaseMarkers").toArray().size(), 1);
        QCOMPARE(graph.value("phaseMarkers").toArray().at(0).toObject()
                     .value("transitionSuffix").toString(), QStringLiteral(" [T]"));
    }

    void rejectsInvalidAndOversizedCurves()
    {
        auto invalid = fixture(1);
        invalid.flow[0].setY(std::numeric_limits<double>::infinity());
        auto result = RemoteShotGraphSnapshot::serialize(invalid);
        QVERIFY(!result.ok);
        QCOMPARE(result.httpStatus, 422);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("invalid_point"));

        auto tooMany = fixture(6001);
        result = RemoteShotGraphSnapshot::serialize(tooMany);
        QVERIFY(!result.ok);
        QCOMPARE(result.httpStatus, 413);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("point_limit"));

        auto tooManyBreaks = fixture(0);
        tooManyBreaks.pressureGoal.clear();
        tooManyBreaks.pressureGoal.resize(6001);
        for (QPointF& point : tooManyBreaks.pressureGoal)
            point = QPointF(qQNaN(), qQNaN());
        result = RemoteShotGraphSnapshot::serialize(tooManyBreaks);
        QVERIFY(!result.ok);
        QCOMPARE(result.httpStatus, 413);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("point_limit"));

        auto invalidSeriesBreak = fixture(0);
        invalidSeriesBreak.pressure.append(QPointF(qQNaN(), qQNaN()));
        result = RemoteShotGraphSnapshot::serialize(invalidSeriesBreak);
        QVERIFY(!result.ok);
        QCOMPARE(result.httpStatus, 422);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("invalid_point"));
    }

    void rejectsMarkerRevisionAndBodyLimits()
    {
        auto tooManyMarkers = fixture(0);
        for (int i = 0; i < 512; ++i)
            tooManyMarkers.phaseMarkers.append({double(i), QStringLiteral("marker"), i,
                                                false, QString(), QString()});
        auto result = RemoteShotGraphSnapshot::serialize(tooManyMarkers);
        QVERIFY(!result.ok);
        QCOMPARE(result.httpStatus, 413);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("phase_marker_limit"));

        auto invalidRevision = fixture(0);
        invalidRevision.revision = 9007199254740992ULL;
        result = RemoteShotGraphSnapshot::serialize(invalidRevision);
        QVERIFY(!result.ok);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("invalid_revision"));

        auto tooLarge = fixture(0);
        tooLarge.phaseMarkers[0].label = QString(2 * 1024 * 1024, QLatin1Char('x'));
        result = RemoteShotGraphSnapshot::serialize(tooLarge);
        QVERIFY(!result.ok);
        QCOMPARE(result.httpStatus, 413);
        QCOMPARE(parse(result.body).value("error").toObject().value("code").toString(),
                 QStringLiteral("body_limit"));
        QVERIFY(result.serializedBodyBytes > 2 * 1024 * 1024);
    }

    void reportsBoundedSerializationTimings()
    {
        reportSerializationTiming(253);
        reportSerializationTiming(2530);
        reportSerializationTiming(6000);
    }
};

QTEST_GUILESS_MAIN(TestRemoteShotGraphSnapshot)
#include "tst_remoteshotgraphsnapshot.moc"
