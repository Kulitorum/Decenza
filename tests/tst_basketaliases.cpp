// Tests for the BasketAliases registry (src/core/basketaliases.h).
//
// Two kinds of coverage:
//   1. Data-integrity sweep over allBaskets() — the registry is a long
//      positional aggregate-init table, so a transposed/typo'd field compiles
//      silently. These assertions catch that class of bug at test time.
//   2. Pure helper logic — lookup() longest-match resolution + substring
//      fallback, and summary() string formatting (the flow-omit, hole, and
//      dose-trim rules). Formatting cases use CONSTRUCTED entries, not registry
//      rows, so they don't break when the basket data is refined.

#include <QTest>
#include <QSet>
#include "core/basketaliases.h"

using namespace BasketAliases;

class TstBasketAliases : public QObject
{
    Q_OBJECT

private slots:
    // Data integrity
    void everyEntry_is58mm();
    void everyEntry_hasSaneDoseRange();
    void everyEntry_hasNonNegativeHoleSpecs();
    void everyEntry_hasBrandAndModel();
    void everyAlias_isLowercaseAndNonEmpty();
    void aliases_areUniqueAcrossEntries();

    // lookup()
    void lookup_emptyOrWhitespace_returnsNotFound();
    void lookup_unknown_returnsNotFound();
    void lookup_exactAlias_resolves();
    void lookup_substringFallback_resolves();
    void lookup_longestMatchWins_acrossEntries();

    // summary()
    void summary_standardFlow_omitsFlowToken();
    void summary_openFlow_includesFlowToken();
    void summary_noHoleData_omitsHoleClause();
    void summary_micronsOnly_rendersMicronHoles();
    void summary_countAndMicrons_rendersBoth();
    void summary_trimsDoseDecimals();
    void summary_fullExample_matchesDocstring();

    // brand/model surface + round-trip
    void allBrands_uniqueAndSorted();
    void findEntryByAlias_roundTrips();
};

// ---- Data integrity -------------------------------------------------------

void TstBasketAliases::everyEntry_is58mm()
{
    for (const auto& b : allBaskets())
        QVERIFY2(b.diameterMm == 58,
                 qPrintable(QStringLiteral("%1 %2 is not 58mm").arg(b.brand, b.model)));
}

void TstBasketAliases::everyEntry_hasSaneDoseRange()
{
    for (const auto& b : allBaskets()) {
        const QString id = QStringLiteral("%1 %2").arg(b.brand, b.model);
        QVERIFY2(b.doseMinG > 0, qPrintable(id + QStringLiteral(": doseMinG must be > 0")));
        QVERIFY2(b.doseMinG <= b.doseMaxG, qPrintable(id + QStringLiteral(": doseMinG > doseMaxG")));
    }
}

void TstBasketAliases::everyEntry_hasNonNegativeHoleSpecs()
{
    for (const auto& b : allBaskets()) {
        const QString id = QStringLiteral("%1 %2").arg(b.brand, b.model);
        QVERIFY2(b.holeCount >= 0, qPrintable(id + QStringLiteral(": negative holeCount")));
        QVERIFY2(b.holeDiameterMicrons >= 0, qPrintable(id + QStringLiteral(": negative holeDiameterMicrons")));
    }
}

void TstBasketAliases::everyEntry_hasBrandAndModel()
{
    for (const auto& b : allBaskets()) {
        QVERIFY(!b.brand.trimmed().isEmpty());
        QVERIFY2(!b.model.trimmed().isEmpty(),
                 qPrintable(QStringLiteral("%1 has empty model").arg(b.brand)));
    }
}

void TstBasketAliases::everyAlias_isLowercaseAndNonEmpty()
{
    // lookup() lowercases its input, so an uppercase alias would never match.
    for (const auto& b : allBaskets()) {
        for (const auto& alias : b.aliases) {
            const QString id = QStringLiteral("%1 %2 alias '%3'").arg(b.brand, b.model, alias);
            QVERIFY2(!alias.trimmed().isEmpty(), qPrintable(id + QStringLiteral(" is empty")));
            QVERIFY2(alias == alias.toLower(), qPrintable(id + QStringLiteral(" is not lowercase")));
        }
    }
}

void TstBasketAliases::aliases_areUniqueAcrossEntries()
{
    // A duplicated alias would make longest-match lookup order-dependent.
    QSet<QString> seen;
    for (const auto& b : allBaskets()) {
        for (const auto& alias : b.aliases) {
            QVERIFY2(!seen.contains(alias),
                     qPrintable(QStringLiteral("duplicate alias '%1' (in %2 %3)").arg(alias, b.brand, b.model)));
            seen.insert(alias);
        }
    }
}

// ---- lookup() -------------------------------------------------------------

void TstBasketAliases::lookup_emptyOrWhitespace_returnsNotFound()
{
    QVERIFY(!lookup(QString()).found);
    QVERIFY(!lookup(QStringLiteral("")).found);
    QVERIFY(!lookup(QStringLiteral("   ")).found);
}

void TstBasketAliases::lookup_unknown_returnsNotFound()
{
    QVERIFY(!lookup(QStringLiteral("totally-not-a-basket-zzz")).found);
}

void TstBasketAliases::lookup_exactAlias_resolves()
{
    const auto r = lookup(QStringLiteral("vst 18g"));
    QVERIFY(r.found);
    QCOMPARE(r.brand, QStringLiteral("VST"));
    QCOMPARE(r.model, QStringLiteral("18g Double"));
}

void TstBasketAliases::lookup_substringFallback_resolves()
{
    // No exact alias equals this; the second (contains) pass should still hit.
    const auto r = lookup(QStringLiteral("VST 18g (ridgeless)"));
    QVERIFY(r.found);
    QCOMPARE(r.brand, QStringLiteral("VST"));
    QCOMPARE(r.model, QStringLiteral("18g Double"));
}

void TstBasketAliases::lookup_longestMatchWins_acrossEntries()
{
    // "pesado" -> HE High Extraction; "pesado ep" -> EP Electro-Polished.
    // A string containing both must resolve to the LONGER alias (EP).
    const auto ep = lookup(QStringLiteral("my pesado ep basket"));
    QVERIFY(ep.found);
    QCOMPARE(ep.brand, QStringLiteral("Pesado"));
    QCOMPARE(ep.model, QStringLiteral("EP Electro-Polished"));

    // Bare "pesado" still resolves to the HE entry.
    const auto he = lookup(QStringLiteral("pesado"));
    QVERIFY(he.found);
    QCOMPARE(he.model, QStringLiteral("HE High Extraction"));
}

// ---- summary() ------------------------------------------------------------

void TstBasketAliases::summary_standardFlow_omitsFlowToken()
{
    BasketEntry b{};
    b.diameterMm = 58;
    b.wall = WallProfile::Straight;
    b.flow = FlowRate::Standard;
    b.doseMinG = 18;
    b.doseMaxG = 20;
    const QString s = summary(b);
    QVERIFY2(!s.contains(QStringLiteral("flow")), qPrintable(s));
    QCOMPARE(s, QStringLiteral("58mm, straight-wall, 18-20g"));
}

void TstBasketAliases::summary_openFlow_includesFlowToken()
{
    BasketEntry b{};
    b.diameterMm = 58;
    b.flow = FlowRate::Open;
    b.doseMinG = 17;
    b.doseMaxG = 19;
    QVERIFY(summary(b).contains(QStringLiteral("open flow")));
}

void TstBasketAliases::summary_noHoleData_omitsHoleClause()
{
    BasketEntry b{};
    b.diameterMm = 58;
    b.doseMinG = 18;
    b.doseMaxG = 20;
    b.holeCount = 0;
    b.holeDiameterMicrons = 0;
    QVERIFY(!summary(b).contains(QStringLiteral("hole")));
}

void TstBasketAliases::summary_micronsOnly_rendersMicronHoles()
{
    BasketEntry b{};
    b.diameterMm = 58;
    b.doseMinG = 18;
    b.doseMaxG = 20;
    b.holeCount = 0;
    b.holeDiameterMicrons = 300;
    const QString um = QString(QChar(0x00B5)) + QStringLiteral("m");  // "µm"
    QVERIFY2(summary(b).endsWith(QStringLiteral("300") + um + QStringLiteral(" holes")),
             qPrintable(summary(b)));
}

void TstBasketAliases::summary_countAndMicrons_rendersBoth()
{
    BasketEntry b{};
    b.diameterMm = 58;
    b.doseMinG = 18;
    b.doseMaxG = 21;
    b.holeCount = 641;
    b.holeDiameterMicrons = 300;
    const QString um = QString(QChar(0x00B5)) + QStringLiteral("m");
    QVERIFY2(summary(b).endsWith(QStringLiteral("641 holes @300") + um), qPrintable(summary(b)));
}

void TstBasketAliases::summary_trimsDoseDecimals()
{
    BasketEntry b{};
    b.diameterMm = 58;
    b.doseMinG = 14;
    b.doseMaxG = 16;
    const QString s = summary(b);
    QVERIFY2(s.contains(QStringLiteral("14-16g")), qPrintable(s));
    QVERIFY2(!s.contains(QStringLiteral(".0")), qPrintable(s));
}

void TstBasketAliases::summary_fullExample_matchesDocstring()
{
    // The exact rendering the summary() docstring promises for the Pesado HE row.
    BasketEntry b{};
    b.diameterMm = 58;
    b.wall = WallProfile::Straight;
    b.precision = true;
    b.flow = FlowRate::Open;
    b.doseMinG = 17;
    b.doseMaxG = 19;
    b.holeCount = 715;
    b.holeDiameterMicrons = 300;
    const QString um = QString(QChar(0x00B5)) + QStringLiteral("m");
    QCOMPARE(summary(b),
             QStringLiteral("58mm, straight-wall, precision, open flow, 17-19g, 715 holes @300") + um);
}

// ---- brand/model surface --------------------------------------------------

void TstBasketAliases::allBrands_uniqueAndSorted()
{
    const QStringList brands = allBrands();
    QVERIFY(!brands.isEmpty());

    // Unique
    QCOMPARE(QSet<QString>(brands.cbegin(), brands.cend()).size(), brands.size());

    // Sorted case-insensitively (the order allBrands() promises)
    QStringList sortedCopy = brands;
    sortedCopy.sort(Qt::CaseInsensitive);
    QCOMPARE(brands, sortedCopy);
}

void TstBasketAliases::findEntryByAlias_roundTrips()
{
    const BasketEntry* e = findEntryByAlias(QStringLiteral("decent stock"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->brand, QStringLiteral("Decent"));
    QCOMPARE(e->model, QStringLiteral("18g Ridgeless"));

    // Composition contract: findEntryByAlias agrees with lookup().
    const auto r = lookup(QStringLiteral("decent stock"));
    QVERIFY(r.found);
    QCOMPARE(e->brand, r.brand);
    QCOMPARE(e->model, r.model);
}

QTEST_APPLESS_MAIN(TstBasketAliases)

#include "tst_basketaliases.moc"
