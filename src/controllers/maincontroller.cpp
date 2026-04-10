#include "maincontroller.h"
#include "shottimingcontroller.h"
#include "../core/settings.h"
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
#include "../ble/refractometers/difluidr2.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVariantMap>
#include <QRandomGenerator>
#include <algorithm>
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

    // Connect to shot sample updates
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &MainController::onShotSampleReceived);

        // Apply user settings immediately after device sends its initial (hardcoded) settings.
        // BleTransport's FIFO queue guarantees our writes follow the initial writes.
        connect(m_device, &DE1Device::initialSettingsComplete,
                this, &MainController::applyAllSettings);
    }
    // Send water refill level to machine when setting changes
    if (m_settings && m_device) {
        connect(m_settings, &Settings::waterRefillPointChanged,
                this, &MainController::applyWaterRefillLevel);
    }

    // Apply refill kit override when setting changes or when kit detection completes
    if (m_settings && m_device) {
        connect(m_settings, &Settings::refillKitOverrideChanged,
                this, &MainController::applyRefillKitOverride);
        connect(m_device, &DE1Device::refillKitDetectedChanged,
                this, &MainController::applyRefillKitOverride);
    }

    // Apply flow calibration multiplier when setting changes
    if (m_settings && m_device) {
        connect(m_settings, &Settings::flowCalibrationMultiplierChanged,
                this, &MainController::applyFlowCalibration);
        connect(m_settings, &Settings::autoFlowCalibrationChanged,
                this, &MainController::applyFlowCalibration);
        connect(m_settings, &Settings::perProfileFlowCalibrationChanged,
                this, &MainController::applyFlowCalibration);
    }
    // Apply heater tweaks when any calibration setting changes (debounced to avoid BLE flood)
    m_heaterTweaksTimer.setSingleShot(true);
    m_heaterTweaksTimer.setInterval(300);
    connect(&m_heaterTweaksTimer, &QTimer::timeout, this, &MainController::applyHeaterTweaks);

    if (m_settings && m_device) {
        auto startHeaterTimer = [this]() { m_heaterTweaksTimer.start(); };
        connect(m_settings, &Settings::heaterIdleTempChanged, this, startHeaterTimer);
        connect(m_settings, &Settings::heaterWarmupFlowChanged, this, startHeaterTimer);
        connect(m_settings, &Settings::heaterTestFlowChanged, this, startHeaterTimer);
        connect(m_settings, &Settings::heaterWarmupTimeoutChanged, this, startHeaterTimer);
        connect(m_settings, &Settings::hotWaterFlowRateChanged, this, startHeaterTimer);
        connect(m_settings, &Settings::steamTwoTapStopChanged, this, startHeaterTimer);
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
                && m_settings && m_settings->steamDisabled()) {
                qDebug() << "Machine entering" << m_machineState->phaseString() << "- clearing temporary steamDisabled flag";
                m_settings->setSteamDisabled(false);
            }

            // Steam session ended — run post-session analysis
            if (phase != MachineState::Phase::Steaming && m_steamStartTime > 0) {
                if (m_steamHealthTracker && m_steamDataModel) {
                    m_steamHealthTracker->onSessionComplete(
                        m_steamDataModel,
                        m_settings->steamFlow(),
                        static_cast<int>(m_settings->steamTemperature()));
                }
                if (m_steamCalibrator && m_steamDataModel)
                    m_steamCalibrator->onSteamEnded(m_steamDataModel);
                m_steamStartTime = 0;
                if (m_steamHealthTracker)
                    m_steamHealthTracker->resetSession();
            }
        });
    }
    // Create visualizer uploader and importer
    m_visualizer = new VisualizerUploader(m_networkManager, m_settings, this);
    m_visualizer->setDevice(m_device);
    m_visualizerImporter = new VisualizerImporter(m_networkManager, this, m_settings, this);

    // Create shot history storage and comparison model
    m_shotHistory = new ShotHistoryStorage(this);
    m_shotHistory->initialize();
    connect(m_shotHistory, &QObject::destroyed, this, [this]() { m_savingShot = false; });

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
        m_shotServer->setPort(m_settings->shotServerPort());

        // Start server if enabled in settings
        if (m_settings->shotServerEnabled()) {
            m_shotServer->start();
        }

        // React to settings changes
        connect(m_settings, &Settings::shotServerEnabledChanged, this, [this]() {
            if (m_settings->shotServerEnabled()) {
                m_shotServer->start();
            } else {
                m_shotServer->stop();
            }
        });
        connect(m_settings, &Settings::shotServerPortChanged, this, [this]() {
            bool wasRunning = m_shotServer->isRunning();
            if (wasRunning) {
                m_shotServer->stop();
            }
            m_shotServer->setPort(m_settings->shotServerPort());
            if (wasRunning) {
                m_shotServer->start();
            }
        });
        connect(m_settings, &Settings::webSecurityEnabledChanged, this, [this]() {
            bool wasRunning = m_shotServer->isRunning();
            if (wasRunning) {
                m_shotServer->stop();
            }
            if (wasRunning || m_settings->shotServerEnabled()) {
                m_shotServer->start();
            }
        });
    }

    // Set MachineState on ShotServer for home automation API
    m_shotServer->setMachineState(m_machineState);

    // Emit remoteSleepRequested when sleep command received via REST API
    connect(m_shotServer, &ShotServer::sleepRequested, this, &MainController::remoteSleepRequested);
    // Create MQTT client for home automation
    m_mqttClient = new MqttClient(m_device, m_machineState, m_settings, this);

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
                m_mqttClient->setCurrentProfileFilename(m_settings->currentProfile());
            }
        }
    });

    // Steam on/off commands
    connect(m_mqttClient, &MqttClient::steamOnRequested, this, [this]() {
        startSteamHeating();
    });
    connect(m_mqttClient, &MqttClient::steamOffRequested, this, [this]() {
        turnOffSteamHeater();
    });

    // Steam settings changes -> republish state
    connect(m_settings, &Settings::steamDisabledChanged, m_mqttClient, &MqttClient::onSteamSettingsChanged);
    connect(m_settings, &Settings::keepSteamHeaterOnChanged, m_mqttClient, &MqttClient::onSteamSettingsChanged);

    // Auto-connect MQTT if enabled
    if (m_settings && m_settings->mqttEnabled() && !m_settings->mqttBrokerHost().isEmpty()) {
        // Deferred call ensures construction completes first.
        // MQTT connects to an external broker over TCP — no BLE dependency.
        QMetaObject::invokeMethod(this, [this]() {
            if (m_settings->mqttEnabled()) {
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

void MainController::loadShotWithMetadata(qint64 shotId) {
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
    QThread* thread = QThread::create([self, dbPath, shotId]() {
        ShotRecord record;
        if (!withTempDb(dbPath, "load_meta", [&](QSqlDatabase& db) {
            record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
        })) {
            qWarning() << "loadShotWithMetadata: Failed to open DB for shot" << shotId;
        }

        // Apply metadata on main thread (interacts with QML state and BLE)
        QMetaObject::invokeMethod(qApp, [self, shotId, record = std::move(record)]() {
            if (self) self->applyLoadedShotMetadata(shotId, record);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainController::applyLoadedShotMetadata(qint64 shotId, const ShotRecord& shotRecord) {
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
        m_settings->setDyeBeanBrand(shotRecord.summary.beanBrand);
        m_settings->setDyeBeanType(shotRecord.summary.beanType);
        m_settings->setDyeRoastDate(shotRecord.roastDate);
        m_settings->setDyeRoastLevel(shotRecord.roastLevel);
        m_settings->setDyeGrinderBrand(shotRecord.grinderBrand);
        m_settings->setDyeGrinderModel(shotRecord.grinderModel);
        m_settings->setDyeGrinderBurrs(shotRecord.grinderBurrs);
        m_settings->setDyeGrinderSetting(shotRecord.grinderSetting);
        m_settings->setDyeBarista(shotRecord.barista);

        // Restore dose (input parameter, not a result)
        if (shotRecord.summary.doseWeight > 0) {
            m_settings->setDyeBeanWeight(shotRecord.summary.doseWeight);
        }
        // Note: Don't copy finalWeight/TDS/EY - those are shot results, not inputs

        // Find matching bean preset or set to -1 for guest bean
        int beanPresetIndex = m_settings->findBeanPresetByContent(
            shotRecord.summary.beanBrand, shotRecord.summary.beanType);
        qDebug() << "applyLoadedShotMetadata: Looking for bean preset - brand:" << shotRecord.summary.beanBrand
                 << "type:" << shotRecord.summary.beanType << "-> found index:" << beanPresetIndex;
        m_settings->setSelectedBeanPreset(beanPresetIndex);

        // Apply brew overrides from history on top of profile defaults (set by loadProfile)
        // Values > 0 indicate overrides were used (0 means no override or old shot)
        bool hasOverrides = false;
        if (shotRecord.temperatureOverride > 0) {
            m_settings->setTemperatureOverride(shotRecord.temperatureOverride);
            hasOverrides = true;
        }

        if (shotRecord.yieldOverride > 0) {
            m_settings->setBrewYieldOverride(shotRecord.yieldOverride);
            hasOverrides = true;
        } else if (shotRecord.summary.finalWeight > 0 && m_profileManager->currentProfile().targetWeight() <= 0) {
            // Old shots from volume/timer-based profiles were saved with yieldOverride=0.
            // Use the actual yield so the user gets a meaningful weight target.
            m_settings->setBrewYieldOverride(shotRecord.summary.finalWeight);
            hasOverrides = true;
        }

        qDebug() << "Loaded shot metadata - brand:" << shotRecord.summary.beanBrand
                 << "type:" << shotRecord.summary.beanType
                 << "grinder:" << shotRecord.grinderModel << shotRecord.grinderSetting
                 << "beanPresetIndex:" << beanPresetIndex
                 << "brewOverrides - temp:" << (shotRecord.temperatureOverride > 0 ? QString::number(shotRecord.temperatureOverride) : "none")
                 << "yield:" << (shotRecord.yieldOverride > 0 ? QString::number(shotRecord.yieldOverride)
                    : (shotRecord.summary.finalWeight > 0 && m_profileManager->currentProfile().targetWeight() <= 0
                       ? QString::number(shotRecord.summary.finalWeight) + " (from actual)"
                       : "none"));

        // Re-upload profile with history overrides applied
        // loadProfile() already uploaded with profile defaults; now we have the actual overrides
        if (hasOverrides) {
            m_profileManager->uploadCurrentProfile();
        }
    }

    emit shotMetadataLoaded(shotId, true);
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

void MainController::sendMachineSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Determine steam temperature to send:
    // - If steam is disabled: send 0
    // - If keepSteamHeaterOn is false: send 0 (user doesn't want heater on)
    // - Otherwise: send configured temperature
    double steamTemp;
    if (m_settings->steamDisabled()) {
        steamTemp = 0.0;
    } else if (!m_settings->keepSteamHeaterOn()) {
        steamTemp = 0.0;
    } else {
        steamTemp = m_settings->steamTemperature();
    }

    double groupTemp = getGroupTemperature();
    qDebug() << "sendMachineSettings: steam=" << steamTemp << "°C, groupTemp=" << groupTemp << "°C";

    // Hot water volume: only send actual ml in volume mode (machine auto-stops via flowmeter).
    // In weight mode send 0 so the app controls stop via scale instead.
    int hotWaterVolume = 0;
    if (m_settings->waterVolumeMode() == "volume") {
        hotWaterVolume = qMin(m_settings->waterVolume(), 255);  // BLE uint8 max
    }

    // 1. ShotSettings (single write with all temperatures)
    m_device->setShotSettings(
        steamTemp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        hotWaterVolume,
        groupTemp
    );

    // 2. Steam flow MMR
    m_device->writeMMR(0x803828, m_settings->steamFlow());

    // 3. Flush flow MMR (value × 10)
    int flowValue = static_cast<int>(m_settings->flushFlow() * 10);
    m_device->writeMMR(0x803840, flowValue);

    // 4. Flush timeout MMR (value × 10)
    int secondsValue = static_cast<int>(m_settings->flushSeconds() * 10);
    m_device->writeMMR(0x803848, secondsValue);
}

void MainController::applySteamSettings() {
    sendMachineSettings();
}

void MainController::applyHotWaterSettings() {
    sendMachineSettings();
    if (m_device && m_device->isConnected())
        m_device->writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, m_settings->hotWaterFlowRate());
}

void MainController::applyFlushSettings() {
    sendMachineSettings();
}

void MainController::applyAllSettings() {
    // 1. Upload current profile (espresso)
    if (m_profileManager->currentProfile().mode() == Profile::Mode::FrameBased) {
        m_profileManager->uploadCurrentProfile();
    }

    // 2. Apply steam/hot water/flush settings (unified)
    sendMachineSettings();

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

    m_device->setWaterRefillLevel(m_settings->waterRefillPoint());
}

void MainController::applyRefillKitOverride() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    int override = m_settings->refillKitOverride();
    // Values match de1app: 0=force off, 1=force on, 2=auto-detect
    m_device->setRefillKitPresent(override);
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
    if (!m_settings->autoFlowCalibration()) {
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
    constexpr double kCalibrationMax = 1.8;          // sanity upper bound
    constexpr double kChangeThreshold = 0.02;        // 2% relative change required to update
    constexpr double kMaxSampleRatio = 2.5;          // per-sample machine/weight ratio — break window on extreme outliers
    constexpr double kMinSampleRatio = 0.4;          // (generous bounds: window-level check is tighter)
    constexpr double kMaxWindowRatio = 1.35;         // window-mean machine/weight ratio — reject if scale data is suspect
    constexpr double kMinWindowRatio = 0.75;         // (de1app GFC users get ~0.9-1.1 ratios on good data)
    constexpr double kMinWindowStartTime = 10.0;     // seconds — skip early extraction where LSLR weight flow
                                                     // lags behind actual flow, producing inflated ratios

    // Find the best steady-pour window: stable pressure above minimum + meaningful weight flow.
    // We track the best (longest) qualifying window found across the entire shot.
    double bestStart = -1, bestEnd = -1;
    double bestSumMF = 0, bestSumWF = 0;
    int bestCount = 0;

    double winStart = -1;
    double winSumMF = 0, winSumWF = 0;
    int winCount = 0;
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
    int wfCursor = 0;
    int mfCursor = 1;
    int mfMissCount = 0;  // Tracks flow interpolation misses for diagnostics

    for (int i = 1; i < pressureData.size(); ++i) {
        double dt = pressureData[i].x() - pressureData[i - 1].x();
        if (dt <= 0) continue;
        double dpdt = qAbs(pressureData[i].y() - pressureData[i - 1].y()) / dt;
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
        for (int k = wfCursor; k < weightFlowData.size(); ++k) {
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
        for (int j = mfCursor; j < flowData.size(); ++j) {
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
             << "ratio=" << windowRatio;

    // Guard against division by zero. Should be impossible since every sample
    // in the window passed the kMinWeightFlow (0.5 g/s) check.
    if (meanWeightFlow < 0.001) {
        qWarning() << "Auto flow cal: meanWeightFlow unexpectedly low ("
                   << meanWeightFlow << ") after qualifying window";
        return;
    }

    // Window-level ratio sanity check. Reject windows where machine flow and
    // scale-derived weight flow diverge too much. A healthy ratio is ~0.9-1.1
    // (matching what de1app GFC users see when the curves overlap). Ratios
    // outside [0.75, 1.35] indicate scale data quality problems: smoothing
    // lag, stale weight readings, or the scale capturing a non-representative
    // period. Without this check, bad windows drag the per-profile calibration
    // down to ~0.6 when the correct value is ~0.9.
    if (windowRatio > kMaxWindowRatio || windowRatio < kMinWindowRatio) {
        qDebug() << "Auto flow cal: window ratio" << windowRatio
                 << "outside bounds [" << kMinWindowRatio << "," << kMaxWindowRatio << "]"
                 << "- skipping (scale data suspect)";
        return;
    }

    // The machine's reported flow includes the active calibration multiplier.
    // To find the ideal multiplier: ideal = weight_flow / (raw_flow * density).
    // Since raw = reported / current_multiplier, this simplifies to:
    // ideal = current_multiplier * weight_flow / (reported_flow * density)
    double currentEffective = m_settings->effectiveFlowCalibration(m_profileManager->baseProfileName());
    double ideal = currentEffective * meanWeightFlow / (meanMachineFlow * kWaterDensity93C);

    if (!std::isfinite(ideal)) {
        qWarning() << "Auto flow cal: computed non-finite value" << ideal
                   << "(meanMachineFlow:" << meanMachineFlow
                   << "meanWeightFlow:" << meanWeightFlow << ")";
        return;
    }

    ideal = qBound(kCalibrationMin, ideal, kCalibrationMax);

    // Only update if the ideal itself differs enough from current. Checking ideal (not the
    // EMA output) preserves the original 2% deadband regardless of alpha. The > 0.01 guard
    // avoids division by zero on first use (before any calibration is set).
    if (currentEffective > 0.01 && qAbs(ideal - currentEffective) / currentEffective < kChangeThreshold) {
        qDebug() << "Auto flow cal: ideal" << ideal << "≈ current" << currentEffective << "(< 2% change, skipping)";
        return;
    }

    // EMA smoothing: blend current toward ideal to dampen shot-to-shot oscillation.
    // alpha=0.3 moves 30% toward the ideal each shot, converging in ~5-7 shots while
    // preventing a single noisy shot from dominating (vs. jumping straight to ideal).
    // Skip EMA on the first shot for this profile — no oscillation history to dampen yet.
    constexpr double kEmaAlpha = 0.3;
    double computed = m_settings->hasProfileFlowCalibration(m_profileManager->baseProfileName())
        ? kEmaAlpha * ideal + (1.0 - kEmaAlpha) * currentEffective
        : ideal;

    double oldValue = currentEffective;
    if (!m_settings->setProfileFlowCalibration(m_profileManager->baseProfileName(), computed)) {
        qWarning() << "Auto flow cal: computed value" << computed
                   << "was rejected by settings for" << m_profileManager->baseProfileName();
        return;
    }
    applyFlowCalibration();

    qDebug() << "Auto flow cal: updated" << m_profileManager->baseProfileName()
             << "from" << oldValue << "to" << computed
             << "(ideal:" << ideal << "EMA alpha:" << kEmaAlpha
             << "window:" << windowDuration << "s," << bestCount << "samples)";

    emit flowCalibrationAutoUpdated(m_profileManager->currentProfile().title(), oldValue, computed);

    // Update global to median of espresso per-profile values (helps new profiles converge faster)
    updateGlobalFromPerProfileMedian();
}

void MainController::updateGlobalFromPerProfileMedian() {
    QJsonObject map = m_settings->allProfileFlowCalibrations();

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
    double current = m_settings->flowCalibrationMultiplier();
    if (current > 0.01 && qAbs(median - current) / current < 0.02) return;

    m_settings->setFlowCalibrationMultiplier(median);
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

    m_device->writeMMR(DE1::MMR::PHASE1_FLOW_RATE, m_settings->heaterWarmupFlow());
    m_device->writeMMR(DE1::MMR::PHASE2_FLOW_RATE, m_settings->heaterTestFlow());
    m_device->writeMMR(DE1::MMR::HOT_WATER_IDLE_TEMP, m_settings->heaterIdleTemp());
    m_device->writeMMR(DE1::MMR::ESPRESSO_WARMUP_TIMEOUT, m_settings->heaterWarmupTimeout());
    m_device->writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, m_settings->hotWaterFlowRate());
    m_device->writeMMR(DE1::MMR::STEAM_TWO_TAP_STOP, m_settings->steamTwoTapStop() ? 1 : 0);
}

double MainController::getGroupTemperature() const {
    if (m_settings && m_settings->hasTemperatureOverride()) {
        double temp = m_settings->temperatureOverride();
        qDebug() << "getGroupTemperature: using override" << temp << "°C";
        return temp;
    }
    return m_profileManager->currentProfile().espressoTemperature();
}

void MainController::setSteamTemperatureImmediate(double temp) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setSteamTemperature(temp);

    // Clear steamDisabled flag when user actively changes temperature
    if (m_settings->steamDisabled()) {
        m_settings->setSteamDisabled(false);
    }

    double groupTemp = getGroupTemperature();

    // Send all shot settings with updated temperature
    m_device->setShotSettings(
        temp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
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
        m_settings->setSteamDisabled(temp == 0);
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
              .arg(m_settings->steamTimeout())
              .arg(m_settings->waterTemperature())
              .arg(m_settings->waterVolume())
              .arg(groupTemp));

    // Send to machine without saving to settings (for enable/disable toggle)
    m_device->setShotSettings(
        temp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
    );

    logToFile("Command queued successfully");
}

void MainController::startSteamHeating() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Clear steamDisabled flag - we're explicitly starting steam heating
    m_settings->setSteamDisabled(false);

    // Always send the configured steam temperature
    double steamTemp = m_settings->steamTemperature();

    double groupTemp = getGroupTemperature();

    m_device->setShotSettings(
        steamTemp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
    );

    // Also send steam flow via MMR
    m_device->writeMMR(0x803828, m_settings->steamFlow());

    qDebug() << "Started steam heating to" << steamTemp << "°C";
}

void MainController::turnOffSteamHeater() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Set steamDisabled flag - this ensures consistent state management
    m_settings->setSteamDisabled(true);

    double groupTemp = getGroupTemperature();

    // Send 0°C to turn off steam heater
    m_device->setShotSettings(
        0.0,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
    );

    qDebug() << "Turned off steam heater (steamDisabled=true)";
}

void MainController::setHotWaterFlowRateImmediate(int flow) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setHotWaterFlowRate(flow);

    m_device->writeMMR(DE1::MMR::HOT_WATER_FLOW_RATE, flow);

    qDebug() << "Hot water flow rate set to:" << flow;
}

void MainController::setSteamFlowImmediate(int flow) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setSteamFlow(flow);

    // Send steam flow via MMR (can be changed in real-time)
    m_device->writeMMR(0x803828, flow);

    qDebug() << "Steam flow set to:" << flow;
}

void MainController::setSteamTimeoutImmediate(int timeout) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_settings->setSteamTimeout(timeout);

    double groupTemp = getGroupTemperature();

    // Send all shot settings with updated timeout
    m_device->setShotSettings(
        m_settings->steamTemperature(),
        timeout,
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
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
        m_settings->steamTemperature(),
        1,  // 1 second - any elapsed time > 1 will trigger stop
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
    );

    qDebug() << "Soft stop steam: sent 1-second timeout to trigger natural stop";
}

void MainController::onEspressoCycleStarted() {
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

    // Clear the graph for the new espresso cycle (previous shot is now saved)
    m_shotStartTime = 0;
    m_lastShotTime = 0;
    m_extractionStarted = false;
    m_lastFrameNumber = -1;
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
        double dose = m_settings ? m_settings->dyeBeanWeight() : 0.0;
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
    if (m_settings && m_settings->visualizerClearNotesOnStart()) {
        m_settings->setDyeShotNotes("");
    }
}

void MainController::onShotEnded() {
    // Clear filtered goals so CupFillView doesn't show stale tracking colors
    if (m_filteredGoalPressure != 0 || m_filteredGoalFlow != 0) {
        m_filteredGoalPressure = 0;
        m_filteredGoalFlow = 0;
        emit goalsChanged();
    }

    // Capture brew overrides before clearing temperature (used later when saving shot)
    // These ALWAYS have values - either user override or profile default
    double shotTemperatureOverride = 0.0;
    double shotYieldOverride = 0.0;

    if (m_settings) {
        // Temperature: user override OR profile's espresso temperature
        if (m_settings->hasTemperatureOverride()) {
            shotTemperatureOverride = m_settings->temperatureOverride();
        } else {
            shotTemperatureOverride = m_profileManager->currentProfile().espressoTemperature();
        }

        // Yield: user override OR profile's target weight (0 for volume-based profiles)
        if (m_settings->hasBrewYieldOverride()) {
            shotYieldOverride = m_settings->brewYieldOverride();
        } else if (m_profileManager->currentProfile().targetWeight() > 0) {
            shotYieldOverride = m_profileManager->currentProfile().targetWeight();
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

    // Use extraction end time (excludes SAW settling phase) for accurate duration.
    // extractionDuration() is set for all shots (SAW and non-SAW) in endShot().
    // Falls back to rawTime only if timing controller is unavailable.
    double duration = (m_timingController && m_timingController->extractionDuration() > 0)
        ? m_timingController->extractionDuration()
        : m_shotDataModel->rawTime();

    double doseWeight = m_settings->dyeBeanWeight();
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

    // Stop debug logging and get the captured log
    QString debugLog;
    if (m_shotDebugLogger) {
        m_shotDebugLogger->stopCapture();
        debugLog = m_shotDebugLogger->getCapturedLog();
    }
    m_pendingDebugLog = debugLog;

    // Build metadata for history
    ShotMetadata metadata;
    metadata.beanBrand = m_settings->dyeBeanBrand();
    metadata.beanType = m_settings->dyeBeanType();
    metadata.roastDate = m_settings->dyeRoastDate();
    metadata.roastLevel = m_settings->dyeRoastLevel();
    metadata.grinderBrand = m_settings->dyeGrinderBrand();
    metadata.grinderModel = m_settings->dyeGrinderModel();
    metadata.grinderBurrs = m_settings->dyeGrinderBurrs();
    metadata.grinderSetting = m_settings->dyeGrinderSetting();
    metadata.beanWeight = m_settings->dyeBeanWeight();
    metadata.drinkWeight = m_settings->dyeDrinkWeight();
    metadata.drinkTds = m_settings->dyeDrinkTds();
    metadata.drinkEy = m_settings->dyeDrinkEy();
    metadata.espressoEnjoyment = m_settings->dyeEspressoEnjoyment();
    metadata.espressoNotes = m_settings->dyeShotNotes();
    metadata.barista = m_settings->dyeBarista();

    // For volume/timer-based profiles (targetWeight=0), use the actual final weight
    // so favorites can restore a meaningful yield target
    if (shotYieldOverride <= 0 && finalWeight > 0) {
        shotYieldOverride = finalWeight;
    }

    // Capture once so both the async callback and synchronous code use the same value
    bool showPostShot = m_settings->visualizerShowAfterShot();

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
            connect(m_shotHistory, &ShotHistoryStorage::shotSaved, this, [this, finalWeight, shotDateTime, showPostShot](qint64 shotId) {
                m_savingShot = false;

                if (shotId > 0) {
                    qDebug() << "[metadata] Shot saved to history with ID:" << shotId;

                    // Store shot ID for post-shot review page (so it can edit the saved shot)
                    m_lastSavedShotId = shotId;
                    emit lastSavedShotIdChanged();

                    // Set shot date/time for display on metadata page
                    m_settings->setDyeShotDateTime(shotDateTime);
                    qDebug() << "[metadata] Set dyeShotDateTime to:" << shotDateTime;

                    // Update the drink weight with actual final weight from this shot
                    m_settings->setDyeDrinkWeight(finalWeight);
                    qDebug() << "[metadata] Set dyeDrinkWeight to:" << finalWeight;

                    // Reset shot-specific metadata for the next shot
                    // Bean/grinder info persists (sticky), but per-shot fields reset
                    m_settings->setDyeEspressoEnjoyment(m_settings->defaultShotRating());
                    m_settings->setDyeShotNotes("");
                    m_settings->setDyeDrinkTds(0);
                    m_settings->setDyeDrinkEy(0);
                    qDebug() << "[metadata] Reset enjoyment, notes, TDS, EY for next shot";

                    // Force QSettings to sync to disk immediately
                    m_settings->sync();

                    // Now that we have a valid shot ID, show the metadata page
                    if (showPostShot) {
                        qDebug() << "[metadata] Showing post-shot review page with shotId:" << shotId;
                        emit shotEndedShowMetadata();
                    }
                } else {
                    qWarning() << "[metadata] Failed to save shot to history (returned" << shotId << ") - metadata preserved for next attempt";
                    m_lastSavedShotId = 0;
                    emit lastSavedShotIdChanged();

                    // Still navigate to review page so user isn't stranded on espresso page
                    if (showPostShot) {
                        qWarning() << "[metadata] Shot save failed but still showing review page";
                        emit shotEndedShowMetadata();
                    }
                }
            }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

            m_shotHistory->saveShot(
                m_shotDataModel, m_profileManager->currentProfilePtr(),
                duration, finalWeight, doseWeight,
                metadata, debugLog,
                shotTemperatureOverride, shotYieldOverride);
        }
    } else {
        qWarning() << "[metadata] Could not save shot - history not ready!";

        // Still navigate to review page so user isn't stranded on espresso page
        if (showPostShot) {
            emit shotEndedShowMetadata();
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

    // Auto-upload if enabled (do this first, before showing metadata page)
    if (m_settings->visualizerAutoUpload() && m_visualizer) {
        qDebug() << "  -> Auto-uploading to visualizer";
        m_visualizer->uploadShot(m_shotDataModel, m_profileManager->currentProfilePtr(), duration, finalWeight, doseWeight, metadata, debugLog);
    }

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
    metadata.beanBrand = m_settings->dyeBeanBrand();
    metadata.beanType = m_settings->dyeBeanType();
    metadata.roastDate = m_settings->dyeRoastDate();
    metadata.roastLevel = m_settings->dyeRoastLevel();
    metadata.grinderBrand = m_settings->dyeGrinderBrand();
    metadata.grinderModel = m_settings->dyeGrinderModel();
    metadata.grinderBurrs = m_settings->dyeGrinderBurrs();
    metadata.grinderSetting = m_settings->dyeGrinderSetting();
    metadata.beanWeight = m_settings->dyeBeanWeight();
    metadata.drinkWeight = m_settings->dyeDrinkWeight();
    metadata.drinkTds = m_settings->dyeDrinkTds();
    metadata.drinkEy = m_settings->dyeDrinkEy();
    metadata.espressoEnjoyment = m_settings->dyeEspressoEnjoyment();
    metadata.barista = m_settings->dyeBarista();

    // Build notes: user notes + AI recommendation (if any)
    QString notes = m_settings->dyeShotNotes();
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

    m_visualizer->uploadShot(m_shotDataModel, m_profileManager->currentProfilePtr(),
                             m_pendingShotDuration, m_pendingShotFinalWeight,
                             m_pendingShotDoseWeight, metadata, m_pendingDebugLog);

    m_hasPendingShot = false;
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
            metadata.beanBrand = m_settings->dyeBeanBrand();
            metadata.beanType = m_settings->dyeBeanType();
            metadata.roastDate = m_settings->dyeRoastDate();
            metadata.roastLevel = m_settings->dyeRoastLevel();
            metadata.grinderBrand = m_settings->dyeGrinderBrand();
            metadata.grinderModel = m_settings->dyeGrinderModel();
            metadata.grinderBurrs = m_settings->dyeGrinderBurrs();
            metadata.grinderSetting = m_settings->dyeGrinderSetting();
            metadata.beanWeight = m_pendingShotDoseWeight;
            metadata.drinkWeight = m_settings->dyeDrinkWeight();
            metadata.drinkTds = m_settings->dyeDrinkTds();
            metadata.drinkEy = m_settings->dyeDrinkEy();
            metadata.espressoEnjoyment = m_settings->dyeEspressoEnjoyment();
            metadata.espressoNotes = m_settings->dyeShotNotes();
            metadata.barista = m_settings->dyeBarista();

            // Use current profile's temperature and target weight as overrides
            double temperatureOverride = m_profileManager->currentProfile().espressoTemperature();
            double yieldOverride = m_profileManager->currentProfile().targetWeight();

            double pendingFinalWeight = m_pendingShotFinalWeight;
            connect(m_shotHistory, &ShotHistoryStorage::shotSaved, this, [this, pendingFinalWeight](qint64 shotId) {
                m_savingShot = false;

                if (shotId > 0) {
                    qDebug() << "DEV: Simulated shot saved to history with ID:" << shotId;
                    m_lastSavedShotId = shotId;
                    emit lastSavedShotIdChanged();

                    // Update drink weight
                    m_settings->setDyeDrinkWeight(pendingFinalWeight);

                    // Reset shot-specific metadata for next shot
                    m_settings->setDyeEspressoEnjoyment(m_settings->defaultShotRating());
                    m_settings->setDyeShotNotes("");
                    m_settings->setDyeDrinkTds(0);
                    m_settings->setDyeDrinkEy(0);
                    m_settings->sync();
                } else {
                    qWarning() << "DEV: Failed to save simulated shot to history";
                }
            }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));

            m_shotHistory->saveShot(
                m_shotDataModel, m_profileManager->currentProfilePtr(),
                totalDuration, m_pendingShotFinalWeight, m_pendingShotDoseWeight,
                metadata, "[Simulated shot]",
                temperatureOverride, yieldOverride);
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

void MainController::onShotSampleReceived(const ShotSample& sample) {
    if (!m_shotDataModel || !m_machineState) {
        return;
    }

    MachineState::Phase phase = m_machineState->phase();

    // Update steam calibrator with heater temp (needed for recovery detection between steps)
    if (m_steamCalibrator)
        m_steamCalibrator->updateHeaterTemp(sample.steamTemp);

    // Forward flow samples to MachineState for FlowScale during any dispensing phase
    bool isDispensingPhase = (phase == MachineState::Phase::Preinfusion ||
                              phase == MachineState::Phase::Pouring ||
                              phase == MachineState::Phase::Steaming ||
                              phase == MachineState::Phase::HotWater ||
                              phase == MachineState::Phase::Flushing);

    if (isDispensingPhase && m_lastSampleTime > 0) {
        double deltaTime = sample.timer - m_lastSampleTime;
        if (deltaTime > 0 && deltaTime < 1.0) {
            m_machineState->onFlowSample(sample.groupFlow, deltaTime);

            // Note: FlowScale is fed by MachineState when it is the active scale (no physical scale).
            // Shadow feeding when a physical scale is present was only used for FlowScale Compare
            // logging and has been removed to reduce main-thread load on slow devices.
        }
    }
    m_lastSampleTime = sample.timer;

    // Record steam data during Steaming phase
    if (phase == MachineState::Phase::Steaming && m_steamDataModel) {
        if (m_steamStartTime == 0) {
            m_steamStartTime = sample.timer;
            m_steamDataModel->clear();
            // Add flow goal line from current settings
            double flowGoal = m_settings->steamFlow() / 100.0;
            m_steamDataModel->addFlowGoalPoint(0, flowGoal);
            m_steamDataModel->addFlowGoalPoint(m_settings->steamTimeout(), flowGoal);
            if (m_steamHealthTracker)
                m_steamHealthTracker->resetSession();
            if (m_steamCalibrator)
                m_steamCalibrator->onSteamStarted();
        }
        double t = sample.timer - m_steamStartTime;
        m_steamDataModel->addSample(t, sample.groupPressure, sample.groupFlow, sample.steamTemp);

        // Steam calibration: track elapsed time and auto-stop when enough data
        if (m_steamCalibrator)
            m_steamCalibrator->onSteamSample(t);

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

    // First sample of this espresso cycle - set the base time
    if (m_shotStartTime == 0) {
        m_shotStartTime = sample.timer;
        m_lastSampleTime = sample.timer;
    }

    double time = sample.timer - m_shotStartTime;

    // Store for weight sample sync
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

    // Track latest sensor values for transition reason inference
    m_lastPressure = sample.groupPressure;
    m_lastFlow = sample.groupFlow;

    // Determine active pump mode for current frame (to show only active goal curve)
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

    // Forward to timing controller for unified timing
    if (m_timingController) {
        m_timingController->onShotSample(sample, pressureGoal, flowGoal, sample.setTempGoal,
                                          sample.frameNumber, isFlowMode);
        // Use timing controller's time for graph data (ensures weight and other curves align)
        time = m_timingController->shotTime();
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

void MainController::setRefractometer(DiFluidR2* refractometer) {
    // Disconnect old signal chain before connecting new one
    if (m_refractometer) {
        disconnect(m_refractometer, nullptr, this, nullptr);
    }
    m_refractometer = refractometer;
    if (!m_refractometer) return;

    // When R2 sends a TDS reading, populate the DYE field.
    // EY calculation is NOT done here — PostShotReviewPage QML is the single
    // source of truth for EY when the page is active (it uses page-local
    // editDoseWeight/editDrinkWeight which may differ from Settings).
    connect(m_refractometer, &DiFluidR2::tdsChanged, this, [this](double tds) {
        if (tds > 0 && m_settings) {
            qDebug() << "[Refractometer] TDS received:" << tds << "- populating DYE field";
            m_settings->setDyeDrinkTds(tds);
        }
    });
}


