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

private slots:
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

        // Phantom-frame guard: the state_change series starts at 0.0, which must
        // NOT register a phase boundary at t≈0.
        for (const auto& ph : res.record.phases)
            QVERIFY2(ph.time > 0.1,
                     qPrintable(QStringLiteral("phantom frame marker at t=%1")
                                .arg(ph.time)));
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

QTEST_MAIN(TstVisualizerShotParse)
#include "tst_visualizershotparse.moc"
