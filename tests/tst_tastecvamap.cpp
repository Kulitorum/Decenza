// Unit tests for the taste-tap → Visualizer CVA mapping (add-ai-taste-intake).
// applyTasteCvaMapping is a pure, dependency-free function, so these assert the
// value table and — most importantly — the never-null / no-fabrication invariant
// that protects hand-entered CVA scores from being clobbered on upload.

#include <QtTest>
#include <QJsonObject>

#include "network/tastecvamap.h"

class tst_TasteCvaMap : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void extractionMapsToAcidityBitterness() {
        { QJsonObject o; applyTasteCvaMapping(o, "sour", "");
          QCOMPARE(o["acidity"].toInt(), 12); QCOMPARE(o["bitterness"].toInt(), 4); }
        { QJsonObject o; applyTasteCvaMapping(o, "balanced", "");
          QCOMPARE(o["acidity"].toInt(), 8);  QCOMPARE(o["bitterness"].toInt(), 8); }
        { QJsonObject o; applyTasteCvaMapping(o, "bitter", "");
          QCOMPARE(o["acidity"].toInt(), 4);  QCOMPARE(o["bitterness"].toInt(), 12); }
    }

    void bodyMapsToMouthfeel() {
        { QJsonObject o; applyTasteCvaMapping(o, "", "thin");   QCOMPARE(o["mouthfeel"].toInt(), 4); }
        { QJsonObject o; applyTasteCvaMapping(o, "", "medium"); QCOMPARE(o["mouthfeel"].toInt(), 8); }
        { QJsonObject o; applyTasteCvaMapping(o, "", "heavy");  QCOMPARE(o["mouthfeel"].toInt(), 12); }
    }

    // The load-bearing invariant: an unset (or unknown) tap writes NOTHING — not a
    // value, not null — so an ordinary PATCH on an untapped shot never clears a
    // CVA score the user entered by hand in Visualizer.
    void unsetTapsWriteNothing() {
        QJsonObject o;
        applyTasteCvaMapping(o, "", "");
        QVERIFY2(o.isEmpty(), "unset taps must not add any key (not even null)");

        QJsonObject o2;
        applyTasteCvaMapping(o2, "garbage", "nonsense");  // unknown → no arm matches
        QVERIFY2(o2.isEmpty(), "out-of-set taps must not add any key");
    }

    // The five CVA attributes the taps don't speak to are never fabricated.
    void untouchedAttributesNeverSet() {
        QJsonObject o;
        applyTasteCvaMapping(o, "sour", "heavy");
        for (const QString& k : {QStringLiteral("sweetness"), QStringLiteral("aftertaste"),
                                 QStringLiteral("aroma"), QStringLiteral("flavor"),
                                 QStringLiteral("fragrance")})
            QVERIFY2(!o.contains(k), qPrintable("must not set " + k));
    }
};

QTEST_APPLESS_MAIN(tst_TasteCvaMap)

#include "tst_tastecvamap.moc"
