// Regression tests for ShotFileParser::parseVisualizerShot — the JSON->ShotRecord
// path used by "Recover shots from Visualizer".
//
// The crux these lock down: visualizer.coffee's /api/shots/{id}/download
// string-encodes numeric DYE scalars (Tcl-huddle convention: "16.2", "0"), while
// a few (espresso_enjoyment) arrive as bare numbers. A naive QJsonValue::toDouble()
// returns 0 for a *string*, which would silently zero dose weight / TDS / EY on
// every recovered shot. These tests parse the real download-schema fixture
// (tests/data/shots/cremina_clean.json) and assert the scalars survive.

#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "history/shotfileparser.h"

class TstVisualizerShotParse : public QObject
{
    Q_OBJECT

private:
    static QJsonObject loadFixture(const QString& name)
    {
        const QString path = QStringLiteral(TST_VIS_PARSE_SOURCE_DIR)
                             + QStringLiteral("/data/shots/") + name;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        return doc.object();
    }

    // Minimal valid download body: enough to clear parseVisualizerShot's guards
    // (non-empty timeframe + pressure), plus whatever extra fields a test sets.
    static QJsonObject minimalShot(const QJsonObject& extra)
    {
        QJsonArray tf;   tf.append(0.0);  tf.append(0.5);  tf.append(1.0);
        QJsonArray pres; pres.append(1.0); pres.append(6.0); pres.append(9.0);
        QJsonObject data;
        data.insert(QStringLiteral("espresso_pressure"), pres);
        QJsonObject shot = extra;
        shot.insert(QStringLiteral("timeframe"), tf);
        shot.insert(QStringLiteral("data"), data);
        return shot;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // The download string-encodes scalars — they must NOT come back as 0.
    void scalars_survive_string_encoding()
    {
        const QJsonObject shot = loadFixture(QStringLiteral("cremina_clean.json"));
        QVERIFY2(!shot.isEmpty(), "cremina_clean.json fixture missing/unreadable");

        // Sanity: confirm the fixture really is string-encoded (guards against a
        // future fixture swap silently defeating the regression).
        QVERIFY2(shot.value("bean_weight").isString(),
                 "fixture no longer string-encodes bean_weight — test is moot");

        const ShotFileParser::ParseResult res = ShotFileParser::parseVisualizerShot(
            shot, QString(), QStringLiteral("test-vis-id"), 1751000000);

        QVERIFY2(res.success, qPrintable(res.errorMessage));

        // bean_weight="16.2", drink_weight="34.8" — string-encoded, must parse.
        QVERIFY2(qAbs(res.record.summary.doseWeight - 16.2) < 0.01,
                 qPrintable(QStringLiteral("doseWeight zeroed/wrong: %1")
                            .arg(res.record.summary.doseWeight)));
        QVERIFY2(qAbs(res.record.summary.finalWeight - 34.8) < 0.01,
                 qPrintable(QStringLiteral("finalWeight zeroed/wrong: %1")
                            .arg(res.record.summary.finalWeight)));
        // duration=32.515 (bare number in this fixture) — bidirectional read.
        QVERIFY2(qAbs(res.record.summary.duration - 32.515) < 0.01,
                 qPrintable(QStringLiteral("duration wrong: %1")
                            .arg(res.record.summary.duration)));
        // espresso_enjoyment=69 (bare int) — must not be lost by the string branch.
        QCOMPARE(res.record.summary.enjoyment, 69);

        // Telemetry present and aligned.
        QVERIFY2(!res.record.pressure.isEmpty(), "pressure series empty");
        QVERIFY2(!res.record.flow.isEmpty(), "flow series empty");

        // Water dispensed is scaled x10 (tenths-of-ml on the wire -> real ml in
        // the DB). The fixture's series peaks at raw 8.1685, so the recovered
        // peak must be ~81.7 ml (not 8.2, and not double-scaled).
        double maxWater = 0;
        for (const auto& pt : res.record.waterDispensed)
            if (pt.y() > maxWater) maxWater = pt.y();
        QVERIFY2(qAbs(maxWater - 81.685) < 0.1,
                 qPrintable(QStringLiteral("water-dispensed x10 wrong: %1").arg(maxWater)));

        // Real frame detection: the fixture's state_change series flips sign 4
        // times. A positive count (not just "no phantom") proves the frame-marker
        // loop actually fires — a regression to zero frames would pass the
        // phantom guard vacuously.
        QCOMPARE(res.record.phases.size(), qsizetype(4));
        QCOMPARE(res.record.phases.first().frameNumber, 1);

        // Phantom-frame guard: the state_change series starts at 0.0, which must
        // NOT register a phase boundary at t≈0.
        for (const auto& ph : res.record.phases)
            QVERIFY2(ph.time > 0.1,
                     qPrintable(QStringLiteral("phantom frame marker at t=%1")
                                .arg(ph.time)));
    }

    // Taste taps round-trip: the uploader maps tasteBalance/tasteBody to the CVA
    // attributes acidity/bitterness/mouthfeel; recovery must map them back so a
    // device-swap recovery keeps the shot's taste dial-in.
    void taste_axes_round_trip_from_cva()
    {
        // sour = (acidity 12, bitterness 4); heavy = (mouthfeel 12) — the values
        // the uploader writes for those taps.
        const QJsonObject sour = minimalShot({
            {QStringLiteral("acidity"), 12},
            {QStringLiteral("bitterness"), 4},
            {QStringLiteral("mouthfeel"), 12},
        });
        const ShotFileParser::ParseResult r1 = ShotFileParser::parseVisualizerShot(
            sour, QString(), QStringLiteral("t-sour"), 1751000000);
        QVERIFY2(r1.success, qPrintable(r1.errorMessage));
        QCOMPARE(r1.record.tasteBalance, QStringLiteral("sour"));
        QCOMPARE(r1.record.tasteBody, QStringLiteral("heavy"));

        // bitter = (acidity 4, bitterness 12); thin = (mouthfeel 4).
        const QJsonObject bitter = minimalShot({
            {QStringLiteral("acidity"), 4},
            {QStringLiteral("bitterness"), 12},
            {QStringLiteral("mouthfeel"), 4},
        });
        const ShotFileParser::ParseResult r2 = ShotFileParser::parseVisualizerShot(
            bitter, QString(), QStringLiteral("t-bitter"), 1751000000);
        QCOMPARE(r2.record.tasteBalance, QStringLiteral("bitter"));
        QCOMPARE(r2.record.tasteBody, QStringLiteral("thin"));
    }

    // An untapped / hand-unscored shot (all CVA attrs 0, or absent) must NOT
    // invent a taste value — the taps stay empty so upload doesn't clobber a
    // Visualizer-scored assessment.
    void taste_axes_unset_when_cva_absent()
    {
        const ShotFileParser::ParseResult r = ShotFileParser::parseVisualizerShot(
            minimalShot({}), QString(), QStringLiteral("t-none"), 1751000000);
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QVERIFY2(r.record.tasteBalance.isEmpty(),
                 qPrintable(QStringLiteral("tasteBalance invented: %1").arg(r.record.tasteBalance)));
        QVERIFY2(r.record.tasteBody.isEmpty(),
                 qPrintable(QStringLiteral("tasteBody invented: %1").arg(r.record.tasteBody)));
    }

    // A shot with a timeframe but no pressure telemetry must FAIL, not import a
    // hollow record that would still be counted as "imported".
    void missing_pressure_fails_not_hollow()
    {
        QJsonObject shot;
        QJsonArray tf; tf.append(QStringLiteral("0.0")); tf.append(QStringLiteral("0.5"));
        shot.insert(QStringLiteral("timeframe"), tf);
        shot.insert(QStringLiteral("data"), QJsonObject{});  // no espresso_pressure
        shot.insert(QStringLiteral("profile_title"), QStringLiteral("X"));

        const ShotFileParser::ParseResult res = ShotFileParser::parseVisualizerShot(
            shot, QString(), QStringLiteral("test-empty"), 1751000000);

        QVERIFY2(!res.success, "hollow shot (no pressure) was accepted");
        QVERIFY(res.errorMessage.contains(QStringLiteral("pressure"), Qt::CaseInsensitive));
    }
};

QTEST_GUILESS_MAIN(TstVisualizerShotParse)
#include "tst_visualizershotparse.moc"
