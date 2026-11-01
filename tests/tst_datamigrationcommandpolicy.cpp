#include <QtTest>
#include "../src/remote/datamigrationcommandpolicy.h"
#include "core/datamigrationclient.h"
#include "core/appsettings.h"
#include "core/fullbackuprestore.h"
#include "core/settings.h"
#include "core/settings_calibration.h"
#include "core/translationmanager.h"
#include "ai/conversationkey.h"
#include <QJsonArray>
#include "remote/datamigrationservice.h"
#include "remote/datamigrationservicesource.h"
#include <QNetworkAccessManager>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QFile>
#include <QThreadPool>
#include <QRunnable>
#include <QSemaphore>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QtEndian>
#include "shotrowfixtures.h"
#include "history/shothistory_types.h"
#include <memory>

namespace {
class MigrationHttpFixture final : public QObject
{
public:
    struct Response { QByteArray body; int status = 200; bool hold = false; };
    QTcpServer server;
    QHash<QByteArray, Response> responses;
    QList<QByteArray> requests;
    QHash<QByteArray, QList<QPointer<QTcpSocket>>> held;

    MigrationHttpFixture()
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                auto input = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, input] {
                    if (socket->property("requestHandled").toBool()) return;
                    *input += socket->readAll();
                    if (!input->contains("\r\n\r\n")) return;
                    socket->setProperty("requestHandled", true);
                    const QByteArray path = input->split(' ').value(1);
                    requests.append(path);
                    const Response response = responses.value(path, {"{}", 404, false});
                    if (response.hold) held[path].append(socket);
                    else respond(socket, response.body, response.status);
                });
            }
        });
    }
    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    qsizetype count(const QByteArray& path) const { return requests.count(path); }
    void setManifest(int conversationCount = 1)
    {
        responses["/api/backup/manifest"] = {QJsonDocument(QJsonObject{
            {QStringLiteral("deviceName"), QStringLiteral("Disposable peer")},
            {QStringLiteral("appVersion"), QStringLiteral("test")},
            {QStringLiteral("aiConversationCount"), conversationCount},
            {QStringLiteral("aiConversationsSize"), 200000},
            {QStringLiteral("sessionToken"), QStringLiteral("must-not-cross-QtRO")}
        }).toJson(QJsonDocument::Compact), 200, false};
    }
    static void respond(QTcpSocket* socket, const QByteArray& body, int status = 200)
    {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\n"
                      "Content-Type: application/json\r\nConnection: close\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    void release(const QByteArray& path, const QByteArray& body)
    {
        const auto pending = held.take(path);
        for (const auto& socket : pending) if (socket) respond(socket, body);
    }
};

struct MigrationServiceFixture final
{
    QNetworkAccessManager network;
    DataMigrationClient client{&network};
    NativeDataMigrationServiceSource source{&client};
    QRemoteObjectHost host{QUrl(QStringLiteral("tcp://127.0.0.1:0"))};
    QRemoteObjectNode node;
    QQmlEngine engine;
    DataMigrationService* service = nullptr;
    bool initialize()
    {
        DataMigrationService::setPublishedNode(nullptr);
        if (!host.enableRemoting(&source) || !node.connectToNode(host.hostUrl())) return false;
        service = DataMigrationService::create(&engine, nullptr);
        service->setNode(&node);
        return true;
    }
    ~MigrationServiceFixture() { delete service; }
};
}


class DataMigrationCommandPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning();
        AppSettings isolated;
        isolated.clear(); // DECENZA_TESTING uses the PID-scoped temporary INI store.
    }
    void cleanup() { AppSettings isolated; isolated.clear(); }

    void replaceAllUsesValidatedBackupAndReplacesRecipes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath = directory.filePath(QStringLiteral("source.db"));
        const QString destinationPath = directory.filePath(QStringLiteral("destination.db"));
        ShotHistoryStorage source;
        ShotRowFixtures::initAndCloseStorage(sourcePath, source);
        ShotRowFixtures::withRawDb(sourcePath, QStringLiteral("replace_source"), [](QSqlDatabase& db) {
            QSqlQuery query(db);
            QVERIFY(query.exec("INSERT INTO recipes (name, profile_title, drink_type, archived) "
                               "VALUES ('Source', 'P', 'espresso', 0)"));
        });
        ShotHistoryStorage destination;
        QVERIFY(destination.initialize(destinationPath));
        ShotRowFixtures::withRawDb(destinationPath, QStringLiteral("replace_destination"), [](QSqlDatabase& db) {
            QSqlQuery query(db);
            QVERIFY(query.exec("INSERT INTO recipes (name, profile_title, drink_type, archived) "
                               "VALUES ('Old', 'P', 'espresso', 0)"));
        });
        QFile database(sourcePath);
        QVERIFY(database.open(QIODevice::ReadOnly));
        const QByteArray bytes = database.readAll();
        QByteArray archive("DCBK", 4);
        char number[8];
        qToLittleEndian<quint32>(1, reinterpret_cast<uchar*>(number));
        archive.append(number, 4); // version
        qToLittleEndian<quint32>(2, reinterpret_cast<uchar*>(number));
        archive.append(number, 4); // two entries
        const QByteArray name("shots.db");
        qToLittleEndian<quint32>(static_cast<quint32>(name.size()), reinterpret_cast<uchar*>(number));
        archive.append(number, 4);
        archive.append(name);
        qToLittleEndian<quint64>(static_cast<quint64>(bytes.size()), reinterpret_cast<uchar*>(number));
        archive.append(number, 8);
        archive.append(bytes);
        const QByteArray settingsName("settings.json");
        const QByteArray settingsBytes("{\"machineTuning\":{\"flowCalibrationMultiplier\":0.8,\"autoFlowCalibration\":false}}");
        qToLittleEndian<quint32>(static_cast<quint32>(settingsName.size()), reinterpret_cast<uchar*>(number));
        archive.append(number, 4);
        archive.append(settingsName);
        qToLittleEndian<quint64>(static_cast<quint64>(settingsBytes.size()), reinterpret_cast<uchar*>(number));
        archive.append(number, 8);
        archive.append(settingsBytes);

        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest();
        peer.responses["/api/backup/full"] = {archive, 200, false};
        MigrationServiceFixture fixture;
        Settings settings;
        settings.calibration()->setFlowCalibrationMultiplier(1.2);
        settings.calibration()->setAutoFlowCalibration(true);
        FullBackupRestore restore(&settings, &destination, nullptr, nullptr, nullptr, nullptr);
        fixture.client.setFullBackupRestore(&restore);
        bool cancellationBlocked = false;
        connect(&restore, &FullBackupRestore::busyChanged, &fixture.client, [&] {
            if (!restore.busy()) return;
            cancellationBlocked = !fixture.client.canCancelImport();
            fixture.client.cancel();
            cancellationBlocked = cancellationBlocked && fixture.client.isImporting();
        });
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.first().at(2).toBool());
        const QString id = fixture.service->execute(QStringLiteral("importAll"), {{QStringLiteral("replace"), true}});
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 10000);
        QCOMPARE(completed.last().at(0).toString(), id);
        QVERIFY2(completed.last().at(2).toBool(), qPrintable(completed.last().at(4).toString()));
        QVERIFY(completed.last().at(3).toMap().value(QStringLiteral("replaced")).toBool());
        QVERIFY(cancellationBlocked);
        QCOMPARE(settings.calibration()->flowCalibrationMultiplier(), 1.2);
        QVERIFY(settings.calibration()->autoFlowCalibration());
        QCOMPARE(peer.count("/api/backup/full"), 1);
        ShotRowFixtures::withRawDb(destinationPath, QStringLiteral("replace_verify"), [](QSqlDatabase& db) {
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT name FROM recipes"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("Source"));
            QVERIFY(!query.next());
        });
        destination.close();
        QTRY_VERIFY(destination.isDbWorkIdle());
    }

    void replaceDownloadCanBeCancelledBeforeRestore()
    {
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest();
        peer.responses["/api/backup/full"] = {{}, 200, true};
        MigrationServiceFixture fixture;
        FullBackupRestore restore(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        fixture.client.setFullBackupRestore(&restore);
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        const QString id = fixture.service->execute(QStringLiteral("importAll"), {{QStringLiteral("replace"), true}});
        QTRY_COMPARE(peer.count("/api/backup/full"), 1);
        QVERIFY(fixture.client.canCancelImport());
        fixture.service->execute(QStringLiteral("cancel"));
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.at(1).at(0).toString(), id);
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("cancelled"));
        QVERIFY(!restore.busy());
        peer.release("/api/backup/full", "invalid");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!restore.busy());
    }

    void settingsImportTransfersSelectedLanguageCustomizations_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("english") << QStringLiteral("en");
        QTest::newRow("british-english") << QStringLiteral("en-GB");
    }

    void settingsImportTransfersSelectedLanguageCustomizations()
    {
        QFETCH(QString, language);
        QStandardPaths::setTestModeEnabled(true);
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/translations");
        QDir(dir).removeRecursively();
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));
        QNetworkAccessManager network;
        TranslationManager manager(&network, &settings);
        manager.registerString(QStringLiteral("fixture.pitcher"), QStringLiteral("Milk pitcher"));
        manager.registerString(QStringLiteral("fixture.local"), QStringLiteral("Local"));
        manager.setCurrentLanguage(language);
        manager.setTranslation(QStringLiteral("fixture.local"), QStringLiteral("Keep me"));
        manager.setCurrentLanguage(QStringLiteral("en"));

        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.responses["/api/backup/manifest"] = {QJsonDocument(QJsonObject{
            {QStringLiteral("deviceName"), QStringLiteral("Peer")},
            {QStringLiteral("hasSettings"), true}}).toJson(), 200, false};
        peer.responses["/api/backup/settings"] = {"{}", 200, false};
        peer.responses["/api/backup/extra-settings"] = {QJsonDocument(QJsonObject{
            {QStringLiteral("language"), language},
            {QStringLiteral("languageTranslation"), QJsonObject{
                {QStringLiteral("language"), language},
                {QStringLiteral("translations"), QJsonObject{
                    {QStringLiteral("fixture.pitcher"), QStringLiteral("Milk jug")}}}}},
            {QStringLiteral("languageOverrides"), language == QLatin1String("en")
                ? QJsonArray{} : QJsonArray{QStringLiteral("fixture.pitcher")}}}).toJson(), 200, false};
        MigrationServiceFixture fixture;
        fixture.client.setSettings(&settings);
        fixture.client.setTranslationManager(&manager);
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        fixture.service->execute(QStringLiteral("importOnlySettings"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 10000);
        QVERIFY2(completed.last().at(2).toBool(), qPrintable(completed.last().at(4).toString()));
        QCOMPARE(manager.currentLanguage(), language);
        QCOMPARE(manager.translateString(QStringLiteral("fixture.pitcher"), QStringLiteral("Milk pitcher")), QStringLiteral("Milk jug"));
        QCOMPARE(manager.translateString(QStringLiteral("fixture.local"), QStringLiteral("Local")), QStringLiteral("Keep me"));
        if (language != QLatin1String("en")) {
            QFile overrides(dir + QLatin1Char('/') + language + QStringLiteral("_overrides.json"));
            QVERIFY(overrides.open(QIODevice::ReadOnly));
            const QJsonArray keys = QJsonDocument::fromJson(overrides.readAll()).object().value(QStringLiteral("overrides")).toArray();
            QVERIFY(keys.contains(QStringLiteral("fixture.pitcher")));
            QVERIFY(keys.contains(QStringLiteral("fixture.local")));
        }
    }

    void manifestAndImportProgressRoundTripThroughRealQtRO()
    {
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest();
        const QString key = ConversationKey::derive(QStringLiteral("Migration fixture"),
            QStringLiteral("Coffee"), QStringLiteral("Profile"), 0);
        const QString message(150000, QLatin1Char('x'));
        const QJsonArray conversations{QJsonObject{
            {QStringLiteral("key"), key}, {QStringLiteral("beanBrand"), QStringLiteral("Migration fixture")},
            {QStringLiteral("beanType"), QStringLiteral("Coffee")}, {QStringLiteral("profileName"), QStringLiteral("Profile")},
            {QStringLiteral("equipmentId"), 0},
            {QStringLiteral("messages"), QJsonArray{QJsonObject{
                {QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), message}}}}
        }};
        peer.responses["/api/backup/ai-conversations"] = {QJsonDocument(conversations).toJson(QJsonDocument::Compact), 200, false};
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        QSignalSpy progress(fixture.service, &DataMigrationService::operationProgress);
        const QString connectId = fixture.service->execute(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), connectId);
        QVERIFY2(completed.first().at(2).toBool(), qPrintable(completed.first().at(4).toString()));
        QTRY_COMPARE(fixture.service->snapshot().value(QStringLiteral("manifest")).toMap()
                         .value(QStringLiteral("deviceName")).toString(), QStringLiteral("Disposable peer"));
        const QVariantMap manifest = fixture.service->snapshot().value(QStringLiteral("manifest")).toMap();
        QCOMPARE(manifest.value(QStringLiteral("aiConversationCount")).toInt(), 1);
        QVERIFY(!manifest.contains(QStringLiteral("sessionToken")));
        QVERIFY(!manifest.contains(QStringLiteral("aiConversationsSize")));
        QCOMPARE(peer.count("/api/backup/manifest"), 1);
        const QString importId = fixture.service->execute(QStringLiteral("importOnlyAIConversations"));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), importId);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(3).toMap().value(QStringLiteral("aiConversationsImported")).toInt(), 1);
        AppSettings isolated;
        const QJsonArray stored = QJsonDocument::fromJson(isolated.value(
            QStringLiteral("ai/conversations/") + key + QStringLiteral("/messages")).toByteArray()).array();
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().toObject().value(QStringLiteral("content")).toString(), message);
        QVERIFY(!fixture.service->busy());
        QVERIFY(!fixture.client.isImporting());
        QVERIFY(!progress.isEmpty());
        bool transferred = false;
        for (const auto& event : progress) {
            QCOMPARE(event.at(0).toString(), importId);
            const double value = event.at(1).toDouble();
            QVERIFY(value >= 0.0 && value <= 1.0);
            if (value > 0.0 && value < 1.0) transferred = true;
        }
        QVERIFY(transferred);
        QCOMPARE(fixture.client.progress(), 1.0);
        QCOMPARE(peer.count("/api/backup/ai-conversations"), 1);
        const QString duplicate = fixture.service->execute(QStringLiteral("importOnlyAIConversations"));
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), duplicate);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(3).toMap().value(QStringLiteral("aiConversationsImported")).toInt(), 0);
        QCOMPARE(QJsonDocument::fromJson(isolated.value(QStringLiteral("ai/conversations/index")).toByteArray()).array().size(), 1);
    }

    void failedImportHasOneCorrelatedTerminalAndCanRetry()
    {
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest();
        peer.responses["/api/backup/ai-conversations"] = {"{\"not\":\"an array\"}", 200, false};
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        QSignalSpy failed(&fixture.client, &DataMigrationClient::importFailed);
        QSignalSpy succeeded(&fixture.client, &DataMigrationClient::importComplete);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.first().at(2).toBool());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*The other device did not return a usable list.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*AI conversations payload was not a JSON array.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*import finished with errors.*"));
        const QString id = fixture.service->execute(QStringLiteral("importOnlyAIConversations"));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), id);
        QVERIFY(!completed.last().at(2).toBool());
        QVERIFY(!completed.last().at(4).toString().isEmpty());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(succeeded.count(), 0);
        QVERIFY(!fixture.service->busy());
        QVERIFY(!fixture.client.isImporting());
        peer.responses["/api/backup/ai-conversations"] = {"[]", 200, false};
        const QString retry = fixture.service->execute(QStringLiteral("importOnlyAIConversations"));
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), retry);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(succeeded.count(), 1);
        QCOMPARE(peer.count("/api/backup/ai-conversations"), 2);
    }

    void httpAndMalformedPayloadFailuresCannotReportSuccessfulImport_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QByteArray>("path");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("detailWarning");
        QTest::newRow("settings-http-500") << QStringLiteral("importOnlySettings") << QByteArray("/api/backup/settings")
            << QByteArray("{}") << 500 << QStringLiteral("Failed to import settings");
        QTest::newRow("settings-malformed") << QStringLiteral("importOnlySettings") << QByteArray("/api/backup/settings")
            << QByteArray("[]") << 200 << QString();
        QTest::newRow("profiles-http-500") << QStringLiteral("importOnlyProfiles") << QByteArray("/api/backup/profiles")
            << QByteArray("{}") << 500 << QStringLiteral("Failed to fetch profile list");
        QTest::newRow("profiles-malformed") << QStringLiteral("importOnlyProfiles") << QByteArray("/api/backup/profiles")
            << QByteArray("{}") << 200 << QStringLiteral("Invalid profile list response");
        QTest::newRow("shots-http-500") << QStringLiteral("importOnlyShots") << QByteArray("/api/backup/shots")
            << QByteArray("{}") << 500 << QStringLiteral("Failed to import shots");
        QTest::newRow("media-http-500") << QStringLiteral("importOnlyMedia") << QByteArray("/api/backup/media")
            << QByteArray("{}") << 500 << QStringLiteral("Failed to fetch media list");
        QTest::newRow("media-malformed") << QStringLiteral("importOnlyMedia") << QByteArray("/api/backup/media")
            << QByteArray("{}") << 200 << QStringLiteral("Invalid media list response");
        QTest::newRow("conversations-http-500") << QStringLiteral("importOnlyAIConversations")
            << QByteArray("/api/backup/ai-conversations") << QByteArray("{}") << 500
            << QStringLiteral("Failed to import AI conversations");
        QTest::newRow("profile-file-http-500") << QStringLiteral("importOnlyProfiles")
            << QByteArray("/api/backup/profile/user/example.json") << QByteArray("{}") << 500
            << QStringLiteral("Failed to download profile");
        QTest::newRow("media-file-http-500") << QStringLiteral("importOnlyMedia")
            << QByteArray("/api/backup/media/example.mp4") << QByteArray("{}") << 500
            << QStringLiteral("Failed to download media");
    }

    void httpAndMalformedPayloadFailuresCannotReportSuccessfulImport()
    {
        QFETCH(QString, operation);
        QFETCH(QByteArray, path);
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(QString, detailWarning);
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.responses["/api/backup/manifest"] = {QJsonDocument(QJsonObject{
            {QStringLiteral("deviceName"), QStringLiteral("Failure fixture")},
            {QStringLiteral("hasSettings"), true}, {QStringLiteral("profileCount"), 1},
            {QStringLiteral("shotCount"), 1}, {QStringLiteral("mediaCount"), 1},
            {QStringLiteral("aiConversationCount"), 1}
        }).toJson(QJsonDocument::Compact), 200, false};
        peer.responses["/api/backup/profiles"] = {"[{\"category\":\"user\",\"filename\":\"example.json\",\"size\":2}]", 200, false};
        peer.responses["/api/backup/media"] = {"[{\"filename\":\"example.mp4\",\"size\":2}]", 200, false};
        peer.responses[path] = {body, status, false};
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        QSignalSpy failed(&fixture.client, &DataMigrationClient::importFailed);
        QSignalSpy succeeded(&fixture.client, &DataMigrationClient::importComplete);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.first().at(2).toBool());
        if (!detailWarning.isEmpty())
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*" + QRegularExpression::escape(detailWarning) + ".*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*(could not|did not return).*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*import finished with errors.*"));
        const QString id = fixture.service->execute(operation);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), id);
        QCOMPARE(completed.last().at(1).toString(), operation);
        QVERIFY(!completed.last().at(2).toBool());
        QVERIFY(!completed.last().at(4).toString().isEmpty());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(succeeded.count(), 0);
        QVERIFY(!fixture.client.isImporting());
        QVERIFY(!fixture.service->busy());
        QCOMPARE(peer.count(path), 1);
        if (operation == QLatin1String("importOnlySettings"))
            QCOMPARE(peer.count("/api/backup/extra-settings"), 1); // Optional older-peer 404 still advances.
    }

    void failedNewConnectionClearsThePreviousManifestAndRefusesImport()
    {
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest();
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!fixture.client.manifest().isEmpty());
        peer.responses["/api/backup/manifest"] = {"not JSON", 200, false};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Invalid response from server.*"));
        const QString reconnect = fixture.service->execute(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), reconnect);
        QVERIFY(!completed.last().at(2).toBool());
        QVERIFY(fixture.client.manifest().isEmpty());
        const QString importId = fixture.service->execute(QStringLiteral("importAll"));
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), importId);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("notConnected"));
        QVERIFY(!fixture.service->busy());
        QCOMPARE(peer.requests.size(), 2);
    }

    void cancelRetiresHeldImportAndReconnectDoesNotReplayIt()
    {
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest();
        peer.responses["/api/backup/ai-conversations"] = {{}, 200, true};
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        const QString pending = fixture.service->execute(QStringLiteral("importOnlyAIConversations"));
        QTRY_COMPARE(peer.count("/api/backup/ai-conversations"), 1);
        QVERIFY(fixture.client.isImporting());
        const QString cancelId = fixture.service->execute(QStringLiteral("cancel"));
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.at(1).at(0).toString(), pending);
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("cancelled"));
        QCOMPARE(completed.at(2).at(0).toString(), cancelId);
        QVERIFY(completed.at(2).at(2).toBool());
        QVERIFY(!fixture.client.isImporting());
        QVERIFY(!fixture.service->busy());
        peer.release("/api/backup/ai-conversations", "[]");
        fixture.service->setNode(nullptr);
        fixture.service->setNode(&fixture.node);
        QTRY_VERIFY(fixture.service->ready());
        fixture.service->refresh();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(completed.count(), 3);
        QCOMPARE(peer.count("/api/backup/ai-conversations"), 1);
        const QString reconnect = fixture.service->execute(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 4);
        QCOMPARE(completed.last().at(0).toString(), reconnect);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(peer.count("/api/backup/manifest"), 2);
        peer.responses["/api/backup/ai-conversations"] = {"[]", 200, false};
        fixture.service->execute(QStringLiteral("importOnlyAIConversations"));
        QTRY_COMPARE(completed.count(), 5);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(peer.count("/api/backup/ai-conversations"), 2);
    }

    void transportDetachFailsPendingConnectWithoutReplayingHostWork()
    {
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.responses["/api/backup/manifest"] = {{}, 200, true};
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        const QString id = fixture.service->execute(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(peer.count("/api/backup/manifest"), 1);
        fixture.service->setNode(nullptr);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        peer.release("/api/backup/manifest", "{\"deviceName\":\"Late peer\"}");
        QTRY_VERIFY(!fixture.client.isConnecting());
        fixture.service->setNode(&fixture.node);
        QTRY_VERIFY(fixture.service->ready());
        QTRY_COMPARE(fixture.service->snapshot().value(QStringLiteral("manifest")).toMap()
                         .value(QStringLiteral("deviceName")).toString(), QStringLiteral("Late peer"));
        QCOMPARE(completed.count(), 1);
        QCOMPARE(peer.count("/api/backup/manifest"), 1);
        const QString disconnectId = fixture.service->execute(QStringLiteral("disconnect"));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), disconnectId);
        QVERIFY(completed.last().at(2).toBool());
        QTRY_VERIFY(fixture.service->snapshot().value(QStringLiteral("serverUrl")).toString().isEmpty());
    }

    void shotWorkerRetainsItsLeaseAndAdmissionUntilPhysicalCompletion_data()
    {
        QTest::addColumn<bool>("destroyClient");
        QTest::newRow("cancel-and-retry") << false;
        QTest::newRow("destroy-before-worker-start") << true;
    }

    void shotWorkerRetainsItsLeaseAndAdmissionUntilPhysicalCompletion()
    {
        QFETCH(bool, destroyClient);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath = directory.filePath(QStringLiteral("source.db"));
        {
            ShotHistoryStorage seed;
            QVERIFY(seed.initialize(sourcePath));
            ShotRecord shot;
            shot.summary.uuid = QStringLiteral("migration-worker-test");
            shot.summary.timestamp = 1760000000;
            shot.summary.profileName = QStringLiteral("Migration fixture");
            shot.summary.beverageType = QStringLiteral("espresso");
            shot.pressure.append(QPointF(0.0, 6.0));
            QVERIFY(seed.importShotRecord(shot, false) > 0);
            QTRY_VERIFY(seed.isDbWorkIdle());
            seed.close();
        }
        QFile input(sourcePath);
        QVERIFY(input.open(QIODevice::ReadOnly));
        const QByteArray database = input.readAll();
        input.close();
        QVERIFY(!database.isEmpty());
        ShotHistoryStorage destination;
        QVERIFY(destination.initialize(directory.filePath(QStringLiteral("destination.db"))));
        QTRY_VERIFY(destination.isDbWorkIdle());
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.responses["/api/backup/manifest"] = {QJsonDocument(QJsonObject{
            {QStringLiteral("deviceName"), QStringLiteral("Shot fixture")},
            {QStringLiteral("shotCount"), 1}, {QStringLiteral("shotsSize"), database.size()}
        }).toJson(QJsonDocument::Compact), 200, false};
        peer.responses["/api/backup/shots"] = {database, 200, false};

        // Queue the real importer behind a held pool job. This establishes the
        // cancellation/destruction interval without a test-only client seam.
        auto* pool = QThreadPool::globalInstance();
        const int previousLimit = pool->maxThreadCount();
        pool->waitForDone();
        pool->setMaxThreadCount(1);
        auto gate = std::make_shared<QSemaphore>();
        auto started = std::make_shared<QSemaphore>();
        const auto restorePool = qScopeGuard([pool, gate, previousLimit] {
            gate->release();
            pool->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            pool->setMaxThreadCount(previousLimit);
        });
        Q_UNUSED(restorePool)
        pool->start(QRunnable::create([gate, started] { started->release(); gate->acquire(); }));
        QVERIFY(started->tryAcquire(1, 5000));
        auto fixture = std::make_unique<MigrationServiceFixture>();
        fixture->client.setShotHistoryStorage(&destination);
        QVERIFY(fixture->initialize());
        QTRY_VERIFY(fixture->service->ready());
        QSignalSpy completed(fixture->service, &DataMigrationService::operationCompleted);
        QSignalSpy networkFinished(&fixture->network, &QNetworkAccessManager::finished);
        fixture->service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.first().at(2).toBool());
        const QString importId = fixture->service->execute(QStringLiteral("importOnlyShots"));
        QTRY_COMPARE(peer.count("/api/backup/shots"), 1);
        // Both HTTP completions have run; the real DB importer is queued behind
        // the held pool job rather than still waiting for the peer response.
        QTRY_COMPARE(networkFinished.count(), 2);
        QVERIFY(fixture->client.isImporting());
        QCOMPARE(ShotHistoryStorage::getShotCountStatic(destination.databasePath()), 0);
        if (destroyClient) {
            const QPointer<DataMigrationClient> destroyed(&fixture->client);
            fixture.reset();
            QVERIFY(destroyed.isNull());
            gate->release();
            pool->waitForDone();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            QCOMPARE(ShotHistoryStorage::getShotCountStatic(destination.databasePath()), 0);
            return;
        }
        const QString stopId = fixture->service->execute(QStringLiteral("stopDiscovery"));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), stopId);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("busy"));
        QVERIFY(fixture->service->busy());
        const QString cancelId = fixture->service->execute(QStringLiteral("cancel"));
        QTRY_COMPARE(completed.count(), 4);
        QCOMPARE(completed.at(2).at(0).toString(), importId);
        QCOMPARE(completed.at(2).at(4).toString(), QStringLiteral("cancelled"));
        QCOMPARE(completed.at(3).at(0).toString(), cancelId);
        QVERIFY(completed.at(3).at(2).toBool());
        QVERIFY(fixture->client.isImporting()); // The physical job still owns the DB slot.
        fixture->service->execute(QStringLiteral("importOnlyShots"));
        QTRY_COMPARE(completed.count(), 5);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("busy"));
        fixture->service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 6);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("busy"));
        gate->release();
        QTRY_VERIFY(!fixture->client.isImporting());
        QCOMPARE(completed.count(), 6); // The retired run emits no second terminal.
        QCOMPARE(ShotHistoryStorage::getShotCountStatic(destination.databasePath()), 0);
        QCOMPARE(destination.totalShots(), 0);
        QCOMPARE(peer.count("/api/backup/shots"), 1);
        const QString retry = fixture->service->execute(QStringLiteral("importOnlyShots"));
        QTRY_COMPARE(completed.count(), 7);
        QCOMPARE(completed.last().at(0).toString(), retry);
        QVERIFY2(completed.last().at(2).toBool(), qPrintable(completed.last().at(4).toString()));
        QCOMPARE(completed.last().at(3).toMap().value(QStringLiteral("shotsImported")).toInt(), 1);
        QCOMPARE(ShotHistoryStorage::getShotCountStatic(destination.databasePath()), 1);
        QCOMPARE(peer.count("/api/backup/shots"), 2);
    }

    void absentImportDataAndDisconnectedAuthenticationAreExplicitRefusals()
    {
        MigrationServiceFixture fixture;
        QVERIFY(fixture.initialize());
        QTRY_VERIFY(fixture.service->ready());
        QSignalSpy completed(fixture.service, &DataMigrationService::operationCompleted);
        const QString auth = fixture.service->execute(QStringLiteral("authenticate"),
            {{QStringLiteral("totpCode"), QStringLiteral("123456")}});
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.last().at(0).toString(), auth);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("notConnected"));
        const QString unconnectedImport = fixture.service->execute(QStringLiteral("importAll"));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), unconnectedImport);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("notConnected"));
        MigrationHttpFixture peer;
        QVERIFY(peer.listen());
        peer.setManifest(0);
        fixture.service->execute(QStringLiteral("connectToServer"), {{QStringLiteral("serverUrl"), peer.url()}});
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(completed.last().at(2).toBool());
        for (const QString& operation : {QStringLiteral("importOnlySettings"), QStringLiteral("importOnlyProfiles"),
                                       QStringLiteral("importOnlyShots"), QStringLiteral("importOnlyMedia"),
                                       QStringLiteral("importOnlyAIConversations")}) {
            const qsizetype before = completed.count();
            const QString request = fixture.service->execute(operation);
            QTRY_COMPARE(completed.count(), before + 1);
            QCOMPARE(completed.last().at(0).toString(), request);
            QCOMPARE(completed.last().at(4).toString(), QStringLiteral("dataUnavailable"));
            QVERIFY(!fixture.service->busy());
        }
        QCOMPARE(peer.requests.size(), 1);
    }

    void acceptsOnlyBoundedHttpAddressesAndSixDigitCodes()
    {
        QVERIFY(DataMigrationCommandPolicy::validate(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), QStringLiteral("192.168.1.2:8888")}}));
        QVERIFY(DataMigrationCommandPolicy::validate(QStringLiteral("authenticate"),
            {{QStringLiteral("totpCode"), QStringLiteral("123456")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("authenticate"),
            {{QStringLiteral("totpCode"), QStringLiteral("12345x")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), QStringLiteral("http://user:pass@example.test")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("importAll"),
            {{QStringLiteral("totpCode"), QStringLiteral("123456")}}));
        QVERIFY(DataMigrationCommandPolicy::validate(QStringLiteral("importAll"),
            {{QStringLiteral("replace"), true}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("importAll"),
            {{QStringLiteral("replace"), QStringLiteral("true")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("forgedOperation"), {}));
    }

    void snapshotProjectionDropsUnapprovedManifestAndDeviceFields()
    {
        const QVariantMap manifest = DataMigrationCommandPolicy::safeManifest({
            {QStringLiteral("deviceName"), QStringLiteral("Kitchen")},
            {QStringLiteral("profileCount"), 7},
            {QStringLiteral("hasLanguageTranslations"), true},
            {QStringLiteral("sessionToken"), QStringLiteral("secret")},
            {QStringLiteral("settingsSize"), 1234}});
        QCOMPARE(manifest.value(QStringLiteral("deviceName")).toString(), QStringLiteral("Kitchen"));
        QVERIFY(!manifest.contains(QStringLiteral("sessionToken")));
        QVERIFY(manifest.value(QStringLiteral("hasLanguageTranslations")).toBool());
        QVERIFY(!manifest.contains(QStringLiteral("settingsSize")));

        const QVariantList devices = DataMigrationCommandPolicy::safeDevices({QVariantMap{
            {QStringLiteral("deviceName"), QStringLiteral("Kitchen")},
            {QStringLiteral("serverUrl"), QStringLiteral("http://192.168.1.2:8888")},
            {QStringLiteral("sessionToken"), QStringLiteral("secret")}}});
        QCOMPARE(devices.size(), 1);
        QVERIFY(!devices.first().toMap().contains(QStringLiteral("sessionToken")));
    }
};

QTEST_GUILESS_MAIN(DataMigrationCommandPolicyTest)
#include "tst_datamigrationcommandpolicy.moc"
