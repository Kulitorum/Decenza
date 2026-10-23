#include <QtTest>

#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QNetworkAccessManager>
#include <QUuid>

#include <limits>
#include <memory>

#include "core/settings.h"
#include "core/settings_ai.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_hardware.h"
#include "core/settings_graph.h"
#include "core/settings_visualizer.h"
#include "core/accessibilitymanager.h"
#include "core/batterymanager.h"
#include "network/shotreporter.h"
#include "remote/settingspreferencesservice.h"
#include "remote/settingspreferencesservice_qml.h"
#include "remote/settingspreferencesservicesource.h"
#include "screensaver/screensavervideomanager.h"

namespace {
QString requestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

struct SettingsRestore final
{
    explicit SettingsRestore(Settings* targetSettings)
        : settings(targetSettings)
        , launcherMode(targetSettings->app()->launcherMode())
        , waterUnit(targetSettings->app()->waterLevelDisplayUnit())
        , temperatureUnit(targetSettings->app()->temperatureUnit())
        , waterRefillPoint(targetSettings->app()->waterRefillPoint())
        , refillKitOverride(targetSettings->app()->refillKitOverride())
        , screenCaptureEnabled(targetSettings->app()->screenCaptureEnabled())
        , keepWarm(targetSettings->brew()->keepWarmWhenIdle())
        , letRecipeDecide(targetSettings->brew()->letRecipeDecide())
        , autoFlush(targetSettings->brew()->steamAutoFlushSeconds())
        , steamTwoTapStop(targetSettings->hardware()->steamTwoTapStop())
        , advancedMode(targetSettings->graph()->advancedMode())
        , flowMultiplier(targetSettings->graph()->flowMultiplier())
        , showAfterShot(targetSettings->visualizer()->visualizerShowAfterShot())
        , clearNotes(targetSettings->visualizer()->visualizerClearNotesOnStart())
        , communitySearchEnabled(targetSettings->visualizer()->communitySearchEnabled())
        , communityMinEnjoyment(targetSettings->visualizer()->communityMinEnjoyment())
        , communityApiBaseUrl(targetSettings->visualizer()->communityApiBaseUrl())
        , communityRelativeGrind(targetSettings->visualizer()->communityRelativeGrind())
        , visualizerEnabled(targetSettings->visualizer()->visualizerEnabled())
        , decentEnabled(targetSettings->decent()->enabled())
        , autoUpload(targetSettings->upload()->autoUpload())
        , autoUpdate(targetSettings->upload()->autoUpdate())
        , minDuration(targetSettings->upload()->minDuration())
        , screensaverValues(captureScreensaverSettings(targetSettings))
        , autoSleepMinutes(targetSettings->value(QStringLiteral("autoSleepMinutes"), 60).toInt())
        , autoWakeEnabled(targetSettings->autoWake()->autoWakeEnabled())
        , autoWakeSchedule(targetSettings->autoWake()->autoWakeSchedule())
        , stayAwakeEnabled(targetSettings->autoWake()->autoWakeStayAwakeEnabled())
        , stayAwakeMinutes(targetSettings->autoWake()->autoWakeStayAwakeMinutes())
        , configurePageScale(targetSettings->value(QStringLiteral("ui/configurePageScale"), false))
        , simulationMode(targetSettings->value(QStringLiteral("developer/simulationMode"), false))
        , chargingMode(targetSettings->value(QStringLiteral("smartBatteryCharging"), BatteryManager::On))
        , usbChargerEnabled(targetSettings->value(QStringLiteral("battery/usbChargerEnabled"), true))
        , shotMapEnabled(targetSettings->value(QStringLiteral("shotmap/enabled"), true).toBool())
    {
    }

    ~SettingsRestore()
    {
        settings->app()->setLauncherMode(launcherMode);
        settings->app()->setWaterLevelDisplayUnit(waterUnit);
        settings->app()->setTemperatureUnit(temperatureUnit);
        settings->app()->setWaterRefillPoint(waterRefillPoint);
        settings->app()->setRefillKitOverride(refillKitOverride);
        settings->app()->setScreenCaptureEnabled(screenCaptureEnabled);
        settings->brew()->setKeepWarmWhenIdle(keepWarm);
        settings->brew()->setLetRecipeDecide(letRecipeDecide);
        settings->brew()->setSteamAutoFlushSeconds(autoFlush);
        settings->hardware()->setSteamTwoTapStop(steamTwoTapStop);
        settings->graph()->setAdvancedMode(advancedMode);
        settings->graph()->setFlowMultiplier(flowMultiplier);
        settings->visualizer()->setVisualizerShowAfterShot(showAfterShot);
        settings->visualizer()->setVisualizerClearNotesOnStart(clearNotes);
        settings->visualizer()->setCommunitySearchEnabled(communitySearchEnabled);
        settings->visualizer()->setCommunityMinEnjoyment(communityMinEnjoyment);
        settings->visualizer()->setCommunityApiBaseUrl(communityApiBaseUrl);
        settings->visualizer()->setCommunityRelativeGrind(communityRelativeGrind);
        settings->visualizer()->setVisualizerEnabled(visualizerEnabled);
        settings->decent()->setEnabled(decentEnabled);
        settings->upload()->setAutoUpload(autoUpload);
        settings->upload()->setAutoUpdate(autoUpdate);
        settings->upload()->setMinDuration(minDuration);
        for (auto it = screensaverValues.cbegin(); it != screensaverValues.cend(); ++it)
            settings->setValue(it.key(), it.value());
        settings->setValue(QStringLiteral("autoSleepMinutes"), autoSleepMinutes);
        settings->autoWake()->setAutoWakeEnabled(autoWakeEnabled);
        settings->autoWake()->setAutoWakeSchedule(autoWakeSchedule);
        settings->autoWake()->setAutoWakeStayAwakeEnabled(stayAwakeEnabled);
        settings->autoWake()->setAutoWakeStayAwakeMinutes(stayAwakeMinutes);
        settings->setValue(QStringLiteral("ui/configurePageScale"), configurePageScale);
        settings->setValue(QStringLiteral("developer/simulationMode"), simulationMode);
        settings->setValue(QStringLiteral("smartBatteryCharging"), chargingMode);
        settings->setValue(QStringLiteral("battery/usbChargerEnabled"), usbChargerEnabled);
        settings->setValue(QStringLiteral("shotmap/enabled"), shotMapEnabled);
    }

    static QVariantMap captureScreensaverSettings(Settings* settings)
    {
        QVariantMap values;
        const QList<QPair<QString, QVariant>> defaults{
            {QStringLiteral("screensaver/enabled"), true},
            {QStringLiteral("screensaver/cacheEnabled"), true},
            {QStringLiteral("screensaver/streamingFallback"), true},
            {QStringLiteral("screensaver/categoryId"), QStringLiteral("espresso")},
            {QStringLiteral("screensaver/imageDisplayDuration"), 10},
            {QStringLiteral("screensaver/showDateOnPersonal"), false},
            {QStringLiteral("screensaver/type"), QStringLiteral("videos")},
            {QStringLiteral("screensaver/pipesSpeed"), 0.5},
            {QStringLiteral("screensaver/pipesCameraSpeed"), 60.0},
            {QStringLiteral("screensaver/flipClockUse3D"), true},
            {QStringLiteral("screensaver/videosShowClock"), true},
            {QStringLiteral("screensaver/pipesShowClock"), true},
            {QStringLiteral("screensaver/attractorShowClock"), false},
            {QStringLiteral("screensaver/shotMapShape"), QStringLiteral("flat")},
            {QStringLiteral("screensaver/shotMapTexture"), QStringLiteral("dark")},
            {QStringLiteral("screensaver/shotMapShowClock"), true},
            {QStringLiteral("screensaver/shotMapShowProfiles"), true},
            {QStringLiteral("screensaver/shotMapShowTerminator"), true},
            {QStringLiteral("screensaver/overlayShowWaterLevel"), false},
            {QStringLiteral("screensaver/overlayShowShotPlan"), false},
            {QStringLiteral("screensaver/overlayShowBattery"), false},
            {QStringLiteral("screensaver/overlayLinkButtonEnabled"), false},
            {QStringLiteral("screensaver/overlayLinkButtonLabel"), QString()},
            {QStringLiteral("screensaver/overlayLinkButtonUrl"), QString()},
            {QStringLiteral("screensaver/dimPercent"), 0},
            {QStringLiteral("screensaver/dimDelayMinutes"), 15},
        };
        for (const auto& entry : defaults)
            values.insert(entry.first, settings->value(entry.first, entry.second));
        return values;
    }

    Settings* settings;
    bool launcherMode;
    QString waterUnit;
    QString temperatureUnit;
    int waterRefillPoint;
    int refillKitOverride;
    bool screenCaptureEnabled;
    bool keepWarm;
    bool letRecipeDecide;
    int autoFlush;
    bool steamTwoTapStop;
    bool advancedMode;
    int flowMultiplier;
    bool showAfterShot;
    bool clearNotes;
    bool communitySearchEnabled;
    int communityMinEnjoyment;
    QString communityApiBaseUrl;
    bool communityRelativeGrind;
    bool visualizerEnabled;
    bool decentEnabled;
    bool autoUpload;
    bool autoUpdate;
    double minDuration;
    QVariantMap screensaverValues;
    int autoSleepMinutes;
    bool autoWakeEnabled;
    QVariantList autoWakeSchedule;
    bool stayAwakeEnabled;
    int stayAwakeMinutes;
    QVariant configurePageScale;
    QVariant simulationMode;
    QVariant chargingMode;
    QVariant usbChargerEnabled;
    bool shotMapEnabled;
};
} // namespace

class SettingsPreferencesServiceTest final : public QObject
{
    Q_OBJECT
private:
    struct Fixture final
    {
        Settings settings;
        SettingsRestore restore{&settings};
        QNetworkAccessManager network;
        BatteryManager batteryManager;
        AccessibilityManager accessibility{AccessibilityManager::TestSkipAudioInit::SkipAudio};
        std::unique_ptr<ScreensaverVideoManager> screensaverManager;
        std::unique_ptr<ShotReporter> shotReporter;
        NativeSettingsPreferencesSource source{&settings};
        QRemoteObjectHost host;
        QRemoteObjectNode client;
        QQmlEngine engine;
        SettingsPreferencesService* service = nullptr;

        Fixture()
        {
            settings.setValue(QStringLiteral("screensaver/enabled"), false);
            settings.setValue(QStringLiteral("screensaver/pipesSpeed"), 0.5);
            settings.setValue(QStringLiteral("screensaver/dimPercent"), 0);
            settings.setValue(QStringLiteral("screensaver/dimDelayMinutes"), 15);
            settings.setValue(QStringLiteral("autoSleepMinutes"), 60);
            settings.setValue(QStringLiteral("ui/configurePageScale"), false);
            settings.setValue(QStringLiteral("developer/simulationMode"), false);
            settings.setValue(QStringLiteral("smartBatteryCharging"), BatteryManager::On);
            settings.setValue(QStringLiteral("battery/usbChargerEnabled"), true);
            settings.setValue(QStringLiteral("shotmap/enabled"), false);
            batteryManager.setSettings(&settings);
            settings.autoWake()->setAutoWakeEnabled(false);
            QVariantList schedule;
            for (int day = 0; day < 7; ++day)
                schedule.append(QVariantMap{{QStringLiteral("enabled"), false},
                                            {QStringLiteral("hour"), 7},
                                            {QStringLiteral("minute"), 0}});
            settings.autoWake()->setAutoWakeSchedule(schedule);
            settings.autoWake()->setAutoWakeStayAwakeEnabled(false);
            settings.autoWake()->setAutoWakeStayAwakeMinutes(120);
            screensaverManager = std::make_unique<ScreensaverVideoManager>(
                &network, &settings, nullptr);
            source.setScreensaverManager(screensaverManager.get());
            source.setBatteryManager(&batteryManager);
            // No LocationProvider is injected: the fixture cannot prompt for
            // GPS permissions, geocode, or make third-party requests.
            shotReporter = std::make_unique<ShotReporter>(&network, &settings, nullptr);
            source.setShotReporter(shotReporter.get());
            source.setAccessibilityManager(&accessibility);
            if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))))
                qFatal("Could not listen for the settings-preferences test replica");
            if (!host.enableRemoting<SettingsPreferencesStateSourceAPI>(&source))
                qFatal("Could not publish the settings-preferences test source");
            if (!client.connectToNode(host.hostUrl()))
                qFatal("Could not connect the settings-preferences test replica");
            SettingsPreferencesService::setPublishedNode(&client);
            service = SettingsPreferencesServiceForeign::create(&engine, &engine);
            if (!service)
                qFatal("Could not create the settings-preferences test facade");
            QTRY_VERIFY(service->ready());
        }

        ~Fixture() { SettingsPreferencesService::setPublishedNode(nullptr); }
    };

private slots:
    void init() { QTest::failOnWarning(); }

    void debugSimulationSwitchesUseTheCanonicalAppDomain()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        const quint64 revision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        const bool hide = !f.settings.app()->hideGhcSimulator();
        f.service->setPreference(requestId(), QStringLiteral("app"), QStringLiteral("hideGhcSimulator"),
                                 hide, revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.app()->hideGhcSimulator(), hide);
        QCOMPARE(f.service->preferences().value(QStringLiteral("app.hideGhcSimulator")).toBool(), hide);

        const quint64 nextRevision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        const bool simulatedScale = !f.settings.app()->simulatedScaleEnabled();
        f.service->setPreference(requestId(), QStringLiteral("app"), QStringLiteral("simulatedScaleEnabled"),
                                 simulatedScale, nextRevision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.app()->simulatedScaleEnabled(), simulatedScale);
        QCOMPARE(f.service->preferences().value(QStringLiteral("app.simulatedScaleEnabled")).toBool(),
                 simulatedScale);
    }

    void graphPreferencesUseCanonicalSettersAndRejectStaleOrInvalidWrites()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        const QString graphDomain = QStringLiteral("graph");
        const QString advancedName = QStringLiteral("advancedMode");
        const QString multiplierName = QStringLiteral("flowMultiplier");
        const quint64 initialRevision = f.service->revisions().value(graphDomain).toULongLong();
        const bool advanced = !f.settings.graph()->advancedMode();
        f.service->setPreference(requestId(), graphDomain, advancedName, advanced, initialRevision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.graph()->advancedMode(), advanced);
        QTRY_COMPARE(f.service->preferences().value(QStringLiteral("graph.advancedMode")).toBool(), advanced);
        QTRY_VERIFY(f.service->revisions().value(graphDomain).toULongLong() > initialRevision);

        const quint64 editedRevision = f.service->revisions().value(graphDomain).toULongLong();
        const int nativeMultiplier = f.settings.graph()->flowMultiplier() == 3 ? 2 : 3;
        f.settings.graph()->setFlowMultiplier(nativeMultiplier);
        QTRY_COMPARE(f.service->preferences().value(QStringLiteral("graph.flowMultiplier")).toInt(),
                     nativeMultiplier);
        QTRY_VERIFY(f.service->revisions().value(graphDomain).toULongLong() > editedRevision);
        f.service->setPreference(requestId(), graphDomain, advancedName, !advanced, editedRevision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.settings.graph()->advancedMode(), advanced);

        const int multiplier = nativeMultiplier == 1 ? 2 : 1;
        f.service->setPreference(requestId(), graphDomain, multiplierName, multiplier,
                                 f.service->revisions().value(graphDomain).toULongLong());
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.graph()->flowMultiplier(), multiplier);
        QTRY_COMPARE(f.service->preferences().value(QStringLiteral("graph.flowMultiplier")).toInt(), multiplier);

        const QList<QPair<QString, QVariant>> invalidWrites{
            {advancedName, 1}, {advancedName, QStringLiteral("true")},
            {multiplierName, true}, {multiplierName, QStringLiteral("2")},
            {multiplierName, 0}, {multiplierName, 4}, {multiplierName, 1.5},
            {multiplierName, std::numeric_limits<double>::quiet_NaN()},
            {QStringLiteral("showSteamFlow"), true}
        };
        for (const auto& write : invalidWrites) {
            const qsizetype count = completed.count();
            f.service->setPreference(requestId(), graphDomain, write.first, write.second,
                                     f.service->revisions().value(graphDomain).toULongLong());
            QTRY_COMPARE(completed.count(), count + 1);
            QVERIFY(!completed.last().at(3).toBool());
            QCOMPARE(f.settings.graph()->advancedMode(), advanced);
            QCOMPARE(f.settings.graph()->flowMultiplier(), multiplier);
        }

        const qsizetype count = completed.count();
        f.service->setNode(nullptr);
        QVERIFY(!f.service->ready());
        f.service->setPreference(requestId(), graphDomain, advancedName, !advanced, 0);
        QTRY_COMPARE(completed.count(), count + 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("unavailable"));
        QCOMPARE(f.settings.graph()->advancedMode(), advanced);
    }

    void canonicalWritesUsePerDomainCasAndReplicate()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());

        const quint64 appRevision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        const quint64 hardwareRevision = f.service->revisions().value(QStringLiteral("hardware")).toULongLong();
        const QString desiredUnit = f.settings.app()->waterLevelDisplayUnit() == QStringLiteral("ml")
            ? QStringLiteral("percent") : QStringLiteral("ml");
        const QString unitId = requestId();
        f.service->setPreference(unitId, QStringLiteral("app"),
                                 QStringLiteral("waterLevelDisplayUnit"),
                                 desiredUnit, appRevision);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(completed.last().at(0).toString(), unitId);
        QCOMPARE(completed.last().at(1).toString(), QStringLiteral("app"));
        QCOMPARE(completed.last().at(2).toString(), QStringLiteral("waterLevelDisplayUnit"));
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.app()->waterLevelDisplayUnit(), desiredUnit);
        QTRY_COMPARE(f.service->preferences().value(QStringLiteral("app.waterLevelDisplayUnit")).toString(),
                     desiredUnit);
        const quint64 nextAppRevision = completed.last().at(4).toULongLong();
        QVERIFY(nextAppRevision > appRevision);

        // A write in another domain does not stale an unrelated domain's CAS.
        const QString hardwareId = requestId();
        const bool desiredTwoTapStop = !f.settings.hardware()->steamTwoTapStop();
        f.service->setPreference(hardwareId, QStringLiteral("hardware"),
                                 QStringLiteral("steamTwoTapStop"),
                                 desiredTwoTapStop, hardwareRevision);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), hardwareId);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.hardware()->steamTwoTapStop(), desiredTwoTapStop);

        // The original app revision is stale and cannot overwrite the first write.
        const QString staleId = requestId();
        const QString staleUnit = desiredUnit == QStringLiteral("ml")
            ? QStringLiteral("percent") : QStringLiteral("ml");
        f.service->setPreference(staleId, QStringLiteral("app"),
                                 QStringLiteral("waterLevelDisplayUnit"),
                                 staleUnit, appRevision);
        QTRY_COMPARE(completed.count(), 3);
        QCOMPARE(completed.last().at(0).toString(), staleId);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(completed.last().at(4).toULongLong(), nextAppRevision);
        QCOMPARE(f.settings.app()->waterLevelDisplayUnit(), desiredUnit);
        QCOMPARE(f.service->revisions().value(QStringLiteral("app")).toULongLong(), nextAppRevision);
    }

    void secondFacadeObservesWritesAndRejectsStaleDrafts()
    {
        Fixture f;
        QRemoteObjectNode observerClient;
        QVERIFY(observerClient.connectToNode(f.host.hostUrl()));
        // Each facade owns a replica acquired from a different valid TCP node. The
        // published-node pointer is only the local initialized-replica fallback.
        SettingsPreferencesService::setPublishedNode(&observerClient);
        QQmlEngine observerEngine;
        SettingsPreferencesService* observer =
            SettingsPreferencesServiceForeign::create(&observerEngine, &observerEngine);
        QVERIFY(observer);
        QTRY_VERIFY(observer->ready());
        const quint64 observerDraftRevision =
            observer->revisions().value(QStringLiteral("app")).toULongLong();
        QVERIFY(f.service->ready());

        const quint64 capturedRevision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        QCOMPARE(capturedRevision, observerDraftRevision);
        const QString nextUnit = f.settings.app()->temperatureUnit() == QStringLiteral("celsius")
            ? QStringLiteral("fahrenheit") : QStringLiteral("celsius");
        QSignalSpy writerCompleted(f.service, &SettingsPreferencesService::preferenceCompleted);
        QSignalSpy observerCompleted(observer, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(writerCompleted.isValid());
        QVERIFY(observerCompleted.isValid());

        f.service->setPreference(requestId(), QStringLiteral("app"),
                                 QStringLiteral("temperatureUnit"), nextUnit, capturedRevision);
        QTRY_COMPARE(writerCompleted.count(), 1);
        QVERIFY(writerCompleted.last().at(3).toBool());
        QTRY_COMPARE(observer->preferences().value(QStringLiteral("app.temperatureUnit")).toString(),
                     nextUnit);
        const quint64 updatedRevision = observer->revisions().value(QStringLiteral("app")).toULongLong();
        QVERIFY(updatedRevision > capturedRevision);

        // A draft captured by the observer before the other facade's write cannot overwrite it.
        const QString staleUnit = nextUnit == QStringLiteral("celsius")
            ? QStringLiteral("fahrenheit") : QStringLiteral("celsius");
        observer->setPreference(requestId(), QStringLiteral("app"),
                                QStringLiteral("temperatureUnit"), staleUnit, observerDraftRevision);
        QTRY_COMPARE(observerCompleted.count(), 1);
        QVERIFY(!observerCompleted.last().at(3).toBool());
        QCOMPARE(observerCompleted.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(observerCompleted.last().at(4).toULongLong(), updatedRevision);
        QCOMPARE(f.settings.app()->temperatureUnit(), nextUnit);
        QCOMPARE(observer->preferences().value(QStringLiteral("app.temperatureUnit")).toString(), nextUnit);
        QCOMPARE(observer->revisions().value(QStringLiteral("app")).toULongLong(), updatedRevision);
    }

    void ordinaryMachinePreferencesAcceptCanonicalValues()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());

        struct AcceptedWrite {
            QString domain;
            QString name;
            QVariant value;
            QString snapshotKey;
        };
        const bool launcherMode = !f.settings.app()->launcherMode();
        const bool screenCapture = !f.settings.app()->screenCaptureEnabled();
        const bool letRecipeDecide = !f.settings.brew()->letRecipeDecide();
        const bool showAfterShot = !f.settings.visualizer()->visualizerShowAfterShot();
        const bool clearNotes = !f.settings.visualizer()->visualizerClearNotesOnStart();
        const QString temperatureUnit = f.settings.app()->temperatureUnit() == QStringLiteral("celsius")
            ? QStringLiteral("fahrenheit") : QStringLiteral("celsius");
        const QList<AcceptedWrite> writes{
            {QStringLiteral("app"), QStringLiteral("launcherMode"), launcherMode,
             QStringLiteral("app.launcherMode")},
            {QStringLiteral("app"), QStringLiteral("temperatureUnit"), temperatureUnit,
             QStringLiteral("app.temperatureUnit")},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), 3,
             QStringLiteral("app.waterRefillPoint")},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), 70,
             QStringLiteral("app.waterRefillPoint")},
            {QStringLiteral("app"), QStringLiteral("refillKitOverride"), 0,
             QStringLiteral("app.refillKitOverride")},
            {QStringLiteral("app"), QStringLiteral("refillKitOverride"), 1,
             QStringLiteral("app.refillKitOverride")},
            {QStringLiteral("app"), QStringLiteral("refillKitOverride"), 2,
             QStringLiteral("app.refillKitOverride")},
            {QStringLiteral("app"), QStringLiteral("screenCaptureEnabled"), screenCapture,
             QStringLiteral("app.screenCaptureEnabled")},
            {QStringLiteral("brew"), QStringLiteral("letRecipeDecide"), letRecipeDecide,
             QStringLiteral("brew.letRecipeDecide")},
            {QStringLiteral("brew"), QStringLiteral("steamAutoFlushSeconds"), 0,
             QStringLiteral("brew.steamAutoFlushSeconds")},
            {QStringLiteral("brew"), QStringLiteral("steamAutoFlushSeconds"), 60,
             QStringLiteral("brew.steamAutoFlushSeconds")},
            {QStringLiteral("visualizer"), QStringLiteral("visualizerShowAfterShot"), showAfterShot,
             QStringLiteral("visualizer.visualizerShowAfterShot")},
            {QStringLiteral("visualizer"), QStringLiteral("visualizerClearNotesOnStart"), clearNotes,
             QStringLiteral("visualizer.visualizerClearNotesOnStart")},
        };

        for (const AcceptedWrite& write : writes) {
            const qsizetype count = completed.count();
            const QVariant oldValue = f.service->preferences().value(write.snapshotKey);
            const quint64 oldRevision = f.service->revisions().value(write.domain).toULongLong();
            f.service->setPreference(requestId(), write.domain, write.name, write.value, oldRevision);
            QTRY_COMPARE(completed.count(), count + 1);
            QVERIFY2(completed.last().at(3).toBool(), qPrintable(write.domain + QLatin1Char('.') + write.name));
            QTRY_COMPARE(f.service->preferences().value(write.snapshotKey), write.value);
            if (oldValue == write.value)
                QCOMPARE(f.service->revisions().value(write.domain).toULongLong(), oldRevision);
            else
                QTRY_VERIFY(f.service->revisions().value(write.domain).toULongLong() > oldRevision);
        }
    }

    void closedAllowlistAndStrictRangesRejectWithoutMutation()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());

        struct InvalidWrite { QString domain; QString name; QVariant value; };
        const QList<InvalidWrite> invalidWrites{
            {QStringLiteral("unknown"), QStringLiteral("launcherMode"), true},
            {QStringLiteral("app"), QStringLiteral("visualizerPassword"), QStringLiteral("not-secret")},
            {QStringLiteral("app"), QStringLiteral("waterLevelDisplayUnit"), QStringLiteral("milliliters")},
            {QStringLiteral("app"), QStringLiteral("temperatureUnit"), QStringLiteral("kelvin")},
            {QStringLiteral("app"), QStringLiteral("refillKitOverride"), 3},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), 2},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), 71},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), 5.5},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), std::numeric_limits<double>::quiet_NaN()},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), std::numeric_limits<double>::infinity()},
            {QStringLiteral("app"), QStringLiteral("waterRefillPoint"), QStringLiteral("5")},
            {QStringLiteral("brew"), QStringLiteral("steamAutoFlushSeconds"), 61},
            {QStringLiteral("brew"), QStringLiteral("steamAutoFlushSeconds"), true},
            {QStringLiteral("brew"), QStringLiteral("keepWarmWhenIdle"), QStringLiteral("true")},
            {QStringLiteral("hardware"), QStringLiteral("steamTwoTapStop"), 1},
            {QStringLiteral("visualizer"), QStringLiteral("visualizerShowAfterShot"), 0},
        };

        const QVariantMap before = f.service->preferences();
        const int beforeWaterRefillPoint = f.settings.app()->waterRefillPoint();
        const int beforeAutoFlush = f.settings.brew()->steamAutoFlushSeconds();
        for (const InvalidWrite& write : invalidWrites) {
            const qsizetype count = completed.count();
            const quint64 revision = f.service->revisions().value(write.domain).toULongLong();
            const QString id = requestId();
            f.service->setPreference(id, write.domain, write.name, write.value, revision);
            QTRY_COMPARE(completed.count(), count + 1);
            QCOMPARE(completed.last().at(0).toString(), id);
            QVERIFY(!completed.last().at(3).toBool());
            QCOMPARE(f.service->preferences(), before);
        }
        QCOMPARE(f.settings.app()->waterRefillPoint(), beforeWaterRefillPoint);
        QCOMPARE(f.settings.brew()->steamAutoFlushSeconds(), beforeAutoFlush);
    }

    void heaterPolicyWriteRequestsOneCanonicalApply()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QSignalSpy applyRequested(&f.source, &NativeSettingsPreferencesSource::steamSettingsApplyRequested);
        QVERIFY(completed.isValid());
        QVERIFY(applyRequested.isValid());

        const bool newValue = !f.settings.brew()->keepWarmWhenIdle();
        const QString id = requestId();
        const quint64 revision = f.service->revisions().value(QStringLiteral("brew")).toULongLong();
        f.service->setPreference(id, QStringLiteral("brew"), QStringLiteral("keepWarmWhenIdle"),
                                 newValue, revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(applyRequested.count(), 1);
        QCOMPARE(f.settings.brew()->keepWarmWhenIdle(), newValue);

        const QString unchangedId = requestId();
        const quint64 currentRevision = f.service->revisions().value(QStringLiteral("brew")).toULongLong();
        f.service->setPreference(unchangedId, QStringLiteral("brew"),
                                 QStringLiteral("keepWarmWhenIdle"), newValue, currentRevision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(applyRequested.count(), 1);
    }

    void screensaverSettingsUseStrictFiniteNumbersAndCanonicalSetters()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        const QString key = QStringLiteral("screensaver.pipesSpeed");
        QVERIFY(f.service->preferences().contains(key));
        QCOMPARE(f.service->preferences().value(key).toDouble(), 0.5);

        const QList<QVariant> invalidValues{
            QStringLiteral("0.7"),
            true,
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            0.09,
            2.01,
        };
        for (const QVariant& value : invalidValues) {
            const qsizetype count = completed.count();
            const quint64 revision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
            const QString id = requestId();
            f.service->setPreference(id, QStringLiteral("screensaver"), QStringLiteral("pipesSpeed"),
                                     value, revision);
            QTRY_COMPARE(completed.count(), count + 1);
            QCOMPARE(completed.last().at(0).toString(), id);
            QVERIFY(!completed.last().at(3).toBool());
            QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
            QCOMPARE(f.screensaverManager->pipesSpeed(), 0.5);
        }

        const quint64 revision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
        const QString id = requestId();
        f.service->setPreference(id, QStringLiteral("screensaver"), QStringLiteral("pipesSpeed"),
                                 1.25, revision);
        QTRY_COMPARE(completed.count(), static_cast<int>(invalidValues.size()) + 1);
        QCOMPARE(completed.last().at(0).toString(), id);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.screensaverManager->pipesSpeed(), 1.25);
        QCOMPARE(f.service->preferences().value(key).toDouble(), 1.25);

        const quint64 currentRevision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
        const QString invalidNameId = requestId();
        f.service->setPreference(invalidNameId, QStringLiteral("screensaver"), QStringLiteral("overlayShowBogus"),
                                 true, currentRevision);
        QTRY_COMPARE(completed.count(), static_cast<int>(invalidValues.size()) + 2);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidPreference"));
    }

    void screensaverPreferenceWritesRejectStaleDomainRevision()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        const quint64 originalRevision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();

        const QString firstId = requestId();
        f.service->setPreference(firstId, QStringLiteral("screensaver"), QStringLiteral("dimPercent"),
                                 35, originalRevision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.screensaverManager->dimPercent(), 35);

        const QString staleId = requestId();
        f.service->setPreference(staleId, QStringLiteral("screensaver"), QStringLiteral("dimPercent"),
                                 60, originalRevision);
        QTRY_COMPARE(completed.count(), 2);
        QCOMPARE(completed.last().at(0).toString(), staleId);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.screensaverManager->dimPercent(), 35);
    }

    void autoWakeAndSleepPreferencesShareScreensaverRevisionAndValidation()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        const QVariantList original = f.service->preferences()
            .value(QStringLiteral("screensaver.autoWakeSchedule")).toList();
        QCOMPARE(original.size(), 7);

        int rejectedCount = 0;
        for (const QVariant& value : {QVariant(241), QVariant(26), QVariant(QStringLiteral("60"))}) {
            const QString id = requestId();
            const quint64 revision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
            f.service->setPreference(id, QStringLiteral("screensaver"), QStringLiteral("autoSleepMinutes"),
                                     value, revision);
            ++rejectedCount;
            QTRY_COMPARE(completed.count(), rejectedCount);
            QVERIFY(!completed.last().at(3).toBool());
            QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
            QCOMPARE(f.settings.value(QStringLiteral("autoSleepMinutes")).toInt(), 60);
        }

        const QVariantList invalidSchedules{
            QVariantList{},
            QVariantList{QVariantMap{{QStringLiteral("enabled"), QStringLiteral("yes")},
                                     {QStringLiteral("hour"), 7}, {QStringLiteral("minute"), 0}}},
        };
        for (const QVariant& scheduleValue : invalidSchedules) {
            const QString id = requestId();
            const quint64 revision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
            f.service->setPreference(id, QStringLiteral("screensaver"), QStringLiteral("autoWakeSchedule"),
                                     scheduleValue, revision);
            ++rejectedCount;
            QTRY_COMPARE(completed.count(), rejectedCount);
            QVERIFY(!completed.last().at(3).toBool());
            QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
            QCOMPARE(f.service->preferences().value(QStringLiteral("screensaver.autoWakeSchedule")).toList(), original);
        }

        QVariantList updated = original;
        QVariantMap monday = updated.first().toMap();
        monday.insert(QStringLiteral("enabled"), true);
        monday.insert(QStringLiteral("hour"), 6);
        monday.insert(QStringLiteral("minute"), 30);
        updated[0] = monday;
        const QString scheduleId = requestId();
        quint64 revision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
        f.service->setPreference(scheduleId, QStringLiteral("screensaver"), QStringLiteral("autoWakeSchedule"),
                                 updated, revision);
        QTRY_COMPARE(completed.count(), rejectedCount + 1);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.autoWake()->autoWakeSchedule(), updated);

        const QString sleepId = requestId();
        revision = f.service->revisions().value(QStringLiteral("screensaver")).toULongLong();
        f.service->setPreference(sleepId, QStringLiteral("screensaver"), QStringLiteral("autoSleepMinutes"),
                                 25, revision);
        QTRY_COMPARE(completed.count(), rejectedCount + 2);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.value(QStringLiteral("autoSleepMinutes")).toInt(), 25);

        const QString staleId = requestId();
        f.service->setPreference(staleId, QStringLiteral("screensaver"), QStringLiteral("autoSleepMinutes"),
                                 30, revision);
        QTRY_COMPARE(completed.count(), rejectedCount + 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.settings.value(QStringLiteral("autoSleepMinutes")).toInt(), 25);
    }

    void machineAndBatteryPreferencesUseCanonicalValidatedSetters()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());

        const auto write = [&f](const QString& domain, const QString& name,
                                const QVariant& value, quint64 revision) {
            const QString id = requestId();
            f.service->setPreference(id, domain, name, value, revision);
        };

        quint64 revision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        write(QStringLiteral("app"), QStringLiteral("configurePageScale"),
              QStringLiteral("true"), revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.settings.value(QStringLiteral("ui/configurePageScale")).toBool(), false);

        revision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        write(QStringLiteral("app"), QStringLiteral("configurePageScale"), true, revision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.value(QStringLiteral("ui/configurePageScale")).toBool(), true);

        revision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        write(QStringLiteral("app"), QStringLiteral("simulationMode"),
              QStringLiteral("true"), revision);
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));

        revision = f.service->revisions().value(QStringLiteral("app")).toULongLong();
        write(QStringLiteral("app"), QStringLiteral("simulationMode"), true, revision);
        QTRY_COMPARE(completed.count(), 4);
        const bool simulatorAvailable = f.service->preferences()
            .value(QStringLiteral("app.simulatorAvailable")).toBool();
        QCOMPARE(completed.last().at(3).toBool(), simulatorAvailable);
        if (!simulatorAvailable)
            QCOMPARE(completed.last().at(5).toString(), QStringLiteral("unavailable"));

        revision = f.service->revisions().value(QStringLiteral("battery")).toULongLong();
        write(QStringLiteral("battery"), QStringLiteral("chargingMode"), 3, revision);
        QTRY_COMPARE(completed.count(), 5);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.batteryManager.chargingMode(), int(BatteryManager::On));

        revision = f.service->revisions().value(QStringLiteral("battery")).toULongLong();
        write(QStringLiteral("battery"), QStringLiteral("chargingMode"), 2, revision);
        QTRY_COMPARE(completed.count(), 6);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.batteryManager.chargingMode(), int(BatteryManager::Night));
        QCOMPARE(f.settings.value(QStringLiteral("smartBatteryCharging")).toInt(),
                 int(BatteryManager::Night));

        revision = f.service->revisions().value(QStringLiteral("battery")).toULongLong();
        write(QStringLiteral("battery"), QStringLiteral("chargingMode"), int(BatteryManager::Off), revision);
        QTRY_COMPARE(completed.count(), 7);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.batteryManager.chargingMode(), int(BatteryManager::Off));
        QCOMPARE(f.settings.value(QStringLiteral("smartBatteryCharging")).toInt(),
                 int(BatteryManager::Off));

        revision = f.service->revisions().value(QStringLiteral("battery")).toULongLong();
        write(QStringLiteral("battery"), QStringLiteral("usbChargerEnabled"), 0, revision);
        QTRY_COMPARE(completed.count(), 8);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.batteryManager.usbChargerEnabled(), true);

        write(QStringLiteral("battery"), QStringLiteral("usbChargerEnabled"), false, revision);
        QTRY_COMPARE(completed.count(), 9);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.batteryManager.usbChargerEnabled(), false);
        QCOMPARE(f.settings.value(QStringLiteral("battery/usbChargerEnabled")).toBool(), false);

        write(QStringLiteral("battery"), QStringLiteral("usbChargerEnabled"), true, revision);
        QTRY_COMPARE(completed.count(), 10);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.batteryManager.usbChargerEnabled(), false);
    }


    void uploadPreferencesUseClosedTypesAndPerDomainCas()
    {
        Fixture f;
        const QVariantMap prefs = f.service->preferences();
        QCOMPARE(prefs.value(QStringLiteral("upload.autoUpload")).toBool(), true);
        QCOMPARE(prefs.value(QStringLiteral("upload.autoUpdate")).toBool(), true);
        QCOMPARE(prefs.value(QStringLiteral("upload.minDuration")).toDouble(), 6.0);
        QVERIFY(!prefs.contains(QStringLiteral("upload.visualizerPassword")));
        QVERIFY(!prefs.contains(QStringLiteral("upload.decentPassword")));

        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        quint64 revision = f.service->revisions().value(QStringLiteral("upload")).toULongLong();
        const auto write = [&f](const QString& name, const QVariant& value, quint64 expected) {
            f.service->setPreference(requestId(), QStringLiteral("upload"), name, value, expected);
        };
        write(QStringLiteral("autoUpload"), 0, revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.settings.upload()->autoUpload(), true);

        write(QStringLiteral("minDuration"), 31.0, revision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        write(QStringLiteral("minDuration"), 4.5, revision);
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.upload()->minDuration(), 4.5);
        write(QStringLiteral("autoUpdate"), false, revision);
        QTRY_COMPARE(completed.count(), 4);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.settings.upload()->autoUpdate(), true);
    }

    void communitySearchPreferencesUseVisualizerDomainAndStrictValidation()
    {
        Fixture f;
        f.settings.visualizer()->setCommunitySearchEnabled(true);
        f.settings.visualizer()->setCommunityMinEnjoyment(70);
        const QVariantMap initial = f.service->preferences();
        QCOMPARE(initial.value(QStringLiteral("visualizer.communitySearchEnabled")).toBool(), true);
        QCOMPARE(initial.value(QStringLiteral("visualizer.communityMinEnjoyment")).toInt(), 70);
        QVERIFY(!initial.contains(QStringLiteral("visualizer.communityApiBaseUrl")));
        QVERIFY(!initial.contains(QStringLiteral("visualizer.communityRelativeGrind")));

        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        const auto write = [&f](const QString& name, const QVariant& value, quint64 revision) {
            f.service->setPreference(requestId(), QStringLiteral("visualizer"), name,
                                     value, revision);
        };

        quint64 revision = f.service->revisions().value(QStringLiteral("visualizer")).toULongLong();
        write(QStringLiteral("communitySearchEnabled"), QStringLiteral("false"), revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.settings.visualizer()->communitySearchEnabled(), true);

        write(QStringLiteral("communitySearchEnabled"), false, revision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.visualizer()->communitySearchEnabled(), false);
        QCOMPARE(f.service->preferences().value(
                     QStringLiteral("visualizer.communitySearchEnabled")).toBool(), false);
        QVERIFY(f.service->revisions().value(QStringLiteral("visualizer")).toULongLong() > revision);

        write(QStringLiteral("communityMinEnjoyment"), 80, revision);
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.settings.visualizer()->communityMinEnjoyment(), 70);

        revision = f.service->revisions().value(QStringLiteral("visualizer")).toULongLong();
        write(QStringLiteral("communityMinEnjoyment"), 85, revision);
        QTRY_COMPARE(completed.count(), 4);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.settings.visualizer()->communityMinEnjoyment(), 85);

        const QList<QVariant> invalidEnjoymentValues{
            -1, 101, 85.5, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), QStringLiteral("85"), true
        };
        int expectedCompletionCount = 5;
        for (const QVariant& value : invalidEnjoymentValues) {
            revision = f.service->revisions().value(QStringLiteral("visualizer")).toULongLong();
            write(QStringLiteral("communityMinEnjoyment"), value, revision);
            QTRY_COMPARE(completed.count(), expectedCompletionCount);
            ++expectedCompletionCount;
            QVERIFY(!completed.last().at(3).toBool());
            QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
            QCOMPARE(f.settings.visualizer()->communityMinEnjoyment(), 85);
        }

        const QString originalBaseUrl = f.settings.visualizer()->communityApiBaseUrl();
        const bool originalRelativeGrind = f.settings.visualizer()->communityRelativeGrind();
        revision = f.service->revisions().value(QStringLiteral("visualizer")).toULongLong();
        write(QStringLiteral("communityApiBaseUrl"), QStringLiteral("https://example.invalid"), revision);
        QTRY_COMPARE(completed.count(), 12);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(f.settings.visualizer()->communityApiBaseUrl(), originalBaseUrl);

        write(QStringLiteral("communityRelativeGrind"), true, revision);
        QTRY_COMPARE(completed.count(), 13);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidPreference"));
        QCOMPARE(f.settings.visualizer()->communityRelativeGrind(), originalRelativeGrind);
    }

    void accessibilityPreferencesUseCanonicalManagerAndStrictValidation()
    {
        Fixture f;
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        const QVariantMap initial = f.service->preferences();
        QCOMPARE(initial.value(QStringLiteral("accessibility.enabled")).toBool(), false);
        QCOMPARE(initial.value(QStringLiteral("accessibility.ttsEnabled")).toBool(), true);
        QCOMPARE(initial.value(QStringLiteral("accessibility.extractionAnnouncementMode")).toString(),
                 QStringLiteral("both"));

        quint64 revision = f.service->revisions().value(QStringLiteral("accessibility")).toULongLong();
        const QString badId = requestId();
        f.service->setPreference(badId, QStringLiteral("accessibility"),
                                 QStringLiteral("extractionAnnouncementInterval"), 31, revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.accessibility.extractionAnnouncementInterval(), 5);

        const QString modeId = requestId();
        f.service->setPreference(modeId, QStringLiteral("accessibility"),
                                 QStringLiteral("extractionAnnouncementMode"),
                                 QStringLiteral("milestones_only"), revision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.accessibility.extractionAnnouncementMode(), QStringLiteral("milestones_only"));
        QCOMPARE(f.service->preferences().value(
                     QStringLiteral("accessibility.extractionAnnouncementMode")).toString(),
                 QStringLiteral("milestones_only"));

        const QString invalidBooleanId = requestId();
        revision = f.service->revisions().value(QStringLiteral("accessibility")).toULongLong();
        f.service->setPreference(invalidBooleanId, QStringLiteral("accessibility"),
                                 QStringLiteral("tickEnabled"), 1, revision);
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.accessibility.tickEnabled(), true);
    }

    void shotMapPreferencesPublishOnlyHostOwnedDisplayState()
    {
        Fixture f;
        const QVariantMap prefs = f.service->preferences();
        QVERIFY(prefs.contains(QStringLiteral("shotMap.enabled")));
        QCOMPARE(prefs.value(QStringLiteral("shotMap.enabled")).toBool(), false);
        QCOMPARE(prefs.value(QStringLiteral("shotMap.manualCity")).toString(), QString());
        QCOMPARE(prefs.value(QStringLiteral("shotMap.hasLocation")).toBool(), false);
        QCOMPARE(prefs.value(QStringLiteral("shotMap.usingManualCity")).toBool(), false);
        QCOMPARE(prefs.value(QStringLiteral("shotMap.city")).toString(), QString());
        QCOMPARE(prefs.value(QStringLiteral("shotMap.countryCode")).toString(), QString());
        QCOMPARE(prefs.value(QStringLiteral("shotMap.latitude")).toDouble(), 0.0);
        QCOMPARE(prefs.value(QStringLiteral("shotMap.longitude")).toDouble(), 0.0);
        QCOMPARE(prefs.value(QStringLiteral("shotMap.gpsEnabled")).toBool(), false);

        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());
        quint64 revision = f.service->revisions().value(QStringLiteral("shotMap")).toULongLong();
        const auto write = [&f](const QString& name, const QVariant& value, quint64 expectedRevision) {
            f.service->setPreference(requestId(), QStringLiteral("shotMap"), name,
                                     value, expectedRevision);
        };

        write(QStringLiteral("enabled"), QStringLiteral("true"), revision);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));
        QCOMPARE(f.shotReporter->isEnabled(), false);

        write(QStringLiteral("manualCity"), 42, revision);
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));

        write(QStringLiteral("manualCity"), QString(121, QLatin1Char('x')), revision);
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("invalidValue"));

        write(QStringLiteral("enabled"), true, revision);
        QTRY_COMPARE(completed.count(), 4);
        QVERIFY(completed.last().at(3).toBool());
        QCOMPARE(f.shotReporter->isEnabled(), true);
        QCOMPARE(f.settings.value(QStringLiteral("shotmap/enabled")).toBool(), true);

        // Refresh routes to the host reporter. With no LocationProvider in
        // this fixture, it cannot start GPS, permission or network activity.
        f.service->refreshShotMapLocation();
        QCOMPARE(f.service->preferences().value(QStringLiteral("shotMap.hasLocation")).toBool(), false);

        write(QStringLiteral("enabled"), false, revision);
        QTRY_COMPARE(completed.count(), 5);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(completed.last().at(5).toString(), QStringLiteral("stalePreferences"));
        QCOMPARE(f.shotReporter->isEnabled(), true);
    }

    void aiPreferencesNeverPublishCredentialValues()
    {
        Fixture f;
        const QString original = f.settings.ai()->openaiApiKey();
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());

        const QVariantMap initial = f.service->preferences();
        QVERIFY(!initial.contains(QStringLiteral("ai.openaiApiKey")));
        QVERIFY(initial.contains(QStringLiteral("ai.hasOpenaiApiKey")));

        const QString replacement = QStringLiteral("private-test-provider-key");
        f.service->setPreference(requestId(), QStringLiteral("ai"),
                                 QStringLiteral("openaiApiKey"), replacement,
                                 f.service->revisions().value(QStringLiteral("ai")).toULongLong());
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last().at(3).toBool());
        const QVariantMap updated = f.service->preferences();
        QVERIFY(!updated.contains(QStringLiteral("ai.openaiApiKey")));
        QVERIFY(!updated.values().contains(replacement));
        QVERIFY(updated.value(QStringLiteral("ai.hasOpenaiApiKey")).toBool());
        QCOMPARE(f.settings.ai()->openaiApiKey(), replacement);

        f.settings.ai()->setOpenaiApiKey(original);
    }

    void aiAndMcpPreferencesUseStrictHostAllowlist()
    {
        Fixture f;
        const QString originalProvider = f.settings.ai()->aiProvider();
        const QString originalEndpoint = f.settings.ai()->openaiEndpoint();
        const int originalAccess = f.settings.mcp()->mcpAccessLevel();
        const int originalDiscussApp = f.settings.network()->discussShotApp();
        QSignalSpy completed(f.service, &SettingsPreferencesService::preferenceCompleted);
        QVERIFY(completed.isValid());

        const QVariantMap prefs = f.service->preferences();
        QVERIFY(prefs.contains(QStringLiteral("ai.aiProvider")));
        QVERIFY(prefs.contains(QStringLiteral("mcp.mcpEnabled")));
        QVERIFY(prefs.contains(QStringLiteral("aiNetwork.discussShotApp")));
        QVERIFY(!prefs.contains(QStringLiteral("ai.openaiApiKey")));
        QVERIFY(!prefs.contains(QStringLiteral("mcp.remoteMcpToken")));

        const auto write = [&f](const QString& domain, const QString& name,
                                const QVariant& value) {
            f.service->setPreference(requestId(), domain, name, value,
                f.service->revisions().value(domain).toULongLong());
        };
        write(QStringLiteral("ai"), QStringLiteral("aiProvider"), 1);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(f.settings.ai()->aiProvider(), originalProvider);

        write(QStringLiteral("ai"), QStringLiteral("openaiEndpoint"),
              QStringLiteral("javascript:alert(1)"));
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(f.settings.ai()->openaiEndpoint(), originalEndpoint);

        write(QStringLiteral("mcp"), QStringLiteral("mcpAccessLevel"), 3);
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(f.settings.mcp()->mcpAccessLevel(), originalAccess);

        write(QStringLiteral("aiNetwork"), QStringLiteral("discussShotApp"), 99);
        QTRY_COMPARE(completed.count(), 4);
        QVERIFY(!completed.last().at(3).toBool());
        QCOMPARE(f.settings.network()->discussShotApp(), originalDiscussApp);

        f.settings.ai()->setOpenaiEndpoint(originalEndpoint);
        f.settings.mcp()->setMcpAccessLevel(originalAccess);
        f.settings.network()->setDiscussShotApp(originalDiscussApp);
    }
};

QTEST_MAIN(SettingsPreferencesServiceTest)
#include "tst_settingspreferencesservice.moc"
