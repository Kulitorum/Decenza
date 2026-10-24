#include "remote/updateoperationpolicy.h"

#include <QtTest>

class tst_UpdateOperationPolicy final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void onlyKnownHostOperationsAreAccepted()
    {
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("checkAll")));
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("startFirmware")));
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("downloadAndInstall")));
        QVERIFY(!UpdateOperationPolicy::supported(QStringLiteral("fetchUrl")));
        QVERIFY(!UpdateOperationPolicy::supported(QStringLiteral("writeFile")));
        QVERIFY(!UpdateOperationPolicy::supported(QStringLiteral("")));
    }

    void operationArgumentsAreClosedAndTyped()
    {
        QVERIFY(UpdateOperationPolicy::validArguments(QStringLiteral("checkAll"), {}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("checkAll"),
                                                       {{QStringLiteral("url"), QStringLiteral("https://example.invalid")}}));
        QVERIFY(UpdateOperationPolicy::validArguments(QStringLiteral("setTranslationUploadEnabled"),
                                                      {{QStringLiteral("enabled"), true}}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("setTranslationUploadEnabled"),
                                                       {{QStringLiteral("enabled"), QStringLiteral("true")}}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("setTranslationUploadEnabled"),
                                                       {{QStringLiteral("enabled"), true},
                                                        {QStringLiteral("extra"), 1}}));
    }
};

QTEST_GUILESS_MAIN(tst_UpdateOperationPolicy)
#include "tst_updateoperationpolicy.moc"
