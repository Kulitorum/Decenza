#include "remote/shotserversettingspolicy.h"

#include <QTest>

#include <QHash>

class tst_ShotServerSettingsPolicy final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void requestIdsAreCanonical()
    {
        const QString id = QStringLiteral("f439d89b-028c-4b72-a1c2-52a811f351ee");
        QVERIFY(ShotServerSettingsPolicy::validRequestId(id));
        QVERIFY(!ShotServerSettingsPolicy::validRequestId(QStringLiteral("{}" ) + id));
        QVERIFY(!ShotServerSettingsPolicy::validRequestId(QString(2048, QLatin1Char('a'))));
    }
    void commandsAreClosedAndBounded()
    {
        QVERIFY(ShotServerSettingsPolicy::validCommand(QStringLiteral("generateTotpSetup"), {}));
        QVERIFY(!ShotServerSettingsPolicy::validCommand(QStringLiteral("generateTotpSetup"),
                                                        {{QStringLiteral("secret"), QStringLiteral("x")}}));
        const QVariantMap valid{{QStringLiteral("secret"), QStringLiteral("JBSWY3DPEHPK3PXP")},
                                {QStringLiteral("code"), QStringLiteral("123456")}};
        QVERIFY(ShotServerSettingsPolicy::validCommand(QStringLiteral("completeTotpSetup"), valid));
        QVariantMap bad = valid;
        bad.insert(QStringLiteral("code"), QStringLiteral("12345"));
        QVERIFY(!ShotServerSettingsPolicy::validCommand(QStringLiteral("completeTotpSetup"), bad));
        bad = valid;
        bad.insert(QStringLiteral("unexpected"), true);
        QVERIFY(!ShotServerSettingsPolicy::validCommand(QStringLiteral("completeTotpSetup"), bad));
        QVERIFY(!ShotServerSettingsPolicy::validCommand(QStringLiteral("factoryResetAndQuit"), valid));
        QVERIFY(!ShotServerSettingsPolicy::validCommand(QStringLiteral("unknown"), {}));
    }
    void completionsRequireMatchingPendingIdentity()
    {
        const QHash<QString, QString> pending{{QStringLiteral("a"), QStringLiteral("completeTotpSetup")}};
        QVERIFY(ShotServerSettingsPolicy::matchesPending(pending, QStringLiteral("a"),
                                                          QStringLiteral("completeTotpSetup")));
        QVERIFY(!ShotServerSettingsPolicy::matchesPending(pending, QStringLiteral("a"),
                                                           QStringLiteral("resetTotpSecret")));
        QVERIFY(!ShotServerSettingsPolicy::matchesPending(pending, QStringLiteral("absent"), {}));
    }
};

QTEST_GUILESS_MAIN(tst_ShotServerSettingsPolicy)
#include "tst_shotserversettingspolicy.moc"
