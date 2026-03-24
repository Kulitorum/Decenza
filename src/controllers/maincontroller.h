#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
#include <QTimer>
#include "profilemanager.h"
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"
#include "../network/visualizerimporter.h"
#include "../ai/aimanager.h"
#include "../models/shotdatamodel.h"
#include "../history/shothistorystorage.h"
#include "../history/shotimporter.h"
#include "../profile/profileconverter.h"
#include "../profile/profileimporter.h"
#include "../models/shotcomparisonmodel.h"
#include "../network/shotserver.h"
#include "../network/shotreporter.h"
#include "../network/mqttclient.h"
#include "../core/updatechecker.h"
#include "../core/datamigrationclient.h"
#include "../core/databasebackupmanager.h"

class QNetworkAccessManager;
class Settings;
class DE1Device;
class MachineState;
class BLEManager;
class FlowScale;
class ProfileStorage;
class ShotDebugLogger;
class LocationProvider;
class ShotTimingController;
struct ShotSample;

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileChanged)

    Q_PROPERTY(QString baseProfileName READ baseProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(bool profileModified READ isProfileModified NOTIFY profileModifiedChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(bool brewByRatioActive READ brewByRatioActive NOTIFY targetWeightChanged)
    Q_PROPERTY(double brewByRatioDose READ brewByRatioDose NOTIFY targetWeightChanged)
    Q_PROPERTY(double brewByRatio READ brewByRatio NOTIFY targetWeightChanged)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList selectedProfiles READ selectedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allBuiltInProfiles READ allBuiltInProfiles NOTIFY allBuiltInProfileListChanged)
    Q_PROPERTY(QVariantList cleaningProfiles READ cleaningProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList downloadedProfiles READ downloadedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList userCreatedProfiles READ userCreatedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allProfilesList READ allProfilesList NOTIFY profilesChanged)
    Q_PROPERTY(VisualizerUploader* visualizer READ visualizer CONSTANT)
    Q_PROPERTY(VisualizerImporter* visualizerImporter READ visualizerImporter CONSTANT)
    Q_PROPERTY(AIManager* aiManager READ aiManager CONSTANT)
    Q_PROPERTY(ShotDataModel* shotDataModel READ shotDataModel CONSTANT)
    Q_PROPERTY(Profile* currentProfilePtr READ currentProfilePtr CONSTANT)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY frameChanged)
    Q_PROPERTY(double filteredGoalPressure READ filteredGoalPressure NOTIFY goalsChanged)
    Q_PROPERTY(double filteredGoalFlow READ filteredGoalFlow NOTIFY goalsChanged)
    Q_PROPERTY(ShotHistoryStorage* shotHistory READ shotHistory CONSTANT)
    Q_PROPERTY(ShotImporter* shotImporter READ shotImporter CONSTANT)
    Q_PROPERTY(ProfileConverter* profileConverter READ profileConverter CONSTANT)
    Q_PROPERTY(ProfileImporter* profileImporter READ profileImporter CONSTANT)
    Q_PROPERTY(ShotComparisonModel* shotComparison READ shotComparison CONSTANT)
    Q_PROPERTY(ShotServer* shotServer READ shotServer CONSTANT)
    Q_PROPERTY(MqttClient* mqttClient READ mqttClient CONSTANT)
    Q_PROPERTY(UpdateChecker* updateChecker READ updateChecker CONSTANT)
    Q_PROPERTY(ShotReporter* shotReporter READ shotReporter CONSTANT)
    Q_PROPERTY(DataMigrationClient* dataMigration READ dataMigration CONSTANT)
    Q_PROPERTY(DatabaseBackupManager* backupManager READ backupManager CONSTANT)
    Q_PROPERTY(bool isCurrentProfileRecipe READ isCurrentProfileRecipe NOTIFY currentProfileChanged)
    Q_PROPERTY(QString currentEditorType READ currentEditorType NOTIFY currentProfileChanged)
    Q_PROPERTY(qint64 lastSavedShotId READ lastSavedShotId NOTIFY lastSavedShotIdChanged)
    Q_PROPERTY(double profileTargetTemperature READ profileTargetTemperature NOTIFY currentProfileChanged)
    Q_PROPERTY(double profileTargetWeight READ profileTargetWeight NOTIFY currentProfileChanged)
    Q_PROPERTY(bool profileHasRecommendedDose READ profileHasRecommendedDose NOTIFY currentProfileChanged)
    Q_PROPERTY(double profileRecommendedDose READ profileRecommendedDose NOTIFY currentProfileChanged)
    Q_PROPERTY(bool sawSettling READ isSawSettling NOTIFY sawSettlingChanged)

public:
    explicit MainController(QNetworkAccessManager* networkManager,
                           Settings* settings, DE1Device* device,
                           MachineState* machineState, ShotDataModel* shotDataModel,
                           ProfileStorage* profileStorage = nullptr,
                           QObject* parent = nullptr);

    // ProfileManager accessor
    ProfileManager* profileManager() const { return m_profileManager; }

    // Profile state — delegated to ProfileManager
    QString currentProfileName() const { return m_profileManager->currentProfileName(); }
    QString baseProfileName() const { return m_profileManager->baseProfileName(); }
    Q_INVOKABLE QString previousProfileName() const { return m_profileManager->previousProfileName(); }
    bool isProfileModified() const { return m_profileManager->isProfileModified(); }
    bool isCurrentProfileRecipe() const { return m_profileManager->isCurrentProfileRecipe(); }
    QString currentEditorType() const { return m_profileManager->currentEditorType(); }
    static bool isDFlowTitle(const QString& title) { return ProfileManager::isDFlowTitle(title); }
    static bool isAFlowTitle(const QString& title) { return ProfileManager::isAFlowTitle(title); }

    // Target weight / brew-by-ratio — delegated to ProfileManager
    double targetWeight() const { return m_profileManager->targetWeight(); }
    void setTargetWeight(double weight) { m_profileManager->setTargetWeight(weight); }
    bool brewByRatioActive() const { return m_profileManager->brewByRatioActive(); }
    double brewByRatioDose() const { return m_profileManager->brewByRatioDose(); }
    double brewByRatio() const { return m_profileManager->brewByRatio(); }
    Q_INVOKABLE void activateBrewWithOverrides(double dose, double yield, double temperature, const QString& grind) { m_profileManager->activateBrewWithOverrides(dose, yield, temperature, grind); }
    Q_INVOKABLE void clearBrewOverrides() { m_profileManager->clearBrewOverrides(); }

    // Profile catalog — delegated to ProfileManager
    QVariantList availableProfiles() const { return m_profileManager->availableProfiles(); }
    QVariantList selectedProfiles() const { return m_profileManager->selectedProfiles(); }
    QVariantList allBuiltInProfiles() const { return m_profileManager->allBuiltInProfiles(); }
    QVariantList cleaningProfiles() const { return m_profileManager->cleaningProfiles(); }
    QVariantList downloadedProfiles() const { return m_profileManager->downloadedProfiles(); }
    QVariantList userCreatedProfiles() const { return m_profileManager->userCreatedProfiles(); }
    QVariantList allProfilesList() const { return m_profileManager->allProfilesList(); }

    // Profile accessors — delegated to ProfileManager
    const Profile& currentProfile() const { return m_profileManager->currentProfile(); }
    Profile currentProfileObject() const { return m_profileManager->currentProfileObject(); }
    Profile* currentProfilePtr() { return m_profileManager->currentProfilePtr(); }
    double profileTargetTemperature() const { return m_profileManager->profileTargetTemperature(); }
    double profileTargetWeight() const { return m_profileManager->profileTargetWeight(); }
    bool profileHasRecommendedDose() const { return m_profileManager->profileHasRecommendedDose(); }
    double profileRecommendedDose() const { return m_profileManager->profileRecommendedDose(); }

    // Non-profile accessors (remain on MainController)
    double filteredGoalPressure() const { return m_filteredGoalPressure; }
    double filteredGoalFlow() const { return m_filteredGoalFlow; }
    VisualizerUploader* visualizer() const { return m_visualizer; }
    VisualizerImporter* visualizerImporter() const { return m_visualizerImporter; }
    ProfileStorage* profileStorage() const { return m_profileStorage; }
    AIManager* aiManager() const { return m_aiManager; }
    void setAiManager(AIManager* aiManager) {
        m_aiManager = aiManager;
        if (m_aiManager && m_shotHistory)
            m_aiManager->setShotHistoryStorage(m_shotHistory);
        if (m_aiManager && m_dataMigration)
            m_dataMigration->setAIManager(m_aiManager);
    }
    void setBLEManager(BLEManager* bleManager) { m_bleManager = bleManager; }
    void setFlowScale(FlowScale* flowScale) { m_flowScale = flowScale; }
    void setTimingController(ShotTimingController* controller) { m_timingController = controller; }
    void setBackupManager(DatabaseBackupManager* backupManager) { m_backupManager = backupManager; }
    ShotDataModel* shotDataModel() const { return m_shotDataModel; }
    bool isSawSettling() const;
    QString currentFrameName() const { return m_currentFrameName; }
    ShotHistoryStorage* shotHistory() const { return m_shotHistory; }
    ShotImporter* shotImporter() const { return m_shotImporter; }
    ProfileConverter* profileConverter() const { return m_profileConverter; }
    ProfileImporter* profileImporter() const { return m_profileImporter; }
    ShotComparisonModel* shotComparison() const { return m_shotComparison; }
    ShotServer* shotServer() const { return m_shotServer; }
    MqttClient* mqttClient() const { return m_mqttClient; }
    UpdateChecker* updateChecker() const { return m_updateChecker; }
    ShotReporter* shotReporter() const { return m_shotReporter; }
    DataMigrationClient* dataMigration() const { return m_dataMigration; }
    DatabaseBackupManager* backupManager() const { return m_backupManager; }
    LocationProvider* locationProvider() const { return m_locationProvider; }
    qint64 lastSavedShotId() const { return m_lastSavedShotId; }

    // For simulator integration
    void handleShotSample(const ShotSample& sample) { onShotSampleReceived(sample); }

    // Profile CRUD — forwarded to ProfileManager for QML compatibility
    Q_INVOKABLE QVariantMap getCurrentProfile() const { return m_profileManager->getCurrentProfile(); }
    Q_INVOKABLE void markProfileClean() { m_profileManager->markProfileClean(); }
    Q_INVOKABLE QString titleToFilename(const QString& title) const { return m_profileManager->titleToFilename(title); }
    Q_INVOKABLE QString findProfileByTitle(const QString& title) const { return m_profileManager->findProfileByTitle(title); }
    Q_INVOKABLE bool profileExists(const QString& filename) const { return m_profileManager->profileExists(filename); }
    Q_INVOKABLE bool deleteProfile(const QString& filename) { return m_profileManager->deleteProfile(filename); }
    Q_INVOKABLE QVariantMap getProfileByFilename(const QString& filename) const { return m_profileManager->getProfileByFilename(filename); }
    Q_INVOKABLE void loadShotWithMetadata(qint64 shotId);  // Stays on MC (uses shot history)

    // Clipboard
    Q_INVOKABLE void copyToClipboard(const QString& text);
    Q_INVOKABLE QString pasteFromClipboard() const;

    // Recipe Editor methods — forwarded to ProfileManager
    Q_INVOKABLE void uploadRecipeProfile(const QVariantMap& recipeParams) { m_profileManager->uploadRecipeProfile(recipeParams); }
    Q_INVOKABLE QVariantMap getOrConvertRecipeParams() { return m_profileManager->getOrConvertRecipeParams(); }
    Q_INVOKABLE void createNewRecipe(const QString& title = "New Recipe") { m_profileManager->createNewRecipe(title); }
    Q_INVOKABLE void createNewAFlowRecipe(const QString& title = "New A-Flow Recipe") { m_profileManager->createNewAFlowRecipe(title); }
    Q_INVOKABLE void createNewPressureProfile(const QString& title = "New Pressure Profile") { m_profileManager->createNewPressureProfile(title); }
    Q_INVOKABLE void createNewFlowProfile(const QString& title = "New Flow Profile") { m_profileManager->createNewFlowProfile(title); }
    Q_INVOKABLE void convertCurrentProfileToAdvanced() { m_profileManager->convertCurrentProfileToAdvanced(); }

    // Frame operations — forwarded to ProfileManager
    Q_INVOKABLE void addFrame(int afterIndex = -1) { m_profileManager->addFrame(afterIndex); }
    Q_INVOKABLE void deleteFrame(int index) { m_profileManager->deleteFrame(index); }
    Q_INVOKABLE void moveFrameUp(int index) { m_profileManager->moveFrameUp(index); }
    Q_INVOKABLE void moveFrameDown(int index) { m_profileManager->moveFrameDown(index); }
    Q_INVOKABLE void duplicateFrame(int index) { m_profileManager->duplicateFrame(index); }
    Q_INVOKABLE void setFrameProperty(int index, const QString& property, const QVariant& value) { m_profileManager->setFrameProperty(index, property, value); }
    Q_INVOKABLE QVariantMap getFrameAt(int index) const { return m_profileManager->getFrameAt(index); }
    Q_INVOKABLE int frameCount() const { return m_profileManager->frameCount(); }
    Q_INVOKABLE void createNewProfile(const QString& title = "New Profile") { m_profileManager->createNewProfile(title); }

public slots:
    void loadProfile(const QString& profileName) { m_profileManager->loadProfile(profileName); }
    Q_INVOKABLE bool loadProfileFromJson(const QString& jsonContent) { return m_profileManager->loadProfileFromJson(jsonContent); }
    void refreshProfiles() { m_profileManager->refreshProfiles(); }
    Q_INVOKABLE void uploadCurrentProfile() { m_profileManager->uploadCurrentProfile(); }
    Q_INVOKABLE void uploadProfile(const QVariantMap& profileData) { m_profileManager->uploadProfile(profileData); }
    Q_INVOKABLE bool saveProfile(const QString& filename) { return m_profileManager->saveProfile(filename); }
    Q_INVOKABLE bool saveProfileAs(const QString& filename, const QString& title) { return m_profileManager->saveProfileAs(filename, title); }

    void applySteamSettings();
    void applyHotWaterSettings();
    void applyFlushSettings();

    // Real-time hot water setting updates
    void setHotWaterFlowRateImmediate(int flow);

    // Real-time steam setting updates
    void setSteamTemperatureImmediate(double temp);
    void setSteamFlowImmediate(int flow);
    void setSteamTimeoutImmediate(int timeout);

    // Soft stop steam (sends 1-second timeout to trigger elapsed > target, no purge)
    Q_INVOKABLE void softStopSteam();

    // Send steam temperature to machine without saving to settings (for enable/disable toggle)
    Q_INVOKABLE void sendSteamTemperature(double temp);

    // Start heating steam heater (ignores keepSteamHeaterOn - for when user wants to steam)
    Q_INVOKABLE void startSteamHeating();

    // Turn off steam heater (sends 0 C)
    Q_INVOKABLE void turnOffSteamHeater();

    void onEspressoCycleStarted();
    void onShotEnded();
    void onScaleWeightChanged(double weight);  // Called by scale weight updates

    // DYE: upload pending shot with current metadata from Settings
    Q_INVOKABLE void uploadPendingShot();

    // Developer mode: generate fake shot data for testing UI
    Q_INVOKABLE void generateFakeShotData();

    // Clear crash log file (called from QML after user dismisses crash report dialog)
    Q_INVOKABLE void clearCrashLog();

    Q_INVOKABLE void factoryResetAndQuit();

signals:
    void currentProfileChanged();
    void profileModifiedChanged();
    void targetWeightChanged();
    void profilesChanged();
    void allBuiltInProfileListChanged();
    void sawSettlingChanged();

    // Filtered goal setpoints (zeroed for non-active mode, reset when extraction ends)
    void goalsChanged();

    // Accessibility: emitted when extraction frame changes
    void frameChanged(int frameIndex, const QString& frameName, const QString& transitionReason);

    // DYE: emitted when shot ends and should show metadata page
    void shotEndedShowMetadata();
    void lastSavedShotIdChanged();

    // Shot aborted because saved scale is not connected
    void shotAbortedNoScale();

    // Shot metadata loaded from history (for async loadShotWithMetadata)
    void shotMetadataLoaded(qint64 shotId, bool success);

    // Auto-wake: emitted when scheduled wake time is reached
    void autoWakeTriggered();

    // Remote sleep: emitted when sleep is triggered via MQTT or REST API
    void remoteSleepRequested();

    // Auto flow calibration: emitted when per-profile multiplier is updated
    void flowCalibrationAutoUpdated(const QString& profileTitle, double oldValue, double newValue);

private slots:
    void onShotSampleReceived(const ShotSample& sample);

private:
    void applyAllSettings();
    void applyLoadedShotMetadata(qint64 shotId, const ShotRecord& shotRecord);
    void applyWaterRefillLevel();
    void applyRefillKitOverride();
    void applyHeaterTweaks();
    void applyFlowCalibration();
    void computeAutoFlowCalibration();
    void updateGlobalFromPerProfileMedian();
    double getGroupTemperature() const;
    void sendMachineSettings();

    ProfileManager* m_profileManager = nullptr;

    QNetworkAccessManager* m_networkManager = nullptr;
    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    ShotDataModel* m_shotDataModel = nullptr;
    ProfileStorage* m_profileStorage = nullptr;
    VisualizerUploader* m_visualizer = nullptr;
    VisualizerImporter* m_visualizerImporter = nullptr;
    AIManager* m_aiManager = nullptr;
    ShotTimingController* m_timingController = nullptr;
    BLEManager* m_bleManager = nullptr;
    FlowScale* m_flowScale = nullptr;  // Shadow FlowScale for comparison logging

    double m_shotStartTime = 0;
    double m_lastSampleTime = 0;  // For delta time calculation (DE1's raw timer)
    double m_lastShotTime = 0;    // Last shot sample time relative to shot start (for weight sync)
    bool m_extractionStarted = false;
    int m_lastFrameNumber = -1;
    int m_trackLogCounter = 0;
    double m_filteredGoalPressure = 0.0;
    double m_filteredGoalFlow = 0.0;
    int m_frameWeightSkipSent = -1;  // Frame number for which we've sent a weight-based skip command
    double m_frameStartTime = 0;     // Shot-relative time when current frame started
    double m_lastPressure = 0;       // Last sample pressure (for transition reason inference)
    double m_lastFlow = 0;           // Last sample flow (for transition reason inference)
    bool m_tareDone = false;  // Track if we've tared for this shot

    QString m_currentFrameName;  // For accessibility announcements

    QTimer m_heaterTweaksTimer;  // Debounce slider changes before sending MMR writes

    // DYE: pending shot data for delayed upload
    bool m_hasPendingShot = false;
    double m_pendingShotDuration = 0;
    double m_pendingShotFinalWeight = 0;
    double m_pendingShotDoseWeight = 0;
    qint64 m_lastSavedShotId = 0;  // ID of most recently saved shot (for post-shot review)
    bool m_savingShot = false;     // Guard against overlapping async saves

    // Shot history and comparison
    ShotHistoryStorage* m_shotHistory = nullptr;
    ShotImporter* m_shotImporter = nullptr;
    ProfileConverter* m_profileConverter = nullptr;
    ProfileImporter* m_profileImporter = nullptr;
    ShotDebugLogger* m_shotDebugLogger = nullptr;
    ShotComparisonModel* m_shotComparison = nullptr;
    ShotServer* m_shotServer = nullptr;
    MqttClient* m_mqttClient = nullptr;
    UpdateChecker* m_updateChecker = nullptr;
    LocationProvider* m_locationProvider = nullptr;
    DataMigrationClient* m_dataMigration = nullptr;
    ShotReporter* m_shotReporter = nullptr;
    DatabaseBackupManager* m_backupManager = nullptr;
};
