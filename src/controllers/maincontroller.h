#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
#include <QTimer>
#include "../profile/profile.h"
#include "../network/visualizeruploader.h"
#include "../network/visualizerimporter.h"

class Settings;
class DE1Device;
class MachineState;
class ShotDataModel;
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
};

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(QString baseProfileName READ baseProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(bool profileModified READ isProfileModified NOTIFY profileModifiedChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList selectedProfiles READ selectedProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList allBuiltInProfiles READ allBuiltInProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList cleaningProfiles READ cleaningProfiles NOTIFY profilesChanged)
    Q_PROPERTY(VisualizerUploader* visualizer READ visualizer CONSTANT)
    Q_PROPERTY(VisualizerImporter* visualizerImporter READ visualizerImporter CONSTANT)
    Q_PROPERTY(bool calibrationMode READ isCalibrationMode NOTIFY calibrationModeChanged)
    Q_PROPERTY(QString currentFrameName READ currentFrameName NOTIFY frameChanged)

public:
    explicit MainController(Settings* settings, DE1Device* device,
                           MachineState* machineState, ShotDataModel* shotDataModel,
                           QObject* parent = nullptr);

    QString currentProfileName() const;
    QString baseProfileName() const { return m_baseProfileName; }
    bool isProfileModified() const { return m_profileModified; }
    double targetWeight() const;
    void setTargetWeight(double weight);
    QVariantList availableProfiles() const;
    QVariantList selectedProfiles() const;
    QVariantList allBuiltInProfiles() const;
    QVariantList cleaningProfiles() const;
    VisualizerUploader* visualizer() const { return m_visualizer; }
    VisualizerImporter* visualizerImporter() const { return m_visualizerImporter; }
    bool isCalibrationMode() const { return m_calibrationMode; }
    QString currentFrameName() const { return m_currentFrameName; }

    const Profile& currentProfile() const { return m_currentProfile; }

    Q_INVOKABLE QVariantMap getCurrentProfile() const;
    Q_INVOKABLE void markProfileClean();  // Called after save
    Q_INVOKABLE QString titleToFilename(const QString& title) const;
    Q_INVOKABLE bool profileExists(const QString& filename) const;
    Q_INVOKABLE bool deleteProfile(const QString& filename);  // Delete user/downloaded profile

public slots:
    void loadProfile(const QString& profileName);
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

    void onEspressoCycleStarted();
    void onShotEnded();
    void onScaleWeightChanged(double weight);  // Called by scale weight updates

signals:
    void currentProfileChanged();
    void profileModifiedChanged();
    void targetWeightChanged();
    void profilesChanged();
    void calibrationModeChanged();

    // Accessibility: emitted when extraction frame changes
    void frameChanged(int frameIndex, const QString& frameName);

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
    VisualizerUploader* m_visualizer = nullptr;
    VisualizerImporter* m_visualizerImporter = nullptr;

    Profile m_currentProfile;
    QStringList m_availableProfiles;
    QMap<QString, QString> m_profileTitles;  // filename -> display title
    QList<ProfileInfo> m_allProfiles;  // Complete list with metadata
    double m_shotStartTime = 0;
    double m_lastSampleTime = 0;  // For delta time calculation
    bool m_extractionStarted = false;
    int m_lastFrameNumber = -1;
    bool m_tareDone = false;  // Track if we've tared for this shot

    QString m_baseProfileName;
    bool m_profileModified = false;
    bool m_calibrationMode = false;
    QString m_currentFrameName;  // For accessibility announcements

    QTimer m_settingsTimer;  // Delayed settings application after connection
};
