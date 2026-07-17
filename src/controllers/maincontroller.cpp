#include "core/settings_app.h"
#include "maincontroller.h"
#include <QUuid>
#include "shottimingcontroller.h"
#include "autoflowcalclassifier.h"
#include "abortedshotclassifier.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_dye.h"
#include "../core/yieldspec.h"
#include "../core/drinktypes.h"
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
#include "../history/recipepromotion.h"
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
#include <QSqlQuery>
#include <optional>
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

    // Recipe storage shares the same database (recipes table, migration 25).
    // Wired BEFORE the clearBrewOverrides connection below on purpose: the
    // deactivate-on-bag-swap watcher inside must see the swap first, so the
    // override reset that follows a bag change cannot be stamped onto a
    // recipe the user is in the act of leaving (add-recipes).
    m_recipeStorage = new RecipeStorage(this);
    m_recipeStorage->initialize(m_shotHistory->databasePath());
    // Migration 31's deferred temp-offset conversion must be queued on the
    // serialized recipe worker BEFORE setupRecipeConnections() — whose tail
    // enqueues the startup active-recipe restore read — so that read (and
    // every later one; the worker is FIFO) sees converted values. Queued
    // after, the first post-upgrade launch would cache the active recipe
    // with tempOffsetC 0 and paint its designed temperature as a phantom
    // override for a whole session. ProfileManager scanned its catalog in
    // its constructor, so the title→temperature snapshot is ready.
    requestRecipeTempOffsetConversion();
    setupRecipeConnections();

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

    // A user bean switch carries the bag's yield spec (fires only from
    // applyActiveBag, never on a keep-fields historical/favorite load). It
    // arrives after the clear-to-profile reset above, so a bag with a saved
    // anchor re-establishes it (idle brew-settings widget turns yellow); a
    // bag whose mode is "none" leaves the brew at the profile default the
    // clear set.
    //
    // This walks the FULL ladder (recipe -> bag -> profile) rather than just
    // vetoing the bag while a recipe is active. The veto looked equivalent
    // and was not: the activeBagIdChanged clear above has already wiped the
    // session anchor by the time this runs, so declining to re-arm doesn't
    // leave the recipe's anchor standing — it leaves NOTHING standing, and
    // targetWeight() drops to the profile while every surface keeps
    // rendering the recipe's ratio (activeBaselineYieldMode reads the recipe
    // row, not the session). Whatever this handler declines to arm, the
    // clear has already taken away.
    //
    // A bean-linked recipe deactivates on the bag change (the
    // activeBagIdChanged handler below) before this fires, so it lands in
    // the no-recipe branch. The recipe branches below are what a recipe with
    // NO bean link needs — it survives the switch and must keep its anchor —
    // and what the spec's "recipe mode none falls through to the bag" rung
    // needs.
    connect(m_settings->dye(), &SettingsDye::activeBagYieldSpecApplied, this,
            [this](double value, const QString& mode) {
        if (!m_settings || !m_settings->brew())
            return;
        if (m_settings->dye()->activeRecipeId() >= 0 && !m_activeRecipe.isEmpty()) {
            const QString recipeMode =
                YieldSpec::normalizedMode(m_activeRecipe.value("yieldMode").toString());
            const double recipeValue = m_activeRecipe.value("yieldValue").toDouble();
            if (YieldSpec::isSet(recipeMode) && recipeValue > 0) {
                // Recipe outranks bag — re-arm the RECIPE's own anchor, which
                // the bag-switch clear just wiped.
                m_settings->brew()->setBrewYieldAnchor(recipeValue, recipeMode);
                return;
            }
            // The recipe designs no yield: the ladder falls through to the
            // bag rung below, exactly as with no recipe active.
        }
        if (value > 0 && mode != QLatin1String("none"))
            m_settings->brew()->setBrewYieldAnchor(value, mode);
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
    // list now — guarded internally on credentials being available.
    // Transiently-failed entries persist for the next boot; permanently
    // failed ones (404) are evicted (see updateFailed connect below).
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
    connect(m_visualizer, &VisualizerUploader::updateFailed, this,
            [this](const QString& visualizerId, bool permanent, const QString& error) {
        // Same in-flight filter as updateSuccess above: ignore failures
        // from PATCHes we didn't issue (user edits from the review pages).
        if (m_migration16InFlightVisualizerId.isEmpty()
            || visualizerId != m_migration16InFlightVisualizerId)
            return;
        m_migration16InFlightVisualizerId.clear();

        if (!permanent) {
            // Transient (anything but 404 — see the classification in
            // VisualizerUploader::onUpdateFinished): leave the entry in
            // the pending list and abort the drain — the queue picks up
            // on the next boot.
            qDebug() << "MainController: migration16 sync — transient failure ("
                     << error << "); drain paused until next boot";
            return;
        }

        // Permanent (HTTP 404): the shot does not exist on Visualizer and
        // never will — e.g. a bogus placeholder visualizer_id that
        // migration 16 queued in good faith. Retrying forever wastes a
        // network round-trip and logs an error on every launch (#1431):
        // evict the entry, clear the dead link on the local shot row so
        // nothing else trusts it, and continue draining. The clear is
        // guarded on the row still holding this exact id, so a link the
        // user replaced meanwhile (re-upload) is never wiped.
        QSettings s(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
        QJsonArray pending = QJsonDocument::fromJson(
            s.value(QStringLiteral("migration16/pendingVisualizerSync")).toByteArray()).array();
        for (qsizetype i = 0; i < pending.size(); ++i) {
            const QJsonObject entry = pending[i].toObject();
            if (entry.value("visualizerId").toString() != visualizerId)
                continue;
            const qint64 shotId = entry.value("shotId").toVariant().toLongLong();
            if (m_shotHistory)
                m_shotHistory->requestClearStaleVisualizerLink(shotId, visualizerId);
            pending.removeAt(i);
            break;
        }
        if (pending.isEmpty())
            s.remove(QStringLiteral("migration16/pendingVisualizerSync"));
        else
            s.setValue(QStringLiteral("migration16/pendingVisualizerSync"),
                       QJsonDocument(pending).toJson(QJsonDocument::Compact));
        qWarning() << "MainController: migration16 sync — dropping visualizerId"
                   << visualizerId << "after permanent failure:" << error;
        dispatchNextPendingVisualizerSync();
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
    // MainController for the recipes/bags/equipment web surfaces (add-recipes)
    m_shotServer->setMainController(this);

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

    // Recipe-aware brew baseline (recipe-baseline-not-override, #1485): the
    // effective baseline + real-override flags change with the active recipe, the
    // live brew overrides, and the profile's own target/temp. Relay all of those
    // into one signal so the baseline Q_PROPERTYs re-evaluate everywhere at once.
    connect(this, &MainController::activeRecipeChanged, this, &MainController::brewBaselineChanged);
    connect(m_settings->brew(), &SettingsBrew::temperatureOverrideChanged, this, &MainController::brewBaselineChanged);
    connect(m_settings->brew(), &SettingsBrew::brewOverridesChanged, this, &MainController::brewBaselineChanged);
    connect(m_profileManager, &ProfileManager::currentProfileChanged, this, &MainController::brewBaselineChanged);
    connect(m_profileManager, &ProfileManager::targetWeightChanged, this, &MainController::brewBaselineChanged);
    // The bag rung of the yield-baseline ladder: a bean switch, an Update Bag
    // write, or the keep-fields cache refresh on recipe activation all move
    // the baseline spec even with no recipe active. activeBagYieldSpecChanged
    // covers every one (activeBagYieldSpecApplied deliberately skips the
    // keep-fields path, so it is the wrong signal to hang this on).
    connect(m_settings->dye(), &SettingsDye::activeBagYieldSpecChanged, this,
            &MainController::brewBaselineChanged);
    connect(m_settings->dye(), &SettingsDye::activeBagIdChanged, this, &MainController::brewBaselineChanged);

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

    // Re-run the temp-offset conversion after any import that can land
    // legacy-source rows (the startup pass ran before setupRecipeConnections,
    // see the constructor's storage block). A device transfer imports profile
    // FILES too, and this C++ connect fires before the QML page's
    // onImportComplete → ProfileManager.refreshProfiles() — so rescan the
    // catalog HERE first, or the conversion would snapshot the pre-import
    // catalog, fail to resolve the transferred recipes' profiles, and drop
    // their temperature pins permanently.
    connect(m_shotHistory, &ShotHistoryStorage::importDatabaseFinished, this,
            [this](bool success) {
        if (success)
            requestRecipeTempOffsetConversion();
    });
    connect(m_dataMigration, &DataMigrationClient::importComplete, this,
            [this]() {
        if (m_profileManager)
            m_profileManager->refreshProfiles();
        requestRecipeTempOffsetConversion();
    });
}

void MainController::requestRecipeTempOffsetConversion() {
    if (!m_recipeStorage || !m_profileManager)
        return;
    QHash<QString, double> tempsByTitle;
    const QList<ProfileInfo>& all = m_profileManager->allProfiles();
    tempsByTitle.reserve(all.size());
    for (const ProfileInfo& info : all) {
        if (info.espressoTemperature > 0)
            tempsByTitle.insert(info.title, info.espressoTemperature);
    }
    m_recipeStorage->requestLegacyTempOffsetConversion(tempsByTitle);
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

        // Apply brew overrides from history on top of profile defaults (set by loadProfile).
        // A frozen historical value that matches the freshly-loaded profile's own
        // default is NOT an override (Bug A: pre-fix shots saved the default here) —
        // only a genuinely-different value arms the flag, so the Shot Plan highlight
        // can't latch on from a coincidental snapshot.
        bool hasOverrides = false;
        if (shotRecord.temperatureOverride > 0
            && qAbs(shotRecord.temperatureOverride
                    - m_profileManager->currentProfile().espressoTemperature()) > 0.1) {
            m_settings->brew()->setTemperatureOverride(shotRecord.temperatureOverride);
            hasOverrides = true;
        }

        // Yield restore is anchor-aware (add-yield-ratio-anchor): a shot that
        // genuinely carried a RATIO restores the ratio — 1:2 against today's
        // dose — which is what "brew this again" means for a ratio shot (and
        // consistent with a ratio surviving profile loads). Absolute shots —
        // including every legacy shot, whose backfilled "absolute" anchor may
        // really be a volume-profile fabrication — restore frozen grams
        // exactly as before, with the Bug-A comparison surviving for them
        // alone: a frozen absolute matching the freshly-loaded profile's own
        // default is not an override.
        if (shotRecord.yieldMode == QLatin1String("ratio")
            && shotRecord.yieldAnchorValue > 0) {
            m_settings->brew()->setBrewRatioAnchor(shotRecord.yieldAnchorValue);
            hasOverrides = true;
        } else if (shotRecord.targetWeight > 0
            && qAbs(shotRecord.targetWeight
                    - m_profileManager->currentProfile().targetWeight()) > 0.1) {
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

// ---------------------------------------------------------------------------
// Recipes (add-recipes)
//
// A recipe is the whole drink: profile + linked bag + equipment + dose/yield/
// temp + the recipe's own grind + steam block. This is the SINGLE activation path —
// QML pill taps, MCP recipe_activate, and the web /activate route all land
// here, so activation semantics cannot drift between surfaces.
// ---------------------------------------------------------------------------

namespace {

// The recipe steam block's JSON shape, shared by activation, the shot-save
// snapshot, the composer prefill, MCP, and the web UI:
//   { "hasMilk": bool, "milkWeightG": n, "pitcherName": s,
//     "durationSec": n, "flow": n, "temperatureC": n }
QJsonObject parseSteamBlock(const QString& json) {
    if (json.isEmpty())
        return QJsonObject();
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

// The recipe hot-water block's JSON shape, shared by activation, the shot-save
// snapshot, the composer prefill, MCP, and the web UI. Hot water is opt-in and
// the selected water vessel carries the values (there is no separate per-recipe
// amount), so the block is a by-value vessel snapshot plus the on/off flag and
// a pour-order flag:
//   { "hasWater": bool, "vesselName": s, "volume": n, "mode": "weight"|"volume",
//     "flowRate": n, "temperatureC": n, "order": "before"|"after" }
// order is the drink intent: "before" = water first (a long black), "after" =
// water last (an Americano, the default). Field names mirror the steam block
// (name->vesselName, temperature->temperatureC) while keeping the vessel's native
// volume/mode/flowRate so a snapshot round-trips straight through SettingsBrew's
// water-vessel preset API. The order is guidance (surfaced in the UI), not a
// scripted two-stage pour.
QJsonObject parseHotWaterBlock(const QString& json) {
    if (json.isEmpty())
        return QJsonObject();
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

QString compactJson(const QJsonObject& o) {
    return o.isEmpty() ? QString()
                       : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

void MainController::setupRecipeConnections() {
    // Activation bundles arrive here from the storage worker.
    connect(m_recipeStorage, &RecipeStorage::recipeActivationReady, this,
            &MainController::applyActivatedRecipe);

    // --- Relink lifecycle (recipe-bag-lifecycle): recipes follow bag
    // inventory events, silently and dup-guarded — roll-on-finish when a
    // bag leaves inventory, wake-on-restock when a new bag arrives. Pure
    // event hooks on the storage signals (no polling, no timers); the
    // courtesy toast lives in main.qml on recipesRelinked.
    connect(m_bagStorage, &CoffeeBagStorage::bagFinished, this, [this](qint64 bagId) {
        m_recipeStorage->requestRelinkForFinishedBag(bagId);
    });
    connect(m_bagStorage, &CoffeeBagStorage::bagCreated, this,
            [this](qint64 bagId, const QVariantMap&) {
        if (bagId > 0)
            m_recipeStorage->requestRelinkForRestockedBag(bagId);
    });
    // A bag RETURNING to inventory (un-finished via MCP/web update) wakes
    // stale siblings exactly like a new bag — idempotent + dup-guarded.
    connect(m_bagStorage, &CoffeeBagStorage::bagRestocked, this, [this](qint64 bagId) {
        m_recipeStorage->requestRelinkForRestockedBag(bagId);
    });
    // When an automatic relink moved the ACTIVE recipe, refresh its cache so
    // the write-through stamps and the deactivate watchers see the new bag link.
    connect(m_recipeStorage, &RecipeStorage::recipesRelinked, this,
            [this](const QVariantList& movedRecipeIds, qint64, const QString&) {
        const qint64 activeId = m_settings->dye()->activeRecipeId();
        if (activeId <= 0)
            return;
        for (const QVariant& moved : movedRecipeIds) {
            if (moved.toLongLong() == activeId) {
                m_recipeStorage->requestRecipe(activeId);
                break;
            }
        }
    });

    // Keep the active-recipe cache fresh after edits (composer, MCP, web,
    // our own stamps). recipeUpdated fires for every update, success or not.
    connect(m_recipeStorage, &RecipeStorage::recipeUpdated, this,
            [this](qint64 recipeId, bool success) {
        if (recipeId != m_settings->dye()->activeRecipeId())
            return;
        // Skip the re-read for our own write-through stamps — the cache
        // already holds those values (mirrors SettingsDye's bag echo skip).
        // Decrement even on a FAILED stamp: the counter must track every
        // stamp we issued, or a failed self-write would leak the count and
        // silently swallow the next external edit's refresh. (A leaked
        // count across a recipe switch is separately cleared in
        // applyActivatedRecipe / deactivateRecipe.)
        if (m_pendingRecipeSelfWrites > 0) {
            m_pendingRecipeSelfWrites--;
            return;
        }
        if (success) {
            // An external edit of the active recipe (wizard/MCP/web). Flag the
            // re-read so recipeReady mirrors the new grind/rpm onto the live
            // dial — the Shot Plan binds to Settings.dye, not the recipe cache,
            // so without this the plan stays stale until re-activation.
            m_refreshDialFromRecipeEdit = true;
            m_recipeStorage->requestRecipe(recipeId);
        }
    });

    // Cache refresh + startup restore both land here.
    connect(m_recipeStorage, &RecipeStorage::recipeReady, this,
            [this](qint64 recipeId, const QVariantMap& recipe) {
        if (recipeId != m_settings->dye()->activeRecipeId())
            return;
        // Consume the edit-refresh flag only for the active recipe's own
        // re-read (a concurrent non-active read returns above without touching
        // it, so it can't swallow a pending refresh).
        const bool refreshDial = m_refreshDialFromRecipeEdit;
        m_refreshDialFromRecipeEdit = false;
        if (recipe.isEmpty() || recipe.value("archived").toBool()) {
            // Row vanished or was archived out from under the selection.
            deactivateRecipe();
            return;
        }
        const qint64 resolvedBagId = m_activeRecipe.value(
            QStringLiteral("resolvedBagId"), m_settings->dye()->activeBagId()).toLongLong();
        const bool hadMilk = activeRecipeHasMilk();
        m_activeRecipe = recipe;
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), resolvedBagId);
        emit activeRecipeChanged();
        // Re-assert the heater hold when hasMilk changed (composer/MCP edit
        // of the active recipe) or on the startup restore of a milk recipe —
        // the 5-9 minute warm-up means the hold must follow the cache.
        if (activeRecipeHasMilk() != hadMilk || activeRecipeHasMilk())
            applySteamSettings();
        // Re-seed the brew overrides from the edited recipe, exactly as
        // re-activating it would (add-yield-ratio-anchor). An edit changes the
        // recipe's DESIGN, and the live setup must follow it — the grind push
        // below has always done this; yield/temperature were left behind,
        // stranding the value activation armed. Edit the yield to 50 and the
        // plan would read an amber "50.0 -> 36.0g" while the shot still
        // targeted the old 36: the recipe reading as an override of itself.
        //
        // "Clear the overrides" in the ladder's sense = back to the store's
        // own values, which is Clear's meaning in Brew Settings — NOT a bare
        // wipe, which would drop the brew to the profile rather than to the
        // edited recipe (targetWeight() resolves the session anchor; it never
        // re-reads the recipe). Only on an actual external edit — our own
        // dose/grind stamps return above without re-reading, so a
        // write-through can't bounce back and wipe the user's dialed tweak.
        // Profile-less (hot-water) recipes own no profile to override.
        if (refreshDial
            && !m_activeRecipe.value(QStringLiteral("profileTitle")).toString().trimmed().isEmpty()
            && applyRecipeBrewOverrides(m_activeRecipe))
            m_profileManager->uploadCurrentProfile();

        // Mirror an edited grind/rpm back onto the live dial so the Shot Plan
        // refreshes without a re-activation (Flow-3 fix). Only on an actual
        // edit re-read; same semantics as applyActivatedRecipe's grind push:
        // grind-less drink types (tea) and an empty grind leave the dial
        // untouched. The cache is already updated above, so the resulting
        // dyeGrinderSettingChanged stamp hits stampActiveRecipe's equality
        // guard and does NOT loop back into another write.
        if (refreshDial
            && DrinkTypes::hasGrind(m_activeRecipe.value(QStringLiteral("drinkType")).toString())) {
            const QString grind = m_activeRecipe.value(QStringLiteral("grindPinned")).toString();
            if (!grind.isEmpty()) {
                m_settings->dye()->setDyeGrinderSetting(grind);
                const qint64 rpm = m_activeRecipe.value(QStringLiteral("rpmPinned")).toLongLong();
                if (rpm > 0)
                    m_settings->dye()->setDyeGrinderRpm(static_cast<int>(rpm));
            }
        }
    });

    // --- Deactivate on ingredient swaps (tweaks refine the recipe; swapping
    // an ingredient means the user has left it). Each watcher compares the
    // new value against the recipe's OWN ingredient, so re-selecting the
    // same thing (or the startup auto-load of the recipe's profile) never
    // deactivates, and a recipe without that rung doesn't own the choice.
    connect(m_settings->dye(), &SettingsDye::activeBagIdChanged, this, [this]() {
        if (m_applyingRecipe || m_activeRecipe.isEmpty())
            return;
        const bool hasBeanLink = m_activeRecipe.value("bagId").toLongLong() > 0
            || !m_activeRecipe.value("beanBaseId").toString().isEmpty()
            || !m_activeRecipe.value("roasterName").toString().isEmpty()
            || !m_activeRecipe.value("coffeeName").toString().isEmpty();
        if (!hasBeanLink)
            return;
        if (m_settings->dye()->activeBagId()
            != m_activeRecipe.value(QStringLiteral("resolvedBagId")).toLongLong())
            deactivateRecipe();
    });
    connect(m_settings->dye(), &SettingsDye::activeEquipmentIdChanged, this, [this]() {
        if (m_applyingRecipe || m_activeRecipe.isEmpty())
            return;
        const qint64 recipeEq = m_activeRecipe.value("equipmentId").toLongLong();
        if (recipeEq > 0 && m_settings->dye()->activeEquipmentId() != recipeEq)
            deactivateRecipe();
    });
    connect(m_profileManager, &ProfileManager::currentProfileChanged, this, [this]() {
        if (m_applyingRecipe || m_activeRecipe.isEmpty())
            return;
        if (m_profileManager->currentProfile().title()
            != m_activeRecipe.value("profileTitle").toString())
            deactivateRecipe();
    });

    // --- Write-through stamps: tweaks while a recipe is active refine the
    // recipe (bag-style, no dirty state). All gated inside stampActiveRecipe
    // on active-recipe presence and the m_applyingRecipe guard.
    connect(m_settings->dye(), &SettingsDye::dyeBeanWeightChanged, this, [this]() {
        stampActiveRecipe(QStringLiteral("doseG"), m_settings->dye()->dyeBeanWeight());
    });
    // Yield/temp are per-brew OVERRIDES, not tweaks: they live in Settings.brew
    // only and are never auto-stamped onto the recipe from the live dial
    // (recipe-aware-brew-settings). The recipe's yieldG/tempOffsetC change
    // only through explicit recipe edits — Brew Settings' "Update Recipe"
    // button, the composer, MCP/web recipe_update — mirroring how a profile's
    // target/temperature never follow the dial either.
    // Grind/rpm edits always stamp the active recipe's own grind (grind lives
    // on the recipe, fix-recipe-grind-integrity) — in parallel with SettingsDye's
    // unconditional bag write-through off the same edit. Grind-less drink
    // types skip the stamp (DrinkTypes::hasGrind, incl. its legacy-row caveat).
    connect(m_settings->dye(), &SettingsDye::dyeGrinderSettingChanged, this, [this]() {
        if (DrinkTypes::hasGrind(m_activeRecipe.value("drinkType").toString()))
            stampActiveRecipe(QStringLiteral("grindPinned"), m_settings->dye()->dyeGrinderSetting());
    });
    connect(m_settings->dye(), &SettingsDye::dyeGrinderRpmChanged, this, [this]() {
        if (DrinkTypes::hasGrind(m_activeRecipe.value("drinkType").toString()))
            stampActiveRecipe(QStringLiteral("rpmPinned"), m_settings->dye()->dyeGrinderRpm());
    });
    // Steam tweaks (pitcher selection/edits, milk weight) refresh the block.
    connect(m_settings->brew(), &SettingsBrew::selectedSteamPitcherChanged, this,
            [this]() { stampActiveRecipeSteam(); });
    connect(m_settings->brew(), &SettingsBrew::steamPitcherPresetsChanged, this,
            [this]() { stampActiveRecipeSteam(); });
    connect(m_settings->brew(), &SettingsBrew::lastSteamMilkGChanged, this,
            [this]() { stampActiveRecipeSteam(); });

    // Hot-water tweaks (vessel selection/edits) refresh the block the same way.
    // Only fires for a hot-water recipe: stampActiveRecipeHotWater re-snapshots
    // the selected vessel, and stampActiveRecipe's equality guard means
    // re-selecting the same vessel is a no-op (never deactivates).
    connect(m_settings->brew(), &SettingsBrew::selectedWaterVesselChanged, this,
            [this]() { stampActiveRecipeHotWater(); });
    connect(m_settings->brew(), &SettingsBrew::waterVesselPresetsChanged, this,
            [this]() { stampActiveRecipeHotWater(); });

    // selectedRecipeId (the synchronous pill-selection marker) follows
    // activeRecipeId in steady state: a successful activation sets activeRecipeId
    // (confirming our optimistic lead), and an external deactivation clears it —
    // both should move the pill highlight. activateRecipe() sets the optimistic
    // lead; this keeps it honest afterwards.
    connect(m_settings->dye(), &SettingsDye::activeRecipeIdChanged, this, [this]() {
        if (m_recipeSelection.onActiveRecipeChanged(m_settings->dye()->activeRecipeId()))
            emit selectedRecipeIdChanged();
    });
    // A FAILED activation never changes activeRecipeId (the connect above won't
    // fire), so the model rolls the optimistic selection back — otherwise a
    // second tap would start a shot for a recipe that never applied. The same
    // event also resolves a deferred start armed by startSelectedRecipeShotWhenApplied.
    connect(this, &MainController::recipeActivated, this, [this](qint64 recipeId, bool success) {
        const auto o = m_recipeSelection.onActivationResult(
            recipeId, success, m_settings->dye()->activeRecipeId());
        if (o.reverted)
            qWarning() << "[recipe] activation failed for" << recipeId
                       << "- reverting selection to active recipe"
                       << m_settings->dye()->activeRecipeId();
        if (o.selectedChanged)
            emit selectedRecipeIdChanged();
        if (o.fireStart && m_device) {
            qDebug() << "[recipe] activation applied — pulling the deferred shot for" << recipeId;
            m_device->startEspresso();
        } else if (o.fireStart || o.startDropped) {
            qWarning() << "[recipe] deferred shot not pulled for" << recipeId
                       << "(success=" << success << "device=" << (m_device != nullptr) << ")";
        }
    });

    // Startup restore: the persisted selection survives a restart (the live
    // settings already persist on their own — nothing is re-applied; this
    // only restores the pill highlight and the active-recipe cache).
    const int savedRecipeId = m_settings->dye()->activeRecipeId();
    m_recipeSelection.reset(savedRecipeId);
    if (savedRecipeId > 0)
        m_recipeStorage->requestRecipe(savedRecipeId);
}

void MainController::activateRecipe(qint64 recipeId) {
    if (!m_recipeStorage) {
        qWarning() << "[recipe] activateRecipe" << recipeId << "- no recipe storage, activation failed";
        emit recipeActivated(recipeId, false);
        return;
    }
    // Optimistically select it NOW (synchronous), before the async DB read +
    // BLE upload, so the "tap the selected pill again to start" gesture works on
    // the very next tap. The model also cancels any deferred start armed for a
    // different recipe. Confirmed on success / rolled back on failure (see the
    // activeRecipeIdChanged + recipeActivated connects in the constructor).
    qDebug() << "[recipe] activateRecipe" << recipeId << "- selecting + requesting activation";
    if (m_recipeSelection.onActivate(recipeId))
        emit selectedRecipeIdChanged();
    // Same-id re-activation (re-tapping the active recipe, e.g. after an edit
    // to push its values to the Shot Plan — the #1466/#1471 flow): re-push the
    // in-memory cache through the apply stages instead of doing a fresh DB
    // read. The cache already holds our own optimistic stamps AND external
    // edits (the recipeUpdated re-read keeps it fresh), while a fresh read can
    // race an in-flight write-through of the user's own just-made edit and
    // revert it (Bug B2, fix-recipe-grind-integrity). The bag map is passed
    // EMPTY on purpose: the bag is already the active bag with live identity
    // fields — applyActivatedRecipe skips the identity re-apply for an empty
    // map, so a stale or restart-restored cache can never write old (or
    // blank) bean fields through to the bag row. First activation and any
    // different-id activation still read fresh — and so does a bag-link
    // change since activation (relink / re-point): resolvedBagId is then
    // stale, so bagId != resolvedBagId falls through to the full fresh read.
    if (m_settings && recipeId == m_settings->dye()->activeRecipeId()
        && !m_activeRecipe.isEmpty()
        && m_activeRecipe.value(QStringLiteral("bagId")).toLongLong()
               == m_activeRecipe.value(QStringLiteral("resolvedBagId")).toLongLong()) {
        applyActivatedRecipe(recipeId, m_activeRecipe,
                             m_activeRecipe.value(QStringLiteral("resolvedBagId")).toLongLong(),
                             QVariantMap());
        return;
    }
    m_recipeStorage->requestRecipeForActivation(recipeId);
}

void MainController::startSelectedRecipeShotWhenApplied() {
    if (!m_device)
        return;
    switch (m_recipeSelection.requestStart(m_settings->dye()->activeRecipeId())) {
    case RecipeSelectionModel::StartDecision::StartNow:
        qDebug() << "[recipe] starting espresso for applied recipe" << m_recipeSelection.selected();
        m_device->startEspresso();
        break;
    case RecipeSelectionModel::StartDecision::Deferred:
        qDebug() << "[recipe] start armed — waiting for recipe" << m_recipeSelection.selected()
                 << "to finish applying before pulling the shot";
        break;
    case RecipeSelectionModel::StartDecision::None:
        break;
    }
}

void MainController::checkRecipesUpgradeEligibility() {
    if (!m_shotHistory) {
        m_recipesUpgradeWillCreate = false;
        m_recipesUpgradeShotRecord = ShotRecord();
        emit recipesUpgradeOfferReady(false, false);
        return;
    }
    const QString dbPath = m_shotHistory->databasePath();
    auto record = std::make_shared<ShotRecord>();
    auto recipeCount = std::make_shared<qint64>(0);
    auto shotId = std::make_shared<qint64>(-1);
    // Tracks whether the recipe count is trustworthy — a failed/unopened
    // query must never be read as "zero recipes" (that would offer, and on
    // accept create, a spurious duplicate starter recipe for a user who
    // already has some).
    auto recipeCountOk = std::make_shared<bool>(false);
    QThread* thread = QThread::create([dbPath, record, recipeCount, shotId, recipeCountOk]() {
        const bool opened = withTempDb(dbPath, "recipes_upgrade_offer", [&](QSqlDatabase& db) {
            QSqlQuery countQuery(db);
            *recipeCountOk = countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM recipes"))
                && countQuery.next();
            if (*recipeCountOk) {
                *recipeCount = countQuery.value(0).toLongLong();
            } else {
                qWarning() << "checkRecipesUpgradeEligibility: recipe count query failed:"
                           << countQuery.lastError().text();
            }

            QSqlQuery latestQuery(db);
            if (latestQuery.exec(QStringLiteral("SELECT id FROM shots ORDER BY timestamp DESC LIMIT 1"))
                && latestQuery.next())
                *shotId = latestQuery.value(0).toLongLong();

            if (*shotId > 0)
                *record = ShotHistoryStorage::loadShotRecordStatic(db, *shotId);
        });
        if (!opened) {
            *recipeCountOk = false;
            qWarning() << "checkRecipesUpgradeEligibility: could not open shot history DB";
        }
    });
    connect(thread, &QThread::finished, this, [this, record, recipeCount, shotId, recipeCountOk]() {
        m_recipesUpgradeWillCreate = RecipePromotion::isEligibleForStarterRecipe(
            *recipeCountOk, *recipeCount, *shotId, record->summary.id);
        m_recipesUpgradeShotRecord = m_recipesUpgradeWillCreate ? *record : ShotRecord();

        const bool milkPreselected = m_recipesUpgradeWillCreate
            && RecipePromotion::milkPreselectedFromSteamJson(m_recipesUpgradeShotRecord.steamJson);
        emit recipesUpgradeOfferReady(m_recipesUpgradeWillCreate, milkPreselected);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainController::acceptRecipesFirstUpgrade(const QString& name, bool hasMilk) {
    if (m_settings && m_settings->network()) {
        m_settings->network()->applyRecipesFirstUpgrade();
        m_settings->network()->setRecipesUpgradeOffered(true);
    }

    if (!m_recipesUpgradeWillCreate || !m_recipeStorage) {
        emit recipesUpgradeApplied(QString(), false);
        return;
    }

    QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
        m_recipesUpgradeShotRecord, name, std::optional<bool>(hasMilk), currentSteamSpecJson());
    // Correlation token: recipeCreated is a broadcast — a concurrent MCP/web
    // create (or its failure) must not be mistaken for the starter recipe.
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    fields.insert(QStringLiteral("requestToken"), token);

    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_recipeStorage, &RecipeStorage::recipeCreated, this,
        [this, conn, name, token](qint64 recipeId, const QVariantMap& recipe) {
            if (recipe.value(QStringLiteral("requestToken")).toString() != token)
                return;
            QObject::disconnect(*conn);
            if (recipeId > 0) {
                activateRecipe(recipeId);
                emit recipesUpgradeApplied(name, false);
            } else {
                // Requested but failed (RecipeStorage logs the cause) — must
                // not look like "no starter recipe was requested" to the UI.
                emit recipesUpgradeApplied(QString(), true);
            }
        });
    m_recipeStorage->requestCreateRecipe(fields);
}

void MainController::applyActivatedRecipe(qint64 recipeId, const QVariantMap& recipe,
                                          qint64 linkedBagId, const QVariantMap& linkedBag) {
    if (recipe.isEmpty()) {
        qWarning() << "applyActivatedRecipe: recipe" << recipeId << "not found";
        emit recipeActivated(recipeId, false);
        return;
    }

    // Profile — installed by title, stored JSON as fallback (the same rule
    // as applyLoadedShotMetadata). loadProfile resets brew overrides to the
    // profile defaults; the recipe's own overrides re-apply below.
    //
    // The profile IS the drink — if neither the titled profile is installed
    // nor a JSON fallback exists (e.g. an MCP/web recipe whose profileTitle
    // was mistyped, or the profile was later deleted), activation must FAIL,
    // not silently light up the pill while the machine keeps the previously
    // loaded profile. Bail before any state changes so the caller can report
    // the failure honestly.
    //
    // Exception (add-recipe-wizard-tea): a PROFILE-LESS recipe — no title,
    // hot-water block present — is a valid hot-water drink (tea). It skips
    // every profile-coupled stage below (profile load, dose write, yield/temp
    // overrides, steam-heater hold) and leaves the loaded espresso profile
    // untouched: the machine action is the user starting Hot Water.
    const QString profileTitle = recipe.value("profileTitle").toString();
    const QString profileJson = recipe.value("profileJson").toString();
    const bool profileLess = profileTitle.trimmed().isEmpty()
        && Recipe::hotWaterActive(recipe.value("hotWaterJson").toString());
    const QString filename = profileLess ? QString()
                                         : m_profileManager->findProfileByTitle(profileTitle);
    if (filename.isEmpty() && profileJson.isEmpty() && !profileLess) {
        qWarning() << "applyActivatedRecipe: no profile data for recipe" << recipeId
                   << "(title" << profileTitle << "not installed, no JSON) - activation failed";
        emit recipeActivated(recipeId, false);
        return;
    }

    m_applyingRecipe = true;
    // Clear any leaked self-write count from a prior recipe (a stamp whose
    // echo arrived after the recipe was deactivated/switched never got a
    // chance to decrement) so it can't swallow this recipe's first edit.
    m_pendingRecipeSelfWrites = 0;
    // Likewise drop a leaked edit-refresh flag: an edit re-read that never
    // reached recipeReady before this switch must not push a stale grind onto
    // the newly-active recipe's first read.
    m_refreshDialFromRecipeEdit = false;

    if (!profileLess) {
        if (!filename.isEmpty()) {
            m_profileManager->loadProfile(filename);
        } else {
            m_profileManager->loadProfileFromJson(profileJson);
            m_profileManager->persistCurrentProfile();
        }
    }

    if (m_settings) {
        auto* dye = m_settings->dye();

        // Bag: select keep-fields (deterministic — no async applyActiveBag
        // racing our values below), then apply the bag's OWN bean fields from
        // the bundle's snapshot. Write-throughs write the bag's values back
        // into it: no-ops. An EMPTY bag map with a positive linkedBagId is
        // the same-id re-activation contract (see activateRecipe): the bag is
        // already the active bag with live identity fields, so nothing must
        // be re-applied — applying an empty map here would write empty
        // strings THROUGH to the bag row, destroying its identity. A
        // bean-less recipe (linkedBagId <= 0) CLEARS the active bag: the
        // session must not stay attributed to — or write its grind into —
        // whatever bag happened to be laying around
        // (fix-recipe-grind-integrity). The deactivation watcher is guarded
        // by m_applyingRecipe, so the clear can't self-deactivate us.
        if (linkedBagId > 0) {
            dye->setActiveBagKeepFields(static_cast<int>(linkedBagId));
            if (!linkedBag.isEmpty()) {
                dye->setDyeBeanBrand(linkedBag.value("roasterName").toString());
                dye->setDyeBeanType(linkedBag.value("coffeeName").toString());
                dye->setDyeRoastDate(linkedBag.value("roastDate").toString());
                dye->setDyeRoastLevel(linkedBag.value("roastLevel").toString());
                dye->setDyeBeanBaseData(linkedBag.value("beanBaseData").toString());
                dye->setDyeBeanBaseId(linkedBag.value("beanBaseId").toString());
            }
        } else {
            dye->setActiveBagId(-1);
        }

        // Equipment: the recipe's own package, else the bag's.
        const qint64 equipmentId = recipe.value("equipmentId").toLongLong() > 0
            ? recipe.value("equipmentId").toLongLong()
            : linkedBag.value("equipmentId").toLongLong();
        if (equipmentId > 0)
            dye->setActiveEquipmentId(equipmentId);

        // Grind: always the recipe's own dial — grind lives on the recipe
        // (fix-recipe-grind-integrity; the bag-inherit branch and the
        // write-through suspension are retired). The setters' unconditional
        // bag write-through mirrors the value onto the linked bag: selecting
        // a recipe that selects a bag counts as dialing it (a bean-less
        // recipe's write-through hits no bag — cleared above). An empty
        // grind (grind-less tea, a never-dialed import) leaves the current
        // dial untouched rather than wiping it.
        const QString grind = recipe.value("grindPinned").toString();
        if (!grind.isEmpty()) {
            dye->setDyeGrinderSetting(grind);
            const qint64 rpm = recipe.value("rpmPinned").toLongLong();
            if (rpm > 0)
                dye->setDyeGrinderRpm(static_cast<int>(rpm));
        }

        // Dose — queued so it wins over loadProfile's own deferred
        // setDyeBeanWeight(recommendedDose) (same trick as shot load).
        // Profile-less recipes skip it: dyeBeanWeight is espresso-shot
        // metadata, and a hot-water tea's leaf dose is not a shot dose.
        const double doseG = recipe.value("doseG").toDouble();
        if (doseG > 0 && !profileLess) {
            QPointer<Settings> settings(m_settings);
            QMetaObject::invokeMethod(this, [settings, doseG]() {
                if (settings) settings->dye()->setDyeBeanWeight(doseG);
            }, Qt::QueuedConnection);
        }

        // Yield / temperature overrides on top of the profile defaults.
        // Profile-less recipes have no profile to override or re-upload.
        // Shared with the active-recipe EDIT refresh — see the helper.
        if (!profileLess && applyRecipeBrewOverrides(recipe, linkedBag))
            m_profileManager->uploadCurrentProfile();

        // Steam block: pitcher (the pitcher preset IS the steam spec —
        // duration/flow/temperature live on it), milk weight, heater intent.
        const QJsonObject steam = parseSteamBlock(recipe.value("steamJson").toString());
        if (!steam.isEmpty()) {
            auto* brew = m_settings->brew();
            const QString pitcherName = steam.value("pitcherName").toString();
            if (!pitcherName.isEmpty()) {
                const QVariantList presets = brew->steamPitcherPresets();
                int index = -1;
                for (int i = 0; i < presets.size(); ++i) {
                    if (presets.at(i).toMap().value("name").toString()
                            .compare(pitcherName, Qt::CaseInsensitive) == 0) {
                        index = i;
                        break;
                    }
                }
                if (index < 0) {
                    // The snapshotted pitcher was deleted — resurrect it from
                    // the recipe's own values so the drink steams as saved
                    // (snapshot-not-reference; visible, not silent).
                    brew->addSteamPitcherPreset(pitcherName,
                                                steam.value("durationSec").toInt(),
                                                steam.value("flow").toInt(),
                                                steam.value("temperatureC").toDouble());
                    index = static_cast<int>(brew->steamPitcherPresets().size()) - 1;
                    qDebug() << "applyActivatedRecipe: recreated deleted pitcher" << pitcherName;
                }
                brew->setSelectedSteamCup(index);
            }
            const double milkG = steam.value("milkWeightG").toDouble();
            if (milkG > 0)
                brew->setLastSteamMilkG(milkG);
        }

        // Cache before the heater derivation below: sendMachineSettings
        // reads activeRecipeHasMilk() from this cache. Safe — the watchers
        // are still behind the m_applyingRecipe guard.
        m_activeRecipe = recipe;
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), linkedBagId);

        // Heater intent derives from hasMilk — no new setting. The steam
        // heater takes 5-9 MINUTES to warm, so a milk recipe HOLDS the
        // heater on for as long as it is active and the machine is awake:
        // sendMachineSettings treats an active milk recipe like
        // keepSteamHeaterOn, so every later settings re-send (wake,
        // reconnect, edits) keeps it warm. A milk-less recipe returns the
        // heater to the user's baseline. Never fights an explicit keep-on.
        // Profile-less (hot-water) recipes never take the hold — hot water
        // needs no pre-warm, and activeRecipeHasMilk() mirrors this rule so
        // later sendMachineSettings re-sends don't re-assert it either.
        if (steam.value("hasMilk").toBool() && !profileLess)
            startSteamHeating(QStringLiteral("recipe-activated"));
        else
            applySteamSettings();

        // Hot-water block (Americano): opt-in, vessel-carried. Re-select the
        // snapshotted vessel by name so its values become the live hot-water
        // settings; recreate the preset from the snapshot if it was deleted
        // (snapshot-not-reference, mirroring the pitcher path above). Unlike
        // steam there is NO heater hold — hot water needs no multi-minute
        // pre-warm — so a milk-less hot-water recipe never lit the steam heater
        // (the branch above already ran applySteamSettings for it).
        const QJsonObject water = parseHotWaterBlock(recipe.value("hotWaterJson").toString());
        // Require a vessel: hasWater with no vessel is an incomplete block (the
        // user toggled it on but never picked one) — applying it would push a
        // 0-volume hot-water target. Leave the live settings at the user's
        // baseline instead.
        if (water.value("hasWater").toBool() && !water.value("vesselName").toString().isEmpty()) {
            auto* brew = m_settings->brew();
            const QString vesselName = water.value("vesselName").toString();
            // Block values are the snapshot (composer). A name-only block (the
            // web form stores just the name, mirroring the steam pitcher form)
            // has no positive volume — resolve those from the live vessel below.
            int volume = water.value("volume").toInt();
            QString mode = water.value("mode").toString(QStringLiteral("weight"));
            int flowRate = water.value("flowRate").toInt(40);
            double tempC = water.value("temperatureC").toDouble(brew->waterTemperature());
            const bool blockHasValues = volume > 0;
            if (!vesselName.isEmpty()) {
                const QVariantList vessels = brew->waterVesselPresets();
                int index = -1;
                for (int i = 0; i < vessels.size(); ++i) {
                    if (vessels.at(i).toMap().value("name").toString()
                            .compare(vesselName, Qt::CaseInsensitive) == 0) {
                        index = i;
                        break;
                    }
                }
                if (index < 0) {
                    // The snapshotted vessel was deleted — resurrect it from the
                    // block's own values so the drink pours as saved
                    // (snapshot-not-reference; visible, not silent).
                    brew->addWaterVesselPreset(vesselName, volume, mode, flowRate, tempC);
                    index = static_cast<int>(brew->waterVesselPresets().size()) - 1;
                    qDebug() << "applyActivatedRecipe: recreated deleted water vessel" << vesselName;
                } else if (!blockHasValues) {
                    // Name-only block (web): adopt the live vessel's values.
                    const QVariantMap p = brew->getWaterVesselPreset(index);
                    volume = p.value("volume").toInt();
                    const QString pMode = p.value("mode").toString();
                    if (!pMode.isEmpty()) mode = pMode;
                    flowRate = p.value("flowRate").toInt();
                    tempC = p.value("temperature").toDouble();
                }
                brew->setSelectedWaterCup(index);
            }
            // Push the vessel's values into the live hot-water settings and send
            // (the non-UI equivalent of selecting the vessel on the brew screen).
            brew->setWaterVolume(volume);
            brew->setWaterVolumeMode(mode);
            m_settings->hardware()->setHotWaterFlowRate(flowRate);
            brew->setWaterTemperature(tempC);
            applyHotWaterSettings();
        }

        // Selection state last, so the watchers above never see a half-
        // applied recipe.
        dye->setActiveRecipeId(static_cast<int>(recipeId));
    } else {
        m_activeRecipe = recipe;
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), linkedBagId);
    }

    emit activeRecipeChanged();
    m_recipeStorage->requestTouchLastUsed(recipeId);
    m_applyingRecipe = false;

    // Queued like shotMetadataLoaded: lands after the queued dose write, so
    // a UI navigating on this signal shows the recipe's dose, not the
    // profile default.
    QMetaObject::invokeMethod(this, [this, recipeId]() {
        emit recipeActivated(recipeId, true);
    }, Qt::QueuedConnection);
}

// Apply a recipe's yield/temperature spec to the SESSION overrides, replacing
// whatever was armed. Shared by activation and the active-recipe edit refresh:
// editing the active recipe re-seeds the live brew from it, exactly as
// re-activating would. Without that, the dialed value stays armed while the
// baseline moves to the edited spec, so the freshly-edited recipe reads as an
// "override" of itself — edit the yield to 50 and the plan shows an amber
// "50.0 -> 36.0g" while the shot still targets the old 36.
//
// `linkedBag` is the activation bundle's bag snapshot; pass an empty map to
// resolve the bag rung from the live dye cache instead (the edit refresh and
// the same-id re-activation contract both do).
//
// Returns true when any override was armed — the caller re-uploads the profile
// so the temperature reaches the machine frames.
bool MainController::applyRecipeBrewOverrides(const QVariantMap& recipe,
                                              const QVariantMap& linkedBag)
{
    if (!m_settings || !m_profileManager)
        return false;
    bool hasOverrides = false;
    // Activation reflects ONLY this recipe's own overrides: clear
    // whatever session anchor was armed before, unconditionally.
    // This cannot be left to the loadProfile reset — a profile load
    // deliberately KEEPS a ratio anchor now (Decision 8), and the
    // bag-switch clear doesn't fire when the recipe's bag is already
    // active — so without this explicit clear a stale session ratio
    // from the previous setup would leak into a yield-less recipe.
    m_settings->brew()->clearAllBrewOverrides();
    // The recipe's yield spec applies VERBATIM — value and mode
    // (add-yield-ratio-anchor). A ratio is always a deliberate
    // anchor, even when it derives exactly the profile's target
    // (no Bug-A gram comparison for ratios — that would discard
    // the anchor precisely when it coincides with the profile).
    // The ratio anchor write is QUEUED, matching the queued dose
    // write above, so its first resolution multiplies the recipe's
    // own dose and never the stale pre-activation one (the yield
    // used to be written synchronously here while the dose was
    // queued — the write-order bug). An absolute yield doesn't
    // depend on the dose, so it stays synchronous, and the "a value
    // matching the profile default is not an override" rule
    // survives for it alone. Mode "none" arms nothing: the ladder
    // falls through to the bag's spec (applied by the bag watcher),
    // then the profile.
    const double yieldValue = recipe.value("yieldValue").toDouble();
    const QString yieldMode = YieldSpec::normalizedMode(recipe.value("yieldMode").toString());
    if (yieldMode == YieldSpec::modeRatio() && yieldValue > 0) {
        QPointer<Settings> settings(m_settings);
        QMetaObject::invokeMethod(this, [settings, yieldValue]() {
            if (settings) settings->brew()->setBrewRatioAnchor(yieldValue);
        }, Qt::QueuedConnection);
        hasOverrides = true;
    } else if (yieldMode == YieldSpec::modeAbsolute() && yieldValue > 0
               && qAbs(yieldValue - m_profileManager->currentProfile().targetWeight()) > 0.1) {
        m_settings->brew()->setBrewYieldOverride(yieldValue);
        hasOverrides = true;
    } else if (!YieldSpec::isSet(yieldMode) || yieldValue <= 0) {
        // The recipe designs no yield: the ladder falls through to
        // its linked bag's spec. Recipe-driven bag selection goes
        // through the keep-fields path, which deliberately does NOT
        // re-arm the session from the bag — so the fall-through is
        // applied here, explicitly, from the activation bundle's own
        // bag snapshot (falling back to the dye cache for the
        // same-id re-activation contract, whose bundle bag map is
        // empty). Armed exactly like a manual bean switch — the
        // spec verbatim, no profile-default comparison (a bag's
        // anchor is first-class, not a deviation) — and QUEUED like
        // the ratio arm above so a bag ratio resolves against the
        // recipe's queued dose, never the stale one.
        double bagValue = linkedBag.value("yieldValue").toDouble();
        QString bagMode = YieldSpec::normalizedMode(
            linkedBag.value("yieldMode").toString());
        if (linkedBag.isEmpty() && m_settings) {
            bagValue = m_settings->dye()->activeBagYieldValue();
            bagMode = m_settings->dye()->activeBagYieldMode();
        }
        if (YieldSpec::isSet(bagMode) && bagValue > 0) {
            QPointer<Settings> settings(m_settings);
            QMetaObject::invokeMethod(this, [settings, bagValue, bagMode]() {
                if (settings) settings->brew()->setBrewYieldAnchor(bagValue, bagMode);
            }, Qt::QueuedConnection);
            hasOverrides = true;
        }
    }
    // Temperature is a stored OFFSET against the profile
    // (recipe-relative-temp-offset): the brew temperature is computed
    // profileTemp + offset at activation, so the recipe follows any
    // later profile temperature edit. Offset 0 is unambiguous "brew at
    // the profile's temperature" — no coincidental-default comparison
    // needed (the old Bug-A guard).
    const double tempOffsetC = recipe.value("tempOffsetC").toDouble();
    const double profileTempC = m_profileManager->currentProfile().espressoTemperature();
    if (qAbs(tempOffsetC) > 0.05 && profileTempC > 0) {
        m_settings->brew()->setTemperatureOverride(profileTempC + tempOffsetC);
        hasOverrides = true;
    } else if (qAbs(tempOffsetC) > 0.05) {
        // A real offset with no profile temperature to anchor on: the
        // shot brews at whatever the machine holds. Loud, because the
        // user asked for "profile −3°" and silently not getting it is
        // undebuggable.
        qWarning() << "applyActivatedRecipe: recipe" << recipe.value("name").toString()
                   << "has temp offset" << tempOffsetC
                   << "but the loaded profile reports no espresso_temperature"
                   << "- skipping the temperature override";
    }

    return hasOverrides;
}

// Recipe-aware brew baseline (recipe-baseline-not-override, #1485). A recipe's
// own yield/temp ARE the baseline when it's active — so a widget must measure
// "is this a real override?" against the recipe, not the profile. These four
// fold that choice into one source of truth. The recipe map keys ("tempOffsetC"
// / "yieldG") match applyActivatedRecipe's read-back; 0 = the recipe pins none,
// so fall back to the profile (which also covers the no-recipe case since
// m_activeRecipe is cleared on deactivation). The temperature baseline is
// OFFSET-derived — profile temp + the recipe's stored delta — never a stored
// absolute (recipe-relative-temp-offset).
double MainController::activeBaselineTemperatureC() const {
    const double profileTemp =
        m_profileManager ? m_profileManager->profileTargetTemperature() : 0.0;
    if (!m_activeRecipe.isEmpty()) {
        const double offset = m_activeRecipe.value(QStringLiteral("tempOffsetC")).toDouble();
        if (qAbs(offset) > 0.05 && profileTemp > 0)
            return profileTemp + offset;
    }
    return profileTemp;
}

// The yield baseline is a SPEC resolved through the ladder
// (add-yield-ratio-anchor): the active recipe's own {value, mode} when it
// designs a yield, else the active bag's, else the profile's target_weight
// as an absolute. A ratio-anchored recipe has NO absolute yield — falling
// back to the profile here is exactly the `yieldG > 0 ? yieldG :
// profileYield` fallthrough that reintroduces #1485's spurious override
// arrow, so the mode is consulted first.
double MainController::activeBaselineYieldValue() const {
    if (!m_activeRecipe.isEmpty()
        && YieldSpec::isSet(YieldSpec::normalizedMode(
               m_activeRecipe.value(QStringLiteral("yieldMode")).toString()))) {
        const double v = m_activeRecipe.value(QStringLiteral("yieldValue")).toDouble();
        if (v > 0.0)
            return v;
    }
    if (m_settings && YieldSpec::isSet(m_settings->dye()->activeBagYieldMode())
        && m_settings->dye()->activeBagYieldValue() > 0.0)
        return m_settings->dye()->activeBagYieldValue();
    return m_profileManager ? m_profileManager->profileTargetWeight() : 0.0;
}

QString MainController::activeBaselineYieldMode() const {
    if (!m_activeRecipe.isEmpty()) {
        const QString mode = YieldSpec::normalizedMode(
            m_activeRecipe.value(QStringLiteral("yieldMode")).toString());
        if (YieldSpec::isSet(mode)
            && m_activeRecipe.value(QStringLiteral("yieldValue")).toDouble() > 0.0)
            return mode;
    }
    if (m_settings && YieldSpec::isSet(m_settings->dye()->activeBagYieldMode())
        && m_settings->dye()->activeBagYieldValue() > 0.0)
        return m_settings->dye()->activeBagYieldMode();
    return YieldSpec::modeAbsolute();  // the profile rung is always absolute
}

double MainController::activeBaselineYieldG() const {
    const double dose = m_profileManager ? m_profileManager->brewByRatioDose() : 0.0;
    const double profileTarget = m_profileManager ? m_profileManager->profileTargetWeight() : 0.0;
    return YieldSpec::resolveGrams(activeBaselineYieldMode(), activeBaselineYieldValue(),
                                   dose, profileTarget);
}

bool MainController::temperatureIsRealOverride() const {
    if (!m_settings || !m_settings->brew()->hasTemperatureOverride())
        return false;
    return qAbs(m_settings->brew()->temperatureOverride() - activeBaselineTemperatureC()) > 0.1;
}

bool MainController::yieldIsRealOverride() const {
    // Compare like with like (add-yield-ratio-anchor): the session anchor
    // against the baseline spec in the SAME unit. A mode difference alone is
    // a real override (an armed ratio deviates from an absolute baseline
    // even when the derived grams coincide). Same-mode ratio deviations
    // convert through the dose so the tolerance is the single 0.1 g rule.
    if (!m_settings || !m_settings->brew()->hasBrewYieldOverride())
        return false;
    SettingsBrew* brew = m_settings->brew();
    const QString baselineMode = activeBaselineYieldMode();
    if (brew->brewYieldMode() != baselineMode)
        return true;
    const double delta = qAbs(brew->brewYieldOverride() - activeBaselineYieldValue());
    if (baselineMode == YieldSpec::modeRatio()) {
        const double dose = m_profileManager ? m_profileManager->brewByRatioDose() : 0.0;
        return dose > 0 ? delta * dose > 0.1 : delta > 0.005;
    }
    return delta > 0.1;
}

void MainController::deactivateRecipe() {
    const bool hadMilk = activeRecipeHasMilk();
    // Drop any in-flight self-write count with the recipe it belonged to —
    // its echo would otherwise land with no active recipe and leak the count.
    m_pendingRecipeSelfWrites = 0;
    m_refreshDialFromRecipeEdit = false;
    if (m_settings) {
        m_settings->dye()->setActiveRecipeId(-1);
    }
    if (!m_activeRecipe.isEmpty()) {
        m_activeRecipe.clear();
        emit activeRecipeChanged();
    }
    // Leaving a milk recipe releases the heater hold: re-send settings so
    // the heater returns to the user's baseline (keep-on users stay warm,
    // eco users go cold).
    if (hadMilk)
        applySteamSettings();
}

bool MainController::activeRecipeHasMilk() const {
    if (m_activeRecipe.isEmpty())
        return false;
    // A profile-less (hot-water tea) recipe never holds the steam heater,
    // even if an MCP/web author attached a milk block to one — activation
    // skipped the hold and re-sends must not re-assert it.
    if (m_activeRecipe.value(QStringLiteral("profileTitle")).toString().trimmed().isEmpty())
        return false;
    return parseSteamBlock(m_activeRecipe.value(QStringLiteral("steamJson")).toString())
        .value(QStringLiteral("hasMilk")).toBool();
}

void MainController::stampActiveRecipe(const QString& field, const QVariant& value) {
    if (m_applyingRecipe || m_activeRecipe.isEmpty() || !m_recipeStorage || !m_settings)
        return;
    const int recipeId = m_settings->dye()->activeRecipeId();
    if (recipeId <= 0)
        return;
    if (m_activeRecipe.value(field) == value)
        return;  // echo of our own apply, or no actual change
    m_activeRecipe.insert(field, value);
    m_pendingRecipeSelfWrites++;
    m_recipeStorage->requestUpdateRecipe(recipeId, {{field, value}});
}

void MainController::stampActiveRecipeSteam() {
    if (m_applyingRecipe || m_activeRecipe.isEmpty())
        return;
    stampActiveRecipe(QStringLiteral("steamJson"), currentSteamSpecJson());
}

QString MainController::currentSteamSpecJson() const {
    if (!m_settings)
        return QString();
    auto* brew = m_settings->brew();
    QJsonObject o;
    // hasMilk is declared intent (a recipe field), not derivable from live
    // settings — carry it over from the active recipe when one is set.
    if (!m_activeRecipe.isEmpty()) {
        const QJsonObject active = parseSteamBlock(m_activeRecipe.value("steamJson").toString());
        if (active.contains("hasMilk"))
            o.insert("hasMilk", active.value("hasMilk"));
    }
    const QVariantMap pitcher = brew->getSteamPitcherPreset(brew->selectedSteamPitcher());
    if (!pitcher.isEmpty() && !pitcher.value("disabled").toBool()) {
        o.insert("pitcherName", pitcher.value("name").toString());
        o.insert("durationSec", pitcher.value("duration").toInt());
        o.insert("flow", pitcher.value("flow").toInt());
        o.insert("temperatureC", pitcher.value("temperature").toDouble());
    }
    if (brew->lastSteamMilkG() > 0)
        o.insert("milkWeightG", brew->lastSteamMilkG());
    return compactJson(o);
}

void MainController::stampActiveRecipeHotWater() {
    if (m_applyingRecipe || m_activeRecipe.isEmpty())
        return;
    // Only write through for a recipe that actually uses hot water. Otherwise a
    // brew-screen vessel change (unrelated to this recipe) would stamp an empty
    // block over a dormant hasWater:false recipe, erasing its remembered vessel
    // and pour order. currentHotWaterSpecJson() returns "" when hasWater is
    // false, so without this guard the equality check would persist that "".
    if (!parseHotWaterBlock(m_activeRecipe.value(QStringLiteral("hotWaterJson")).toString())
             .value(QStringLiteral("hasWater")).toBool())
        return;
    stampActiveRecipe(QStringLiteral("hotWaterJson"), currentHotWaterSpecJson());
}

QString MainController::currentHotWaterSpecJson() const {
    if (!m_settings)
        return QString();
    auto* brew = m_settings->brew();
    QJsonObject o;
    // hasWater is declared intent (a recipe field), not derivable from live
    // settings — carry it over from the active recipe when one is set. Without
    // an active hot-water recipe there is nothing to snapshot.
    if (m_activeRecipe.isEmpty())
        return QString();
    const QJsonObject active = parseHotWaterBlock(m_activeRecipe.value("hotWaterJson").toString());
    if (!active.value("hasWater").toBool())
        return QString();
    o.insert("hasWater", true);
    // order (before/after) is declared intent, not derivable from live settings
    // — carry it over from the active recipe's block (default "after").
    const QString order = active.value("order").toString();
    o.insert("order", order.isEmpty() ? QStringLiteral("after") : order);
    // The selected water vessel IS the values — snapshot it by value.
    const QVariantMap vessel = brew->getWaterVesselPreset(brew->selectedWaterVessel());
    if (!vessel.isEmpty()) {
        const QString mode = vessel.value("mode").toString();
        o.insert("vesselName", vessel.value("name").toString());
        o.insert("volume", vessel.value("volume").toInt());
        o.insert("mode", mode.isEmpty() ? QStringLiteral("weight") : mode);
        o.insert("flowRate", vessel.value("flowRate").toInt());
        o.insert("temperatureC", vessel.value("temperature").toDouble());
    }
    return compactJson(o);
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
    } else if (!m_settings->brew()->keepSteamHeaterOn() && !activeRecipeHasMilk()) {
        // The steam heater needs 5-9 MINUTES to come up to temperature, so a
        // milk recipe must HOLD the heater on for as long as it is active and
        // the machine is awake (add-recipes) — a one-time warm at activation
        // would be undone by the next settings re-send for keep-heater-off
        // users, and warming at steam time would mean a long wait.
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
    QString shotYieldMode = YieldSpec::modeNone();
    double shotYieldAnchorValue = 0.0;

    if (m_settings) {
        // Temperature: user override OR profile's espresso temperature
        if (m_settings->brew()->hasTemperatureOverride()) {
            shotTemperatureOverride = m_settings->brew()->temperatureOverride();
        } else {
            shotTemperatureOverride = m_profileManager->currentProfile().espressoTemperature();
        }

        // Yield: the shot's START-OF-SHOT snapshot — the resolved grams that
        // actually ran (targetWeight() is the ladder's evaluation point; a
        // ratio never lands in yield_override, which stays the
        // resolved-grams column every detector reads) plus the anchor that
        // produced them, recorded as intent (shots.yield_mode /
        // yield_anchor_value): stored, never derived at read time, so a
        // post-shot dose correction cannot rewrite it.
        //
        // Read the SNAPSHOT, never the live session. This path runs after
        // SAW settling — by then the shot latch has released, so re-reading
        // the session would record whatever it drifted to during the pour
        // rather than what the machine used: weigh the next dose while the
        // cup fills and a 1:2.5 shot that ran to 45 g would record 50 g; a
        // bean switch mid-pour would record the profile default. The latch
        // keeps the machine honest; this keeps the record honest.
        if (m_profileManager->hasShotSnapshot()) {
            shotTargetWeight = m_profileManager->latchedTargetG();
            shotYieldMode = m_profileManager->latchedYieldMode();
            shotYieldAnchorValue = m_profileManager->latchedYieldAnchorValue();
        } else if (m_profileManager->currentProfile().targetWeight() > 0) {
            // No snapshot (a shot that never signalled cycle-start): fall
            // back to the profile default, as before.
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
    metadata.storageHint = m_settings->dye()->activeBagStorageHint();
    metadata.openedDate = m_settings->dye()->activeBagOpenedDate();
    // Recipe provenance (add-recipes): the recipe active at shot time and
    // the steam spec in effect, so promote-from-shot round-trips the drink.
    metadata.recipeId = m_settings->dye()->activeRecipeId();
    metadata.steamJson = currentSteamSpecJson();
    metadata.hotWaterJson = currentHotWaterSpecJson();
    // Yield anchor provenance (add-yield-ratio-anchor): what was MEANT,
    // alongside the resolved grams in shotTargetWeight (what ran).
    metadata.yieldMode = shotYieldMode;
    metadata.yieldAnchorValue = shotYieldAnchorValue;

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

            // Stamp the actual dose onto the active bag ("last used with
            // this bag" — the dose may come from SAW/profile settings rather
            // than a manual edit). Independent of save success: a failed
            // stamp is logged inside storage and never blocks the shot save.
            // No user prompt (bean-bag-inventory).
            //
            // The YIELD half of this stamp is gone (add-yield-ratio-anchor):
            // a shot is a measurement, the bag's yield spec is intent — it
            // changes only via the explicit "Update Bag" action. The old
            // per-shot yield stamp was the second, easy-to-miss auto-writer
            // that kept the bean silently learning its yield, and the
            // mechanism that drifted the stored dose/yield pair into a
            // ratio nobody chose.
            if (m_bagStorage && bagIdIsSet(metadata.bagId)) {
                QVariantMap stamp;
                if (doseWeight > 0)
                    stamp.insert(QStringLiteral("doseWeightG"), doseWeight);
                stamp.insert(QStringLiteral("lastUsedEpoch"), QDateTime::currentSecsSinceEpoch());
                m_bagStorage->requestUpdateBag(metadata.bagId, stamp);
            }

            // Pulling a shot with a recipe active bumps its MRU standing
            // (the idle pills order by last use, add-recipes).
            if (m_recipeStorage && metadata.recipeId > 0)
                m_recipeStorage->requestTouchLastUsed(metadata.recipeId);
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
    metadata.storageHint = m_settings->dye()->activeBagStorageHint();
    metadata.openedDate = m_settings->dye()->activeBagOpenedDate();
    // Recipe provenance (add-recipes): the recipe active at shot time and
    // the steam spec in effect, so promote-from-shot round-trips the drink.
    metadata.recipeId = m_settings->dye()->activeRecipeId();
    metadata.steamJson = currentSteamSpecJson();
    metadata.hotWaterJson = currentHotWaterSpecJson();

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

        // addSample(time, pressure, flow, temperature, mixTemp, pressureGoal, flowGoal, temperatureGoal, temperatureMixGoal, frameNumber, isFlowMode)
        // Simulation uses pressure mode (isFlowMode = false)
        m_shotDataModel->addSample(t, pressure, flow, temperature, temperature, pressureGoal, flowGoal, 92.0, 93.0, frameNumber, false);
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
            metadata.storageHint = m_settings->dye()->activeBagStorageHint();
            metadata.openedDate = m_settings->dye()->activeBagOpenedDate();
            metadata.recipeId = m_settings->dye()->activeRecipeId();
            metadata.steamJson = currentSteamSpecJson();
            metadata.hotWaterJson = currentHotWaterSpecJson();

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
                    // Exit condition was configured, but the sensor threshold was NOT
                    // confirmed above and time did not expire — usually a real sensor
                    // exit whose crossing fell between BLE samples. Record it as an
                    // UNCONFIRMED sensor exit (hint from exitType): displays render it
                    // like the sensor exit it probably was, the grind detector's
                    // limiter-tail trim treats pressure_unconfirmed as limiter
                    // engagement, but the skip-first-frame guard only trusts confirmed
                    // "pressure"/"flow"/"weight" — so a genuinely skipped frame (which
                    // lands in this branch) still flags.
                    transitionReason = prevFrame.exitType.contains(QStringLiteral("pressure"))
                        ? QStringLiteral("pressure_unconfirmed") : QStringLiteral("flow_unconfirmed");
                    qDebug() << "MainController: Frame" << prevFrameIndex
                             << "exit reason unconfirmed - exitType:" << prevFrame.exitType
                             << "pressure:" << m_lastPressure << "flow:" << m_lastFlow
                             << "recorded as" << transitionReason;
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
    // Two transposable pairs of same-typed °C args: (headTemp, mixTemp) and
    // (setTempGoal, setMixTempGoal). A consistent swap of both stays plausible
    // — mix does run above basket — so label them at the call site.
    m_shotDataModel->addSample(time, sample.groupPressure,
                               sample.groupFlow,
                               /*temperature*/ sample.headTemp,
                               /*mixTemp*/ sample.mixTemp,
                               pressureGoal, flowGoal,
                               /*temperatureGoal*/ sample.setTempGoal,
                               /*temperatureMixGoal*/ sample.setMixTempGoal,
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


