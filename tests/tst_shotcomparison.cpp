#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "history/shotcomparison.h"
#include "history/shotprojection.h"

using namespace ShotComparison;

namespace {

QVariantList curve(const QList<QPointF>& pts)
{
    QVariantList out;
    for (const QPointF& p : pts)
        out.append(QVariantMap{ { QStringLiteral("x"), p.x() }, { QStringLiteral("y"), p.y() } });
    return out;
}

ShotProjection shot(qint64 id)
{
    ShotProjection s;
    s.id = id;
    s.profileName = QStringLiteral("D-Flow / Q");
    s.doseWeightG = 18.0;
    s.targetWeightG = 54.0;
    s.grinderBrand = QStringLiteral("Niche");
    s.grinderModel = QStringLiteral("Zero");
    s.grinderSetting = QStringLiteral("10");
    s.beanBrand = QStringLiteral("Saka");
    return s;
}

QJsonObject row(const QJsonArray& rows, const QString& key)
{
    for (const QJsonValue& v : rows)
        if (v.toObject().value(QStringLiteral("key")).toString() == key) return v.toObject();
    return {};
}

// The metric keys a summary names, in order.
QStringList metricFacts(const QJsonArray& summary)
{
    QStringList out;
    for (const QJsonValue& v : summary)
        if (v.toObject()[QStringLiteral("kind")].toString() == QLatin1String("metric"))
            out << v.toObject()[QStringLiteral("key")].toString();
    return out;
}

QJsonObject factOf(const QJsonArray& summary, const char* kind)
{
    for (const QJsonValue& v : summary)
        if (v.toObject()[QStringLiteral("kind")].toString() == QLatin1String(kind)) return v.toObject();
    return {};
}

QString bundledProfile()
{
    QFile f(QStringLiteral(DECENZA_SOURCE_DIR "/resources/profiles/80_s_espresso.json"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

class TstShotComparison : public QObject {
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void inputState_data();
    void inputState();
    void grindDeltaNeedsTheSameGrinder();
    void oneSidedIsShownButIsNotAChange();
    void metricsFollowTheirDefinitions();
    void profileVersionIgnoresEncoding();
    void comparisonJsonShape();
    void pairChangesReadsFromTo();
    void summaryIgnoresNoise();
    void deltaMatchesShownValues();
};

// Each row: base value, other value, the field it lands in, expected state.
void TstShotComparison::inputState_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("a");
    QTest::addColumn<QString>("b");
    QTest::addColumn<int>("expected");

    const int same = int(FieldState::Same), changed = int(FieldState::Changed);
    QTest::newRow("dose within scale noise")  << "doseG" << "18.0" << "18.2" << same;
    QTest::newRow("dose moved")               << "doseG" << "18.0" << "18.5" << changed;
    QTest::newRow("target within rounding")   << "targetYieldG" << "54.0" << "54.4" << same;
    QTest::newRow("target moved")             << "targetYieldG" << "54.0" << "45.0" << changed;
    QTest::newRow("rpm rounding")             << "rpm" << "1200" << "1220" << same;
    QTest::newRow("rpm moved")                << "rpm" << "1200" << "1300" << changed;
    QTest::newRow("grind notation only")      << "grinderSetting" << "10" << "10.0" << same;
    QTest::newRow("grind fine move")          << "grinderSetting" << "4.0" << "4.2" << changed;
    QTest::newRow("grind moved")              << "grinderSetting" << "10" << "9.5" << changed;
    QTest::newRow("compound spacing")         << "grinderSetting" << "1 + 4" << "1+4" << same;
    QTest::newRow("lettered dial")            << "grinderSetting" << "3F" << "3C" << changed;
    QTest::newRow("bean case only")           << "bean" << "Saka" << "saka" << same;
    QTest::newRow("bean swapped")             << "bean" << "Saka" << "Sey" << changed;
}

void TstShotComparison::inputState()
{
    QFETCH(QString, field);
    QFETCH(QString, a);
    QFETCH(QString, b);
    QFETCH(int, expected);

    ShotProjection base = shot(1), other = shot(2);
    auto set = [&](ShotProjection& s, const QString& v) {
        if (field == QLatin1String("doseG")) s.doseWeightG = v.toDouble();
        else if (field == QLatin1String("targetYieldG")) s.targetWeightG = v.toDouble();
        else if (field == QLatin1String("rpm")) s.rpm = v.toLongLong();
        else if (field == QLatin1String("grinderSetting")) s.grinderSetting = v;
        else if (field == QLatin1String("bean")) s.beanBrand = v;
    };
    set(base, a);
    set(other, b);

    const InputDiff d = diffInputs(base, other);
    QVERIFY(d.field(field));
    QCOMPARE(int(d.field(field)->state), expected);
}

void TstShotComparison::grindDeltaNeedsTheSameGrinder()
{
    ShotProjection base = shot(1), other = shot(2);
    other.grinderSetting = QStringLiteral("9.5");
    const InputDiff sameGrinder = diffInputs(base, other);
    QVERIFY(sameGrinder.field(QStringLiteral("grinderSetting"))->delta.has_value());
    QCOMPARE(*sameGrinder.field(QStringLiteral("grinderSetting"))->delta, -0.5);

    // On another grinder the numbers are on another scale.
    other.grinderModel = QStringLiteral("DF64");
    const InputDiff swapped = diffInputs(base, other);
    QCOMPARE(swapped.field(QStringLiteral("grinder"))->state, FieldState::Changed);
    QCOMPARE(swapped.field(QStringLiteral("grinderSetting"))->state, FieldState::Changed);
    QVERIFY(!swapped.field(QStringLiteral("grinderSetting"))->delta.has_value());
}

// The advisor reads changed(); the page shows OneSided. They must stay apart.
void TstShotComparison::oneSidedIsShownButIsNotAChange()
{
    ShotProjection base = shot(1), other = shot(2);
    base.grinderSetting.clear();
    other.rpm = 1200;

    const InputDiff d = diffInputs(base, other);
    QCOMPARE(d.field(QStringLiteral("grinderSetting"))->state, FieldState::OneSided);
    QCOMPARE(d.field(QStringLiteral("rpm"))->state, FieldState::OneSided);
    QVERIFY(!d.changed(QStringLiteral("grinderSetting")));
    QVERIFY(!d.changed(QStringLiteral("rpm")));
    QVERIFY(d.anyDifference());
    QVERIFY(!d.field(QStringLiteral("grinderSetting"))->delta.has_value());
}

void TstShotComparison::metricsFollowTheirDefinitions()
{
    ShotProjection s = shot(1);
    s.durationSec = 30.0;
    s.finalWeightG = 36.0;
    s.detectorResults = { { QStringLiteral("pourStartSec"), 5.0 }, { QStringLiteral("pourEndSec"), 30.0 } };
    s.pressure = curve({ { 0, 1 }, { 5, 3 }, { 10, 9 }, { 20, 6 }, { 30, 4 } });
    // A fill spike at t=1 the pour window must exclude, and a stall at t=25.
    s.flow = curve({ { 1, 8 }, { 5, 2 }, { 10, 2 }, { 20, 2 }, { 25, 0.2 }, { 30, 2 } });
    s.weight = curve({ { 0, 0 }, { 5, 0.2 }, { 6, 0.6 }, { 30, 36 } });
    s.temperature = curve({ { 0, 93 }, { 5, 92 }, { 15, 88 }, { 30, 90 } });
    // The stalled sample has no flow under it and must not count.
    s.resistance = curve({ { 10, 1.0 }, { 20, 2.0 }, { 25, 50.0 } });

    const QMap<QString, double> m = metricsFor(s);
    QCOMPARE(m.value(QStringLiteral("firstDropSec")), 6.0);
    QCOMPARE(m.value(QStringLiteral("preinfusionSec")), 5.0);
    QCOMPARE(m.value(QStringLiteral("pourSec")), 25.0);
    QCOMPARE(m.value(QStringLiteral("peakPressureBar")), 9.0);
    QCOMPARE(m.value(QStringLiteral("peakFlowMlPerSec")), 2.0);
    QCOMPARE(m.value(QStringLiteral("meanFlowMlPerSec")), 8.2 / 5);
    QCOMPARE(m.value(QStringLiteral("weightFlowGPerSec")), 36.0 / 24.0);
    QCOMPARE(m.value(QStringLiteral("tempSagC")), 4.0);
    QCOMPARE(m.value(QStringLiteral("resistance")), 1.5);
    QCOMPARE(m.value(QStringLiteral("ratio")), 2.0);
    QVERIFY(!m.contains(QStringLiteral("drinkTdsPct")));

    // An imported shot can carry curves without a duration: they bound the windows.
    s.durationSec = 0.0;
    QCOMPARE(metricsFor(s).value(QStringLiteral("peakPressureBar")), 9.0);
}

void TstShotComparison::profileVersionIgnoresEncoding()
{
    const QString json = bundledProfile();
    QVERIFY(!json.isEmpty());
    QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();

    ShotProjection base = shot(1), other = shot(2);
    base.profileJson = json;

    // Same numbers written differently: not a new version.
    QJsonObject reformatted = obj;
    reformatted[QStringLiteral("target_weight")] = QString::number(obj.value(QStringLiteral("target_weight")).toVariant().toDouble(), 'f', 2);
    other.profileJson = QString::fromUtf8(QJsonDocument(reformatted).toJson());
    QJsonObject c = compare({ base, other }, 0)[QStringLiteral("comparisons")].toArray().first().toObject();
    QCOMPARE(c[QStringLiteral("profile")].toObject()[QStringLiteral("sameVersion")].toBool(), true);

    // A real re-tune is a changed input.
    QJsonObject retuned = obj;
    retuned[QStringLiteral("target_weight")] = 42.0;
    other.profileJson = QString::fromUtf8(QJsonDocument(retuned).toJson());
    c = compare({ base, other }, 0)[QStringLiteral("comparisons")].toArray().first().toObject();
    QCOMPARE(c[QStringLiteral("profile")].toObject()[QStringLiteral("sameVersion")].toBool(), false);
    QVERIFY(!c[QStringLiteral("profile")].toObject()[QStringLiteral("rows")].toArray().isEmpty());
    QVERIFY(c[QStringLiteral("changedInputs")].toArray().contains(QStringLiteral("profileSettings")));
}

void TstShotComparison::comparisonJsonShape()
{
    ShotProjection older = shot(1), newer = shot(2);
    older.durationSec = 37.2;
    older.finalWeightG = 53.3;
    older.channelingDetected = true;
    older.enjoyment0to100 = 70;
    newer.durationSec = 11.0;
    newer.finalWeightG = 13.3;
    older.stoppedBy = QStringLiteral("weight");
    newer.stoppedBy = QStringLiteral("manual");

    // Base given second: it must still come out first.
    const QJsonObject out = compare({ newer, older }, 1);
    QCOMPARE(out[QStringLiteral("baseShotId")].toInteger(), 1);
    const QJsonArray shots = out[QStringLiteral("shots")].toArray();
    QCOMPARE(shots[0].toObject()[QStringLiteral("shotId")].toInteger(), 1);
    QVERIFY(shots[1].toObject()[QStringLiteral("rating0to100")].isNull());   // unrated is not 0

    const QJsonObject duration = row(out[QStringLiteral("metrics")].toArray(), QStringLiteral("durationSec"));
    QCOMPARE(duration[QStringLiteral("cells")].toArray()[1].toObject()[QStringLiteral("delta")].toDouble(), -26.2);

    // Nothing the user set differs: one "unchanged" line, no input rows.
    QVERIFY(out[QStringLiteral("inputs")].toArray().isEmpty());
    QVERIFY(!row(out[QStringLiteral("unchanged")].toArray(), QStringLiteral("doseG")).isEmpty());

    const QJsonObject c = out[QStringLiteral("comparisons")].toArray().first().toObject();
    QVERIFY(c[QStringLiteral("nothingChanged")].toBool());
    const QJsonArray summary = c[QStringLiteral("summary")].toArray();
    // Yield moved 40 noise floors, duration 26: yield ranks first. Ratio is not a
    // candidate because the dose did not move.
    const QStringList metrics = metricFacts(summary);
    QCOMPARE(metrics.first(), QStringLiteral("yieldG"));
    QVERIFY(!metrics.contains(QStringLiteral("ratio")));
    // Nothing changed in the setup, and the summary says so first.
    QCOMPARE(summary[0].toObject()[QStringLiteral("kind")].toString(), QStringLiteral("sameSetup"));
    const QJsonObject yield = summary[1].toObject();
    QCOMPARE(yield[QStringLiteral("phrase")].toString(), QStringLiteral("phrase.less"));
    QCOMPARE(yield[QStringLiteral("delta")].toDouble(), -40.0);
    // Then how it stopped, then the badge that went away.
    QCOMPARE(factOf(summary, "stopped")[QStringLiteral("stoppedBy")].toString(), QStringLiteral("manual"));
    QCOMPARE(factOf(summary, "badgeGone")[QStringLiteral("badge")].toString(), QStringLiteral("channeling"));
}

// A change inside a metric's noise floor is not summary-worthy however large it is
// relatively; ranking by relative change once put a 0.1 °C sag in the top three.
void TstShotComparison::summaryIgnoresNoise()
{
    ShotProjection a = shot(1), b = shot(2);
    a.durationSec = 30.0; b.durationSec = 30.4;   // under the 1 s floor
    // Only noise moved: the summary says nothing notable rather than nothing at all.
    const QJsonArray quiet = compare({ a, b }, 0)[QStringLiteral("comparisons")].toArray()
        .first().toObject()[QStringLiteral("summary")].toArray();
    QCOMPARE(quiet.size(), 2);
    QVERIFY(!factOf(quiet, "sameSetup").isEmpty());
    QVERIFY(!factOf(quiet, "noNotable").isEmpty());
    a.finalWeightG = 36.0; b.finalWeightG = 40.0; // 4 floors
    const QStringList top = metricFacts(compare({ a, b }, 0)[QStringLiteral("comparisons")].toArray()
        .first().toObject()[QStringLiteral("summary")].toArray());
    QCOMPARE(top, QStringList{ QStringLiteral("yieldG") });

    // A row behind "Show more" ranks after a visible one however far it moved.
    a.drinkTdsPct = 8.0; b.drinkTdsPct = 10.0;    // 20 floors, but hidden by default
    const QStringList withHidden = metricFacts(compare({ a, b }, 0)[QStringLiteral("comparisons")].toArray()
        .first().toObject()[QStringLiteral("summary")].toArray());
    QCOMPARE(withHidden, (QStringList{ QStringLiteral("yieldG"), QStringLiteral("drinkTdsPct") }));
}

// A Δ is the difference of the values as shown: 28.84 and 28.86 read 28.8 and 28.9,
// so the Δ reads 0.1 rather than vanishing.
void TstShotComparison::deltaMatchesShownValues()
{
    ShotProjection a = shot(1), b = shot(2);
    a.durationSec = 28.84; b.durationSec = 28.86;   // shown 28.8 and 28.9
    const QJsonObject row = [&] {
        for (const QJsonValue& v : compare({ a, b }, 0)[QStringLiteral("metrics")].toArray())
            if (v.toObject()[QStringLiteral("key")].toString() == QLatin1String("durationSec")) return v.toObject();
        return QJsonObject();
    }();
    QCOMPARE(row[QStringLiteral("cells")].toArray()[1].toObject()[QStringLiteral("delta")].toDouble(), 0.1);
}

// The advisor's changeFromPrev/changeFromBest: direction, tolerance, and a
// value recorded on one side shown as null rather than dropped.
void TstShotComparison::pairChangesReadsFromTo()
{
    ShotProjection a = shot(1), b = shot(2);
    QVERIFY(pairChanges(a, b).isEmpty());

    b.grinderSetting = QStringLiteral("9.5");
    b.rpm = 1210;            // only recorded on b
    a.durationSec = 30.0;
    b.durationSec = 34.0;
    a.enjoyment0to100 = 60;  // b unrated: no rating outcome
    const QJsonObject c = pairChanges(a, b);
    const QJsonObject inputs = c[QStringLiteral("inputs")].toObject();
    const QJsonObject grind = inputs[QStringLiteral("grinderSetting")].toObject();
    QCOMPARE(grind[QStringLiteral("from")].toString(), QStringLiteral("10"));
    QCOMPARE(grind[QStringLiteral("to")].toString(), QStringLiteral("9.5"));
    QCOMPARE(grind[QStringLiteral("delta")].toDouble(), -0.5);
    QVERIFY(inputs[QStringLiteral("rpm")].toObject()[QStringLiteral("from")].isNull());
    QCOMPARE(inputs[QStringLiteral("rpm")].toObject()[QStringLiteral("to")].toInt(), 1210);
    const QJsonObject outcomes = c[QStringLiteral("outcomes")].toObject();
    QCOMPARE(outcomes[QStringLiteral("durationSec")].toObject()[QStringLiteral("delta")].toDouble(), 4.0);
    QVERIFY(!outcomes.contains(QStringLiteral("rating0to100")));

    // The advisor states bean and setup per shot, so its dial-in diff leaves them out.
    b.beanBrand = QStringLiteral("Sey");
    QVERIFY(pairChanges(a, b)[QStringLiteral("inputs")].toObject().contains(QStringLiteral("bean")));
    const QJsonObject dialIn = pairChanges(a, b, PairInputs::DialIn)[QStringLiteral("inputs")].toObject();
    QVERIFY(!dialIn.contains(QStringLiteral("bean")));
    QVERIFY(dialIn.contains(QStringLiteral("grinderSetting")));
}

QTEST_GUILESS_MAIN(TstShotComparison)
#include "tst_shotcomparison.moc"
