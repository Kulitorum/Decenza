#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "network/visualizeruploader.h"
#include "history/shotprojection.h"

// Guards the mix temperature goal in the shot JSON uploaded to Visualizer.
//
// The load-bearing rule: interpolateGoalData() returns an array of ZEROS when
// handed an empty goal vector. Emitting `mix_goal` unconditionally would upload
// a flat 0 °C goal line for every shot recorded before this series existed, and
// for every shot imported from a de1app .shot file (de1app has no
// espresso_temperature_mix_goal vector at all). Visualizer treats a MISSING
// mix_goal as legacy data and draws nothing — so absent must mean absent.
class tst_VisualizerMixGoal : public QObject {
    Q_OBJECT

private:
    static QVariantList series(const QVector<QPointF>& pts) {
        QVariantList out;
        for (const auto& p : pts) {
            QVariantMap m;
            m["x"] = p.x();
            m["y"] = p.y();
            out.append(m);
        }
        return out;
    }

    // A minimal projection with a 3-sample timeline. Pressure is the master
    // timeline every other series interpolates onto.
    static ShotProjection baseProjection() {
        ShotProjection p;
        p.pressure = series({{0.0, 6.0}, {1.0, 9.0}, {2.0, 9.0}});
        p.flow = series({{0.0, 1.0}, {1.0, 2.0}, {2.0, 2.2}});
        p.temperature = series({{0.0, 92.0}, {1.0, 92.1}, {2.0, 92.2}});
        p.temperatureGoal = series({{0.0, 92.0}, {1.0, 92.0}, {2.0, 92.0}});
        return p;
    }

    static QJsonObject temperatureOf(const ShotProjection& p) {
        QByteArray json = VisualizerUploader::buildHistoryShotJson(p);
        return QJsonDocument::fromJson(json).object()["temperature"].toObject();
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void historyUploadIncludesMixGoalWhenPresent() {
        ShotProjection p = baseProjection();
        p.temperatureMixGoal = series({{0.0, 94.0}, {1.0, 94.0}, {2.0, 94.0}});

        QJsonObject temp = temperatureOf(p);

        QVERIFY(temp.contains("mix_goal"));
        QJsonArray mixGoal = temp["mix_goal"].toArray();
        // Must line up with the elapsed timeline, which comes from pressure.
        QCOMPARE(mixGoal.size(), 3);
        QVERIFY(qAbs(mixGoal[0].toDouble() - 94.0) < 0.01);
    }

    void historyUploadOmitsMixGoalWhenAbsent() {
        ShotProjection p = baseProjection();  // no temperatureMixGoal

        QJsonObject temp = temperatureOf(p);

        // Absent, NOT a zero-filled array — see the class comment.
        QVERIFY(!temp.contains("mix_goal"));
    }

    void historyUploadStillCarriesBasketGoal() {
        ShotProjection p = baseProjection();
        p.temperatureMixGoal = series({{0.0, 94.0}, {1.0, 94.0}, {2.0, 94.0}});

        QJsonObject temp = temperatureOf(p);

        // temperature.goal is SetHeadTemp and must not be displaced by mix_goal:
        // Visualizer labels it "Basket Temperature Goal".
        QVERIFY(temp.contains("goal"));
        QVERIFY(qAbs(temp["goal"].toArray()[0].toDouble() - 92.0) < 0.01);
    }

    // The history builder used to omit `mix` entirely while the live builder
    // sent it, so re-uploading a shot silently dropped the measured mix line.
    void historyUploadIncludesMeasuredMixWhenPresent() {
        ShotProjection p = baseProjection();
        p.temperatureMix = series({{0.0, 90.0}, {1.0, 90.5}, {2.0, 90.7}});

        QJsonObject temp = temperatureOf(p);

        QVERIFY(temp.contains("mix"));
        QCOMPARE(temp["mix"].toArray().size(), 3);
    }

    void historyUploadOmitsMeasuredMixWhenAbsent() {
        ShotProjection p = baseProjection();

        QJsonObject temp = temperatureOf(p);

        QVERIFY(!temp.contains("mix"));
    }
};

QTEST_MAIN(tst_VisualizerMixGoal)
#include "tst_visualizermixgoal.moc"
