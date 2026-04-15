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
#include "../models/steamdatamodel.h"
#include "../machine/steamhealthtracker.h"
#include "../machine/steamcalibrator.h"
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
class DiFluidR2;
class ProfileStorage;
class ShotDebugLogger;
class LocationProvider;
class ShotTimingController;
struct ShotSample;

class MainController : public QObject {
    Q_OBJECT

    // Non-profile QML properties (profile properties are on ProfileManager)
    Q_PROPERTY(VisualizerUploader* visualizer READ visualizer CONSTANT)
    Q_PROPERTY(VisualizerImporter* visualizerImporter READ visualizerImporter CONSTANT)
    Q_PROPERTY(AIManager* aiManager READ aiManager CONSTANT)
    Q_PROPERTY(ShotDataModel* shotDataModel READ shotDataModel CONSTANT)
    Q_PROPERTY(SteamDataModel* steamDataModel READ steamDataModel CONSTANT)
    Q_PROPERTY(SteamHealthTracker* steamHealthTracker READ steamHealthTracker CONSTANT)
    Q_PROPERTY(SteamCalibrator* steamCalibrator READ steamCalibrator CONSTANT)
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
    Q_PROPERTY(qint64 lastSavedShotId READ lastSavedShotId NOTIFY lastSavedShotIdChanged)
    Q_PROPERTY(bool sawSettling READ isSawSettling NOTIFY sawSettlingChanged)

public:
    explicit MainController(QNetworkAccessManager* networkManager,
                           Settings* settings, DE1Device* device,
                           MachineState* machineState, ShotDataModel* shotDataModel,
                           ProfileStorage* profileStorage = nullptr,
                           QObject* parent = nullptr);

    // ProfileManager accessor
    ProfileManager* profileManager() const { return m_profileManager; }

    // Non-profile accessors
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
    void setRefractometer(DiFluidR2* refractometer);
    DiFluidR2* refractometer() const { return m_refractometer; }
    void setTimingController(ShotTimingController* controller) { m_timingController = controller; }
    void setBackupManager(DatabaseBackupManager* backupManager) { m_backupManager = backupManager; }
    ShotDataModel* shotDataModel() const { return m_shotDataModel; }
    SteamDataModel* steamDataModel() const { return m_steamDataModel; }
    SteamHealthTracker* steamHealthTracker() const { return m_steamHealthTracker; }
    SteamCalibrator* steamCalibrator() const { return m_steamCalibrator; }
    void setSteamDataModel(SteamDataModel* model) { m_steamDataModel = model; }
    void setSteamHealthTracker(SteamHealthTracker* tracker) { m_steamHealthTracker = tracker; }
    void setSteamCalibrator(SteamCalibrator* calibrator) {
        m_steamCalibrator = calibrator;
        if (calibrator) {
            connect(calibrator, &SteamCalibrator::settingsApplied, this, &MainController::sendMachineSettings);
        }
    }
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

    Q_INVOKABLE void loadShotWithMetadata(qint64 shotId);  // Uses shot history

    // Clipboard
    Q_INVOKABLE void copyToClipboard(const QString& text);
    Q_INVOKABLE QString pasteFromClipboard() const;

public slots:
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
    // Verify that the DE1's stored steam/group targets match what we've
    // commanded. Logs drift and auto-heals by re-sending ShotSettings, with
    // a retry budget to avoid infinite loops when the DE1 refuses the value.
    void onShotSettingsReported(double deviceSteamTargetC, double deviceGroupTargetC);

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
    // Returns the steam target we'd command right now based on the current
    // Settings (steamDisabled / keepSteamHeaterOn / steamTemperature). Used
    // by onShotSettingsReported to detect drift.
    double expectedSteamTargetC() const;

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
    DiFluidR2* m_refractometer = nullptr;

    SteamDataModel* m_steamDataModel = nullptr;
    SteamHealthTracker* m_steamHealthTracker = nullptr;
    SteamCalibrator* m_steamCalibrator = nullptr;
    double m_steamStartTime = 0;  // Timer base for relative steam timestamps

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

    // ShotSettings drift auto-heal tracking. The commanded values live on
    // DE1Device (so every call site — MainController, ProfileManager,
    // SteamCalibrator — feeds the same tracker); we only keep the retry
    // bookkeeping here.
    int m_shotSettingsDriftResendCount = 0;
    qint64 m_lastShotSettingsResendMs = 0;
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
    qint64 m_pendingShotEpoch = 0;
    QString m_pendingDebugLog;
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
