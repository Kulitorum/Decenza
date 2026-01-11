#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
#include <QTimer>
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
#include "../core/updatechecker.h"

class Settings;
class DE1Device;
class MachineState;
class ProfileStorage;
class ShotDebugLogger;
class LocationProvider;
struct ShotSample;

// Profile source enumeration
enum class ProfileSource {
    BuiltIn,      // Shipped with app in :/profiles/
    Downloaded,   // Downloaded from visualizer.coffee
    UserCreated   // Created or edited by user
};

// Profile metadata for filtering and display
struct ProfileInfo {
    QString filename;
    QString title;
    QString beverageType;
    ProfileSource source;
    bool isRecipeMode = false;
};

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(QString baseProfileName READ baseProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(bool profileModified READ isProfileModified NOTIFY profileModifiedChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(bool brewByRatioActive READ brewByRatioActive NOTIFY brewByRatioChanged)
    Q_PROPERTY(double brewByRatioDose READ brewByRatioDose NOTIFY brewByRatioChanged)
    Q_PROPERTY(double brewByRatio READ brewByRatio NOTIFY brewByRatioChanged)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList selectedProfiles READ selectedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allBuiltInProfiles READ allBuiltInProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList cleaningProfiles READ cleaningProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList downloadedProfiles READ downloadedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList userCreatedProfiles READ userCreatedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allProfilesList READ allProfilesList NOTIFY profilesChanged)
    Q_PROPERTY(VisualizerUploader* visualizer READ visualizer CONSTANT)
    Q_PROPERTY(VisualizerImporter* visualizerImporter READ visualizerImporter CONSTANT)
    Q_PROPERTY(AIManager* aiManager READ aiManager CONSTANT)
    Q_PROPERTY(ShotDataModel* shotDataModel READ shotDataModel CONSTANT)
    Q_PROPERTY(Profile* currentProfilePtr READ currentProfilePtr CONSTANT)
    Q_PROPERTY(bool calibrationMode READ isCalibrationMode NOTIFY calibrationModeChanged)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY frameChanged)
    Q_PROPERTY(ShotHistoryStorage* shotHistory READ shotHistory CONSTANT)
    Q_PROPERTY(ShotImporter* shotImporter READ shotImporter CONSTANT)
    Q_PROPERTY(ProfileConverter* profileConverter READ profileConverter CONSTANT)
    Q_PROPERTY(ProfileImporter* profileImporter READ profileImporter CONSTANT)
    Q_PROPERTY(ShotComparisonModel* shotComparison READ shotComparison CONSTANT)
    Q_PROPERTY(ShotServer* shotServer READ shotServer CONSTANT)
    Q_PROPERTY(UpdateChecker* updateChecker READ updateChecker CONSTANT)
    Q_PROPERTY(ShotReporter* shotReporter READ shotReporter CONSTANT)
    Q_PROPERTY(bool isCurrentProfileRecipe READ isCurrentProfileRecipe NOTIFY currentProfileChanged)

public:
    explicit MainController(Settings* settings, DE1Device* device,
                           MachineState* machineState, ShotDataModel* shotDataModel,
                           ProfileStorage* profileStorage = nullptr,
                           QObject* parent = nullptr);

    QString currentProfileName() const;
    QString baseProfileName() const { return m_baseProfileName; }
    bool isProfileModified() const { return m_profileModified; }
    double targetWeight() const;
    void setTargetWeight(double weight);
    bool brewByRatioActive() const { return m_brewByRatioActive; }
    double brewByRatioDose() const { return m_brewByRatioDose; }
    double brewByRatio() const { return m_brewByRatio; }
    Q_INVOKABLE void activateBrewByRatio(double dose, double ratio);
    Q_INVOKABLE void clearBrewByRatio();
    QVariantList availableProfiles() const;
    QVariantList selectedProfiles() const;
    QVariantList allBuiltInProfiles() const;
    QVariantList cleaningProfiles() const;
    QVariantList downloadedProfiles() const;
    QVariantList userCreatedProfiles() const;
    QVariantList allProfilesList() const;
    VisualizerUploader* visualizer() const { return m_visualizer; }
    VisualizerImporter* visualizerImporter() const { return m_visualizerImporter; }
    ProfileStorage* profileStorage() const { return m_profileStorage; }
    AIManager* aiManager() const { return m_aiManager; }
    void setAiManager(AIManager* aiManager) { m_aiManager = aiManager; }
    ShotDataModel* shotDataModel() const { return m_shotDataModel; }
    Profile* currentProfilePtr() { return &m_currentProfile; }
    bool isCalibrationMode() const { return m_calibrationMode; }
    QString currentFrameName() const { return m_currentFrameName; }
    bool isCurrentProfileRecipe() const { return m_currentProfile.isRecipeMode(); }
    ShotHistoryStorage* shotHistory() const { return m_shotHistory; }
    ShotImporter* shotImporter() const { return m_shotImporter; }
    ProfileConverter* profileConverter() const { return m_profileConverter; }
    ProfileImporter* profileImporter() const { return m_profileImporter; }
    ShotComparisonModel* shotComparison() const { return m_shotComparison; }
    ShotServer* shotServer() const { return m_shotServer; }
    UpdateChecker* updateChecker() const { return m_updateChecker; }
    ShotReporter* shotReporter() const { return m_shotReporter; }

    const Profile& currentProfile() const { return m_currentProfile; }
    Profile currentProfileObject() const { return m_currentProfile; }

    // For simulator integration
    void handleShotSample(const ShotSample& sample) { onShotSampleReceived(sample); }

    Q_INVOKABLE QVariantMap getCurrentProfile() const;
    Q_INVOKABLE void markProfileClean();  // Called after save
    Q_INVOKABLE QString titleToFilename(const QString& title) const;
    Q_INVOKABLE QString findProfileByTitle(const QString& title) const;  // Returns filename or empty string
    Q_INVOKABLE bool profileExists(const QString& filename) const;
    Q_INVOKABLE bool deleteProfile(const QString& filename);  // Delete user/downloaded profile

    // Recipe Editor methods (DEPRECATED - kept for backward compatibility)
    Q_INVOKABLE void uploadRecipeProfile(const QVariantMap& recipeParams);
    Q_INVOKABLE QVariantMap getCurrentRecipeParams() const;
    Q_INVOKABLE void createNewRecipe(const QString& title = "New Recipe");
    Q_INVOKABLE void applyRecipePreset(const QString& presetName);

    // Profile mode conversion
    Q_INVOKABLE void convertCurrentProfileToRecipe();   // Advanced -> D-Flow (simplifies profile)
    Q_INVOKABLE void convertCurrentProfileToAdvanced(); // D-Flow -> Advanced (expands capabilities)

    // === D-Flow Frame Editor Methods ===
    // Frame operations
    Q_INVOKABLE void addFrame(int afterIndex = -1);       // Add new frame after index (-1 = at end)
    Q_INVOKABLE void deleteFrame(int index);              // Delete frame at index
    Q_INVOKABLE void moveFrameUp(int index);              // Move frame up (swap with previous)
    Q_INVOKABLE void moveFrameDown(int index);            // Move frame down (swap with next)
    Q_INVOKABLE void duplicateFrame(int index);           // Duplicate frame at index

    // Frame property editing
    Q_INVOKABLE void setFrameProperty(int index, const QString& property, const QVariant& value);
    Q_INVOKABLE QVariantMap getFrameAt(int index) const;  // Get frame as QVariantMap for QML
    Q_INVOKABLE int frameCount() const;                   // Number of frames in current profile

    // New profile creation
    Q_INVOKABLE void createNewProfile(const QString& title = "New Profile");

public slots:
    void loadProfile(const QString& profileName);
    Q_INVOKABLE bool loadProfileFromJson(const QString& jsonContent);  // Load profile from JSON string (e.g., from shot history)
    void refreshProfiles();
    void uploadCurrentProfile();
    Q_INVOKABLE void uploadProfile(const QVariantMap& profileData);
    Q_INVOKABLE bool saveProfile(const QString& filename);
    Q_INVOKABLE bool saveProfileAs(const QString& filename, const QString& title);

    void applySteamSettings();
    void applyHotWaterSettings();
    void applyFlushSettings();

    // Flow sensor calibration
    Q_INVOKABLE void startCalibrationDispense(double flowRate, double targetWeight);
    Q_INVOKABLE void startVerificationDispense(double targetWeight);  // Uses FlowScale to stop
    Q_INVOKABLE void restoreCurrentProfile();

    // Real-time steam setting updates
    void setSteamTemperatureImmediate(double temp);
    void setSteamFlowImmediate(int flow);
    void setSteamTimeoutImmediate(int timeout);

    // Soft stop steam (sends 1-second timeout to trigger elapsed > target, no purge)
    Q_INVOKABLE void softStopSteam();

    // Send steam temperature to machine without saving to settings (for enable/disable toggle)
    Q_INVOKABLE void sendSteamTemperature(double temp);

    void onEspressoCycleStarted();
    void onShotEnded();
    void onScaleWeightChanged(double weight);  // Called by scale weight updates

    // DYE: upload pending shot with current metadata from Settings
    Q_INVOKABLE void uploadPendingShot();

    // Developer mode: generate fake shot data for testing UI
    Q_INVOKABLE void generateFakeShotData();

signals:
    void currentProfileChanged();
    void profileModifiedChanged();
    void targetWeightChanged();
    void brewByRatioChanged();
    void profilesChanged();
    void calibrationModeChanged();

    // Accessibility: emitted when extraction frame changes
    void frameChanged(int frameIndex, const QString& frameName);

    // DYE: emitted when shot ends and should show metadata page
    void shotEndedShowMetadata();

private slots:
    void onShotSampleReceived(const ShotSample& sample);

private:
    void loadDefaultProfile();
    void migrateProfileFolders();
    QString profilesPath() const;
    QString userProfilesPath() const;
    QString downloadedProfilesPath() const;
    void applyAllSettings();

    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    ShotDataModel* m_shotDataModel = nullptr;
    ProfileStorage* m_profileStorage = nullptr;
    VisualizerUploader* m_visualizer = nullptr;
    VisualizerImporter* m_visualizerImporter = nullptr;
    AIManager* m_aiManager = nullptr;

    Profile m_currentProfile;
    QStringList m_availableProfiles;
    QMap<QString, QString> m_profileTitles;  // filename -> display title
    QList<ProfileInfo> m_allProfiles;  // Complete list with metadata
    double m_shotStartTime = 0;
    double m_lastSampleTime = 0;  // For delta time calculation
    double m_weightTimeOffset = 0;  // Offset to sync weight time with shot sample time
    bool m_extractionStarted = false;
    int m_lastFrameNumber = -1;
    int m_frameWeightSkipSent = -1;  // Frame number for which we've sent a weight-based skip command
    bool m_tareDone = false;  // Track if we've tared for this shot

    QString m_baseProfileName;
    bool m_profileModified = false;
    bool m_calibrationMode = false;
    QString m_currentFrameName;  // For accessibility announcements

    // Brew-by-ratio state
    bool m_brewByRatioActive = false;
    double m_brewByRatioDose = 0.0;
    double m_brewByRatio = 2.0;

    QTimer m_settingsTimer;  // Delayed settings application after connection

    // DYE: pending shot data for delayed upload
    bool m_hasPendingShot = false;
    double m_pendingShotDuration = 0;
    double m_pendingShotFinalWeight = 0;
    double m_pendingShotDoseWeight = 0;

    // Shot history and comparison
    ShotHistoryStorage* m_shotHistory = nullptr;
    ShotImporter* m_shotImporter = nullptr;
    ProfileConverter* m_profileConverter = nullptr;
    ProfileImporter* m_profileImporter = nullptr;
    ShotDebugLogger* m_shotDebugLogger = nullptr;
    ShotComparisonModel* m_shotComparison = nullptr;
    ShotServer* m_shotServer = nullptr;
    UpdateChecker* m_updateChecker = nullptr;
    LocationProvider* m_locationProvider = nullptr;
    ShotReporter* m_shotReporter = nullptr;
};
