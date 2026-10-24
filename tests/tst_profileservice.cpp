#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QStandardPaths>

#include "mocks/McpTestFixture.h"
#include "controllers/profilemanager.h"
#include "profile/profile.h"
#include "../src/remote/profileservice.h"
#include "../src/remote/profileservice_qml.h"
#include "../src/remote/profileservicesource.h"

class TestProfileSource final : public ProfileStateSimpleSource
{
    Q_OBJECT
public:
    using ProfileStateSimpleSource::setAllProfilesList;
    using ProfileStateSimpleSource::setBaseProfileName;
    using ProfileStateSimpleSource::setPreviousProfileName;
    using ProfileStateSimpleSource::setCatalogRevision;
    using ProfileStateSimpleSource::setAvailableProfiles;
    using ProfileStateSimpleSource::setAutoLoadProfileFilename;
    using ProfileStateSimpleSource::setAutoLoadRevertMinutes;
    using ProfileStateSimpleSource::setCurrentProfileReadOnly;
    using ProfileStateSimpleSource::setCurrentProfileSummary;
    using ProfileStateSimpleSource::setErrorMessage;
    using ProfileStateSimpleSource::setFavoriteProfiles;
    using ProfileStateSimpleSource::setFavoriteProfileOrder;
    using ProfileStateSimpleSource::setFlowCalibration;
    using ProfileStateSimpleSource::setGrindDirections;
    using ProfileStateSimpleSource::setInstalledProfileTitles;
    using ProfileStateSimpleSource::setLoading;
    using ProfileStateSimpleSource::setProfileModified;
    using ProfileStateSimpleSource::setProfileSummaries;
    using ProfileStateSimpleSource::setProfileUsage;
    using ProfileStateSimpleSource::setRoastAffinities;
    using ProfileStateSimpleSource::setReady;

    int executeCalls = 0;
    int detailCalls = 0;
    bool holdOperation = false;
    QString pendingId;
    QString pendingOperation;
    QString pendingFilename;
    QHash<QString, QString> detailRequests;
    QHash<QString, QPair<QString, QString>> knowledgeRequests;
    qint64 lastExpectedRevision = -1;
    QVariantMap lastFields;

    void refresh() override {}
    void requestProfileDetail(QString requestId, QString filename) override
    {
        ++detailCalls;
        detailRequests.insert(std::move(requestId), std::move(filename));
    }
    void requestKnowledge(QString requestId, QString title, QString profileJson) override
    {
        knowledgeRequests.insert(std::move(requestId), {std::move(title), std::move(profileJson)});
    }
    void execute(QString requestId, QString operation, QString filename,
                 QVariantMap fields, qint64 expectedRevision) override
    {
        ++executeCalls;
        lastExpectedRevision = expectedRevision;
        lastFields = fields;
        if (holdOperation) {
            pendingId = std::move(requestId);
            pendingOperation = std::move(operation);
            pendingFilename = std::move(filename);
            return;
        }
        emit operationCompleted(requestId, operation, filename, true, {},
                                {{QStringLiteral("favorite"), true}});
    }

    void returnDetail(const QString& requestId, const QVariantMap& detail)
    {
        const QString filename = detailRequests.value(requestId);
        emit profileDetailResult(requestId, filename, detail, QString());
    }
    void returnKnowledge(const QString& requestId, const QVariantMap& payload)
    {
        const auto request = knowledgeRequests.value(requestId);
        emit knowledgeResult(requestId, request.first, request.second, payload, QString());
    }
    void finishHeldOperation(bool success, const QString& reason = {})
    {
        emit operationCompleted(pendingId, pendingOperation, pendingFilename,
                                success, reason, {});
    }
};

class ProfileServiceTest final : public QObject
{
    Q_OBJECT
private:
    static void clearTestProfileStore()
    {
        const QString base = testProfilesPath();
        for (const QString& sub : {QString(), QStringLiteral("/user"), QStringLiteral("/downloaded")}) {
            QDir dir(base + sub);
            for (const QString& file : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
                QFile::remove(dir.filePath(file));
        }
    }

    static QString testProfilesPath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/profiles");
    }

    static QString testUserProfilesPath()
    {
        return testProfilesPath() + QStringLiteral("/user");
    }

    static void createSavedProfile(McpTestFixture& fixture, const QString& filename,
                                   const QString& title)
    {
        fixture.profileManager.createNewDFlowProfile(title);
        QVERIFY(fixture.profileManager.saveProfileAs(filename, title));
    }

    static QVariantMap row(const QString& filename, const QString& title, int source,
                           const QString& beverage = QStringLiteral("espresso"))
    {
        return {{QStringLiteral("name"), filename}, {QStringLiteral("title"), title},
                {QStringLiteral("source"), source},
                {QStringLiteral("beverageType"), beverage}};
    }

    struct Fixture {
        TestProfileSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        ProfileService* service = nullptr;

        Fixture()
        {
            source.setReady(true);
            source.setLoading(false);
            source.setCatalogRevision(1);
            source.setAllProfilesList({row(QStringLiteral("a.json"), QStringLiteral("A"), 0),
                                       row(QStringLiteral("b.json"), QStringLiteral("B"), 2,
                                           QStringLiteral("tea"))});
            source.setFavoriteProfiles({QVariantMap{{QStringLiteral("name"), QStringLiteral("A")},
                                                    {QStringLiteral("filename"), QStringLiteral("a.json")}}});
            source.setFavoriteProfileOrder(QStringLiteral("custom"));
            source.setFlowCalibration({
                {QStringLiteral("perProfileFlowCalVersion"), 1},
                {QStringLiteral("autoFlowCalibration"), false},
                {QStringLiteral("flowCalibrationMultiplier"), 1.0},
                {QStringLiteral("profiles"), QVariantMap{}}
            });
            source.setInstalledProfileTitles({QStringLiteral("A"), QStringLiteral("B")});
            QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
            QVERIFY(host.enableRemoting<ProfileStateSourceAPI>(&source));
            QVERIFY(client.connectToNode(host.hostUrl()));
            ProfileService::setPublishedNode(&client);
            service = ProfileServiceForeign::create(&engine, &engine);
            QTRY_VERIFY(service->ready());
        }

        ~Fixture() { ProfileService::setPublishedNode(nullptr); }
    };

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(
            QStringLiteral("DecenzaProfileServiceSourceTest-%1").arg(QCoreApplication::applicationPid()));
        clearTestProfileStore();
    }

    void cleanupTestCase() { clearTestProfileStore(); }

    void init() { QTest::failOnWarning(); }

    void nativeSourceCommitsDraftAndOmitsEditorTypeFromDisk()
    {
        clearTestProfileStore();
        McpTestFixture fixture;
        createSavedProfile(fixture, QStringLiteral("service_source"),
                           QStringLiteral("D-Flow / Service source"));
        NativeProfileServiceSource source(&fixture.profileManager, fixture.settings.app());
        QSignalSpy completed(&source, &ProfileStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        QVariantMap draft = fixture.profileManager.editorProfileDetail(QStringLiteral("service_source"));
        const QString revision = draft.value(QStringLiteral("_sourceRevision")).toString();
        QVERIFY(!revision.isEmpty());
        draft.insert(QStringLiteral("target_weight"), 41.5);
        draft.insert(QStringLiteral("editorType"), QStringLiteral("dflow"));
        source.execute(QStringLiteral("save-1"), QStringLiteral("saveDraft"),
                       QStringLiteral("service_source"),
                       {{QStringLiteral("draft"), draft},
                        {QStringLiteral("sourceRevision"), revision},
                        {QStringLiteral("saveAsTitle"), QString()},
                        {QStringLiteral("overwriteRevision"), QString()}}, 0);

        QCOMPARE(completed.size(), 1);
        const QList<QVariant> result = completed.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("save-1"));
        QCOMPARE(result.at(1).toString(), QStringLiteral("saveDraft"));
        QVERIFY(result.at(3).toBool());
        QVERIFY(result.at(4).toString().isEmpty());
        QVERIFY(!result.at(5).toMap().isEmpty());

        QFile stored(testUserProfilesPath() + QStringLiteral("/service_source.json"));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(stored.readAll()).object();
        QCOMPARE(profileJsonToDouble(saved.value(QStringLiteral("target_weight")), -1.0), 41.5);
        QVERIFY(!saved.contains(QStringLiteral("editor_type")));
        QVERIFY(!saved.contains(QStringLiteral("editorType")));
        QVERIFY(!saved.contains(QStringLiteral("recipe")));
    }

    void currentSummaryTracksLoadedAndUnsavedProfileNumbers()
    {
        clearTestProfileStore();
        McpTestFixture fixture;
        const QString filename = QStringLiteral("summary_source");
        const QString title = QStringLiteral("D-Flow / Summary source");
        createSavedProfile(fixture, filename, title);
        QVERIFY(fixture.profileManager.loadProfile(filename));
        const double storedTemp = fixture.profileManager.profileTargetTemperature();
        const double storedYield = fixture.profileManager.profileTargetWeight();
        QVERIFY(storedTemp > 0);
        QVERIFY(storedYield > 0);

        QFile stored(testUserProfilesPath() + QStringLiteral("/summary_source.json"));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        const QByteArray savedBytes = stored.readAll();
        stored.close();

        NativeProfileServiceSource source(&fixture.profileManager, fixture.settings.app());
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ProfileStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        ProfileService::setPublishedNode(&client);
        struct ResetNode { ~ResetNode() { ProfileService::setPublishedNode(nullptr); } } resetNode;
        auto* service = ProfileServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QTRY_COMPARE(service->currentProfileSummary().value(QStringLiteral("filename")).toString(), filename);
        QCOMPARE(service->currentProfileSummary().value(QStringLiteral("espressoTemperatureC")).toDouble(), storedTemp);
        QCOMPARE(service->currentProfileSummary().value(QStringLiteral("targetWeightG")).toDouble(), storedYield);
        const QVariantMap catalog = service->profileCatalogInfoForTitle(title);
        QCOMPARE(catalog.value(QStringLiteral("espressoTemperatureC")).toDouble(), storedTemp);
        QCOMPARE(catalog.value(QStringLiteral("targetWeightG")).toDouble(), storedYield);

        // Change the live profile without saving its catalog entry. The Apply
        // sheet must show these current defaults, rather than the disk defaults.
        const double editedTemp = storedTemp + 2.5;
        const double editedYield = storedYield + 9.5;
        fixture.profileManager.uploadProfile({{QStringLiteral("espresso_temperature"), editedTemp},
                                               {QStringLiteral("target_weight"), editedYield}});
        QVERIFY(fixture.profileManager.isProfileModified());
        QCOMPARE(source.currentProfileSummary().value(QStringLiteral("espressoTemperatureC")).toDouble(), editedTemp);
        QCOMPARE(source.currentProfileSummary().value(QStringLiteral("targetWeightG")).toDouble(), editedYield);
        QTRY_COMPARE(service->currentProfileSummary().value(QStringLiteral("espressoTemperatureC")).toDouble(), editedTemp);
        QTRY_COMPARE(service->currentProfileSummary().value(QStringLiteral("targetWeightG")).toDouble(), editedYield);
        QCOMPARE(service->profileCatalogInfoForTitle(title), catalog);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), savedBytes);
        stored.close();

        // Loading the saved profile again also updates the replicated summary.
        QVERIFY(fixture.profileManager.loadProfile(filename));
        QTRY_COMPARE(service->currentProfileSummary().value(QStringLiteral("espressoTemperatureC")).toDouble(), storedTemp);
        QTRY_COMPARE(service->currentProfileSummary().value(QStringLiteral("targetWeightG")).toDouble(), storedYield);
    }

    void nativeSourceRejectsStaleSourceAndDestinationRevisions()
    {
        clearTestProfileStore();
        McpTestFixture fixture;
        createSavedProfile(fixture, QStringLiteral("service_source"),
                           QStringLiteral("D-Flow / Source"));
        NativeProfileServiceSource source(&fixture.profileManager, fixture.settings.app());
        QSignalSpy completed(&source, &ProfileStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        const QVariantMap staleDraft = fixture.profileManager.editorProfileDetail(
            QStringLiteral("service_source"));
        const QString staleRevision = staleDraft.value(QStringLiteral("_sourceRevision")).toString();
        QVERIFY(!staleRevision.isEmpty());
        QVERIFY(fixture.profileManager.loadProfile(QStringLiteral("service_source")));
        QVariantMap changed = fixture.profileManager.getCurrentProfile();
        changed.insert(QStringLiteral("target_weight"), 44.0);
        fixture.profileManager.uploadProfile(changed);
        QVERIFY(fixture.profileManager.saveProfile(QStringLiteral("service_source")));

        source.execute(QStringLiteral("stale-source"), QStringLiteral("saveDraft"),
                       QStringLiteral("service_source"),
                       {{QStringLiteral("draft"), staleDraft},
                        {QStringLiteral("sourceRevision"), staleRevision},
                        {QStringLiteral("saveAsTitle"), QString()},
                        {QStringLiteral("overwriteRevision"), QString()}}, 0);
        QCOMPARE(completed.size(), 1);
        QList<QVariant> result = completed.takeFirst();
        QVERIFY(!result.at(3).toBool());
        QCOMPARE(result.at(4).toString(), QStringLiteral("conflict"));
        QCOMPARE(fixture.profileManager.getProfileByFilename(QStringLiteral("service_source"))
                     .value(QStringLiteral("target_weight")).toDouble(), 44.0);

        const QString destinationTitle = QStringLiteral("D-Flow / Existing destination");
        QVERIFY(fixture.profileManager.duplicateProfile(QStringLiteral("service_source"), destinationTitle));
        const QString destination = fixture.profileManager.titleToFilename(destinationTitle);
        const QVariantMap currentDraft = fixture.profileManager.editorProfileDetail(
            QStringLiteral("service_source"));
        const QString currentRevision = currentDraft.value(QStringLiteral("_sourceRevision")).toString();
        QVERIFY(!currentRevision.isEmpty());
        const QVariantMap saveAsFields{
            {QStringLiteral("draft"), currentDraft},
            {QStringLiteral("sourceRevision"), currentRevision},
            {QStringLiteral("saveAsTitle"), destinationTitle},
            {QStringLiteral("overwriteRevision"), QString()}
        };
        source.execute(QStringLiteral("destination-probe"), QStringLiteral("saveDraft"),
                       QStringLiteral("service_source"), saveAsFields, 0);
        QCOMPARE(completed.size(), 1);
        result = completed.takeFirst();
        QVERIFY(!result.at(3).toBool());
        QCOMPARE(result.at(4).toString(), QStringLiteral("alreadyExists"));
        const QString overwriteRevision = result.at(5).toMap()
                                              .value(QStringLiteral("overwriteRevision")).toString();
        QVERIFY(!overwriteRevision.isEmpty());

        QVERIFY(fixture.profileManager.loadProfile(destination));
        changed = fixture.profileManager.getCurrentProfile();
        changed.insert(QStringLiteral("target_weight"), 48.0);
        fixture.profileManager.uploadProfile(changed);
        QVERIFY(fixture.profileManager.saveProfile(destination));
        QVariantMap staleOverwriteFields = saveAsFields;
        staleOverwriteFields.insert(QStringLiteral("overwriteRevision"), overwriteRevision);
        source.execute(QStringLiteral("destination-stale"), QStringLiteral("saveDraft"),
                       QStringLiteral("service_source"), staleOverwriteFields, 0);
        QCOMPARE(completed.size(), 1);
        result = completed.takeFirst();
        QVERIFY(!result.at(3).toBool());
        QCOMPARE(result.at(4).toString(), QStringLiteral("conflict"));
        QCOMPARE(fixture.profileManager.getProfileByFilename(destination)
                     .value(QStringLiteral("target_weight")).toDouble(), 48.0);
    }

    void nativeSourceTryDraftReturnsWithoutPersistingSource()
    {
        clearTestProfileStore();
        McpTestFixture fixture;
        createSavedProfile(fixture, QStringLiteral("service_source"),
                           QStringLiteral("D-Flow / Try source"));
        NativeProfileServiceSource source(&fixture.profileManager, fixture.settings.app());
        QSignalSpy completed(&source, &ProfileStateSimpleSource::operationCompleted);
        QVERIFY(completed.isValid());

        QFile stored(testUserProfilesPath() + QStringLiteral("/service_source.json"));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        const QByteArray before = stored.readAll();
        stored.close();
        QVariantMap draft = fixture.profileManager.editorProfileDetail(QStringLiteral("service_source"));
        const QString revision = draft.value(QStringLiteral("_sourceRevision")).toString();
        QVERIFY(!revision.isEmpty());
        draft.insert(QStringLiteral("target_weight"), 52.0);
        source.execute(QStringLiteral("try-1"), QStringLiteral("tryDraft"),
                       QStringLiteral("service_source"),
                       {{QStringLiteral("draft"), draft},
                        {QStringLiteral("sourceRevision"), revision}}, 0);

        QCOMPARE(completed.size(), 1);
        const QList<QVariant> result = completed.takeFirst();
        QVERIFY(result.at(3).toBool());
        QVERIFY(result.at(4).toString().isEmpty());
        QVERIFY(!result.at(5).toMap().isEmpty());
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), before);
        stored.close();
        QVERIFY(QFile::exists(testProfilesPath() + QStringLiteral("/_current.json")));

        source.execute(QStringLiteral("try-stale"), QStringLiteral("tryDraft"),
                       QStringLiteral("service_source"),
                       {{QStringLiteral("draft"), draft},
                        {QStringLiteral("sourceRevision"), QString()}}, 0);
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> rejected = completed.takeFirst();
        QVERIFY(!rejected.at(3).toBool());
        QCOMPARE(rejected.at(4).toString(), QStringLiteral("conflict"));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), before);
    }
    void disconnectNotificationCanDeleteFacade()
    {
        Fixture f;
        QPointer<ProfileService> service = f.service;
        f.source.holdOperation = true;
        service->loadProfile(QStringLiteral("a.json"));
        QTRY_COMPARE(f.source.executeCalls, 1);
        service->requestProfileDetail(QStringLiteral("a.json"));
        connect(service.data(), &ProfileService::busyChanged, this, [&] {
            if (!service->busy()) delete service.data();
        });
        service->setNode(nullptr);
        QVERIFY(service.isNull());
        QCoreApplication::sendPostedEvents();
    }

    void catalogFilterAndFacetUseReplicatedRows()
    {
        Fixture f;
        const QVariantList filtered = f.service->filterProfiles(
            {{QStringLiteral("favorites"), true}}, QString(), {});
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("a.json"));

        const QVariantMap counts = f.service->facetCounts({}, QString(), {});
        QCOMPARE(counts.value(QStringLiteral("favorites")).toInt(), 1);
        QCOMPARE(counts.value(QStringLiteral("tea")).toInt(), 1);
        QCOMPARE(f.service->installedProfileTitles(),
                 QStringList({QStringLiteral("A"), QStringLiteral("B")}));
        QCOMPARE(f.service->beverageBucketForTitle(QStringLiteral("A")), QStringLiteral("espresso"));
        QCOMPARE(f.service->beverageBucketForTitle(QStringLiteral("B")), QStringLiteral("tea"));
        QCOMPARE(f.service->beverageBucketForTitle(QStringLiteral("missing")), QString());
    }

    void everyReplicatedStateFieldUpdatesTheFacade()
    {
        Fixture f;
        QSignalSpy stateChanged(f.service, &ProfileService::stateChanged);
        f.source.setErrorMessage(QStringLiteral("catalog warning"));
        f.source.setReady(false);
        f.source.setLoading(true);
        f.source.setCatalogRevision(8);
        f.source.setAllProfilesList({row(QStringLiteral("c.json"), QStringLiteral("C"), 1)});
        f.source.setAvailableProfiles({row(QStringLiteral("c.json"), QStringLiteral("C"), 1)});
        f.source.setInstalledProfileTitles({QStringLiteral("C")});
        f.source.setBaseProfileName(QStringLiteral("c.json"));
        f.source.setPreviousProfileName(QStringLiteral("b.json"));
        f.source.setProfileModified(true);
        f.source.setCurrentProfileReadOnly(true);
        f.source.setProfileUsage({{QStringLiteral("c.json"), 4}});
        f.source.setFavoriteProfiles({QVariantMap{{QStringLiteral("name"), QStringLiteral("C")},
                                                {QStringLiteral("filename"), QStringLiteral("c.json")}}});
        f.source.setFavoriteProfileOrder(QStringLiteral("custom"));
        f.source.setAutoLoadProfileFilename(QStringLiteral("c.json"));
        f.source.setAutoLoadRevertMinutes(7);
        f.source.setProfileSummaries({{QStringLiteral("C"), QVariantMap{{QStringLiteral("baseTempC"), 93.0}}}});
        f.source.setCurrentProfileSummary({{QStringLiteral("name"), QStringLiteral("C")},
                                           {QStringLiteral("stepTempsC"), QVariantList{93.0, 94.0}}});
        f.source.setRoastAffinities({{QStringLiteral("C"), QStringList{QStringLiteral("light")}}});
        f.source.setGrindDirections({{QStringLiteral("C"), QVariantMap{{QStringLiteral("A"), QStringLiteral("finer")}}}});
        f.source.setFlowCalibration({
            {QStringLiteral("perProfileFlowCalVersion"), 2},
            {QStringLiteral("autoFlowCalibration"), true},
            {QStringLiteral("flowCalibrationMultiplier"), 1.23},
            {QStringLiteral("profiles"), QVariantMap{
                {QStringLiteral("c.json"), QVariantMap{
                    {QStringLiteral("effectiveFlowCalibration"), 1.7},
                    {QStringLiteral("hasProfileFlowCalibration"), true}
                }}
            }}
        });

        QTRY_COMPARE(f.service->errorMessage(), QStringLiteral("catalog warning"));
        QTRY_VERIFY(!f.service->ready());
        QTRY_VERIFY(f.service->loading());
        QTRY_COMPARE(f.service->catalogRevision(), qint64(8));
        QTRY_COMPARE(f.service->allProfilesList().size(), 1);
        QTRY_COMPARE(f.service->availableProfiles().size(), 1);
        QTRY_COMPARE(f.service->installedProfileTitles(), QStringList{QStringLiteral("C")});
        QTRY_COMPARE(f.service->baseProfileName(), QStringLiteral("c.json"));
        QTRY_COMPARE(f.service->previousProfileName(), QStringLiteral("b.json"));
        QTRY_VERIFY(f.service->profileModified());
        QTRY_VERIFY(f.service->isCurrentProfileReadOnly());
        QTRY_COMPARE(f.service->profileUsage().value(QStringLiteral("c.json")).toInt(), 4);
        QTRY_VERIFY(f.service->isFavoriteProfile(QStringLiteral("c.json")));
        QTRY_COMPARE(f.service->favoriteProfileOrder(), QStringLiteral("custom"));
        QTRY_COMPARE(f.service->autoLoadProfileFilename(), QStringLiteral("c.json"));
        QTRY_COMPARE(f.service->autoLoadRevertMinutes(), 7);
        QTRY_COMPARE(f.service->profileSummaryForTitle(QStringLiteral("C"))
                         .value(QStringLiteral("baseTempC")).toDouble(), 93.0);
        QTRY_COMPARE(f.service->currentProfileSummary().value(QStringLiteral("name")).toString(),
                     QStringLiteral("C"));
        QTRY_COMPARE(f.service->roastAffinities().value(QStringLiteral("C")).toStringList(),
                     QStringList{QStringLiteral("light")});
        QTRY_COMPARE(f.service->grindDirectionBetween(QStringLiteral("C"), QStringLiteral("A")),
                     QStringLiteral("finer"));
        QTRY_COMPARE(f.service->flowCalibration().value(QStringLiteral("perProfileFlowCalVersion")).toInt(), 2);
        QTRY_VERIFY(f.service->flowCalibration().value(QStringLiteral("autoFlowCalibration")).toBool());
        QTRY_COMPARE(f.service->flowCalibration().value(QStringLiteral("flowCalibrationMultiplier")).toDouble(), 1.23);
        QTRY_COMPARE(f.service->flowCalibration().value(QStringLiteral("profiles")).toMap()
                         .value(QStringLiteral("c.json")).toMap()
                         .value(QStringLiteral("effectiveFlowCalibration")).toDouble(), 1.7);
        f.source.setReady(true);
        QTRY_VERIFY(f.service->ready());
        QVERIFY(stateChanged.count() > 0);
    }

    void detailsAreCorrelatedCachedAndInvalidatedByCatalogRevision()
    {
        Fixture f;
        QSignalSpy detailReady(f.service, &ProfileService::profileDetailReady);
        const QString requestId = f.service->requestProfileDetail(QStringLiteral("a.json"));
        const QString secondId = f.service->requestProfileDetail(QStringLiteral("b.json"));
        QVERIFY(!requestId.isEmpty() && !secondId.isEmpty());
        QTRY_COMPARE(f.source.detailCalls, 2);
        emit f.source.profileDetailResult(QStringLiteral("unknown-id"), QStringLiteral("a.json"),
                                          {{QStringLiteral("title"), QStringLiteral("wrong")}}, {});
        QCoreApplication::processEvents();
        QCOMPARE(detailReady.count(), 0);
        f.source.returnDetail(secondId, {{QStringLiteral("title"), QStringLiteral("B")},
                                         {QStringLiteral("steps"), QVariantList{4, 5}}});
        QTRY_COMPARE(detailReady.count(), 1);
        QCOMPARE(detailReady.first().at(0).toString(), secondId);
        QCOMPARE(detailReady.first().at(1).toString(), QStringLiteral("b.json"));
        f.source.returnDetail(requestId, {{QStringLiteral("title"), QStringLiteral("A")},
                                         {QStringLiteral("steps"), QVariantList{1, 2, 3}}});
        QTRY_COMPARE(detailReady.count(), 2);
        QCOMPARE(detailReady.last().at(0).toString(), requestId);
        QCOMPARE(f.service->cachedProfileByFilename(QStringLiteral("a.json"))
                     .value(QStringLiteral("steps")).toList().size(), 3);
        QCOMPARE(f.service->cachedProfileByFilename(QStringLiteral("b.json"))
                     .value(QStringLiteral("steps")).toList().size(), 2);

        f.source.setCatalogRevision(2);
        QTRY_VERIFY(f.service->cachedProfileByFilename(QStringLiteral("a.json")).isEmpty());
        QTRY_VERIFY(f.service->cachedProfileByFilename(QStringLiteral("b.json")).isEmpty());

        QSignalSpy knowledgeReady(f.service, &ProfileService::knowledgeReady);
        const QString knowledgeId = f.service->requestKnowledge(
            QStringLiteral("A"), QStringLiteral("{\"title\":\"A\"}"));
        QTRY_VERIFY(f.source.knowledgeRequests.contains(knowledgeId));
        emit f.source.knowledgeResult(QStringLiteral("unrelated-id"), QStringLiteral("A"),
                                      QStringLiteral("{}"), {}, {});
        emit f.source.knowledgeResult(knowledgeId, QStringLiteral("A"),
                                      QStringLiteral("{\"title\":\"wrong\"}"), {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(knowledgeReady.count(), 0);
        f.source.returnKnowledge(knowledgeId,
            {{QStringLiteral("hasKnowledge"), true}, {QStringLiteral("content"), QStringLiteral("notes")}});
        QTRY_COMPARE(knowledgeReady.count(), 1);
        QCOMPARE(knowledgeReady.first().at(0).toString(), knowledgeId);
        QCOMPARE(knowledgeReady.first().at(1).toString(), QStringLiteral("A"));
        QCOMPARE(knowledgeReady.first().at(2).toString(), QStringLiteral("{\"title\":\"A\"}"));
        QCOMPARE(knowledgeReady.first().at(3).toMap().value(QStringLiteral("content")).toString(),
                 QStringLiteral("notes"));
    }

    void mutationsCarryCurrentRevisionAndDisconnectDoesNotReplay()
    {
        Fixture f;
        QSignalSpy completed(f.service, &ProfileService::operationCompleted);
        const QString completedId = f.service->toggleFavoriteProfile(QStringLiteral("a.json"));
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), completedId);
        QVERIFY(completed.first().at(3).toBool());
        QCOMPARE(f.source.lastExpectedRevision, qint64(1));
        QCOMPARE(f.source.executeCalls, 1);

        f.source.holdOperation = true;
        const QString pendingId = f.service->renameProfile(QStringLiteral("b.json"),
                                                           QStringLiteral("B renamed"));
        QTRY_VERIFY(!f.source.pendingId.isEmpty());
        QCOMPARE(f.source.lastFields.value(QStringLiteral("title")).toString(),
                 QStringLiteral("B renamed"));
        const int callsWhileHeld = f.source.executeCalls;
        const QString rejectedId = f.service->deleteProfile(QStringLiteral("a.json"));
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), rejectedId);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("busy"));
        QCOMPARE(f.source.executeCalls, callsWhileHeld);
        f.service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), pendingId);
        QVERIFY(!completed.last().at(3).toBool());
        const int calls = f.source.executeCalls;
        f.service->setNode(&f.client);
        QTRY_VERIFY(f.service->ready());
        QCoreApplication::processEvents();
        QCOMPARE(f.source.executeCalls, calls);
        QCOMPARE(completed.count(), 3);
    }
};

QTEST_GUILESS_MAIN(ProfileServiceTest)
#include "tst_profileservice.moc"
