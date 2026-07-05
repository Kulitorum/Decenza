#include "core/settings_app.h"
#include "maincontroller.h"
#include "shottimingcontroller.h"
#include "autoflowcalclassifier.h"
#include "abortedshotclassifier.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_dye.h"
#include "../network/beanbase_blob.h"
#include "../core/settings_network.h"
#include "../core/settings_calibration.h"
#include "../core/settings_mqtt.h"
#include "../core/settings_hardware.h"
#include "../core/settings_visualizer.h"
#include "../core/profilestorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"
#include "../models/shotcomparisonmodel.h"
#include "../network/visualizeruploader.h"
#include "../network/visualizerimporter.h"
#include "../ai/aimanager.h"
#include "../history/shothistorystorage.h"
#include "../history/shotimporter.h"
#include "../history/shotdebuglogger.h"
#include "../network/shotserver.h"
#include "../network/locationprovider.h"
#include "../core/crashhandler.h"
#include "../ble/blemanager.h"
#include "../ble/scaledevice.h"
#include "../ble/scales/flowscale.h"
#include "../ble/refractometers/refractometerdevice.h"
#include <QGuiApplication>
#include <QClipboard>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QSqlDatabase>
#include "../core/dbutils.h"
#include <QSqlError>
#include <QPointer>
#include <tuple>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>
#include <QRandomGenerator>
#include <algorithm>
#include <memory>
#include <QCoreApplication>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

MainController::MainController(QNetworkAccessManager* networkManager,
                               Settings* settings, DE1Device* device,
                               MachineState* machineState, ShotDataModel* shotDataModel,
                               ProfileStorage* profileStorage,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_shotDataModel(shotDataModel)
    , m_profileStorage(profileStorage)
    , m_networkManager(networkManager)
{
    // Create ProfileManager — owns all profile lifecycle operations
    m_profileManager = new ProfileManager(m_settings, m_device, m_machineState, m_profileStorage, this);

    // Create LiveSteamCoach — local, real-time during-steam coaching cues. It
    // subscribes itself to MachineState phase/shot-time changes and reads the
    // target steam duration from Settings; no AI/network/DB in the hot path.
    m_liveSteamCoach = new LiveSteamCoach(m_machineState, m_settings, this);

    // Connect to shot sample updates
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &MainController::onShotSampleReceived);

        // Apply user settings immediately after device sends its initial (hardcoded) settings.
        // BleTransport's FIFO queue guarantees our writes follow the initial writes.
        connect(m_device, &DE1Device::initialSettingsComplete,
                this, &MainController::applyAllSettings);

        // Verify that the DE1 stored what we commanded. Fires on every
        // SHOT_SETTINGS indication from the DE1 (the characteristic is
        // subscribed in BleTransport::subscribeAll()) and on the one-time
        // read issued by subscribeAll() at connect time.
        connect(m_device, &DE1Device::shotSettingsReported,
                this, &MainController::onShotSettingsReported);
    }
    // Send water refill level to machine when setting changes
    if (m_settings && m_device) {
        connect(m_settings->app(), &SettingsApp::waterRefillPointChanged,
                this, &MainController::applyWaterRefillLevel);
    }

    // Apply refill kit override when setting changes or when kit detection completes.
    // Kit state also gates the effective refill level (see applyWaterRefillLevel), so
    // re-send the refill level on those transitions too.
    if (m_settings && m_device) {
        connect(m_settings->app(), &SettingsApp::refillKitOverrideChanged,
                this, &MainController::applyRefillKitOverride);
        connect(m_settings->app(), &SettingsApp::refillKitOverrideChanged,
                this, &MainController::applyWaterRefillLevel);
        connect(m_device, &DE1Device::refillKitDetectedChanged,
                this, &MainController::applyRefillKitOverride);
        connect(m_device, &DE1Device::refillKitDetectedChanged,
                this, &MainController::applyWaterRefillLevel);
    }

    // Apply flow calibration multiplier when setting changes
    if (m_settings && m_device) {
        connect(m_settings->calibration(), &SettingsCalibration::flowCalibrationMultiplierChanged,
                this, &MainController::applyFlowCalibration);
        connect(m_settings->calibration(), &SettingsCalibration::autoFlowCalibrationChanged,
                this, &MainController::applyFlowCalibration);
        connect(m_settings->calibration(), &SettingsCalibration::perProfileFlowCalibrationChanged,
                this, &MainController::applyFlowCalibration);
    }
    // Apply heater tweaks when any calibration setting changes (debounced to avoid BLE flood)
    m_heaterTweaksTimer.setSingleShot(true);
    m_heaterTweaksTimer.setInterval(300);
    connect(&m_heaterTweaksTimer, &QTimer::timeout, this, &MainController::applyHeaterTweaks);

    if (m_settings && m_device) {
        auto startHeaterTimer = [this]() { m_heaterTweaksTimer.start(); };
        auto* hw = m_settings->hardware();
        connect(hw, &SettingsHardware::heaterIdleTempChanged, this, startHeaterTimer);
        connect(hw, &SettingsHardware::heaterWarmupFlowChanged, this, startHeaterTimer);
        connect(hw, &SettingsHardware::heaterTestFlowChanged, this, startHeaterTimer);
        connect(hw, &SettingsHardware::heaterWarmupTimeoutChanged, this, startHeaterTimer);
        connect(hw, &SettingsHardware::hotWaterFlowRateChanged, this, startHeaterTimer);
        connect(hw, &SettingsHardware::steamTwoTapStopChanged, this, startHeaterTimer);
        connect(hw, &SettingsHardware::fanThresholdChanged, this, startHeaterTimer);
    }
    // Connect to machine state events
    if (m_machineState) {
        connect(m_machineState, &MachineState::espressoCycleStarted,
                this, &MainController::onEspressoCycleStarted);
        // Note: shotEnded -> onShotEnded is NOT connected here.
        // Instead, ShotTimingController::shotProcessingReady -> onShotEnded is connected in main.cpp
        // This ensures shot processing waits for SAW settling if needed.
        // Clear any pre-tare weight samples when tare completes (race condition fix)
        connect(m_machineState, &MachineState::tareCompleted, this, [this]() {
            if (m_shotDataModel) {
                m_shotDataModel->clearWeightData();
            }
        });

        // Clear temporary steam disable when machine goes to sleep or disconnects
        // so it resets to normal behavior on next wake/reconnect
        connect(m_machineState, &MachineState::phaseChanged, this, [this]() {
            auto phase = m_machineState->phase();
            if ((phase == MachineState::Phase::Sleep || phase == MachineState::Phase::Disconnected)
                && m_settings && m_settings->brew()->steamDisabled()) {
                qDebug() << "Machine entering" << m_machineState->phaseString() << "- clearing temporary steamDisabled flag";
                m_settings->brew()->setSteamDisabled(false);
            }

            // Steam session ended — run post-session analysis. m_steamStartTimeMs
            // is only set when isFlowing() was true (Steaming/Pouring substates),
            // so this fires after all flowing samples have been collected even
            // though the Steaming phase persists through Puffing/Ending substates.
            if (phase != MachineState::Phase::Steaming && m_steamStartTimeMs > 0) {
                if (m_steamHealthTracker && m_steamDataModel) {
                    m_steamHealthTracker->onSessionComplete(
                        m_steamDataModel,
                        m_settings->brew()->steamFlow(),
                        static_cast<int>(m_settings->brew()->steamTemperature()));
                }
                m_steamStartTimeMs = 0;
                if (m_steamHealthTracker)
                    m_steamHealthTracker->resetSession();
            }
        });
    }
    // Create visualizer uploader and importer
    m_visualizer = new VisualizerUploader(m_networkManager, m_settings, this);
    m_visualizer->setDevice(m_device);
    m_visualizerImporter = new VisualizerImporter(m_networkManager, this, m_settings, this);
    m_beanbase = new BeanBaseClient(m_networkManager, m_settings, this);

    // Create shot history storage and comparison model
    m_shotHistory = new ShotHistoryStorage(this);
    m_shotHistory->initialize();
    // Mirror the startup-seeded latest-shot id (initialize() reads MAX(id))
    // so QML's MainController.lastSavedShotId is valid across restarts —
    // no emit needed: QML bindings haven't been created yet.
    m_lastSavedShotId = m_shotHistory->lastSavedShotId();
    connect(m_shotHistory, &QObject::destroyed, this, [this]() { m_savingShot = false; });

    // Coffee bag storage shares the shot history database (coffee_bags
    // table, created by migration 19 inside initialize() above).
    m_bagStorage = new CoffeeBagStorage(this);
    m_bagStorage->initialize(m_shotHistory->databasePath());
    // Adopt the bag that the legacy preset import mapped bean/selectedPreset
    // to — through the setter, so the settings cache and NOTIFY fire.
    if (m_shotHistory->migratedActiveBagId() > 0)
        m_settings->dye()->setActiveBagId(static_cast<int>(m_shotHistory->migratedActiveBagId()));
    m_settings->dye()->setBagStorage(m_bagStorage);

    // Equipment storage shares the same database (equipment_packages +
    // equipment_items tables, created by migration 22). Switchable grinder
    // packages the active bag points at via equipment_id.
    m_equipmentStorage = new EquipmentStorage(this);
    m_equipmentStorage->initialize(m_shotHistory->databasePath());
    m_settings->dye()->setEquipmentStorage(m_equipmentStorage);

    // Switching beans resets the brew overrides to the active profile's
    // defaults — a new coffee starts from the profile + bean baseline, not
    // the previous coffee's manual tweaks (the bag's own dose is applied by
    // SettingsDye when the bag loads). Loading a favorite or historical shot
    // is NOT affected: applyLoadedShotMetadata re-applies that shot's saved
    // overrides AFTER the bag change (this is a synchronous direct
    // connection), so the loaded settings win. Connected here — after the
    // startup migration adoption above — so adoption doesn't reset anything.
    connect(m_settings->dye(), &SettingsDye::activeBagIdChanged, this, [this]() {
        if (m_profileManager)
            m_profileManager->clearBrewOverrides();
    });

    // A user bean switch carries the bag's yield override (fires only from
    // applyActiveBag, never on a keep-fields historical/favorite load). It
    // arrives after the clear-to-profile reset above, so a bag with a saved
    // override re-establishes it (idle brew-settings widget turns yellow); a
    // bag with none (0) leaves the brew at the profile default the clear set.
    connect(m_settings->dye(), &SettingsDye::activeBagYieldOverrideApplied, this,
            [this](double yieldOverrideG) {
        if (yieldOverrideG > 0 && m_settings && m_settings->brew())
            m_settings->brew()->setBrewYieldOverride(yieldOverrideG);
    });

    // Unified bean search (Change Beans dialog): inventory + canonical
    // autocomplete + shot history in one ranked model.
    m_beanSearch = new UnifiedBeanSearchModel(this);
    m_beanSearch->setSources(m_bagStorage, m_beanbase, m_shotHistory->databasePath());

    // Coffee Management sync needs the local DB to read the uploaded shot's
    // bag and persist the remote ids back onto it.
    m_visualizer->setLocalDbPath(m_shotHistory->databasePath());

    // Push a bag edit to its already-synced Visualizer bag, gated on the SAME
    // toggle as shot metadata updates (bag sync follows shot sync). The signal
    // fires only when a Visualizer-stored field changed (CoffeeBagStorage owns
    // that test); updateBagOnVisualizer additionally no-ops unless CM is Active
    // and the bag has a visualizerBagId. Create/link still rides auto-upload via
    // the post-upload sync chain — this handles the edit-an-existing-bag case.
    connect(m_bagStorage, &CoffeeBagStorage::bagVisualizerFieldsChanged, this,
            [this](qint64 bagId) {
        if (m_visualizer && m_settings && m_settings->visualizer()->visualizerAutoUpdate())
            m_visualizer->updateBagOnVisualizer(bagId);
    });

    // Authoritative C++ writeback: a successful Visualizer upload
    // persists its returned id to the originating local shot row here,
    // independent of any UI page. (Previously only a transient
    // PostShotReviewPage/ShotDetailPage handler did this, so uploads
    // silently went unrecorded when the review page was disabled,
    // auto-closed, or navigated away before the ~1s round-trip — see
    // OpenSpec change persist-visualizer-id-in-controller.)
    connect(m_visualizer, &VisualizerUploader::uploadSucceededForShot, this,
            [this](qint64 dbShotId, const QString& visualizerId, const QString& url) {
        if (dbShotId <= 0 || visualizerId.isEmpty()) {
            qWarning() << "MainController: upload succeeded but no local shot id"
                          " to link (dbShotId=" << dbShotId << ")";
            return;
        }
        if (m_shotHistory && m_shotHistory->isReady())
            m_shotHistory->requestUpdateVisualizerInfo(dbShotId, visualizerId, url);
    });

    // Migration 16 ran inside initialize() above. If it found inferred
    // shots that were uploaded to Visualizer, it queued them in
    // QSettings under migration16/pendingVisualizerSync. Drain that
    // list now — guarded internally on credentials being available;
    // failed entries persist for the next boot.
    processPendingVisualizerRatingSync();

    // One-time reconciliation: relink shots that were uploaded before
    // the authoritative C++ writeback existed (the orphaned cohort),
    // then push their now-correct local rating to the cloud. Order-
    // independent of the migration16 drain above — see OpenSpec change
    // persist-visualizer-id-in-controller.
    processVisualizerReconciliation();

    connect(m_visualizer, &VisualizerUploader::updateSuccess, this,
            [this](const QString& visualizerId) {
        // Only react when this matches the migration16 PATCH we issued;
        // other concurrent updates (e.g., user-initiated edits from
        // PostShotReviewPage) must not pop our queue entries.
        if (m_migration16InFlightVisualizerId.isEmpty()
            || visualizerId != m_migration16InFlightVisualizerId)
            return;

        // Must match the app's settings scope (Settings owns
        // QSettings("DecentEspresso","DE1Qt")); a bare QSettings()
        // resolves to a different, empty store (main.cpp sets
        // applicationName "Decenza"). Same fix applies to every
        // QSettings construction in this file's reconciliation /
        // pending-sync code.
        QSettings s(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
        QJsonArray pending = QJsonDocument::fromJson(
            s.value(QStringLiteral("migration16/pendingVisualizerSync")).toByteArray()).array();
        for (qsizetype i = 0; i < pending.size(); ++i) {
            if (pending[i].toObject().value("visualizerId").toString() == visualizerId) {
                pending.removeAt(i);
                break;
            }
        }
        if (pending.isEmpty())
            s.remove(QStringLiteral("migration16/pendingVisualizerSync"));
        else
            s.setValue(QStringLiteral("migration16/pendingVisualizerSync"),
                       QJsonDocument(pending).toJson(QJsonDocument::Compact));
        m_migration16InFlightVisualizerId.clear();
        dispatchNextPendingVisualizerSync();
    });
    connect(m_visualizer, &VisualizerUploader::uploadFailed, this,
            [this](const QString& /*error*/) {
        // VisualizerUploader::uploadFailed carries no shot-id correlation, so
        // we can't tell whether the failure was ours or someone else's. The
        // safe rule: if a migration16 PATCH is in flight, assume it failed
        // (the same upload session can't error AND succeed on the network
        // layer), leave the entry in the pending list, and abort the drain.
        // A spurious abort just means the queue picks up on next boot.
        if (m_migration16InFlightVisualizerId.isEmpty()) return;
        m_migration16InFlightVisualizerId.clear();
    });

    // Create shot importer for importing .shot files from DE1 app
    m_shotImporter = new ShotImporter(m_shotHistory, this);

    // Create profile converter for batch converting DE1 app profiles
    m_profileConverter = new ProfileConverter(this);

    // Create profile importer for importing profiles from DE1 tablet
    m_profileImporter = new ProfileImporter(this, settings, this);

    m_shotComparison = new ShotComparisonModel(this);
    m_shotComparison->setStorage(m_shotHistory);

    // Create debug logger for shot diagnostics
    // It captures all qDebug/qWarning/etc. output during shot extraction
    m_shotDebugLogger = new ShotDebugLogger(this);
    // Create shot server for remote access to shot data
    m_shotServer = new ShotServer(m_shotHistory, m_device, this);
    m_shotServer->setSettings(m_settings);
    m_shotServer->setProfileStorage(m_profileStorage);
    if (m_settings) {
        m_shotServer->setPort(m_settings->network()->shotServerPort());

        // Start server if enabled in settings
        if (m_settings->network()->shotServerEnabled()) {
            m_shotServer->start();
        }

        // React to settings changes
        connect(m_settings->network(), &SettingsNetwork::shotServerEnabledChanged, this, [this]() {
            if (m_settings->network()->shotServerEnabled()) {
                m_shotServer->start();
            } else {
                m_shotServer->stop();
            }
        });
        connect(m_settings->network(), &SettingsNetwork::shotServerPortChanged, this, [this]() {
            bool wasRunning = m_shotServer->isRunning();
            if (wasRunning) {
                m_shotServer->stop();
            }
            m_shotServer->setPort(m_settings->network()->shotServerPort());
            if (wasRunning) {
                m_shotServer->start();
            }
        });
        connect(m_settings->network(), &SettingsNetwork::webSecurityEnabledChanged, this, [this]() {
            bool wasRunning = m_shotServer->isRunning();
            if (wasRunning) {
                m_shotServer->stop();
            }
            if (wasRunning || m_settings->network()->shotServerEnabled()) {
                m_shotServer->start();
            }
        });
    }

    // Set MachineState on ShotServer for home automation API
    m_shotServer->setMachineState(m_machineState);

    // Emit remoteSleepRequested when sleep command received via REST API
    connect(m_shotServer, &ShotServer::sleepRequested, this, &MainController::remoteSleepRequested);
    // Create MQTT client for home automation
    m_mqttClient = new MqttClient(m_device, m_machineState, m_settings, m_settings->mqtt(), this);

    // Pass MainController reference for shot history access
    m_mqttClient->setMainController(this);

    // Emit remoteSleepRequested when sleep command received via MQTT
    connect(m_mqttClient, &MqttClient::commandReceived, this, [this](const QString& command) {
        if (command == "sleep") {
            emit remoteSleepRequested();
        }
    });

    // Handle profile selection via MQTT
    connect(m_mqttClient, &MqttClient::profileSelectRequested, this, [this](const QString& profileName) {
        qDebug() << "MainController: MQTT profile selection requested:" << profileName;
        m_profileManager->loadProfile(profileName);
    });

    // Update MQTT with current profile when it changes
    connect(m_profileManager, &ProfileManager::currentProfileChanged, this, [this]() {
        if (m_mqttClient) {
            m_mqttClient->setCurrentProfile(m_profileManager->currentProfile().title());
            // Settings::currentProfile() stores the filename (set in loadProfile)
            if (m_settings) {
                m_mqttClient->setCurrentProfileFilename(m_settings->app()->currentProfile());
            }
        }
    });

    // Steam on/off commands
    connect(m_mqttClient, &MqttClient::steamOnRequested, this, [this]() {
        startSteamHeating(QStringLiteral("mqtt-steam-on"));
    });
    connect(m_mqttClient, &MqttClient::steamOffRequested, this, [this]() {
        turnOffSteamHeater();
    });

    // Steam settings changes -> republish state
    connect(m_settings->brew(), &SettingsBrew::steamDisabledChanged, m_mqttClient, &MqttClient::onSteamSettingsChanged);
    connect(m_settings->brew(), &SettingsBrew::keepSteamHeaterOnChanged, m_mqttClient, &MqttClient::onSteamSettingsChanged);

    // Auto-connect MQTT if enabled
    if (m_settings && m_settings->mqtt()->mqttEnabled() && !m_settings->mqtt()->mqttBrokerHost().isEmpty()) {
        // Deferred call ensures construction completes first.
        // MQTT connects to an external broker over TCP — no BLE dependency.
        QMetaObject::invokeMethod(this, [this]() {
            if (m_settings->mqtt()->mqttEnabled()) {
                m_mqttClient->connectToBroker();
            }
        }, Qt::QueuedConnection);
    }

    // Initialize location provider and shot reporter for decenza.coffee shot map
    m_locationProvider = new LocationProvider(m_networkManager, this);
    m_shotReporter = new ShotReporter(m_networkManager, m_settings, m_locationProvider, this);

    // Request location update if shot reporting is enabled
    if (m_settings && m_settings->value("shotmap/enabled", false).toBool()) {
        m_locationProvider->requestUpdate();
    }

    // Initialize update checker
    m_updateChecker = new UpdateChecker(m_networkManager, m_settings, this);

    // Initialize DE1 firmware update pipeline. FirmwareAssetCache shares
    // the MainController's QNetworkAccessManager (so proxy/TLS settings
    // apply uniformly). FirmwareUpdater is wired to DE1Device for BLE
    // writes, to MachineState for the precondition gate, and exposes a
    // QProperty for QML.
    m_firmwareAssetCache = new DE1::Firmware::FirmwareAssetCache(this);
    m_firmwareAssetCache->setNetworkManager(m_networkManager);
    if (m_settings) {
        m_firmwareAssetCache->setChannel(m_settings->app()->firmwareNightlyChannel()
            ? DE1::Firmware::FirmwareAssetCache::Channel::Nightly
            : DE1::Firmware::FirmwareAssetCache::Channel::Stable);
        connect(m_settings->app(), &SettingsApp::firmwareNightlyChannelChanged,
                this, [this]() {
            if (!m_firmwareAssetCache) return;
            m_firmwareAssetCache->setChannel(m_settings->app()->firmwareNightlyChannel()
                ? DE1::Firmware::FirmwareAssetCache::Channel::Nightly
                : DE1::Firmware::FirmwareAssetCache::Channel::Stable);
            // Re-check immediately so the UI reflects the new channel's
            // available version without waiting for the weekly poll.
            // dismissLingeringFailure() wipes any stale Failed state from a
            // prior check (e.g. a transient network error in the weekly
            // auto-check) so the "Update failed" error strip doesn't briefly
            // re-render beneath the channel toggle while the new check
            // resolves.
            if (m_firmwareUpdater) {
                m_firmwareUpdater->dismissLingeringFailure();
                m_firmwareUpdater->checkForUpdate();
            }
        });
    }
    m_firmwareUpdater    = new FirmwareUpdater(m_device, m_firmwareAssetCache, this);
    qDebug() << "[firmware] MainController wired FirmwareUpdater"
             << "device=" << (m_device ? "ok" : "null")
             << "device.firmwareBuildNumber=" << (m_device ? m_device->firmwareBuildNumber() : -1);

    m_firmwareUpdater->setInstalledVersionProvider([this]() -> uint32_t {
        if (!m_device) return 0;
        // Simulator: pretend to be on an ancient firmware so both the
        // stable and nightly channels always register as "update available",
        // letting a developer exercise the Firmware page end-to-end without
        // a real DE1 to flash. The simulator never ships a firmware
        // build-number, so this is the only signal the page has anyway.
        if (m_device->simulationMode()) {
            return 1u;
        }
        const int bn = m_device->firmwareBuildNumber();
        return bn > 0 ? static_cast<uint32_t>(bn) : 0;
    });
    m_firmwareUpdater->setPreconditionProvider([this]() -> bool {
        if (!m_machineState) return true;
        using P = MachineState::Phase;
        switch (m_machineState->phase()) {
            case P::Sleep:
            case P::Idle:
            case P::Heating:
            case P::Ready:
                return true;
            default:
                return false;
        }
    });
    // Auto-check cadence: startup (30 s after construction) + weekly
    // thereafter. firmware/lastCheckedAt in QSettings persists the last
    // check so a user who relaunches the app daily doesn't re-check on
    // every launch.
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    const qint64 lastCheckedAt = m_settings
        ? m_settings->value("firmware/lastCheckedAt", 0).toLongLong() : 0;
    const qint64 weekSec = 168LL * 3600LL;
    const qint64 sinceLast = nowSec - lastCheckedAt;
    const int startupDelayMs = (lastCheckedAt > 0 && sinceLast < weekSec)
        ? int(qMin<qint64>((weekSec - sinceLast) * 1000, INT_MAX))
        : 30 * 1000;
    QTimer::singleShot(startupDelayMs, this, [this]() {
        if (m_firmwareUpdater) m_firmwareUpdater->checkForUpdate();
        if (m_settings) m_settings->setValue(
            "firmware/lastCheckedAt", QDateTime::currentSecsSinceEpoch());
    });
    m_firmwareCheckTimer = new QTimer(this);
    m_firmwareCheckTimer->setSingleShot(false);
    m_firmwareCheckTimer->setInterval(int(weekSec * 1000LL));
    connect(m_firmwareCheckTimer, &QTimer::timeout, this, [this]() {
        if (m_firmwareUpdater) m_firmwareUpdater->checkForUpdate();
        if (m_settings) m_settings->setValue(
            "firmware/lastCheckedAt", QDateTime::currentSecsSinceEpoch());
    });
    m_firmwareCheckTimer->start();

    // Create data migration client for importing from other devices
    m_dataMigration = new DataMigrationClient(m_networkManager, this);
    m_dataMigration->setSettings(m_settings);
    m_dataMigration->setProfileStorage(m_profileStorage);
    m_dataMigration->setShotHistoryStorage(m_shotHistory);

    // Connect ProfileManager's profileUploadBlocked to ShotDebugLogger
    connect(m_profileManager, &ProfileManager::profileUploadBlocked, this, [this](const QString& phaseString, const QString& stackTrace) {
        if (m_shotDebugLogger) {
            m_shotDebugLogger->logInfo(QString("BLOCKED uploadCurrentProfile() during %1\n%2")
                .arg(phaseString)
                .arg(stackTrace));
        }
    });
}

void MainController::loadShotWithMetadata(qint64 shotId, double doseOverride) {
    if (!m_shotHistory) {
        qWarning() << "loadShotWithMetadata: No shot history storage";
        emit shotMetadataLoaded(shotId, false);
        return;
    }

    // Load shot record on a background thread to avoid blocking the UI
    const QString dbPath = m_shotHistory->databasePath();
    QPointer<MainController> self(this);

    // NOTE: QPointer is NOT thread-safe — it tracks QObject destruction via the main
    // event loop. The background thread captures `self` by value but MUST NOT dereference
    // it. All dereferences occur inside the QueuedConnection callback, which runs on the
    // main thread where QPointer's tracking is valid.
    QThread* thread = QThread::create([self, dbPath, shotId, doseOverride]() {
        ShotRecord record;
        qint64 matchedBagId = -1;
        if (!withTempDb(dbPath, "load_meta", [&](QSqlDatabase& db) {
            record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            // Resolve the shot's bag (its bag_id link, or an identity match
            // for pre-bag shots) while the connection is open — the apply
            // step must select it BEFORE writing dye fields, or those writes
            // would write through into whatever bag is currently active.
            matchedBagId = CoffeeBagStorage::findBagForShotStatic(
                db, shotId, record.summary.beanBrand, record.summary.beanType);
        })) {
            qWarning() << "loadShotWithMetadata: Failed to open DB for shot" << shotId;
        }

        // Apply metadata on main thread (interacts with QML state and BLE)
        QMetaObject::invokeMethod(qApp, [self, shotId, doseOverride, matchedBagId, record = std::move(record)]() {
            if (self) self->applyLoadedShotMetadata(shotId, record, doseOverride, matchedBagId);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainController::applyLoadedShotMetadata(qint64 shotId, const ShotRecord& shotRecord, double doseOverride,
                                             qint64 matchedBagId) {
    if (shotRecord.summary.id <= 0) {
        qWarning() << "applyLoadedShotMetadata: Shot not found or DB open failed for id:" << shotId;
        emit shotMetadataLoaded(shotId, false);
        return;
    }

    // Load the profile - prefer installed profile, fall back to stored JSON
    QString filename = m_profileManager->findProfileByTitle(shotRecord.summary.profileName);
    qDebug() << "applyLoadedShotMetadata: profileTitle=" << shotRecord.summary.profileName
             << "filename=" << filename;
    if (!filename.isEmpty()) {
        m_profileManager->loadProfile(filename);
    } else if (!shotRecord.profileJson.isEmpty()) {
        m_profileManager->loadProfileFromJson(shotRecord.profileJson);
        // Persist to downloaded folder so the profile is available by name on next startup
        m_profileManager->persistCurrentProfile();
    } else {
        qWarning() << "applyLoadedShotMetadata: No profile data available for shot";
    }

    // Copy metadata to DYE settings
    if (m_settings) {
        // Select the shot's bag FIRST, keeping fields — the setters below
        // write through to the active bag, so the bag must be the right one
        // (or none) before any field is written. The shot's values win over
        // the bag's last-used values; via write-through the bag adopts them.
        m_settings->dye()->setActiveBagKeepFields(matchedBagId > 0 ? static_cast<int>(matchedBagId) : -1);

        m_settings->dye()->setDyeBeanBrand(shotRecord.summary.beanBrand);
        m_settings->dye()->setDyeBeanType(shotRecord.summary.beanType);
        m_settings->dye()->setDyeRoastDate(shotRecord.roastDate);
        m_settings->dye()->setDyeRoastLevel(shotRecord.roastLevel);
        m_settings->dye()->setDyeGrinderBrand(shotRecord.grinderBrand);
        m_settings->dye()->setDyeGrinderModel(shotRecord.grinderModel);
        m_settings->dye()->setDyeGrinderBurrs(shotRecord.grinderBurrs);
        m_settings->dye()->setDyeGrinderSetting(shotRecord.grinderSetting);
        m_settings->dye()->setDyeBarista(shotRecord.barista);
        // Bean Base link follows the shot's snapshot — and clears when the
        // shot was unlinked, so the previous bag's link can't leak onto a
        // guest bean.
        m_settings->dye()->setDyeBeanBaseData(shotRecord.beanBaseJson);
        m_settings->dye()->setDyeBeanBaseId(BeanBaseBlob::canonicalId(shotRecord.beanBaseJson));

        // Restore dose (input parameter, not a result). When loading an auto-favorite,
        // `doseOverride` holds the bucketed dose shown on the card — apply that instead
        // of the shot's raw saved dose so what-you-see is what-gets-loaded. The override
        // is queued to run after ProfileManager::loadProfile's own deferred
        // setDyeBeanWeight(recommendedDose) (also a QueuedConnection), so ours wins.
        double doseToLoad = doseOverride > 0 ? doseOverride : shotRecord.summary.doseWeight;
        if (doseToLoad > 0) {
            QPointer<Settings> settings(m_settings);
            QMetaObject::invokeMethod(this, [settings, doseToLoad]() {
                if (settings) settings->dye()->setDyeBeanWeight(doseToLoad);
            }, Qt::QueuedConnection);
        }
        // Note: Don't copy finalWeight/TDS/EY - those are shot results, not inputs
        // (bag selection happened above, before the field writes)

        // Apply brew overrides from history on top of profile defaults (set by loadProfile)
        // Values > 0 indicate overrides were used (0 means no override or old shot)
        bool hasOverrides = false;
        if (shotRecord.temperatureOverride > 0) {
            m_settings->brew()->setTemperatureOverride(shotRecord.temperatureOverride);
            hasOverrides = true;
        }

        if (shotRecord.targetWeight > 0) {
            m_settings->brew()->setBrewYieldOverride(shotRecord.targetWeight);
            hasOverrides = true;
        } else if (shotRecord.summary.finalWeight > 0 && m_profileManager->currentProfile().targetWeight() <= 0) {
            // Old shots from volume/timer-based profiles were saved with targetWeight=0.
            // Use the actual yield so the user gets a meaningful weight target.
            m_settings->brew()->setBrewYieldOverride(shotRecord.summary.finalWeight);
            hasOverrides = true;
        }

        qDebug() << "Loaded shot metadata - brand:" << shotRecord.summary.beanBrand
                 << "type:" << shotRecord.summary.beanType
                 << "grinder:" << shotRecord.grinderModel << shotRecord.grinderSetting
                 << "matchedBagId:" << matchedBagId
                 << "brewOverrides - temp:" << (shotRecord.temperatureOverride > 0 ? QString::number(shotRecord.temperatureOverride) : "none")
                 << "target:" << (shotRecord.targetWeight > 0 ? QString::number(shotRecord.targetWeight)
                    : (shotRecord.summary.finalWeight > 0 && m_profileManager->currentProfile().targetWeight() <= 0
                       ? QString::number(shotRecord.summary.finalWeight) + " (from actual)"
                       : "none"));

        // Re-upload profile with history overrides applied
        // loadProfile() already uploaded with profile defaults; now we have the actual overrides
        if (hasOverrides) {
            m_profileManager->uploadCurrentProfile();
        }
    }

    // Queue the success signal so it fires after the queued setDyeBeanWeight above
    // (Qt drains queued events FIFO per-thread). Otherwise AutoFavoritesPage would
    // pop to IdlePage and auto-open the brew dialog before the dose setter ran,
    // briefly showing the profile default instead of the bucketed dose.
    QMetaObject::invokeMethod(this, [this, shotId]() {
        emit shotMetadataLoaded(shotId, true);
    }, Qt::QueuedConnection);
}

void MainController::copyToClipboard(const QString& text) {
    auto* cb = QGuiApplication::clipboard();
    if (cb) {
        cb->setText(text, QClipboard::Clipboard);
        qDebug() << "Copied to clipboard:" << text;
    }
}

QString MainController::pasteFromClipboard() const {
    auto* cb = QGuiApplication::clipboard();
    if (!cb) return {};
    QString text = cb->text(QClipboard::Clipboard);
    qDebug() << "Paste from clipboard:" << text;
    return text;
}

void MainController::onShotSettingsReported(double deviceSteamTargetC, int deviceSteamDurationSec,
                                             double deviceHotWaterTempC, int deviceHotWaterVolMl,
                                             double deviceGroupTargetC) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    const double commandedSteam = m_device->commandedSteamTargetC();
    const int commandedDuration = m_device->commandedSteamDurationSec();
    const double commandedHotWaterTemp = m_device->commandedHotWaterTempC();
    const int commandedHotWaterVol = m_device->commandedHotWaterVolMl();
    const double commandedGroup = m_device->commandedGroupTargetC();
    const bool haveCommanded = (commandedSteam >= 0.0 && commandedDuration >= 0
                                && commandedHotWaterTemp >= 0.0 && commandedHotWaterVol >= 0
                                && commandedGroup >= 0.0);

    // Sentinel values emitted by DE1Device on disconnect — skip, there's
    // nothing to compare against.
    if (deviceSteamTargetC < 0.0 || deviceGroupTargetC < 0.0
        || deviceSteamDurationSec < 0 || deviceHotWaterTempC < 0.0
        || deviceHotWaterVolMl < 0) {
        return;
    }

    // Tolerances cover BLE encoding rounding. Temperatures use u8p0 (1°C
    // quantum) or u16p8; 0.5°C absorbs FP noise. Integer fields (duration,
    // volume) must match exactly.
    constexpr double kTempToleranceC = 0.5;

    // Compare reported against COMMANDED — "did the DE1 honor our last
    // write?" This is the authoritative question for #746, and it correctly
    // handles code paths that write values diverging from Settings
    // (startSteamHeating forces heater on regardless of keepSteamHeaterOn,
    // softStopSteam writes a 1s timeout, etc.). Comparing against
    // Settings-derived "expected" would make the drift handler clobber
    // those writes.
    const bool steamDrift = haveCommanded &&
        std::abs(deviceSteamTargetC - commandedSteam) > kTempToleranceC;
    const bool durationDrift = haveCommanded &&
        deviceSteamDurationSec != commandedDuration;
    const bool hotWaterTempDrift = haveCommanded &&
        std::abs(deviceHotWaterTempC - commandedHotWaterTemp) > kTempToleranceC;
    const bool hotWaterVolDrift = haveCommanded &&
        deviceHotWaterVolMl != commandedHotWaterVol;
    const bool groupDrift = haveCommanded &&
        std::abs(deviceGroupTargetC - commandedGroup) > kTempToleranceC;

    // Skip before we've ever written — DE1's initial indication on subscribe
    // reflects its power-on state, not ours, and racing against that would
    // log a bogus drift on every connect.
    if (!haveCommanded) {
        qDebug().noquote() << QString(
            "[SettingsDrift] pre-commanded report ignored: "
            "reported(steam=%1C dur=%2s hw=%3C vol=%4ml group=%5C) — waiting for first write")
            .arg(deviceSteamTargetC, 0, 'f', 1)
            .arg(deviceSteamDurationSec)
            .arg(deviceHotWaterTempC, 0, 'f', 1)
            .arg(deviceHotWaterVolMl)
            .arg(deviceGroupTargetC, 0, 'f', 2);
        return;
    }

    if (!steamDrift && !durationDrift && !hotWaterTempDrift && !hotWaterVolDrift && !groupDrift) {
        // DE1 stored what we sent. Reset retry bookkeeping.
        if (m_shotSettingsDriftResendCount > 0) {
            qDebug().noquote() << QString(
                "[SettingsDrift] resolved after %1 resend(s) — DE1 stored "
                "steam=%2C dur=%3s hw=%4C vol=%5ml group=%6C")
                .arg(m_shotSettingsDriftResendCount)
                .arg(deviceSteamTargetC, 0, 'f', 1)
                .arg(deviceSteamDurationSec)
                .arg(deviceHotWaterTempC, 0, 'f', 1)
                .arg(deviceHotWaterVolMl)
                .arg(deviceGroupTargetC, 0, 'f', 2);
            m_shotSettingsDriftResendCount = 0;
        }
        m_shotSettingsResendInFlight = false;
        return;
    }

    // Drift detected. The received indication is the post-write state
    // (read is queued after the write), so any mismatch is real drift.

    // Classify for the log so we can scan `grep SettingsDrift` in bug
    // reports and immediately see what happened.
    QString summary;
    if (steamDrift) {
        if (commandedSteam == 0.0 && deviceSteamTargetC > 0.0) {
            summary = QStringLiteral("steam heater ON at %1C but we commanded OFF")
                          .arg(deviceSteamTargetC, 0, 'f', 0);
        } else if (commandedSteam > 0.0 && deviceSteamTargetC == 0.0) {
            summary = QStringLiteral("steam heater OFF but we commanded %1C")
                          .arg(commandedSteam, 0, 'f', 0);
        } else {
            summary = QStringLiteral("steam target %1C but we commanded %2C")
                          .arg(deviceSteamTargetC, 0, 'f', 0)
                          .arg(commandedSteam, 0, 'f', 0);
        }
    }
    if (durationDrift) {
        QString note = QStringLiteral("steam duration %1s but we commanded %2s")
                           .arg(deviceSteamDurationSec).arg(commandedDuration);
        summary = summary.isEmpty() ? note : summary + QStringLiteral("; ") + note;
    }
    if (hotWaterTempDrift) {
        QString note = QStringLiteral("hot water temp %1C but we commanded %2C")
                           .arg(deviceHotWaterTempC, 0, 'f', 1)
                           .arg(commandedHotWaterTemp, 0, 'f', 1);
        summary = summary.isEmpty() ? note : summary + QStringLiteral("; ") + note;
    }
    if (hotWaterVolDrift) {
        QString note = QStringLiteral("hot water vol %1ml but we commanded %2ml")
                           .arg(deviceHotWaterVolMl).arg(commandedHotWaterVol);
        summary = summary.isEmpty() ? note : summary + QStringLiteral("; ") + note;
    }
    if (groupDrift) {
        QString note = QStringLiteral("group target %1C but we commanded %2C")
                           .arg(deviceGroupTargetC, 0, 'f', 2)
                           .arg(commandedGroup, 0, 'f', 2);
        summary = summary.isEmpty() ? note : summary + QStringLiteral("; ") + note;
    }

    qWarning().noquote() << QString(
        "[SettingsDrift] DE1-dropped-write: %1 | "
        "reported(steam=%2C dur=%3s hw=%4C vol=%5ml group=%6C) "
        "commanded(steam=%7C dur=%8s hw=%9C vol=%10ml group=%11C)")
        .arg(summary)
        .arg(deviceSteamTargetC, 0, 'f', 1)
        .arg(deviceSteamDurationSec)
        .arg(deviceHotWaterTempC, 0, 'f', 1)
        .arg(deviceHotWaterVolMl)
        .arg(deviceGroupTargetC, 0, 'f', 2)
        .arg(commandedSteam, 0, 'f', 1)
        .arg(commandedDuration)
        .arg(commandedHotWaterTemp, 0, 'f', 1)
        .arg(commandedHotWaterVol)
        .arg(commandedGroup, 0, 'f', 2);

    // If a resend is already in flight (we sent one and haven't yet received
    // its indication), wait for that to resolve before firing another. This
    // is the event-based replacement for a 2s wall-clock rate limit.
    if (m_shotSettingsResendInFlight) {
        qDebug() << "[SettingsDrift] resend already in flight — waiting for its indication";
        return;
    }

    constexpr int kMaxResendAttempts = 3;
    if (m_shotSettingsDriftResendCount >= kMaxResendAttempts) {
        qWarning().noquote() << QString(
            "[SettingsDrift] giving up after %1 resend attempts — DE1 not honoring ShotSettings")
            .arg(m_shotSettingsDriftResendCount);
        return;
    }

    // Re-check the connection right before committing to the resend — signals
    // emitted above could have flipped state, and we don't want to burn a
    // retry slot on a write that will no-op inside the transport.
    if (!m_device->isConnected()) {
        qDebug() << "[SettingsDrift] device disconnected during drift handling — skipping resend";
        return;
    }

    m_shotSettingsDriftResendCount++;
    m_shotSettingsResendInFlight = true;
    qWarning().noquote() << QString(
        "[SettingsDrift] resending last ShotSettings payload (attempt %1 of %2)")
        .arg(m_shotSettingsDriftResendCount).arg(kMaxResendAttempts);
    // Re-assert exactly what we last commanded — do NOT re-derive from
    // Settings via sendMachineSettings(). Some code paths (startSteamHeating,
    // softStopSteam, setSteamTimeoutImmediate) deliberately write values that
    // diverge from Settings, and re-deriving would clobber them.
    m_device->resendLastShotSettings();
}

void MainController::sendMachineSettings(const QString& reason) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Determine steam temperature to send:
    // - If the currently selected steam preset is an "Off" pill: send 0
    //   (matches de1app's persistent steam_disabled behavior — target 0 is
    //   pushed so the DE1 firmware can still honor GHC presses with no
    //   steam produced.)
    // - If steam is disabled (session flag): send 0
    // - If keepSteamHeaterOn is false: send 0 (user doesn't want heater on)
    // - Otherwise: send configured temperature
    //
    // Rationalization: if the user has selected a non-Off preset, the session
    // flag is stale (set by a previous turnOffSteamHeater call on a prior Off
    // selection). The preset is the authoritative user intent — clear the
    // flag so downstream code sees a consistent state. Without this, IdlePage
    // / SteamItem applySteamSettings paths (which don't call startSteamHeating)
    // would leave the flag set and the heater off until the user opened
    // SteamPage.
    const QVariantMap currentPitcher = m_settings->brew()->getSteamPitcherPreset(m_settings->brew()->selectedSteamPitcher());
    const bool currentPitcherDisabled = currentPitcher.value("disabled").toBool();
    if (!currentPitcherDisabled && m_settings->brew()->steamDisabled()) {
        m_settings->brew()->setSteamDisabled(false);
    }
    double steamTemp;
    if (currentPitcherDisabled || m_settings->brew()->steamDisabled()) {
        steamTemp = 0.0;
    } else if (!m_settings->brew()->keepSteamHeaterOn()) {
        steamTemp = 0.0;
    } else {
        steamTemp = m_settings->brew()->steamTemperature();
    }

    double groupTemp = getGroupTemperature();
    qDebug() << "sendMachineSettings: steam=" << steamTemp << "°C, groupTemp=" << groupTemp << "°C";

    // 1. ShotSettings (single write with all temperatures).
    // DE1Device::setShotSettings() internally records the commanded values
    // so onShotSettingsReported() can compare reported against commanded.
    m_device->setShotSettings(
        steamTemp,
        m_settings->brew()->steamTimeout(),
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        reason.isEmpty() ? QStringLiteral("sendMachineSettings") : reason
    );

    const QString mmrReason = reason.isEmpty()
        ? QStringLiteral("sendMachineSettings") : reason;

    // 2. Steam flow MMR
    m_device->writeMMR(0x803828, m_settings->brew()->steamFlow(), mmrReason);

    // 3. Flush flow MMR (value × 10)
    int flowValue = static_cast<int>(m_settings->brew()->flushFlow() * 10);
    m_device->writeMMR(0x803840, flowValue, mmrReason);

    // 4. Flush timeout MMR (value × 10)
    int secondsValue = static_cast<int>(m_settings->brew()->flushSeconds() * 10);
    m_device->writeMMR(0x803848, secondsValue, mmrReason);
}

void MainController::applySteamSettings() {
    sendMachineSettings(QStringLiteral("applySteamSettings"));
}

void MainController::applyHotWaterSettings() {
    sendMachineSettings(QStringLiteral("applyHotWaterSettings"));
    if (m_device && m_device->isConnected())
        m_device->writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE,
                           m_settings->hardware()->hotWaterFlowRate(),
                           QStringLiteral("applyHotWaterSettings"));
}

void MainController::applyFlushSettings() {
    sendMachineSettings(QStringLiteral("applyFlushSettings"));
}

void MainController::applyAllSettings() {
    // Fresh connection — reset ShotSettings drift bookkeeping so a prior
    // session's exhausted retry budget doesn't permanently disable auto-heal.
    m_shotSettingsDriftResendCount = 0;
    m_shotSettingsResendInFlight = false;

    // 1. Upload current profile (espresso)
    if (m_profileManager->currentProfile().mode() == Profile::Mode::FrameBased) {
        m_profileManager->uploadCurrentProfile();
    }

    // 2. Apply steam/hot water/flush settings (unified)
    sendMachineSettings(QStringLiteral("applyAllSettings"));

    // 3. Apply water refill level
    applyWaterRefillLevel();

    // 4. Apply refill kit override
    applyRefillKitOverride();

    // 5. Apply flow calibration multiplier
    applyFlowCalibration();

    // Note: heater tweaks are NOT sent here — matching de1app's save_settings_to_de1()
    // which does not call set_heater_tweaks(). They are sent on connection in
    // DE1Device::sendInitialSettings() and on user calibration changes via signal/slot.
}

void MainController::applyWaterRefillLevel() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // When the refill kit is active (forced on, or auto-detected), the kit keeps the
    // reservoir topped up — so suppress the firmware-side "Refill" phase by sending the
    // slider's floor value (3mm). If the reservoir does run nearly empty (e.g. kit
    // failure), the firmware will still raise Refill once raw level reaches 3mm. The
    // user's stored waterRefillPoint is preserved untouched and resumes whenever the
    // kit is forced off.
    //
    // In auto-detect mode (override == 2), refillKitDetected starts at -1 until the
    // MMR read completes, so the first apply on connect uses the user's stored value;
    // a follow-up write fires from refillKitDetectedChanged once detection resolves.
    // Don't try to gate this on `detected != -1` — that would break the Force-Off path.
    const int kitOverride = m_settings->app()->refillKitOverride();
    const bool kitActive = (kitOverride == 1) ||
                           (kitOverride == 2 && m_device->refillKitDetected() == 1);
    const int effective = kitActive ? 3 : m_settings->app()->waterRefillPoint();

    m_device->setWaterRefillLevel(effective);
}

void MainController::applyRefillKitOverride() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Values match de1app: 0=force off, 1=force on, 2=auto-detect
    int kitOverride = m_settings->app()->refillKitOverride();
    m_device->setRefillKitPresent(kitOverride);
}

void MainController::applyFlowCalibration() {
    m_profileManager->applyFlowCalibration();
}

void MainController::computeAutoFlowCalibration() {
    if (!m_settings || !m_shotDataModel) {
        qWarning() << "Auto flow cal: skipped due to null pointer"
                   << "(settings:" << (m_settings != nullptr)
                   << "shotDataModel:" << (m_shotDataModel != nullptr) << ")";
        return;
    }
    if (!m_settings->calibration()->autoFlowCalibration()) {
        qDebug() << "Auto flow cal: disabled in settings";
        return;
    }

    if (m_profileManager->baseProfileName().isEmpty()) {
        qDebug() << "Auto flow cal: skipped (no profile name set)";
        return;
    }

    // Require a physical BLE scale (not FlowScale). FlowScale derives weight from
    // the DE1's own flow sensor, so comparing machine flow against FlowScale weight
    // would be circular and produce meaningless calibration values.
    bool hasPhysicalScale = m_bleManager && m_bleManager->scaleDevice()
                            && m_bleManager->scaleDevice()->isConnected()
                            && m_bleManager->scaleDevice()->type() != "flow";
    if (!hasPhysicalScale) {
        qDebug() << "Auto flow cal: skipped (no physical BLE scale connected)";
        return;
    }

    const auto& weightFlowData = m_shotDataModel->weightFlowRateData();
    if (weightFlowData.isEmpty()) {
        qDebug() << "Auto flow cal: skipped (no scale weight data)";
        return;
    }

    // Reject shots where settled weight dropped significantly below the weight at SAW stop.
    // This indicates stream impact force was inflating scale readings during extraction,
    // which would produce an unreliable (too high) calibration multiplier.
    double weightAtStop = m_shotDataModel->weightAtStop();
    double finalWeight = m_shotDataModel->finalWeight();
    if (weightAtStop > 5.0 && finalWeight > 0 && finalWeight < weightAtStop - 3.0) {
        qDebug() << "Auto flow cal: skipped (weight dropped after stop:"
                 << weightAtStop << "g ->" << finalWeight << "g,"
                 << "delta:" << (weightAtStop - finalWeight) << "g — likely stream force artifact)";
        return;
    }

    const auto& flowData = m_shotDataModel->flowData();
    const auto& pressureData = m_shotDataModel->pressureData();
    if (flowData.size() < 10 || pressureData.size() < 10) {
        qDebug() << "Auto flow cal: skipped (insufficient data - flow:"
                 << flowData.size() << "pressure:" << pressureData.size() << ")";
        return;
    }

    // Algorithm thresholds
    constexpr double kMaxPressureChangeRate = 0.5;   // bar/sec - max dP/dt for "stable" pressure
    constexpr double kMinPressure = 1.5;             // bar - rejects empty-portafilter shots
    constexpr double kMinWeightFlow = 0.5;           // g/s - excludes dripping/dead time
    constexpr double kMinMachineFlow = 0.1;          // ml/s - excludes stalled flow
    constexpr double kMaxScaleDataGap = 1.0;         // seconds - max distance to nearest weight flow point
    constexpr double kMinWindowDuration = 1.5;       // seconds — shorter profiles (e.g. Adaptive v2) have brief steady phases
    constexpr int    kMinWindowSamples = 7;          // ~1.5s at 5Hz pressure sampling
    constexpr double kWaterDensity93C = 0.963;       // g/ml - density correction for water at ~93°C
    constexpr double kCalibrationMin = 0.5;          // sanity lower bound
    // Sanity upper bound. Keeps auto-cal ~10% below the firmware-side cap so the algorithm
    // has headroom before hitting the hard firmware limit:
    //   - Pre-v1337 firmware: 1.8 (firmware cap 2.0 × 0.9)
    //   - v1337+ firmware:    2.7 (firmware cap 3.0 × 0.9, newer pump hardware)
    // Values above the old 1.8 ceiling are legitimate on newer firmware but worth flagging
    // to the user; on older firmware they almost always indicate scale artefacts.
    const int kFirmwareCapBumped = 1337;
    const int fwBuild = m_device ? m_device->firmwareBuildNumber() : 0;
    const double kCalibrationMax = (fwBuild >= kFirmwareCapBumped) ? 2.7 : 1.8;
    constexpr double kChangeThreshold = 0.03;        // 3% relative change required to update
    constexpr double kMaxSampleRatio = 2.5;          // per-sample machine/weight ratio — break window on extreme outliers
    constexpr double kMinSampleRatio = 0.4;          // (generous bounds: window-level check is tighter)
    constexpr double kMaxWindowRatio = 1.35;         // window-mean machine/weight ratio — reject if scale data is suspect
    constexpr double kMinWindowRatio = 0.75;         // (de1app GFC users get ~0.9-1.1 ratios on good data)
    constexpr double kMinWindowStartTime = 10.0;     // seconds — skip early extraction where LSLR weight flow
                                                     // lags behind actual flow, producing inflated ratios

    // 3-sample centered moving average on pressure for dpdt computation.
    // The DE1's PID causes rapid small pressure corrections (~0.1-0.2 bar per sample)
    // that exceed the dpdt threshold on flow profiles, producing artificially short
    // steady windows. Smoothing filters this PID jitter while preserving genuine
    // pressure transitions (frame changes, preinfusion→pour).
    QVector<double> smoothedPressure(pressureData.size());
    for (qsizetype i = 0; i < pressureData.size(); ++i) {
        if (i == 0 || i == pressureData.size() - 1) {
            smoothedPressure[i] = pressureData[i].y();
        } else {
            smoothedPressure[i] = (pressureData[i - 1].y()
                                   + pressureData[i].y()
                                   + pressureData[i + 1].y()) / 3.0;
        }
    }

    // Find the best steady-pour window: stable pressure above minimum + meaningful weight flow.
    // We track the best (longest) qualifying window found across the entire shot.
    double bestStart = -1, bestEnd = -1;
    double bestSumMF = 0, bestSumWF = 0;
    qsizetype bestCount = 0;

    double winStart = -1;
    double winSumMF = 0, winSumWF = 0;
    qsizetype winCount = 0;
    double winLastT = -1;

    // Finish the current window: save as best if longest, then reset for next window
    auto finishWindow = [&]() {
        if (winStart >= 0 && (winLastT - winStart) > (bestEnd - bestStart)) {
            bestStart = winStart;
            bestEnd = winLastT;
            bestSumMF = winSumMF;
            bestSumWF = winSumWF;
            bestCount = winCount;
        }
        winStart = -1;
        winCount = 0;
        winSumMF = 0;
        winSumWF = 0;
    };

    // Cursors for nearest-point/interpolation search (both arrays are time-sorted)
    qsizetype wfCursor = 0;
    qsizetype mfCursor = 1;
    int mfMissCount = 0;  // Tracks flow interpolation misses for diagnostics

    for (qsizetype i = 1; i < pressureData.size(); ++i) {
        double dt = pressureData[i].x() - pressureData[i - 1].x();
        if (dt <= 0) continue;
        // Use smoothed pressure for dpdt to filter PID jitter
        double dpdt = qAbs(smoothedPressure[i] - smoothedPressure[i - 1]) / dt;
        // Use original pressure for minimum pressure check (smoothing could mask real drops)
        double pressure = pressureData[i].y();
        double t = pressureData[i].x();

        // Skip early extraction where LSLR weight flow hasn't converged yet.
        // The rolling regression lags behind actual flow for the first ~10-12s,
        // producing artificially low weight flow and inflated machine/weight ratios.
        if (t < kMinWindowStartTime) {
            continue;
        }

        // Require stable pressure AND minimum pressure.
        // The minimum pressure rejects empty-portafilter / no-coffee shots where
        // water flows freely through the basket with near-zero back-pressure.
        if (dpdt > kMaxPressureChangeRate || pressure < kMinPressure) {
            finishWindow();
            continue;
        }

        // Find weight flow at this time (nearest point, using cursor since t increases monotonically)
        double wf = 0;
        double nearestDist = 1e9;
        for (qsizetype k = wfCursor; k < weightFlowData.size(); ++k) {
            double dist = qAbs(weightFlowData[k].x() - t);
            if (dist < nearestDist) {
                nearestDist = dist;
                wf = weightFlowData[k].y();
                wfCursor = k;
            } else {
                break;  // Past the nearest point, distances only increase from here
            }
        }

        if (nearestDist > kMaxScaleDataGap || wf < kMinWeightFlow) {
            finishWindow();
            continue;
        }

        // Find machine flow at this time (linear interpolation, using cursor)
        double mf = 0;
        for (qsizetype j = mfCursor; j < flowData.size(); ++j) {
            if (flowData[j].x() >= t) {
                double t0 = flowData[j - 1].x();
                double t1 = flowData[j].x();
                double dt2 = t1 - t0;
                if (dt2 > 0) {
                    double frac = (t - t0) / dt2;
                    mf = flowData[j - 1].y() + frac * (flowData[j].y() - flowData[j - 1].y());
                } else {
                    mf = flowData[j].y();
                }
                mfCursor = j;
                break;
            }
        }

        if (mf < kMinMachineFlow) {
            if (mf == 0.0) mfMissCount++;  // Interpolation produced no match
            finishWindow();
            continue;
        }

        // Per-sample ratio guard: reject samples where machine/weight flow diverge
        // wildly, which indicates scale data hasn't caught up (smoothing delay) or
        // weight flow is from a stale/interpolated reading. Uses generous bounds
        // since individual samples are noisy; the tighter window-level check below
        // catches systematic issues.
        double sampleRatio = mf / wf;
        if (sampleRatio > kMaxSampleRatio || sampleRatio < kMinSampleRatio) {
            finishWindow();
            continue;
        }

        // Extend or start window
        if (winStart < 0) {
            winStart = t;
        }
        winLastT = t;
        winSumMF += mf;
        winSumWF += wf;
        winCount++;
    }

    // Check the final window
    finishWindow();

    double windowDuration = bestEnd - bestStart;
    if (windowDuration < kMinWindowDuration || bestCount < kMinWindowSamples) {
        qDebug() << "Auto flow cal: no qualifying steady window found"
                 << "(duration:" << windowDuration << "samples:" << bestCount
                 << "flowInterpolationMisses:" << mfMissCount << ")";
        return;
    }

    double meanMachineFlow = bestSumMF / bestCount;
    double meanWeightFlow = bestSumWF / bestCount;
    double windowRatio = meanMachineFlow / meanWeightFlow;

    qDebug() << "Auto flow cal: steady window found"
             << "t=" << bestStart << "-" << bestEnd << "(" << windowDuration << "s,"
             << bestCount << "samples)"
             << "meanMachineFlow=" << meanMachineFlow
             << "meanWeightFlow=" << meanWeightFlow
             << "ratio=" << windowRatio
             << "currentFactor=" << m_settings->calibration()->effectiveFlowCalibration(m_profileManager->baseProfileName());

    // Guard against division by zero. Should be impossible since every sample
    // in the window passed the kMinWeightFlow (0.5 g/s) check.
    if (meanWeightFlow < 0.001) {
        qWarning() << "Auto flow cal: meanWeightFlow unexpectedly low ("
                   << meanWeightFlow << ") after qualifying window";
        return;
    }

    // Classify the pump-control mode active during the steady window.
    // For flow frames, the DE1's PID locks reported flow to the target
    // regardless of the calibration factor, which creates a feedback loop
    // if we use reported flow in the formula (factor drifts down over time).
    // For pressure frames, reported flow IS the sensor reading and is the
    // right anchor. Hybrid profiles (e.g. ASL9-3 — pressure declines + a
    // flow-controlled tail) need window-level classification: the steady
    // window almost always lands in the pressure declines, so anchoring to
    // the tail's flow target produces false rejections and spurious
    // multiplier jumps.
    //
    // classifyAutoFlowCalWindow() uses the shot's PhaseMarker stream to
    // determine which frames the window actually touched. If no markers are
    // available (very short shots, legacy data), it reports fallback and we
    // reuse the old profile-level scan so calibration still runs.
    const auto& steps = m_profileManager->currentProfile().steps();
    QList<FrameTransition> transitions;
    {
        const auto& markers = m_shotDataModel->phaseMarkersList();
        transitions.reserve(markers.size());
        for (const auto& m : markers) {
            transitions.append({m.time, m.frameNumber});
        }
    }
    AutoFlowCalClassification cls = classifyAutoFlowCalWindow(
        steps, transitions, bestStart, bestEnd, meanMachineFlow);

    if (cls.mixedMode) {
        qDebug() << "Auto flow cal: window spans mixed flow/pressure frames"
                 << "[" << cls.firstFrameInWindow << ".." << cls.lastFrameInWindow << "]"
                 << "— skipping (ambiguous target)";
        return;
    }

    double profileTargetFlow = 0;
    bool isFlowProfile = false;

    if (cls.fallbackToProfileScan) {
        // No phase markers — fall back to the historical profile-level scan.
        // Skips preinfusion frames because those are almost always flow-
        // controlled even on pressure profiles.
        int preinfuseCount = m_profileManager->currentProfile().preinfuseFrameCount();
        int flowFrameCount = 0;
        for (qsizetype i = preinfuseCount; i < steps.size(); ++i) {
            if (steps[i].isFlowControl() && steps[i].flow > 0.1)
                flowFrameCount++;
        }
        if (flowFrameCount > 0) {
            isFlowProfile = true;
            double bestDist = 1e9;
            for (qsizetype i = preinfuseCount; i < steps.size(); ++i) {
                const auto& frame = steps[i];
                if (frame.isFlowControl() && frame.flow > 0.1) {
                    double dist = qAbs(frame.flow - meanMachineFlow);
                    if (dist < bestDist) {
                        bestDist = dist;
                        profileTargetFlow = frame.flow;
                    }
                }
            }
        }
        qDebug() << "Auto flow cal: no phase markers — profile-level scan"
                 << "mode:" << (isFlowProfile ? "flow" : "pressure");
    } else {
        isFlowProfile = cls.isFlowProfile;
        profileTargetFlow = cls.targetFlow;
        qDebug() << "Auto flow cal: window mode="
                 << (isFlowProfile ? "flow" : "pressure")
                 << "frames=[" << cls.firstFrameInWindow
                 << ".." << cls.lastFrameInWindow << "]"
                 << (isFlowProfile ? QString("target=%1 ml/s").arg(profileTargetFlow)
                                   : QString());
    }

    // Window-level ratio sanity check. For flow profiles, compare weight flow
    // against the profile's known target flow (density-adjusted) instead of
    // the machine's reported flow (which is PID-locked to target and useless
    // for ratio checking). For pressure profiles, compare machine vs weight flow.
    // Reject windows where the ratio is outside [0.75, 1.35] — indicates
    // channeling, scale issues, or other extraction anomalies.
    if (isFlowProfile) {
        double flowProfileRatio = (profileTargetFlow * kWaterDensity93C) / meanWeightFlow;
        if (flowProfileRatio > kMaxWindowRatio || flowProfileRatio < kMinWindowRatio) {
            qDebug() << "Auto flow cal: flow profile ratio" << flowProfileRatio
                     << "(target" << profileTargetFlow << "vs weight" << meanWeightFlow << ")"
                     << "outside bounds [" << kMinWindowRatio << "," << kMaxWindowRatio << "]"
                     << "- skipping (extraction anomaly)";
            return;
        }
    } else if (windowRatio > kMaxWindowRatio || windowRatio < kMinWindowRatio) {
        qDebug() << "Auto flow cal: window ratio" << windowRatio
                 << "outside bounds [" << kMinWindowRatio << "," << kMaxWindowRatio << "]"
                 << "- skipping (scale data suspect)";
        return;
    }

    double currentEffective = m_settings->calibration()->effectiveFlowCalibration(m_profileManager->baseProfileName());
    double ideal;
    if (isFlowProfile) {
        // Flow profile: use the profile's target flow (independent of calibration).
        // ideal = weightFlow / (targetFlow * density)
        // This has no dependency on the current calibration factor, preventing
        // the feedback loop where lowering the factor → less pumping → lower
        // weight flow → factor keeps drifting down.
        ideal = meanWeightFlow / (profileTargetFlow * kWaterDensity93C);
        qDebug() << "Auto flow cal: flow profile — using target flow"
                 << profileTargetFlow << "ml/s (reported:" << meanMachineFlow << ")";
    } else {
        // Pressure profile: the machine doesn't control flow, so reported flow
        // reflects actual sensor readings (already multiplied by the calibration
        // factor). Divide out the current factor to get raw sensor flow.
        // ideal = currentFactor * weightFlow / (reportedFlow * density)
        ideal = currentEffective * meanWeightFlow / (meanMachineFlow * kWaterDensity93C);
    }

    if (!std::isfinite(ideal)) {
        qWarning() << "Auto flow cal: computed non-finite value" << ideal
                   << "(meanMachineFlow:" << meanMachineFlow
                   << "meanWeightFlow:" << meanWeightFlow << ")";
        return;
    }

    ideal = qBound(kCalibrationMin, ideal, kCalibrationMax);

    // On v1337+ firmware, legitimate multipliers can exceed the classic 1.8 ceiling
    // (better pumps → higher genuine ratios). Warn so telemetry / user-visible UI can
    // flag shots where the computed value looks unusually high — helps catch scale bias
    // before it walks the calibration to absurd values.
    constexpr double kClassicCeiling = 1.8;
    if (ideal > kClassicCeiling) {
        qWarning() << "Auto flow cal: computed multiplier" << ideal
                   << "exceeds classic ceiling" << kClassicCeiling
                   << "— verify scale accuracy (firmware build:" << fwBuild << ")";
    }

    // Batched median accumulator: collect ideals across multiple shots at a constant C,
    // then update C using the batch median. This prevents the feedback loop where each
    // C update changes pump behavior, which changes puck dynamics, which changes the next
    // ideal — producing oscillation instead of convergence. The median also provides
    // natural outlier rejection (runaway shots, channeling anomalies).
    constexpr qsizetype kBatchSize = 5;
    constexpr double kBatchEmaAlpha = 0.5;  // Higher alpha is safe because median of N shots is more reliable

    QString profileName = m_profileManager->baseProfileName();
    m_settings->calibration()->appendFlowCalPendingIdeal(profileName, ideal);
    QVector<double> pending = m_settings->calibration()->flowCalPendingIdeals(profileName);

    qDebug() << "Auto flow cal: accumulated ideal" << ideal
             << "for" << profileName << "(" << pending.size() << "/" << kBatchSize << ")"
             << "window:" << windowDuration << "s," << bestCount << "samples"
             << "mode:" << (isFlowProfile ? "flow" : "pressure");

    if (pending.size() < kBatchSize) {
        return;  // Keep accumulating — don't update C yet
    }

    // Batch complete — compute median
    std::sort(pending.begin(), pending.end());
    qsizetype n = pending.size();
    double median = (n % 2 == 0)
        ? (pending[n / 2 - 1] + pending[n / 2]) / 2.0
        : pending[n / 2];

    // Clear the batch now that we've consumed it
    m_settings->calibration()->clearFlowCalPendingIdeals(profileName);

    double alpha = kBatchEmaAlpha;

    // On first calibration for this profile, use median directly (no history to blend with)
    double computed = m_settings->calibration()->hasProfileFlowCalibration(profileName)
        ? alpha * median + (1.0 - alpha) * currentEffective
        : median;

    // Re-clamp after EMA. When the user has manually set the global multiplier above
    // kCalibrationMax (e.g. 3.0 via the manual UI vs. kCalibrationMax=2.7), currentEffective
    // falls back to that global for profiles without a per-profile value, and EMA can then
    // land outside [kCalibrationMin, kCalibrationMax]. Without this, setProfileFlowCalibration
    // would silently reject the write and auto-cal would stop converging for that profile.
    computed = qBound(kCalibrationMin, computed, kCalibrationMax);

    // Only update if meaningfully different (> 3% change)
    if (currentEffective > 0.01 && qAbs(computed - currentEffective) / currentEffective < kChangeThreshold) {
        qDebug() << "Auto flow cal: batch median" << median << "≈ current" << currentEffective
                 << "(computed" << computed << "< 3% change, skipping)";
        return;
    }

    double oldValue = currentEffective;
    if (!m_settings->calibration()->setProfileFlowCalibration(profileName, computed)) {
        qWarning() << "Auto flow cal: computed value" << computed
                   << "was rejected by settings for" << profileName;
        return;
    }
    applyFlowCalibration();

    qDebug() << "Auto flow cal: updated" << profileName
             << "from" << oldValue << "to" << computed
             << "(batch median:" << median << "from" << n << "ideals"
             << "alpha:" << alpha
             << "mode:" << (isFlowProfile ? "flow" : "pressure") << ")";

    emit flowCalibrationAutoUpdated(m_profileManager->currentProfile().title(), oldValue, computed);

    // Update global to median of espresso per-profile values (helps new profiles converge faster)
    updateGlobalFromPerProfileMedian();
}

void MainController::updateGlobalFromPerProfileMedian() {
    QJsonObject map = m_settings->calibration()->allProfileFlowCalibrations();

    // Collect multipliers from espresso profiles only
    QVector<double> values;
    values.reserve(map.size());
    for (auto it = map.begin(); it != map.end(); ++it) {
        auto profileIt = std::find_if(m_profileManager->allProfiles().begin(), m_profileManager->allProfiles().end(),
            [&](const ProfileInfo& p) { return p.filename == it.key(); });
        if (profileIt != m_profileManager->allProfiles().end() && profileIt->beverageType == "espresso") {
            values.append(it.value().toDouble());
        }
    }

    if (values.isEmpty()) return;

    std::sort(values.begin(), values.end());

    // Remove outliers using IQR fence method (1.5x IQR from Q1/Q3)
    if (values.size() >= 4) {
        qsizetype n = values.size();
        double q1 = values[n / 4];
        double q3 = values[3 * n / 4];
        double iqr = q3 - q1;
        double lower = q1 - 1.5 * iqr;
        double upper = q3 + 1.5 * iqr;
        QVector<double> filtered;
        for (double v : values) {
            if (v >= lower && v <= upper) filtered.append(v);
        }
        if (filtered.size() >= 2) {
            values = filtered;
        }
        // If filtering leaves <2, keep unfiltered values (IQR unreliable with few data points)
    }

    qsizetype n = values.size();
    double median = (n % 2 == 0)
        ? (values[n/2 - 1] + values[n/2]) / 2.0
        : values[n/2];

    // Only update if meaningfully different (>2% change)
    double current = m_settings->calibration()->flowCalibrationMultiplier();
    if (current > 0.01 && qAbs(median - current) / current < 0.02) return;

    m_settings->calibration()->setFlowCalibrationMultiplier(median);
    qDebug() << "Auto flow cal: updated global to espresso median" << median
             << "from" << values.size() << "espresso profiles"
             << "(" << map.size() << "total in map)";
}

void MainController::applyHeaterTweaks() {
    if (!m_device || !m_device->isConnected() || !m_settings) {
        qDebug() << "applyHeaterTweaks: skipped (device connected:"
                 << (m_device && m_device->isConnected()) << ")";
        return;
    }

    const QString reason = QStringLiteral("applyHeaterTweaks");
    m_device->writeMMR(DE1::MMR::PHASE1_FLOW_RATE, m_settings->hardware()->heaterWarmupFlow(), reason);
    m_device->writeMMR(DE1::MMR::PHASE2_FLOW_RATE, m_settings->hardware()->heaterTestFlow(), reason);
    m_device->writeMMR(DE1::MMR::HOT_WATER_IDLE_TEMP, m_settings->hardware()->heaterIdleTemp(), reason);
    m_device->writeMMR(DE1::MMR::ESPRESSO_WARMUP_TIMEOUT, m_settings->hardware()->heaterWarmupTimeout(), reason);
    m_device->writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, m_settings->hardware()->hotWaterFlowRate(), reason);
    m_device->writeMMR(DE1::MMR::STEAM_TWO_TAP_STOP, m_settings->hardware()->steamTwoTapStop() ? 1 : 0, reason);
    m_device->writeMMR(DE1::MMR::FAN_THRESHOLD, m_settings->hardware()->fanThreshold(), reason);
}

double MainController::getGroupTemperature() const {
    if (m_settings && m_settings->brew()->hasTemperatureOverride()) {
        double temp = m_settings->brew()->temperatureOverride();
        qDebug() << "getGroupTemperature: using override" << temp << "°C";
        return temp;
    }
    return m_profileManager->currentProfile().espressoTemperature();
}

void MainController::setSteamTemperatureImmediate(double temp) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->brew()->setSteamTemperature(temp);

    // Clear steamDisabled flag when user actively changes temperature
    if (m_settings->brew()->steamDisabled()) {
        m_settings->brew()->setSteamDisabled(false);
    }

    double groupTemp = getGroupTemperature();

    // Send all shot settings with updated temperature
    m_device->setShotSettings(
        temp,
        m_settings->brew()->steamTimeout(),
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        QStringLiteral("setSteamTemperatureImmediate")
    );

    qDebug() << "Steam temperature set to:" << temp;
}

void MainController::sendSteamTemperature(double temp) {
    // File-based logging for debugging when not connected to console
    auto logToFile = [](const QString& msg) {
        QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/steam_debug.log";
        QFile file(logPath);
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
            file.close();
        }
    };

    logToFile(QString("sendSteamTemperature called with temp=%1").arg(temp));
    qDebug() << "sendSteamTemperature:" << temp << "°C";

    // Update steamDisabled flag based on temperature
    // 0°C means disabled, any other temp means enabled
    if (m_settings) {
        m_settings->brew()->setSteamDisabled(temp == 0);
    }

    if (!m_device) {
        logToFile("ERROR: No device");
        return;
    }
    if (!m_device->isConnected()) {
        logToFile("ERROR: Device not connected");
        return;
    }
    if (!m_settings) {
        logToFile("ERROR: No settings");
        return;
    }

    double groupTemp = getGroupTemperature();

    logToFile(QString("Sending: steamTemp=%1 timeout=%2 waterTemp=%3 waterVol=%4 groupTemp=%5")
              .arg(temp)
              .arg(m_settings->brew()->steamTimeout())
              .arg(m_settings->brew()->waterTemperature())
              .arg(m_settings->brew()->effectiveHotWaterVolume())
              .arg(groupTemp));

    // Send to machine without saving to settings (for enable/disable toggle)
    m_device->setShotSettings(
        temp,
        m_settings->brew()->steamTimeout(),
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        QStringLiteral("sendSteamTemperature")
    );

    logToFile("Command queued successfully");
}

void MainController::startSteamHeating(const QString& reason) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Clear steamDisabled flag - we're explicitly starting steam heating
    m_settings->brew()->setSteamDisabled(false);

    // Always send the configured steam temperature
    double steamTemp = m_settings->brew()->steamTemperature();

    double groupTemp = getGroupTemperature();

    m_device->setShotSettings(
        steamTemp,
        m_settings->brew()->steamTimeout(),
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        reason.isEmpty() ? QStringLiteral("startSteamHeating") : reason
    );

    // Also send steam flow via MMR
    m_device->writeMMR(0x803828, m_settings->brew()->steamFlow(),
                       QStringLiteral("startSteamHeating"));

    qDebug() << "Started steam heating to" << steamTemp << "°C"
             << "from" << (reason.isEmpty() ? QStringLiteral("<unspecified>") : reason);
}

void MainController::turnOffSteamHeater() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Set steamDisabled flag - this ensures consistent state management
    m_settings->brew()->setSteamDisabled(true);

    double groupTemp = getGroupTemperature();

    // Send 0°C to turn off steam heater
    m_device->setShotSettings(
        0.0,
        m_settings->brew()->steamTimeout(),
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        QStringLiteral("turnOffSteamHeater")
    );

    qDebug() << "Turned off steam heater (steamDisabled=true)";
}

void MainController::setHotWaterFlowRateImmediate(int flow) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->hardware()->setHotWaterFlowRate(flow);

    m_device->writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, flow,
                       QStringLiteral("setHotWaterFlowRateImmediate"));

    qDebug() << "Hot water flow rate set to:" << flow;
}

void MainController::setSteamFlowImmediate(int flow) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->brew()->setSteamFlow(flow);

    // Verify-and-retry as defensive insurance: a single MMR write should be
    // enough (on-device testing showed zero retries needed across many slider
    // drags), but steam flow is user-visible enough that we read the register
    // back and retry on mismatch rather than relying on the write alone. One
    // caller-side call (slider release, preset tap) stays one logical command.
    m_device->writeMMRVerified(0x803828, flow,
                               QStringLiteral("setSteamFlowImmediate"));

    qDebug() << "Steam flow set to:" << flow;
}

void MainController::setSteamTimeoutImmediate(int timeout) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->brew()->setSteamTimeout(timeout);

    double groupTemp = getGroupTemperature();

    // Send all shot settings with updated timeout
    m_device->setShotSettings(
        m_settings->brew()->steamTemperature(),
        timeout,
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        QStringLiteral("setSteamTimeoutImmediate")
    );

    qDebug() << "Steam timeout set to:" << timeout;
}

void MainController::softStopSteam() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    double groupTemp = getGroupTemperature();

    // Send shot settings with 1-second timeout to trigger elapsed > target stop
    // This stops steam without triggering the purge sequence (which requestIdle() would do)
    // Does NOT save to settings - just sends the command
    m_device->setShotSettings(
        m_settings->brew()->steamTemperature(),
        1,  // 1 second - any elapsed time > 1 will trigger stop
        m_settings->brew()->waterTemperature(),
        m_settings->brew()->effectiveHotWaterVolume(),
        groupTemp,
        QStringLiteral("softStopSteam")
    );

    qDebug() << "Soft stop steam: sent 1-second timeout to trigger natural stop";
}

void MainController::reportShotStopReason(const QString& reason) {
    // #1161: QML pushes its resolved stopReason here whenever it changes
    // (single onStopReasonChanged handler covering all stop entry points).
    // We only store it — the actual stop command is still issued by the
    // existing QML/device path, unchanged. onShotEnded() maps this to the
    // persisted stoppedBy, with SAW/SAV C++ state taking precedence.
    m_pendingStopReason = reason;
}

void MainController::onEspressoCycleStarted() {
    // #1161: clear the prior shot's QML-reported stop reason at cycle
    // start so it can't bleed into this shot's stoppedBy. (QML also resets
    // its own stopReason to "" on shotStarted; this is belt-and-suspenders
    // and independent of QML/C++ signal ordering.)
    m_pendingStopReason.clear();

    // Safety check: abort shot if user has a saved scale but it's not connected,
    // AND the current profile actually uses weight (stop-at-weight or frame exit weights).
    // This prevents running a shot without weight tracking when the user expects it.
    // The machine may have been started from the group head button, so we can only
    // abort here (during preheat) — before any water flows.
    // Volume-based profiles can proceed without a physical scale.
    // Skip this check if any real scale (BLE or USB) is currently active.
    if (m_bleManager && !m_bleManager->isDisabled() && m_bleManager->hasSavedScale()) {
        // Check if any real scale is connected (BLE scale, USB scale, etc.)
        ScaleDevice* activeScale = m_machineState ? m_machineState->scale() : nullptr;
        bool hasRealScale = activeScale && activeScale->isConnected()
                            && activeScale->type() != QStringLiteral("flow");
        if (!hasRealScale) {
            // Check if the profile actually needs a scale
            bool profileNeedsScale = (m_profileManager->currentProfile().targetWeight() > 0);
            if (!profileNeedsScale) {
                // Also check per-frame exit weights
                for (const auto& step : m_profileManager->currentProfile().steps()) {
                    if (step.exitWeight > 0) {
                        profileNeedsScale = true;
                        break;
                    }
                }
            }
            if (profileNeedsScale) {
                qWarning() << "Shot aborted: saved scale is not connected and profile uses weight";
                if (m_device) {
                    m_device->requestState(DE1::State::Idle);
                }
                emit shotAbortedNoScale();
                return;
            }
            qDebug() << "Scale not connected but profile doesn't use weight - proceeding with shot";
        }
    }

    // Save previous shot if settling is still in progress — startShot() emits
    // shotProcessingReady synchronously, which triggers onShotEnded(). This must
    // happen BEFORE clearing the model or resetting m_extractionStarted, otherwise
    // the previous shot's data is lost.
    if (m_timingController) {
        m_timingController->setTargetWeight(m_profileManager->targetWeight());
        m_timingController->setCurrentProfile(m_profileManager->currentProfilePtr());
        m_timingController->startShot();
        m_timingController->tare();
    } else {
        qWarning() << "No timing controller!";
    }

    // Clear the graph for the new espresso cycle (previous shot is now saved).
    // Espresso elapsed time is sourced from m_timingController->shotTime();
    // its m_displayTimeBase reset happens inside m_timingController->startShot()
    // above, so we don't keep a parallel anchor here.
    m_lastShotTime = 0;
    m_extractionStarted = false;
    m_lastFrameNumber = -1;
    m_lastSampleTime = 0;  // prior shot's last sample.timer would otherwise stale-out the inter-sample delta gate
    m_trackLogCounter = 0;
    m_frameWeightSkipSent = -1;
    m_frameStartTime = 0;
    m_lastPressure = 0;
    if (m_filteredGoalPressure != 0 || m_filteredGoalFlow != 0) {
        m_filteredGoalPressure = 0;
        m_filteredGoalFlow = 0;
        emit goalsChanged();
    }
    m_lastFlow = 0;
    m_tareDone = true;
    if (m_shotDataModel) {
        m_shotDataModel->clear();
    }

    // Reset FlowScale and set dose for puck absorption compensation
    if (m_flowScale) {
        m_flowScale->reset();
        double dose = m_settings ? m_settings->dye()->dyeBeanWeight() : 0.0;
        m_flowScale->setDose(dose);
    }

    // Clear any pending BLE commands to prevent stale profile uploads
    if (m_device) {
        m_device->clearCommandQueue();
    }

    // Start debug logging for this shot
    if (m_shotDebugLogger) {
        m_shotDebugLogger->startCapture();
        m_shotDebugLogger->logInfo(QString("Profile: %1").arg(m_profileManager->currentProfile().title()));
    }

    // Clear shot notes if setting is enabled
    if (m_settings && m_settings->visualizer()->visualizerClearNotesOnStart()) {
        m_settings->dye()->setDyeShotNotes("");
    }
}

void MainController::onShotEnded() {
    // Clear any +10g bump applied via bumpTargetWeight() so MachineState::targetWeight
    // matches the profile again before the next shot. Doing this at shot end (rather
    // than at next-shot start) avoids depending on signal-handler connection order.
    if (m_machineState && m_profileManager) {
        m_machineState->setTargetWeight(m_profileManager->targetWeight());
    }

    // Clear filtered goals so CupFillView doesn't show stale tracking colors
    if (m_filteredGoalPressure != 0 || m_filteredGoalFlow != 0) {
        m_filteredGoalPressure = 0;
        m_filteredGoalFlow = 0;
        emit goalsChanged();
    }

    // Capture brew overrides before clearing temperature (used later when saving shot)
    // These ALWAYS have values - either user override or profile default
    double shotTemperatureOverride = 0.0;
    double shotTargetWeight = 0.0;

    if (m_settings) {
        // Temperature: user override OR profile's espresso temperature
        if (m_settings->brew()->hasTemperatureOverride()) {
            shotTemperatureOverride = m_settings->brew()->temperatureOverride();
        } else {
            shotTemperatureOverride = m_profileManager->currentProfile().espressoTemperature();
        }

        // Yield: user override OR profile's target weight (0 for volume-based profiles)
        if (m_settings->brew()->hasBrewYieldOverride()) {
            shotTargetWeight = m_settings->brew()->brewYieldOverride();
        } else if (m_profileManager->currentProfile().targetWeight() > 0) {
            shotTargetWeight = m_profileManager->currentProfile().targetWeight();
        }
    }

    // Only process espresso shots that actually extracted
    if (!m_extractionStarted || !m_settings || !m_shotDataModel) {
        // Stop debug logging even if we don't save
        if (m_shotDebugLogger) {
            m_shotDebugLogger->stopCapture();
        }
        return;
    }

    // Machine maintenance cycles must not pollute shot history or post-shot review.
    // Shared tier (also gates the Visualizer and MCP uploads and the Shot Plan warning).
    if (m_profileManager) {
        const QString beverageType = m_profileManager->currentProfile().beverageType();
        if (Profile::isMaintenanceBeverageType(beverageType)) {
            // Log the skip — this is the one maintenance gate with no other user-visible
            // trace, and "my shot didn't get saved" is undiagnosable without it.
            qInfo() << "Skipping shot history for maintenance profile, beverage_type:" << beverageType;
            if (m_shotDebugLogger)
                m_shotDebugLogger->stopCapture();
            m_extractionStarted = false;
            return;
        }
    }

    // Use extraction end time (excludes SAW settling phase) for accurate duration.
    // extractionDuration() is set for all shots (SAW and non-SAW) in endShot().
    // Falls back to rawTime only if timing controller is unavailable.
    double duration = (m_timingController && m_timingController->extractionDuration() > 0)
        ? m_timingController->extractionDuration()
        : m_shotDataModel->rawTime();

    double doseWeight = m_settings->dye()->dyeBeanWeight();
    // If DYE dose is unset (0), fall back to profile's recommended dose
    if (doseWeight <= 0 && m_profileManager->currentProfile().hasRecommendedDose())
        doseWeight = m_profileManager->currentProfile().recommendedDose();

    // Get final weight from timing controller (post-settling weight for SAW shots includes
    // drip after stop; for non-SAW shots this is the instantaneous weight at stop time).
    // Fall back to last recorded scale data, then estimate from volume.
    double finalWeight = 0;
    const auto& cumulativeWeight = m_shotDataModel->cumulativeWeightData();
    if (m_timingController && m_timingController->currentWeight() > 0) {
        finalWeight = m_timingController->currentWeight();
    } else if (!cumulativeWeight.isEmpty()) {
        finalWeight = cumulativeWeight.last().y();
    } else if (m_machineState) {
        // No scale data at all — estimate weight from volume: ml - 5 - dose*0.5
        // (5g waste tray loss + 50% of dose retained in wet puck)
        double cumulativeVolume = m_machineState->cumulativeVolume();
        double puckRetention = doseWeight > 0 ? doseWeight * 0.5 : 9.0;  // fallback 9g if no dose
        finalWeight = cumulativeVolume - 5.0 - puckRetention;
        if (finalWeight < 0) finalWeight = 0;
        qDebug() << "No scale: estimated weight from" << cumulativeVolume << "ml ->" << finalWeight << "g";
    }
    // Last resort: if yield is still 0 and profile has a target weight, use that
    // (SAW-stopped shots reach approximately the target weight)
    if (finalWeight <= 0 && m_profileManager->currentProfile().targetWeight() > 0)
        finalWeight = m_profileManager->currentProfile().targetWeight();

    // Record at the scale's real resolution (~0.1 g). The raw cumulative-weight
    // reading carries float noise (e.g. 35.36518272805495 g); rounding once here,
    // at the source, keeps every consumer rational — the shot record, the DYE
    // drink-weight metadata, the Visualizer upload, MCP, and exports — without
    // sprinkling rounding across each of them.
    if (finalWeight > 0)
        finalWeight = std::round(finalWeight * 10.0) / 10.0;

    // Trim trailing zero-pressure samples from SAW settling period before saving.
    // During settling the DE1 reports 0 pressure/flow while the scale settles — these
    // cause a vertical drop to 0 at the end of the graph. Weight data is preserved.
    // NOTE: Must run before smoothWeightFlowRate() — smoothWeightFlowRate() snapshots
    // m_weightFlowRatePoints as the raw export copy before smoothing. Running trim first
    // ensures neither the smoothed nor raw copy includes trailing zeros.
    m_shotDataModel->trimSettlingData();

    // Smooth weight flow rate before saving (centered moving average, window=5, ≈ 2.2s at 5Hz).
    // The raw LSLR data from recording has staircase artifacts from 0.1g scale quantization;
    // this post-processing matches de1app's smoothing level for storage and visualizer export.
    m_shotDataModel->smoothWeightFlowRate();

    // Auto flow calibration: compute per-profile multiplier from this shot's data.
    // Must run before stopCapture() so its debug output is included in the shot log.
    computeAutoFlowCalibration();

    // Capture shot-end epoch now so uploads (including deferred pending uploads) use consistent time.
    // Held in a local until after the discard branch so a dropped shot doesn't corrupt
    // m_pendingShotEpoch / m_pendingDebugLog that may still belong to a prior unflushed shot
    // (uploadPendingShot is gated on m_hasPendingShot, which the discard path doesn't touch).
    const qint64 pendingShotEpoch = QDateTime::currentSecsSinceEpoch();

    // Stop debug logging and get the captured log
    QString debugLog;
    if (m_shotDebugLogger) {
        m_shotDebugLogger->stopCapture();
        debugLog = m_shotDebugLogger->getCapturedLog();
    }

    // Build metadata for history
    ShotMetadata metadata;
    metadata.beanBrand = m_settings->dye()->dyeBeanBrand();
    metadata.beanType = m_settings->dye()->dyeBeanType();
    metadata.roastDate = m_settings->dye()->dyeRoastDate();
    metadata.roastLevel = m_settings->dye()->dyeRoastLevel();
    metadata.grinderBrand = m_settings->dye()->dyeGrinderBrand();
    metadata.grinderModel = m_settings->dye()->dyeGrinderModel();
    metadata.grinderBurrs = m_settings->dye()->dyeGrinderBurrs();
    metadata.grinderSetting = m_settings->dye()->dyeGrinderSetting();
    metadata.equipmentId = m_settings->dye()->activeEquipmentId();
    metadata.rpm = m_settings->dye()->dyeGrinderRpm();
    metadata.beanWeight = m_settings->dye()->dyeBeanWeight();
    metadata.drinkWeight = m_settings->dye()->dyeDrinkWeight();
    metadata.drinkTds = m_settings->dye()->dyeDrinkTds();
    metadata.drinkEy = m_settings->dye()->dyeDrinkEy();
    metadata.espressoEnjoyment = m_settings->dye()->dyeEspressoEnjoyment();
    metadata.espressoNotes = m_settings->dye()->dyeShotNotes();
    metadata.barista = m_settings->dye()->dyeBarista();
    metadata.beanBaseJson = m_settings->dye()->dyeBeanBaseData();
    // Coffee bag snapshot: which bag this shot was pulled with, and the
    // beans' freeze lifecycle at shot time (bean-bag-inventory).
    metadata.bagId = m_settings->dye()->activeBagId();
    metadata.frozenDate = m_settings->dye()->activeBagFrozenDate();
    metadata.defrostDate = m_settings->dye()->activeBagDefrostDate();

    // For volume/timer-based profiles (targetWeight=0), use the actual final weight
    // so favorites can restore a meaningful yield target
    if (shotTargetWeight <= 0 && finalWeight > 0) {
        shotTargetWeight = finalWeight;
    }

    // Capture once so both the async callback and synchronous code use the same value
    bool showPostShot = m_settings->visualizer()->visualizerShowAfterShot();

    // Aborted-shot classifier: drop shots that did not start (extraction < 10s AND yield < 5g).
    // Always on — validated against an 882-shot corpus, 5/882 (0.57%) discarded, all genuine
    // "did not start" cases. See openspec/specs/shot-save-filter/spec.md.
    {
        const bool aborted = decenza::isAbortedShot(duration, finalWeight);
        qInfo().noquote() << QStringLiteral("[discard-classifier] extractionDurationSec=%1 finalWeightG=%2 verdict=%3 action=%4")
            .arg(QString::number(duration, 'f', 3),
                 QString::number(finalWeight, 'f', 1),
                 aborted ? QStringLiteral("aborted") : QStringLiteral("kept"),
                 aborted ? QStringLiteral("discarded") : QStringLiteral("saved"));

        if (aborted) {
            emit shotDiscarded(duration, finalWeight);
            // Skip save, skip auto-upload, skip post-shot review navigation.
            // Reset extraction flag so subsequent operations don't re-trigger shot logic.
            m_extractionStarted = false;
            return;
        }
    }

    // Past the discard gate — commit the pending-shot snapshot used by uploadPendingShot()
    // and the synchronous visualizer auto-upload below.
    m_pendingShotEpoch = pendingShotEpoch;
    m_pendingDebugLog = debugLog;

    // Always save shot to local history (async — DB work runs on background thread)
    qDebug() << "[metadata] Saving shot - shotHistory:" << (m_shotHistory ? "exists" : "null")
             << "isReady:" << (m_shotHistory ? m_shotHistory->isReady() : false);
    if (m_shotHistory && m_shotHistory->isReady()) {
        if (m_savingShot) {
            qWarning() << "[metadata] Shot save already in progress, skipping";
        } else {
            m_savingShot = true;

            // Capture timestamp now (before async save) so it reflects shot end time
            QString shotDateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");

            // Connect to shotSaved signal for completion (single-shot, auto-disconnects)
            connect(m_shotHistory, &ShotHistoryStorage::shotSaved, this,
                    [this, finalWeight, shotDateTime, showPostShot,
                     duration, doseWeight, metadata, debugLog](qint64 shotId) {
                m_savingShot = false;

                if (shotId > 0) {
                    qDebug() << "[metadata] Shot saved to history with ID:" << shotId;

                    // Store shot ID for post-shot review page (so it can edit the saved shot)
                    m_lastSavedShotId = shotId;
                    emit lastSavedShotIdChanged();

                    // Auto-upload here (not before save) so we know the
                    // local shots.id and can pass it to the uploader.
                    // VisualizerUploader emits uploadSucceededForShot with
                    // this id, and MainController persists the link from
                    // C++ — independent of any UI page being alive. A
                    // shot that failed to save has no row to link, so we
                    // intentionally do NOT auto-upload it (avoids the
                    // orphaned-upload bug this change exists to fix).
                    if (m_settings->visualizer()->visualizerAutoUpload() && m_visualizer) {
                        qDebug() << "  -> Auto-uploading to visualizer for shot" << shotId;
                        m_visualizer->uploadShot(
                            m_shotDataModel, m_profileManager->currentProfilePtr(),
                            duration, finalWeight, doseWeight, metadata, debugLog,
                            m_pendingShotEpoch, shotId);
                    }

                    // Set shot date/time for display on metadata page
                    m_settings->dye()->setDyeShotDateTime(shotDateTime);
                    qDebug() << "[metadata] Set dyeShotDateTime to:" << shotDateTime;

                    // Update the drink weight with actual final weight from this shot
                    m_settings->dye()->setDyeDrinkWeight(finalWeight);
                    qDebug() << "[metadata] Set dyeDrinkWeight to:" << finalWeight;

                    // Reset shot-specific metadata for the next shot
                    // Bean/grinder info persists (sticky), but per-shot fields reset
                    m_settings->dye()->setDyeEspressoEnjoyment(m_settings->visualizer()->defaultShotRating());
                    m_settings->dye()->setDyeShotNotes("");
                    m_settings->dye()->setDyeDrinkTds(0);
                    m_settings->dye()->setDyeDrinkEy(0);
                    qDebug() << "[metadata] Reset enjoyment, notes, TDS, EY for next shot";

                    // Force QSettings to sync to disk immediately
                    m_settings->sync();

                    // Now that we have a valid shot ID, show the metadata page
                    if (showPostShot) {
                        qDebug() << "[metadata] Showing post-shot review page with shotId:" << shotId;
                        emit shotEndedShowMetadata(shotId);
                    }
                } else {
                    qWarning() << "[metadata] Failed to save shot to history (returned" << shotId << ") - metadata preserved for next attempt";
                    // Deliberately NOT zeroing m_lastSavedShotId: a failed save
                    // does not change which stored shot is newest, and zeroing
                    // killed every "most recent shot" consumer (review-page
                    // sticky-sync gate, Last Shot widget) until restart.

                    // Leave the espresso page either way; 0 tells the handler
                    // there is no shot to review (it must NOT open the prior
                    // shot via lastSavedShotId).
                    if (showPostShot) {
                        qWarning() << "[metadata] Shot save failed - leaving espresso page without a review target";
                        emit shotEndedShowMetadata(0);
                    }
                }
            }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

            // #1161: classify why the shot ended so the dial-in advisor
            // can discount yield/duration on manually-stopped shots (their
            // yield is user-chosen, not an extraction outcome). Precedence,
            // in order:
            //   1. SAW (wasSawTriggered, backed by m_stopAtWeightTriggered)
            //      — C++ ground truth, survives settling, wins outright.
            //   2. SAV (wasVolumeStopped) — C++ ground truth.
            //   3. QML stopReason "manual" — a deliberate user stop.
            //   4. QML stopReason "weight" — defensive fallback for a
            //      weight stop the C++ SAW flag did not capture (e.g. a
            //      QML-side weight signal without onSawTriggered); normally
            //      branch 1 already handled it.
            //   5. else → "profileEnd": profile ran its course OR the DE1's
            //      own hardware button (the BLE protocol cannot distinguish
            //      those). The consumer treats a sub-target "profileEnd"
            //      like "manual", covering the DE1-button case.
            QString stoppedBy;
            if (m_timingController && m_timingController->wasSawTriggered())
                stoppedBy = QStringLiteral("weight");
            else if (m_machineState && m_machineState->wasVolumeStopped())
                stoppedBy = QStringLiteral("volume");
            else if (m_pendingStopReason == QStringLiteral("manual"))
                stoppedBy = QStringLiteral("manual");
            else if (m_pendingStopReason == QStringLiteral("weight"))
                stoppedBy = QStringLiteral("weight");
            else
                stoppedBy = QStringLiteral("profileEnd");

            m_shotHistory->saveShot(
                m_shotDataModel, m_profileManager->currentProfilePtr(),
                duration, finalWeight, doseWeight,
                metadata, debugLog,
                shotTemperatureOverride, shotTargetWeight, stoppedBy);

            // Stamp the actual dose/yield onto the active bag ("last used
            // with this bag" — the dose may come from SAW/profile settings
            // rather than a manual edit). Independent of save success:
            // a failed stamp is logged inside storage and never blocks the
            // shot save. No user prompt (bean-bag-inventory).
            if (m_bagStorage && bagIdIsSet(metadata.bagId)) {
                QVariantMap stamp;
                if (doseWeight > 0)
                    stamp.insert(QStringLiteral("doseWeightG"), doseWeight);
                // Yield is an OVERRIDE: stamp it only when the shot's target
                // differs from the profile's default weight, else 0 so the bag
                // follows the profile baseline (shared, tested rule).
                if (m_profileManager) {
                    stamp.insert(QStringLiteral("yieldOverrideG"),
                                 CoffeeBagStorage::yieldOverrideForTarget(
                                     shotTargetWeight,
                                     m_profileManager->currentProfile().targetWeight()));
                }
                stamp.insert(QStringLiteral("lastUsedEpoch"), QDateTime::currentSecsSinceEpoch());
                m_bagStorage->requestUpdateBag(metadata.bagId, stamp);
            }
        }
    } else {
        qWarning() << "[metadata] Could not save shot - history not ready!";

        // Leave the espresso page; 0 = no shot to review (see signal doc).
        if (showPostShot) {
            emit shotEndedShowMetadata(0);
        }
    }

    // Report shot to decenza.coffee shot map
    if (m_shotReporter && m_shotReporter->isEnabled()) {
        m_shotReporter->reportShot(m_profileManager->currentProfile().title(), "Decent DE1");
    }

    // Log final shot state for debugging early exits
    const auto& pressureData = m_shotDataModel->pressureData();
    const auto& flowData = m_shotDataModel->flowData();
    double finalPressure = pressureData.isEmpty() ? 0 : pressureData.last().y();
    double finalFlow = flowData.isEmpty() ? 0 : flowData.last().y();
    qDebug() << "MainController: Shot ended -"
             << "Duration:" << QString::number(duration, 'f', 1) << "s"
             << "Weight:" << QString::number(finalWeight, 'f', 1) << "g"
             << "Final P:" << QString::number(finalPressure, 'f', 2) << "bar"
             << "Final F:" << QString::number(finalFlow, 'f', 2) << "ml/s";

    // Auto-upload is dispatched from the shotSaved callback above (once
    // the local shots.id is known) so the returned Visualizer id can be
    // persisted to the right row from C++. Do NOT auto-upload here —
    // before save the id is unknown and the upload would orphan.

    // Store pending shot data for later upload (user can re-upload with updated metadata)
    // Note: shotEndedShowMetadata is emitted from the shotSaved callback above,
    // after m_lastSavedShotId is set, so PostShotReviewPage gets a valid shot ID.
    if (showPostShot) {
        m_hasPendingShot = true;
        m_pendingShotDuration = duration;
        m_pendingShotFinalWeight = finalWeight;
        m_pendingShotDoseWeight = doseWeight;
        qDebug() << "  -> Will show metadata page after shot is saved";
    }

    // Reset extraction flag so that subsequent Steam/HotWater/Flush operations
    // don't incorrectly trigger shot metadata page or upload
    m_extractionStarted = false;
}

void MainController::uploadPendingShot() {
    if (!m_hasPendingShot || !m_settings || !m_shotDataModel || !m_visualizer) {
        qDebug() << "MainController: No pending shot to upload";
        return;
    }

    // Build metadata from current settings
    ShotMetadata metadata;
    metadata.beanBrand = m_settings->dye()->dyeBeanBrand();
    metadata.beanType = m_settings->dye()->dyeBeanType();
    metadata.roastDate = m_settings->dye()->dyeRoastDate();
    metadata.roastLevel = m_settings->dye()->dyeRoastLevel();
    metadata.grinderBrand = m_settings->dye()->dyeGrinderBrand();
    metadata.grinderModel = m_settings->dye()->dyeGrinderModel();
    metadata.grinderBurrs = m_settings->dye()->dyeGrinderBurrs();
    metadata.grinderSetting = m_settings->dye()->dyeGrinderSetting();
    metadata.equipmentId = m_settings->dye()->activeEquipmentId();
    metadata.rpm = m_settings->dye()->dyeGrinderRpm();
    metadata.beanWeight = m_settings->dye()->dyeBeanWeight();
    metadata.drinkWeight = m_settings->dye()->dyeDrinkWeight();
    metadata.drinkTds = m_settings->dye()->dyeDrinkTds();
    metadata.drinkEy = m_settings->dye()->dyeDrinkEy();
    metadata.espressoEnjoyment = m_settings->dye()->dyeEspressoEnjoyment();
    metadata.barista = m_settings->dye()->dyeBarista();
    metadata.beanBaseJson = m_settings->dye()->dyeBeanBaseData();
    metadata.bagId = m_settings->dye()->activeBagId();
    metadata.frozenDate = m_settings->dye()->activeBagFrozenDate();
    metadata.defrostDate = m_settings->dye()->activeBagDefrostDate();

    // Build notes: user notes + AI recommendation (if any)
    QString notes = m_settings->dye()->dyeShotNotes();
    if (m_aiManager && !m_aiManager->lastRecommendation().isEmpty()) {
        QString aiRec = m_aiManager->lastRecommendation();
        QString provider = m_aiManager->selectedProvider();
        QString providerName = provider;
        if (provider == "openai") providerName = "OpenAI GPT-4o";
        else if (provider == "anthropic") providerName = "Anthropic Claude";
        else if (provider == "gemini") providerName = "Google Gemini";
        else if (provider == "ollama") providerName = "Ollama";

        if (!notes.isEmpty()) {
            notes += "\n\n---\n\n";
        }
        notes += aiRec + "\n\n---\nAdvice by " + providerName;
    }
    metadata.espressoNotes = notes;

    qDebug() << "MainController: Uploading pending shot with metadata -"
             << "Profile:" << m_profileManager->currentProfile().title()
             << "Duration:" << m_pendingShotDuration << "s"
             << "Bean:" << metadata.beanBrand << metadata.beanType;

    // Manual re-upload of the just-finished shot: by now the shot is
    // saved and m_lastSavedShotId holds its row id, so the link is
    // persisted from C++ via uploadSucceededForShot.
    m_visualizer->uploadShot(m_shotDataModel, m_profileManager->currentProfilePtr(),
                             m_pendingShotDuration, m_pendingShotFinalWeight,
                             m_pendingShotDoseWeight, metadata, m_pendingDebugLog,
                             m_pendingShotEpoch, m_lastSavedShotId);

    m_hasPendingShot = false;
    m_pendingDebugLog.clear();
}

void MainController::generateFakeShotData() {
    if (!m_shotDataModel) return;

    qDebug() << "DEV: Generating fake shot data for testing";

    // Clear existing data
    m_shotDataModel->clear();

    // Generate ~30 seconds of realistic espresso data at 5Hz (150 samples)
    const double sampleRate = 0.2;  // 5Hz = 0.2s between samples
    const double totalDuration = 30.0;
    const int numSamples = static_cast<int>(totalDuration / sampleRate);

    // Phase timings
    const double preinfusionEnd = 8.0;
    const double rampEnd = 12.0;
    const double steadyEnd = 25.0;

    // Helper for small random noise
    auto noise = [](double range) {
        return (QRandomGenerator::global()->bounded(100) / 100.0) * range;
    };

    double prevWeight = 0.0;
    for (int i = 0; i < numSamples; i++) {
        double t = i * sampleRate;
        double temperature = 92.0 + noise(1.0);  // 92-93°C

        double pressure, flow, pressureGoal, flowGoal, weight;
        int frameNumber;

        if (t < preinfusionEnd) {
            // Preinfusion: low pressure, minimal flow
            double progress = t / preinfusionEnd;
            pressure = 2.0 + progress * 2.0 + noise(0.5);
            flow = 0.5 + progress * 1.0 + noise(0.5);
            pressureGoal = 4.0;
            flowGoal = 0.0;
            frameNumber = 0;
            weight = progress * 3.0;  // ~3g by end of preinfusion
        } else if (t < rampEnd) {
            // Ramp up: pressure rising to 9 bar
            double progress = (t - preinfusionEnd) / (rampEnd - preinfusionEnd);
            pressure = 4.0 + progress * 5.0 + noise(0.5);
            flow = 1.5 + progress * 1.5 + noise(0.5);
            pressureGoal = 9.0;
            flowGoal = 0.0;
            frameNumber = 1;
            weight = 3.0 + progress * 8.0;  // 3-11g
        } else if (t < steadyEnd) {
            // Steady extraction: ~9 bar, 2-2.5 ml/s flow
            double progress = (t - rampEnd) / (steadyEnd - rampEnd);
            pressure = 8.5 + noise(1.0);  // 8.5-9.5 bar
            flow = 2.0 + noise(0.5);  // 2.0-2.5 ml/s
            pressureGoal = 9.0;
            flowGoal = 0.0;
            frameNumber = 2;
            weight = 11.0 + progress * 25.0;  // 11-36g
        } else {
            // Taper/ending: pressure drops
            double progress = (t - steadyEnd) / (totalDuration - steadyEnd);
            pressure = 8.5 - progress * 6.0 + noise(0.5);
            flow = 2.0 - progress * 1.5 + noise(0.5);
            pressureGoal = 3.0;
            flowGoal = 0.0;
            frameNumber = 3;
            weight = 36.0 + progress * 4.0;  // 36-40g
        }

        // Derive weight flow rate (g/s) from weight delta
        double weightFlowRate = (i > 0) ? (weight - prevWeight) / sampleRate : 0.0;
        prevWeight = weight;

        // addSample(time, pressure, flow, temperature, mixTemp, pressureGoal, flowGoal, temperatureGoal, frameNumber, isFlowMode)
        // Simulation uses pressure mode (isFlowMode = false)
        m_shotDataModel->addSample(t, pressure, flow, temperature, temperature, pressureGoal, flowGoal, 92.0, frameNumber, false);
        m_shotDataModel->addWeightSample(t, weight, weightFlowRate);
    }

    // Add phase markers (simulation uses pressure mode)
    m_shotDataModel->addPhaseMarker(0.0, "Preinfusion", 0, false);
    m_shotDataModel->addPhaseMarker(preinfusionEnd, "Extraction", 1, false);
    m_shotDataModel->addPhaseMarker(steadyEnd, "Ending", 3, false);

    // Set up pending shot state
    m_hasPendingShot = true;
    m_pendingShotDuration = totalDuration;
    m_pendingShotFinalWeight = 40.0;
    m_pendingShotDoseWeight = 18.0;

    qDebug() << "DEV: Generated" << numSamples << "fake samples";

    // Save simulated shot to history (like a real shot, async)
    if (m_shotHistory && m_shotHistory->isReady() && m_settings) {
        if (m_savingShot) {
            qWarning() << "DEV: Shot save already in progress, skipping simulated shot";
        } else {
            m_savingShot = true;

            ShotMetadata metadata;
            metadata.beanBrand = m_settings->dye()->dyeBeanBrand();
            metadata.beanType = m_settings->dye()->dyeBeanType();
            metadata.roastDate = m_settings->dye()->dyeRoastDate();
            metadata.roastLevel = m_settings->dye()->dyeRoastLevel();
            metadata.grinderBrand = m_settings->dye()->dyeGrinderBrand();
            metadata.grinderModel = m_settings->dye()->dyeGrinderModel();
            metadata.grinderBurrs = m_settings->dye()->dyeGrinderBurrs();
            metadata.grinderSetting = m_settings->dye()->dyeGrinderSetting();
            metadata.equipmentId = m_settings->dye()->activeEquipmentId();
            metadata.rpm = m_settings->dye()->dyeGrinderRpm();
            metadata.beanWeight = m_pendingShotDoseWeight;
            metadata.drinkWeight = m_settings->dye()->dyeDrinkWeight();
            metadata.drinkTds = m_settings->dye()->dyeDrinkTds();
            metadata.drinkEy = m_settings->dye()->dyeDrinkEy();
            metadata.espressoEnjoyment = m_settings->dye()->dyeEspressoEnjoyment();
            metadata.espressoNotes = m_settings->dye()->dyeShotNotes();
            metadata.barista = m_settings->dye()->dyeBarista();
            metadata.beanBaseJson = m_settings->dye()->dyeBeanBaseData();
            metadata.bagId = m_settings->dye()->activeBagId();
            metadata.frozenDate = m_settings->dye()->activeBagFrozenDate();
            metadata.defrostDate = m_settings->dye()->activeBagDefrostDate();

            // Use current profile's temperature and target weight as overrides
            double temperatureOverride = m_profileManager->currentProfile().espressoTemperature();
            double targetWeight = m_profileManager->currentProfile().targetWeight();

            double pendingFinalWeight = m_pendingShotFinalWeight;
            connect(m_shotHistory, &ShotHistoryStorage::shotSaved, this, [this, pendingFinalWeight](qint64 shotId) {
                m_savingShot = false;

                if (shotId > 0) {
                    qDebug() << "DEV: Simulated shot saved to history with ID:" << shotId;
                    m_lastSavedShotId = shotId;
                    emit lastSavedShotIdChanged();

                    // Update drink weight
                    m_settings->dye()->setDyeDrinkWeight(pendingFinalWeight);

                    // Reset shot-specific metadata for next shot
                    m_settings->dye()->setDyeEspressoEnjoyment(m_settings->visualizer()->defaultShotRating());
                    m_settings->dye()->setDyeShotNotes("");
                    m_settings->dye()->setDyeDrinkTds(0);
                    m_settings->dye()->setDyeDrinkEy(0);
                    m_settings->sync();
                } else {
                    qWarning() << "DEV: Failed to save simulated shot to history";
                }
            }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

            m_shotHistory->saveShot(
                m_shotDataModel, m_profileManager->currentProfilePtr(),
                totalDuration, m_pendingShotFinalWeight, m_pendingShotDoseWeight,
                metadata, "[Simulated shot]",
                temperatureOverride, targetWeight);
        }
    }
}

void MainController::clearCrashLog() {
    QString path = CrashHandler::crashLogPath();
    if (QFile::exists(path)) {
        QFile::remove(path);
        qDebug() << "MainController: Cleared crash log at" << path;
    }
}

void MainController::factoryResetAndQuit()
{
    qWarning() << "MainController::factoryResetAndQuit() - Starting factory reset";

    // 1. Stop the web server so it can't serve during wipe
    if (m_shotServer) {
        m_shotServer->stop();
    }

    // 2. Close the shot database so files can be deleted
    if (m_shotHistory) {
        m_shotHistory->close();
    }

    // 3. Wipe all data
    m_settings->factoryReset();

    // 4. Platform-specific exit
#ifdef Q_OS_ANDROID
    // Launch system uninstall dialog
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "requestUninstall",
        "()V");
#endif

    // 5. Quit the app
    QCoreApplication::quit();
}

void MainController::bumpTargetWeight(double deltaG)
{
    if (!m_machineState) return;
    const double current = m_machineState->targetWeight();
    if (current <= 0.0) return;

    const auto phase = m_machineState->phase();
    if (phase != MachineState::Phase::Preinfusion && phase != MachineState::Phase::Pouring) {
        return;
    }

    const double newTarget = current + deltaG;
    qInfo().noquote() << "MainController::bumpTargetWeight: targetWeight"
                      << current << "->" << newTarget << "g (delta=" << deltaG << ")";
    m_machineState->setTargetWeight(newTarget);
}

void MainController::onShotSampleReceived(const ShotSample& sample) {
    if (!m_shotDataModel || !m_machineState) {
        return;
    }

    MachineState::Phase phase = m_machineState->phase();

    // Forward flow samples to MachineState for FlowScale during any dispensing phase
    bool isDispensingPhase = (phase == MachineState::Phase::Preinfusion ||
                              phase == MachineState::Phase::Pouring ||
                              phase == MachineState::Phase::Steaming ||
                              phase == MachineState::Phase::HotWater ||
                              phase == MachineState::Phase::Flushing);

    if (isDispensingPhase && m_lastSampleTime > 0) {
        // sample.timer is a 16-bit value (wraps at 65536/100 = 655.36 s);
        // a wrap during dispensing produces a hugely negative naive delta.
        // Unwrap before the bounded check so we don't drop a frame's worth
        // of FlowScale integration on every wrap.
        constexpr double kSampleTimerModSec = 65536.0 / 100.0;
        double deltaTime = sample.timer - m_lastSampleTime;
        if (deltaTime < 0) deltaTime += kSampleTimerModSec;
        if (deltaTime > 0 && deltaTime < 1.0) {
            m_machineState->onFlowSample(sample.groupFlow, deltaTime);

            // Note: FlowScale is fed by MachineState when it is the active scale (no physical scale).
            // Shadow feeding when a physical scale is present was only used for FlowScale Compare
            // logging and has been removed to reduce main-thread load on slow devices.
        }
    }
    m_lastSampleTime = sample.timer;

    // Record steam data only while steam is actually flowing. isFlowing()
    // returns true only for SubState::Steaming or SubState::Pouring (whitelist);
    // all other Steaming-phase substates (Puffing, Ending, FinalHeating, etc.)
    // are excluded. Without this gate, post-flow purge/wind-down samples inflate
    // rawTime() and skew SteamHealth's avg/peak metrics with non-steaming data.
    bool steamFlowing = (phase == MachineState::Phase::Steaming
                         && m_machineState->isFlowing());
    if (steamFlowing && m_steamDataModel) {
        if (m_steamStartTimeMs == 0) {
            m_steamStartTimeMs = QDateTime::currentMSecsSinceEpoch();
            m_steamDataModel->clear();
            // Add flow goal line from current settings
            double flowGoal = m_settings->brew()->steamFlow() / 100.0;
            m_steamDataModel->addFlowGoalPoint(0, flowGoal);
            m_steamDataModel->addFlowGoalPoint(m_settings->brew()->steamTimeout(), flowGoal);
            if (m_steamHealthTracker)
                m_steamHealthTracker->resetSession();
        }
        double t = (QDateTime::currentMSecsSinceEpoch() - m_steamStartTimeMs) / 1000.0;
        m_steamDataModel->addSample(t, sample.groupPressure, sample.groupFlow, sample.steamTemp);

        // Live threshold warnings
        if (m_steamHealthTracker)
            m_steamHealthTracker->onSample(sample.groupPressure, sample.steamTemp);
    }

    // Record shot data only during active espresso phases OR during settling (for drip visualization)
    bool isEspressoPhase = (phase == MachineState::Phase::Preinfusion ||
                           phase == MachineState::Phase::Pouring);
    bool isSettling = m_timingController && m_timingController->isSawSettling();

    if (!isEspressoPhase && !isSettling) {
        return;
    }

    // Track latest sensor values for transition reason inference
    m_lastPressure = sample.groupPressure;
    m_lastFlow = sample.groupFlow;

    // Determine active pump mode for current frame (to show only active goal
    // curve). Computed early so the values can be passed to ShotTimingController
    // below — its onShotSample is the single anchor point for shot-elapsed
    // time, and we route everything through its shotTime() afterward.
    double pressureGoal = sample.setPressureGoal;
    double flowGoal = sample.setFlowGoal;
    bool isFlowMode = false;
    {
        int fi = sample.frameNumber;
        const auto& steps = m_profileManager->currentProfile().steps();
        if (fi >= 0 && fi < steps.size()) {
            isFlowMode = steps[fi].isFlowControl();
            if (isFlowMode) {
                pressureGoal = 0;  // Flow mode - hide pressure goal
            } else {
                flowGoal = 0;      // Pressure mode - hide flow goal
            }
        }
    }

    // Forward to ShotTimingController FIRST so ITS m_displayTimeBase anchor
    // and ITS m_extractionStarted flag are up to date before we read
    // shotTime() below. (Both classes happen to have an m_extractionStarted
    // member; the one referenced here is ShotTimingController's, which is
    // what shotTime() consults.) Anchoring through a single source of truth
    // keeps phase markers and graph data points on the same t=0 origin —
    // otherwise MainController could fire one BLE sample earlier than the
    // timing controller, and the two would disagree by the inter-sample
    // interval.
    if (m_timingController) {
        m_timingController->onShotSample(sample, pressureGoal, flowGoal, sample.setTempGoal,
                                          sample.frameNumber, isFlowMode);
    }

    double time = m_timingController ? m_timingController->shotTime() : 0.0;
    m_lastShotTime = time;

    // Mark when extraction actually starts (transition from preheating to preinfusion/pouring)
    bool isExtracting = (phase == MachineState::Phase::Preinfusion ||
                        phase == MachineState::Phase::Pouring ||
                        phase == MachineState::Phase::Ending);

    if (isExtracting && !m_extractionStarted) {
        m_extractionStarted = true;
        m_frameStartTime = time;
        m_shotDataModel->markExtractionStart(time);
    }

    // Update filtered goals for QML (zeroed for non-active mode)
    if (m_filteredGoalPressure != pressureGoal || m_filteredGoalFlow != flowGoal) {
        m_filteredGoalPressure = pressureGoal;
        m_filteredGoalFlow = flowGoal;
        emit goalsChanged();
    }

    // Detect frame changes and add markers with frame names from profile
    // Only track during actual extraction phases (not preheating - frame numbers are unreliable then)
    if (isExtracting && sample.frameNumber >= 0 && sample.frameNumber != m_lastFrameNumber) {
        QString frameName;
        int frameIndex = sample.frameNumber;

        // Look up frame name from current profile
        const auto& steps = m_profileManager->currentProfile().steps();
        if (frameIndex >= 0 && frameIndex < steps.size()) {
            frameName = steps[frameIndex].name;
        }

        // Fall back to frame number if no name
        if (frameName.isEmpty()) {
            frameName = QString("F%1").arg(frameIndex);
        }

        // Determine transition reason for the PREVIOUS frame that just exited
        QString transitionReason;
        int prevFrameIndex = m_lastFrameNumber;
        if (prevFrameIndex >= 0 && prevFrameIndex < steps.size()) {
            const ProfileFrame& prevFrame = steps[prevFrameIndex];

            if (m_timingController && m_timingController->wasWeightExit(prevFrameIndex)) {
                // App sent skipToNextFrame() due to weight - 100% certain
                transitionReason = QStringLiteral("weight");
            } else if (prevFrame.exitIf) {
                // Machine-side exit condition was configured - infer from sensor values
                double frameElapsed = time - m_frameStartTime;
                bool timeExpired = frameElapsed >= prevFrame.seconds * 0.9;

                if (prevFrame.exitType == QStringLiteral("pressure_over") && m_lastPressure >= prevFrame.exitPressureOver) {
                    transitionReason = QStringLiteral("pressure");
                } else if (prevFrame.exitType == QStringLiteral("pressure_under") && m_lastPressure > 0 && m_lastPressure <= prevFrame.exitPressureUnder) {
                    transitionReason = QStringLiteral("pressure");
                } else if (prevFrame.exitType == QStringLiteral("flow_over") && m_lastFlow >= prevFrame.exitFlowOver) {
                    transitionReason = QStringLiteral("flow");
                } else if (prevFrame.exitType == QStringLiteral("flow_under") && m_lastFlow > 0 && m_lastFlow <= prevFrame.exitFlowUnder) {
                    transitionReason = QStringLiteral("flow");
                } else if (timeExpired) {
                    // Exit condition configured but time ran out first
                    transitionReason = QStringLiteral("time");
                } else {
                    // Condition was configured, values near threshold - machine likely triggered it
                    transitionReason = prevFrame.exitType.contains(QStringLiteral("pressure"))
                        ? QStringLiteral("pressure") : QStringLiteral("flow");
                    qDebug() << "MainController: Frame" << prevFrameIndex
                             << "exit reason ambiguous - exitType:" << prevFrame.exitType
                             << "pressure:" << m_lastPressure << "flow:" << m_lastFlow
                             << "inferred:" << transitionReason;
                }
            } else {
                // No exit condition configured - frame ended by time
                transitionReason = QStringLiteral("time");
            }
        }

        m_shotDataModel->addPhaseMarker(time, frameName, frameIndex, isFlowMode, transitionReason);
        m_frameStartTime = time;  // Record start time of new frame
        m_lastFrameNumber = sample.frameNumber;
        m_currentFrameName = frameName;  // Store for accessibility QML binding

        // Notify of frame change (tick sound + transition reason for UI pill)
        emit frameChanged(frameIndex, frameName, transitionReason);
    }

    // Skip adding sensor data to graph during settling — DE1 reports 0 pressure/flow
    // while the scale settles, which draws a vertical drop to 0 on the live graph.
    // Weight data still flows — ShotTimingController::weightSampleReady connects
    // directly to ShotDataModel::addWeightSample in main.cpp, bypassing this function.
    if (isSettling) {
        return;
    }

    // Add sample data to graph
    m_shotDataModel->addSample(time, sample.groupPressure,
                               sample.groupFlow, sample.headTemp,
                               sample.mixTemp,
                               pressureGoal, flowGoal, sample.setTempGoal,
                               sample.frameNumber, isFlowMode);

    // Log tracking delta every 10 shot samples for debug (at the DE1's ~5Hz sample rate,
    // this is roughly every 2 seconds). Only log when a goal is active.
    if (isExtracting && m_trackLogCounter++ % 10 == 0) {
        double goal = isFlowMode ? flowGoal : pressureGoal;
        if (goal > 0) {
            double actual = isFlowMode ? sample.groupFlow : sample.groupPressure;
            double delta = qAbs(actual - goal);
            // Thresholds must match Theme.trackingColor() in Theme.qml
            double floorGood = isFlowMode ? 0.4 : 0.8;
            double floorWarn = isFlowMode ? 0.8 : 1.8;
            double threshGood = qMax(floorGood, goal * 0.25);
            double threshWarn = qMax(floorWarn, goal * 0.50);
            QString color = delta < threshGood ? "GREEN" : (delta < threshWarn ? "YELLOW" : "RED");
            qDebug() << "[ExtractionTrack]" << (isFlowMode ? "flow" : "pressure")
                     << "actual=" << QString::number(actual, 'f', 2)
                     << "goal=" << QString::number(goal, 'f', 2)
                     << "delta=" << QString::number(delta, 'f', 2)
                     << "threshG=" << QString::number(threshGood, 'f', 2)
                     << "threshW=" << QString::number(threshWarn, 'f', 2)
                     << color;
        }
    }
}

void MainController::onScaleWeightChanged(double weight) {
    if (!m_machineState) {
        return;
    }

    // Weight processing (LSLR, SOW, per-frame exits) is now handled by WeightProcessor
    // on a dedicated worker thread. It receives weight samples directly from the scale
    // and feeds ShotTimingController via flowRatesReady signal.

    // FlowScale comparison logging: log both physical scale and FlowScale estimated weight
    // during espresso extraction to validate puck absorption model.
    // Disabled when a physical BT scale is connected - the comparison logging is not needed
    // and running it at 5Hz on the main thread adds load on slow devices.
    // Also disabled in simulator mode - SimulatedScale isn't a BT scale so btScaleConnected
    // would be false, but FlowScale never receives flow samples there (raw/est always 0).
    bool btScaleConnected = m_bleManager && m_bleManager->scaleDevice() &&
                            m_bleManager->scaleDevice()->isConnected();
    bool simulatorScaleActive = m_machineState->scale() &&
                                m_machineState->scale()->type() == "simulated";
    if (m_flowScale && m_extractionStarted && m_settings && m_settings->useFlowScale() &&
        !btScaleConnected && !simulatorScaleActive) {
        MachineState::Phase phase = m_machineState->phase();
        if (phase == MachineState::Phase::Preinfusion ||
            phase == MachineState::Phase::Pouring ||
            phase == MachineState::Phase::Ending) {
            double estimatedWeight = m_flowScale->weight();
            double rawFlow = m_flowScale->rawFlowIntegral();
            double error = estimatedWeight - weight;
            qDebug().nospace() << "[FlowScale Compare] "
                << "time=" << QString::number(m_lastShotTime, 'f', 1) << "s"
                << " scale=" << QString::number(weight, 'f', 1) << "g"
                << " est=" << QString::number(estimatedWeight, 'f', 1) << "g"
                << " raw=" << QString::number(rawFlow, 'f', 1) << "g"
                << " err=" << QString::number(error, 'f', 1) << "g"
                << " phase=" << (phase == MachineState::Phase::Preinfusion ? "PI" : "Pour");
        }
    }
}

bool MainController::isSawSettling() const {
    return m_timingController ? m_timingController->isSawSettling() : false;
}

void MainController::processPendingVisualizerRatingSync()
{
    QSettings s(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
    if (!s.contains(QStringLiteral("migration16/pendingVisualizerSync")))
        return;

    // Visualizer needs credentials to PATCH. If absent, leave the list
    // for a future boot where the user has configured an account.
    const QString user = s.value(QStringLiteral("visualizer/username")).toString();
    const QString pass = s.value(QStringLiteral("visualizer/password")).toString();
    if (user.isEmpty() || pass.isEmpty()) {
        qDebug() << "MainController: migration16 pending Visualizer sync skipped (no credentials)";
        return;
    }

    dispatchNextPendingVisualizerSync();
}

void MainController::dispatchNextPendingVisualizerSync()
{
    if (!m_migration16InFlightVisualizerId.isEmpty()) return;
    if (!m_visualizer || !m_shotHistory) return;

    QSettings s(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
    const QByteArray raw = s.value(
        QStringLiteral("migration16/pendingVisualizerSync")).toByteArray();
    if (raw.isEmpty()) return;

    const QJsonArray pending = QJsonDocument::fromJson(raw).array();
    if (pending.isEmpty()) {
        s.remove(QStringLiteral("migration16/pendingVisualizerSync"));
        return;
    }

    const QJsonObject entry = pending.first().toObject();
    const qint64 shotId = entry.value("shotId").toVariant().toLongLong();
    const QString visualizerId = entry.value("visualizerId").toString();
    if (shotId <= 0 || visualizerId.isEmpty()) {
        // Malformed entry — drop and try the next one. Re-write the list
        // without the bad entry and recurse.
        QJsonArray rest = pending;
        rest.removeFirst();
        if (rest.isEmpty())
            s.remove(QStringLiteral("migration16/pendingVisualizerSync"));
        else
            s.setValue(QStringLiteral("migration16/pendingVisualizerSync"),
                       QJsonDocument(rest).toJson(QJsonDocument::Compact));
        dispatchNextPendingVisualizerSync();
        return;
    }

    m_migration16InFlightVisualizerId = visualizerId;

    // Load the (now-corrected) shot off the background thread and
    // dispatch the PATCH from the callback. shotReady fires for every
    // shot load app-wide, so we filter on shotId and disconnect once
    // we've handled the matching one. The Connection handle is held in
    // a shared_ptr captured by the lambda so it self-destructs whether
    // the lambda is invoked (explicit disconnect below) or the context
    // object dies first (Qt auto-disconnects on `this`, then the lambda
    // copy in Qt's connection store gets cleaned up). Pre-shared_ptr
    // this used a raw `new` that leaked on context destruction.
    // Note: requestShot does NOT emit shotReady on a DB-open FAILURE (only on a
    // genuine load — found or not-found). That is deliberate: the !isValid()
    // branch below permanently drops the pending entry, so a transient open
    // failure must not reach it. On such a failure this connection simply never
    // fires; the queue stalls and resumes next boot. Do not "fix" that stall by
    // making requestShot emit an empty projection on open failure — it would
    // silently discard the sync. (See ShotHistoryStorage::requestShot.)
    QPointer<MainController> self(this);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_shotHistory, &ShotHistoryStorage::shotReady, this,
        [self, conn, shotId, visualizerId](qint64 readyId, const ShotProjection& shot) {
        if (!self || readyId != shotId) return;
        QObject::disconnect(*conn);
        if (!shot.isValid()) {
            qWarning() << "MainController: migration16 sync — shot" << shotId << "no longer exists; dropping";
            // Pop the bad entry and continue.
            QSettings ss(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
            QJsonArray remain = QJsonDocument::fromJson(
                ss.value(QStringLiteral("migration16/pendingVisualizerSync")).toByteArray()).array();
            if (!remain.isEmpty()) remain.removeFirst();
            if (remain.isEmpty())
                ss.remove(QStringLiteral("migration16/pendingVisualizerSync"));
            else
                ss.setValue(QStringLiteral("migration16/pendingVisualizerSync"),
                            QJsonDocument(remain).toJson(QJsonDocument::Compact));
            self->m_migration16InFlightVisualizerId.clear();
            self->dispatchNextPendingVisualizerSync();
            return;
        }
        qDebug() << "MainController: migration16 sync — re-PATCHing visualizerId" << visualizerId
                 << "with corrected enjoyment" << shot.enjoyment0to100;
        self->m_visualizer->updateShotOnVisualizer(visualizerId, shot);
    });
    m_shotHistory->requestShot(shotId);
}

void MainController::processVisualizerReconciliation()
{
    if (!m_visualizer || !m_shotHistory) return;

    QSettings s(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
    if (s.value(QStringLiteral("visualizerBackfill/doneV1"), false).toBool())
        return;  // already reconciled on this device

    const QString user = s.value(QStringLiteral("visualizer/username")).toString();
    const QString pass = s.value(QStringLiteral("visualizer/password")).toString();
    if (user.isEmpty() || pass.isEmpty()) {
        // No credentials: skip WITHOUT setting the run-once flag so it
        // retries on a later boot once an account is configured.
        qDebug() << "MainController: Visualizer reconciliation skipped (no credentials)";
        return;
    }

    // Bounded window. Widening this later requires bumping the run-once
    // key to doneV2 (this device already has doneV1 set after one pass).
    constexpr qint64 kReconcileWindowDays = 60;
    const qint64 windowStartEpoch =
        QDateTime::currentSecsSinceEpoch() - kReconcileWindowDays * 24 * 3600;

    // Fetch → reconcile → self-correct, each a single-shot hop.
    connect(m_visualizer, &VisualizerUploader::shotListFailed, this,
            [](const QString& err) {
        // Fail safe: do NOT set the run-once flag — retried next boot.
        qWarning() << "MainController: Visualizer reconciliation list fetch failed:"
                   << err << "(will retry next boot)";
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

    connect(m_visualizer, &VisualizerUploader::shotListFetched, this,
            [this, windowStartEpoch](const QVariantList& cloudShots) {
        if (m_shotHistory && m_shotHistory->isReady())
            m_shotHistory->requestReconcileVisualizerLinks(cloudShots, windowStartEpoch);
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

    connect(m_shotHistory, &ShotHistoryStorage::visualizerLinksReconciled, this,
            [this](bool ok, const QVariantList& linked) {
        if (!ok) {
            // DB open / SQL failure — NOT a completed pass. Leave the
            // run-once flag unset so it retries on the next boot rather
            // than permanently skipping the backfill after one transient
            // hiccup (e.g. DB momentarily locked at boot).
            qWarning() << "MainController: Visualizer reconciliation did not "
                          "complete (DB error) — will retry next boot";
            return;
        }
        // Genuinely completed pass — safe to set the run-once flag.
        QSettings ss(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
        ss.setValue(QStringLiteral("visualizerBackfill/doneV1"), true);
        ss.sync();

        if (linked.isEmpty()) {
            qDebug() << "MainController: Visualizer reconciliation — nothing to relink";
            return;
        }
        // Push the now-authoritative local rating to each freshly
        // linked cloud shot by appending to the same serial drain queue
        // the migration16 sync uses (load shot → PATCH local rating;
        // a cleared rating goes up as JSON null). Unconditional per
        // linked row — the list API doesn't return the cloud rating and
        // the PATCH is idempotent over this bounded set.
        QJsonArray queue = QJsonDocument::fromJson(
            ss.value(QStringLiteral("migration16/pendingVisualizerSync")).toByteArray()).array();
        for (const QVariant& v : linked) {
            const QVariantMap m = v.toMap();
            QJsonObject e;
            e["shotId"] = m.value("shotId").toLongLong();
            e["visualizerId"] = m.value("visualizerId").toString();
            queue.append(e);
        }
        ss.setValue(QStringLiteral("migration16/pendingVisualizerSync"),
                    QJsonDocument(queue).toJson(QJsonDocument::Compact));
        ss.sync();
        qDebug() << "MainController: Visualizer reconciliation linked"
                 << linked.size() << "shot(s); queued for rating push";
        dispatchNextPendingVisualizerSync();
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

    qDebug() << "MainController: starting one-time Visualizer reconciliation (window"
             << kReconcileWindowDays << "days)";
    m_visualizer->fetchShotListSince(windowStartEpoch);
}

void MainController::setRefractometer(RefractometerDevice* refractometer) {
    // Disconnect old signal chain before connecting new one
    if (m_refractometer) {
        disconnect(m_refractometer, nullptr, this, nullptr);
    }
    m_refractometer = refractometer;
    if (!m_refractometer) return;

    // Non-mutating log only. PostShotReviewPage owns context-gated capture; do
    // not write to Settings here — device-initiated readings would leak forward.
    connect(m_refractometer, &RefractometerDevice::tdsChanged, this, [](double tds) {
        qDebug() << "[Refractometer] tdsChanged" << tds;
    });
}


