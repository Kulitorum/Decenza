#include <QtTest>

#include <QDateTime>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QUuid>
#include <limits>

#include "../src/core/settings.h"
#include "../src/core/settings_graph.h"
#include "../src/history/shothistorystorage.h"
#include "../src/remote/recipecontextservice.h"
#include "../src/remote/recipecontextservice_qml.h"
#include "../src/remote/recipecontextservicesource.h"

class TestRecipeContextSource final : public RecipeContextStateSimpleSource
{
    Q_OBJECT
public:
    using RecipeContextStateSimpleSource::setHistoryReady;
    using RecipeContextStateSimpleSource::setPreferences;
    using RecipeContextStateSimpleSource::setReady;
    using RecipeContextStateSimpleSource::setRevision;

    int preferenceCalls = 0;
    int historyCalls = 0;
    int metadataCalls = 0;
    int baselineCalls = 0;
    int presetEditCalls = 0;
    quint64 lastExpectedRevision = 0;
    QString lastRequestId;
    QString pendingPreferenceId;
    QString pendingPreferenceName;
    QVariant pendingPreferenceValue;
    QString pendingPresetDomain;
    QString pendingPresetOperation;
    QVariantMap pendingPresetFields;
    QString lastOperation;
    QVariantMap lastFields;
    QVariantMap lastBaselineFields;
    qint64 lastShotId = 0;
    bool holdHistory = true;
    bool holdPreference = false;
    bool holdPresetEdit = false;

    void refresh() override {}

    void setPreference(QString requestId, QString name, QVariant value,
                       quint64 expectedRevision) override
    {
        ++preferenceCalls;
        lastExpectedRevision = expectedRevision;
        if (holdPreference) {
            pendingPreferenceId = std::move(requestId);
            pendingPreferenceName = std::move(name);
            pendingPreferenceValue = std::move(value);
            return;
        }
        finishPreference(requestId, name, value, expectedRevision);
    }

    void applyShotBaseline(QString requestId, QVariantMap fields,
                           quint64 expectedRevision) override
    {
        ++baselineCalls;
        lastRequestId = requestId;
        lastBaselineFields = fields;
        lastExpectedRevision = expectedRevision;
        if (expectedRevision != revision()) {
            emit shotBaselineCompleted(requestId, false, revision(), preferences(),
                                       QStringLiteral("stalePreferences"));
            return;
        }
        QVariantMap current = preferences();
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            current.insert(it.key(), it.value());
        setPreferences(current);
        setRevision(revision() + 1);
        emit shotBaselineCompleted(requestId, true, revision(), preferences(), {});
    }

    void executePresetEdit(QString requestId, QString domain, QString operation,
                           QVariantMap fields, quint64 expectedRevision) override
    {
        ++presetEditCalls;
        lastRequestId = requestId;
        pendingPresetDomain = domain;
        pendingPresetOperation = operation;
        pendingPresetFields = fields;
        lastExpectedRevision = expectedRevision;
        if (holdPresetEdit)
            return;
        if (expectedRevision != revision()) {
            emit presetEditCompleted(requestId, domain, operation, false, revision(),
                                     preferences(), QStringLiteral("stalePreferences"));
            return;
        }
        setRevision(revision() + 1);
        emit presetEditCompleted(requestId, domain, operation, true, revision(),
                                 preferences(), {});
    }

    void finishPreference(const QString& requestId, const QString& name,
                          const QVariant& value, quint64 expectedRevision)
    {
        if (expectedRevision != revision()) {
            emit preferenceCompleted(requestId, name, false, preferences(),
                                    QStringLiteral("stalePreferences"));
            return;
        }
        QVariantMap current = preferences();
        current.insert(name, value);
        setPreferences(current);
        setRevision(revision() + 1);
        emit preferenceCompleted(requestId, name, true, preferences(), {});
    }

    void finishHeldPreference()
    {
        finishPreference(pendingPreferenceId, pendingPreferenceName,
                         pendingPreferenceValue, lastExpectedRevision);
    }

    void requestHistory(QString requestId, QString operation,
                        QVariantMap fields) override
    {
        ++historyCalls;
        lastRequestId = std::move(requestId);
        lastOperation = std::move(operation);
        lastFields = std::move(fields);
        if (!holdHistory)
            finishHistory(lastRequestId, lastOperation,
                          {{QStringLiteral("values"), QStringList{QStringLiteral("one")}}});
    }

    void requestShotMetadataUpdate(QString requestId, qint64 shotId,
                                   QVariantMap metadata) override
    {
        ++metadataCalls;
        lastRequestId = std::move(requestId);
        lastOperation = QStringLiteral("updateShotMetadata");
        lastFields = std::move(metadata);
        lastShotId = shotId;
    }

    void finishHistory(const QString& requestId, const QString& operation,
                       const QVariantMap& result = {})
    {
        emit historyCompleted(requestId, operation, true, result, {});
    }

    void finishFailure(const QString& requestId, const QString& operation,
                       const QString& reason)
    {
        emit historyCompleted(requestId, operation, false, {}, reason);
    }
};

class RecipeContextServiceTest final : public QObject
{
    Q_OBJECT
private:
    struct Fixture {
        TestRecipeContextSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        RecipeContextService* service = nullptr;

        Fixture()
        {
            source.setReady(true);
            source.setHistoryReady(true);
            source.setPreferences({{QStringLiteral("ratioPreset1"), 8.0}});
            source.setRevision(4);
            QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
            QVERIFY(host.enableRemoting<RecipeContextStateSourceAPI>(&source));
            QVERIFY(client.connectToNode(host.hostUrl()));
            RecipeContextService::setPublishedNode(&client);
            service = RecipeContextServiceForeign::create(&engine, &engine);
            QTRY_VERIFY(service->ready());
        }

        ~Fixture() { RecipeContextService::setPublishedNode(nullptr); }
    };

private slots:
    void init() { QTest::failOnWarning(); }
    void shotBaselineIsOneCorrelatedRevisionGuardedOperation()
    {
        Fixture f;
        QSignalSpy completed(f.service, &RecipeContextService::shotBaselineCompleted);
        const QVariantMap fields{{QStringLiteral("dyeBeanBrand"), QStringLiteral("Roaster")},
                                 {QStringLiteral("dyeBeanWeight"), 18.5}};
        const QString requestId = f.service->newRequestId();
        f.service->applyShotBaseline(requestId, fields, 4);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(f.source.baselineCalls, 1);
        QCOMPARE(f.source.lastRequestId, requestId);
        QCOMPARE(f.source.lastExpectedRevision, quint64(4));
        QCOMPARE(f.source.lastBaselineFields, fields);
        QVERIFY(completed.first().at(1).toBool());
        QCOMPARE(completed.first().at(2).toULongLong(), quint64(5));

        const QString staleId = f.service->newRequestId();
        f.service->applyShotBaseline(staleId, fields, 4);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), staleId);
        QVERIFY(!completed.last().at(1).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("stalePreferences"));
    }

    void preferenceCasUsesPublishedRevision()
    {
        Fixture f;
        QSignalSpy completed(f.service, &RecipeContextService::preferenceCompleted);
        const QString requestId = f.service->newRequestId();
        f.service->setPreference(requestId, QStringLiteral("ratioPreset1"), 9.0, 4);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(f.source.lastExpectedRevision, quint64(4));
        QCOMPARE(f.service->revision(), quint64(5));
        QCOMPARE(f.service->preferences().value(QStringLiteral("ratioPreset1")).toDouble(), 9.0);

        const QString staleId = f.service->newRequestId();
        f.service->setPreference(staleId, QStringLiteral("ratioPreset1"), 10.0, 4);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), staleId);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.service->preferences().value(QStringLiteral("ratioPreset1")).toDouble(), 9.0);
    }

    void presetEditFacadeCorrelatesAndFailsPendingOnDisconnect()
    {
        Fixture f;
        QSignalSpy completed(f.service, &RecipeContextService::presetEditCompleted);
        const QVariantMap fields{{QStringLiteral("index"), 3},
                                 {QStringLiteral("expectedName"), QStringLiteral("Pitcher")},
                                 {QStringLiteral("name"), QStringLiteral("Pitcher new")},
                                 {QStringLiteral("volume"), 240},
                                 {QStringLiteral("mode"), QStringLiteral("weight")},
                                 {QStringLiteral("flowRate"), 45},
                                 {QStringLiteral("temperature"), 90.0}};
        const QString requestId = f.service->newRequestId();
        f.service->executePresetEdit(requestId, QStringLiteral("waterVessel"),
                                     QStringLiteral("update"), fields, 4);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(f.source.presetEditCalls, 1);
        QCOMPARE(f.source.lastRequestId, requestId);
        QCOMPARE(f.source.pendingPresetDomain, QStringLiteral("waterVessel"));
        QCOMPARE(f.source.pendingPresetOperation, QStringLiteral("update"));
        QCOMPARE(f.source.pendingPresetFields, fields);
        QCOMPARE(f.source.lastExpectedRevision, quint64(4));
        QCOMPARE(completed.last().at(0).toString(), requestId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("waterVessel"));
        QCOMPARE(completed.last().at(2).toString(), QStringLiteral("update"));
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(4).toULongLong(), quint64(5));

        f.source.holdPresetEdit = true;
        const QString pendingId = f.service->newRequestId();
        f.service->executePresetEdit(pendingId, QStringLiteral("flush"),
                                     QStringLiteral("commit"), {}, f.service->revision());
        QTRY_COMPARE(f.source.presetEditCalls, 2);
        f.service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), pendingId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("flush"));
        QCOMPARE(completed.last().at(2).toString(), QStringLiteral("commit"));
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(6).toString(), QStringLiteral("disconnected"));

        emit f.source.presetEditCompleted(pendingId, QStringLiteral("flush"),
                                          QStringLiteral("commit"), true, 6, {}, {});
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 2);
    }

    void concurrentHistoryRequestsRemainScoped()
    {
        Fixture f;
        QSignalSpy completed(f.service, &RecipeContextService::historyCompleted);
        const QString brandsId = f.service->newRequestId();
        const QString typesId = f.service->newRequestId();
        f.service->requestHistory(brandsId, QStringLiteral("distinctBeanBrands"));
        f.service->requestHistory(typesId, QStringLiteral("distinctBeanTypesForBrand"),
                                  {{QStringLiteral("beanBrand"), QStringLiteral("Roaster")}});
        QTRY_COMPARE(f.source.historyCalls, 2);
        QCOMPARE(f.source.lastOperation, QStringLiteral("distinctBeanTypesForBrand"));
        QCOMPARE(f.source.lastFields.value(QStringLiteral("beanBrand")).toString(),
                 QStringLiteral("Roaster"));

        f.source.finishHistory(typesId, QStringLiteral("distinctBeanTypesForBrand"),
                               {{QStringLiteral("values"), QStringList{QStringLiteral("Coffee")}}});
        f.source.finishHistory(brandsId, QStringLiteral("distinctBeanBrands"),
                               {{QStringLiteral("values"), QStringList{QStringLiteral("Roaster")}}});
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.at(0).at(0).toString(), typesId);
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("distinctBeanTypesForBrand"));
        QCOMPARE(completed.at(0).at(3).toMap().value(QStringLiteral("values")).toStringList(),
                 QStringList{QStringLiteral("Coffee")});
        QCOMPARE(completed.at(1).at(0).toString(), brandsId);
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("distinctBeanBrands"));
    }

    void preferenceDisconnectFailsPendingWithoutAcceptingLateCompletion()
    {
        Fixture f;
        f.source.holdPreference = true;
        QSignalSpy completed(f.service, &RecipeContextService::preferenceCompleted);
        const QString requestId = f.service->newRequestId();
        f.service->setPreference(requestId, QStringLiteral("ratioPreset2"), 12.0,
                                 f.service->revision());
        QTRY_COMPARE(f.source.preferenceCalls, 1);
        f.service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));
        QVERIFY(f.service->preferences().isEmpty());

        emit f.source.preferenceCompleted(requestId, QStringLiteral("ratioPreset2"), true,
                                          {{QStringLiteral("ratioPreset2"), 12.0}}, {});
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
        f.service->setNode(&f.client);
        QTRY_VERIFY(f.service->ready());
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
        QCOMPARE(f.service->preferences().value(QStringLiteral("ratioPreset2")).toDouble(), 0.0);
    }

    void metadataUpdateCarriesShotAndFieldsAndCompletesCorrelated()
    {
        Fixture f;
        QSignalSpy completed(f.service, &RecipeContextService::historyCompleted);
        const QString requestId = f.service->newRequestId();
        const QVariantMap fields{{QStringLiteral("bagId"), 42}};
        f.service->requestShotMetadataUpdate(requestId, 73, fields);
        QTRY_COMPARE(f.source.metadataCalls, 1);
        QCOMPARE(f.source.lastRequestId, requestId);
        QCOMPARE(f.source.lastShotId, qint64(73));
        QCOMPARE(f.source.lastFields, fields);
        f.source.finishHistory(requestId, QStringLiteral("updateShotMetadata"),
                               {{QStringLiteral("shotId"), 73}});
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QCOMPARE(completed.first().at(1).toString(), QStringLiteral("updateShotMetadata"));
        QVERIFY(completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(3).toMap().value(QStringLiteral("shotId")).toLongLong(),
                 qint64(73));
    }

    void disconnectFailsPendingHistoryWithoutReplay()
    {
        Fixture f;
        QSignalSpy completed(f.service, &RecipeContextService::historyCompleted);
        const QString requestId = f.service->newRequestId();
        f.service->requestHistory(requestId, QStringLiteral("distinctBeanBrands"));
        QTRY_COMPARE(f.source.historyCalls, 1);
        f.service->setNode(nullptr);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.first().at(0).toString(), requestId);
        QVERIFY(!completed.first().at(2).toBool());
        QCOMPARE(completed.first().at(4).toString(), QStringLiteral("disconnected"));

        f.service->setNode(&f.client);
        QTRY_VERIFY(f.service->ready());
        QCoreApplication::processEvents();
        QCOMPARE(completed.count(), 1);
        QCOMPARE(f.source.historyCalls, 1);
    }

    void nativePresetEditsValidateCasAndUseCanonicalSettingsBrew()
    {
        Settings settings;
        const QVariantList originalWater = settings.brew()->waterVesselPresets();
        const QVariantList originalFlush = settings.brew()->flushPresets();
        const QVariant originalWaterJson = settings.value(QStringLiteral("water/vesselPresets"));
        const QVariant originalFlushJson = settings.value(QStringLiteral("flush/presets"));
        auto* brew = settings.brew();
        auto* hardware = settings.hardware();
        struct RestorePresets {
            Settings* settings;
            QVariant water;
            QVariant flush;
            int selectedWater;
            int selectedFlush;
            int waterVolume;
            QString waterMode;
            double waterTemperature;
            int hotWaterFlowRate;
            double flushFlow;
            double flushSeconds;
            ~RestorePresets()
            {
                settings->brew()->setSelectedWaterCup(selectedWater);
                settings->brew()->setSelectedFlushPreset(selectedFlush);
                settings->brew()->setWaterVolume(waterVolume);
                settings->brew()->setWaterVolumeMode(waterMode);
                settings->brew()->setWaterTemperature(waterTemperature);
                settings->hardware()->setHotWaterFlowRate(hotWaterFlowRate);
                settings->brew()->setFlushFlow(flushFlow);
                settings->brew()->setFlushSeconds(flushSeconds);
                settings->setValue(QStringLiteral("water/vesselPresets"), water);
                settings->setValue(QStringLiteral("flush/presets"), flush);
            }
        } restore{&settings, originalWaterJson, originalFlushJson,
                  brew->selectedWaterVessel(), brew->selectedFlushPreset(), brew->waterVolume(),
                  brew->waterVolumeMode(), brew->waterTemperature(), hardware->hotWaterFlowRate(),
                  brew->flushFlow(), brew->flushSeconds()};

        NativeRecipeContextSource source(&settings, nullptr);
        QVERIFY(source.ready());
        QSignalSpy completed(&source, &RecipeContextStateSimpleSource::presetEditCompleted);
        const auto request = [] {
            return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        };

        const QString waterName = QStringLiteral("Shared Water %1")
                                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QVariantMap waterFields{{QStringLiteral("name"), waterName},
                                      {QStringLiteral("volume"), 230},
                                      {QStringLiteral("mode"), QStringLiteral("weight")},
                                      {QStringLiteral("flowRate"), 45},
                                      {QStringLiteral("temperature"), 91.5}};
        const quint64 beforeWaterAdd = source.revision();
        const QString waterAddId = request();
        source.executePresetEdit(waterAddId, QStringLiteral("waterVessel"),
                                 QStringLiteral("add"), waterFields, beforeWaterAdd);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.last().at(0).toString(), waterAddId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("waterVessel"));
        QCOMPARE(completed.last().at(2).toString(), QStringLiteral("add"));
        QVERIFY(completed.last().at(3).toBool());
        QVERIFY(source.revision() > beforeWaterAdd);
        const int waterIndex = static_cast<int>(settings.brew()->waterVesselPresets().size()) - 1;
        QCOMPARE(settings.brew()->getWaterVesselPreset(waterIndex).value(QStringLiteral("name")).toString(),
                 waterName);

        // A stale editor cannot overwrite a row after another settings write has
        // advanced the published revision.
        QVariantMap updateWater = waterFields;
        updateWater.insert(QStringLiteral("index"), waterIndex);
        updateWater.insert(QStringLiteral("expectedName"), waterName);
        updateWater[QStringLiteral("name")] = waterName.toUpper();
        const QString staleWaterId = request();
        source.executePresetEdit(staleWaterId, QStringLiteral("waterVessel"),
                                 QStringLiteral("update"), updateWater, beforeWaterAdd);
        QCOMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(6).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(settings.brew()->getWaterVesselPreset(waterIndex).value(QStringLiteral("name")).toString(),
                 waterName);

        // Water names are canonical identifiers and remain case-insensitive
        // unique, including through this remote edit boundary.
        QVariantMap duplicateWater = waterFields;
        duplicateWater[QStringLiteral("name")] = waterName.toUpper();
        const QString duplicateWaterId = request();
        source.executePresetEdit(duplicateWaterId, QStringLiteral("waterVessel"),
                                 QStringLiteral("add"), duplicateWater, source.revision());
        QCOMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->waterVesselPresets().size(), originalWater.size() + 1);

        const QString updateWaterId = request();
        source.executePresetEdit(updateWaterId, QStringLiteral("waterVessel"),
                                 QStringLiteral("update"), updateWater, source.revision());
        QCOMPARE(completed.count(), 4);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->getWaterVesselPreset(waterIndex).value(QStringLiteral("name")).toString(),
                 waterName.toUpper());

        const QString secondWaterName = waterName + QStringLiteral(" second");
        QVariantMap secondWater = waterFields;
        secondWater[QStringLiteral("name")] = secondWaterName;
        const QString secondWaterAddId = request();
        source.executePresetEdit(secondWaterAddId, QStringLiteral("waterVessel"),
                                 QStringLiteral("add"), secondWater, source.revision());
        QCOMPARE(completed.count(), 5);
        QVERIFY(completed.last().at(3).toBool());
        const int secondWaterIndex = static_cast<int>(settings.brew()->waterVesselPresets().size()) - 1;
        const QVariantMap waterMove{{QStringLiteral("fromIndex"), secondWaterIndex},
                                    {QStringLiteral("expectedFromName"), secondWaterName},
                                    {QStringLiteral("toIndex"), waterIndex},
                                    {QStringLiteral("expectedToName"), waterName.toUpper()}};
        const QString waterMoveId = request();
        source.executePresetEdit(waterMoveId, QStringLiteral("waterVessel"),
                                 QStringLiteral("move"), waterMove, source.revision());
        QCOMPARE(completed.count(), 6);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->getWaterVesselPreset(waterIndex).value(QStringLiteral("name")).toString(),
                 secondWaterName);
        const QVariantMap waterRemove{{QStringLiteral("index"), waterIndex},
                                      {QStringLiteral("expectedName"), secondWaterName}};
        const QString waterRemoveId = request();
        source.executePresetEdit(waterRemoveId, QStringLiteral("waterVessel"),
                                 QStringLiteral("remove"), waterRemove, source.revision());
        QCOMPARE(completed.count(), 7);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->waterVesselPresets().size(), originalWater.size() + 1);

        QSignalSpy applySpy(&source, &NativeRecipeContextSource::presetApplyRequested);
        QVariantMap waterCommit = updateWater;
        waterCommit[QStringLiteral("expectedName")] = waterName.toUpper();
        waterCommit[QStringLiteral("volume")] = 245;
        waterCommit[QStringLiteral("mode")] = QStringLiteral("volume");
        waterCommit[QStringLiteral("flowRate")] = 55;
        waterCommit[QStringLiteral("temperature")] = 94.0;
        settings.brew()->setSelectedWaterCup(waterIndex);
        const QString staleCommitId = request();
        source.executePresetEdit(staleCommitId, QStringLiteral("waterVessel"),
                                 QStringLiteral("commit"), waterCommit,
                                 source.revision() - 1);
        QCOMPARE(completed.count(), 8);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(6).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(applySpy.count(), 0);

        QVariantMap wrongSelectionCommit = waterCommit;
        wrongSelectionCommit[QStringLiteral("index")] = 0;
        wrongSelectionCommit[QStringLiteral("expectedName")]
            = settings.brew()->getWaterVesselPreset(0).value(QStringLiteral("name"));
        const QString wrongSelectionId = request();
        source.executePresetEdit(wrongSelectionId, QStringLiteral("waterVessel"),
                                 QStringLiteral("commit"), wrongSelectionCommit,
                                 source.revision());
        QCOMPARE(completed.count(), 9);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(applySpy.count(), 0);
        QCOMPARE(settings.brew()->getWaterVesselPreset(waterIndex).value(QStringLiteral("volume")).toInt(),
                 230);

        const QString waterCommitId = request();
        QSignalSpy commitPreferencesChanged(&source,
                                            &RecipeContextStateSimpleSource::preferencesChanged);
        QSignalSpy commitRevisionChanged(&source,
                                         &RecipeContextStateSimpleSource::revisionChanged);
        source.executePresetEdit(waterCommitId, QStringLiteral("waterVessel"),
                                 QStringLiteral("commit"), waterCommit, source.revision());
        QCOMPARE(completed.count(), 10);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(commitPreferencesChanged.count(), 1);
        QCOMPARE(commitRevisionChanged.count(), 1);
        QCOMPARE(applySpy.count(), 1);
        QCOMPARE(applySpy.first().at(0).toString(), QStringLiteral("waterVessel"));
        QCOMPARE(settings.brew()->waterVolume(), 245);
        QCOMPARE(settings.brew()->waterVolumeMode(), QStringLiteral("volume"));
        QCOMPARE(settings.hardware()->hotWaterFlowRate(), 55);
        QCOMPARE(settings.brew()->waterTemperature(), 94.0);

        QVariantMap malformedWater = updateWater;
        malformedWater.insert(QStringLiteral("surprise"), true);
        const QString malformedId = request();
        const qsizetype beforeMalformed = completed.count();
        source.executePresetEdit(malformedId, QStringLiteral("waterVessel"),
                                 QStringLiteral("update"), malformedWater, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->getWaterVesselPreset(waterIndex).value(QStringLiteral("name")).toString(),
                 waterName.toUpper());

        const QVariantMap flushFields{{QStringLiteral("name"), QStringLiteral("Repeated Flush")},
                                      {QStringLiteral("flow"), 6.5},
                                      {QStringLiteral("seconds"), 7.0}};
        const QString flushFirstId = request();
        source.executePresetEdit(flushFirstId, QStringLiteral("flush"), QStringLiteral("add"),
                                 flushFields, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 2);
        QVERIFY(completed.last().at(3).toBool());
        const QString flushSecondId = request();
        source.executePresetEdit(flushSecondId, QStringLiteral("flush"), QStringLiteral("add"),
                                 flushFields, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 3);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->flushPresets().size(), originalFlush.size() + 2);

        const int flushIndex = static_cast<int>(settings.brew()->flushPresets().size()) - 1;
        const QVariantMap flushUpdate{{QStringLiteral("index"), flushIndex},
                                      {QStringLiteral("expectedName"), QStringLiteral("Repeated Flush")},
                                      {QStringLiteral("name"), QStringLiteral("Updated Flush")},
                                      {QStringLiteral("flow"), 7.0},
                                      {QStringLiteral("seconds"), 8.5}};
        const QString flushUpdateId = request();
        source.executePresetEdit(flushUpdateId, QStringLiteral("flush"), QStringLiteral("update"),
                                 flushUpdate, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 4);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->getFlushPreset(flushIndex).value(QStringLiteral("seconds")).toDouble(),
                 8.5);

        const QVariantMap flushMove{{QStringLiteral("fromIndex"), flushIndex},
                                    {QStringLiteral("expectedFromName"), QStringLiteral("Updated Flush")},
                                    {QStringLiteral("toIndex"), flushIndex - 1},
                                    {QStringLiteral("expectedToName"), QStringLiteral("Repeated Flush")}};
        const QString flushMoveId = request();
        source.executePresetEdit(flushMoveId, QStringLiteral("flush"), QStringLiteral("move"),
                                 flushMove, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 5);
        QVERIFY(completed.last().at(3).toBool());

        const QVariantMap flushRemove{{QStringLiteral("index"), flushIndex - 1},
                                      {QStringLiteral("expectedName"), QStringLiteral("Updated Flush")}};
        const QString flushRemoveId = request();
        source.executePresetEdit(flushRemoveId, QStringLiteral("flush"), QStringLiteral("remove"),
                                 flushRemove, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 6);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(settings.brew()->flushPresets().size(), originalFlush.size() + 1);

        const int selectedFlush = static_cast<int>(settings.brew()->flushPresets().size()) - 1;
        settings.brew()->setSelectedFlushPreset(selectedFlush);
        QVariantMap flushCommit{{QStringLiteral("index"), selectedFlush},
                                {QStringLiteral("expectedName"),
                                 settings.brew()->getFlushPreset(selectedFlush).value(QStringLiteral("name"))},
                                {QStringLiteral("name"), QStringLiteral("Committed Flush")},
                                {QStringLiteral("flow"), 7.5},
                                {QStringLiteral("seconds"), 9.0}};
        const QString flushCommitId = request();
        source.executePresetEdit(flushCommitId, QStringLiteral("flush"), QStringLiteral("commit"),
                                 flushCommit, source.revision());
        QCOMPARE(completed.count(), beforeMalformed + 7);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(applySpy.count(), 2);
        QCOMPARE(applySpy.last().at(0).toString(), QStringLiteral("flush"));
        QCOMPARE(settings.brew()->flushFlow(), 7.5);
        QCOMPARE(settings.brew()->flushSeconds(), 9.0);

        // The shipped seed rows omit optional fields. Existing zero-flow rows
        // must expose the same effective values as machine selection, so a
        // temperature-only edit is not refused by the strict service validator.
        const QByteArray legacyRows = R"JSON([
            {"name":"Seed cup","volume":200},
            {"name":"Legacy mug","volume":350,"mode":"","flowRate":0,"temperature":85}
        ])JSON";
        settings.setValue(QStringLiteral("water/vesselPresets"), legacyRows);
        brew->setSelectedWaterCup(1);
        brew->setWaterTemperature(85);
        source.refresh();
        const QVariantList projected = source.preferences()
            .value(QStringLiteral("waterVesselPresets")).toList();
        QCOMPARE(projected.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("weight"));
        QCOMPARE(projected.at(0).toMap().value(QStringLiteral("flowRate")).toInt(), 40);
        QCOMPARE(projected.at(0).toMap().value(QStringLiteral("temperature")).toDouble(), 85.0);
        QCOMPARE(settings.value(QStringLiteral("water/vesselPresets")).toByteArray(), legacyRows);
        QVariantMap legacyEdit = brew->getWaterVesselPreset(1);
        QCOMPARE(legacyEdit, projected.at(1).toMap());
        QCOMPARE(legacyEdit.value(QStringLiteral("flowRate")).toInt(), 5);
        legacyEdit.insert(QStringLiteral("index"), 1);
        legacyEdit.insert(QStringLiteral("expectedName"), QStringLiteral("Legacy mug"));
        legacyEdit.insert(QStringLiteral("temperature"), 86.0);
        const qsizetype beforeLegacy = completed.count();
        source.executePresetEdit(request(), QStringLiteral("waterVessel"), QStringLiteral("commit"),
                                 legacyEdit, source.revision());
        QCOMPARE(completed.count(), beforeLegacy + 1);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(hardware->hotWaterFlowRate(), 5);
        QCOMPARE(brew->waterTemperature(), 86.0);
        const QVariantMap stored = QJsonDocument::fromJson(settings.value(
            QStringLiteral("water/vesselPresets")).toByteArray()).array().at(1).toObject().toVariantMap();
        QCOMPARE(stored.value(QStringLiteral("mode")).toString(), QStringLiteral("weight"));
        QCOMPARE(stored.value(QStringLiteral("flowRate")).toInt(), 5);
    }

    void nativeDoseCapturePreferencesRejectStaleAndMalformedWrites()
    {
        Settings settings;
        auto* brew = settings.brew();
        const double originalTare = brew->doseCupTareWeight();
        const bool originalSound = brew->doseCaptureSoundEnabled();
        const auto restore = qScopeGuard([&] {
            brew->setDoseCupTareWeight(originalTare);
            brew->setDoseCaptureSoundEnabled(originalSound);
        });
        brew->setDoseCupTareWeight(12.5);
        brew->setDoseCaptureSoundEnabled(false);
        NativeRecipeContextSource source(&settings, nullptr);
        QSignalSpy completed(&source, &RecipeContextStateSimpleSource::preferenceCompleted);
        const auto write = [&](const QString& name, const QVariant& value, quint64 revision) {
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
            source.setPreference(id, name, value, revision);
            QCOMPARE(completed.last().at(0).toString(), id);
        };
        const quint64 before = source.revision();
        write(QStringLiteral("doseCupTareWeight"), 20.5, before);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(brew->doseCupTareWeight(), 20.5);
        QCOMPARE(source.preferences().value(QStringLiteral("doseCupTareWeight")).toDouble(), 20.5);
        write(QStringLiteral("doseCaptureSoundEnabled"), true, before);
        QVERIFY(!completed.last().at(2).toBool());
        QVERIFY(!brew->doseCaptureSoundEnabled());
        write(QStringLiteral("doseCaptureSoundEnabled"), true, source.revision());
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(brew->doseCaptureSoundEnabled());
        for (const QVariant& invalid : QVariantList{true, QStringLiteral("20"), -1.0,
                std::numeric_limits<double>::quiet_NaN(), 1001.0}) {
            write(QStringLiteral("doseCupTareWeight"), invalid, source.revision());
            QVERIFY(!completed.last().at(2).toBool());
            QCOMPARE(brew->doseCupTareWeight(), 20.5);
        }
        write(QStringLiteral("doseCaptureSoundEnabled"), 0, source.revision());
        QVERIFY(!completed.last().at(2).toBool());
        QVERIFY(brew->doseCaptureSoundEnabled());
    }

    void nativeEspressoViewPreferencesAreTypedAndVersioned()
    {
        Settings settings;
        const QVariant originalView = settings.value(QStringLiteral("espresso/extractionView"),
                                                      QStringLiteral("chart"));
        const QVariant originalPhase = settings.value(QStringLiteral("espresso/showPhaseIndicator"), true);
        const QVariant originalStats = settings.value(QStringLiteral("espresso/showStats"), true);
        const auto restore = qScopeGuard([&] {
            settings.setValue(QStringLiteral("espresso/extractionView"), originalView);
            settings.setValue(QStringLiteral("espresso/showPhaseIndicator"), originalPhase);
            settings.setValue(QStringLiteral("espresso/showStats"), originalStats);
        });
        settings.setValue(QStringLiteral("espresso/extractionView"), QStringLiteral("chart"));
        settings.setValue(QStringLiteral("espresso/showPhaseIndicator"), true);
        settings.setValue(QStringLiteral("espresso/showStats"), true);

        NativeRecipeContextSource source(&settings, nullptr);
        QSignalSpy completed(&source, &RecipeContextStateSimpleSource::preferenceCompleted);
        const auto write = [&](const QString& name, const QVariant& value, quint64 revision) {
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
            source.setPreference(id, name, value, revision);
            QCOMPARE(completed.last().at(0).toString(), id);
        };

        const quint64 initial = source.revision();
        QCOMPARE(source.preferences().value(QStringLiteral("espressoExtractionView")).toString(),
                 QStringLiteral("chart"));
        QVERIFY(source.preferences().value(QStringLiteral("espressoShowPhaseIndicator")).toBool());
        QVERIFY(source.preferences().value(QStringLiteral("espressoShowStats")).toBool());
        write(QStringLiteral("espressoExtractionView"), QStringLiteral("cupFill"), initial);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("espresso/extractionView")).toString(),
                 QStringLiteral("cupFill"));
        write(QStringLiteral("espressoShowPhaseIndicator"), false, source.revision());
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(!settings.value(QStringLiteral("espresso/showPhaseIndicator")).toBool());
        const quint64 current = source.revision();
        write(QStringLiteral("espressoShowStats"), false, current);
        QVERIFY(completed.last().at(2).toBool());
        write(QStringLiteral("espressoShowStats"), true, current);
        QVERIFY(!completed.last().at(2).toBool());
        QVERIFY(!settings.value(QStringLiteral("espresso/showStats")).toBool());
        write(QStringLiteral("espressoExtractionView"), QStringLiteral("unexpected"), source.revision());
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("espresso/extractionView")).toString(),
                 QStringLiteral("cupFill"));
    }

    void nativePostShotReviewTimeoutIsBoundedAndVersioned()
    {
        QSettings store(Settings::testQSettingsPath(), QSettings::IniFormat);
        const bool wasPresent = store.contains(QStringLiteral("postShotReviewTimeout"));
        const QVariant original = store.value(QStringLiteral("postShotReviewTimeout"));
        const auto restore = qScopeGuard([&] {
            if (wasPresent)
                store.setValue(QStringLiteral("postShotReviewTimeout"), original);
            else
                store.remove(QStringLiteral("postShotReviewTimeout"));
        });
        Settings settings;
        settings.setValue(QStringLiteral("postShotReviewTimeout"), 31);

        NativeRecipeContextSource source(&settings, nullptr);
        QSignalSpy completed(&source, &RecipeContextStateSimpleSource::preferenceCompleted);
        QVERIFY(completed.isValid());
        const auto write = [&](const QVariant& value, quint64 expectedRevision) {
            const qsizetype previousCount = completed.count();
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
            source.setPreference(id, QStringLiteral("postShotReviewTimeout"), value, expectedRevision);
            QCOMPARE(completed.count(), previousCount + 1);
            QCOMPARE(completed.last().at(0).toString(), id);
        };

        const quint64 initialRevision = source.revision();
        write(0, initialRevision);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("postShotReviewTimeout")).toInt(), 0);
        QCOMPARE(source.preferences().value(QStringLiteral("postShotReviewTimeout")).toInt(), 0);

        const quint64 zeroRevision = source.revision();
        write(3600, zeroRevision);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("postShotReviewTimeout")).toInt(), 3600);
        const quint64 maxRevision = source.revision();
        QVERIFY(maxRevision > zeroRevision);

        write(60, zeroRevision);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(settings.value(QStringLiteral("postShotReviewTimeout")).toInt(), 3600);

        for (const int invalid : {-1, 3601}) {
            write(invalid, maxRevision);
            QVERIFY(!completed.last().at(2).toBool());
            QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidPreference"));
            QCOMPARE(settings.value(QStringLiteral("postShotReviewTimeout")).toInt(), 3600);
            QCOMPARE(source.revision(), maxRevision);
        }
    }

    void nativeGrindDialInCommitsTogetherAndPreservesUntouchedRpm()
    {
        Settings settings;
        auto* dye = settings.dye();
        const QString originalSetting = dye->dyeGrinderSetting();
        const int originalRpm = dye->dyeGrinderRpm();
        struct RestoreGrinder {
            SettingsDye* dye;
            QString setting;
            int rpm;
            ~RestoreGrinder()
            {
                dye->setDyeGrinderSetting(setting);
                dye->setDyeGrinderRpm(rpm);
            }
        } restore{dye, originalSetting, originalRpm};

        dye->setDyeGrinderSetting(QStringLiteral("4.0"));
        dye->setDyeGrinderRpm(1200);
        NativeRecipeContextSource source(&settings, nullptr);
        QVERIFY(source.ready());
        QSignalSpy completed(&source, &RecipeContextStateSimpleSource::preferenceCompleted);
        QSignalSpy preferencesChanged(&source,
                                      &RecipeContextStateSimpleSource::preferencesChanged);
        QSignalSpy revisionChanged(&source,
                                   &RecipeContextStateSimpleSource::revisionChanged);
        const auto request = [] {
            return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        };

        const quint64 initialRevision = source.revision();
        const QString pairedId = request();
        source.setPreference(pairedId, QStringLiteral("grindDialIn"),
            QVariantMap{{QStringLiteral("dyeGrinderSetting"), QStringLiteral("5.2")},
                        {QStringLiteral("dyeGrinderRpm"), 1650}}, initialRevision);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.last().at(0).toString(), pairedId);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(dye->dyeGrinderSetting(), QStringLiteral("5.2"));
        QCOMPARE(dye->dyeGrinderRpm(), 1650);
        QCOMPARE(preferencesChanged.count(), 1);
        QCOMPARE(revisionChanged.count(), 1);

        // The synthetic RPM anchor is not a commit. Omitting it preserves the
        // existing host value while still applying the grind edit.
        const QString grindOnlyId = request();
        source.setPreference(grindOnlyId, QStringLiteral("grindDialIn"),
            QVariantMap{{QStringLiteral("dyeGrinderSetting"), QStringLiteral("5.3")}},
            source.revision());
        QCOMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(dye->dyeGrinderSetting(), QStringLiteral("5.3"));
        QCOMPARE(dye->dyeGrinderRpm(), 1650);

        const QString invalidId = request();
        source.setPreference(invalidId, QStringLiteral("grindDialIn"),
            QVariantMap{{QStringLiteral("dyeGrinderSetting"), QStringLiteral("6.0")},
                        {QStringLiteral("dyeGrinderRpm"), -1}}, source.revision());
        QCOMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(dye->dyeGrinderSetting(), QStringLiteral("5.3"));
        QCOMPARE(dye->dyeGrinderRpm(), 1650);
    }

    void graphPreferencesShareCanonicalSnapshotAndCycleGuard()
    {
        Settings settings;
        auto* graph = settings.graph();
        const bool originalPressure = graph->showPressure();
        const bool originalFlow = graph->showFlow();
        const QString originalAxis = graph->rightAxisMode();
        const auto restore = qScopeGuard([graph, originalPressure, originalFlow, originalAxis] {
            graph->setShowPressure(originalPressure);
            graph->setShowFlow(originalFlow);
            graph->setRightAxisMode(originalAxis);
        });

        NativeRecipeContextSource source(&settings, nullptr);
        QVERIFY(source.ready());
        QVariantMap preferences = source.preferences();
        QVERIFY(preferences.contains(QStringLiteral("showPressure")));
        QCOMPARE(preferences.value(QStringLiteral("showPressure")).toBool(), originalPressure);
        QCOMPARE(preferences.value(QStringLiteral("rightAxisMode")).toString(), originalAxis);

        QSignalSpy completed(&source, &RecipeContextStateSimpleSource::preferenceCompleted);
        const quint64 firstRevision = source.revision();
        const QString traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(traceId, QStringLiteral("showPressure"), !originalPressure,
                             firstRevision);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.last().at(0).toString(), traceId);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(graph->showPressure(), !originalPressure);
        QCOMPARE(source.preferences().value(QStringLiteral("showPressure")).toBool(),
                 !originalPressure);

        const QString nextAxis = SettingsGraph::nextRightAxisMode(originalAxis);
        const QString axisId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(axisId, QStringLiteral("rightAxisMode"), nextAxis, source.revision());
        QCOMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(graph->rightAxisMode(), nextAxis);

        const QString invalidId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(invalidId, QStringLiteral("rightAxisMode"), originalAxis,
                             source.revision());
        QCOMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(graph->rightAxisMode(), nextAxis);

        const quint64 staleRevision = source.revision();
        graph->setShowFlow(!graph->showFlow());
        QVERIFY(source.revision() > staleRevision);
        const QString staleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        source.setPreference(staleId, QStringLiteral("rightAxisMode"),
                             SettingsGraph::nextRightAxisMode(graph->rightAxisMode()),
                             staleRevision);
        QCOMPARE(completed.count(), 4);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(graph->rightAxisMode(), nextAxis);
    }

    void nativeSourceUsesSettingsCasAndScopedHistoryStorage()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        Settings settings;
        ShotHistoryStorage history;
        QVERIFY(history.initialize(directory.filePath(QStringLiteral("recipe-context.db"))));
        QTRY_VERIFY(history.isReady());

        // Seed two rows through a separate connection so this test exercises the real
        // migrated store and the same queries the native history page uses.
        const QString connectionName = QStringLiteral("recipe-context-test-%1")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qint64 firstShotId = 0;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(directory.filePath(QStringLiteral("recipe-context.db")));
            QVERIFY(db.open());
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, bean_brand, bean_type, grinder_setting) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)"));
            insert.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
            insert.addBindValue(QDateTime::currentSecsSinceEpoch());
            insert.addBindValue(QStringLiteral("Test profile"));
            insert.addBindValue(30.0);
            insert.addBindValue(QStringLiteral("Roaster"));
            insert.addBindValue(QStringLiteral("Coffee A"));
            insert.addBindValue(QStringLiteral("12"));
            QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
            firstShotId = insert.lastInsertId().toLongLong();
            QVERIFY(firstShotId > 0);
            insert.finish();
            QVERIFY(db.tables().contains(QStringLiteral("shots")));
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        NativeRecipeContextSource source(&settings, &history);
        QVERIFY(source.ready());
        QVERIFY(source.historyReady());
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<RecipeContextStateSourceAPI>(&source));
        QVERIFY(client.connectToNode(host.hostUrl()));
        RecipeContextService::setPublishedNode(&client);
        struct ResetPublishedNode {
            ~ResetPublishedNode() { RecipeContextService::setPublishedNode(nullptr); }
        } resetPublishedNode;
        RecipeContextService* service = RecipeContextServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(service->ready());
        QTRY_VERIFY(service->historyReady());
        QSignalSpy preferenceSpy(service, &RecipeContextService::preferenceCompleted);
        QSignalSpy historySpy(service, &RecipeContextService::historyCompleted);
        QSignalSpy hostHistorySpy(&source, &RecipeContextStateSimpleSource::historyCompleted);

        const double originalRatio = settings.brew()->ratioPreset1();
        auto* dye = settings.dye();
        const QVariantMap originalDye{{QStringLiteral("brand"), dye->dyeBeanBrand()},
                                      {QStringLiteral("type"), dye->dyeBeanType()},
                                      {QStringLiteral("roastDate"), dye->dyeRoastDate()},
                                      {QStringLiteral("roastLevel"), dye->dyeRoastLevel()},
                                      {QStringLiteral("grinderSetting"), dye->dyeGrinderSetting()},
                                      {QStringLiteral("grinderRpm"), dye->dyeGrinderRpm()},
                                      {QStringLiteral("barista"), dye->dyeBarista()},
                                      {QStringLiteral("beanWeight"), dye->dyeBeanWeight()},
                                      {QStringLiteral("drinkWeight"), dye->dyeDrinkWeight()},
                                      {QStringLiteral("beanBaseId"), dye->dyeBeanBaseId()},
                                      {QStringLiteral("beanBaseData"), dye->dyeBeanBaseData()},
                                      {QStringLiteral("equipmentId"), dye->activeEquipmentId()}};
        struct RestoreDye {
            SettingsDye* dye;
            QVariantMap values;
            ~RestoreDye()
            {
                dye->setDyeBeanBrand(values.value(QStringLiteral("brand")).toString());
                dye->setDyeBeanType(values.value(QStringLiteral("type")).toString());
                dye->setDyeRoastDate(values.value(QStringLiteral("roastDate")).toString());
                dye->setDyeRoastLevel(values.value(QStringLiteral("roastLevel")).toString());
                dye->setDyeGrinderSetting(values.value(QStringLiteral("grinderSetting")).toString());
                dye->setDyeGrinderRpm(values.value(QStringLiteral("grinderRpm")).toInt());
                dye->setDyeBarista(values.value(QStringLiteral("barista")).toString());
                dye->setDyeBeanWeight(values.value(QStringLiteral("beanWeight")).toDouble());
                dye->setDyeDrinkWeight(values.value(QStringLiteral("drinkWeight")).toDouble());
                dye->setDyeBeanBaseId(values.value(QStringLiteral("beanBaseId")).toString());
                dye->setDyeBeanBaseData(values.value(QStringLiteral("beanBaseData")).toString());
                dye->setActiveEquipmentId(values.value(QStringLiteral("equipmentId")).toLongLong());
            }
        } restoreDye{dye, originalDye};
        struct RestoreRatio {
            SettingsBrew* brew;
            double value;
            ~RestoreRatio() { brew->setRatioPreset1(value); }
        } restoreRatio{settings.brew(), originalRatio};

        // The review page sends the complete editable baseline in one guarded
        // operation. A shot without equipment must not clear the currently
        // selected package, matching the native page's positive-ID-only rule.
        dye->setActiveEquipmentId(47);
        // The native source publishes this setting change over QtRO. Wait for
        // the new revision before using it as the guarded baseline's CAS token.
        QTRY_COMPARE(service->preferences().value(QStringLiteral("activeEquipmentId")).toLongLong(),
                     qint64(47));
        const QVariantMap baseline{
            {QStringLiteral("dyeBeanBrand"), QStringLiteral("Review Roaster")},
            {QStringLiteral("dyeBeanType"), QStringLiteral("Review Coffee")},
            {QStringLiteral("dyeRoastDate"), QStringLiteral("2026-10-20")},
            {QStringLiteral("dyeRoastLevel"), QStringLiteral("Medium")},
            {QStringLiteral("dyeGrinderSetting"), QStringLiteral("5.2")},
            {QStringLiteral("dyeGrinderRpm"), 0},
            {QStringLiteral("activeEquipmentId"), -1},
            {QStringLiteral("shotEquipmentId"), -1},
            {QStringLiteral("dyeBarista"), QStringLiteral("Reviewer")},
            {QStringLiteral("dyeBeanWeight"), 18.5},
            {QStringLiteral("dyeDrinkWeight"), 37.0},
            {QStringLiteral("dyeBeanBaseId"), QString()},
            {QStringLiteral("dyeBeanBaseData"), QString()}};
        QSignalSpy baselineSpy(service, &RecipeContextService::shotBaselineCompleted);
        const quint64 baselineRevision = service->revision();
        const QString baselineId = service->newRequestId();
        service->applyShotBaseline(baselineId, baseline, baselineRevision);
        QTRY_COMPARE(baselineSpy.count(), 1);
        QCOMPARE(baselineSpy.last().at(0).toString(), baselineId);
        QVERIFY(baselineSpy.last().at(1).toBool());
        QCOMPARE(dye->dyeBeanBrand(), QStringLiteral("Review Roaster"));
        QCOMPARE(dye->dyeBeanType(), QStringLiteral("Review Coffee"));
        QCOMPARE(dye->activeEquipmentId(), qint64(47));
        QCOMPARE(dye->dyeGrinderSetting(), QStringLiteral("5.2"));
        QCOMPARE(dye->dyeBeanWeight(), 18.5);
        QCOMPARE(dye->dyeDrinkWeight(), 37.0);

        QVariantMap invalidBaseline = baseline;
        invalidBaseline.insert(QStringLiteral("dyeGrinderRpm"), -1);
        const QString invalidBaselineId = service->newRequestId();
        service->applyShotBaseline(invalidBaselineId, invalidBaseline, service->revision());
        QTRY_COMPARE(baselineSpy.count(), 2);
        QVERIFY(!baselineSpy.last().at(1).toBool());
        QCOMPARE(baselineSpy.last().at(4).toString(), QStringLiteral("invalidBaseline"));
        QCOMPARE(dye->dyeBeanBrand(), QStringLiteral("Review Roaster"));

        const QString staleBaselineId = service->newRequestId();
        service->applyShotBaseline(staleBaselineId, baseline, baselineRevision);
        QTRY_COMPARE(baselineSpy.count(), 3);
        QVERIFY(!baselineSpy.last().at(1).toBool());
        QCOMPARE(baselineSpy.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(dye->dyeBeanBrand(), QStringLiteral("Review Roaster"));

        const double updatedRatio = originalRatio == 8.5 ? 8.75 : 8.5;
        const quint64 initialRevision = service->revision();
        const QString validPreferenceId = service->newRequestId();
        service->setPreference(validPreferenceId, QStringLiteral("ratioPreset1"), updatedRatio,
                               initialRevision);
        QTRY_COMPARE(preferenceSpy.count(), 1);
        QCOMPARE(preferenceSpy.last().at(0).toString(), validPreferenceId);
        QVERIFY(preferenceSpy.last().at(2).toBool());
        QCOMPARE(settings.brew()->ratioPreset1(), updatedRatio);
        QVERIFY(service->revision() > initialRevision);

        const quint64 currentRevision = service->revision();
        const QString staleId = service->newRequestId();
        service->setPreference(staleId, QStringLiteral("ratioPreset1"), 9.0,
                               currentRevision - 1);
        QTRY_COMPARE(preferenceSpy.count(), 2);
        QCOMPARE(preferenceSpy.last().at(0).toString(), staleId);
        QVERIFY(!preferenceSpy.last().at(2).toBool());
        QCOMPARE(preferenceSpy.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(settings.brew()->ratioPreset1(), updatedRatio);

        const QString invalidId = service->newRequestId();
        service->setPreference(invalidId, QStringLiteral("ratioPreset1"),
                               settings.brew()->maxRatio() + 1.0,
                               service->revision());
        QTRY_COMPARE(preferenceSpy.count(), 3);
        QVERIFY(!preferenceSpy.last().at(2).toBool());
        QCOMPARE(preferenceSpy.last().at(4).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(settings.brew()->ratioPreset1(), updatedRatio);

        const bool originalPhaseSummary = settings.value(
            QStringLiteral("shotReview/phaseSummaryExpanded"), false).toBool();
        struct RestorePhaseSummary {
            Settings* settings;
            bool value;
            ~RestorePhaseSummary()
            {
                settings->setValue(QStringLiteral("shotReview/phaseSummaryExpanded"), value);
            }
        } restorePhaseSummary{&settings, originalPhaseSummary};
        const quint64 beforePhaseSummary = service->revision();
        const QString phaseSummaryId = service->newRequestId();
        service->setPreference(phaseSummaryId, QStringLiteral("phaseSummaryExpanded"),
                               !originalPhaseSummary, beforePhaseSummary);
        QTRY_COMPARE(preferenceSpy.count(), 4);
        QVERIFY(preferenceSpy.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("shotReview/phaseSummaryExpanded"), false).toBool(),
                 !originalPhaseSummary);
        QVERIFY(service->preferences().contains(QStringLiteral("activeBagFrozenDate")));
        QVERIFY(service->preferences().contains(QStringLiteral("activeBagDefrostDate")));
        QVERIFY(service->preferences().contains(QStringLiteral("activeBagOpenedDate")));

        const double originalComparisonGraphHeight = settings.value(
            QStringLiteral("comparison/graphHeight"), 0.0).toDouble();
        struct RestoreComparisonGraphHeight {
            Settings* settings;
            double value;
            ~RestoreComparisonGraphHeight()
            {
                settings->setValue(QStringLiteral("comparison/graphHeight"), value);
            }
        } restoreComparisonGraphHeight{&settings, originalComparisonGraphHeight};
        const QString comparisonHeightId = service->newRequestId();
        service->setPreference(comparisonHeightId, QStringLiteral("comparisonGraphHeight"),
                               420.0, service->revision());
        QTRY_COMPARE(preferenceSpy.count(), 5);
        QCOMPARE(preferenceSpy.last().at(0).toString(), comparisonHeightId);
        QVERIFY(preferenceSpy.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("comparison/graphHeight")).toDouble(), 420.0);
        QCOMPARE(service->preferences().value(QStringLiteral("comparisonGraphHeight")).toDouble(),
                 420.0);

        const QString invalidComparisonHeightId = service->newRequestId();
        service->setPreference(invalidComparisonHeightId,
                               QStringLiteral("comparisonGraphHeight"), 4001.0,
                               service->revision());
        QTRY_COMPARE(preferenceSpy.count(), 6);
        QCOMPARE(preferenceSpy.last().at(0).toString(), invalidComparisonHeightId);
        QVERIFY(!preferenceSpy.last().at(2).toBool());
        QCOMPARE(settings.value(QStringLiteral("comparison/graphHeight")).toDouble(), 420.0);

        auto* network = settings.network();
        const QString originalBagSortField = network->bagSortField();
        const QString originalBagSortDirection = network->bagSortDirection();
        struct RestoreBagSort {
            SettingsNetwork* network;
            QString field;
            QString direction;
            ~RestoreBagSort()
            {
                network->setBagSortField(field);
                network->setBagSortDirection(direction);
            }
        } restoreBagSort{network, originalBagSortField, originalBagSortDirection};

        const QString bagField = originalBagSortField == QLatin1String("coffee")
            ? QStringLiteral("roastDate") : QStringLiteral("coffee");
        const QString bagDirection = originalBagSortDirection == QLatin1String("ASC")
            ? QStringLiteral("DESC") : QStringLiteral("ASC");
        const quint64 beforeBagSort = service->revision();
        const QString bagSortId = service->newRequestId();
        service->setPreference(bagSortId, QStringLiteral("bagSort"),
                               QVariantMap{{QStringLiteral("field"), bagField},
                                           {QStringLiteral("direction"), bagDirection}},
                               beforeBagSort);
        QTRY_COMPARE(preferenceSpy.count(), 7);
        QCOMPARE(preferenceSpy.last().at(0).toString(), bagSortId);
        QVERIFY(preferenceSpy.last().at(2).toBool());
        QCOMPARE(network->bagSortField(), bagField);
        QCOMPARE(network->bagSortDirection(), bagDirection);
        QCOMPARE(settings.value(QStringLiteral("bags/sortField")).toString(), bagField);
        QCOMPARE(settings.value(QStringLiteral("bags/sortDirection")).toString(), bagDirection);
        QTRY_COMPARE(service->preferences().value(QStringLiteral("bagSortField")).toString(),
                     bagField);
        QCOMPARE(service->preferences().value(QStringLiteral("bagSortDirection")).toString(),
                 bagDirection);

        const quint64 bagRevision = service->revision();
        const QString staleBagSortId = service->newRequestId();
        service->setPreference(staleBagSortId, QStringLiteral("bagSort"),
                               QVariantMap{{QStringLiteral("field"), QStringLiteral("roaster")},
                                           {QStringLiteral("direction"), QStringLiteral("DESC")}},
                               bagRevision - 1);
        QTRY_COMPARE(preferenceSpy.count(), 8);
        QCOMPARE(preferenceSpy.last().at(0).toString(), staleBagSortId);
        QVERIFY(!preferenceSpy.last().at(2).toBool());
        QCOMPARE(preferenceSpy.last().at(4).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(network->bagSortField(), bagField);
        QCOMPARE(network->bagSortDirection(), bagDirection);

        const QString invalidBagSortId = service->newRequestId();
        service->setPreference(invalidBagSortId, QStringLiteral("bagSort"),
                               QVariantMap{{QStringLiteral("field"), QStringLiteral("profile")},
                                           {QStringLiteral("direction"), QStringLiteral("DESC")}},
                               service->revision());
        QTRY_COMPARE(preferenceSpy.count(), 9);
        QCOMPARE(preferenceSpy.last().at(0).toString(), invalidBagSortId);
        QVERIFY(!preferenceSpy.last().at(2).toBool());
        QCOMPARE(preferenceSpy.last().at(4).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(network->bagSortField(), bagField);
        QCOMPARE(network->bagSortDirection(), bagDirection);

        const QString brandsId = service->newRequestId();
        service->requestHistory(brandsId, QStringLiteral("distinctBeanBrands"));
        QTRY_COMPARE(historySpy.count(), 1);
        QCOMPARE(historySpy.last().at(0).toString(), brandsId);
        QCOMPARE(historySpy.last().at(1).toString(), QStringLiteral("distinctBeanBrands"));
        QVERIFY(historySpy.last().at(2).toBool());
        QCOMPARE(historySpy.last().at(3).toMap().value(QStringLiteral("values")).toStringList(),
                 QStringList{QStringLiteral("Roaster")});

        const QString typesId = service->newRequestId();
        service->requestHistory(typesId, QStringLiteral("distinctBeanTypesForBrand"),
                                {{QStringLiteral("beanBrand"), QStringLiteral("Roaster")}});
        QTRY_COMPARE(historySpy.count(), 2);
        QCOMPARE(historySpy.last().at(0).toString(), typesId);
        QCOMPARE(historySpy.last().at(3).toMap().value(QStringLiteral("values")).toStringList(),
                 QStringList{QStringLiteral("Coffee A")});

        const QString metadataId = service->newRequestId();
        service->requestShotMetadataUpdate(metadataId, firstShotId,
                                           {{QStringLiteral("beanType"), QStringLiteral("Coffee B")} });
        QTRY_COMPARE(historySpy.count(), 3);
        QCOMPARE(historySpy.last().at(0).toString(), metadataId);
        QCOMPARE(historySpy.last().at(1).toString(), QStringLiteral("updateShotMetadata"));
        QVERIFY(historySpy.last().at(2).toBool());
        QCOMPARE(historySpy.last().at(3).toMap().value(QStringLiteral("shotId")).toLongLong(),
                 firstShotId);

        // The source writes through ShotHistoryStorage's canonical metadata path.
        const QString verifyConnection = connectionName + QStringLiteral("-verify");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
            db.setDatabaseName(directory.filePath(QStringLiteral("recipe-context.db")));
            QVERIFY(db.open());
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT bean_type FROM shots WHERE id = ?"));
            query.addBindValue(firstShotId);
            QVERIFY(query.exec());
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("Coffee B"));
            db.close();
        }
        QSqlDatabase::removeDatabase(verifyConnection);

        // A request already accepted by the service fails terminally if the source
        // connection is dropped before its reply arrives; it is not replayed later.
        const QString pendingId = service->newRequestId();
        service->requestHistory(pendingId, QStringLiteral("distinctBeanBrands"));
        service->setNode(nullptr);
        QTRY_COMPARE(historySpy.count(), 4);
        QCOMPARE(historySpy.last().at(0).toString(), pendingId);
        QVERIFY(!historySpy.last().at(2).toBool());
        QCOMPARE(historySpy.last().at(4).toString(), QStringLiteral("disconnected"));
        QTRY_COMPARE(hostHistorySpy.count(), 4);

        service->setNode(&client);
        QTRY_VERIFY(service->historyReady());
        historySpy.clear();
        const QVariantMap grindQuery{{QStringLiteral("beanBrand"), QStringLiteral("Roaster")},
                                    {QStringLiteral("beanType"), QStringLiteral("Coffee B")},
                                    {QStringLiteral("roastLevel"), QString()},
                                    {QStringLiteral("equipmentId"), 0}};
        const QString unscopedId = service->newRequestId();
        const QString scopedId = service->newRequestId();
        service->requestHistory(unscopedId, QStringLiteral("latestGrindForBean"), grindQuery);
        QVariantMap scopedQuery = grindQuery;
        const qint64 absentEquipmentId = 9000000000LL;
        scopedQuery.insert(QStringLiteral("equipmentId"), absentEquipmentId);
        service->requestHistory(scopedId, QStringLiteral("latestGrindForBean"), scopedQuery);
        QTRY_COMPARE(historySpy.count(), 2);
        for (const auto& reply : historySpy) {
            QVERIFY(reply.at(2).toBool());
            const auto result = reply.at(3).toMap();
            if (reply.at(0).toString() == unscopedId) {
                QCOMPARE(result.value(QStringLiteral("grinderSetting")).toString(), QStringLiteral("12"));
                QCOMPARE(result.value(QStringLiteral("queryEquipmentId")).toLongLong(), 0);
            } else {
                QCOMPARE(reply.at(0).toString(), scopedId);
                QVERIFY(!result.contains(QStringLiteral("grinderSetting")));
                QCOMPARE(result.value(QStringLiteral("queryEquipmentId")).toLongLong(), absentEquipmentId);
            }
        }
        scopedQuery.insert(QStringLiteral("equipmentId"), QStringLiteral("9000000000"));
        service->requestHistory(service->newRequestId(), QStringLiteral("latestGrindForBean"), scopedQuery);
        QTRY_COMPARE(historySpy.count(), 3);
        QVERIFY(!historySpy.last().at(2).toBool());
        QCOMPARE(historySpy.last().at(4).toString(), QStringLiteral("invalidFields"));

        // Restore the one preference changed by this integration test.
        settings.brew()->setRatioPreset1(originalRatio);
        RecipeContextService::setPublishedNode(nullptr);
        QTRY_VERIFY(history.isDbWorkIdle());
        history.close();
        QTRY_VERIFY(history.isDbWorkIdle());
    }
};

QTEST_GUILESS_MAIN(RecipeContextServiceTest)
#include "tst_recipecontextservice.moc"
