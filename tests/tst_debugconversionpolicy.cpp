#include "../src/remote/debugconversionpolicy.h"

#include <QtTest>

class DebugConversionPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void bundleIdsMustBeOpaqueUuidShape()
    {
        QVERIFY(DebugConversionPolicy::validOpaqueId(
            QStringLiteral("a1234567-b123-c123-d123-e123456789ab")));
        QVERIFY(!DebugConversionPolicy::validOpaqueId(QStringLiteral("../bundle.zip")));
        QVERIFY(!DebugConversionPolicy::validOpaqueId(QString(36, QLatin1Char('z'))));
    }

    void offsetsMustBeExactBoundedIntegers()
    {
        int parsed = -1;
        QVERIFY(DebugConversionPolicy::validOffset(12, 12, 100, &parsed));
        QCOMPARE(parsed, 12);
        QVERIFY(DebugConversionPolicy::validOffset(12.0, 12, 100, &parsed));
        QVERIFY(!DebugConversionPolicy::validOffset(12.5, 12, 100, &parsed));
        QVERIFY(!DebugConversionPolicy::validOffset(QStringLiteral("12"), 12, 100, &parsed));
        QVERIFY(!DebugConversionPolicy::validOffset(13, 12, 100, &parsed));
        QVERIFY(!DebugConversionPolicy::validOffset(101, 12, 100, &parsed));
    }

    void chunksCannotSkipOrExceedTheBundle()
    {
        QVERIFY(DebugConversionPolicy::validChunk(0, 48 * 1024, 50 * 1024));
        QVERIFY(DebugConversionPolicy::validChunk(48 * 1024, 2 * 1024, 50 * 1024));
        QVERIFY(!DebugConversionPolicy::validChunk(0, 0, 50 * 1024));
        QVERIFY(!DebugConversionPolicy::validChunk(0, 48 * 1024 + 1, 50 * 1024));
        QVERIFY(!DebugConversionPolicy::validChunk(49 * 1024, 2 * 1024, 50 * 1024));
    }
};

QTEST_APPLESS_MAIN(DebugConversionPolicyTest)
#include "tst_debugconversionpolicy.moc"
