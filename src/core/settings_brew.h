#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Brew-domain settings: espresso, steam, hot water, flush, presets, and
// session-only brew/temperature overrides. Split from Settings to keep
// settings.h's transitive-include footprint small.
class SettingsBrew : public QObject {
    Q_OBJECT

    // Espresso
    Q_PROPERTY(double espressoTemperature READ espressoTemperature WRITE setEspressoTemperature NOTIFY espressoTemperatureChanged)
    Q_PROPERTY(double targetWeight READ targetWeight WRITE setTargetWeight NOTIFY targetWeightChanged)
    Q_PROPERTY(double lastUsedRatio READ lastUsedRatio WRITE setLastUsedRatio NOTIFY lastUsedRatioChanged)

    // Steam
    Q_PROPERTY(double steamTemperature READ steamTemperature WRITE setSteamTemperature NOTIFY steamTemperatureChanged)
    Q_PROPERTY(int steamTimeout READ steamTimeout WRITE setSteamTimeout NOTIFY steamTimeoutChanged)
    Q_PROPERTY(int steamFlow READ steamFlow WRITE setSteamFlow NOTIFY steamFlowChanged)
    Q_PROPERTY(bool steamDisabled READ steamDisabled WRITE setSteamDisabled NOTIFY steamDisabledChanged)
    Q_PROPERTY(bool keepSteamHeaterOn READ keepSteamHeaterOn WRITE setKeepSteamHeaterOn NOTIFY keepSteamHeaterOnChanged)
    Q_PROPERTY(int steamAutoFlushSeconds READ steamAutoFlushSeconds WRITE setSteamAutoFlushSeconds NOTIFY steamAutoFlushSecondsChanged)

    // Steam pitcher presets
    Q_PROPERTY(QVariantList steamPitcherPresets READ steamPitcherPresets NOTIFY steamPitcherPresetsChanged)
    Q_PROPERTY(int selectedSteamPitcher READ selectedSteamPitcher WRITE setSelectedSteamCup NOTIFY selectedSteamPitcherChanged)

    // Hot water
    Q_PROPERTY(double waterTemperature READ waterTemperature WRITE setWaterTemperature NOTIFY waterTemperatureChanged)
    Q_PROPERTY(int waterVolume READ waterVolume WRITE setWaterVolume NOTIFY waterVolumeChanged)
    Q_PROPERTY(QString waterVolumeMode READ waterVolumeMode WRITE setWaterVolumeMode NOTIFY waterVolumeModeChanged)
    Q_PROPERTY(double hotWaterSawOffset READ hotWaterSawOffset WRITE setHotWaterSawOffset NOTIFY hotWaterSawOffsetChanged)

    // Hot water vessel presets
    Q_PROPERTY(QVariantList waterVesselPresets READ waterVesselPresets NOTIFY waterVesselPresetsChanged)
    Q_PROPERTY(int selectedWaterVessel READ selectedWaterVessel WRITE setSelectedWaterCup NOTIFY selectedWaterVesselChanged)

    // Flush presets
    Q_PROPERTY(QVariantList flushPresets READ flushPresets NOTIFY flushPresetsChanged)
    Q_PROPERTY(int selectedFlushPreset READ selectedFlushPreset WRITE setSelectedFlushPreset NOTIFY selectedFlushPresetChanged)
    Q_PROPERTY(double flushFlow READ flushFlow WRITE setFlushFlow NOTIFY flushFlowChanged)
    Q_PROPERTY(double flushSeconds READ flushSeconds WRITE setFlushSeconds NOTIFY flushSecondsChanged)

    // Temperature override (persistent)
    Q_PROPERTY(double temperatureOverride READ temperatureOverride WRITE setTemperatureOverride NOTIFY temperatureOverrideChanged)
    Q_PROPERTY(bool hasTemperatureOverride READ hasTemperatureOverride NOTIFY temperatureOverrideChanged)

    // Brew parameter overrides (persistent)
    Q_PROPERTY(double brewYieldOverride READ brewYieldOverride WRITE setBrewYieldOverride NOTIFY brewOverridesChanged)
    Q_PROPERTY(bool hasBrewYieldOverride READ hasBrewYieldOverride NOTIFY brewOverridesChanged)

    // Stop-at-volume gating when a BLE scale provides weight data
    Q_PROPERTY(bool ignoreVolumeWithScale READ ignoreVolumeWithScale WRITE setIgnoreVolumeWithScale NOTIFY ignoreVolumeWithScaleChanged)

public:
    explicit SettingsBrew(QObject* parent = nullptr);

    // Espresso
    double espressoTemperature() const;
    void setEspressoTemperature(double temp);

    double targetWeight() const;
    void setTargetWeight(double weight);

    double lastUsedRatio() const;
    void setLastUsedRatio(double ratio);

    // Steam
    double steamTemperature() const;
    void setSteamTemperature(double temp);

    int steamTimeout() const;
    void setSteamTimeout(int timeout);

    int steamFlow() const;
    void setSteamFlow(int flow);

    bool steamDisabled() const;
    void setSteamDisabled(bool disabled);

    bool keepSteamHeaterOn() const;
    void setKeepSteamHeaterOn(bool keep);

    int steamAutoFlushSeconds() const;
    void setSteamAutoFlushSeconds(int seconds);

    // Steam pitcher presets
    QVariantList steamPitcherPresets() const;
    int selectedSteamPitcher() const;
    void setSelectedSteamCup(int index);

    Q_INVOKABLE void addSteamPitcherPreset(const QString& name, int duration, int flow);
    Q_INVOKABLE void addSteamPitcherPresetDisabled(const QString& name);
    Q_INVOKABLE void updateSteamPitcherPreset(int index, const QString& name, int duration, int flow);
    Q_INVOKABLE void removeSteamPitcherPreset(int index);
    Q_INVOKABLE void moveSteamPitcherPreset(int from, int to);
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;
    Q_INVOKABLE void setSteamPitcherWeight(int index, double weightG);

    // Hot water
    double waterTemperature() const;
    void setWaterTemperature(double temp);

    int waterVolume() const;
    void setWaterVolume(int volume);

    QString waterVolumeMode() const;  // "weight" or "volume"
    void setWaterVolumeMode(const QString& mode);

    // Hot water volume byte to send in DE1 ShotSettings. In "weight" mode the
    // app stops hot water via the scale and sends 0 so the DE1 flowmeter
    // auto-stop is disabled; in "volume" mode it returns the clamped
    // waterVolume(). All setShotSettings() call sites must use this to keep
    // payloads consistent — otherwise the ShotSettings drift detector trips
    // on BLE echo reordering.
    int effectiveHotWaterVolume() const;

    double hotWaterSawOffset() const;
    void setHotWaterSawOffset(double offset);
    int hotWaterSawSampleCount() const;
    void setHotWaterSawSampleCount(int count);

    // Hot water vessel presets
    QVariantList waterVesselPresets() const;
    int selectedWaterVessel() const;
    void setSelectedWaterCup(int index);

    Q_INVOKABLE void addWaterVesselPreset(const QString& name, int volume, const QString& mode = "weight", int flowRate = 40);
    Q_INVOKABLE void updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode = "weight", int flowRate = 40);
    Q_INVOKABLE void removeWaterVesselPreset(int index);
    Q_INVOKABLE void moveWaterVesselPreset(int from, int to);
    Q_INVOKABLE QVariantMap getWaterVesselPreset(int index) const;

    // Flush
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

    // Temperature override (persistent)
    double temperatureOverride() const;
    void setTemperatureOverride(double temp);
    bool hasTemperatureOverride() const;
    Q_INVOKABLE void clearTemperatureOverride();

    // Brew parameter overrides (persistent)
    double brewYieldOverride() const;
    void setBrewYieldOverride(double yield);
    bool hasBrewYieldOverride() const;
    Q_INVOKABLE void clearAllBrewOverrides();

    // Stop-at-volume gating
    bool ignoreVolumeWithScale() const;
    void setIgnoreVolumeWithScale(bool enabled);

signals:
    void espressoTemperatureChanged();
    void targetWeightChanged();
    void lastUsedRatioChanged();
    void steamTemperatureChanged();
    void steamTimeoutChanged();
    void steamFlowChanged();
    void steamDisabledChanged();
    void keepSteamHeaterOnChanged();
    void steamAutoFlushSecondsChanged();
    void steamPitcherPresetsChanged();
    void selectedSteamPitcherChanged();
    void waterTemperatureChanged();
    void waterVolumeChanged();
    void waterVolumeModeChanged();
    void hotWaterSawOffsetChanged();
    void waterVesselPresetsChanged();
    void selectedWaterVesselChanged();
    void flushPresetsChanged();
    void selectedFlushPresetChanged();
    void flushFlowChanged();
    void flushSecondsChanged();
    void temperatureOverrideChanged();
    void brewOverridesChanged();
    void ignoreVolumeWithScaleChanged();

private:
    mutable QSettings m_settings;

    // Session-only steam-disable flag (used during descaling)
    bool m_steamDisabled = false;

    // Persistent overrides — backing fields cached so getters don't hit QSettings.
    double m_temperatureOverride = 0.0;
    bool m_hasTemperatureOverride = false;
    double m_brewYieldOverride = 0.0;
    bool m_hasBrewYieldOverride = false;
};
