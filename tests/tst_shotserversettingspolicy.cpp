#include "remote/shotserversettingsservice.h"
#include "rep_shotserversettingsservice_source.h"
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QScopedPointer>
#include <QSignalSpy>

#include "remote/shotserversettingspolicy.h"

#include <QTest>

#include <QHash>

class PendingShotServerSettingsServiceSource final : public ShotServerSettingsStateSimpleSource
{
public:
    QStringList requests;
    PendingShotServerSettingsServiceSource()
    {
        setReady(true);
    }
    void refresh() override {}
    QVariantMap executePrivate(QString id, QString, QVariantMap) override
    {
        requests.append(id);
        return {{QStringLiteral("success"), true}};
    }
};

class tst_ShotServerSettingsPolicy final : public QObject
{
    Q_OBJECT
private slots:
    void requestAdmissionSurvivesSynchronousObservers_data()
    {
        QTest::addColumn<QString>("transition");
        for (const char* transition : {"detach", "rebind", "same-node",
                                       "delete-on-admission", "delete-on-failure"})
            QTest::newRow(transition) << QString::fromLatin1(transition);
    }

    void requestAdmissionSurvivesSynchronousObservers()
    {
        QFETCH(QString, transition);
        PendingShotServerSettingsServiceSource firstSource, secondSource;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<ShotServerSettingsStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<ShotServerSettingsStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        // Prepare a ready replacement before request admission.
        QScopedPointer<ShotServerSettingsStateReplica> warmed(secondClient.acquire<ShotServerSettingsStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        QPointer<ShotServerSettingsService> service = ShotServerSettingsService::create(&engine, nullptr);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &ShotServerSettingsService::operationCompleted);
        bool transitioned = false;
        connect(service, &ShotServerSettingsService::busyChanged, this, [&] {
            if (!service) return;
            if (transition == QLatin1String("delete-on-failure")
                && transitioned && !service->busy()) {
                delete service.data();
                return;
            }
            if (transitioned || !service->busy()) return;
            transitioned = true;
            if (transition == QLatin1String("delete-on-admission")) {
                delete service.data();
            } else if (transition == QLatin1String("same-node")) {
                service->setNode(&firstClient);
            } else {
                service->setNode(transition == QLatin1String("rebind") ? &secondClient : nullptr);
            }
        });
        const QString retiredId = service->execute(QStringLiteral("resetTotpSecret"));
        QVERIFY(transitioned);
        if (transition.startsWith(QLatin1String("delete-"))) {
            QVERIFY(!service);
            QVERIFY(firstSource.requests.isEmpty());
            QVERIFY(secondSource.requests.isEmpty());
            return;
        }
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), retiredId);
        if (transition == QLatin1String("same-node")) {
            QVERIFY(completed.first().at(2).toBool());
            QCOMPARE(firstSource.requests, QStringList{retiredId});
            QVERIFY(secondSource.requests.isEmpty());
            return;
        }
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        service->setNode(&secondClient);
        QTRY_VERIFY(service->ready());
        const QString freshId = service->execute(QStringLiteral("resetTotpSecret"));
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), freshId);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(firstSource.requests.isEmpty());
        QCOMPARE(secondSource.requests, QStringList{freshId});
        QVERIFY(!service->busy());
    }

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
