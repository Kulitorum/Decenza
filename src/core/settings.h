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

    // Flow sensor calibration
    Q_PROPERTY(double flowCalibrationFactor READ flowCalibrationFactor WRITE setFlowCalibrationFactor NOTIFY flowCalibrationFactorChanged)

    // Espresso settings
    Q_PROPERTY(double espressoTemperature READ espressoTemperature WRITE setEspressoTemperature NOTIFY espressoTemperatureChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)

    // Steam settings
    Q_PROPERTY(double steamTemperature READ steamTemperature WRITE setSteamTemperature NOTIFY steamTemperatureChanged)
    Q_PROPERTY(int steamTimeout READ steamTimeout WRITE setSteamTimeout NOTIFY steamTimeoutChanged)
    Q_PROPERTY(int steamFlow READ steamFlow WRITE setSteamFlow NOTIFY steamFlowChanged)

    // Steam pitcher presets
    Q_PROPERTY(QVariantList steamPitcherPresets READ steamPitcherPresets NOTIFY steamPitcherPresetsChanged)
    Q_PROPERTY(int selectedSteamPitcher READ selectedSteamPitcher WRITE setSelectedSteamCup NOTIFY selectedSteamPitcherChanged)

    // Profile favorites
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY favoriteProfilesChanged)
    Q_PROPERTY(int selectedFavoriteProfile READ selectedFavoriteProfile WRITE setSelectedFavoriteProfile NOTIFY selectedFavoriteProfileChanged)

    // Selected built-in profiles (shown in "Selected" view)
    Q_PROPERTY(QStringList selectedBuiltInProfiles READ selectedBuiltInProfiles WRITE setSelectedBuiltInProfiles NOTIFY selectedBuiltInProfilesChanged)

    // Hot water settings
    Q_PROPERTY(double waterTemperature READ waterTemperature WRITE setWaterTemperature NOTIFY waterTemperatureChanged)
    Q_PROPERTY(int waterVolume READ waterVolume WRITE setWaterVolume NOTIFY waterVolumeChanged)

    // Hot water vessel presets
    Q_PROPERTY(QVariantList waterVesselPresets READ waterVesselPresets NOTIFY waterVesselPresetsChanged)
    Q_PROPERTY(int selectedWaterVessel READ selectedWaterVessel WRITE setSelectedWaterCup NOTIFY selectedWaterVesselChanged)

    // Flush presets
    Q_PROPERTY(QVariantList flushPresets READ flushPresets NOTIFY flushPresetsChanged)
    Q_PROPERTY(int selectedFlushPreset READ selectedFlushPreset WRITE setSelectedFlushPreset NOTIFY selectedFlushPresetChanged)
    Q_PROPERTY(double flushFlow READ flushFlow WRITE setFlushFlow NOTIFY flushFlowChanged)
    Q_PROPERTY(double flushSeconds READ flushSeconds WRITE setFlushSeconds NOTIFY flushSecondsChanged)

    // UI settings
    Q_PROPERTY(QString skin READ skin WRITE setSkin NOTIFY skinChanged)
    Q_PROPERTY(QString skinPath READ skinPath NOTIFY skinChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)

    // Theme settings
    Q_PROPERTY(QVariantMap customThemeColors READ customThemeColors WRITE setCustomThemeColors NOTIFY customThemeColorsChanged)
    Q_PROPERTY(QVariantList colorGroups READ colorGroups WRITE setColorGroups NOTIFY colorGroupsChanged)
    Q_PROPERTY(QString activeThemeName READ activeThemeName WRITE setActiveThemeName NOTIFY activeThemeNameChanged)
    Q_PROPERTY(double screenBrightness READ screenBrightness WRITE setScreenBrightness NOTIFY screenBrightnessChanged)

    // Visualizer settings
    Q_PROPERTY(QString visualizerUsername READ visualizerUsername WRITE setVisualizerUsername NOTIFY visualizerUsernameChanged)
    Q_PROPERTY(QString visualizerPassword READ visualizerPassword WRITE setVisualizerPassword NOTIFY visualizerPasswordChanged)
    Q_PROPERTY(bool visualizerAutoUpload READ visualizerAutoUpload WRITE setVisualizerAutoUpload NOTIFY visualizerAutoUploadChanged)
    Q_PROPERTY(double visualizerMinDuration READ visualizerMinDuration WRITE setVisualizerMinDuration NOTIFY visualizerMinDurationChanged)

public:
    explicit Settings(QObject* parent = nullptr);

    // Machine settings
    QString machineAddress() const;
    void setMachineAddress(const QString& address);

    QString scaleAddress() const;
    void setScaleAddress(const QString& address);

    QString scaleType() const;
    void setScaleType(const QString& type);

    // Flow sensor calibration
    double flowCalibrationFactor() const;
    void setFlowCalibrationFactor(double factor);

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

    // Steam pitcher presets
    QVariantList steamPitcherPresets() const;
    int selectedSteamPitcher() const;
    void setSelectedSteamCup(int index);

    Q_INVOKABLE void addSteamPitcherPreset(const QString& name, int duration, int flow);
    Q_INVOKABLE void updateSteamPitcherPreset(int index, const QString& name, int duration, int flow);
    Q_INVOKABLE void removeSteamPitcherPreset(int index);
    Q_INVOKABLE void moveSteamPitcherPreset(int from, int to);
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;

    // Profile favorites (max 5)
    QVariantList favoriteProfiles() const;
    int selectedFavoriteProfile() const;
    void setSelectedFavoriteProfile(int index);

    Q_INVOKABLE void addFavoriteProfile(const QString& name, const QString& filename);
    Q_INVOKABLE void removeFavoriteProfile(int index);
    Q_INVOKABLE void moveFavoriteProfile(int from, int to);
    Q_INVOKABLE QVariantMap getFavoriteProfile(int index) const;
    Q_INVOKABLE bool isFavoriteProfile(const QString& filename) const;
    Q_INVOKABLE bool updateFavoriteProfile(const QString& oldFilename, const QString& newFilename, const QString& newTitle);

    // Selected built-in profiles
    QStringList selectedBuiltInProfiles() const;
    void setSelectedBuiltInProfiles(const QStringList& profiles);
    Q_INVOKABLE void addSelectedBuiltInProfile(const QString& filename);
    Q_INVOKABLE void removeSelectedBuiltInProfile(const QString& filename);
    Q_INVOKABLE bool isSelectedBuiltInProfile(const QString& filename) const;

    // Hot water settings
    double waterTemperature() const;
    void setWaterTemperature(double temp);

    int waterVolume() const;
    void setWaterVolume(int volume);

    // Hot water vessel presets
    QVariantList waterVesselPresets() const;
    int selectedWaterVessel() const;
    void setSelectedWaterCup(int index);

    Q_INVOKABLE void addWaterVesselPreset(const QString& name, int volume);
    Q_INVOKABLE void updateWaterVesselPreset(int index, const QString& name, int volume);
    Q_INVOKABLE void removeWaterVesselPreset(int index);
    Q_INVOKABLE void moveWaterVesselPreset(int from, int to);
    Q_INVOKABLE QVariantMap getWaterVesselPreset(int index) const;

    // Flush presets
    QVariantList flushPresets() const;
    int selectedFlushPreset() const;
    void setSelectedFlushPreset(int index);

    double flushFlow() const;
    void setFlushFlow(double flow);

    double flushSeconds() const;
    void setFlushSeconds(double seconds);

    Q_INVOKABLE void addFlushPreset(const QString& name, double flow, double seconds);
    Q_INVOKABLE void updateFlushPreset(int index, const QString& name, double flow, double seconds);
    Q_INVOKABLE void removeFlushPreset(int index);
    Q_INVOKABLE void moveFlushPreset(int from, int to);
    Q_INVOKABLE QVariantMap getFlushPreset(int index) const;

    // UI settings
    QString skin() const;
    void setSkin(const QString& skin);
    QString skinPath() const;

    QString currentProfile() const;
    void setCurrentProfile(const QString& profile);

    // Theme settings
    QVariantMap customThemeColors() const;
    void setCustomThemeColors(const QVariantMap& colors);

    QVariantList colorGroups() const;
    void setColorGroups(const QVariantList& groups);

    QString activeThemeName() const;
    void setActiveThemeName(const QString& name);

    Q_INVOKABLE void setThemeColor(const QString& colorName, const QString& colorValue);
    Q_INVOKABLE QString getThemeColor(const QString& colorName) const;
    Q_INVOKABLE void resetThemeToDefault();
    Q_INVOKABLE QVariantList getPresetThemes() const;
    Q_INVOKABLE void applyPresetTheme(const QString& name);
    Q_INVOKABLE void saveCurrentTheme(const QString& name);
    Q_INVOKABLE void deleteUserTheme(const QString& name);
    Q_INVOKABLE bool saveThemeToFile(const QString& filePath);
    Q_INVOKABLE bool loadThemeFromFile(const QString& filePath);
    Q_INVOKABLE QVariantMap generatePalette(double hue, double saturation, double lightness) const;

    double screenBrightness() const;
    void setScreenBrightness(double brightness);

    // Visualizer settings
    QString visualizerUsername() const;
    void setVisualizerUsername(const QString& username);

    QString visualizerPassword() const;
    void setVisualizerPassword(const QString& password);

    bool visualizerAutoUpload() const;
    void setVisualizerAutoUpload(bool enabled);

    double visualizerMinDuration() const;
    void setVisualizerMinDuration(double seconds);

    // Generic settings access (for extensibility)
    Q_INVOKABLE QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

signals:
    void machineAddressChanged();
    void scaleAddressChanged();
    void scaleTypeChanged();
    void flowCalibrationFactorChanged();
    void espressoTemperatureChanged();
    void targetWeightChanged();
    void steamTemperatureChanged();
    void steamTimeoutChanged();
    void steamFlowChanged();
    void steamPitcherPresetsChanged();
    void selectedSteamPitcherChanged();
    void favoriteProfilesChanged();
    void selectedFavoriteProfileChanged();
    void selectedBuiltInProfilesChanged();
    void waterTemperatureChanged();
    void waterVolumeChanged();
    void waterVesselPresetsChanged();
    void selectedWaterVesselChanged();
    void flushPresetsChanged();
    void selectedFlushPresetChanged();
    void flushFlowChanged();
    void flushSecondsChanged();
    void skinChanged();
    void currentProfileChanged();
    void customThemeColorsChanged();
    void colorGroupsChanged();
    void activeThemeNameChanged();
    void screenBrightnessChanged();
    void visualizerUsernameChanged();
    void visualizerPasswordChanged();
    void visualizerAutoUploadChanged();
    void visualizerMinDurationChanged();
    void valueChanged(const QString& key);

private:
    QSettings m_settings;
};
