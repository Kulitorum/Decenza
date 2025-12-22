#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QJsonArray>
#include <QJsonObject>

class Settings : public QObject {
    Q_OBJECT

    // Machine settings
    Q_PROPERTY(QString machineAddress READ machineAddress WRITE setMachineAddress NOTIFY machineAddressChanged)
    Q_PROPERTY(QString scaleAddress READ scaleAddress WRITE setScaleAddress NOTIFY scaleAddressChanged)
    Q_PROPERTY(QString scaleType READ scaleType WRITE setScaleType NOTIFY scaleTypeChanged)

    // Espresso settings
    Q_PROPERTY(double espressoTemperature READ espressoTemperature WRITE setEspressoTemperature NOTIFY espressoTemperatureChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)

    // Steam settings
    Q_PROPERTY(double steamTemperature READ steamTemperature WRITE setSteamTemperature NOTIFY steamTemperatureChanged)
    Q_PROPERTY(int steamTimeout READ steamTimeout WRITE setSteamTimeout NOTIFY steamTimeoutChanged)
    Q_PROPERTY(int steamFlow READ steamFlow WRITE setSteamFlow NOTIFY steamFlowChanged)

    // Steam cup presets
    Q_PROPERTY(QVariantList steamCupPresets READ steamCupPresets NOTIFY steamCupPresetsChanged)
    Q_PROPERTY(int selectedSteamCup READ selectedSteamCup WRITE setSelectedSteamCup NOTIFY selectedSteamCupChanged)

    // Hot water settings
    Q_PROPERTY(double waterTemperature READ waterTemperature WRITE setWaterTemperature NOTIFY waterTemperatureChanged)
    Q_PROPERTY(int waterVolume READ waterVolume WRITE setWaterVolume NOTIFY waterVolumeChanged)

    // UI settings
    Q_PROPERTY(QString skin READ skin WRITE setSkin NOTIFY skinChanged)
    Q_PROPERTY(QString skinPath READ skinPath NOTIFY skinChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

public:
    explicit Settings(QObject* parent = nullptr);

    // Machine settings
    QString machineAddress() const;
    void setMachineAddress(const QString& address);

    QString scaleAddress() const;
    void setScaleAddress(const QString& address);

    QString scaleType() const;
    void setScaleType(const QString& type);

    // Espresso settings
    double espressoTemperature() const;
    void setEspressoTemperature(double temp);

    double targetWeight() const;
    void setTargetWeight(double weight);

    // Steam settings
    double steamTemperature() const;
    void setSteamTemperature(double temp);

    int steamTimeout() const;
    void setSteamTimeout(int timeout);

    int steamFlow() const;
    void setSteamFlow(int flow);

    // Steam cup presets
    QVariantList steamCupPresets() const;
    int selectedSteamCup() const;
    void setSelectedSteamCup(int index);

    Q_INVOKABLE void addSteamCupPreset(const QString& name, int duration, int flow);
    Q_INVOKABLE void updateSteamCupPreset(int index, const QString& name, int duration, int flow);
    Q_INVOKABLE void removeSteamCupPreset(int index);
    Q_INVOKABLE void moveSteamCupPreset(int from, int to);
    Q_INVOKABLE QVariantMap getSteamCupPreset(int index) const;

    // Hot water settings
    double waterTemperature() const;
    void setWaterTemperature(double temp);

    int waterVolume() const;
    void setWaterVolume(int volume);

    // UI settings
    QString skin() const;
    void setSkin(const QString& skin);
    QString skinPath() const;

    QString currentProfile() const;
    void setCurrentProfile(const QString& profile);

signals:
    void machineAddressChanged();
    void scaleAddressChanged();
    void scaleTypeChanged();
    void espressoTemperatureChanged();
    void targetWeightChanged();
    void steamTemperatureChanged();
    void steamTimeoutChanged();
    void steamFlowChanged();
    void steamCupPresetsChanged();
    void selectedSteamCupChanged();
    void waterTemperatureChanged();
    void waterVolumeChanged();
    void skinChanged();
    void currentProfileChanged();

private:
    QSettings m_settings;
};
