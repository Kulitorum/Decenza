#include <QtTest>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "core/settings_dye.h"
#include "history/coffeebagstorage.h"
#include "history/shothistorystorage.h"
#include "network/baglinkcontroller.h"
#include "network/beanbaseclient.h"
#include "shotrowfixtures.h"

class tst_BagLinkController : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_temp;

    QString freshDatabase()
    {
        const QString path = m_temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                              + QStringLiteral(".sqlite"));
        ShotHistoryStorage schema;
        if (!schema.initialize(path))
            return {};
        schema.close();
        QElapsedTimer timer;
        timer.start();
        while (!schema.isDbWorkIdle() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        if (!schema.isDbWorkIdle())
            return {};
        return path;
    }

    qint64 insertBag(const QString& path, const QString& canonicalId, const QString& blob)
    {
        qint64 id = -1;
        ShotRowFixtures::withRawDb(path, "baglink_setup", [&](QSqlDatabase& db) {
            CoffeeBag bag;
            bag.beanBaseId = canonicalId;
            bag.beanBaseData = blob;
            bag.roasterName = QStringLiteral("Roaster");
            bag.coffeeName = QStringLiteral("Coffee");
            id = CoffeeBagStorage::insertBagStatic(db, bag);
        });
        return id;
    }

    QString linkRequestId(qint64 bagId, const QString& canonicalId, const QString& url)
    {
        const QString imageKey = BeanBaseClient::imageKeyFor(bagId, canonicalId);
        const QByteArray material = QString::number(bagId).toUtf8() + '\n'
            + imageKey.toUtf8() + '\n' + url.toUtf8();
        return QStringLiteral("baglink-")
            + QString::fromLatin1(QCryptographicHash::hash(material,
                QCryptographicHash::Sha256).toHex());
    }

    QVariantMap readBag(CoffeeBagStorage& storage, qint64 id)
    {
        bool done = false;
        QVariantMap row;
        storage.requestBagWithCompletion(id, this,
            [&](qint64, bool success, const QVariantMap& bag, const QString&) {
                if (success)
                    row = bag;
                done = true;
            });
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 5000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        return row;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void inspectionReportsStorageFailuresInsteadOfSuccess()
    {
        CoffeeBagStorage storage;
        QNetworkAccessManager network;
        BeanBaseClient beanBase(&network, nullptr);
        BagLinkController controller(&storage, &beanBase, nullptr, nullptr);
        QSignalSpy finished(&controller, &BagLinkController::inspectionFinished);

        controller.inspectBag(9, QStringLiteral("request-read-failure"));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("request-read-failure"));
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("databaseUnavailable"));
        QVERIFY(!controller.refreshVisualizerBags());
    }

    void duplicateInspectionSharesOnePendingValidation()
    {
        const QString path = freshDatabase();
        QVERIFY(!path.isEmpty());
        const QString canonicalId = QStringLiteral("canonical-test-id");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QString link = QStringLiteral("http://127.0.0.1:%1/coffee").arg(server.serverPort());
        const QString blob = QStringLiteral("{\"link\":\"%1\"}").arg(link);
        const qint64 bagId = insertBag(path, canonicalId, blob);
        QVERIFY(bagId > 0);

        CoffeeBagStorage storage;
        storage.initialize(path);
        QNetworkAccessManager network;
        BeanBaseClient beanBase(&network, nullptr);
        SettingsDye dye;
        BagLinkController controller(&storage, &beanBase, &dye, nullptr);
        QSignalSpy inspected(&controller, &BagLinkController::inspectionFinished);
        QSignalSpy changed(&storage, &CoffeeBagStorage::bagsChanged);
        QList<QTcpSocket*> heldSockets;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (server.hasPendingConnections())
                heldSockets.append(server.nextPendingConnection());
        });

        controller.inspectBag(bagId, QStringLiteral("first"));
        controller.inspectBag(bagId, QStringLiteral("duplicate"));
        QTRY_COMPARE_WITH_TIMEOUT(inspected.count(), 2, 5000);
        QVERIFY(inspected.at(0).at(1).toBool());
        QVERIFY(inspected.at(1).at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!heldSockets.isEmpty(), 5000);

        const QString token = linkRequestId(bagId, canonicalId, link);
        beanBase.bagLinkResolved(token, link);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);
        const QVariantMap row = readBag(storage, bagId);
        const QJsonObject updated = QJsonDocument::fromJson(
            row.value(QStringLiteral("beanBaseData")).toString().toUtf8()).object();
        QVERIFY(updated.value(QStringLiteral("linkChecked")).toBool());
        QCOMPARE(updated.value(QStringLiteral("link")).toString(), link);

        qDeleteAll(heldSockets);
        heldSockets.clear();
        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);
    }

    void lateValidationDoesNotReplaceEditedProductUrl()
    {
        const QString path = freshDatabase();
        QVERIFY(!path.isEmpty());
        const QString canonicalId = QStringLiteral("canonical-stale-test");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QString oldLink = QStringLiteral("http://127.0.0.1:%1/old").arg(server.serverPort());
        const QString newLink = QStringLiteral("https://roaster.example/new");
        const qint64 bagId = insertBag(path, canonicalId,
            QStringLiteral("{\"link\":\"%1\"}").arg(oldLink));
        QVERIFY(bagId > 0);

        CoffeeBagStorage storage;
        storage.initialize(path);
        QNetworkAccessManager network;
        BeanBaseClient beanBase(&network, nullptr);
        SettingsDye dye;
        BagLinkController controller(&storage, &beanBase, &dye, nullptr);
        QSignalSpy inspected(&controller, &BagLinkController::inspectionFinished);
        QSignalSpy bagUpdated(&storage, &CoffeeBagStorage::bagUpdated);
        QSignalSpy updateFinished(&storage, &CoffeeBagStorage::bagUpdateFinished);
        QList<QTcpSocket*> heldSockets;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (server.hasPendingConnections())
                heldSockets.append(server.nextPendingConnection());
        });

        controller.inspectBag(bagId, QStringLiteral("stale-result"));
        QTRY_COMPARE_WITH_TIMEOUT(inspected.count(), 1, 5000);
        QVERIFY(inspected.at(0).at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!heldSockets.isEmpty(), 5000);

        const QString editedBlob = QStringLiteral("{\"link\":\"%1\",\"custom\":\"new\"}")
            .arg(newLink);
        storage.requestUpdateBag(bagId, {{QStringLiteral("beanBaseData"), editedBlob}});
        QTRY_COMPARE_WITH_TIMEOUT(bagUpdated.count(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(updateFinished.count(), 1, 5000);

        beanBase.bagLinkResolved(linkRequestId(bagId, canonicalId, oldLink), oldLink);
        QTRY_COMPARE_WITH_TIMEOUT(updateFinished.count(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(storage.isDbWorkIdle(), 5000);
        const QVariantMap row = readBag(storage, bagId);
        QCOMPARE(row.value(QStringLiteral("beanBaseData")).toString(), editedBlob);
        qDeleteAll(heldSockets);
        heldSockets.clear();
    }
};

QTEST_GUILESS_MAIN(tst_BagLinkController)
#include "tst_baglinkcontroller.moc"
