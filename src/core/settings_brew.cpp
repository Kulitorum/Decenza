#include "settings_brew.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

SettingsBrew::SettingsBrew(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
    // Seed default steam pitcher presets if none exist
    if (!m_settings.contains("steam/pitcherPresets")) {
        QJsonArray defaults;
        QJsonObject small;
        small["name"] = "Small";
        small["duration"] = 30;
        small["flow"] = 150;  // 1.5 ml/s
        defaults.append(small);

        QJsonObject large;
        large["name"] = "Large";
        large["duration"] = 60;
        large["flow"] = 150;  // 1.5 ml/s
        defaults.append(large);

        m_settings.setValue("steam/pitcherPresets", QJsonDocument(defaults).toJson());
    }

    // Seed default water vessel presets if none exist
    if (!m_settings.contains("water/vesselPresets")) {
        QJsonArray defaults;
        QJsonObject vessel;
        vessel["name"] = "Cup";
        vessel["volume"] = 200;
        defaults.append(vessel);

        QJsonObject mug;
        mug["name"] = "Mug";
        mug["volume"] = 350;
        defaults.append(mug);

        m_settings.setValue("water/vesselPresets", QJsonDocument(defaults).toJson());
    }

    // Seed default flush presets if none exist
    if (!m_settings.contains("flush/presets")) {
        QJsonArray defaults;
        QJsonObject quick;
        quick["name"] = "Quick";
        quick["flow"] = 6.0;
        quick["seconds"] = 3.0;
        defaults.append(quick);

        QJsonObject normal;
        normal["name"] = "Normal";
        normal["flow"] = 6.0;
        normal["seconds"] = 5.0;
        defaults.append(normal);

        QJsonObject thorough;
        thorough["name"] = "Thorough";
        thorough["flow"] = 6.0;
        thorough["seconds"] = 10.0;
        defaults.append(thorough);

        m_settings.setValue("flush/presets", QJsonDocument(defaults).toJson());
    }

    // Load persistent brew overrides into the cache (Settings used to do this).
    m_hasTemperatureOverride = m_settings.value("brew/hasTemperatureOverride", false).toBool();
    if (m_hasTemperatureOverride) {
        m_temperatureOverride = m_settings.value("brew/temperatureOverride", 0.0).toDouble();
    }

    m_hasBrewYieldOverride = m_settings.value("brew/hasBrewYieldOverride", false).toBool();
    if (m_hasBrewYieldOverride) {
        m_brewYieldOverride = m_settings.value("brew/brewYieldOverride", 0.0).toDouble();
    }
}

// Espresso

double SettingsBrew::espressoTemperature() const {
    return m_settings.value("espresso/temperature", 93.0).toDouble();
}

void SettingsBrew::setEspressoTemperature(double temp) {
    if (espressoTemperature() != temp) {
        m_settings.setValue("espresso/temperature", temp);
        emit espressoTemperatureChanged();
    }
}

double SettingsBrew::targetWeight() const {
    return m_settings.value("espresso/targetWeight", 36.0).toDouble();
}

void SettingsBrew::setTargetWeight(double weight) {
    if (targetWeight() != weight) {
        m_settings.setValue("espresso/targetWeight", weight);
        emit targetWeightChanged();
    }
}

double SettingsBrew::lastUsedRatio() const {
    return m_settings.value("espresso/lastUsedRatio", 2.0).toDouble();
}

void SettingsBrew::setLastUsedRatio(double ratio) {
    if (lastUsedRatio() != ratio) {
        m_settings.setValue("espresso/lastUsedRatio", ratio);
        emit lastUsedRatioChanged();
    }
}

// Steam

double SettingsBrew::steamTemperature() const {
    return m_settings.value("steam/temperature", 160.0).toDouble();
}

void SettingsBrew::setSteamTemperature(double temp) {
    if (steamTemperature() != temp) {
        m_settings.setValue("steam/temperature", temp);
        emit steamTemperatureChanged();
    }
}

int SettingsBrew::steamTimeout() const {
    return m_settings.value("steam/timeout", 120).toInt();
}

void SettingsBrew::setSteamTimeout(int timeout) {
    if (steamTimeout() != timeout) {
        m_settings.setValue("steam/timeout", timeout);
        emit steamTimeoutChanged();
    }
}

int SettingsBrew::steamFlow() const {
    return m_settings.value("steam/flow", 150).toInt();  // 150 = 1.5 ml/s (range: 40-250)
}

void SettingsBrew::setSteamFlow(int flow) {
    if (steamFlow() != flow) {
        m_settings.setValue("steam/flow", flow);
        emit steamFlowChanged();
    }
}

bool SettingsBrew::steamDisabled() const {
    return m_steamDisabled;
}

void SettingsBrew::setSteamDisabled(bool disabled) {
    if (m_steamDisabled != disabled) {
        m_steamDisabled = disabled;
        emit steamDisabledChanged();
    }
}

bool SettingsBrew::keepSteamHeaterOn() const {
    return m_settings.value("steam/keepHeaterOn", true).toBool();
}

void SettingsBrew::setKeepSteamHeaterOn(bool keep) {
    if (keepSteamHeaterOn() != keep) {
        m_settings.setValue("steam/keepHeaterOn", keep);
        emit keepSteamHeaterOnChanged();
    }
}

int SettingsBrew::steamAutoFlushSeconds() const {
    return m_settings.value("steam/autoFlushSeconds", 0).toInt();
}

void SettingsBrew::setSteamAutoFlushSeconds(int seconds) {
    if (steamAutoFlushSeconds() != seconds) {
        m_settings.setValue("steam/autoFlushSeconds", seconds);
        emit steamAutoFlushSecondsChanged();
    }
}

// Steam pitcher presets

QVariantList SettingsBrew::steamPitcherPresets() const {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int SettingsBrew::selectedSteamPitcher() const {
    return m_settings.value("steam/selectedPitcher", 0).toInt();
}

void SettingsBrew::setSelectedSteamCup(int index) {
    if (selectedSteamPitcher() != index) {
        m_settings.setValue("steam/selectedPitcher", index);
        emit selectedSteamPitcherChanged();
    }
}

void SettingsBrew::addSteamPitcherPreset(const QString& name, int duration, int flow) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["duration"] = duration;
    preset["flow"] = flow;
    arr.append(preset);

    m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
    emit steamPitcherPresetsChanged();
}

void SettingsBrew::addSteamPitcherPresetDisabled(const QString& name) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["disabled"] = true;
    arr.append(preset);

    m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
    emit steamPitcherPresetsChanged();
}

void SettingsBrew::updateSteamPitcherPreset(int index, const QString& name, int duration, int flow) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();  // Read existing to preserve pitcherWeightG
        preset["name"] = name;
        preset["duration"] = duration;
        preset["flow"] = flow;
        arr[index] = preset;

        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::removeSteamPitcherPreset(int index) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());

        int selected = selectedSteamPitcher();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedSteamCup(static_cast<int>(arr.size()) - 1);
        }

        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::moveSteamPitcherPreset(int from, int to) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());

        int selected = selectedSteamPitcher();
        if (selected == from) {
            setSelectedSteamCup(to);
        } else if (from < selected && to >= selected) {
            setSelectedSteamCup(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedSteamCup(selected + 1);
        }

        emit steamPitcherPresetsChanged();
    }
}

void SettingsBrew::setSteamPitcherWeight(int index, double weightG) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();
        preset["pitcherWeightG"] = weightG;
        arr[index] = preset;
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
        emit steamPitcherPresetsChanged();
    }
}

QVariantMap SettingsBrew::getSteamPitcherPreset(int index) const {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Hot water

double SettingsBrew::waterTemperature() const {
    return m_settings.value("water/temperature", 85.0).toDouble();
}

void SettingsBrew::setWaterTemperature(double temp) {
    if (waterTemperature() != temp) {
        m_settings.setValue("water/temperature", temp);
        emit waterTemperatureChanged();
    }
}

int SettingsBrew::waterVolume() const {
    return m_settings.value("water/volume", 200).toInt();
}

void SettingsBrew::setWaterVolume(int volume) {
    if (waterVolume() != volume) {
        m_settings.setValue("water/volume", volume);
        emit waterVolumeChanged();
    }
}

QString SettingsBrew::waterVolumeMode() const {
    return m_settings.value("water/volumeMode", "weight").toString();
}

void SettingsBrew::setWaterVolumeMode(const QString& mode) {
    if (waterVolumeMode() != mode) {
        m_settings.setValue("water/volumeMode", mode);
        emit waterVolumeModeChanged();
    }
}

int SettingsBrew::effectiveHotWaterVolume() const {
    if (waterVolumeMode() != "volume") return 0;
    return qBound(0, waterVolume(), 255);  // BLE uint8 range
}

double SettingsBrew::hotWaterSawOffset() const {
    return m_settings.value("water/sawOffset", 2.0).toDouble();
}

void SettingsBrew::setHotWaterSawOffset(double offset) {
    if (!qFuzzyCompare(hotWaterSawOffset(), offset)) {
        m_settings.setValue("water/sawOffset", offset);
        emit hotWaterSawOffsetChanged();
    }
}

int SettingsBrew::hotWaterSawSampleCount() const {
    return m_settings.value("water/sawSampleCount", 0).toInt();
}

void SettingsBrew::setHotWaterSawSampleCount(int count) {
    m_settings.setValue("water/sawSampleCount", count);
}

// Hot water vessel presets

QVariantList SettingsBrew::waterVesselPresets() const {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int SettingsBrew::selectedWaterVessel() const {
    return m_settings.value("water/selectedVessel", 0).toInt();
}

void SettingsBrew::setSelectedWaterCup(int index) {
    if (selectedWaterVessel() != index) {
        m_settings.setValue("water/selectedVessel", index);
        emit selectedWaterVesselChanged();
    }
}

void SettingsBrew::addWaterVesselPreset(const QString& name, int volume, const QString& mode, int flowRate) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["volume"] = volume;
    preset["mode"] = mode;
    preset["flowRate"] = flowRate;
    arr.append(preset);

    m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
    emit waterVesselPresetsChanged();
}

void SettingsBrew::updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode, int flowRate) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["volume"] = volume;
        preset["mode"] = mode;
        preset["flowRate"] = flowRate;
        arr[index] = preset;

        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
        emit waterVesselPresetsChanged();
    }
}

void SettingsBrew::removeWaterVesselPreset(int index) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());

        int selected = selectedWaterVessel();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedWaterCup(static_cast<int>(arr.size()) - 1);
        }

        emit waterVesselPresetsChanged();
    }
}

void SettingsBrew::moveWaterVesselPreset(int from, int to) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());

        int selected = selectedWaterVessel();
        if (selected == from) {
            setSelectedWaterCup(to);
        } else if (from < selected && to >= selected) {
            setSelectedWaterCup(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedWaterCup(selected + 1);
        }

        emit waterVesselPresetsChanged();
    }
}

QVariantMap SettingsBrew::getWaterVesselPreset(int index) const {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Flush

QVariantList SettingsBrew::flushPresets() const {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int SettingsBrew::selectedFlushPreset() const {
    return m_settings.value("flush/selectedPreset", 0).toInt();
}

void SettingsBrew::setSelectedFlushPreset(int index) {
    if (selectedFlushPreset() != index) {
        m_settings.setValue("flush/selectedPreset", index);
        emit selectedFlushPresetChanged();
    }
}

double SettingsBrew::flushFlow() const {
    return m_settings.value("flush/flow", 6.0).toDouble();
}

void SettingsBrew::setFlushFlow(double flow) {
    if (flushFlow() != flow) {
        m_settings.setValue("flush/flow", flow);
        emit flushFlowChanged();
    }
}

double SettingsBrew::flushSeconds() const {
    return m_settings.value("flush/seconds", 5.0).toDouble();
}

void SettingsBrew::setFlushSeconds(double seconds) {
    if (flushSeconds() != seconds) {
        m_settings.setValue("flush/seconds", seconds);
        emit flushSecondsChanged();
    }
}

void SettingsBrew::addFlushPreset(const QString& name, double flow, double seconds) {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["flow"] = flow;
    preset["seconds"] = seconds;
    arr.append(preset);

    m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());
    emit flushPresetsChanged();
}

void SettingsBrew::updateFlushPreset(int index, const QString& name, double flow, double seconds) {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["flow"] = flow;
        preset["seconds"] = seconds;
        arr[index] = preset;

        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());
        emit flushPresetsChanged();
    }
}

void SettingsBrew::removeFlushPreset(int index) {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());

        int selected = selectedFlushPreset();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedFlushPreset(static_cast<int>(arr.size()) - 1);
        }

        emit flushPresetsChanged();
    }
}

void SettingsBrew::moveFlushPreset(int from, int to) {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());

        int selected = selectedFlushPreset();
        if (selected == from) {
            setSelectedFlushPreset(to);
        } else if (from < selected && to >= selected) {
            setSelectedFlushPreset(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedFlushPreset(selected + 1);
        }

        emit flushPresetsChanged();
    }
}

QVariantMap SettingsBrew::getFlushPreset(int index) const {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Temperature override (persistent)

double SettingsBrew::temperatureOverride() const {
    return m_temperatureOverride;
}

void SettingsBrew::setTemperatureOverride(double temp) {
    if (!qFuzzyCompare(m_temperatureOverride, temp) || !m_hasTemperatureOverride) {
        qDebug() << "setTemperatureOverride:" << m_temperatureOverride << "→" << temp;
        m_temperatureOverride = temp;
        m_hasTemperatureOverride = true;
        m_settings.setValue("brew/temperatureOverride", temp);
        m_settings.setValue("brew/hasTemperatureOverride", true);
        emit temperatureOverrideChanged();
    }
}

bool SettingsBrew::hasTemperatureOverride() const {
    return m_hasTemperatureOverride;
}

void SettingsBrew::clearTemperatureOverride() {
    if (m_hasTemperatureOverride || !qFuzzyIsNull(m_temperatureOverride)) {
        m_hasTemperatureOverride = false;
        m_temperatureOverride = 0.0;
        m_settings.remove("brew/temperatureOverride");
        m_settings.remove("brew/hasTemperatureOverride");
        emit temperatureOverrideChanged();
    }
}

// Brew yield override (persistent)

double SettingsBrew::brewYieldOverride() const {
    return m_brewYieldOverride;
}

void SettingsBrew::setBrewYieldOverride(double yield) {
    bool changed = false;
    if (yield <= 0) {
        if (m_hasBrewYieldOverride || !qFuzzyIsNull(m_brewYieldOverride)) {
            m_brewYieldOverride = 0;
            m_hasBrewYieldOverride = false;
            m_settings.remove("brew/brewYieldOverride");
            m_settings.remove("brew/hasBrewYieldOverride");
            changed = true;
        }
    } else {
        if (!qFuzzyCompare(1.0 + m_brewYieldOverride, 1.0 + yield) || !m_hasBrewYieldOverride) {
            m_brewYieldOverride = yield;
            m_hasBrewYieldOverride = true;
            m_settings.setValue("brew/brewYieldOverride", yield);
            m_settings.setValue("brew/hasBrewYieldOverride", true);
            changed = true;
        }
    }
    if (changed) {
        emit brewOverridesChanged();
    }
}

bool SettingsBrew::hasBrewYieldOverride() const {
    return m_hasBrewYieldOverride;
}

void SettingsBrew::clearAllBrewOverrides() {
    bool changed = false;

    if (m_hasBrewYieldOverride || !qFuzzyIsNull(m_brewYieldOverride)) {
        m_brewYieldOverride = 0.0;
        m_hasBrewYieldOverride = false;
        m_settings.remove("brew/brewYieldOverride");
        m_settings.remove("brew/hasBrewYieldOverride");
        changed = true;
    }

    bool tempChanged = false;
    if (m_hasTemperatureOverride || !qFuzzyIsNull(m_temperatureOverride)) {
        m_temperatureOverride = 0.0;
        m_hasTemperatureOverride = false;
        m_settings.remove("brew/temperatureOverride");
        m_settings.remove("brew/hasTemperatureOverride");
        changed = true;
        tempChanged = true;
    }

    if (changed) {
        emit brewOverridesChanged();
    }
    if (tempChanged) {
        emit temperatureOverrideChanged();
    }
}

// Stop-at-volume gating

bool SettingsBrew::ignoreVolumeWithScale() const {
    return m_settings.value("espresso/ignoreVolumeWithScale", false).toBool();
}

void SettingsBrew::setIgnoreVolumeWithScale(bool enabled) {
    if (ignoreVolumeWithScale() != enabled) {
        m_settings.setValue("espresso/ignoreVolumeWithScale", enabled);
        emit ignoreVolumeWithScaleChanged();
    }
}
