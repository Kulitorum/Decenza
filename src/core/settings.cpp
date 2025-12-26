#include "settings.h"
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>

Settings::Settings(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
{
    // Initialize default cup presets if none exist
    if (!m_settings.contains("steam/cupPresets")) {
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

        m_settings.setValue("steam/cupPresets", QJsonDocument(defaultPresets).toJson());
    }

    // Initialize default favorite profiles if none exist
    if (!m_settings.contains("profile/favorites")) {
        QJsonArray defaultFavorites;

        QJsonObject adaptive;
        adaptive["name"] = "Adaptive v2";
        adaptive["filename"] = "Adaptive v2";
        defaultFavorites.append(adaptive);

        QJsonObject blooming;
        blooming["name"] = "blooming espresso";
        blooming["filename"] = "blooming espresso";
        defaultFavorites.append(blooming);

        m_settings.setValue("profile/favorites", QJsonDocument(defaultFavorites).toJson());
    }

    // Initialize default water cup presets if none exist
    if (!m_settings.contains("water/cupPresets")) {
        QJsonArray defaultPresets;

        QJsonObject cup;
        cup["name"] = "Cup";
        cup["volume"] = 200;
        defaultPresets.append(cup);

        QJsonObject mug;
        mug["name"] = "Mug";
        mug["volume"] = 350;
        defaultPresets.append(mug);

        m_settings.setValue("water/cupPresets", QJsonDocument(defaultPresets).toJson());
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

// Steam cup presets
QVariantList Settings::steamCupPresets() const {
    QByteArray data = m_settings.value("steam/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedSteamCup() const {
    return m_settings.value("steam/selectedCup", 0).toInt();
}

void Settings::setSelectedSteamCup(int index) {
    if (selectedSteamCup() != index) {
        m_settings.setValue("steam/selectedCup", index);
        emit selectedSteamCupChanged();
    }
}

void Settings::addSteamCupPreset(const QString& name, int duration, int flow) {
    QByteArray data = m_settings.value("steam/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["duration"] = duration;
    preset["flow"] = flow;
    arr.append(preset);

    m_settings.setValue("steam/cupPresets", QJsonDocument(arr).toJson());
    emit steamCupPresetsChanged();
}

void Settings::updateSteamCupPreset(int index, const QString& name, int duration, int flow) {
    QByteArray data = m_settings.value("steam/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["duration"] = duration;
        preset["flow"] = flow;
        arr[index] = preset;

        m_settings.setValue("steam/cupPresets", QJsonDocument(arr).toJson());
        emit steamCupPresetsChanged();
    }
}

void Settings::removeSteamCupPreset(int index) {
    QByteArray data = m_settings.value("steam/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("steam/cupPresets", QJsonDocument(arr).toJson());

        // Adjust selected cup if needed
        int selected = selectedSteamCup();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedSteamCup(arr.size() - 1);
        }

        emit steamCupPresetsChanged();
    }
}

void Settings::moveSteamCupPreset(int from, int to) {
    QByteArray data = m_settings.value("steam/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("steam/cupPresets", QJsonDocument(arr).toJson());

        // Update selected cup to follow the moved item if it was selected
        int selected = selectedSteamCup();
        if (selected == from) {
            setSelectedSteamCup(to);
        } else if (from < selected && to >= selected) {
            setSelectedSteamCup(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedSteamCup(selected + 1);
        }

        emit steamCupPresetsChanged();
    }
}

QVariantMap Settings::getSteamCupPreset(int index) const {
    QByteArray data = m_settings.value("steam/cupPresets").toByteArray();
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

    // Max 5 favorites
    if (arr.size() >= 5) {
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

// Hot water cup presets
QVariantList Settings::waterCupPresets() const {
    QByteArray data = m_settings.value("water/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedWaterCup() const {
    return m_settings.value("water/selectedCup", 0).toInt();
}

void Settings::setSelectedWaterCup(int index) {
    if (selectedWaterCup() != index) {
        m_settings.setValue("water/selectedCup", index);
        emit selectedWaterCupChanged();
    }
}

void Settings::addWaterCupPreset(const QString& name, int volume) {
    QByteArray data = m_settings.value("water/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QJsonObject preset;
    preset["name"] = name;
    preset["volume"] = volume;
    arr.append(preset);

    m_settings.setValue("water/cupPresets", QJsonDocument(arr).toJson());
    emit waterCupPresetsChanged();
}

void Settings::updateWaterCupPreset(int index, const QString& name, int volume) {
    QByteArray data = m_settings.value("water/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["volume"] = volume;
        arr[index] = preset;

        m_settings.setValue("water/cupPresets", QJsonDocument(arr).toJson());
        emit waterCupPresetsChanged();
    }
}

void Settings::removeWaterCupPreset(int index) {
    QByteArray data = m_settings.value("water/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("water/cupPresets", QJsonDocument(arr).toJson());

        // Adjust selected cup if needed
        int selected = selectedWaterCup();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedWaterCup(arr.size() - 1);
        }

        emit waterCupPresetsChanged();
    }
}

void Settings::moveWaterCupPreset(int from, int to) {
    QByteArray data = m_settings.value("water/cupPresets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("water/cupPresets", QJsonDocument(arr).toJson());

        // Update selected cup to follow the moved item if it was selected
        int selected = selectedWaterCup();
        if (selected == from) {
            setSelectedWaterCup(to);
        } else if (from < selected && to >= selected) {
            setSelectedWaterCup(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedWaterCup(selected + 1);
        }

        emit waterCupPresetsChanged();
    }
}

QVariantMap Settings::getWaterCupPreset(int index) const {
    QByteArray data = m_settings.value("water/cupPresets").toByteArray();
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

// Generic settings access
QVariant Settings::value(const QString& key, const QVariant& defaultValue) const {
    return m_settings.value(key, defaultValue);
}

void Settings::setValue(const QString& key, const QVariant& value) {
    m_settings.setValue(key, value);
    emit valueChanged(key);
}
