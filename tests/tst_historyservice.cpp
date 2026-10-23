#include <QtTest>

#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QByteArray>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QSet>

#include "core/appsettings.h"
#include "core/settings_network.h"
#include "history/shothistorystorage.h"
#include "history/shothistory_types.h"
#include "../src/remote/historyservice.h"
#include "../src/remote/historyservice_qml.h"
#include "../src/remote/historyservicesource.h"

namespace {
QByteArray minimalShotSamples()
{
    return qCompress(QByteArrayLiteral(
        R"({"pressure":{"t":[0.0],"v":[9.5]},"flow":{"t":[0.0],"v":[0.0]}})"));
}
}

class TestHistorySource final : public HistoryStateSimpleSource
{
    Q_OBJECT
public:
    using HistoryStateSimpleSource::setHistoryRevision;
    using HistoryStateSimpleSource::setLastSavedShotId;
    using HistoryStateSimpleSource::setPreferences;
    using HistoryStateSimpleSource::setPreferencesRevision;
    using HistoryStateSimpleSource::setReady;
    using HistoryStateSimpleSource::setTotalShots;

    QString pendingRequestId;
    QString pendingOperation;
    qint64 pendingShotId = 0;
    int pageCalls = 0;
    int shotCalls = 0;
    bool holdShot = false;

    void refresh() override {}
    void requestAutoFavorites(QString id, QString, int) override
    { emit operationCompleted(id, QStringLiteral("autoFavorites"), true, {{QStringLiteral("favorites"), QVariantList{}}}, {}); }
    void requestAutoFavoriteGroupDetails(QString id, QVariantMap) override
    { emit operationCompleted(id, QStringLiteral("autoFavoriteDetails"), true, {{QStringLiteral("details"), QVariantMap{}}}, {}); }

    void requestMostRecentShotId(QString requestId) override
    {
        emit operationCompleted(requestId, QStringLiteral("mostRecentShotId"), true,
                                {{QStringLiteral("shotId"), qint64(42)}}, {});
    }

    void requestPage(QString requestId, QVariantMap, int offset, int) override
    {
        ++pageCalls;
        pendingRequestId = requestId;
        pendingOperation = QStringLiteral("page");
        emit pageReady(requestId, offset,
                       QVariantList{QVariant(QVariantMap{{QStringLiteral("id"), 42}})}, 1, {});
    }

    void requestShot(QString requestId, qint64 shotId) override
    {
        ++shotCalls;
        pendingRequestId = requestId;
        pendingOperation = QStringLiteral("shot");
        pendingShotId = shotId;
        if (holdShot)
            return;
        emit operationCompleted(requestId, pendingOperation, true,
                                {{QStringLiteral("shotId"), shotId},
                                 {QStringLiteral("shot"), QVariantMap{{QStringLiteral("id"), shotId}}}}, {});
    }

    void finishHeldShot()
    {
        emit operationCompleted(pendingRequestId, pendingOperation, true,
                                {{QStringLiteral("shotId"), pendingShotId},
                                 {QStringLiteral("shot"), QVariantMap{{QStringLiteral("id"), pendingShotId}}}}, {});
    }

    void requestOutcome(QString requestId, qint64 shotId) override
    {
        pendingRequestId = requestId;
        pendingOperation = QStringLiteral("outcome");
        pendingShotId = shotId;
        emit operationCompleted(requestId, pendingOperation, true,
                                {{QStringLiteral("shotId"), shotId},
                                 {QStringLiteral("previousShotId"), qint64(17)},
                                 {QStringLiteral("previousDateTime"), QStringLiteral("yesterday")},
                                 {QStringLiteral("comparison"), QVariantMap{{QStringLiteral("delta"), 1.0}}}}, {});
    }

    void requestUploadState(QString requestId, qint64 shotId) override
    {
        pendingRequestId = requestId;
        pendingOperation = QStringLiteral("uploadState");
        pendingShotId = shotId;
        emit operationCompleted(requestId, pendingOperation, true,
                                {{QStringLiteral("shotId"), shotId},
                                 {QStringLiteral("uploaded"), false},
                                 {QStringLiteral("serverShotId"), QStringLiteral("")},
                                 {QStringLiteral("serial"), QStringLiteral("DE1")},
                                 {QStringLiteral("replacePending"), false},
                                 {QStringLiteral("rejected"), false},
                                 {QStringLiteral("rejectedStatus"), 0}}, {});
    }

    void requestBaristas(QString requestId) override
    {
        emit operationCompleted(requestId, QStringLiteral("baristas"), true,
                                {{QStringLiteral("baristas"), QStringList{QStringLiteral("A")}}}, {});
    }

    void updateMetadata(QString requestId, qint64 shotId, QVariantMap) override
    {
        emit operationCompleted(requestId, QStringLiteral("metadata"), true,
                                {{QStringLiteral("shotId"), shotId}}, {});
    }

    void deleteShot(QString requestId, qint64 shotId) override
    {
        emit operationCompleted(requestId, QStringLiteral("deleteShot"), true,
                                {{QStringLiteral("shotId"), shotId}}, {});
    }

    void deleteShots(QString requestId, QVariantList shotIds) override
    {
        emit operationCompleted(requestId, QStringLiteral("deleteShots"), true,
                                {{QStringLiteral("shotIds"), shotIds}}, {});
    }

    void loadShotWithMetadata(QString requestId, qint64 shotId) override
    {
        emit operationCompleted(requestId, QStringLiteral("loadShotWithMetadata"), true,
                                {{QStringLiteral("shotId"), shotId}}, {});
    }

    void setPreference(QString requestId, QString, QVariant, quint64) override
    {
        emit operationCompleted(requestId, QStringLiteral("preference"), true, {}, {});
    }
};

class HistoryServiceTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_tempDir;
private slots:
    void init() { QTest::failOnWarning(); }

    void wrappersDeliverOnlyTheirCorrelatedResults()
    {
        TestHistorySource source;
        source.setReady(true);
        source.setTotalShots(19);
        source.setLastSavedShotId(77);
        source.setHistoryRevision(4);
        source.setPreferences({{QStringLiteral("sortField"), QStringLiteral("timestamp")}});
        source.setPreferencesRevision(2);
        QRemoteObjectHost host;
        QRemoteObjectNode node;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<HistoryStateSourceAPI>(&source));
        QVERIFY(node.connectToNode(host.hostUrl()));
        HistoryService::setPublishedNode(&node);
        HistoryService* service = HistoryServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QCOMPARE(service->totalShots(), qint64(19));
        QCOMPARE(service->lastSavedShotId(), qint64(77));
        QCOMPARE(service->historyRevision(), quint64(4));

        QSignalSpy completed(service, &HistoryService::operationCompleted);
        QSignalSpy outcomeReady(service, &HistoryService::shotOutcomeReady);
        const QString requestId = service->requestShotOutcome(73);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("outcome"));
        QVERIFY(completed.first().at(2).toBool());
        const QVariantMap outcome = completed.first().at(3).toMap();
        QCOMPARE(outcome.value(QStringLiteral("shotId")).toLongLong(), qint64(73));
        QCOMPARE(outcome.value(QStringLiteral("previousShotId")).toLongLong(), qint64(17));
        QCOMPARE(outcomeReady.count(), 1);
        QCOMPARE(outcomeReady.first().at(0).toLongLong(), qint64(73));
        QCOMPARE(outcomeReady.first().at(1).toMap().value(QStringLiteral("previousShotId")).toLongLong(),
                 qint64(17));

        QSignalSpy uploadReady(service, &HistoryService::decentUploadStateReady);
        const QString uploadRequest = service->requestDecentUploadState(73);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), uploadRequest);
        QCOMPARE(completed.last().at(3).toMap().value(QStringLiteral("serial")).toString(),
                 QStringLiteral("DE1"));
        QCOMPARE(uploadReady.count(), 1);
        // The legacy signal intentionally retains its original payload shape (no shotId key).
        QVERIFY(!uploadReady.first().at(1).toMap().contains(QStringLiteral("shotId")));

        QSignalSpy metadataReady(service, &HistoryService::shotMetadataUpdated);
        const QString metadataId = service->requestUpdateShotMetadata(73, {});
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), metadataId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("metadata"));
        QCOMPARE(metadataReady.count(), 1);
        QCOMPARE(metadataReady.first().at(0).toLongLong(), qint64(73));
        QVERIFY(metadataReady.first().at(1).toBool());

        QSignalSpy deleted(service, &HistoryService::shotDeleted);
        const QString deleteId = service->requestDeleteShot(73);
        QTRY_COMPARE(completed.count(), 4);
        QCOMPARE(completed.last().at(0).toString(), deleteId);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(deleted.count(), 1);
        QCOMPARE(deleted.first().at(0).toLongLong(), qint64(73));

        const QString baristasId = service->requestBaristas();
        QTRY_COMPARE(completed.count(), 5);
        QCOMPARE(completed.last().at(0).toString(), baristasId);
        QCOMPARE(completed.last().at(3).toMap().value(QStringLiteral("baristas")).toStringList(),
                 QStringList{QStringLiteral("A")});

        HistoryService::setPublishedNode(nullptr);
    }

    void latestShotRequestReturnsCorrelatedOperationResult()
    {
        TestHistorySource source;
        source.setReady(true);
        QRemoteObjectHost host;
        QRemoteObjectNode node;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<HistoryStateSourceAPI>(&source));
        QVERIFY(node.connectToNode(host.hostUrl()));
        HistoryService::setPublishedNode(&node);
        HistoryService* service = HistoryServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());

        QSignalSpy completed(service, &HistoryService::operationCompleted);
        const QString requestId = service->requestMostRecentShotId();
        QVERIFY(!requestId.isEmpty());
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("mostRecentShotId"));
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toMap().value(QStringLiteral("shotId")).toLongLong(),
                 qint64(42));
    }

    void pageAndShotRequestsAreScopedAndExposeNativeAliases()
    {
        TestHistorySource source;
        source.setReady(true);
        QRemoteObjectHost host;
        QRemoteObjectNode node;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<HistoryStateSourceAPI>(&source));
        QVERIFY(node.connectToNode(host.hostUrl()));
        HistoryService::setPublishedNode(&node);
        HistoryService* service = HistoryService::create(&engine, &engine);
        QTRY_VERIFY(service->ready());

        QSignalSpy pageSpy(service, &HistoryService::pageReady);
        const QString pageId = service->requestPage({}, 25, 50);
        QTRY_COMPARE(pageSpy.count(), 1);
        QCOMPARE(pageSpy.first().at(0).toString(), pageId);
        QCOMPARE(pageSpy.first().at(1).toInt(), 25);
        QCOMPARE(pageSpy.first().at(2).toList().first().toMap().value(QStringLiteral("id")).toInt(), 42);

        QSignalSpy shotReady(service, &HistoryService::shotReady);
        QSignalSpy completeSpy(service, &HistoryService::operationCompleted);
        const QString shotRequest = service->requestShot(91);
        QTRY_COMPARE(shotReady.count(), 1);
        QCOMPARE(shotReady.first().at(0).toLongLong(), qint64(91));
        QCOMPARE(shotReady.first().at(1).toMap().value(QStringLiteral("id")).toLongLong(), qint64(91));
        QCOMPARE(completeSpy.last().at(0).toString(), shotRequest);
        QCOMPARE(completeSpy.last().at(1).toString(), QStringLiteral("shot"));

        HistoryService::setPublishedNode(nullptr);
    }

    void disconnectFailsPendingAndDoesNotReplay()
    {
        TestHistorySource source;
        source.setReady(true);
        QRemoteObjectHost host;
        QRemoteObjectNode node;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<HistoryStateSourceAPI>(&source));
        QVERIFY(node.connectToNode(host.hostUrl()));
        HistoryService::setPublishedNode(&node);
        HistoryService* service = HistoryService::create(&engine, &engine);
        QTRY_VERIFY(service->ready());

        QSignalSpy completed(service, &HistoryService::operationCompleted);
        QSignalSpy shotReady(service, &HistoryService::shotReady);
        source.holdShot = true;
        const int callsBefore = source.shotCalls;
        const QString pendingId = service->requestShot(21);
        QTRY_COMPARE(source.shotCalls, callsBefore + 1);
        QCOMPARE(source.pendingRequestId, pendingId);

        service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), pendingId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        QCOMPARE(shotReady.count(), 0);

        source.finishHeldShot();
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
        QCOMPARE(source.shotCalls, callsBefore + 1);

        HistoryService::setPublishedNode(nullptr);
    }

    void nativePreferencesValidateCompareAndPersist()
    {
        QVERIFY(m_tempDir.isValid());
        {
            AppSettings clean;
            clean.remove(QStringLiteral("shotHistory"));
            clean.sync();
        }

        SettingsNetwork settings;
        NativeHistoryServiceSource source(nullptr, &settings, {});
        QSignalSpy completed(&source, &HistoryStateSimpleSource::operationCompleted);
        QVERIFY(source.preferencesRevision() > 0);
        const quint64 originalRevision = source.preferencesRevision();

        const QString validSortRequest = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(validSortRequest, QStringLiteral("sort"),
            QVariantMap{{QStringLiteral("field"), QStringLiteral("bean_brand")},
                        {QStringLiteral("direction"), QStringLiteral("ASC")}},
            originalRevision);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.last().at(0).toString(), validSortRequest);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.shotHistorySortField(), QStringLiteral("bean_brand"));
        QCOMPARE(settings.shotHistorySortDirection(), QStringLiteral("ASC"));
        QVERIFY(source.preferencesRevision() > originalRevision);
        const quint64 sortRevision = source.preferencesRevision();

        const QString staleRequest = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(staleRequest, QStringLiteral("sort"),
            QVariantMap{{QStringLiteral("field"), QStringLiteral("timestamp")},
                        {QStringLiteral("direction"), QStringLiteral("DESC")}},
            originalRevision);
        QCOMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), staleRequest);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(settings.shotHistorySortField(), QStringLiteral("bean_brand"));
        QCOMPARE(settings.shotHistorySortDirection(), QStringLiteral("ASC"));
        QCOMPARE(source.preferencesRevision(), sortRevision);

        const QString invalidSortRequest = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(invalidSortRequest, QStringLiteral("sort"),
            QVariantMap{{QStringLiteral("field"), QStringLiteral("profile_json")},
                        {QStringLiteral("direction"), QStringLiteral("ASC")}},
            sortRevision);
        QCOMPARE(completed.size(), 3);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(source.preferencesRevision(), sortRevision);

        const QString savedSearchRequest = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QStringList savedSearches{QStringLiteral("Ethiopia Light"), QStringLiteral("washed")};
        source.setPreference(savedSearchRequest, QStringLiteral("savedSearches"),
                             QVariant::fromValue(savedSearches), sortRevision);
        QCOMPARE(completed.size(), 4);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.savedSearches(), savedSearches);
        QVERIFY(source.preferencesRevision() > sortRevision);
        QCOMPARE(source.preferences().value(QStringLiteral("savedSearches")).toStringList(),
                 savedSearches);
        const quint64 savedSearchRevision = source.preferencesRevision();

        const QString invalidSearchRequest = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(invalidSearchRequest, QStringLiteral("savedSearches"),
            QVariantList{QStringLiteral("washed"), QStringLiteral("washed")}, savedSearchRevision);
        QCOMPARE(completed.size(), 5);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(settings.savedSearches(), savedSearches);

        AppSettings persisted;
        persisted.sync();
        SettingsNetwork reopenedSettings;
        QCOMPARE(reopenedSettings.shotHistorySortField(), QStringLiteral("bean_brand"));
        QCOMPARE(reopenedSettings.shotHistorySortDirection(), QStringLiteral("ASC"));
        QCOMPARE(reopenedSettings.savedSearches(), savedSearches);
    }

    void autoFavoritesCorrelateQueriesAndPreserveLegacyResults()
    {
        ShotHistoryStorage storage;
        const QString dbPath = m_tempDir.filePath(QStringLiteral("history-favorites.db"));
        QVERIFY(storage.initialize(dbPath));
        QTRY_VERIFY_WITH_TIMEOUT(storage.isReady(), 15000);
        ShotSaveData shot;
        shot.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shot.timestamp = QDateTime::currentSecsSinceEpoch();
        shot.profileName = QStringLiteral("Favorite fixture");
        shot.profileJson = QStringLiteral("{}");
        shot.duration = 24;
        shot.doseWeight = 18;
        shot.finalWeight = 36;
        shot.compressedSamples = minimalShotSamples();
        shot.sampleCount = 1;
        QVERIFY(ShotHistoryStorage::saveShotStatic(dbPath, shot) > 0);

        NativeHistoryServiceSource source(&storage, nullptr, {});
        QSignalSpy done(&source, &HistoryStateSimpleSource::operationCompleted);
        QSignalSpy legacy(&storage, &ShotHistoryStorage::autoFavoritesReady);
        const QString listId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString detailId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.requestAutoFavorites(listId, QStringLiteral("profile"), 10);
        source.requestAutoFavoriteGroupDetails(detailId,
            {{QStringLiteral("groupBy"), QStringLiteral("profile")},
             {QStringLiteral("profileName"), shot.profileName}});
        storage.requestAutoFavorites(QStringLiteral("bean"), 5);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(legacy.size(), 1, 15000);
        QSet<QString> seen;
        for (const auto& reply : done) {
            const QString id = reply.at(0).toString();
            seen.insert(id);
            QVERIFY2(reply.at(2).toBool(), qPrintable(reply.at(4).toString()));
            if (id == listId) {
                const auto rows = reply.at(3).toMap().value(QStringLiteral("favorites")).toList();
                QCOMPARE(rows.size(), 1);
                QCOMPARE(rows.first().toMap().value(QStringLiteral("profileName")).toString(), shot.profileName);
            } else {
                QCOMPARE(id, detailId);
                QCOMPARE(reply.at(3).toMap().value(QStringLiteral("details")).toMap()
                    .value(QStringLiteral("avgDose")).toDouble(), 18.0);
            }
        }
        QCOMPARE(seen, (QSet<QString>{listId, detailId}));
        source.requestAutoFavorites(QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("unknown"), 10);
        QCOMPARE(done.size(), 3);
        QVERIFY(!done.last().at(2).toBool());
    }

    void autoFavoritesPreferencesValidateAndRejectStaleWrites()
    {
        SettingsNetwork settings;
        settings.setAutoFavoritesGroupBy(QStringLiteral("bean_profile"));
        NativeHistoryServiceSource source(nullptr, &settings, {});
        QSignalSpy done(&source, &HistoryStateSimpleSource::operationCompleted);
        const auto write = [&](const QString& key, const QVariant& value, quint64 revision) {
            source.setPreference(QUuid::createUuid().toString(QUuid::WithoutBraces), key, value, revision);
        };
        const quint64 original = source.preferencesRevision();
        write(QStringLiteral("autoFavoritesGroupBy"), QStringLiteral("profile"), original);
        QVERIFY(done.last().at(2).toBool());
        QCOMPARE(settings.autoFavoritesGroupBy(), QStringLiteral("profile"));
        write(QStringLiteral("autoFavoritesGroupBy"), QStringLiteral("bean"), original);
        QVERIFY(!done.last().at(2).toBool());
        QCOMPARE(done.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(settings.autoFavoritesGroupBy(), QStringLiteral("profile"));
        write(QStringLiteral("autoFavoritesMaxItems"), 500, source.preferencesRevision());
        QVERIFY(!done.last().at(2).toBool());
        write(QStringLiteral("autoFavoritesMaxItems"), QStringLiteral("10"), source.preferencesRevision());
        QVERIFY(!done.last().at(2).toBool());
        write(QStringLiteral("autoFavoriteGraphHeight"), QStringLiteral("300"), source.preferencesRevision());
        QVERIFY(!done.last().at(2).toBool());
        write(QStringLiteral("autoFavoritesHideUnrated"), QStringLiteral("true"), source.preferencesRevision());
        QVERIFY(!done.last().at(2).toBool());
        write(QStringLiteral("autoFavoriteGraphHeight"), 300.0, source.preferencesRevision());
        QVERIFY(done.last().at(2).toBool());
        QCOMPARE(settings.autoFavoriteGraphHeight(), 300.0);
        settings.setAutoFavoritesHideUnrated(true);
        QVERIFY(source.preferences().value(QStringLiteral("autoFavoritesHideUnrated")).toBool());
    }

    void nativeMetadataRequestUsesCanonicalStorageUpdate()
    {
        QVERIFY(m_tempDir.isValid());
        const QString dbPath = m_tempDir.filePath(QStringLiteral("history-metadata.db"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));
        QTRY_VERIFY_WITH_TIMEOUT(storage.isReady(), 15000);

        ShotSaveData shotData;
        shotData.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shotData.timestamp = QDateTime::currentSecsSinceEpoch();
        shotData.profileName = QStringLiteral("History service fixture");
        shotData.profileJson = QStringLiteral("{}");
        shotData.duration = 24.0;
        shotData.doseWeight = 18.0;
        shotData.finalWeight = 36.0;
        shotData.compressedSamples = minimalShotSamples();
        shotData.sampleCount = 1;
        const qint64 shotId = ShotHistoryStorage::saveShotStatic(dbPath, shotData);
        QVERIFY(shotId > 0);

        NativeHistoryServiceSource source(&storage, nullptr, {});
        QSignalSpy completed(&source, &HistoryStateSimpleSource::operationCompleted);
        const QString updateId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.updateMetadata(updateId, shotId,
            {{QStringLiteral("espressoNotes"), QStringLiteral("citrus finish")},
             {QStringLiteral("tasteBalance"), QStringLiteral("SOUR")}});
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QCOMPARE(completed.last().at(0).toString(), updateId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("metadata"));
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(completed.last().at(4).toString().isEmpty());

        const QString readId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.requestShot(readId, shotId);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 15000);
        QCOMPARE(completed.last().at(0).toString(), readId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("shot"));
        QVERIFY(completed.last().at(2).toBool());
        const QVariantMap storedShot = completed.last().at(3).toMap().value(QStringLiteral("shot")).toMap();
        QCOMPARE(storedShot.value(QStringLiteral("espressoNotes")).toString(),
                 QStringLiteral("citrus finish"));
        QCOMPARE(storedShot.value(QStringLiteral("tasteBalance")).toString(), QStringLiteral("sour"));
    }

    void nativeLatestShotIdsUseRequestScopedStorageResults()
    {
        QVERIFY(m_tempDir.isValid());
        const QString dbPath = m_tempDir.filePath(QStringLiteral("history-latest-id.db"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));
        QTRY_VERIFY_WITH_TIMEOUT(storage.isReady(), 15000);

        ShotSaveData shotData;
        shotData.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shotData.timestamp = QDateTime::currentSecsSinceEpoch();
        shotData.profileName = QStringLiteral("History service fixture");
        shotData.profileJson = QStringLiteral("{}");
        shotData.duration = 24.0;
        shotData.doseWeight = 18.0;
        shotData.finalWeight = 36.0;
        shotData.compressedSamples = minimalShotSamples();
        shotData.sampleCount = 1;
        const qint64 expectedShotId = ShotHistoryStorage::saveShotStatic(dbPath, shotData);
        QVERIFY(expectedShotId > 0);

        NativeHistoryServiceSource source(&storage, nullptr, {});
        QSignalSpy completed(&source, &HistoryStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());
        const QString firstId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString secondId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.requestMostRecentShotId(firstId);
        source.requestMostRecentShotId(secondId);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 15000);

        QSet<QString> completedIds;
        for (const auto& result : completed) {
            QCOMPARE(result.at(1).toString(), QStringLiteral("mostRecentShotId"));
            QVERIFY(result.at(2).toBool());
            QCOMPARE(result.at(3).toMap().value(QStringLiteral("shotId")).toLongLong(),
                     expectedShotId);
            completedIds.insert(result.at(0).toString());
        }
        QCOMPARE(completedIds.size(), 2);
        QVERIFY(completedIds.contains(firstId));
        QVERIFY(completedIds.contains(secondId));
    }

    void nativeFilteredPageReportsDatabaseOpenFailure()
    {
        QVERIFY(m_tempDir.isValid());
        const QString directory = m_tempDir.filePath(QStringLiteral("history-page"));
        QVERIFY(QDir().mkpath(directory));
        const QString dbPath = directory + QStringLiteral("/history.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));
        QTRY_VERIFY_WITH_TIMEOUT(storage.isReady(), 15000);

        NativeHistoryServiceSource source(&storage, nullptr, {});
        QSignalSpy pageReady(&source, &HistoryStateSimpleSource::pageReady);
        QVERIFY(pageReady.isValid());

        // Keep storage's ready state intact while making a new worker connection
        // unable to open its path. This exercises the post-initialization failure
        // that used to look exactly like a legitimate empty history page.
        const QString movedDirectory = m_tempDir.filePath(QStringLiteral("history-page-moved"));
        QVERIFY(QDir().rename(directory, movedDirectory));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("withTempDb: DB open failed"));

        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.requestPage(requestId, {}, 0, 50);
        QTRY_COMPARE_WITH_TIMEOUT(pageReady.size(), 1, 15000);
        QCOMPARE(pageReady.first().at(0).toString(), requestId);
        QVERIFY(pageReady.first().at(2).toList().isEmpty());
        QCOMPARE(pageReady.first().at(3).toInt(), 0);
        QCOMPARE(pageReady.first().at(4).toString(), QStringLiteral("Could not open shot database"));
    }

    void nativeLoadShotCallbackCompletesOnlyItsMatchingRequest()
    {
        QVERIFY(m_tempDir.isValid());
        const QString dbPath = m_tempDir.filePath(QStringLiteral("history-load-shot.db"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));
        QTRY_VERIFY_WITH_TIMEOUT(storage.isReady(), 15000);

        ShotSaveData shotData;
        shotData.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shotData.timestamp = QDateTime::currentSecsSinceEpoch();
        shotData.profileName = QStringLiteral("History load callback fixture");
        shotData.profileJson = QStringLiteral("{}");
        shotData.duration = 24.0;
        shotData.doseWeight = 18.0;
        shotData.finalWeight = 36.0;
        shotData.compressedSamples = minimalShotSamples();
        shotData.sampleCount = 1;
        const qint64 shotId = ShotHistoryStorage::saveShotStatic(dbPath, shotData);
        QVERIFY(shotId > 0);

        QVector<QPair<QString, qint64>> dispatched;
        NativeHistoryServiceSource source(&storage, nullptr,
            [&dispatched](const QString& requestId, qint64 requestedShotId) {
                dispatched.append({requestId, requestedShotId});
            });
        QSignalSpy completed(&source, &HistoryStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        const QString firstId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.loadShotWithMetadata(firstId, shotId);
        QCOMPARE(dispatched.size(), 1);
        QCOMPARE(dispatched.first().first, firstId);
        QCOMPARE(dispatched.first().second, shotId);
        source.completeShotLoad(QStringLiteral("unrelated-request"), shotId, true);
        source.completeShotLoad(firstId, shotId + 1, true);
        QCOMPARE(completed.size(), 0);

        source.completeShotLoad(firstId, shotId, true);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), firstId);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("loadShotWithMetadata"));
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toMap().value(QStringLiteral("shotId")).toLongLong(), shotId);

        const QString secondId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.loadShotWithMetadata(secondId, shotId);
        QCOMPARE(dispatched.size(), 2);
        source.completeShotLoad(firstId, shotId, true);
        QCOMPARE(completed.size(), 1);
        source.completeShotLoad(secondId, shotId, true);
        QCOMPARE(completed.size(), 2);
        QCOMPARE(completed.last().at(0).toString(), secondId);
    }
};

QTEST_GUILESS_MAIN(HistoryServiceTest)
#include "tst_historyservice.moc"
