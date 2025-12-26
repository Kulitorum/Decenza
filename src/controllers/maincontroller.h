#pragma once

#include <QObject>
#include <QVariantList>
#include <QMap>
#include "../profile/profile.h"

class Settings;
class DE1Device;
class MachineState;
class ShotDataModel;
struct ShotSample;

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentProfileName READ currentProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(QString baseProfileName READ baseProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(bool profileModified READ isProfileModified NOTIFY profileModifiedChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY profilesChanged)

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

    const Profile& currentProfile() const { return m_currentProfile; }

    Q_INVOKABLE QVariantMap getCurrentProfile() const;
    Q_INVOKABLE void markProfileClean();  // Called after save

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

private slots:
    void onShotSampleReceived(const ShotSample& sample);

private:
    void loadDefaultProfile();
    QString profilesPath() const;

    Settings* m_settings = nullptr;
    DE1Device* m_device = nullptr;
    MachineState* m_machineState = nullptr;
    ShotDataModel* m_shotDataModel = nullptr;

    Profile m_currentProfile;
    QStringList m_availableProfiles;
    QMap<QString, QString> m_profileTitles;  // filename -> display title
    double m_shotStartTime = 0;
    double m_lastSampleTime = 0;  // For delta time calculation
    bool m_extractionStarted = false;
    int m_lastFrameNumber = -1;
    bool m_tareDone = false;  // Track if we've tared for this shot

    QString m_baseProfileName;
    bool m_profileModified = false;
};
