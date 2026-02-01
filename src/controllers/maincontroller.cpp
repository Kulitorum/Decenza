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
    }

    // Set MachineState on ShotServer for home automation API
    m_shotServer->setMachineState(m_machineState);

    // Emit remoteSleepRequested when sleep command received via REST API
    connect(m_shotServer, &ShotServer::sleepRequested, this, &MainController::remoteSleepRequested);

    // Create MQTT client for home automation
    m_mqttClient = new MqttClient(m_device, m_machineState, m_settings, this);

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
        }
    });

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
        }
        if (m_machineState) {
            m_machineState->setTargetWeight(m_currentProfile.targetWeight());
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
}

QString MainController::currentProfileName() const {
    if (m_profileModified) {
        return "*" + m_currentProfile.title();
    }
    return m_currentProfile.title();
}

double MainController::targetWeight() const {
    // Return calculated target when brew-by-ratio is active
    if (m_brewByRatioActive) {
        return m_brewByRatioDose * m_brewByRatio;
    }
    return m_currentProfile.targetWeight();
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
        // Store dose and grind in DYE fields (source of truth)
        m_settings->setDyeBeanWeight(dose);
        m_settings->setDyeGrinderSetting(grind);

        // Store yield as override (for next shot)
        m_settings->setBrewYieldOverride(yield);

        // Store temperature override if different from profile
        if (qAbs(temperature - m_currentProfile.espressoTemperature()) > 0.1) {
            m_settings->setTemperatureOverride(temperature);
        }
    }

    double ratio = dose > 0 ? yield / dose : 2.0;
    m_brewByRatioActive = true;
    m_brewByRatioDose = dose;
    m_brewByRatio = ratio;

    // Calculate target and set it in MachineState for stop-at-weight logic
    double calculatedTarget = dose * ratio;
    if (m_machineState) {
        m_machineState->setTargetWeight(calculatedTarget);
    }

    qDebug() << "Brew overrides activated: dose=" << dose << "g, ratio=1:" << ratio
             << "-> target=" << calculatedTarget << "g";

    // Re-upload profile with temperature override applied to machine frames
    if (m_settings && m_settings->hasTemperatureOverride()) {
        uploadCurrentProfile();
    }

    emit brewByRatioChanged();
    emit targetWeightChanged();
}

void MainController::clearBrewByRatio() {
    if (!m_brewByRatioActive) {
        return;
    }

    m_brewByRatioActive = false;

    // Restore profile's target weight to MachineState
    if (m_machineState) {
        m_machineState->setTargetWeight(m_currentProfile.targetWeight());
    }

    qDebug() << "Brew-by-ratio cleared, restored target=" << m_currentProfile.targetWeight() << "g";

    emit brewByRatioChanged();
    emit targetWeightChanged();
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

    for (const ProfileInfo& info : m_allProfiles) {
        bool include = false;

        switch (info.source) {
        case ProfileSource::BuiltIn:
            // Only include if selected
            include = selectedBuiltIns.contains(info.filename);
            break;
        case ProfileSource::Downloaded:
        case ProfileSource::UserCreated:
            // Always include user and downloaded profiles
            include = true;
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

    // Get selected built-in profile names from settings
    QStringList selectedBuiltIns = m_settings ? m_settings->selectedBuiltInProfiles() : QStringList();

    for (const ProfileInfo& info : m_allProfiles) {
        if (info.source == ProfileSource::BuiltIn) {
            QVariantMap profile;
            profile["name"] = info.filename;
            profile["title"] = info.title;
            profile["beverageType"] = info.beverageType;
            profile["source"] = static_cast<int>(info.source);
            profile["isSelected"] = selectedBuiltIns.contains(info.filename);
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

    // Cannot delete built-in profiles
    if (source == ProfileSource::BuiltIn) {
        qWarning() << "Cannot delete built-in profile:" << filename;
        return false;
    }

    bool deleted = false;

    // Try ProfileStorage first (SAF on Android)
    if (m_profileStorage && m_profileStorage->isConfigured()) {
        if (m_profileStorage->deleteProfile(filename)) {
            qDebug() << "Deleted profile from ProfileStorage:" << filename;
            deleted = true;
        }
    }

    // Also try deleting from local folders (fallback or legacy)
    if (!deleted) {
        QString path;
        if (source == ProfileSource::Downloaded) {
            path = downloadedProfilesPath() + "/" + filename + ".json";
        } else if (source == ProfileSource::UserCreated) {
            path = userProfilesPath() + "/" + filename + ".json";
        }

        if (QFile::remove(path)) {
            qDebug() << "Deleted profile from local storage:" << path;
            deleted = true;
        }
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

    // Track the base profile name (filename without extension)
    m_baseProfileName = resolvedName;
    bool wasModified = m_profileModified;
    m_profileModified = false;

    if (m_settings) {
        m_settings->setCurrentProfile(resolvedName);
        // Sync selectedFavoriteProfile with the loaded profile
        // This ensures the UI shows the correct pill as selected, or -1 if not a favorite
        int favoriteIndex = m_settings->findFavoriteIndexByFilename(resolvedName);
        qDebug() << "loadProfile: resolvedName=" << resolvedName << "favoriteIndex=" << favoriteIndex;
        m_settings->setSelectedFavoriteProfile(favoriteIndex);
    }

    // Clear brew-by-ratio, yield and temperature overrides when loading a new profile
    // Dose and grind are tied to the bean, not the profile, so they persist
    m_brewByRatioActive = false;
    if (m_settings) {
        m_settings->setBrewYieldOverride(0);
        m_settings->clearTemperatureOverride();

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
        m_settings->setSelectedBeanPreset(beanPresetIndex);

        // Apply brew overrides from history (after loadProfile cleared them)
        if (shotRecord.hasTemperatureOverride) {
            m_settings->setTemperatureOverride(shotRecord.temperatureOverride);
        } else {
            m_settings->clearTemperatureOverride();
        }

        if (shotRecord.hasYieldOverride) {
            m_settings->setBrewYieldOverride(shotRecord.yieldOverride);
        } else if (m_settings->hasBrewYieldOverride()) {
            m_settings->clearAllBrewOverrides();
        }

        qDebug() << "Loaded shot metadata - brand:" << shotRecord.summary.beanBrand
                 << "type:" << shotRecord.summary.beanType
                 << "grinder:" << shotRecord.grinderModel << shotRecord.grinderSetting
                 << "beanPresetIndex:" << beanPresetIndex
                 << "brewOverrides - temp:" << (shotRecord.hasTemperatureOverride ? QString::number(shotRecord.temperatureOverride) : "none")
                 << "yield:" << (shotRecord.hasYieldOverride ? QString::number(shotRecord.yieldOverride) : "none");
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
        info.isRecipeMode = isRecipeMode;
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    // 2. Load profiles from ProfileStorage (SAF folder or fallback)
    if (m_profileStorage) {
        QStringList storageProfiles = m_profileStorage->listProfiles();
        for (const QString& name : storageProfiles) {
            if (m_availableProfiles.contains(name)) {
                continue;  // Skip if already loaded (e.g., built-in with same name)
            }

            QString jsonContent = m_profileStorage->readProfile(name);
            if (jsonContent.isEmpty()) {
                continue;
            }

            auto [title, beverageType, isRecipeMode] = loadProfileMetaFromJson(jsonContent);

            ProfileInfo info;
            info.filename = name;
            info.title = title.isEmpty() ? name : title;
            info.beverageType = beverageType;
            info.source = ProfileSource::UserCreated;  // All SAF profiles are user-created
            info.isRecipeMode = isRecipeMode;
            m_allProfiles.append(info);

            m_availableProfiles.append(name);
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
        info.isRecipeMode = isRecipeMode;
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
        info.isRecipeMode = isRecipeMode;
        m_allProfiles.append(info);

        m_availableProfiles.append(name);
        m_profileTitles[name] = info.title;
    }

    emit profilesChanged();
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
            double steamTemp = m_settings->steamDisabled() ? 0.0 : m_settings->steamTemperature();
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
        m_currentProfile.setEspressoTemperature(profileData["espresso_temperature"].toDouble());
    }
    if (profileData.contains("target_weight")) {
        m_currentProfile.setTargetWeight(profileData["target_weight"].toDouble());
        if (m_machineState) {
            m_machineState->setTargetWeight(m_currentProfile.targetWeight());
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
    m_currentProfile.saveToFile(tempPath);
    qDebug() << "Saved modified profile to temp file:" << tempPath;

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

    // Update current profile's recipe params and regenerate frames
    m_currentProfile.setRecipeMode(true);
    m_currentProfile.setRecipeParams(recipe);
    m_currentProfile.regenerateFromRecipe();

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

QVariantMap MainController::getCurrentRecipeParams() const {
    if (m_currentProfile.isRecipeMode()) {
        return m_currentProfile.recipeParams().toVariantMap();
    }
    // Return default params if not in recipe mode
    return RecipeParams().toVariantMap();
}

void MainController::createNewRecipe(const QString& title) {
    RecipeParams recipe;  // Default recipe params

    // Create new profile from recipe
    m_currentProfile = RecipeGenerator::createProfile(recipe, title);
    m_baseProfileName = "";  // New profile, no base filename yet
    m_profileModified = true;

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // New profile, not in favorites
    }

    emit currentProfileChanged();
    emit profileModifiedChanged();
    emit targetWeightChanged();
    emit profilesChanged();

    // Upload to machine
    uploadCurrentProfile();

    qDebug() << "Created new recipe profile:" << title;
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

void MainController::applyRecipePreset(const QString& presetName) {
    RecipeParams recipe;

    if (presetName == "classic") {
        recipe = RecipeParams::classic();
    } else if (presetName == "londinium") {
        recipe = RecipeParams::londinium();
    } else if (presetName == "turbo") {
        recipe = RecipeParams::turbo();
    } else if (presetName == "blooming") {
        recipe = RecipeParams::blooming();
    } else if (presetName == "dflowDefault") {
        recipe = RecipeParams::dflowDefault();
    } else {
        qWarning() << "Unknown recipe preset:" << presetName;
        return;
    }

    // Preserve current target weight and title
    recipe.targetWeight = m_currentProfile.targetWeight();

    // Update current profile with preset
    m_currentProfile.setRecipeMode(true);
    m_currentProfile.setRecipeParams(recipe);
    m_currentProfile.regenerateFromRecipe();

    if (!m_profileModified) {
        m_profileModified = true;
        emit profileModifiedChanged();
    }
    emit currentProfileChanged();

    // Upload to machine
    uploadCurrentProfile();

    qDebug() << "Applied recipe preset:" << presetName;
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

    if (afterIndex < 0 || afterIndex >= m_currentProfile.steps().size()) {
        // Add at end
        m_currentProfile.addStep(newFrame);
    } else {
        // Insert after specified index
        m_currentProfile.insertStep(afterIndex + 1, newFrame);
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

    m_currentProfile.removeStep(index);
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
    m_currentProfile.insertStep(index + 1, copy);
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
    m_currentProfile.addStep(defaultFrame);

    m_baseProfileName = "";
    m_profileModified = true;

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // New profile, not in favorites
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

void MainController::applySteamSettings() {
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
        // User doesn't want steam heater on when idle
        steamTemp = 0.0;
        reason = "keepSteamHeaterOn=false";
    } else {
        steamTemp = m_settings->steamTemperature();
        reason = "keepSteamHeaterOn=true";
    }

    QString phase = m_machineState ? QString::number(static_cast<int>(m_machineState->phase())) : "null";
    qDebug() << "applySteamSettings: sending" << steamTemp << "°C (reason:" << reason
             << ", phase:" << phase << ", configuredTemp:" << m_settings->steamTemperature() << ")";

    double groupTemp = getGroupTemperature();

    // Send shot settings (includes steam temp/timeout)
    m_device->setShotSettings(
        steamTemp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
    );

    // Send steam flow via MMR
    m_device->writeMMR(0x803828, m_settings->steamFlow());

    // Reset flush timeout MMR to high value (255 seconds) to prevent
    // stale flush duration from affecting steam mode
    m_device->writeMMR(0x803848, 2550);
}

void MainController::applyHotWaterSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Same steam temp logic as applySteamSettings()
    double steamTemp;
    if (m_settings->steamDisabled()) {
        steamTemp = 0.0;
    } else if (!m_settings->keepSteamHeaterOn()) {
        steamTemp = 0.0;
    } else {
        steamTemp = m_settings->steamTemperature();
    }

    qDebug() << "applyHotWaterSettings: steam temp=" << steamTemp << "°C";

    double groupTemp = getGroupTemperature();

    // Send shot settings (includes water temp/volume)
    m_device->setShotSettings(
        steamTemp,
        m_settings->steamTimeout(),
        m_settings->waterTemperature(),
        m_settings->waterVolume(),
        groupTemp
    );

    // Reset flush timeout MMR to high value (255 seconds) to prevent
    // stale flush duration from affecting hot water mode
    m_device->writeMMR(0x803848, 2550);
}

void MainController::applyFlushSettings() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Flush flow rate at MMR 0x803840, value × 10
    // Flush timeout at MMR 0x803848, value × 10
    int flowValue = static_cast<int>(m_settings->flushFlow() * 10);
    int secondsValue = static_cast<int>(m_settings->flushSeconds() * 10);

    m_device->writeMMR(0x803840, flowValue);
    m_device->writeMMR(0x803848, secondsValue);
}

void MainController::applyAllSettings() {
    // 1. Upload current profile (espresso)
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) {
        uploadCurrentProfile();
    }

    // 2. Apply steam settings
    applySteamSettings();

    // 3. Apply hot water settings
    applyHotWaterSettings();

    // 4. Apply flush settings
    applyFlushSettings();

    // 5. Apply water refill level
    applyWaterRefillLevel();
}

void MainController::applyWaterRefillLevel() {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    m_device->setWaterRefillLevel(m_settings->waterRefillPoint());
}

double MainController::getGroupTemperature() const {
    if (m_settings && m_settings->hasTemperatureOverride()) {
        return m_settings->temperatureOverride();
    }
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

void MainController::startCalibrationDispense(double flowRate, double targetWeight) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Create a simple calibration profile with a single flow-controlled frame
    Profile calibrationProfile;
    calibrationProfile.setTitle("Calibration");
    calibrationProfile.setTargetWeight(targetWeight);
    calibrationProfile.setMode(Profile::Mode::FrameBased);

    // Single frame: flow control at the target flow rate
    // Use volume limit so DE1 stops based on its own flow sensor (what we're calibrating)
    ProfileFrame frame;
    frame.name = "Calibration";
    frame.pump = "flow";           // Flow control mode
    frame.flow = flowRate;         // Target flow rate in mL/s
    frame.temperature = m_settings->waterTemperature();  // Use hot water temp
    frame.sensor = "water";        // Use mix temp sensor (not basket/coffee)
    frame.transition = "fast";     // Instant transition
    frame.seconds = 120.0;         // 2 minutes max timeout
    frame.volume = targetWeight;   // DE1 stops when its flow sensor thinks this much dispensed
    frame.pressure = 0;            // Not used in flow mode
    frame.maxFlowOrPressure = 0;   // No limiter needed

    calibrationProfile.addStep(frame);
    calibrationProfile.setPreinfuseFrameCount(0);  // No preinfusion

    // Disable stop-at-weight during calibration - let DE1's volume limit stop instead
    // Set a very high target so app's stop-at-weight doesn't interfere
    if (m_machineState) {
        m_machineState->setTargetWeight(999);
    }

    // Enter calibration mode (prevents navigation to espresso page)
    m_calibrationMode = true;
    emit calibrationModeChanged();

    // Tare the scale for the user before starting
    if (m_machineState) {
        m_machineState->tareScale();
    }

    // Upload calibration profile (user must press espresso button on DE1)
    m_device->uploadProfile(calibrationProfile);

    qDebug() << "=== CALIBRATION READY: flow" << flowRate << "mL/s, target" << targetWeight << "g - press espresso button ===";
}

void MainController::startVerificationDispense(double targetWeight) {
    if (!m_device || !m_device->isConnected() || !m_settings) return;

    // Create verification profile - uses FlowScale (with calibration factor) to stop
    Profile verificationProfile;
    verificationProfile.setTitle("Verification");
    verificationProfile.setTargetWeight(targetWeight);
    verificationProfile.setMode(Profile::Mode::FrameBased);

    // Single frame: flow control at medium rate, NO volume limit
    // FlowScale's calibrated weight will trigger stop-at-weight
    ProfileFrame frame;
    frame.name = "Verification";
    frame.pump = "flow";
    frame.flow = 6.0;  // Medium flow rate
    frame.temperature = m_settings->waterTemperature();
    frame.sensor = "water";
    frame.transition = "fast";
    frame.seconds = 120.0;  // Long timeout - FlowScale will stop it
    frame.volume = 0;       // NO volume limit - let FlowScale stop
    frame.pressure = 0;
    frame.maxFlowOrPressure = 0;

    verificationProfile.addStep(frame);
    verificationProfile.setPreinfuseFrameCount(0);

    // Enable stop-at-weight using FlowScale's calibrated weight
    if (m_machineState) {
        m_machineState->setTargetWeight(targetWeight);
    }

    // Enter calibration mode (prevents navigation)
    m_calibrationMode = true;
    emit calibrationModeChanged();

    // Tare the scale
    if (m_machineState) {
        m_machineState->tareScale();
    }

    // Upload profile
    m_device->uploadProfile(verificationProfile);

    qDebug() << "=== VERIFICATION READY: target" << targetWeight << "g using FlowScale - press espresso button ===";
}

void MainController::restoreCurrentProfile() {
    // Exit calibration mode
    m_calibrationMode = false;
    emit calibrationModeChanged();

    // Re-upload the user's actual profile after calibration
    if (m_device && m_device->isConnected()) {
        uploadCurrentProfile();

        // Also restore the target weight from the profile
        if (m_machineState) {
            m_machineState->setTargetWeight(m_currentProfile.targetWeight());
        }
    }
    qDebug() << "=== RESTORED PROFILE:" << m_currentProfile.title() << "===";
}

void MainController::onEspressoCycleStarted() {
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
    // Always clear brew-by-ratio mode when shot ends
    clearBrewByRatio();

    // Capture brew overrides before clearing (used later when saving shot)
    double shotTemperatureOverride = 0.0;
    bool shotHasTemperatureOverride = false;
    double shotYieldOverride = 0.0;
    bool shotHasYieldOverride = false;

    if (m_settings) {
        shotTemperatureOverride = m_settings->temperatureOverride();
        shotHasTemperatureOverride = m_settings->hasTemperatureOverride();
        shotYieldOverride = m_settings->brewYieldOverride();
        shotHasYieldOverride = m_settings->hasBrewYieldOverride();

        // Clear temperature override after shot
        m_settings->clearTemperatureOverride();
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

    // Always save shot to local history
    qDebug() << "[metadata] Saving shot - shotHistory:" << (m_shotHistory ? "exists" : "null")
             << "isReady:" << (m_shotHistory ? m_shotHistory->isReady() : false);
    if (m_shotHistory && m_shotHistory->isReady()) {
        qint64 shotId = m_shotHistory->saveShot(
            m_shotDataModel, &m_currentProfile,
            duration, finalWeight, doseWeight,
            metadata, debugLog,
            shotTemperatureOverride, shotHasTemperatureOverride,
            shotYieldOverride, shotHasYieldOverride);
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

        // Force QSettings to sync to disk immediately
        m_settings->sync();
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
    // Show when: (extended metadata enabled AND show after shot) OR (AI configured AND show after shot)
    bool hasAI = m_aiManager && m_aiManager->isConfigured();
    bool showPostShot = m_settings->visualizerShowAfterShot() &&
                        (m_settings->visualizerExtendedMetadata() || hasAI);

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

        // addSample(time, pressure, flow, temperature, mixTemp, pressureGoal, flowGoal, temperatureGoal, frameNumber, isFlowMode)
        // Simulation uses pressure mode (isFlowMode = false)
        m_shotDataModel->addSample(t, pressure, flow, temperature, temperature, pressureGoal, flowGoal, 92.0, frameNumber, false);
        // addWeightSample(time, weight, flowRate)
        m_shotDataModel->addWeightSample(t, weight, flow);
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
}

void MainController::clearCrashLog() {
    QString path = CrashHandler::crashLogPath();
    if (QFile::exists(path)) {
        QFile::remove(path);
        qDebug() << "MainController: Cleared crash log at" << path;
    }
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

    // Forward to timing controller which handles stop-at-weight and graph data
    if (m_timingController) {
        double flowRate = m_machineState->scaleFlowRate();
        m_timingController->onWeightSample(weight, flowRate);
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

    m_currentProfile.addStep(preinfusion);
    m_currentProfile.addStep(extraction);
    m_currentProfile.setPreinfuseFrameCount(1);

    if (m_settings) {
        m_settings->setSelectedFavoriteProfile(-1);  // Default profile, not in favorites
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
