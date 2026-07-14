#include <QTest>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QRegularExpression>

#include "history/shotprojection.h"

// Guards ShotProjection::coerce() — the conversion that lets Q_INVOKABLE
// uploaders take `const QVariant&` and accept BOTH shapes QML hands across the
// boundary: a genuine ShotProjection gadget, and a plain JS object (QVariantMap)
// produced by PostShotReviewPage's clonePersistedShot after a badge update or a
// metadata edit. Passing the clone to a `const ShotProjection&` parameter threw
// "Could not convert argument from [object Object] to ShotProjection" on the
// Qt 6.11 QML→C++ argument-binding path, silently dropping the visualizer PATCH.
class TstShotProjection : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void coerce_gadgetVariant_passesThroughIntact();
    void coerce_plainMap_reconstructsValidProjection();
    void coerce_emptyVariant_yieldsInvalidProjection();
    void coerce_nonMapScalar_yieldsInvalidProjection();
    void hasBag_followsSentinelRule();
    void toVariantMap_sparseEmitsBagIdOnlyWhenPresent();
};

static ShotProjection makeSampleShot()
{
    ShotProjection p;
    p.id = 974;
    p.uuid = QStringLiteral("1923cf99-922a-4da5-9da0-ffbb81ba6cf5");
    p.profileName = QStringLiteral("D-Flow / Q");
    p.durationSec = 19.568;
    p.finalWeightG = 34.8;
    p.doseWeightG = 18.0;
    p.beanBrand = QStringLiteral("Saka");
    p.beanType = QStringLiteral("Gran Bar");
    p.enjoyment0to100 = 75;
    p.visualizerId = QStringLiteral("2767d127-a166-41a2-a3db-dc7d8834a2d6");
    p.pressure = QVariantList{0.0, 6.0, 9.0};
    p.flow = QVariantList{0.0, 1.8, 1.8};
    return p;
}

// A genuine gadget wrapped in QVariant (the raw shotReady() shape, and what the
// C++ MCP callers pass via QVariant::fromValue) must round-trip unchanged.
void TstShotProjection::coerce_gadgetVariant_passesThroughIntact()
{
    const ShotProjection original = makeSampleShot();
    const QVariant v = QVariant::fromValue(original);

    const ShotProjection result = ShotProjection::coerce(v);

    QVERIFY(result.isValid());
    QCOMPARE(result.id, original.id);
    QCOMPARE(result.profileName, original.profileName);
    QCOMPARE(result.durationSec, original.durationSec);
    QCOMPARE(result.visualizerId, original.visualizerId);
    QCOMPARE(result.pressure.size(), original.pressure.size());
    QCOMPARE(result.enjoyment0to100, original.enjoyment0to100);
}

// The regression case: a plain QVariantMap (what a QML JS object marshals to —
// e.g. clonePersistedShot's output) must reconstruct a valid projection with
// id / durationSec / curve arrays intact, so isValid() passes and the upload
// is not silently dropped.
void TstShotProjection::coerce_plainMap_reconstructsValidProjection()
{
    const ShotProjection sample = makeSampleShot();
    const QVariantMap map = sample.toVariantMap();
    const QVariant v(map);  // a QVariantMap, NOT a ShotProjection gadget

    QVERIFY(v.userType() != qMetaTypeId<ShotProjection>());

    const ShotProjection result = ShotProjection::coerce(v);

    QVERIFY2(result.isValid(), "coerced clone must be valid (id != 0)");
    QCOMPARE(result.id, sample.id);
    QCOMPARE(result.profileName, sample.profileName);
    QCOMPARE(result.durationSec, sample.durationSec);
    QCOMPARE(result.visualizerId, sample.visualizerId);
    QCOMPARE(result.pressure.size(), sample.pressure.size());
}

void TstShotProjection::coerce_emptyVariant_yieldsInvalidProjection()
{
    // coerce() logs a diagnostic on empty/non-map input — assert it fires.
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression("ShotProjection::coerce: empty/non-map arg.*"));
    const ShotProjection result = ShotProjection::coerce(QVariant());
    QVERIFY(!result.isValid());
    QCOMPARE(result.id, qint64(0));
}

void TstShotProjection::coerce_nonMapScalar_yieldsInvalidProjection()
{
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression("ShotProjection::coerce: empty/non-map arg.*"));
    const ShotProjection result = ShotProjection::coerce(QVariant(QStringLiteral("not a shot")));
    QVERIFY(!result.isValid());
}

// hasBag() is the canonical "no bag" sentinel test (bagId <= 0 == none). The
// boundary matters: -1 (struct default), 0 (the NULL-mapped column value), and
// any positive id are all live values, and a future drift to >= 0 / != -1 would
// leak a phantom bagId into the Visualizer payload.
void TstShotProjection::hasBag_followsSentinelRule()
{
    ShotProjection p = makeSampleShot();

    p.bagId = -1;  // struct default / explicit "none"
    QVERIFY(!p.hasBag());
    p.bagId = 0;   // NULL column maps here under the sentinel rule
    QVERIFY(!p.hasBag());
    p.bagId = 1;   // smallest real bag id
    QVERIFY(p.hasBag());
    p.bagId = 974;
    QVERIFY(p.hasBag());
}

// toVariantMap() sparse-emits bagId: present only when hasBag(), so a no-bag
// shot never serializes a misleading bagId: 0 / -1 into the QML/MCP/upload map.
void TstShotProjection::toVariantMap_sparseEmitsBagIdOnlyWhenPresent()
{
    ShotProjection p = makeSampleShot();

    p.bagId = -1;
    QVERIFY2(!p.toVariantMap().contains(QStringLiteral("bagId")),
             "no-bag shot (-1) must omit the bagId key");
    p.bagId = 0;
    QVERIFY2(!p.toVariantMap().contains(QStringLiteral("bagId")),
             "no-bag shot (0) must omit the bagId key");
    p.bagId = 42;
    const QVariantMap withBag = p.toVariantMap();
    QVERIFY(withBag.contains(QStringLiteral("bagId")));
    QCOMPARE(withBag.value(QStringLiteral("bagId")).toLongLong(), qint64(42));
}

QTEST_APPLESS_MAIN(TstShotProjection)

#include "tst_shotprojection.moc"
