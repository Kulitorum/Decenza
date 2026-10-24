#include "remote/flowcalibrationservice.h"
#include "rep_flowcalibrationservice_source.h"
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QScopedPointer>
#include <QSignalSpy>

#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>

#include "core/settings.h"
#include "history/shothistorystorage.h"
#include "history/shothistory_types.h"
#include "models/flowcalibrationmodel.h"
#include "remote/flowcalibrationservicesource.h"

class PendingFlowCalibrationServiceSource final : public FlowCalibrationStateSimpleSource
{
public:
    QStringList requests;
    bool replyAutomatically = true;
    PendingFlowCalibrationServiceSource()
    {
        setReady(true);
    }
    void refresh() override {}
    void loadRecentShots(QString) override {}
    void loadShot(QString, int, quint64) override {}
    void save(QString id, double, qint64) override
    {
        requests.append(id);
        if (replyAutomatically) emit operationCompleted(id, QStringLiteral("save"), true, {}, {});
    }
};

class FlowCalibrationServiceTest final : public QObject
{
    Q_OBJECT
private:
    static void drain()
    {
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }

    static ShotRecord shot(const QString& uuid, qint64 timestamp, const QString& profile)
    {
        ShotRecord record;
        record.summary.uuid = uuid;
        record.summary.timestamp = timestamp;
        record.summary.profileName = profile;
        record.summary.beverageType = QStringLiteral("espresso");
        record.flow = {{0.0, 2.0}, {1.0, 3.0}};
        record.weightFlowRate = {{0.0, 1.0}, {1.0, 1.2}};
        record.pressure = {{0.0, 6.0}, {1.0, 7.0}};
        return record;
    }

    static bool deleteShotRow(const QString& path, qint64 id)
    {
        const QString connectionName = QStringLiteral("flowcalibration-test-delete-%1")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool deleted = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(path);
            if (db.open()) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral("DELETE FROM shots WHERE id = ?"));
                query.addBindValue(id);
                deleted = query.exec() && query.numRowsAffected() == 1;
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        return deleted;
    }

private slots:
    void queuedReplyFromRetiredReplicaCannotCompleteAReusedId()
    {
        PendingFlowCalibrationServiceSource firstSource, secondSource;
        firstSource.replyAutomatically = secondSource.replyAutomatically = false;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<FlowCalibrationStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<FlowCalibrationStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        QScopedPointer<FlowCalibrationStateReplica> warmed(secondClient.acquire<FlowCalibrationStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        FlowCalibrationService* service = FlowCalibrationService::create(&engine);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &FlowCalibrationService::operationCompleted);
        const QString id = QStringLiteral("reused-request");
        QCOMPARE(service->save(1.2, 1, id), id);
        QTRY_COMPARE(firstSource.requests, QStringList{id});
        FlowCalibrationStateReplica* origin = nullptr;
        for (auto* replica : service->findChildren<FlowCalibrationStateReplica*>())
            if (replica->isReplicaValid()) origin = replica;
        QVERIFY(origin);
        // Queue the notification on the shipping handler before replacing its
        // replica. The current admission must survive that already queued reply.
        emit origin->operationCompleted(id, QStringLiteral("save"), true, {}, {});
        service->setNode(&secondClient);
        QVERIFY(service->ready());
        QCOMPARE(service->save(1.2, 1, id), id);
        QTRY_COMPARE(secondSource.requests, QStringList{id});
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
        QVERIFY(service->busy());
        emit secondSource.operationCompleted(id, QStringLiteral("save"), true, {}, {});
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(!service->busy());
    }

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
        PendingFlowCalibrationServiceSource firstSource, secondSource;
        QRemoteObjectHost firstHost, secondHost;
        QVERIFY(firstHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(secondHost.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(firstHost.enableRemoting<FlowCalibrationStateSourceAPI>(&firstSource));
        QVERIFY(secondHost.enableRemoting<FlowCalibrationStateSourceAPI>(&secondSource));
        QRemoteObjectNode firstClient, secondClient;
        QVERIFY(firstClient.connectToNode(firstHost.hostUrl()));
        QVERIFY(secondClient.connectToNode(secondHost.hostUrl()));
        // Prepare a ready replacement before request admission.
        QScopedPointer<FlowCalibrationStateReplica> warmed(secondClient.acquire<FlowCalibrationStateReplica>());
        QTRY_VERIFY(warmed->isReplicaValid());
        QQmlEngine engine;
        QPointer<FlowCalibrationService> service = FlowCalibrationService::create(&engine);
        service->setNode(&firstClient);
        QTRY_VERIFY(service->ready());
        QSignalSpy completed(service, &FlowCalibrationService::operationCompleted);
        bool transitioned = false;
        connect(service, &FlowCalibrationService::busyChanged, this, [&] {
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
        const QString retiredId = service->save(1.2, 1);
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
        const QString freshId = service->save(1.2, 1);
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), freshId);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(firstSource.requests.isEmpty());
        QCOMPARE(secondSource.requests, QStringList{freshId});
        QVERIFY(!service->busy());
    }

    void init() { QTest::failOnWarning(); }

    void staleDraftRevisionCannotOverwriteAnotherClientsCalibration()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString dbPath = directory.filePath(QStringLiteral("flow-calibration-stale-save.db"));

        Settings settings;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));
        FlowCalibrationModel model;
        model.setStorage(&storage);
        model.setSettings(settings.calibration());
        NativeFlowCalibrationSource source(&model, settings.calibration());
        QSignalSpy completed(&source, &FlowCalibrationStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        const qint64 draftRevision = source.calibrationRevision();
        source.save(QStringLiteral("first-client-save"), 1.2, draftRevision);
        QCOMPARE(completed.size(), 1);
        QVERIFY(completed.at(0).at(2).toBool());
        QCOMPARE(settings.calibration()->flowCalibrationMultiplier(), 1.2);

        // A second page still holding the original draft revision must conflict,
        // leaving the newer host value intact.
        source.save(QStringLiteral("stale-client-save"), 1.1, draftRevision);
        QCOMPARE(completed.size(), 2);
        QCOMPARE(completed.at(1).at(0).toString(), QStringLiteral("stale-client-save"));
        QVERIFY(!completed.at(1).at(2).toBool());
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("conflict"));
        QCOMPARE(settings.calibration()->flowCalibrationMultiplier(), 1.2);

        storage.close();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 5000);
        drain();
    }

    void failedNavigationClearsSelectedShotAndCompletesAsFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString dbPath = directory.filePath(QStringLiteral("flow-calibration.db"));

        Settings settings;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));
        const qint64 newer = storage.importShotRecord(
            shot(QStringLiteral("newer-shot"), 1751000000, QStringLiteral("Recent")), false);
        const qint64 older = storage.importShotRecord(
            shot(QStringLiteral("older-shot"), 1750990000, QStringLiteral("Older")), false);
        QVERIFY(newer > 0);
        QVERIFY(older > 0);

        FlowCalibrationModel model;
        model.setStorage(&storage);
        model.setSettings(settings.calibration());
        NativeFlowCalibrationSource source(&model, settings.calibration());
        QSignalSpy completed(&source, &FlowCalibrationStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        source.loadRecentShots(QStringLiteral("recent-1"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        QCOMPARE(completed.at(0).at(0).toString(), QStringLiteral("recent-1"));
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("loadRecentShots"));
        QVERIFY(completed.at(0).at(2).toBool());
        QVERIFY(model.hasData());
        QCOMPARE(model.shotCount(), 2);
        QVERIFY(!model.originalFlowData().isEmpty());

        QVERIFY(deleteShotRow(dbPath, older));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            "^\\[Storage\\]\\[ShotHistoryStorage\\] operation=loadShotRecord "
            "requestedBy=.* shotId=\\s*2 result=missingRow$"));
        source.loadShot(QStringLiteral("missing-2"), 1, model.shotListRevision());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 5000);
        const QList<QVariant> failure = completed.at(1);
        QCOMPARE(failure.at(0).toString(), QStringLiteral("missing-2"));
        QCOMPARE(failure.at(1).toString(), QStringLiteral("loadShot"));
        QVERIFY(!failure.at(2).toBool());
        QVERIFY(!failure.at(3).toMap().value(QStringLiteral("hasData")).toBool());
        QVERIFY(failure.at(3).toMap().value(QStringLiteral("originalFlowData")).toList().isEmpty());
        QVERIFY(!failure.at(4).toString().isEmpty());
        QVERIFY(!model.hasData());
        QVERIFY(model.originalFlowData().isEmpty());

        // A client with already loaded curves can still save its calibration
        // while another client's failed navigation has cleared the host model.
        source.save(QStringLiteral("save-after-other-read"), 1.1, source.calibrationRevision());
        QCOMPARE(completed.size(), 3);
        QVERIFY(completed.at(2).at(2).toBool());
        QCOMPARE(settings.calibration()->flowCalibrationMultiplier(), 1.1);

        storage.close();
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 5000);
        drain();
    }
};

QTEST_GUILESS_MAIN(FlowCalibrationServiceTest)
#include "tst_flowcalibrationservice.moc"
