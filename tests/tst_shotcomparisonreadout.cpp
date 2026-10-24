#include <QtTest>
#include <QPointF>
#include "history/shotcomparisonreadout.h"

class TstShotComparisonReadout : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void nearestValuesRetainSeriesPresenceSemantics();
    void missingDerivativeIsDistinctFromNegativeOne();
};

void TstShotComparisonReadout::nearestValuesRetainSeriesPresenceSemantics()
{
    const QVariantList pressure{QPointF(1.0, 4.2), QPointF(2.0, 8.1)};
    const QVariantList flow{QPointF(1.0, 0.0)};
    const QVariantList readout = {
        pressure, flow, QVariantList{}, QVariantList{}, QVariantList{},
        QVariantList{}, QVariantList{}, QVariantList{}, QVariantList{},
        QVariantList{}, QVariantList{}
    };
    const QVariantMap atSample = ShotComparisonReadout::valuesAtTime(readout, 1.25);
    QCOMPARE(atSample.value(QStringLiteral("pressure")).toDouble(), 4.2);
    QVERIFY(atSample.value(QStringLiteral("hasPressure")).toBool());
    QVERIFY(atSample.value(QStringLiteral("hasFlow")).toBool());
    QVERIFY(!atSample.value(QStringLiteral("hasTemperature")).toBool());

    const QVariantMap outsideWindow = ShotComparisonReadout::valuesAtTime(readout, 3.1);
    QVERIFY(!outsideWindow.value(QStringLiteral("hasPressure")).toBool());
    QCOMPARE(outsideWindow.value(QStringLiteral("pressure")).toDouble(), -1.0);
}

void TstShotComparisonReadout::missingDerivativeIsDistinctFromNegativeOne()
{
    QVariantList derivative{QPointF(1.0, -1.0)};
    QVariantList series(11);
    series[7] = derivative;
    const QVariantMap actualNegativeOne = ShotComparisonReadout::valuesAtTime(series, 1.0);
    QVERIFY(actualNegativeOne.value(QStringLiteral("hasConductanceDerivative")).toBool());
    QCOMPARE(actualNegativeOne.value(QStringLiteral("conductanceDerivative")).toDouble(), -1.0);

    series[7] = {};
    const QVariantMap absent = ShotComparisonReadout::valuesAtTime(series, 1.0);
    QVERIFY(!absent.value(QStringLiteral("hasConductanceDerivative")).toBool());
}

QTEST_GUILESS_MAIN(TstShotComparisonReadout)
#include "tst_shotcomparisonreadout.moc"
