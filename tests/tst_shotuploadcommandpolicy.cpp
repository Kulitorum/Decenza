#include <QtTest>

#include "shotuploadcommandpolicy.h"

class ShotUploadCommandPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void allowsOnlyBoundedHostCommands()
    {
        ShotUploadCommandPolicy::Command command;
        QString reason;
        QVERIFY(ShotUploadCommandPolicy::decode(
            QStringLiteral("connectVisualizer"),
            {{QStringLiteral("identity"), QStringLiteral("user@example.test")},
             {QStringLiteral("password"), QStringLiteral("one-shot")}},
            &command, &reason));
        QCOMPARE(command.operation, QStringLiteral("connectVisualizer"));
        QVERIFY(reason.isEmpty());

        QVERIFY(!ShotUploadCommandPolicy::decode(
            QStringLiteral("uploadMissing"),
            {{QStringLiteral("destination"), QStringLiteral("https://attacker.invalid")}},
            &command, &reason));
        QCOMPARE(reason, QStringLiteral("invalidDestination"));
        QVERIFY(!ShotUploadCommandPolicy::decode(
            QStringLiteral("linkDecent"),
            {{QStringLiteral("email"), QStringLiteral("user@example.test")},
             {QStringLiteral("password"), QString(513, QLatin1Char('x'))}},
            &command, &reason));
        QCOMPARE(reason, QStringLiteral("invalidArguments"));
        QVERIFY(ShotUploadCommandPolicy::decode(
            QStringLiteral("chooseDecentMachine"),
            {{QStringLiteral("serial"), QStringLiteral("123456")}}, &command, &reason));
        QVERIFY(!ShotUploadCommandPolicy::decode(
            QStringLiteral("chooseDecentMachine"),
            {{QStringLiteral("serial"), QStringLiteral("../attacker")}}, &command, &reason));
        QVERIFY(!ShotUploadCommandPolicy::decode(
            QStringLiteral("disconnectVisualizer"),
            {{QStringLiteral("password"), QStringLiteral("unexpected")}},
            &command, &reason));
    }

    void recoveryRangeRequiresWholeEpochSecondsAndBoundedWindow()
    {
        ShotUploadCommandPolicy::Command command;
        QString reason;
        const QVariantMap valid{{QStringLiteral("fromEpoch"), 1792540800.0},
                                {QStringLiteral("toEpoch"), 1793145599.0}};
        QVERIFY(ShotUploadCommandPolicy::decode(QStringLiteral("recoverVisualizer"),
                                                valid, &command, &reason));
        auto invalid = valid;
        invalid[QStringLiteral("toEpoch")] = 1792540800.5;
        QVERIFY(!ShotUploadCommandPolicy::decode(QStringLiteral("recoverVisualizer"),
                                                 invalid, &command, &reason));
        invalid = {{QStringLiteral("fromEpoch"), 1793145599LL},
                   {QStringLiteral("toEpoch"), 1792540800LL}};
        QVERIFY(!ShotUploadCommandPolicy::decode(QStringLiteral("recoverVisualizer"),
                                                 invalid, &command, &reason));
    }

    void completionMustMatchNonemptyOwnerAndOperation()
    {
        QVERIFY(ShotUploadCommandPolicy::validCompletionOwner(
            QStringLiteral("request-1"), QStringLiteral("linkDecent"),
            QStringLiteral("request-1"), QStringLiteral("linkDecent")));
        QVERIFY(!ShotUploadCommandPolicy::validCompletionOwner({}, {}, {}, {}));
        QVERIFY(!ShotUploadCommandPolicy::validCompletionOwner(
            QStringLiteral("request-2"), QStringLiteral("linkDecent"),
            QStringLiteral("request-1"), QStringLiteral("linkDecent")));
        QVERIFY(!ShotUploadCommandPolicy::validCompletionOwner(
            QStringLiteral("request-1"), QStringLiteral("unlinkDecent"),
            QStringLiteral("request-1"), QStringLiteral("linkDecent")));
    }
};

QTEST_GUILESS_MAIN(ShotUploadCommandPolicyTest)
#include "tst_shotuploadcommandpolicy.moc"
