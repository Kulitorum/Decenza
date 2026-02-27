#include "maincontroller.h"
#include "shottimingcontroller.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../models/shotdatamodel.h"
#include "../profile/recipegenerator.h"
#include "../profile/recipeanalyzer.h"
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
#include <cmath>
#include <QDir>
#include <QFile>
#include <QTextStream>
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

#ifndef Q_OS_WIN
#include <dlfcn.h>   // For dladdr() to resolve caller symbols
#include <unwind.h>  // For _Unwind_Backtrace stack walking

// Helper for stack unwinding
struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

static size_t captureBacktrace(void** buffer, size_t maxFrames) {
    BacktraceState state = {buffer, buffer + maxFrames};
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}
#endif


MainController::MainController(Settings* settings, DE1Device* device,
                               MachineState* machineState, ShotDataModel* shotDataModel,
                               ProfileStorage* profileStorage,
                               QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_device(device)
    , m_machineState(machineState)
    , m_shotDataModel(shotDataModel)
    , m_profileStorage(profileStorage)
{
    // Set up delayed settings timer (1 second after connection)
    // The initial settings from DE1Device use hardcoded values; we need to send
    // user settings quickly to set the correct steam temperature for keepHeaterOn.
    m_settingsTimer.setSingleShot(true);
    m_settingsTimer.setInterval(1000);
    connect(&m_settingsTimer, &QTimer::timeout, this, &MainController::applyAllSettings);

    // Connect to shot sample updates
    if (m_device) {
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &MainController::onShotSampleReceived);

        // Start delayed settings timer after device initial settings complete
        connect(m_device, &DE1Device::initialSettingsComplete, this, [this]() {
            m_settingsTimer.start();
        });
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
    }

    // Create visualizer uploader and importer
    m_visualizer = new VisualizerUploader(m_settings, this);
    m_visualizerImporter = new VisualizerImporter(this, m_settings, this);

    // Create shot history storage and comparison model
    m_shotHistory = new ShotHistoryStorage(this);
    m_shotHistory->initialize();

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
        loadProfile(profileName);
    });

    // Update MQTT with current profile when it changes
    connect(this, &MainController::currentProfileChanged, this, [this]() {
        if (m_mqttClient) {
            m_mqttClient->setCurrentProfile(m_currentProfile.title());
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
        // Delay connection to allow BLE to initialize
        QTimer::singleShot(3000, this, [this]() {
            if (m_settings->mqttEnabled()) {
                m_mqttClient->connectToBroker();
            }
        });
    }

    // Initialize location provider and shot reporter for decenza.coffee shot map
    m_locationProvider = new LocationProvider(this);
    m_shotReporter = new ShotReporter(m_settings, m_locationProvider, this);

    // Request location update if shot reporting is enabled
    if (m_settings && m_settings->value("shotmap/enabled", false).toBool()) {
        m_locationProvider->requestUpdate();
    }

    // Initialize update checker
    m_updateChecker = new UpdateChecker(m_settings, this);

    // Create data migration client for importing from other devices
    m_dataMigration = new DataMigrationClient(this);
    m_dataMigration->setSettings(m_settings);
    m_dataMigration->setProfileStorage(m_profileStorage);
    m_dataMigration->setShotHistoryStorage(m_shotHistory);

    // Refresh profiles when storage permission changes (Android)
    if (m_profileStorage) {
        connect(m_profileStorage, &ProfileStorage::configuredChanged, this, [this]() {
            if (m_profileStorage->isConfigured()) {
                qDebug() << "[MainController] Storage configured, refreshing profiles";
                refreshProfiles();
            }
        });
    }

    // Migrate profile folders (one-time migration for existing users)
    migrateProfileFolders();

    // Load initial profile
    refreshProfiles();

    // Check for temp file (modified profile from previous session)
    QString tempPath = profilesPath() + "/_current.json";
    if (QFile::exists(tempPath)) {
        qDebug() << "Loading modified profile from temp file:" << tempPath;
        m_currentProfile = Profile::loadFromFile(tempPath);
        m_profileModified = true;
        // Get the base profile name from settings
        if (m_settings) {
            m_baseProfileName = m_settings->currentProfile();
            // Sync selectedFavoriteProfile so UI shows correct pill
            int favoriteIndex = m_settings->findFavoriteIndexByFilename(m_baseProfileName);
            m_settings->setSelectedFavoriteProfile(favoriteIndex);
            // Sync overrides so shot plan shows correct profile data
            m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
            m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
        }
        if (m_machineState) {
            m_machineState->setTargetWeight(targetWeight());
        }
        // Upload to machine if connected
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }
    } else if (m_settings) {
        loadProfile(m_settings->currentProfile());
    } else {
        loadDefaultProfile();
    }

    // Keep MachineState in sync when yield override changes in Settings
    if (m_settings) {
        connect(m_settings, &Settings::brewOverridesChanged, this, [this]() {
            if (m_machineState) {
                m_machineState->setTargetWeight(targetWeight());
            }
            emit targetWeightChanged();
        });
        connect(m_settings, &Settings::dyeBeanWeightChanged, this, [this]() {
            emit targetWeightChanged();
        });

        // Update profile lists when selection/hidden state changes
        connect(m_settings, &Settings::selectedBuiltInProfilesChanged, this, &MainController::profilesChanged);
        connect(m_settings, &Settings::hiddenProfilesChanged, this, &MainController::profilesChanged);
    }
}

QString MainController::currentProfileName() const {
    if (m_profileModified) {
        return "*" + m_currentProfile.title();
    }
    return m_currentProfile.title();
}



bool MainController::isDFlowTitle(const QString& title) {
    // Check if title indicates a D-Flow profile (matching de1app behavior)
    // Ignores leading * (modified indicator that may come from imports)
    QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
    return t.startsWith(QStringLiteral("D-Flow"), Qt::CaseInsensitive);
}

bool MainController::isAFlowTitle(const QString& title) {
    // Check if title indicates an A-Flow profile (matching de1app behavior)
    // Ignores leading * (modified indicator that may come from imports)
    QString t = title.startsWith(QLatin1Char('*')) ? title.mid(1) : title;
    return t.startsWith(QStringLiteral("A-Flow"), Qt::CaseInsensitive);
}

bool MainController::isCurrentProfileRecipe() const {
    // Match de1app behavior: detect D-Flow/A-Flow profiles by title prefix at runtime
    if (m_currentProfile.isRecipeMode()) {
        return true;
    }
    return isDFlowTitle(m_currentProfile.title()) || isAFlowTitle(m_currentProfile.title());
}

QString MainController::currentEditorType() const {
    if (m_currentProfile.isRecipeMode()) {
        return editorTypeToString(m_currentProfile.recipeParams().editorType);
    }
    // Detect by profile type for non-recipe-mode profiles
    QString profileType = m_currentProfile.profileType();
    if (profileType == "settings_2a") {
        return QStringLiteral("pressure");
    }
    if (profileType == "settings_2b") {
        return QStringLiteral("flow");
    }
    // Detect by title prefix
    if (isDFlowTitle(m_currentProfile.title())) return QStringLiteral("dflow");
    if (isAFlowTitle(m_currentProfile.title())) return QStringLiteral("aflow");
    return QStringLiteral("advanced");
}

double MainController::targetWeight() const {
    if (m_settings && m_settings->hasBrewYieldOverride()) {
        return m_settings->brewYieldOverride();
    }
    return m_currentProfile.targetWeight();
}

bool MainController::brewByRatioActive() const {
    if (!m_settings || !m_settings->hasBrewYieldOverride())
        return false;
    return qAbs(m_settings->brewYieldOverride() - m_currentProfile.targetWeight()) > 0.1;
}

double MainController::brewByRatioDose() const {
    return m_settings ? m_settings->dyeBeanWeight() : 0.0;
}

double MainController::brewByRatio() const {
    if (!m_settings || !m_settings->hasBrewYieldOverride()) return 0.0;
    double dose = m_settings->dyeBeanWeight();
    return dose > 0 ? m_settings->brewYieldOverride() / dose : 0.0;
}

void MainController::setTargetWeight(double weight) {
    if (m_currentProfile.targetWeight() != weight) {
        m_currentProfile.setTargetWeight(weight);
        if (m_machineState) {
            m_machineState->setTargetWeight(weight);
        }
        emit targetWeightChanged();
    }
}


void MainController::activateBrewWithOverrides(double dose, double yield, double temperature, const QString& grind) {
    if (m_settings) {
        m_settings->setDyeBeanWeight(dose);
        m_settings->setDyeGrinderSetting(grind);
        m_settings->setBrewYieldOverride(yield);

        m_settings->setTemperatureOverride(temperature);
    }

    double ratio = dose > 0 ? yield / dose : 2.0;
    qDebug() << "Brew overrides activated: dose=" << dose << "g, ratio=1:" << ratio
             << "-> target=" << yield << "g";

    // MachineState sync happens via brewOverridesChanged signal connection

    // Re-upload profile with temperature applied to machine frames
    uploadCurrentProfile();
}

void MainController::clearBrewOverrides() {
    if (m_settings) {
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }
    // MachineState sync happens via brewOverridesChanged signal connection
    qDebug() << "Brew overrides reset to profile defaults, target=" << m_currentProfile.targetWeight() << "g";
}

QVariantList MainController::availableProfiles() const {
    QVariantList result;
    for (const QString& name : m_availableProfiles) {
        QVariantMap profile;
        profile["name"] = name;  // filename for loading
        profile["title"] = m_profileTitles.value(name, name);  // display title
        result.append(profile);
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList MainController::selectedProfiles() const {
    QVariantList result;

    // Get selected built-in profile names from settings
    QStringList selectedBuiltIns = m_settings ? m_settings->selectedBuiltInProfiles() : QStringList();
    QStringList hiddenProfiles = m_settings ? m_settings->hiddenProfiles() : QStringList();

    for (const ProfileInfo& info : m_allProfiles) {
        bool include = false;

        switch (info.source) {
        case ProfileSource::BuiltIn:
            // Only include if selected
            include = selectedBuiltIns.contains(info.filename);
            break;
        case ProfileSource::Downloaded:
        case ProfileSource::UserCreated:
            // Include unless explicitly hidden
            include = !hiddenProfiles.contains(info.filename);
            break;
        }

        if (include) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isRecipeMode"] = info.isRecipeMode;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList MainController::allBuiltInProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::BuiltIn) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isRecipeMode"] = info.isRecipeMode;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList MainController::cleaningProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        // Include both cleaning and descale profiles in this category
        if (info.beverageType == "cleaning" || info.beverageType == "descale") {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isRecipeMode"] = info.isRecipeMode;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList MainController::downloadedProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::Downloaded) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isRecipeMode"] = info.isRecipeMode;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList MainController::userCreatedProfiles() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::UserCreated) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isRecipeMode"] = info.isRecipeMode;
            result.append(profile);
        }
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

QVariantList MainController::allProfilesList() const {
    QVariantList result;

    for (const ProfileInfo& info : m_allProfiles) {
        QVariantMap profile;
        profile["name"] = info.filename;
        profile["title"] = info.title;
        profile["beverageType"] = info.beverageType;
        profile["source"] = static_cast<int>(info.source);
        profile["isRecipeMode"] = info.isRecipeMode;
        result.append(profile);
    }

    // Sort by title alphabetically (case-insensitive)
    std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["title"].toString().compare(
            b.toMap()["title"].toString(), Qt::CaseInsensitive) < 0;
    });

    return result;
}

bool MainController::deleteProfile(const QString& filename) {
    // Find the profile info
    ProfileSource source = ProfileSource::BuiltIn;
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.filename == filename) {
            source = info.source;
            break;
        }
    }

    bool deleted = false;

    // Always try to clean up ProfileStorage copies (even for built-in profiles,
    // since imported copies can shadow the built-in version)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        if (m_profileStorage->deleteProfile(filename)) {
            qDebug() << "Deleted profile from ProfileStorage:" << filename;
            deleted = true;
        }
    }

    // Always try to clean up local folder copies
    QString userPath = userProfilesPath() + "/" + filename + ".json";
    if (QFile::remove(userPath)) {
        qDebug() << "Deleted profile from user storage:" << userPath;
        deleted = true;
    }
    QString downloadedPath = downloadedProfilesPath() + "/" + filename + ".json";
    if (QFile::remove(downloadedPath)) {
        qDebug() << "Deleted profile from downloaded storage:" << downloadedPath;
        deleted = true;
    }

    // Built-in profiles can't be fully deleted (they'll still show from QRC),
    // but we cleaned up any local overrides above
    if (source == ProfileSource::BuiltIn) {
        if (deleted) {
            qDebug() << "Cleaned up local override for built-in profile:" << filename;
            refreshProfiles();
        }
        return false;
    }

    if (deleted) {
        // Remove from favorites if it was a favorite
        if (m_settings && m_settings->isFavoriteProfile(filename)) {
            // Find index and remove
            QVariantList favorites = m_settings->favoriteProfiles();
            for (int i = 0; i < favorites.size(); ++i) {
                if (favorites[i].toMap()["filename"].toString() == filename) {
                    m_settings->removeFavoriteProfile(i);
                    break;
                }
            }
        }

        // Refresh the profile list
        refreshProfiles();
        return true;
    }

    qWarning() << "Failed to delete profile:" << filename;
    return false;
}

QVariantMap MainController::getCurrentProfile() const {
    QVariantMap profile;
    profile["title"] = m_currentProfile.title();
    profile["author"] = m_currentProfile.author();
    profile["profile_notes"] = m_currentProfile.profileNotes();
    profile["target_weight"] = m_currentProfile.targetWeight();
    profile["target_volume"] = m_currentProfile.targetVolume();
    profile["stop_at_type"] = m_currentProfile.stopAtType() == Profile::StopAtType::Volume ? "volume" : "weight";
    profile["espresso_temperature"] = m_currentProfile.espressoTemperature();
    profile["mode"] = m_currentProfile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";
    profile["has_recommended_dose"] = m_currentProfile.hasRecommendedDose();
    profile["recommended_dose"] = m_currentProfile.recommendedDose();
    profile["tank_desired_water_temperature"] = m_currentProfile.tankDesiredWaterTemperature();
    profile["maximum_flow_range_advanced"] = m_currentProfile.maximumFlowRangeAdvanced();
    profile["maximum_pressure_range_advanced"] = m_currentProfile.maximumPressureRangeAdvanced();
    profile["maximum_pressure"] = m_currentProfile.maximumPressure();
    profile["maximum_flow"] = m_currentProfile.maximumFlow();
    profile["preinfuse_frame_count"] = m_currentProfile.preinfuseFrameCount();

    QVariantList steps;
    for (const auto& frame : m_currentProfile.steps()) {
        QVariantMap step;
        step["name"] = frame.name;
        step["temperature"] = frame.temperature;
        step["sensor"] = frame.sensor;
        step["pump"] = frame.pump;
        step["transition"] = frame.transition;
        step["pressure"] = frame.pressure;
        step["flow"] = frame.flow;
        step["seconds"] = frame.seconds;
        step["volume"] = frame.volume;
        step["exit_if"] = frame.exitIf;
        step["exit_type"] = frame.exitType;
        step["exit_pressure_over"] = frame.exitPressureOver;
        step["exit_pressure_under"] = frame.exitPressureUnder;
        step["exit_flow_over"] = frame.exitFlowOver;
        step["exit_flow_under"] = frame.exitFlowUnder;
        step["exit_weight"] = frame.exitWeight;
        step["popup"] = frame.popup;
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    profile["steps"] = steps;

    return profile;
}

QVariantMap MainController::getProfileByFilename(const QString& filename) const {
    Profile profile;
    bool found = false;

    // 1. Check ProfileStorage first (SAF folder on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        QString jsonContent = m_profileStorage->readProfile(filename);
        if (!jsonContent.isEmpty()) {
            profile = Profile::loadFromJsonString(jsonContent);
            found = true;
        }
    }

    // 2. Check user profiles (local fallback)
    if (!found) {
        QString path = userProfilesPath() + "/" + filename + ".json";
        if (QFile::exists(path)) {
            profile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 3. Check downloaded profiles (local fallback)
    if (!found) {
        QString path = downloadedProfilesPath() + "/" + filename + ".json";
        if (QFile::exists(path)) {
            profile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 4. Check built-in profiles
    if (!found) {
        QString path = ":/profiles/" + filename + ".json";
        if (QFile::exists(path)) {
            profile = Profile::loadFromFile(path);
            found = true;
        }
    }

    if (!found) {
        return QVariantMap();  // Return empty map if not found
    }

    // Backfill empty notes from built-in profile (handles imported copies from before notes were added)
    if (profile.profileNotes().isEmpty()) {
        QString builtInPath = ":/profiles/" + filename + ".json";
        if (QFile::exists(builtInPath)) {
            Profile builtIn = Profile::loadFromFile(builtInPath);
            if (!builtIn.profileNotes().isEmpty()) {
                profile.setProfileNotes(builtIn.profileNotes());
            }
        }
    }

    // Build result map (same format as getCurrentProfile)
    QVariantMap result;
    result["title"] = profile.title();
    result["author"] = profile.author();
    result["profile_notes"] = profile.profileNotes();
    result["target_weight"] = profile.targetWeight();
    result["target_volume"] = profile.targetVolume();
    result["stop_at_type"] = profile.stopAtType() == Profile::StopAtType::Volume ? "volume" : "weight";
    result["espresso_temperature"] = profile.espressoTemperature();
    result["mode"] = profile.mode() == Profile::Mode::FrameBased ? "frame_based" : "direct";
    result["has_recommended_dose"] = profile.hasRecommendedDose();
    result["recommended_dose"] = profile.recommendedDose();
    result["tank_desired_water_temperature"] = profile.tankDesiredWaterTemperature();
    result["maximum_flow_range_advanced"] = profile.maximumFlowRangeAdvanced();
    result["maximum_pressure_range_advanced"] = profile.maximumPressureRangeAdvanced();
    result["maximum_pressure"] = profile.maximumPressure();
    result["maximum_flow"] = profile.maximumFlow();
    result["preinfuse_frame_count"] = profile.preinfuseFrameCount();

    QVariantList steps;
    for (const auto& frame : profile.steps()) {
        QVariantMap step;
        step["name"] = frame.name;
        step["temperature"] = frame.temperature;
        step["sensor"] = frame.sensor;
        step["pump"] = frame.pump;
        step["transition"] = frame.transition;
        step["pressure"] = frame.pressure;
        step["flow"] = frame.flow;
        step["seconds"] = frame.seconds;
        step["volume"] = frame.volume;
        step["exit_if"] = frame.exitIf;
        step["exit_type"] = frame.exitType;
        step["exit_pressure_over"] = frame.exitPressureOver;
        step["exit_pressure_under"] = frame.exitPressureUnder;
        step["exit_flow_over"] = frame.exitFlowOver;
        step["exit_flow_under"] = frame.exitFlowUnder;
        step["exit_weight"] = frame.exitWeight;
        step["popup"] = frame.popup;
        step["max_flow_or_pressure"] = frame.maxFlowOrPressure;
        step["max_flow_or_pressure_range"] = frame.maxFlowOrPressureRange;
        steps.append(step);
    }
    result["steps"] = steps;

    return result;
}

void MainController::loadProfile(const QString& profileName) {
    QString path;
    bool found = false;

    // Resolve profile name: could be title or filename (MQTT publishes titles)
    QString resolvedName = profileName;

    // First, check if it's a title (most common case from MQTT)
    for (auto it = m_profileTitles.begin(); it != m_profileTitles.end(); ++it) {
        if (it.value() == profileName) {
            resolvedName = it.key();  // Found filename for this title
            qDebug() << "MainController::loadProfile: Resolved title" << profileName << "to filename" << resolvedName;
            break;
        }
    }

    // If not found as title, assume it's already a filename (fallback)

    // 1. Check ProfileStorage first (SAF folder on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        QString jsonContent = m_profileStorage->readProfile(resolvedName);
        if (!jsonContent.isEmpty()) {
            m_currentProfile = Profile::loadFromJsonString(jsonContent);
            found = true;
            qDebug() << "Loaded profile from ProfileStorage:" << resolvedName;
        }
    }

    // 2. Check user profiles (local fallback)
    if (!found) {
        path = userProfilesPath() + "/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 3. Check downloaded profiles (local fallback)
    if (!found) {
        path = downloadedProfilesPath() + "/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 4. Check built-in profiles
    if (!found) {
        path = ":/profiles/" + resolvedName + ".json";
        if (QFile::exists(path)) {
            m_currentProfile = Profile::loadFromFile(path);
            found = true;
        }
    }

    // 5. Fall back to default
    if (!found) {
        qWarning() << "MainController::loadProfile: Profile not found:" << profileName << "(resolved:" << resolvedName << ")";
        loadDefaultProfile();
    }

    // Backfill empty notes from built-in profile (handles imported copies from before notes were added)
    if (found && m_currentProfile.profileNotes().isEmpty()) {
        QString builtInPath = ":/profiles/" + resolvedName + ".json";
        if (QFile::exists(builtInPath)) {
            Profile builtIn = Profile::loadFromFile(builtInPath);
            if (!builtIn.profileNotes().isEmpty()) {
                m_currentProfile.setProfileNotes(builtIn.profileNotes());
            }
        }
    }

    // Track the base profile name (filename without extension)
    m_baseProfileName = resolvedName;
    bool wasModified = m_profileModified;
    m_profileModified = false;

    // Remove stale temp file so next startup loads the correct profile
    if (wasModified) {
        QString tempPath = profilesPath() + "/_current.json";
        QFile::remove(tempPath);
    }

    if (m_settings) {
        m_settings->setCurrentProfile(resolvedName);
        // Sync selectedFavoriteProfile with the loaded profile
        // This ensures the UI shows the correct pill as selected, or -1 if not a favorite
        int favoriteIndex = m_settings->findFavoriteIndexByFilename(resolvedName);
        qDebug() << "loadProfile: resolvedName=" << resolvedName << "favoriteIndex=" << favoriteIndex;
        m_settings->setSelectedFavoriteProfile(favoriteIndex);
    }

    // Initialize yield and temperature from the new profile
    // These are "shot plan" settings that always reflect the current plan
    if (m_settings) {
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());

        // Apply recommended dose from profile if set
        // Deferred to next event loop to avoid QML signal cascade during profile load
        if (m_currentProfile.hasRecommendedDose() && m_currentProfile.recommendedDose() > 0) {
            double dose = m_currentProfile.recommendedDose();
            QTimer::singleShot(0, this, [this, dose]() {
                if (m_settings) {
                    m_settings->setDyeBeanWeight(dose);
                }
            });
        }
    }

    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setStopAtType(
            m_currentProfile.stopAtType() == Profile::StopAtType::Volume
                ? MachineState::StopAtType::Volume
                : MachineState::StopAtType::Weight);
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    // Apply per-profile flow calibration if auto-cal is enabled
    applyFlowCalibration();

    emit currentProfileChanged();
    emit targetWeightChanged();
    if (wasModified) {
        emit profileModifiedChanged();
    }
}

bool MainController::loadProfileFromJson(const QString& jsonContent) {
    if (jsonContent.isEmpty()) {
        qWarning() << "loadProfileFromJson: Empty JSON content";
        return false;
    }

    // Try our native format first
    m_currentProfile = Profile::loadFromJsonString(jsonContent);

    // If native format didn't work (empty title or no steps), try DE1 app format
    if (m_currentProfile.title().isEmpty() || m_currentProfile.steps().isEmpty()) {
        qDebug() << "Native JSON format failed, trying DE1 app format...";
        m_currentProfile = Profile::loadFromDE1AppJson(jsonContent);
    }

    if (m_currentProfile.title().isEmpty() || m_currentProfile.steps().isEmpty()) {
        qWarning() << "loadProfileFromJson: Failed to parse profile JSON in any format";
        return false;
    }

    // Use title as base name since this profile isn't from a file
    m_baseProfileName = m_currentProfile.title();
    m_profileModified = false;

    if (m_settings) {
        // Set selectedFavoriteProfile to -1 to show non-favorite pill
        // Profiles loaded from JSON (e.g., shot history) are typically not in favorites
        m_settings->setSelectedFavoriteProfile(-1);

        // Initialize yield and temperature from the new profile
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }

    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        m_machineState->setStopAtType(
            m_currentProfile.stopAtType() == Profile::StopAtType::Volume
                ? MachineState::StopAtType::Volume
                : MachineState::StopAtType::Weight);
    }

    // Upload to machine if connected (for frame-based mode)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    qDebug() << "Loaded profile from JSON:" << m_currentProfile.title()
             << "with" << m_currentProfile.steps().size() << "steps";

    emit currentProfileChanged();
    emit targetWeightChanged();
    return true;
}

void MainController::loadShotWithMetadata(qint64 shotId) {
    if (!m_shotHistory) {
        qWarning() << "loadShotWithMetadata: No shot history storage";
        return;
    }

    // Get full shot record from history
    ShotRecord shotRecord = m_shotHistory->getShotRecord(shotId);
    if (shotRecord.summary.id <= 0) {
        qWarning() << "loadShotWithMetadata: Shot not found with id:" << shotId;
        return;
    }

    // Load the profile - prefer installed profile, fall back to stored JSON
    QString filename = findProfileByTitle(shotRecord.summary.profileName);
    qDebug() << "loadShotWithMetadata: profileTitle=" << shotRecord.summary.profileName
             << "filename=" << filename;
    if (!filename.isEmpty()) {
        loadProfile(filename);
    } else if (!shotRecord.profileJson.isEmpty()) {
        loadProfileFromJson(shotRecord.profileJson);
    } else {
        qWarning() << "loadShotWithMetadata: No profile data available for shot";
    }

    // Copy metadata to DYE settings
    if (m_settings) {
        m_settings->setDyeBeanBrand(shotRecord.summary.beanBrand);
        m_settings->setDyeBeanType(shotRecord.summary.beanType);
        m_settings->setDyeRoastDate(shotRecord.roastDate);
        m_settings->setDyeRoastLevel(shotRecord.roastLevel);
        m_settings->setDyeGrinderModel(shotRecord.grinderModel);
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
        qDebug() << "loadShotWithMetadata: Looking for bean preset - brand:" << shotRecord.summary.beanBrand
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
        } else if (shotRecord.summary.finalWeight > 0 && m_currentProfile.targetWeight() <= 0) {
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
                    : (shotRecord.summary.finalWeight > 0 && m_currentProfile.targetWeight() <= 0
                       ? QString::number(shotRecord.summary.finalWeight) + " (from actual)"
                       : "none"));

        // Re-upload profile with history overrides applied
        // loadProfile() already uploaded with profile defaults; now we have the actual overrides
        if (hasOverrides) {
            uploadCurrentProfile();
        }
    }
}

void MainController::refreshProfiles() {
    m_availableProfiles.clear();
    m_profileTitles.clear();
    m_allProfiles.clear();

    // Helper to load profile metadata from file path
    auto loadProfileMeta = [](const QString& path) -> std::tuple<QString, QString, bool> {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonObject obj = doc.object();
            return {obj["title"].toString(), obj["beverage_type"].toString(), obj["is_recipe_mode"].toBool(false)};
        }
        return {QString(), QString(), false};
    };

    // Helper to load profile metadata from JSON string
    auto loadProfileMetaFromJson = [](const QString& jsonContent) -> std::tuple<QString, QString, bool> {
        QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
        QJsonObject obj = doc.object();
        return {obj["title"].toString(), obj["beverage_type"].toString(), obj["is_recipe_mode"].toBool(false)};
    };

    QStringList filters;
    filters << "*.json";

    // 1. Load built-in profiles (always available)
    QDir builtInDir(":/profiles");
    QStringList files = builtInDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);  // Remove .json
        auto [title, beverageType, isRecipeMode] = loadProfileMeta(":/profiles/" + file);

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.source = ProfileSource::BuiltIn;
        info.isRecipeMode = isRecipeMode || isDFlowTitle(info.title) || isAFlowTitle(info.title);
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 2. Load profiles from ProfileStorage (SAF folder or fallback)
    // ProfileStorage takes loading priority over built-in (loadProfile checks it first),
    // so if a copy exists here it should override the built-in entry in the list too.
    if (m_profileStorage) {
        QStringList storageProfiles = m_profileStorage->listProfiles();
        for (const QString& name : storageProfiles) {
            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) {
                continue;
            }

            auto [title, beverageType, isRecipeMode] = loadProfileMetaFromJson(jsonContent);

            ProfileInfo info;
            info.filename = name;
            info.title = title.isEmpty() ? name : title;
            info.beverageType = beverageType;
            info.source = ProfileSource::UserCreated;
            info.isRecipeMode = isRecipeMode || isDFlowTitle(info.title) || isAFlowTitle(info.title);

            if (m_availableProfiles.contains(name)) {
                // Override built-in entry so list matches what loadProfile() actually loads
                for (int i = 0; i < m_allProfiles.size(); ++i) {
                    if (m_allProfiles[i].filename == name) {
                        m_allProfiles[i] = info;
                        break;
                    }
                }
            } else {
                m_allProfiles.append(info);
                m_availableProfiles.append(name);
            }
            m_profileTitles[name] = info.title;
        }
    }

    // 3. Load downloaded profiles (legacy local folder)
    QDir downloadedDir(downloadedProfilesPath());
    files = downloadedDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (m_availableProfiles.contains(name)) {
            continue;  // Skip if already loaded from ProfileStorage
        }

        auto [title, beverageType, isRecipeMode] = loadProfileMeta(downloadedDir.filePath(file));

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.source = ProfileSource::Downloaded;
        info.isRecipeMode = isRecipeMode || isDFlowTitle(info.title) || isAFlowTitle(info.title);
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 4. Load user-created profiles (legacy local folder)
    QDir userDir(userProfilesPath());
    files = userDir.entryList(filters, QDir::Files);
    for (const QString& file : files) {
        QString name = file.left(file.length() - 5);
        if (m_availableProfiles.contains(name)) {
            continue;  // Skip if already loaded from ProfileStorage
        }

        auto [title, beverageType, isRecipeMode] = loadProfileMeta(userDir.filePath(file));

        ProfileInfo info;
        info.filename = name;
        info.title = title.isEmpty() ? name : title;
        info.beverageType = beverageType;
        info.source = ProfileSource::UserCreated;
        info.isRecipeMode = isRecipeMode || isDFlowTitle(info.title) || isAFlowTitle(info.title);
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    emit profilesChanged();
    emit allBuiltInProfileListChanged();
}

void MainController::uploadCurrentProfile() {
    // Guard: Don't upload profile during active operations - this corrupts the running shot!
    if (m_machineState) {
        auto phase = m_machineState->phase();
        bool isActivePhase = (phase == MachineState::Phase::EspressoPreheating ||
                              phase == MachineState::Phase::Preinfusion ||
                              phase == MachineState::Phase::Pouring ||
                              phase == MachineState::Phase::Ending ||
                              phase == MachineState::Phase::Steaming ||
                              phase == MachineState::Phase::HotWater ||
                              phase == MachineState::Phase::Flushing ||
                              phase == MachineState::Phase::Descaling ||
                              phase == MachineState::Phase::Cleaning);

        if (isActivePhase) {
            qWarning() << "uploadCurrentProfile() BLOCKED during active phase:"
                       << m_machineState->phaseString();

            QString stackTrace = "Stack trace:\n";
#ifndef Q_OS_WIN
            void* stack[15];
            size_t frameCount = captureBacktrace(stack, 15);
            for (size_t i = 0; i < frameCount; i++) {
                Dl_info info;
                QString frameLine;
                if (dladdr(stack[i], &info) && info.dli_sname) {
                    frameLine = QString("  #%1: %2 (+%3)")
                        .arg(i)
                        .arg(info.dli_sname)
                        .arg(reinterpret_cast<quintptr>(stack[i]) - reinterpret_cast<quintptr>(info.dli_saddr));
                } else {
                    frameLine = QString("  #%1: 0x%2")
                        .arg(i)
                        .arg(reinterpret_cast<quintptr>(stack[i]), 0, 16);
                }
                stackTrace += frameLine + "\n";
                qWarning().noquote() << frameLine;
            }
#else
            stackTrace += "  (not available on Windows)\n";
#endif
            if (m_shotDebugLogger) {
                m_shotDebugLogger->logInfo(QString("BLOCKED uploadCurrentProfile() during %1\n%2")
                    .arg(m_machineState->phaseString())
                    .arg(stackTrace));
            }
            return;  // Don't upload!
        }
    }

    if (m_device && m_device->isConnected()) {
        double groupTemp;

        // Apply temperature override as delta offset (preserves per-frame differences)
        if (m_settings && m_settings->hasTemperatureOverride()) {
            Profile modifiedProfile = m_currentProfile;
            double overrideTemp = m_settings->temperatureOverride();
            double delta = overrideTemp - m_currentProfile.espressoTemperature();
            QList<ProfileFrame> steps = modifiedProfile.steps();
            for (int i = 0; i < steps.size(); ++i) {
                steps[i].temperature += delta;
            }
            modifiedProfile.setSteps(steps);
            modifiedProfile.setEspressoTemperature(overrideTemp);
            qDebug() << "Uploading profile with temperature override:" << overrideTemp
                     << "°C (delta:" << delta << "°C)";
            m_device->uploadProfile(modifiedProfile);
            groupTemp = overrideTemp;
        } else {
            m_device->uploadProfile(m_currentProfile);
            groupTemp = m_currentProfile.espressoTemperature();
        }

        // Update shot settings with the profile's target temperature
        // This controls what temperature the machine heats to in Ready state
        if (m_settings) {
            double steamTemp = (m_settings->steamDisabled() || !m_settings->keepSteamHeaterOn()) ? 0.0 : m_settings->steamTemperature();
            m_device->setShotSettings(
                steamTemp,
                m_settings->steamTimeout(),
                m_settings->waterTemperature(),
                m_settings->waterVolume(),
                groupTemp
            );
            qDebug() << "Set group temp to" << groupTemp << "°C for profile" << m_currentProfile.title();
        }
    }
}

void MainController::uploadProfile(const QVariantMap& profileData) {
    // Update current profile from QML data
    if (profileData.contains("title")) {
        m_currentProfile.setTitle(profileData["title"].toString());
    }
    if (profileData.contains("author")) {
        m_currentProfile.setAuthor(profileData["author"].toString());
    }
    if (profileData.contains("profile_notes")) {
        m_currentProfile.setProfileNotes(profileData["profile_notes"].toString());
    }
    if (profileData.contains("espresso_temperature")) {
        double newTemp = profileData["espresso_temperature"].toDouble();
        m_currentProfile.setEspressoTemperature(newTemp);
        // Sync temperature override so uploadCurrentProfile doesn't apply wrong delta
        if (m_settings) {
            m_settings->setTemperatureOverride(newTemp);
        }
    }
    if (profileData.contains("target_weight")) {
        double newWeight = profileData["target_weight"].toDouble();
        m_currentProfile.setTargetWeight(newWeight);
        if (m_machineState) {
            m_machineState->setTargetWeight(newWeight);
        }
        // Sync yield override so it reflects the new profile target
        if (m_settings) {
            m_settings->setBrewYieldOverride(newWeight);
        }
    }
    if (profileData.contains("target_volume")) {
        m_currentProfile.setTargetVolume(profileData["target_volume"].toDouble());
        if (m_machineState) {
            m_machineState->setTargetVolume(m_currentProfile.targetVolume());
        }
    }
    if (profileData.contains("stop_at_type")) {
        QString typeStr = profileData["stop_at_type"].toString();
        Profile::StopAtType type = (typeStr == "volume") ? Profile::StopAtType::Volume : Profile::StopAtType::Weight;
        m_currentProfile.setStopAtType(type);
        if (m_machineState) {
            m_machineState->setStopAtType(type == Profile::StopAtType::Volume ? MachineState::StopAtType::Volume : MachineState::StopAtType::Weight);
        }
    }

    if (profileData.contains("has_recommended_dose")) {
        m_currentProfile.setHasRecommendedDose(profileData["has_recommended_dose"].toBool());
    }
    if (profileData.contains("recommended_dose")) {
        m_currentProfile.setRecommendedDose(profileData["recommended_dose"].toDouble());
    }
    if (profileData.contains("tank_desired_water_temperature")) {
        m_currentProfile.setTankDesiredWaterTemperature(profileData["tank_desired_water_temperature"].toDouble());
    }
    if (profileData.contains("maximum_flow_range_advanced")) {
        m_currentProfile.setMaximumFlowRangeAdvanced(profileData["maximum_flow_range_advanced"].toDouble());
    }
    if (profileData.contains("maximum_pressure_range_advanced")) {
        m_currentProfile.setMaximumPressureRangeAdvanced(profileData["maximum_pressure_range_advanced"].toDouble());
    }
    if (profileData.contains("preinfuse_frame_count")) {
        m_currentProfile.setPreinfuseFrameCount(profileData["preinfuse_frame_count"].toInt());
    }

    // Update steps/frames - build new list atomically to avoid any reference issues
    if (profileData.contains("steps")) {
        QList<ProfileFrame> newSteps;
        QVariantList steps = profileData["steps"].toList();
        newSteps.reserve(steps.size());
        for (const QVariant& stepVar : steps) {
            QVariantMap step = stepVar.toMap();
            ProfileFrame frame;
            frame.name = step["name"].toString();
            frame.temperature = step["temperature"].toDouble();
            frame.sensor = step["sensor"].toString();
            frame.pump = step["pump"].toString();
            frame.transition = step["transition"].toString();
            frame.pressure = step["pressure"].toDouble();
            frame.flow = step["flow"].toDouble();
            frame.seconds = step["seconds"].toDouble();
            frame.volume = step["volume"].toDouble();
            frame.exitIf = step["exit_if"].toBool();
            frame.exitType = step["exit_type"].toString();
            frame.exitPressureOver = step["exit_pressure_over"].toDouble();
            frame.exitPressureUnder = step["exit_pressure_under"].toDouble();
            frame.exitFlowOver = step["exit_flow_over"].toDouble();
            frame.exitFlowUnder = step["exit_flow_under"].toDouble();
            frame.exitWeight = step["exit_weight"].toDouble();
            frame.popup = step["popup"].toString();
            frame.maxFlowOrPressure = step["max_flow_or_pressure"].toDouble();
            frame.maxFlowOrPressureRange = step["max_flow_or_pressure_range"].toDouble();
            newSteps.append(frame);
        }
        // Replace all steps atomically
        m_currentProfile.setSteps(newSteps);

        qDebug() << "uploadProfile: Updated" << newSteps.size() << "steps";
        for (int i = 0; i < newSteps.size(); i++) {
            qDebug() << "  Frame" << i << ":" << newSteps[i].name << "temp=" << newSteps[i].temperature;
        }
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }

    // Save to temp file for persistence across restarts
    QString tempPath = profilesPath() + "/_current.json";
    if (!m_currentProfile.saveToFile(tempPath)) {
        qWarning() << "Failed to save modified profile to temp file:" << tempPath;
    }

    // Upload to machine
    uploadCurrentProfile();

    emit currentProfileChanged();
}

bool MainController::saveProfile(const QString& filename) {
    bool success = false;

    // Try ProfileStorage first (SAF on Android), then fall back to local file
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, m_currentProfile.toJsonString());
        if (success) {
            qDebug() << "Saved profile to ProfileStorage:" << filename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + filename + ".json";
        success = m_currentProfile.saveToFile(path);
        if (success) {
            qDebug() << "Saved profile to local file:" << path;
        } else {
            qWarning() << "Failed to save profile to:" << path;
        }
    }

    if (success) {
        // If saving a built-in profile, auto-select it and update favorites
        if (m_settings) {
            // Check if this was originally a built-in
            bool wasBuiltIn = false;
            for (const ProfileInfo& info : m_allProfiles) {
                if (info.filename == m_baseProfileName && info.source == ProfileSource::BuiltIn) {
                    wasBuiltIn = true;
                    break;
                }
            }

            // If it was a built-in and is in favorites, the favorite now points to user copy
            if (wasBuiltIn && m_settings->isFavoriteProfile(m_baseProfileName)) {
                m_settings->updateFavoriteProfile(m_baseProfileName, filename, m_currentProfile.title());
            }
        }

        m_baseProfileName = filename;
        markProfileClean();
        refreshProfiles();

        // Re-upload profile to machine to ensure it's synced after save
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }
    }
    return success;
}

void MainController::markProfileClean() {
    if (m_profileModified) {
        m_profileModified = false;
        emit profileModifiedChanged();
        emit currentProfileChanged();  // Update the name (remove * prefix)

        // Remove temp file since we're now clean
        QString tempPath = profilesPath() + "/_current.json";
        QFile::remove(tempPath);
        qDebug() << "Profile marked clean, removed temp file";
    }
}

// === Recipe Editor Methods ===

void MainController::uploadRecipeProfile(const QVariantMap& recipeParams) {
    RecipeParams recipe = RecipeParams::fromVariantMap(recipeParams);

    // Validate recipe parameters before generating frames
    QStringList issues = recipe.validate();
    if (!issues.isEmpty()) {
        qWarning() << "RecipeParams validation issues:" << issues.join("; ");
    }
    recipe.clamp();  // Ensure values are within hardware limits

    // Update current profile's recipe params and regenerate frames
    m_currentProfile.setRecipeMode(true);
    m_currentProfile.setRecipeParams(recipe);
    m_currentProfile.regenerateFromRecipe();

    // Sync overrides so uploadCurrentProfile doesn't apply wrong delta
    // and shot plan text shows correct values (not stale overrides)
    if (m_settings) {
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
    }

    // Mark as modified
    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();
    emit targetWeightChanged();

    // Upload to machine
    uploadCurrentProfile();

    qDebug() << "Recipe profile uploaded with" << m_currentProfile.steps().size() << "frames";
}

QVariantMap MainController::getOrConvertRecipeParams() {
    if (!m_currentProfile.isRecipeMode() && isDFlowTitle(m_currentProfile.title())) {
        // Auto-convert D-Flow profiles (detected by title prefix, matching de1app behavior)
        RecipeAnalyzer::forceConvertToRecipe(m_currentProfile);
        qDebug() << "Auto-converted D-Flow profile to recipe mode:" << m_currentProfile.title();
    }
    if (!m_currentProfile.isRecipeMode() && isAFlowTitle(m_currentProfile.title())) {
        // Auto-convert A-Flow profiles (detected by title prefix, matching de1app behavior)
        RecipeAnalyzer::forceConvertToRecipe(m_currentProfile);
        // Set editor type to aflow for A-Flow title-detected profiles
        RecipeParams params = m_currentProfile.recipeParams();
        params.editorType = EditorType::AFlow;
        m_currentProfile.setRecipeParams(params);
        qDebug() << "Auto-converted A-Flow profile to recipe mode:" << m_currentProfile.title();
    }
    if (m_currentProfile.isRecipeMode()) {
        // Always ensure editorType matches title (handles profiles saved with wrong type)
        RecipeParams params = m_currentProfile.recipeParams();
        if (isAFlowTitle(m_currentProfile.title()) && params.editorType != EditorType::AFlow) {
            params.editorType = EditorType::AFlow;
            m_currentProfile.setRecipeParams(params);
            qDebug() << "Corrected editorType to aflow for:" << m_currentProfile.title();
        }
        return m_currentProfile.recipeParams().toVariantMap();
    }
    // Return default params if not in recipe mode
    return RecipeParams().toVariantMap();
}

void MainController::createNewRecipe(const QString& title) {
    createNewProfileWithEditorType(EditorType::DFlow, title);
}

void MainController::createNewAFlowRecipe(const QString& title) {
    createNewProfileWithEditorType(EditorType::AFlow, title);
}

void MainController::createNewPressureProfile(const QString& title) {
    createNewProfileWithEditorType(EditorType::Pressure, title);
}

void MainController::createNewFlowProfile(const QString& title) {
    createNewProfileWithEditorType(EditorType::Flow, title);
}

void MainController::createNewProfileWithEditorType(EditorType type, const QString& title) {
    RecipeParams recipe;
    recipe.editorType = type;
    recipe.clamp();  // Ensure values are within hardware limits

    m_currentProfile = RecipeGenerator::createProfile(recipe, title);
    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();
    emit profilesChanged();
    emit allBuiltInProfileListChanged();

    uploadCurrentProfile();
    qDebug() << "Created new" << editorTypeToString(type) << "profile:" << title;
}

void MainController::convertCurrentProfileToRecipe() {
    // Force convert the current profile to recipe mode
    // This simplifies complex profiles to fit D-Flow pattern
    RecipeAnalyzer::forceConvertToRecipe(m_currentProfile);

    // Regenerate frames from recipe params
    RecipeParams params = m_currentProfile.recipeParams();
    auto frames = RecipeGenerator::generateFrames(params);
    m_currentProfile.setSteps(frames);

    m_profileModified = true;

    // Sync overrides to match the converted profile
    if (m_settings) {
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();

    // Upload the converted profile to machine
    uploadCurrentProfile();

    qDebug() << "Converted profile to D-Flow mode:" << m_currentProfile.title();
}

void MainController::convertCurrentProfileToAdvanced() {
    // Convert from recipe mode to advanced mode
    // The frames are already generated, just disable recipe mode
    m_currentProfile.setRecipeMode(false);

    m_profileModified = true;

    emit currentProfileChanged();
    emit profileModifiedChanged();

    qDebug() << "Converted profile to Advanced mode:" << m_currentProfile.title();
}

// === D-Flow Frame Editor Methods ===

void MainController::addFrame(int afterIndex) {
    if (m_currentProfile.steps().size() >= Profile::MAX_FRAMES) {
        qWarning() << "Cannot add frame: maximum" << Profile::MAX_FRAMES << "frames reached";
        return;
    }

    // Create a new default frame
    ProfileFrame newFrame;
    newFrame.name = QString("Step %1").arg(m_currentProfile.steps().size() + 1);
    newFrame.temperature = 93.0;
    newFrame.sensor = "coffee";
    newFrame.pump = "pressure";
    newFrame.transition = "fast";
    newFrame.pressure = 9.0;
    newFrame.flow = 2.0;
    newFrame.seconds = 30.0;
    newFrame.volume = 0;
    newFrame.exitIf = false;

    bool added = false;
    if (afterIndex < 0 || afterIndex >= m_currentProfile.steps().size()) {
        // Add at end
        added = m_currentProfile.addStep(newFrame);
    } else {
        // Insert after specified index
        added = m_currentProfile.insertStep(afterIndex + 1, newFrame);
    }
    if (!added) {
        qWarning() << "Failed to add frame: maximum frame count reached (" << Profile::MAX_FRAMES << ")";
        return;
    }

    // Disable recipe mode - we're now in frame editing mode
    m_currentProfile.setRecipeMode(false);

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Added frame at index" << (afterIndex + 1) << ", total frames:" << m_currentProfile.steps().size();
}

void MainController::deleteFrame(int index) {
    if (index < 0 || index >= m_currentProfile.steps().size()) {
        qWarning() << "Cannot delete frame: invalid index" << index;
        return;
    }

    // Don't allow deleting the last frame
    if (m_currentProfile.steps().size() <= 1) {
        qWarning() << "Cannot delete the last frame";
        return;
    }

    if (!m_currentProfile.removeStep(index)) {
        qWarning() << "deleteFrame: removeStep failed for index" << index;
        return;
    }
    m_currentProfile.setRecipeMode(false);

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Deleted frame at index" << index << ", total frames:" << m_currentProfile.steps().size();
}

void MainController::moveFrameUp(int index) {
    if (index <= 0 || index >= m_currentProfile.steps().size()) {
        return;  // Can't move up if already at top or invalid
    }

    m_currentProfile.moveStep(index, index - 1);
    m_currentProfile.setRecipeMode(false);

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Moved frame from" << index << "to" << (index - 1);
}

void MainController::moveFrameDown(int index) {
    if (index < 0 || index >= m_currentProfile.steps().size() - 1) {
        return;  // Can't move down if already at bottom or invalid
    }

    m_currentProfile.moveStep(index, index + 1);
    m_currentProfile.setRecipeMode(false);

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Moved frame from" << index << "to" << (index + 1);
}

void MainController::duplicateFrame(int index) {
    if (index < 0 || index >= m_currentProfile.steps().size()) {
        qWarning() << "Cannot duplicate frame: invalid index" << index;
        return;
    }

    if (m_currentProfile.steps().size() >= Profile::MAX_FRAMES) {
        qWarning() << "Cannot duplicate frame: maximum" << Profile::MAX_FRAMES << "frames reached";
        return;
    }

    ProfileFrame copy = m_currentProfile.steps().at(index);
    copy.name = copy.name + " (copy)";
    if (!m_currentProfile.insertStep(index + 1, copy)) {
        qWarning() << "duplicateFrame: insertStep failed at index" << (index + 1);
        return;
    }
    m_currentProfile.setRecipeMode(false);

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
    qDebug() << "Duplicated frame at index" << index;
}

void MainController::setFrameProperty(int index, const QString& property, const QVariant& value) {
    if (index < 0 || index >= m_currentProfile.steps().size()) {
        qWarning() << "setFrameProperty: invalid index" << index;
        return;
    }

    ProfileFrame frame = m_currentProfile.steps().at(index);

    // Basic properties
    if (property == "name") frame.name = value.toString();
    else if (property == "temperature") frame.temperature = value.toDouble();
    else if (property == "sensor") frame.sensor = value.toString();
    else if (property == "pump") frame.pump = value.toString();
    else if (property == "transition") frame.transition = value.toString();
    else if (property == "pressure") frame.pressure = value.toDouble();
    else if (property == "flow") frame.flow = value.toDouble();
    else if (property == "seconds") frame.seconds = value.toDouble();
    else if (property == "volume") frame.volume = value.toDouble();
    // Exit conditions
    else if (property == "exitIf") frame.exitIf = value.toBool();
    else if (property == "exitType") frame.exitType = value.toString();
    else if (property == "exitPressureOver") frame.exitPressureOver = value.toDouble();
    else if (property == "exitPressureUnder") frame.exitPressureUnder = value.toDouble();
    else if (property == "exitFlowOver") frame.exitFlowOver = value.toDouble();
    else if (property == "exitFlowUnder") frame.exitFlowUnder = value.toDouble();
    else if (property == "exitWeight") frame.exitWeight = value.toDouble();
    // Limiter
    else if (property == "maxFlowOrPressure") frame.maxFlowOrPressure = value.toDouble();
    else if (property == "maxFlowOrPressureRange") frame.maxFlowOrPressureRange = value.toDouble();
    // Popup message
    else if (property == "popup") frame.popup = value.toString();
    else {
        qWarning() << "setFrameProperty: unknown property" << property;
        return;
    }

    m_currentProfile.setStepAt(index, frame);
    m_currentProfile.setRecipeMode(false);

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    uploadCurrentProfile();
}

QVariantMap MainController::getFrameAt(int index) const {
    if (index < 0 || index >= m_currentProfile.steps().size()) {
        return QVariantMap();
    }

    const ProfileFrame& frame = m_currentProfile.steps().at(index);
    QVariantMap map;

    // Basic properties
    map["name"] = frame.name;
    map["temperature"] = frame.temperature;
    map["sensor"] = frame.sensor;
    map["pump"] = frame.pump;
    map["transition"] = frame.transition;
    map["pressure"] = frame.pressure;
    map["flow"] = frame.flow;
    map["seconds"] = frame.seconds;
    map["volume"] = frame.volume;

    // Exit conditions
    map["exitIf"] = frame.exitIf;
    map["exitType"] = frame.exitType;
    map["exitPressureOver"] = frame.exitPressureOver;
    map["exitPressureUnder"] = frame.exitPressureUnder;
    map["exitFlowOver"] = frame.exitFlowOver;
    map["exitFlowUnder"] = frame.exitFlowUnder;
    map["exitWeight"] = frame.exitWeight;

    // Limiter
    map["maxFlowOrPressure"] = frame.maxFlowOrPressure;
    map["maxFlowOrPressureRange"] = frame.maxFlowOrPressureRange;

    // Popup
    map["popup"] = frame.popup;

    return map;
}

int MainController::frameCount() const {
    return m_currentProfile.steps().size();
}

void MainController::createNewProfile(const QString& title) {
    // Create a new profile with a single default frame
    m_currentProfile = Profile();
    m_currentProfile.setTitle(title);
    m_currentProfile.setAuthor("");
    m_currentProfile.setProfileNotes("");
    m_currentProfile.setBeverageType("espresso");
    m_currentProfile.setProfileType("settings_2c");
    m_currentProfile.setTargetWeight(36.0);
    m_currentProfile.setTargetVolume(36.0);
    m_currentProfile.setEspressoTemperature(93.0);
    m_currentProfile.setRecipeMode(false);

    // Add a single default extraction frame
    ProfileFrame defaultFrame;
    defaultFrame.name = "Extraction";
    defaultFrame.temperature = 93.0;
    defaultFrame.sensor = "coffee";
    defaultFrame.pump = "pressure";
    defaultFrame.transition = "fast";
    defaultFrame.pressure = 9.0;
    defaultFrame.flow = 2.0;
    defaultFrame.seconds = 60.0;
    defaultFrame.volume = 0;
    defaultFrame.exitIf = false;
    if (!m_currentProfile.addStep(defaultFrame)) {
        qWarning() << "createNewBlankProfile: failed to add default frame";
    }

    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // New profile, not in favorites
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();

    uploadCurrentProfile();
    qDebug() << "Created new blank profile:" << title;
}

bool MainController::saveProfileAs(const QString& filename, const QString& title) {
    // Remember old filename for favorite update
    QString oldFilename = m_baseProfileName;

    // Update the profile title
    m_currentProfile.setTitle(title);

    bool success = false;

    // Try ProfileStorage first (SAF on Android), then fall back to local file
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        success = m_profileStorage->writeProfile(filename, m_currentProfile.toJsonString());
        if (success) {
            qDebug() << "Saved profile as to ProfileStorage:" << filename;
        }
    }

    if (!success) {
        // Fall back to local file
        QString path = userProfilesPath() + "/" + filename + ".json";
        success = m_currentProfile.saveToFile(path);
        if (success) {
            qDebug() << "Saved profile as to local file:" << path;
        } else {
            qWarning() << "Failed to save profile to:" << path;
        }
    }

    if (success) {
        m_baseProfileName = filename;
        if (m_settings) {
            m_settings->setCurrentProfile(filename);

            // Handle favorites based on whether this is a true "Save As" or just "Save"
            if (!oldFilename.isEmpty() && oldFilename != filename) {
                // True "Save As" - keep original favorite, add new profile to favorites
                m_settings->addFavoriteProfile(title, filename);
            } else if (!oldFilename.isEmpty()) {
                // Same filename - just update the title if it changed
                m_settings->updateFavoriteProfile(oldFilename, filename, title);
            } else {
                // New profile (no old filename) - add to favorites
                m_settings->addFavoriteProfile(title, filename);
            }
        }
        markProfileClean();
        refreshProfiles();

        // Re-upload profile to machine to ensure it's synced after save
        // This catches edge cases where the previous upload may not have completed
        if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
            uploadCurrentProfile();
        }

        emit currentProfileChanged();
    }
    return success;
}

QString MainController::titleToFilename(const QString& title) const {
    // Replace accented characters
    QString result = title;
    result.replace(QChar(0xE9), 'e');  // é
    result.replace(QChar(0xE8), 'e');  // è
    result.replace(QChar(0xEA), 'e');  // ê
    result.replace(QChar(0xEB), 'e');  // ë
    result.replace(QChar(0xE1), 'a');  // á
    result.replace(QChar(0xE0), 'a');  // à
    result.replace(QChar(0xE2), 'a');  // â
    result.replace(QChar(0xE4), 'a');  // ä
    result.replace(QChar(0xED), 'i');  // í
    result.replace(QChar(0xEC), 'i');  // ì
    result.replace(QChar(0xEE), 'i');  // î
    result.replace(QChar(0xEF), 'i');  // ï
    result.replace(QChar(0xF3), 'o');  // ó
    result.replace(QChar(0xF2), 'o');  // ò
    result.replace(QChar(0xF4), 'o');  // ô
    result.replace(QChar(0xF6), 'o');  // ö
    result.replace(QChar(0xFA), 'u');  // ú
    result.replace(QChar(0xF9), 'u');  // ù
    result.replace(QChar(0xFB), 'u');  // û
    result.replace(QChar(0xFC), 'u');  // ü
    result.replace(QChar(0xF1), 'n');  // ñ
    result.replace(QChar(0xE7), 'c');  // ç

    // Replace non-alphanumeric with underscore
    QString sanitized;
    for (const QChar& c : result) {
        if (c.isLetterOrNumber()) {
            sanitized += c.toLower();
        } else {
            sanitized += '_';
        }
    }

    // Collapse multiple underscores and trim
    while (sanitized.contains("__")) {
        sanitized.replace("__", "_");
    }
    while (sanitized.startsWith('_')) sanitized.remove(0, 1);
    while (sanitized.endsWith('_')) sanitized.chop(1);

    return sanitized;
}

QString MainController::findProfileByTitle(const QString& title) const {
    for (const ProfileInfo& info : m_allProfiles) {
        if (info.title == title) {
            return info.filename;
        }
    }
    return QString();
}

bool MainController::profileExists(const QString& filename) const {
    QString path = profilesPath() + "/" + filename + ".json";
    return QFile::exists(path);
}

void MainController::sendMachineSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Determine steam temperature to send:
    // - If steam is disabled: send 0
    // - If keepSteamHeaterOn is false: send 0 (user doesn't want heater on)
    // - Otherwise: send configured temperature
    double steamTemp;
    QString reason;
    if (m_settings->steamDisabled()) {
        steamTemp = 0.0;
        reason = "steamDisabled=true";
    } else if (!m_settings->keepSteamHeaterOn()) {
        steamTemp = 0.0;
        reason = "keepSteamHeaterOn=false";
    } else {
        steamTemp = m_settings->steamTemperature();
        reason = "keepSteamHeaterOn=true";
    }

    QString phase = m_machineState ? QString::number(static_cast<int>(m_machineState->phase())) : "null";
    qDebug() << "sendMachineSettings: sending steam" << steamTemp << "°C (reason:" << reason
             << ", phase:" << phase << ", configuredTemp:" << m_settings->steamTemperature() << ")";

    double groupTemp = getGroupTemperature();
    qDebug() << "sendMachineSettings: sending groupTemp" << groupTemp << "°C";

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
}

void MainController::applyFlushSettings() {
    sendMachineSettings();
}

void MainController::applyAllSettings() {
    // 1. Upload current profile (espresso)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
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
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    double multiplier = m_settings->effectiveFlowCalibration(m_baseProfileName);
    m_device->setFlowCalibrationMultiplier(multiplier);
}

void MainController::computeAutoFlowCalibration() {
    if (!m_settings || !m_settings->autoFlowCalibration() || !m_shotDataModel)
        return;

    if (m_baseProfileName.isEmpty()) {
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
    constexpr double kMinWindowDuration = 5.0;       // seconds
    constexpr int    kMinWindowSamples = 5;
    constexpr double kWaterDensity93C = 0.963;       // g/ml - density correction for water at ~93°C
    constexpr double kCalibrationMin = 0.5;          // sanity lower bound
    constexpr double kCalibrationMax = 1.8;          // sanity upper bound
    constexpr double kChangeThreshold = 0.02;        // 2% relative change required to update

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

    for (int i = 1; i < pressureData.size(); ++i) {
        double dt = pressureData[i].x() - pressureData[i - 1].x();
        if (dt <= 0) continue;
        double dpdt = qAbs(pressureData[i].y() - pressureData[i - 1].y()) / dt;
        double pressure = pressureData[i].y();
        double t = pressureData[i].x();

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
                 << "(duration:" << windowDuration << "samples:" << bestCount << ")";
        return;
    }

    double meanMachineFlow = bestSumMF / bestCount;
    double meanWeightFlow = bestSumWF / bestCount;

    // Defensive: should be impossible given per-sample kMinWeightFlow threshold
    if (meanWeightFlow < kMinMachineFlow) {
        qWarning() << "Auto flow cal: meanWeightFlow unexpectedly low ("
                   << meanWeightFlow << ") after qualifying window";
        return;
    }

    // Density correction: machine measures volume (ml/s), scale measures mass (g/s).
    // At ~93°C, water density is ~0.963 g/ml, so 1 ml/s reads as ~0.963 g/s on the scale.
    // Multiplying the raw ratio by 0.963 compensates for this.
    double computed = (meanMachineFlow / meanWeightFlow) * kWaterDensity93C;

    if (!std::isfinite(computed)) {
        qWarning() << "Auto flow cal: computed non-finite value" << computed
                   << "(meanMachineFlow:" << meanMachineFlow
                   << "meanWeightFlow:" << meanWeightFlow << ")";
        return;
    }

    computed = qBound(kCalibrationMin, computed, kCalibrationMax);

    double currentEffective = m_settings->effectiveFlowCalibration(m_baseProfileName);

    // Only update if relative change > 2% (per-profile values are clamped to [0.5, 1.8];
    // global fallback can be as low as 0.35)
    if (currentEffective > 0.01 && qAbs(computed - currentEffective) / currentEffective < kChangeThreshold) {
        qDebug() << "Auto flow cal: computed" << computed << "≈ current" << currentEffective << "(< 2% change, skipping)";
        return;
    }

    double oldValue = currentEffective;
    m_settings->setProfileFlowCalibration(m_baseProfileName, computed);
    applyFlowCalibration();

    qDebug() << "Auto flow cal: updated" << m_baseProfileName
             << "from" << oldValue << "to" << computed
             << "(window:" << windowDuration << "s," << bestCount << "samples)";

    emit flowCalibrationAutoUpdated(m_currentProfile.title(), oldValue, computed);
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
        double overrideTemp = m_settings->temperatureOverride();
        qDebug() << "getGroupTemperature: using override" << overrideTemp << "°C"
                 << "(profile base:" << m_currentProfile.espressoTemperature() << "°C)";
        return overrideTemp;
    }
    qDebug() << "getGroupTemperature: no override, using profile"
             << m_currentProfile.espressoTemperature() << "°C";
    return m_currentProfile.espressoTemperature();
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
    if (m_bleManager && !m_bleManager->isDisabled() && m_bleManager->hasSavedScale()) {
        ScaleDevice* physicalScale = m_bleManager->scaleDevice();
        if (!physicalScale || !physicalScale->isConnected()) {
            // Check if the profile actually needs a scale
            bool profileNeedsScale = (m_currentProfile.stopAtType() == Profile::StopAtType::Weight);
            if (!profileNeedsScale) {
                // Also check per-frame exit weights
                for (const auto& step : m_currentProfile.steps()) {
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

    // Clear the graph when entering espresso preheating (new cycle from idle)
    m_shotStartTime = 0;
    m_lastShotTime = 0;
    m_extractionStarted = false;
    m_lastFrameNumber = -1;
    m_frameWeightSkipSent = -1;
    m_frameStartTime = 0;
    m_lastPressure = 0;
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

    // Start timing controller and tare via it
    if (m_timingController) {
        m_timingController->setTargetWeight(targetWeight());
        m_timingController->setCurrentProfile(&m_currentProfile);
        m_timingController->startShot();
        m_timingController->tare();
    } else {
        qWarning() << "No timing controller!";
    }

    // Clear any pending BLE commands to prevent stale profile uploads
    if (m_device) {
        m_device->clearCommandQueue();
    }

    // Start debug logging for this shot
    if (m_shotDebugLogger) {
        m_shotDebugLogger->startCapture();
        m_shotDebugLogger->logInfo(QString("Profile: %1").arg(m_currentProfile.title()));
    }

    // Clear shot notes if setting is enabled
    if (m_settings && m_settings->visualizerClearNotesOnStart()) {
        m_settings->setDyeShotNotes("");
    }
}

void MainController::onShotEnded() {
    // Capture brew overrides before clearing temperature (used later when saving shot)
    // These ALWAYS have values - either user override or profile default
    double shotTemperatureOverride = 0.0;
    double shotYieldOverride = 0.0;

    if (m_settings) {
        // Temperature: user override OR profile's espresso temperature
        if (m_settings->hasTemperatureOverride()) {
            shotTemperatureOverride = m_settings->temperatureOverride();
        } else {
            shotTemperatureOverride = m_currentProfile.espressoTemperature();
        }

        // Yield: user override OR profile's target weight (0 for volume-based profiles)
        if (m_settings->hasBrewYieldOverride()) {
            shotYieldOverride = m_settings->brewYieldOverride();
        } else if (m_currentProfile.targetWeight() > 0) {
            shotYieldOverride = m_currentProfile.targetWeight();
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

    double duration = m_shotDataModel->rawTime();  // Use rawTime, not maxTime (which is for graph axis)

    double doseWeight = m_settings->dyeBeanWeight();

    // Get final weight from shot data (cumulative weight, not flow rate)
    // In volume mode, estimate weight from ml: ml - 5 - dose*0.5
    // (5g waste tray loss + 50% of dose retained in wet puck)
    double finalWeight = 0;
    if (m_machineState && m_machineState->stopAtType() == MachineState::StopAtType::Volume) {
        double cumulativeVolume = m_machineState->cumulativeVolume();
        double puckRetention = doseWeight > 0 ? doseWeight * 0.5 : 9.0;  // fallback 9g if no dose
        finalWeight = cumulativeVolume - 5.0 - puckRetention;
        if (finalWeight < 0) finalWeight = 0;
        qDebug() << "Volume mode: estimated weight from" << cumulativeVolume << "ml ->" << finalWeight << "g";
    } else {
        const auto& cumulativeWeight = m_shotDataModel->cumulativeWeightData();
        if (!cumulativeWeight.isEmpty()) {
            finalWeight = cumulativeWeight.last().y();
        }
    }

    // Stop debug logging and get the captured log
    QString debugLog;
    if (m_shotDebugLogger) {
        m_shotDebugLogger->stopCapture();
        debugLog = m_shotDebugLogger->getCapturedLog();
    }

    // Build metadata for history
    ShotMetadata metadata;
    metadata.beanBrand = m_settings->dyeBeanBrand();
    metadata.beanType = m_settings->dyeBeanType();
    metadata.roastDate = m_settings->dyeRoastDate();
    metadata.roastLevel = m_settings->dyeRoastLevel();
    metadata.grinderModel = m_settings->dyeGrinderModel();
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

    // Smooth weight flow rate before saving (centered moving average over 7 points ≈ 1.4s at 5Hz).
    // The raw LSLR data from recording has staircase artifacts from 0.1g scale quantization;
    // this post-processing matches de1app's smoothing level for storage and visualizer export.
    m_shotDataModel->smoothWeightFlowRate();

    // Auto flow calibration: compute per-profile multiplier from this shot's data
    computeAutoFlowCalibration();

    // Always save shot to local history
    qDebug() << "[metadata] Saving shot - shotHistory:" << (m_shotHistory ? "exists" : "null")
             << "isReady:" << (m_shotHistory ? m_shotHistory->isReady() : false);
    if (m_shotHistory && m_shotHistory->isReady()) {
        qint64 shotId = m_shotHistory->saveShot(
            m_shotDataModel, &m_currentProfile,
            duration, finalWeight, doseWeight,
            metadata, debugLog,
            shotTemperatureOverride, shotYieldOverride);

        if (shotId > 0) {
            qDebug() << "[metadata] Shot saved to history with ID:" << shotId;

            // Store shot ID for post-shot review page (so it can edit the saved shot)
            m_lastSavedShotId = shotId;
            emit lastSavedShotIdChanged();

            // Set shot date/time for display on metadata page
            QString shotDateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
            m_settings->setDyeShotDateTime(shotDateTime);
            qDebug() << "[metadata] Set dyeShotDateTime to:" << shotDateTime;

            // Update the drink weight with actual final weight from this shot
            m_settings->setDyeDrinkWeight(finalWeight);
            qDebug() << "[metadata] Set dyeDrinkWeight to:" << finalWeight;

            // Reset shot-specific metadata (enjoyment and notes) for the next shot
            // Bean/grinder info persists (sticky), but rating resets to default
            m_settings->setDyeEspressoEnjoyment(m_settings->defaultShotRating());
            m_settings->setDyeShotNotes("");
            qDebug() << "[metadata] Reset enjoyment to" << m_settings->defaultShotRating() << "and notes for next shot";

            // Force QSettings to sync to disk immediately
            m_settings->sync();
        } else {
            qWarning() << "[metadata] Failed to save shot to history (returned" << shotId << ") - metadata preserved for next attempt";
            m_lastSavedShotId = 0;
            emit lastSavedShotIdChanged();
        }
    } else {
        qDebug() << "[metadata] WARNING: Could not save shot - history not ready!";
    }

    // Report shot to decenza.coffee shot map
    if (m_shotReporter && m_shotReporter->isEnabled()) {
        m_shotReporter->reportShot(m_currentProfile.title(), "Decent DE1");
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

    // Check if we should show metadata page after shot (regardless of auto-upload)
    bool showPostShot = m_settings->visualizerShowAfterShot();

    // Auto-upload if enabled (do this first, before showing metadata page)
    if (m_settings->visualizerAutoUpload() && m_visualizer) {
        qDebug() << "  -> Auto-uploading to visualizer";
        m_visualizer->uploadShot(m_shotDataModel, &m_currentProfile, duration, finalWeight, doseWeight, metadata);
    }

    // Show metadata page if enabled (user can edit and re-upload if desired)
    if (showPostShot) {
        // Store pending shot data for later upload (user can re-upload with updated metadata)
        m_hasPendingShot = true;
        m_pendingShotDuration = duration;
        m_pendingShotFinalWeight = finalWeight;
        m_pendingShotDoseWeight = doseWeight;

        qDebug() << "  -> Showing metadata page";
        emit shotEndedShowMetadata();
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
    metadata.grinderModel = m_settings->dyeGrinderModel();
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
             << "Profile:" << m_currentProfile.title()
             << "Duration:" << m_pendingShotDuration << "s"
             << "Bean:" << metadata.beanBrand << metadata.beanType;

    m_visualizer->uploadShot(m_shotDataModel, &m_currentProfile,
                             m_pendingShotDuration, m_pendingShotFinalWeight,
                             m_pendingShotDoseWeight, metadata);

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

    // Save simulated shot to history (like a real shot)
    if (m_shotHistory && m_shotHistory->isReady() && m_settings) {
        ShotMetadata metadata;
        metadata.beanBrand = m_settings->dyeBeanBrand();
        metadata.beanType = m_settings->dyeBeanType();
        metadata.roastDate = m_settings->dyeRoastDate();
        metadata.roastLevel = m_settings->dyeRoastLevel();
        metadata.grinderModel = m_settings->dyeGrinderModel();
        metadata.grinderSetting = m_settings->dyeGrinderSetting();
        metadata.beanWeight = m_pendingShotDoseWeight;
        metadata.drinkWeight = m_settings->dyeDrinkWeight();
        metadata.drinkTds = m_settings->dyeDrinkTds();
        metadata.drinkEy = m_settings->dyeDrinkEy();
        metadata.espressoEnjoyment = m_settings->dyeEspressoEnjoyment();
        metadata.espressoNotes = m_settings->dyeShotNotes();
        metadata.barista = m_settings->dyeBarista();

        // Use current profile's temperature and target weight as overrides
        double temperatureOverride = m_currentProfile.espressoTemperature();
        double yieldOverride = m_currentProfile.targetWeight();

        qint64 shotId = m_shotHistory->saveShot(
            m_shotDataModel, &m_currentProfile,
            totalDuration, m_pendingShotFinalWeight, m_pendingShotDoseWeight,
            metadata, "[Simulated shot]",
            temperatureOverride, yieldOverride);

        if (shotId > 0) {
            qDebug() << "DEV: Simulated shot saved to history with ID:" << shotId;
            m_lastSavedShotId = shotId;
            emit lastSavedShotIdChanged();

            // Update drink weight
            m_settings->setDyeDrinkWeight(m_pendingShotFinalWeight);

            // Reset shot-specific metadata for next shot
            m_settings->setDyeEspressoEnjoyment(m_settings->defaultShotRating());
            m_settings->setDyeShotNotes("");
            m_settings->sync();
        } else {
            qDebug() << "DEV: WARNING: Failed to save simulated shot to history";
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

            // Shadow: feed FlowScale in parallel when a physical scale is active.
            // When FlowScale IS the active scale, MachineState already feeds it,
            // so only feed here when using a physical scale (to avoid double-counting).
            // Only runs when useFlowScale is enabled (virtual scale preference).
            if (m_flowScale && m_settings && m_settings->useFlowScale() &&
                m_bleManager && m_bleManager->scaleDevice() &&
                m_bleManager->scaleDevice()->isConnected()) {
                m_flowScale->addFlowSample(sample.groupFlow, deltaTime);
            }
        }
    }
    m_lastSampleTime = sample.timer;

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
        const auto& steps = m_currentProfile.steps();
        if (fi >= 0 && fi < steps.size()) {
            isFlowMode = steps[fi].isFlowControl();
            if (isFlowMode) {
                pressureGoal = 0;  // Flow mode - hide pressure goal
            } else {
                flowGoal = 0;      // Pressure mode - hide flow goal
            }
        }

    }

    // Detect frame changes and add markers with frame names from profile
    // Only track during actual extraction phases (not preheating - frame numbers are unreliable then)
    if (isExtracting && sample.frameNumber >= 0 && sample.frameNumber != m_lastFrameNumber) {
        QString frameName;
        int frameIndex = sample.frameNumber;

        // Look up frame name from current profile
        const auto& steps = m_currentProfile.steps();
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

        // Accessibility: notify of frame change for tick sound
        emit frameChanged(frameIndex, frameName);
    }

    // Forward to timing controller for unified timing
    if (m_timingController) {
        m_timingController->onShotSample(sample, pressureGoal, flowGoal, sample.setTempGoal,
                                          sample.frameNumber, isFlowMode);
        // Use timing controller's time for graph data (ensures weight and other curves align)
        time = m_timingController->shotTime();
    }

    // Add sample data to graph
    m_shotDataModel->addSample(time, sample.groupPressure,
                               sample.groupFlow, sample.headTemp,
                               sample.mixTemp,
                               pressureGoal, flowGoal, sample.setTempGoal,
                               sample.frameNumber, isFlowMode);
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
    // Only logs when useFlowScale is enabled to avoid confusing 0.0g entries.
    if (m_flowScale && m_extractionStarted && m_settings && m_settings->useFlowScale()) {
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

void MainController::loadDefaultProfile() {
    m_currentProfile = Profile();
    m_currentProfile.setTitle("Default");
    m_currentProfile.setTargetWeight(36.0);

    // Create a simple default profile
    ProfileFrame preinfusion;
    preinfusion.name = "Preinfusion";
    preinfusion.pump = "pressure";
    preinfusion.pressure = 4.0;
    preinfusion.temperature = 93.0;
    preinfusion.seconds = 10.0;
    preinfusion.exitIf = true;
    preinfusion.exitType = "pressure_over";
    preinfusion.exitPressureOver = 3.0;

    ProfileFrame extraction;
    extraction.name = "Extraction";
    extraction.pump = "pressure";
    extraction.pressure = 9.0;
    extraction.temperature = 93.0;
    extraction.seconds = 30.0;

    if (!m_currentProfile.addStep(preinfusion)) {
        qWarning() << "loadDefaultProfile: failed to add preinfusion frame";
    }
    if (!m_currentProfile.addStep(extraction)) {
        qWarning() << "loadDefaultProfile: failed to add extraction frame";
    }
    m_currentProfile.setPreinfuseFrameCount(1);

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // Default profile, not in favorites
        m_settings->setBrewYieldOverride(m_currentProfile.targetWeight());
        m_settings->setTemperatureOverride(m_currentProfile.espressoTemperature());
    }
}

QString MainController::profilesPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}

QString MainController::userProfilesPath() const {
    QString path = profilesPath() + "/user";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString MainController::downloadedProfilesPath() const {
    QString path = profilesPath() + "/downloaded";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

void MainController::migrateProfileFolders() {
    QString basePath = profilesPath();
    QString userPath = basePath + "/user";
    QString downloadedPath = basePath + "/downloaded";

    // Check if migration already done (user folder exists and has content, or we've done it before)
    QDir userDir(userPath);
    QDir downloadedDir(downloadedPath);

    // If user folder already exists, migration was already done
    if (userDir.exists()) {
        // Just ensure downloaded folder exists too
        if (!downloadedDir.exists()) {
            downloadedDir.mkpath(".");
        }
        return;
    }

    qDebug() << "Migrating profile folders...";

    // Create both folders
    userDir.mkpath(".");
    downloadedDir.mkpath(".");

    // Move all existing .json files (except _current.json) from profiles/ to profiles/user/
    QDir baseDir(basePath);
    QStringList filters;
    filters << "*.json";
    QStringList files = baseDir.entryList(filters, QDir::Files);

    for (const QString& file : files) {
        if (file == "_current.json") {
            continue;  // Skip the temp file
        }

        QString srcPath = basePath + "/" + file;
        QString dstPath = userPath + "/" + file;

        if (QFile::rename(srcPath, dstPath)) {
            qDebug() << "Migrated profile:" << file;
        } else {
            qWarning() << "Failed to migrate profile:" << file;
        }
    }

    qDebug() << "Profile folder migration complete";
}
