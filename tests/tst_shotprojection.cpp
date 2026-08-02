#include <QTest>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QRegularExpression>
#include <QMetaProperty>
#include <QSet>
#include <QVector>

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
    void toVariantMap_roundTripsTasteAxes();
    void toVariantMap_roundTripsRpm();
    void toVariantMap_roundTripsRecipeDisplayAndDateTime();
    void everyQPropertySurvivesARoundTrip();
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

// Structured taste axes (add-ai-taste-intake) sparse-emit when unset and round-
// trip through toVariantMap()/fromVariantMap() when set.
void TstShotProjection::toVariantMap_roundTripsTasteAxes()
{
    ShotProjection p = makeSampleShot();
    QVERIFY2(!p.toVariantMap().contains(QStringLiteral("tasteBalance")),
             "unset tasteBalance must be omitted");
    QVERIFY2(!p.toVariantMap().contains(QStringLiteral("tasteBody")),
             "unset tasteBody must be omitted");

    p.tasteBalance = QStringLiteral("sour");
    p.tasteBody = QStringLiteral("thin");
    const QVariantMap m = p.toVariantMap();
    QCOMPARE(m.value(QStringLiteral("tasteBalance")).toString(), QStringLiteral("sour"));
    QCOMPARE(m.value(QStringLiteral("tasteBody")).toString(), QStringLiteral("thin"));

    const ShotProjection back = ShotProjection::fromVariantMap(m);
    QCOMPARE(back.tasteBalance, QStringLiteral("sour"));
    QCOMPARE(back.tasteBody, QStringLiteral("thin"));
}

// RPM is the second half of the dial-in; it must survive the JSON round-trip
// (the linchpin that carries rpm into shots_get_detail / shots_compare and the
// clone/coerce path), and stay sparse (omitted) when unset.
void TstShotProjection::toVariantMap_roundTripsRpm()
{
    ShotProjection p = makeSampleShot();
    QVERIFY2(!p.toVariantMap().contains(QStringLiteral("rpm")),
             "unset rpm (0) must be omitted");

    p.rpm = 1400;
    const QVariantMap m = p.toVariantMap();
    QCOMPARE(m.value(QStringLiteral("rpm")).toLongLong(), static_cast<qint64>(1400));

    const ShotProjection back = ShotProjection::fromVariantMap(m);
    QCOMPARE(back.rpm, static_cast<qint64>(1400));
}

// Recipe identity (history-recipe-identity) and the preformatted dateTime must
// survive a QVariantMap round-trip. This is the defect class it guards, not a
// hypothetical: `dateTime` was declared as a member AND a Q_PROPERTY but was
// read by neither conversion function, so every consumer that round-trips a
// shot through a map — the ShotServer shot list among them — rendered a blank
// date, for as long as that code has existed. The recipe fields were about to
// repeat it exactly. A field that exists in the struct but in only one of the
// two functions produces no compiler error and no warning; it just silently
// arrives empty.
void TstShotProjection::toVariantMap_roundTripsRecipeDisplayAndDateTime()
{
    ShotProjection p = makeSampleShot();

    // Sparse when unset — a recipe-less shot must not carry empty recipe keys.
    const QVariantMap bare = p.toVariantMap();
    QVERIFY2(!bare.contains(QStringLiteral("recipeName")), "unset recipeName must be omitted");
    QVERIFY2(!bare.contains(QStringLiteral("recipeDrinkType")), "unset recipeDrinkType must be omitted");
    QVERIFY2(!bare.contains(QStringLiteral("recipeArchived")), "false recipeArchived must be omitted");
    QVERIFY2(!bare.contains(QStringLiteral("dateTime")), "unset dateTime must be omitted");

    p.recipeId = 7;
    p.recipeName = QStringLiteral("Dad Monday");
    p.recipeDrinkType = QStringLiteral("latte");
    p.recipeArchived = true;
    p.dateTime = QStringLiteral("2026-08-01 09:21");

    const ShotProjection back = ShotProjection::fromVariantMap(p.toVariantMap());
    QCOMPARE(back.recipeId, static_cast<qint64>(7));
    QCOMPARE(back.recipeName, QStringLiteral("Dad Monday"));
    QCOMPARE(back.recipeDrinkType, QStringLiteral("latte"));
    QCOMPARE(back.recipeArchived, true);
    QCOMPARE(back.dateTime, QStringLiteral("2026-08-01 09:21"));
}

// Closes the DEFECT CLASS rather than another instance of it.
//
// Every field above is carried by two hand-written functions that map keys by
// name. A member declared in the header but absent from one of them — or, worse,
// spelled differently in each — compiles clean, passes qmllint, and silently
// arrives empty at every consumer. That has happened at least three times in
// this struct: `dateTime` (in neither function, blanked the web shot list's
// date), `timestamp` (read but never written), and the six equipment/basket
// fields (in neither). A per-field test only ever catches the field someone
// already noticed.
//
// So: drive it from the metaobject. Set every scalar Q_PROPERTY to a
// distinctive non-default value, round-trip through the map, and require it
// back. Any future field added to the header and forgotten in either function
// fails here on the day it is added, with the property's name in the message.
//
// Container properties (the time-series QVariantLists, detectorResults,
// phaseSummaries) are skipped: they round-trip as opaque QVariants and building
// meaningful values for them here would test QVariant, not the mapping.
void TstShotProjection::everyQPropertySurvivesARoundTrip()
{
    ShotProjection p = makeSampleShot();
    const QMetaObject& mo = ShotProjection::staticMetaObject;

    // Fields whose emit is conditional on a value the round-trip cannot preserve
    // by design — documented sparse rules, not oversights. Each needs a reason.
    static const QSet<QByteArray> kSkip = {
        // stoppedBy sparse-emits only manual/weight/volume; "profileEnd" and ""
        // deliberately collapse to "" (see toVariantMap).
        QByteArrayLiteral("stoppedBy"),
        // yieldMode sparse-emits only absolute/ratio, and yieldAnchorValue rides
        // with it.
        QByteArrayLiteral("yieldMode"),
        QByteArrayLiteral("yieldAnchorValue"),
    };

    QVector<QByteArray> checked;
    for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
        const QMetaProperty prop = mo.property(i);
        const QByteArray name = prop.name();
        if (kSkip.contains(name))
            continue;

        QVariant probe;
        switch (prop.metaType().id()) {
        case QMetaType::QString:  probe = QStringLiteral("rt-%1").arg(i); break;
        case QMetaType::Int:      probe = 40 + i; break;
        case QMetaType::LongLong: probe = static_cast<qint64>(90000 + i); break;
        case QMetaType::Double:   probe = 1.0 + i; break;
        case QMetaType::Bool:     probe = true; break;
        default:                  continue;   // containers — see comment above
        }
        QVERIFY2(prop.writeOnGadget(&p, probe),
                 qPrintable(QStringLiteral("could not set %1").arg(QString::fromLatin1(name))));
        checked.append(name);
    }

    // Guard against the guard: if the switch ever stops matching anything, an
    // empty loop below would pass silently.
    QVERIFY2(checked.size() > 30,
             qPrintable(QStringLiteral("only %1 scalar properties probed — the "
                                       "metaType switch has gone stale").arg(checked.size())));

    const ShotProjection back = ShotProjection::fromVariantMap(p.toVariantMap());
    for (const QByteArray& name : checked) {
        const QMetaProperty prop = mo.property(mo.indexOfProperty(name.constData()));
        QCOMPARE_NE(prop.readOnGadget(&p), QVariant());
        QVERIFY2(prop.readOnGadget(&back) == prop.readOnGadget(&p),
                 qPrintable(QStringLiteral("%1 did not survive toVariantMap/fromVariantMap — "
                                           "it is missing from one of them, or the two spell "
                                           "its key differently")
                                .arg(QString::fromLatin1(name))));
    }
}

QTEST_APPLESS_MAIN(TstShotProjection)

#include "tst_shotprojection.moc"
