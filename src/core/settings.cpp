#include "settings.h"
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QFile>
#include <QUrl>
#include <QtMath>
#include <QColor>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif

Settings::Settings(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
    // Initialize default pitcher presets if none exist
    if (!m_settings.contains("steam/pitcherPresets")) {
        QJsonArray defaultPresets;
        QJsonObject small;
        small["name"] = "Small";
        small["duration"] = 30;
        small["flow"] = 150;  // 1.5 ml/s
        defaultPresets.append(small);

        QJsonObject large;
        large["name"] = "Large";
        large["duration"] = 60;
        large["flow"] = 150;  // 1.5 ml/s
        defaultPresets.append(large);

        m_settings.setValue("steam/pitcherPresets", QJsonDocument(defaultPresets).toJson());
    }

    // Initialize default favorite profiles if none exist
    if (!m_settings.contains("profile/favorites")) {
        QJsonArray defaultFavorites;

        QJsonObject adaptive;
        adaptive["name"] = "Adaptive v2";
        adaptive["filename"] = "adaptive_v2";
        defaultFavorites.append(adaptive);

        QJsonObject blooming;
        blooming["name"] = "Blooming Espresso";
        blooming["filename"] = "blooming_espresso";
        defaultFavorites.append(blooming);

        m_settings.setValue("profile/favorites", QJsonDocument(defaultFavorites).toJson());
    }

    // Initialize default selected built-in profiles if none exist
    if (!m_settings.contains("profile/selectedBuiltIns")) {
        QStringList defaultSelected;
        defaultSelected << "adaptive_v2"
                        << "blooming_espresso"
                        << "best_overall_pressure_profile"
                        << "flow_profile_for_straight_espresso"
                        << "turbo_shot";
        m_settings.setValue("profile/selectedBuiltIns", defaultSelected);
    }

    // Initialize default water vessel presets if none exist
    if (!m_settings.contains("water/vesselPresets")) {
        QJsonArray defaultPresets;

        QJsonObject vessel;
        vessel["name"] = "Cup";
        vessel["volume"] = 200;
        defaultPresets.append(vessel);

        QJsonObject mug;
        mug["name"] = "Mug";
        mug["volume"] = 350;
        defaultPresets.append(mug);

        m_settings.setValue("water/vesselPresets", QJsonDocument(defaultPresets).toJson());
    }

    // Initialize default flush presets if none exist
    if (!m_settings.contains("flush/presets")) {
        QJsonArray defaultPresets;

        QJsonObject quick;
        quick["name"] = "Quick";
        quick["flow"] = 6.0;
        quick["seconds"] = 3.0;
        defaultPresets.append(quick);

        QJsonObject normal;
        normal["name"] = "Normal";
        normal["flow"] = 6.0;
        normal["seconds"] = 5.0;
        defaultPresets.append(normal);

        QJsonObject thorough;
        thorough["name"] = "Thorough";
        thorough["flow"] = 6.0;
        thorough["seconds"] = 10.0;
        defaultPresets.append(thorough);

        m_settings.setValue("flush/presets", QJsonDocument(defaultPresets).toJson());
    }
}

// Machine settings
QString Settings::machineAddress() const {
    return m_settings.value("machine/address", "").toString();
}

void Settings::setMachineAddress(const QString& address) {
    if (machineAddress() != address) {
        m_settings.setValue("machine/address", address);
        emit machineAddressChanged();
    }
}

QString Settings::scaleAddress() const {
    return m_settings.value("scale/address", "").toString();
}

void Settings::setScaleAddress(const QString& address) {
    if (scaleAddress() != address) {
        m_settings.setValue("scale/address", address);
        emit scaleAddressChanged();
    }
}

QString Settings::scaleType() const {
    return m_settings.value("scale/type", "decent").toString();
}

void Settings::setScaleType(const QString& type) {
    if (scaleType() != type) {
        m_settings.setValue("scale/type", type);
        emit scaleTypeChanged();
    }
}

// Flow sensor calibration
double Settings::flowCalibrationFactor() const {
    return m_settings.value("flow/calibrationFactor", 1.29).toDouble();
}

void Settings::setFlowCalibrationFactor(double factor) {
    if (flowCalibrationFactor() != factor) {
        m_settings.setValue("flow/calibrationFactor", factor);
        emit flowCalibrationFactorChanged();
    }
}

// Espresso settings
double Settings::espressoTemperature() const {
    return m_settings.value("espresso/temperature", 93.0).toDouble();
}

void Settings::setEspressoTemperature(double temp) {
    if (espressoTemperature() != temp) {
        m_settings.setValue("espresso/temperature", temp);
        emit espressoTemperatureChanged();
    }
}

double Settings::targetWeight() const {
    return m_settings.value("espresso/targetWeight", 36.0).toDouble();
}

void Settings::setTargetWeight(double weight) {
    if (targetWeight() != weight) {
        m_settings.setValue("espresso/targetWeight", weight);
        emit targetWeightChanged();
    }
}

// Steam settings
double Settings::steamTemperature() const {
    return m_settings.value("steam/temperature", 160.0).toDouble();
}

void Settings::setSteamTemperature(double temp) {
    if (steamTemperature() != temp) {
        m_settings.setValue("steam/temperature", temp);
        emit steamTemperatureChanged();
    }
}

int Settings::steamTimeout() const {
    return m_settings.value("steam/timeout", 120).toInt();
}

void Settings::setSteamTimeout(int timeout) {
    if (steamTimeout() != timeout) {
        m_settings.setValue("steam/timeout", timeout);
        emit steamTimeoutChanged();
    }
}

int Settings::steamFlow() const {
    return m_settings.value("steam/flow", 150).toInt();  // 150 = 1.5 ml/s (range: 40-250)
}

void Settings::setSteamFlow(int flow) {
    if (steamFlow() != flow) {
        m_settings.setValue("steam/flow", flow);
        emit steamFlowChanged();
    }
}

bool Settings::steamDisabled() const {
    return m_steamDisabled;
}

void Settings::setSteamDisabled(bool disabled) {
    if (m_steamDisabled != disabled) {
        m_steamDisabled = disabled;
        emit steamDisabledChanged();
    }
}

bool Settings::keepSteamHeaterOn() const {
    return m_settings.value("steam/keepHeaterOn", false).toBool();
}

void Settings::setKeepSteamHeaterOn(bool keep) {
    if (keepSteamHeaterOn() != keep) {
        m_settings.setValue("steam/keepHeaterOn", keep);
        emit keepSteamHeaterOnChanged();
    }
}

// Steam pitcher presets
QVariantList Settings::steamPitcherPresets() const {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedSteamPitcher() const {
    return m_settings.value("steam/selectedPitcher", 0).toInt();
}

void Settings::setSelectedSteamCup(int index) {
    if (selectedSteamPitcher() != index) {
        m_settings.setValue("steam/selectedPitcher", index);
        emit selectedSteamPitcherChanged();
    }
}

void Settings::addSteamPitcherPreset(const QString& name, int duration, int flow) {
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

void Settings::updateSteamPitcherPreset(int index, const QString& name, int duration, int flow) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["duration"] = duration;
        preset["flow"] = flow;
        arr[index] = preset;

        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
        emit steamPitcherPresetsChanged();
    }
}

void Settings::removeSteamPitcherPreset(int index) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());

        // Adjust selected preset if needed
        int selected = selectedSteamPitcher();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedSteamCup(arr.size() - 1);
        }

        emit steamPitcherPresetsChanged();
    }
}

void Settings::moveSteamPitcherPreset(int from, int to) {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());

        // Update selected preset to follow the moved item if it was selected
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

QVariantMap Settings::getSteamPitcherPreset(int index) const {
    QByteArray data = m_settings.value("steam/pitcherPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Profile favorites
QVariantList Settings::favoriteProfiles() const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedFavoriteProfile() const {
    return m_settings.value("profile/selectedFavorite", 0).toInt();
}

void Settings::setSelectedFavoriteProfile(int index) {
    if (selectedFavoriteProfile() != index) {
        m_settings.setValue("profile/selectedFavorite", index);
        emit selectedFavoriteProfileChanged();
    }
}

void Settings::addFavoriteProfile(const QString& name, const QString& filename) {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    // Max 50 favorites
    if (arr.size() >= 50) {
        return;
    }

    // Don't add duplicates
    for (const QJsonValue& v : arr) {
        if (v.toObject()["filename"].toString() == filename) {
            return;
        }
    }

    QJsonObject favorite;
    favorite["name"] = name;
    favorite["filename"] = filename;
    arr.append(favorite);

    m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());
    emit favoriteProfilesChanged();
}

void Settings::removeFavoriteProfile(int index) {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());

        // Adjust selected if needed
        int selected = selectedFavoriteProfile();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedFavoriteProfile(arr.size() - 1);
        } else if (arr.size() == 0) {
            setSelectedFavoriteProfile(0);
        }

        emit favoriteProfilesChanged();
    }
}

void Settings::moveFavoriteProfile(int from, int to) {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());

        // Update selection to follow the moved item if it was selected
        int selected = selectedFavoriteProfile();
        if (selected == from) {
            setSelectedFavoriteProfile(to);
        } else if (from < selected && to >= selected) {
            setSelectedFavoriteProfile(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedFavoriteProfile(selected + 1);
        }

        emit favoriteProfilesChanged();
    }
}

QVariantMap Settings::getFavoriteProfile(int index) const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

bool Settings::isFavoriteProfile(const QString& filename) const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    for (const QJsonValue& v : arr) {
        if (v.toObject()["filename"].toString() == filename) {
            return true;
        }
    }
    return false;
}

bool Settings::updateFavoriteProfile(const QString& oldFilename, const QString& newFilename, const QString& newTitle) {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["filename"].toString() == oldFilename) {
            obj["filename"] = newFilename;
            obj["name"] = newTitle;
            arr[i] = obj;
            m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());
            emit favoriteProfilesChanged();
            return true;
        }
    }
    return false;
}

// Selected built-in profiles
QStringList Settings::selectedBuiltInProfiles() const {
    return m_settings.value("profile/selectedBuiltIns").toStringList();
}

void Settings::setSelectedBuiltInProfiles(const QStringList& profiles) {
    if (selectedBuiltInProfiles() != profiles) {
        m_settings.setValue("profile/selectedBuiltIns", profiles);
        emit selectedBuiltInProfilesChanged();
    }
}

void Settings::addSelectedBuiltInProfile(const QString& filename) {
    QStringList current = selectedBuiltInProfiles();
    if (!current.contains(filename)) {
        current.append(filename);
        m_settings.setValue("profile/selectedBuiltIns", current);
        emit selectedBuiltInProfilesChanged();
    }
}

void Settings::removeSelectedBuiltInProfile(const QString& filename) {
    QStringList current = selectedBuiltInProfiles();
    if (current.removeAll(filename) > 0) {
        m_settings.setValue("profile/selectedBuiltIns", current);
        emit selectedBuiltInProfilesChanged();

        // Also remove from favorites if it was a favorite
        if (isFavoriteProfile(filename)) {
            QByteArray data = m_settings.value("profile/favorites").toByteArray();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonArray arr = doc.array();

            for (int i = arr.size() - 1; i >= 0; --i) {
                if (arr[i].toObject()["filename"].toString() == filename) {
                    arr.removeAt(i);
                    break;
                }
            }

            m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());

            // Adjust selected favorite if needed
            int selected = selectedFavoriteProfile();
            if (selected >= arr.size() && arr.size() > 0) {
                setSelectedFavoriteProfile(arr.size() - 1);
            }

            emit favoriteProfilesChanged();
        }
    }
}

bool Settings::isSelectedBuiltInProfile(const QString& filename) const {
    return selectedBuiltInProfiles().contains(filename);
}

// Hot water settings
double Settings::waterTemperature() const {
    return m_settings.value("water/temperature", 85.0).toDouble();
}

void Settings::setWaterTemperature(double temp) {
    if (waterTemperature() != temp) {
        m_settings.setValue("water/temperature", temp);
        emit waterTemperatureChanged();
    }
}

int Settings::waterVolume() const {
    return m_settings.value("water/volume", 200).toInt();
}

void Settings::setWaterVolume(int volume) {
    if (waterVolume() != volume) {
        m_settings.setValue("water/volume", volume);
        emit waterVolumeChanged();
    }
}

// Hot water vessel presets
QVariantList Settings::waterVesselPresets() const {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedWaterVessel() const {
    return m_settings.value("water/selectedVessel", 0).toInt();
}

void Settings::setSelectedWaterCup(int index) {
    if (selectedWaterVessel() != index) {
        m_settings.setValue("water/selectedVessel", index);
        emit selectedWaterVesselChanged();
    }
}

void Settings::addWaterVesselPreset(const QString& name, int volume) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["volume"] = volume;
    arr.append(preset);

    m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
    emit waterVesselPresetsChanged();
}

void Settings::updateWaterVesselPreset(int index, const QString& name, int volume) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["volume"] = volume;
        arr[index] = preset;

        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
        emit waterVesselPresetsChanged();
    }
}

void Settings::removeWaterVesselPreset(int index) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());

        // Adjust selected preset if needed
        int selected = selectedWaterVessel();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedWaterCup(arr.size() - 1);
        }

        emit waterVesselPresetsChanged();
    }
}

void Settings::moveWaterVesselPreset(int from, int to) {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("water/vesselPresets", QJsonDocument(arr).toJson());

        // Update selected preset to follow the moved item if it was selected
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

QVariantMap Settings::getWaterVesselPreset(int index) const {
    QByteArray data = m_settings.value("water/vesselPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// Flush presets
QVariantList Settings::flushPresets() const {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedFlushPreset() const {
    return m_settings.value("flush/selectedPreset", 0).toInt();
}

void Settings::setSelectedFlushPreset(int index) {
    if (selectedFlushPreset() != index) {
        m_settings.setValue("flush/selectedPreset", index);
        emit selectedFlushPresetChanged();
    }
}

double Settings::flushFlow() const {
    return m_settings.value("flush/flow", 6.0).toDouble();
}

void Settings::setFlushFlow(double flow) {
    if (flushFlow() != flow) {
        m_settings.setValue("flush/flow", flow);
        emit flushFlowChanged();
    }
}

double Settings::flushSeconds() const {
    return m_settings.value("flush/seconds", 5.0).toDouble();
}

void Settings::setFlushSeconds(double seconds) {
    if (flushSeconds() != seconds) {
        m_settings.setValue("flush/seconds", seconds);
        emit flushSecondsChanged();
    }
}

void Settings::addFlushPreset(const QString& name, double flow, double seconds) {
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

void Settings::updateFlushPreset(int index, const QString& name, double flow, double seconds) {
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

void Settings::removeFlushPreset(int index) {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());

        // Adjust selected if needed
        int selected = selectedFlushPreset();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedFlushPreset(arr.size() - 1);
        }

        emit flushPresetsChanged();
    }
}

void Settings::moveFlushPreset(int from, int to) {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("flush/presets", QJsonDocument(arr).toJson());

        // Update selected to follow the moved item if it was selected
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

QVariantMap Settings::getFlushPreset(int index) const {
    QByteArray data = m_settings.value("flush/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

// UI settings
QString Settings::skin() const {
    return m_settings.value("ui/skin", "default").toString();
}

void Settings::setSkin(const QString& skin) {
    if (this->skin() != skin) {
        m_settings.setValue("ui/skin", skin);
        emit skinChanged();
    }
}

QString Settings::skinPath() const {
    // Look for skins in standard locations
    QStringList searchPaths = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/skins/" + skin(),
        ":/skins/" + skin(),
        "./skins/" + skin()
    };

    for (const QString& path : searchPaths) {
        if (QDir(path).exists()) {
            return path;
        }
    }

    // Default fallback
    return ":/skins/default";
}

QString Settings::currentProfile() const {
    return m_settings.value("profile/current", "default").toString();
}

void Settings::setCurrentProfile(const QString& profile) {
    if (currentProfile() != profile) {
        m_settings.setValue("profile/current", profile);
        emit currentProfileChanged();
    }
}

// Theme settings
QVariantMap Settings::customThemeColors() const {
    QByteArray data = m_settings.value("theme/customColors").toByteArray();
    if (data.isEmpty()) {
        return QVariantMap();
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.object().toVariantMap();
}

void Settings::setCustomThemeColors(const QVariantMap& colors) {
    QJsonObject obj = QJsonObject::fromVariantMap(colors);
    m_settings.setValue("theme/customColors", QJsonDocument(obj).toJson());
    emit customThemeColorsChanged();
}

QVariantList Settings::colorGroups() const {
    QByteArray data = m_settings.value("theme/colorGroups").toByteArray();
    if (data.isEmpty()) {
        return QVariantList();
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

void Settings::setColorGroups(const QVariantList& groups) {
    QJsonArray arr;
    for (const QVariant& v : groups) {
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    m_settings.setValue("theme/colorGroups", QJsonDocument(arr).toJson());
    emit colorGroupsChanged();
}

QString Settings::activeThemeName() const {
    return m_settings.value("theme/activeName", "Default").toString();
}

void Settings::setActiveThemeName(const QString& name) {
    if (activeThemeName() != name) {
        m_settings.setValue("theme/activeName", name);
        emit activeThemeNameChanged();
    }
}

double Settings::screenBrightness() const {
    return m_settings.value("theme/screenBrightness", 1.0).toDouble();
}

void Settings::setScreenBrightness(double brightness) {
    double clamped = qBound(0.0, brightness, 1.0);
    if (qAbs(screenBrightness() - clamped) > 0.001) {
        m_settings.setValue("theme/screenBrightness", clamped);

#ifdef Q_OS_ANDROID
        // Must run on Android UI thread
        float androidBrightness = (clamped < 0.01) ? 0.01f : static_cast<float>(clamped);
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread([androidBrightness]() {
            QJniObject activity = QNativeInterface::QAndroidApplication::context();
            if (activity.isValid()) {
                QJniObject window = activity.callObjectMethod(
                    "getWindow", "()Landroid/view/Window;");
                if (window.isValid()) {
                    QJniObject params = window.callObjectMethod(
                        "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
                    if (params.isValid()) {
                        params.setField<jfloat>("screenBrightness", androidBrightness);
                        window.callMethod<void>("setAttributes",
                            "(Landroid/view/WindowManager$LayoutParams;)V",
                            params.object());
                    }
                }
            }
        });
#endif

        emit screenBrightnessChanged();
    }
}

void Settings::setThemeColor(const QString& colorName, const QString& colorValue) {
    QVariantMap colors = customThemeColors();
    colors[colorName] = colorValue;
    setCustomThemeColors(colors);

    // Mark as custom theme when user edits any color
    if (activeThemeName() != "Custom") {
        setActiveThemeName("Custom");
    }
}

QString Settings::getThemeColor(const QString& colorName) const {
    QVariantMap colors = customThemeColors();
    return colors.value(colorName).toString();
}

void Settings::resetThemeToDefault() {
    m_settings.remove("theme/customColors");
    m_settings.remove("theme/colorGroups");
    setActiveThemeName("Default");
    emit customThemeColorsChanged();
    emit colorGroupsChanged();
}

QVariantList Settings::getPresetThemes() const {
    QVariantList themes;

    // Default theme (built-in, always first)
    QVariantMap defaultTheme;
    defaultTheme["name"] = "Default";
    defaultTheme["primaryColor"] = "#4e85f4";
    defaultTheme["isBuiltIn"] = true;
    themes.append(defaultTheme);

    // Load user-saved themes
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QJsonObject obj = val.toObject();
        QVariantMap theme;
        theme["name"] = obj["name"].toString();
        theme["primaryColor"] = obj["colors"].toObject()["primaryColor"].toString();
        theme["isBuiltIn"] = false;
        themes.append(theme);
    }

    return themes;
}

void Settings::applyPresetTheme(const QString& name) {
    QVariantMap palette;

    if (name == "Default") {
        // Exact Theme.qml defaults
        palette["backgroundColor"] = "#1a1a2e";
        palette["surfaceColor"] = "#252538";
        palette["primaryColor"] = "#4e85f4";
        palette["secondaryColor"] = "#c0c5e3";
        palette["textColor"] = "#ffffff";
        palette["textSecondaryColor"] = "#a0a8b8";
        palette["accentColor"] = "#e94560";
        palette["successColor"] = "#00ff88";
        palette["warningColor"] = "#ffaa00";
        palette["errorColor"] = "#ff4444";
        palette["borderColor"] = "#3a3a4e";
        palette["pressureColor"] = "#18c37e";
        palette["pressureGoalColor"] = "#69fdb3";
        palette["flowColor"] = "#4e85f4";
        palette["flowGoalColor"] = "#7aaaff";
        palette["temperatureColor"] = "#e73249";
        palette["temperatureGoalColor"] = "#ffa5a6";
        palette["weightColor"] = "#a2693d";

        setCustomThemeColors(palette);
        setActiveThemeName(name);
        return;
    }

    // Look for user theme
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QJsonObject obj = val.toObject();
        if (obj["name"].toString() == name) {
            QJsonObject colors = obj["colors"].toObject();
            for (const QString& key : colors.keys()) {
                palette[key] = colors[key].toString();
            }
            setCustomThemeColors(palette);
            setActiveThemeName(name);
            return;
        }
    }
}

void Settings::saveCurrentTheme(const QString& name) {
    if (name.isEmpty() || name == "Default") {
        return; // Can't save with empty name or overwrite Default
    }

    // Load existing user themes
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    // Remove existing theme with same name (if any)
    for (int i = userThemes.size() - 1; i >= 0; --i) {
        if (userThemes[i].toObject()["name"].toString() == name) {
            userThemes.removeAt(i);
        }
    }

    // Create new theme entry
    QJsonObject newTheme;
    newTheme["name"] = name;
    newTheme["colors"] = QJsonObject::fromVariantMap(customThemeColors());
    userThemes.append(newTheme);

    // Save to settings
    m_settings.setValue("theme/userThemes", QJsonDocument(userThemes).toJson(QJsonDocument::Compact));
    setActiveThemeName(name);
}

void Settings::deleteUserTheme(const QString& name) {
    if (name == "Default") {
        return; // Can't delete Default
    }

    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (int i = userThemes.size() - 1; i >= 0; --i) {
        if (userThemes[i].toObject()["name"].toString() == name) {
            userThemes.removeAt(i);
        }
    }

    m_settings.setValue("theme/userThemes", QJsonDocument(userThemes).toJson(QJsonDocument::Compact));

    // If we deleted the active theme, switch to Default
    if (activeThemeName() == name) {
        applyPresetTheme("Default");
    }
}

bool Settings::saveThemeToFile(const QString& filePath) {
    QString path = filePath;
    if (path.startsWith("file:///")) {
        path = QUrl(path).toLocalFile();
    }

    QJsonObject root;
    root["name"] = activeThemeName();
    root["colors"] = QJsonObject::fromVariantMap(customThemeColors());

    QJsonArray groupsArr;
    for (const QVariant& g : colorGroups()) {
        groupsArr.append(QJsonObject::fromVariantMap(g.toMap()));
    }
    root["groups"] = groupsArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
    return false;
}

bool Settings::loadThemeFromFile(const QString& filePath) {
    QString path = filePath;
    if (path.startsWith("file:///")) {
        path = QUrl(path).toLocalFile();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    if (root.contains("name")) {
        setActiveThemeName(root["name"].toString());
    }
    if (root.contains("colors")) {
        setCustomThemeColors(root["colors"].toObject().toVariantMap());
    }
    if (root.contains("groups")) {
        QVariantList groups;
        for (const QJsonValue& v : root["groups"].toArray()) {
            groups.append(v.toObject().toVariantMap());
        }
        setColorGroups(groups);
    }

    return true;
}

// Helper function to create HSL color string
static QString hslColor(double h, double s, double l) {
    // Normalize values
    h = fmod(h, 360.0);
    if (h < 0) h += 360.0;
    s = qBound(0.0, s, 100.0);
    l = qBound(0.0, l, 100.0);

    // Convert HSL to RGB
    double c = (1.0 - qAbs(2.0 * l / 100.0 - 1.0)) * s / 100.0;
    double x = c * (1.0 - qAbs(fmod(h / 60.0, 2.0) - 1.0));
    double m = l / 100.0 - c / 2.0;

    double r, g, b;
    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }

    int ri = qRound((r + m) * 255);
    int gi = qRound((g + m) * 255);
    int bi = qRound((b + m) * 255);

    return QString("#%1%2%3")
        .arg(ri, 2, 16, QChar('0'))
        .arg(gi, 2, 16, QChar('0'))
        .arg(bi, 2, 16, QChar('0'));
}

QVariantMap Settings::generatePalette(double baseHue, double baseSat, double baseLight) const {
    QVariantMap palette;

    // Use color harmony - different hues for different roles
    const double complementary = baseHue + 180.0;      // Opposite
    const double triadic1 = baseHue + 120.0;           // Triadic
    const double triadic2 = baseHue + 240.0;           // Triadic
    const double splitComp1 = baseHue + 150.0;         // Split-complementary
    const double splitComp2 = baseHue + 210.0;         // Split-complementary
    const double analogous1 = baseHue + 30.0;          // Analogous
    const double analogous2 = baseHue - 30.0;          // Analogous

    // Vibrant saturation range
    double sat = qBound(60.0, baseSat, 100.0);
    double light = qBound(45.0, baseLight, 65.0);

    // Core UI colors - use different harmonies for variety!
    palette["primaryColor"] = hslColor(baseHue, sat, light);
    palette["accentColor"] = hslColor(complementary, sat, light);
    palette["secondaryColor"] = hslColor(analogous1, sat * 0.7, 60.0);

    // Backgrounds - GO WILD! Any color, any brightness!
    double bgLight = 5.0 + fmod(baseHue, 60.0);  // 5-65% based on hue - could be dark OR bright!
    double surfLight = 10.0 + fmod(baseHue * 1.5, 50.0);  // 10-60%
    palette["backgroundColor"] = hslColor(complementary, 60.0 + fmod(baseSat, 30.0), bgLight);
    palette["surfaceColor"] = hslColor(triadic1, 55.0 + fmod(baseSat, 35.0), surfLight);
    palette["borderColor"] = hslColor(triadic2, 70.0, 40.0 + fmod(baseHue, 30.0));

    // Text - adaptive! Dark text on light bg, light text on dark bg
    double textLight = (bgLight > 40.0) ? 10.0 : 95.0;  // Dark or light based on background
    double textSecLight = (bgLight > 40.0) ? 25.0 : 70.0;
    palette["textColor"] = hslColor(analogous2, 15.0, textLight);
    palette["textSecondaryColor"] = hslColor(analogous1, 20.0, textSecLight);

    // Status colors - tinted versions of semantic colors
    palette["successColor"] = hslColor(140.0 + (baseHue * 0.1), 80.0, 50.0);
    palette["warningColor"] = hslColor(35.0 + (baseHue * 0.1), 90.0, 55.0);
    palette["errorColor"] = hslColor(fmod(360.0 + baseHue * 0.1, 360.0), 75.0, 55.0);

    // Chart colors - spread across the wheel using golden angle from different starting points
    const double goldenAngle = 137.5;
    palette["pressureColor"] = hslColor(triadic1 + goldenAngle * 0, 80.0, 55.0);
    palette["flowColor"] = hslColor(triadic2 + goldenAngle * 1, 80.0, 55.0);
    palette["temperatureColor"] = hslColor(complementary + goldenAngle * 2, 80.0, 55.0);
    palette["weightColor"] = hslColor(splitComp1 + goldenAngle * 3, 65.0, 50.0);

    // Goal variants - lighter, desaturated versions of chart colors
    palette["pressureGoalColor"] = hslColor(triadic1 + goldenAngle * 0, 55.0, 75.0);
    palette["flowGoalColor"] = hslColor(triadic2 + goldenAngle * 1, 55.0, 75.0);
    palette["temperatureGoalColor"] = hslColor(complementary + goldenAngle * 2, 55.0, 75.0);

    // Derived colors
    palette["focusColor"] = palette["primaryColor"];
    palette["shadowColor"] = "#40000000";

    return palette;
}

// Visualizer settings
QString Settings::visualizerUsername() const {
    return m_settings.value("visualizer/username", "").toString();
}

void Settings::setVisualizerUsername(const QString& username) {
    if (visualizerUsername() != username) {
        m_settings.setValue("visualizer/username", username);
        emit visualizerUsernameChanged();
    }
}

QString Settings::visualizerPassword() const {
    return m_settings.value("visualizer/password", "").toString();
}

void Settings::setVisualizerPassword(const QString& password) {
    if (visualizerPassword() != password) {
        m_settings.setValue("visualizer/password", password);
        emit visualizerPasswordChanged();
    }
}

bool Settings::visualizerAutoUpload() const {
    return m_settings.value("visualizer/autoUpload", true).toBool();
}

void Settings::setVisualizerAutoUpload(bool enabled) {
    if (visualizerAutoUpload() != enabled) {
        m_settings.setValue("visualizer/autoUpload", enabled);
        emit visualizerAutoUploadChanged();
    }
}

double Settings::visualizerMinDuration() const {
    return m_settings.value("visualizer/minDuration", 6.0).toDouble();
}

void Settings::setVisualizerMinDuration(double seconds) {
    if (visualizerMinDuration() != seconds) {
        m_settings.setValue("visualizer/minDuration", seconds);
        emit visualizerMinDurationChanged();
    }
}

bool Settings::visualizerExtendedMetadata() const {
    return m_settings.value("visualizer/extendedMetadata", false).toBool();
}

void Settings::setVisualizerExtendedMetadata(bool enabled) {
    if (visualizerExtendedMetadata() != enabled) {
        m_settings.setValue("visualizer/extendedMetadata", enabled);
        emit visualizerExtendedMetadataChanged();
    }
}

bool Settings::visualizerShowAfterShot() const {
    return m_settings.value("visualizer/showAfterShot", true).toBool();
}

void Settings::setVisualizerShowAfterShot(bool enabled) {
    if (visualizerShowAfterShot() != enabled) {
        m_settings.setValue("visualizer/showAfterShot", enabled);
        emit visualizerShowAfterShotChanged();
    }
}

// AI Dialing Assistant settings
QString Settings::aiProvider() const {
    return m_settings.value("ai/provider", "openai").toString();
}

void Settings::setAiProvider(const QString& provider) {
    if (aiProvider() != provider) {
        m_settings.setValue("ai/provider", provider);
        emit aiProviderChanged();
        emit valueChanged("ai/provider");
    }
}

QString Settings::openaiApiKey() const {
    return m_settings.value("ai/openaiKey", "").toString();
}

void Settings::setOpenaiApiKey(const QString& key) {
    if (openaiApiKey() != key) {
        m_settings.setValue("ai/openaiKey", key);
        emit openaiApiKeyChanged();
        emit valueChanged("ai/openaiKey");
    }
}

QString Settings::anthropicApiKey() const {
    return m_settings.value("ai/anthropicKey", "").toString();
}

void Settings::setAnthropicApiKey(const QString& key) {
    if (anthropicApiKey() != key) {
        m_settings.setValue("ai/anthropicKey", key);
        emit anthropicApiKeyChanged();
        emit valueChanged("ai/anthropicKey");
    }
}

QString Settings::geminiApiKey() const {
    return m_settings.value("ai/geminiKey", "").toString();
}

void Settings::setGeminiApiKey(const QString& key) {
    if (geminiApiKey() != key) {
        m_settings.setValue("ai/geminiKey", key);
        emit geminiApiKeyChanged();
        emit valueChanged("ai/geminiKey");
    }
}

QString Settings::ollamaEndpoint() const {
    return m_settings.value("ai/ollamaEndpoint", "http://localhost:11434").toString();
}

void Settings::setOllamaEndpoint(const QString& endpoint) {
    if (ollamaEndpoint() != endpoint) {
        m_settings.setValue("ai/ollamaEndpoint", endpoint);
        emit ollamaEndpointChanged();
        emit valueChanged("ai/ollamaEndpoint");
    }
}

QString Settings::ollamaModel() const {
    return m_settings.value("ai/ollamaModel", "").toString();
}

void Settings::setOllamaModel(const QString& model) {
    if (ollamaModel() != model) {
        m_settings.setValue("ai/ollamaModel", model);
        emit ollamaModelChanged();
        emit valueChanged("ai/ollamaModel");
    }
}

// Build info
bool Settings::isDebugBuild() const {
#ifdef QT_DEBUG
    return true;
#else
    return false;
#endif
}

// DYE metadata
QString Settings::dyeBeanBrand() const {
    return m_settings.value("dye/beanBrand", "").toString();
}

void Settings::setDyeBeanBrand(const QString& value) {
    if (dyeBeanBrand() != value) {
        m_settings.setValue("dye/beanBrand", value);
        emit dyeBeanBrandChanged();
    }
}

QString Settings::dyeBeanType() const {
    return m_settings.value("dye/beanType", "").toString();
}

void Settings::setDyeBeanType(const QString& value) {
    if (dyeBeanType() != value) {
        m_settings.setValue("dye/beanType", value);
        emit dyeBeanTypeChanged();
    }
}

QString Settings::dyeRoastDate() const {
    return m_settings.value("dye/roastDate", "").toString();
}

void Settings::setDyeRoastDate(const QString& value) {
    if (dyeRoastDate() != value) {
        m_settings.setValue("dye/roastDate", value);
        emit dyeRoastDateChanged();
    }
}

QString Settings::dyeRoastLevel() const {
    return m_settings.value("dye/roastLevel", "").toString();
}

void Settings::setDyeRoastLevel(const QString& value) {
    if (dyeRoastLevel() != value) {
        m_settings.setValue("dye/roastLevel", value);
        emit dyeRoastLevelChanged();
    }
}

QString Settings::dyeGrinderModel() const {
    return m_settings.value("dye/grinderModel", "").toString();
}

void Settings::setDyeGrinderModel(const QString& value) {
    if (dyeGrinderModel() != value) {
        m_settings.setValue("dye/grinderModel", value);
        emit dyeGrinderModelChanged();
    }
}

QString Settings::dyeGrinderSetting() const {
    return m_settings.value("dye/grinderSetting", "").toString();
}

void Settings::setDyeGrinderSetting(const QString& value) {
    if (dyeGrinderSetting() != value) {
        m_settings.setValue("dye/grinderSetting", value);
        emit dyeGrinderSettingChanged();
    }
}

double Settings::dyeBeanWeight() const {
    return m_settings.value("dye/beanWeight", 18.0).toDouble();
}

void Settings::setDyeBeanWeight(double value) {
    if (!qFuzzyCompare(1.0 + dyeBeanWeight(), 1.0 + value)) {
        m_settings.setValue("dye/beanWeight", value);
        emit dyeBeanWeightChanged();
    }
}

double Settings::dyeDrinkWeight() const {
    return m_settings.value("dye/drinkWeight", 36.0).toDouble();
}

void Settings::setDyeDrinkWeight(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkWeight(), 1.0 + value)) {
        m_settings.setValue("dye/drinkWeight", value);
        emit dyeDrinkWeightChanged();
    }
}

double Settings::dyeDrinkTds() const {
    return m_settings.value("dye/drinkTds", 0.0).toDouble();
}

void Settings::setDyeDrinkTds(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkTds(), 1.0 + value)) {
        m_settings.setValue("dye/drinkTds", value);
        emit dyeDrinkTdsChanged();
    }
}

double Settings::dyeDrinkEy() const {
    return m_settings.value("dye/drinkEy", 0.0).toDouble();
}

void Settings::setDyeDrinkEy(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkEy(), 1.0 + value)) {
        m_settings.setValue("dye/drinkEy", value);
        emit dyeDrinkEyChanged();
    }
}

int Settings::dyeEspressoEnjoyment() const {
    return m_settings.value("dye/espressoEnjoyment", 0).toInt();
}

void Settings::setDyeEspressoEnjoyment(int value) {
    if (dyeEspressoEnjoyment() != value) {
        m_settings.setValue("dye/espressoEnjoyment", value);
        emit dyeEspressoEnjoymentChanged();
    }
}

QString Settings::dyeEspressoNotes() const {
    return m_settings.value("dye/espressoNotes", "").toString();
}

void Settings::setDyeEspressoNotes(const QString& value) {
    if (dyeEspressoNotes() != value) {
        m_settings.setValue("dye/espressoNotes", value);
        emit dyeEspressoNotesChanged();
    }
}

QString Settings::dyeBarista() const {
    return m_settings.value("dye/barista", "").toString();
}

void Settings::setDyeBarista(const QString& value) {
    if (dyeBarista() != value) {
        m_settings.setValue("dye/barista", value);
        emit dyeBaristaChanged();
    }
}

QString Settings::dyeShotDateTime() const {
    return m_settings.value("dye/shotDateTime", "").toString();
}

void Settings::setDyeShotDateTime(const QString& value) {
    if (dyeShotDateTime() != value) {
        m_settings.setValue("dye/shotDateTime", value);
        emit dyeShotDateTimeChanged();
    }
}

// Water tank calibration
double Settings::waterLevelMinMm() const {
    // Default 5mm matches de1app's water_level_mm_correction (sensor offset)
    return m_settings.value("waterTank/minMm", 5.0).toDouble();
}

void Settings::setWaterLevelMinMm(double mm) {
    if (!qFuzzyCompare(1.0 + waterLevelMinMm(), 1.0 + mm)) {
        m_settings.setValue("waterTank/minMm", mm);
        emit waterLevelMinMmChanged();
    }
}

double Settings::waterLevelMaxMm() const {
    // Default 50mm is a reasonable starting point (~1400mL)
    // Will auto-calibrate higher when user fills tank
    return m_settings.value("waterTank/maxMm", 50.0).toDouble();
}

void Settings::setWaterLevelMaxMm(double mm) {
    if (!qFuzzyCompare(1.0 + waterLevelMaxMm(), 1.0 + mm)) {
        m_settings.setValue("waterTank/maxMm", mm);
        emit waterLevelMaxMmChanged();
    }
}

void Settings::resetWaterLevelCalibration() {
    m_settings.remove("waterTank/minMm");
    m_settings.remove("waterTank/maxMm");
    emit waterLevelMinMmChanged();
    emit waterLevelMaxMmChanged();
}

// Shot server settings
bool Settings::shotServerEnabled() const {
    return m_settings.value("shotServer/enabled", false).toBool();
}

void Settings::setShotServerEnabled(bool enabled) {
    if (shotServerEnabled() != enabled) {
        m_settings.setValue("shotServer/enabled", enabled);
        emit shotServerEnabledChanged();
    }
}

QString Settings::shotServerHostname() const {
    return m_settings.value("shotServer/hostname", "").toString();
}

void Settings::setShotServerHostname(const QString& hostname) {
    if (shotServerHostname() != hostname) {
        m_settings.setValue("shotServer/hostname", hostname);
        emit shotServerHostnameChanged();
    }
}

int Settings::shotServerPort() const {
    return m_settings.value("shotServer/port", 8888).toInt();
}

void Settings::setShotServerPort(int port) {
    if (shotServerPort() != port) {
        m_settings.setValue("shotServer/port", port);
        emit shotServerPortChanged();
    }
}

bool Settings::autoCheckUpdates() const {
    return m_settings.value("updates/autoCheck", true).toBool();
}

void Settings::setAutoCheckUpdates(bool enabled) {
    if (autoCheckUpdates() != enabled) {
        m_settings.setValue("updates/autoCheck", enabled);
        emit autoCheckUpdatesChanged();
    }
}

// Generic settings access
QVariant Settings::value(const QString& key, const QVariant& defaultValue) const {
    return m_settings.value(key, defaultValue);
}

void Settings::setValue(const QString& key, const QVariant& value) {
    m_settings.setValue(key, value);
    emit valueChanged(key);
}
