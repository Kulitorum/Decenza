#include "settings.h"
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QUrl>
#include <QtMath>
#include <QColor>
#include <QUuid>

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

    // Initialize empty bean presets if none exist (user will add their own)
    if (!m_settings.contains("bean/presets")) {
        QJsonArray emptyPresets;
        m_settings.setValue("bean/presets", QJsonDocument(emptyPresets).toJson());
    }

    // Migrate flat shader/* keys to shader/crt/* (one-time, v1.5.x → v1.6)
    if (m_settings.contains("shader/scanlineIntensity") && !m_settings.contains("shader/migrated")) {
        static const QStringList crtParams = {
            "scanlineIntensity", "scanlineSize", "noiseIntensity", "bloomStrength",
            "aberration", "jitterAmount", "vignetteStrength", "tintStrength",
            "flickerAmount", "glitchRate", "glowStart", "noiseSize",
            "reflectionStrength"
        };
        for (const QString& name : crtParams) {
            const QString oldKey = "shader/" + name;
            if (m_settings.contains(oldKey)) {
                m_settings.setValue("shader/crt/" + name, m_settings.value(oldKey));
                m_settings.remove(oldKey);
            }
        }
        m_settings.setValue("shader/migrated", true);
        qDebug() << "Settings: Migrated flat shader params to shader/crt/ namespace";
    }

    // Load brew parameter overrides (persistent)
    m_hasTemperatureOverride = m_settings.value("brew/hasTemperatureOverride", false).toBool();
    if (m_hasTemperatureOverride) {
        m_temperatureOverride = m_settings.value("brew/temperatureOverride", 0.0).toDouble();
    }

    m_hasBrewYieldOverride = m_settings.value("brew/hasBrewYieldOverride", false).toBool();
    if (m_hasBrewYieldOverride) {
        m_brewYieldOverride = m_settings.value("brew/brewYieldOverride", 0.0).toDouble();
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

QString Settings::scaleName() const {
    return m_settings.value("scale/name", "").toString();
}

void Settings::setScaleName(const QString& name) {
    if (scaleName() != name) {
        m_settings.setValue("scale/name", name);
        emit scaleNameChanged();
    }
}

// FlowScale
bool Settings::useFlowScale() const {
    return m_settings.value("flow/useFlowScale", true).toBool();
}

void Settings::setUseFlowScale(bool enabled) {
    if (useFlowScale() != enabled) {
        m_settings.setValue("flow/useFlowScale", enabled);
        emit useFlowScaleChanged();
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

double Settings::lastUsedRatio() const {
    return m_settings.value("espresso/lastUsedRatio", 2.0).toDouble();
}

void Settings::setLastUsedRatio(double ratio) {
    if (lastUsedRatio() != ratio) {
        m_settings.setValue("espresso/lastUsedRatio", ratio);
        emit lastUsedRatioChanged();
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
    return m_settings.value("steam/keepHeaterOn", true).toBool();
}

void Settings::setKeepSteamHeaterOn(bool keep) {
    if (keepSteamHeaterOn() != keep) {
        m_settings.setValue("steam/keepHeaterOn", keep);
        emit keepSteamHeaterOnChanged();
    }
}

int Settings::steamAutoFlushSeconds() const {
    return m_settings.value("steam/autoFlushSeconds", 0).toInt();
}

void Settings::setSteamAutoFlushSeconds(int seconds) {
    if (steamAutoFlushSeconds() != seconds) {
        m_settings.setValue("steam/autoFlushSeconds", seconds);
        emit steamAutoFlushSecondsChanged();
    }
}

// Headless machine settings
bool Settings::headlessSkipPurgeConfirm() const {
    return m_settings.value("headless/skipPurgeConfirm", false).toBool();
}

void Settings::setHeadlessSkipPurgeConfirm(bool skip) {
    if (headlessSkipPurgeConfirm() != skip) {
        m_settings.setValue("headless/skipPurgeConfirm", skip);
        emit headlessSkipPurgeConfirmChanged();
    }
}

// Launcher mode (Android only)
bool Settings::launcherMode() const {
    return m_settings.value("app/launcherMode", false).toBool();
}

void Settings::setLauncherMode(bool enabled) {
    bool changed = (launcherMode() != enabled);
    m_settings.setValue("app/launcherMode", enabled);

#ifdef Q_OS_ANDROID
    // Enable/disable the LauncherAlias activity-alias at runtime
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject pm = activity.callObjectMethod(
            "getPackageManager", "()Landroid/content/pm/PackageManager;");
        QJniObject pkgName = activity.callObjectMethod(
            "getPackageName", "()Ljava/lang/String;");
        QJniObject aliasName = QJniObject::fromString(
            "io.github.kulitorum.decenza_de1.LauncherAlias");

        QJniObject componentName("android/content/ComponentName",
            "(Ljava/lang/String;Ljava/lang/String;)V",
            pkgName.object<jstring>(), aliasName.object<jstring>());

        // COMPONENT_ENABLED_STATE_ENABLED = 1, COMPONENT_ENABLED_STATE_DISABLED = 2
        // DONT_KILL_APP = 1
        int state = enabled ? 1 : 2;
        pm.callMethod<void>("setComponentEnabledSetting",
            "(Landroid/content/ComponentName;II)V",
            componentName.object(), state, 1);
    }
#endif

    if (changed)
        emit launcherModeChanged();
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
        qDebug() << "setSelectedFavoriteProfile:" << selectedFavoriteProfile() << "->" << index;
        m_settings.setValue("profile/selectedFavorite", index);
        emit selectedFavoriteProfileChanged();
    }
}

void Settings::addFavoriteProfile(const QString& name, const QString& filename) {
    // Ensure consistency: un-hide a profile when favoriting it
    removeHiddenProfile(filename);

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

int Settings::findFavoriteIndexByFilename(const QString& filename) const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    qDebug() << "findFavoriteIndexByFilename: looking for" << filename << "in" << arr.size() << "favorites";
    for (int i = 0; i < arr.size(); ++i) {
        QString favFilename = arr[i].toObject()["filename"].toString();
        qDebug() << "  [" << i << "]" << favFilename << (favFilename == filename ? "MATCH" : "");
        if (favFilename == filename) {
            return i;
        }
    }
    return -1;
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

// Hidden profiles (downloaded/user profiles removed from "Selected" view)
QStringList Settings::hiddenProfiles() const {
    return m_settings.value("profile/hiddenProfiles").toStringList();
}

void Settings::setHiddenProfiles(const QStringList& profiles) {
    if (hiddenProfiles() != profiles) {
        m_settings.setValue("profile/hiddenProfiles", profiles);
        emit hiddenProfilesChanged();
    }
}

void Settings::addHiddenProfile(const QString& filename) {
    QStringList current = hiddenProfiles();
    if (!current.contains(filename)) {
        current.append(filename);
        m_settings.setValue("profile/hiddenProfiles", current);
        emit hiddenProfilesChanged();

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

            int selected = selectedFavoriteProfile();
            if (arr.isEmpty()) {
                setSelectedFavoriteProfile(-1);
            } else if (selected >= arr.size()) {
                setSelectedFavoriteProfile(arr.size() - 1);
            }

            emit favoriteProfilesChanged();
        }
    }
}

void Settings::removeHiddenProfile(const QString& filename) {
    QStringList current = hiddenProfiles();
    if (current.removeAll(filename) > 0) {
        m_settings.setValue("profile/hiddenProfiles", current);
        emit hiddenProfilesChanged();
    }
}

bool Settings::isHiddenProfile(const QString& filename) const {
    return hiddenProfiles().contains(filename);
}

// Saved searches (shot history)
QStringList Settings::savedSearches() const {
    return m_settings.value("shotHistory/savedSearches").toStringList();
}

void Settings::setSavedSearches(const QStringList& searches) {
    if (savedSearches() != searches) {
        m_settings.setValue("shotHistory/savedSearches", searches);
        emit savedSearchesChanged();
    }
}

void Settings::addSavedSearch(const QString& search) {
    QString trimmed = search.trimmed();
    if (trimmed.isEmpty()) return;
    QStringList current = savedSearches();
    if (!current.contains(trimmed)) {
        if (current.size() >= 30) return;  // Cap at 30 saved searches
        current.append(trimmed);
        m_settings.setValue("shotHistory/savedSearches", current);
        emit savedSearchesChanged();
    }
}

void Settings::removeSavedSearch(const QString& search) {
    QStringList current = savedSearches();
    if (current.removeAll(search) > 0) {
        m_settings.setValue("shotHistory/savedSearches", current);
        emit savedSearchesChanged();
    }
}

// Shot history sort settings
QString Settings::shotHistorySortField() const {
    return m_settings.value("shotHistory/sortField", "timestamp").toString();
}

void Settings::setShotHistorySortField(const QString& field) {
    if (shotHistorySortField() != field) {
        m_settings.setValue("shotHistory/sortField", field);
        emit shotHistorySortFieldChanged();
    }
}

QString Settings::shotHistorySortDirection() const {
    return m_settings.value("shotHistory/sortDirection", "DESC").toString();
}

void Settings::setShotHistorySortDirection(const QString& direction) {
    if (shotHistorySortDirection() != direction) {
        m_settings.setValue("shotHistory/sortDirection", direction);
        emit shotHistorySortDirectionChanged();
    }
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

QString Settings::waterVolumeMode() const {
    return m_settings.value("water/volumeMode", "weight").toString();
}

void Settings::setWaterVolumeMode(const QString& mode) {
    if (waterVolumeMode() != mode) {
        m_settings.setValue("water/volumeMode", mode);
        emit waterVolumeModeChanged();
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

void Settings::addWaterVesselPreset(const QString& name, int volume, const QString& mode, int flowRate) {
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

void Settings::updateWaterVesselPreset(int index, const QString& name, int volume, const QString& mode, int flowRate) {
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
            setSelectedFlushPreset(static_cast<int>(arr.size()) - 1);
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

// Helper method to get bean presets as QJsonArray
QJsonArray Settings::getBeanPresetsArray() const {
    QByteArray data = m_settings.value("bean/presets").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.array();
}

// Bean presets
QVariantList Settings::beanPresets() const {
    QJsonArray arr = getBeanPresetsArray();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int Settings::selectedBeanPreset() const {
    return m_settings.value("bean/selectedPreset", -1).toInt();
}

void Settings::setSelectedBeanPreset(int index) {
    if (selectedBeanPreset() != index) {
        m_settings.setValue("bean/selectedPreset", index);
        emit selectedBeanPresetChanged();
    }
}

void Settings::addBeanPreset(const QString& name, const QString& brand, const QString& type,
                             const QString& roastDate, const QString& roastLevel,
                             const QString& grinderModel, const QString& grinderSetting) {
    QJsonArray arr = getBeanPresetsArray();

    QJsonObject preset;
    preset["name"] = name;
    preset["brand"] = brand;
    preset["type"] = type;
    preset["roastDate"] = roastDate;
    preset["roastLevel"] = roastLevel;
    preset["grinderModel"] = grinderModel;
    preset["grinderSetting"] = grinderSetting;
    arr.append(preset);

    m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
    emit beanPresetsChanged();
}

void Settings::updateBeanPreset(int index, const QString& name, const QString& brand,
                                const QString& type, const QString& roastDate,
                                const QString& roastLevel, const QString& grinderModel,
                                const QString& grinderSetting) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < arr.size()) {
        QJsonObject preset;
        preset["name"] = name;
        preset["brand"] = brand;
        preset["type"] = type;
        preset["roastDate"] = roastDate;
        preset["roastLevel"] = roastLevel;
        preset["grinderModel"] = grinderModel;
        preset["grinderSetting"] = grinderSetting;
        arr[index] = preset;

        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
        emit beanPresetsChanged();
    }
}

void Settings::removeBeanPreset(int index) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < arr.size()) {
        arr.removeAt(index);
        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());

        // Adjust selected if needed
        int selected = selectedBeanPreset();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedBeanPreset(static_cast<int>(arr.size()) - 1);
        } else if (arr.size() == 0) {
            setSelectedBeanPreset(-1);
        } else if (selected > index) {
            setSelectedBeanPreset(selected - 1);
        }

        emit beanPresetsChanged();
    }
}

void Settings::moveBeanPreset(int from, int to) {
    QJsonArray arr = getBeanPresetsArray();

    if (from >= 0 && from < arr.size() && to >= 0 && to < arr.size() && from != to) {
        QJsonValue item = arr[from];
        arr.removeAt(from);
        arr.insert(to, item);
        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());

        // Update selected to follow the moved item if it was selected
        int selected = selectedBeanPreset();
        if (selected == from) {
            setSelectedBeanPreset(to);
        } else if (from < selected && to >= selected) {
            setSelectedBeanPreset(selected - 1);
        } else if (from > selected && to <= selected) {
            setSelectedBeanPreset(selected + 1);
        }

        emit beanPresetsChanged();
    }
}

QVariantMap Settings::getBeanPreset(int index) const {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

void Settings::applyBeanPreset(int index) {
    QVariantMap preset = getBeanPreset(index);
    if (preset.isEmpty()) {
        return;
    }

    // Apply all preset fields to DYE settings
    setDyeBeanBrand(preset.value("brand").toString());
    setDyeBeanType(preset.value("type").toString());
    setDyeRoastDate(preset.value("roastDate").toString());
    setDyeRoastLevel(preset.value("roastLevel").toString());
    setDyeGrinderModel(preset.value("grinderModel").toString());
    setDyeGrinderSetting(preset.value("grinderSetting").toString());
}

void Settings::saveBeanPresetFromCurrent(const QString& name) {
    // Check if a preset with this name already exists
    int existingIndex = findBeanPresetByName(name);
    if (existingIndex >= 0) {
        // Update existing preset
        updateBeanPreset(existingIndex,
                        name,
                        dyeBeanBrand(),
                        dyeBeanType(),
                        dyeRoastDate(),
                        dyeRoastLevel(),
                        dyeGrinderModel(),
                        dyeGrinderSetting());
    } else {
        // Add new preset
        addBeanPreset(name,
                     dyeBeanBrand(),
                     dyeBeanType(),
                     dyeRoastDate(),
                     dyeRoastLevel(),
                     dyeGrinderModel(),
                     dyeGrinderSetting());
    }
}

int Settings::findBeanPresetByContent(const QString& brand, const QString& type) const {
    QJsonArray arr = getBeanPresetsArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        QString presetBrand = obj["brand"].toString();
        QString presetType = obj["type"].toString();
        if (presetBrand == brand && presetType == type) {
            return i;
        }
    }
    return -1;
}

int Settings::findBeanPresetByName(const QString& name) const {
    QJsonArray arr = getBeanPresetsArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["name"].toString() == name) {
            return i;
        }
    }
    return -1;
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
    return m_settings.value("profile/current", "Adaptive v2").toString();
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

QString Settings::activeShader() const {
    return m_settings.value("theme/activeShader", "").toString();
}

void Settings::setActiveShader(const QString& shader) {
    if (activeShader() != shader) {
        m_settings.setValue("theme/activeShader", shader);
        emit activeShaderChanged();
    }
}

// Known parameter names per effect (for const-correct enumeration)
static const QMap<QString, QStringList>& knownEffectParams() {
    static const QMap<QString, QStringList> params = {
        {"crt", {
            "scanlineIntensity", "scanlineSize", "noiseIntensity", "bloomStrength",
            "aberration", "jitterAmount", "vignetteStrength", "tintStrength",
            "flickerAmount", "glitchRate", "glowStart", "noiseSize",
            "reflectionStrength",
            "colorGain", "colorContrast", "colorSaturation", "hueShift"
        }}
        // Future: {"vhs", {"trackingError", "colorBleed", ...}}
    };
    return params;
}

QVariantMap Settings::shaderParams() const {
    return effectParams(activeShader());
}

void Settings::setShaderParam(const QString& name, double value) {
    setEffectParam(activeShader(), name, value);
}

QVariantMap Settings::effectParams(const QString& effectId) const {
    if (effectId.isEmpty()) return {};
    const auto& allParams = knownEffectParams();
    if (!allParams.contains(effectId)) return {};

    QVariantMap params;
    for (const QString& name : allParams[effectId]) {
        const QString key = "shader/" + effectId + "/" + name;
        if (m_settings.contains(key))
            params[name] = m_settings.value(key).toDouble();
    }
    return params;
}

void Settings::setEffectParam(const QString& effectId, const QString& name, double value) {
    if (effectId.isEmpty()) return;
    const QString key = "shader/" + effectId + "/" + name;
    double current = m_settings.value(key, -99999.0).toDouble();
    if (qAbs(current - value) > 0.0001) {
        m_settings.setValue(key, value);
        emit shaderParamsChanged();
    }
}

QJsonObject Settings::screenEffectJson() const {
    QJsonObject result;
    result["active"] = activeShader();

    QJsonObject effects;
    const auto& allParams = knownEffectParams();
    for (auto it = allParams.begin(); it != allParams.end(); ++it) {
        QVariantMap params = effectParams(it.key());
        if (!params.isEmpty()) {
            effects[it.key()] = QJsonObject::fromVariantMap(params);
        }
    }
    result["effects"] = effects;
    return result;
}

void Settings::applyScreenEffect(const QJsonObject& screenEffect) {
    // Set active effect (empty = none)
    QString active = screenEffect["active"].toString();
    setActiveShader(active);

    // Restore per-effect params
    QJsonObject effects = screenEffect["effects"].toObject();
    for (auto it = effects.begin(); it != effects.end(); ++it) {
        QString effectId = it.key();
        QJsonObject params = it.value().toObject();
        for (auto pit = params.begin(); pit != params.end(); ++pit) {
            setEffectParam(effectId, pit.key(), pit.value().toDouble());
        }
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

// Font size customization
QVariantMap Settings::customFontSizes() const {
    QByteArray data = m_settings.value("theme/customFontSizes").toByteArray();
    if (data.isEmpty()) {
        return QVariantMap();
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.object().toVariantMap();
}

void Settings::setCustomFontSizes(const QVariantMap& sizes) {
    QJsonObject obj = QJsonObject::fromVariantMap(sizes);
    m_settings.setValue("theme/customFontSizes", QJsonDocument(obj).toJson());
    emit customFontSizesChanged();
}

void Settings::setFontSize(const QString& fontName, int size) {
    QVariantMap sizes = customFontSizes();
    sizes[fontName] = size;
    setCustomFontSizes(sizes);
}

int Settings::getFontSize(const QString& fontName) const {
    QVariantMap sizes = customFontSizes();
    return sizes.value(fontName, 0).toInt();
}

void Settings::resetFontSizesToDefault() {
    m_settings.remove("theme/customFontSizes");
    emit customFontSizesChanged();
}

void Settings::setCurrentPageColors(const QStringList& colors) {
    if (m_currentPageColors != colors) {
        m_currentPageColors = colors;
        emit currentPageColorsChanged();
    }
}

void Settings::flashThemeColor(const QString& colorName) {
    // Stop any existing flash
    if (m_flashTimer) {
        m_flashTimer->stop();
        m_flashTimer->deleteLater();
        m_flashTimer = nullptr;
    }

    m_flashColorName = colorName;
    m_flashPhase = 1;
    emit flashColorNameChanged();
    emit flashPhaseChanged();

    m_flashTimer = new QTimer(this);
    m_flashTimer->setInterval(130);
    connect(m_flashTimer, &QTimer::timeout, this, [this]() {
        m_flashPhase++;
        if (m_flashPhase > 6) {
            m_flashTimer->stop();
            m_flashTimer->deleteLater();
            m_flashTimer = nullptr;
            m_flashColorName.clear();
            m_flashPhase = 0;
            emit flashColorNameChanged();
        }
        emit flashPhaseChanged();
    });
    m_flashTimer->start();
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
        setActiveShader("");  // Default theme has no screen effect
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
            // Restore screen effect (or disable for old presets without it)
            if (obj.contains("screenEffect"))
                applyScreenEffect(obj["screenEffect"].toObject());
            else
                setActiveShader("");
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
    for (int i = static_cast<int>(userThemes.size()) - 1; i >= 0; --i) {
        if (userThemes[i].toObject()["name"].toString() == name) {
            userThemes.removeAt(i);
        }
    }

    // Create new theme entry
    QJsonObject newTheme;
    newTheme["name"] = name;
    newTheme["colors"] = QJsonObject::fromVariantMap(customThemeColors());
    newTheme["screenEffect"] = screenEffectJson();
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

    // Weight flow - lighter variant of weight color (same relationship as goal colors)
    palette["weightFlowColor"] = hslColor(splitComp1 + goldenAngle * 3, 55.0, 70.0);

    // Highlight color - analogous to warning for attention-drawing
    palette["highlightColor"] = palette["warningColor"];

    // DYE measurement colors - spread using golden angle from split-complementary
    palette["dyeDoseColor"] = hslColor(splitComp2 + goldenAngle * 0, 50.0, 40.0);
    palette["dyeOutputColor"] = hslColor(splitComp2 + goldenAngle * 1, 65.0, 50.0);
    palette["dyeTdsColor"] = hslColor(splitComp2 + goldenAngle * 2, 75.0, 55.0);
    palette["dyeEyColor"] = hslColor(splitComp2 + goldenAngle * 3, 55.0, 45.0);

    // Button states
    palette["buttonDisabled"] = hslColor(baseHue, 10.0, 35.0);

    // UI indicator colors - derived from status colors
    palette["stopMarkerColor"] = hslColor(fmod(360.0 + baseHue * 0.1, 360.0), 70.0, 65.0);  // Error-ish but lighter
    palette["modifiedIndicatorColor"] = hslColor(50.0 + (baseHue * 0.1), 85.0, 55.0);  // Yellow-ish
    palette["simulationIndicatorColor"] = hslColor(25.0 + (baseHue * 0.1), 85.0, 50.0);  // Orange-ish
    palette["warningButtonColor"] = palette["warningColor"];
    palette["successButtonColor"] = hslColor(140.0 + (baseHue * 0.1), 70.0, 35.0);  // Darker success

    // Frame markers - semi-transparent overlay, adapts to background brightness
    palette["frameMarkerColor"] = (bgLight > 40.0) ? "#66000000" : "#66ffffff";

    // Row alternate backgrounds - very dark/light variants of background
    double rowLight = qBound(3.0, bgLight - 5.0, 15.0);
    palette["rowAlternateColor"] = hslColor(complementary, 20.0, rowLight);
    palette["rowAlternateLightColor"] = hslColor(complementary, 15.0, rowLight + 5.0);

    // Source badge colors - 3 distinct hues spread across the wheel
    palette["sourceBadgeBlueColor"] = hslColor(baseHue + 210.0, 65.0, 55.0);   // Cool tone
    palette["sourceBadgeGreenColor"] = hslColor(baseHue + 90.0, 65.0, 55.0);   // Green-ish
    palette["sourceBadgeOrangeColor"] = hslColor(baseHue + 330.0, 65.0, 55.0); // Warm tone

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

bool Settings::visualizerClearNotesOnStart() const {
    return m_settings.value("visualizer/clearNotesOnStart", false).toBool();
}

void Settings::setVisualizerClearNotesOnStart(bool enabled) {
    if (visualizerClearNotesOnStart() != enabled) {
        m_settings.setValue("visualizer/clearNotesOnStart", enabled);
        emit visualizerClearNotesOnStartChanged();
    }
}

int Settings::defaultShotRating() const {
    return m_settings.value("shot/defaultRating", 75).toInt();
}

void Settings::setDefaultShotRating(int rating) {
    if (defaultShotRating() != rating) {
        m_settings.setValue("shot/defaultRating", rating);
        emit defaultShotRatingChanged();

        // Also update the current shot's enjoyment rating so the new default
        // applies immediately to the next shot (not the shot after that)
        setDyeEspressoEnjoyment(rating);
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
    return m_settings.value("ai/ollamaEndpoint", "").toString();
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

QString Settings::openrouterApiKey() const {
    return m_settings.value("ai/openrouterKey", "").toString();
}

void Settings::setOpenrouterApiKey(const QString& key) {
    if (openrouterApiKey() != key) {
        m_settings.setValue("ai/openrouterKey", key);
        emit openrouterApiKeyChanged();
        emit valueChanged("ai/openrouterKey");
    }
}

QString Settings::openrouterModel() const {
    return m_settings.value("ai/openrouterModel", "anthropic/claude-sonnet-4").toString();
}

void Settings::setOpenrouterModel(const QString& model) {
    if (openrouterModel() != model) {
        m_settings.setValue("ai/openrouterModel", model);
        emit openrouterModelChanged();
        emit valueChanged("ai/openrouterModel");
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

void Settings::ensureDyeCacheLoaded() const {
    if (!m_dyeCacheInitialized) {
        m_dyeGrinderModelCache = m_settings.value("dye/grinderModel", "").toString();
        m_dyeGrinderSettingCache = m_settings.value("dye/grinderSetting", "").toString();
        m_dyeBeanWeightCache = m_settings.value("dye/beanWeight", 18.0).toDouble();
        m_dyeDrinkWeightCache = m_settings.value("dye/drinkWeight", 36.0).toDouble();
        m_dyeCacheInitialized = true;
    }
}

QString Settings::dyeGrinderModel() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderModelCache;
}

void Settings::setDyeGrinderModel(const QString& value) {
    if (dyeGrinderModel() != value) {
        m_dyeGrinderModelCache = value;
        m_settings.setValue("dye/grinderModel", value);
        emit dyeGrinderModelChanged();
    }
}

QString Settings::dyeGrinderSetting() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderSettingCache;
}

void Settings::setDyeGrinderSetting(const QString& value) {
    if (dyeGrinderSetting() != value) {
        m_dyeGrinderSettingCache = value;
        m_settings.setValue("dye/grinderSetting", value);
        emit dyeGrinderSettingChanged();
    }
}

double Settings::dyeBeanWeight() const {
    ensureDyeCacheLoaded();
    return m_dyeBeanWeightCache;
}

void Settings::setDyeBeanWeight(double value) {
    if (!qFuzzyCompare(1.0 + dyeBeanWeight(), 1.0 + value)) {
        m_dyeBeanWeightCache = value;
        m_settings.setValue("dye/beanWeight", value);
        emit dyeBeanWeightChanged();
    }
}

double Settings::dyeDrinkWeight() const {
    ensureDyeCacheLoaded();
    return m_dyeDrinkWeightCache;
}

void Settings::setDyeDrinkWeight(double value) {
    if (!qFuzzyCompare(1.0 + dyeDrinkWeight(), 1.0 + value)) {
        m_dyeDrinkWeightCache = value;
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
    return m_settings.value("dye/espressoEnjoyment", defaultShotRating()).toInt();
}

void Settings::setDyeEspressoEnjoyment(int value) {
    if (dyeEspressoEnjoyment() != value) {
        m_settings.setValue("dye/espressoEnjoyment", value);
        emit dyeEspressoEnjoymentChanged();
    }
}

QString Settings::dyeShotNotes() const {
    // Try new key first, fall back to old key for backward compatibility
    QString notes = m_settings.value("dye/shotNotes", "").toString();
    if (notes.isEmpty()) {
        notes = m_settings.value("dye/espressoNotes", "").toString();
    }
    return notes;
}

void Settings::setDyeShotNotes(const QString& value) {
    if (dyeShotNotes() != value) {
        m_settings.setValue("dye/shotNotes", value);
        // Clear legacy key so the fallback in the getter doesn't resurrect old notes
        if (value.isEmpty()) {
            m_settings.remove("dye/espressoNotes");
        }
        emit dyeShotNotesChanged();
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

bool Settings::webSecurityEnabled() const {
    return m_settings.value("shotServer/webSecurityEnabled", false).toBool();
}

void Settings::setWebSecurityEnabled(bool enabled) {
    if (webSecurityEnabled() != enabled) {
        m_settings.setValue("shotServer/webSecurityEnabled", enabled);
        emit webSecurityEnabledChanged();
    }
}

// Auto-favorites settings
QString Settings::autoFavoritesGroupBy() const {
    return m_settings.value("autoFavorites/groupBy", "bean_profile").toString();
}

void Settings::setAutoFavoritesGroupBy(const QString& groupBy) {
    if (autoFavoritesGroupBy() != groupBy) {
        m_settings.setValue("autoFavorites/groupBy", groupBy);
        emit autoFavoritesGroupByChanged();
    }
}

int Settings::autoFavoritesMaxItems() const {
    return m_settings.value("autoFavorites/maxItems", 10).toInt();
}

void Settings::setAutoFavoritesMaxItems(int maxItems) {
    if (autoFavoritesMaxItems() != maxItems) {
        m_settings.setValue("autoFavorites/maxItems", maxItems);
        emit autoFavoritesMaxItemsChanged();
    }
}

bool Settings::bleHealthRefreshEnabled() const {
    // Migration path: keep honoring legacy key if new key is not yet present.
    if (m_settings.contains("ble/healthRefreshEnabled")) {
        return m_settings.value("ble/healthRefreshEnabled", false).toBool();
    }
    return m_settings.value("ble/resetOnWake", false).toBool();
}

void Settings::setBleHealthRefreshEnabled(bool enabled) {
    if (bleHealthRefreshEnabled() != enabled) {
        m_settings.setValue("ble/healthRefreshEnabled", enabled);
        // Keep legacy key in sync for downgrade compatibility.
        m_settings.setValue("ble/resetOnWake", enabled);
        emit bleHealthRefreshEnabledChanged();
    }
}

bool Settings::autoFavoritesOpenBrewSettings() const {
    return m_settings.value("autoFavorites/openBrewSettings", false).toBool();
}

void Settings::setAutoFavoritesOpenBrewSettings(bool open) {
    if (autoFavoritesOpenBrewSettings() != open) {
        m_settings.setValue("autoFavorites/openBrewSettings", open);
        emit autoFavoritesOpenBrewSettingsChanged();
    }
}

bool Settings::autoFavoritesHideUnrated() const {
    return m_settings.value("autoFavorites/hideUnrated", false).toBool();
}

void Settings::setAutoFavoritesHideUnrated(bool hide) {
    if (autoFavoritesHideUnrated() != hide) {
        m_settings.setValue("autoFavorites/hideUnrated", hide);
        emit autoFavoritesHideUnratedChanged();
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

bool Settings::betaUpdatesEnabled() const {
    return m_settings.value("updates/betaEnabled", false).toBool();
}

void Settings::setBetaUpdatesEnabled(bool enabled) {
    if (betaUpdatesEnabled() != enabled) {
        m_settings.setValue("updates/betaEnabled", enabled);
        emit betaUpdatesEnabledChanged();
    }
}

int Settings::dailyBackupHour() const {
    return m_settings.value("backup/dailyBackupHour", -1).toInt();  // -1 = off
}

void Settings::setDailyBackupHour(int hour) {
    if (dailyBackupHour() != hour) {
        m_settings.setValue("backup/dailyBackupHour", hour);
        emit dailyBackupHourChanged();
    }
}

QString Settings::waterLevelDisplayUnit() const {
    return m_settings.value("display/waterLevelUnit", "percent").toString();
}

void Settings::setWaterLevelDisplayUnit(const QString& unit) {
    if (waterLevelDisplayUnit() != unit) {
        m_settings.setValue("display/waterLevelUnit", unit);
        emit waterLevelDisplayUnitChanged();
    }
}

int Settings::waterRefillPoint() const {
    return m_settings.value("water/refillPoint", 5).toInt();
}

void Settings::setWaterRefillPoint(int mm) {
    if (waterRefillPoint() != mm) {
        m_settings.setValue("water/refillPoint", mm);
        emit waterRefillPointChanged();
    }
}

int Settings::refillKitOverride() const {
    return m_settings.value("water/refillKitOverride", 2).toInt();  // Default: auto-detect
}

void Settings::setRefillKitOverride(int value) {
    if (refillKitOverride() != value) {
        m_settings.setValue("water/refillKitOverride", value);
        emit refillKitOverrideChanged();
    }
}

// Heater calibration (de1app's set_heater_tweaks)
// All values stored in raw firmware units (tenths)

int Settings::heaterIdleTemp() const {
    int val = m_settings.value("calibration/heaterIdleTemp", 990).toInt();
    return qBound(0, val, 990);
}

void Settings::setHeaterIdleTemp(int value) {
    if (heaterIdleTemp() != value) {
        m_settings.setValue("calibration/heaterIdleTemp", value);
        emit heaterIdleTempChanged();
    }
}

int Settings::heaterWarmupFlow() const {
    int val = m_settings.value("calibration/heaterWarmupFlow", 20).toInt();
    return qBound(5, val, 60);
}

void Settings::setHeaterWarmupFlow(int value) {
    if (heaterWarmupFlow() != value) {
        m_settings.setValue("calibration/heaterWarmupFlow", value);
        emit heaterWarmupFlowChanged();
    }
}

int Settings::heaterTestFlow() const {
    int val = m_settings.value("calibration/heaterTestFlow", 40).toInt();
    return qBound(5, val, 80);
}

void Settings::setHeaterTestFlow(int value) {
    if (heaterTestFlow() != value) {
        m_settings.setValue("calibration/heaterTestFlow", value);
        emit heaterTestFlowChanged();
    }
}

int Settings::heaterWarmupTimeout() const {
    int val = m_settings.value("calibration/heaterWarmupTimeout", 10).toInt();
    return qBound(10, val, 300);
}

void Settings::setHeaterWarmupTimeout(int value) {
    if (heaterWarmupTimeout() != value) {
        m_settings.setValue("calibration/heaterWarmupTimeout", value);
        emit heaterWarmupTimeoutChanged();
    }
}

int Settings::hotWaterFlowRate() const {
    int val = m_settings.value("calibration/hotWaterFlowRate", 10).toInt();
    return qBound(5, val, 80);
}

void Settings::setHotWaterFlowRate(int value) {
    if (hotWaterFlowRate() != value) {
        m_settings.setValue("calibration/hotWaterFlowRate", value);
        emit hotWaterFlowRateChanged();
    }
}

bool Settings::steamTwoTapStop() const {
    return m_settings.value("calibration/steamTwoTapStop", true).toBool();
}

void Settings::setSteamTwoTapStop(bool value) {
    if (steamTwoTapStop() != value) {
        m_settings.setValue("calibration/steamTwoTapStop", value);
        emit steamTwoTapStopChanged();
    }
}

bool Settings::developerTranslationUpload() const {
    // Runtime-only flag - not persisted, resets to false on app restart
    return m_developerTranslationUpload;
}

void Settings::setDeveloperTranslationUpload(bool enabled) {
    if (m_developerTranslationUpload != enabled) {
        m_developerTranslationUpload = enabled;
        emit developerTranslationUploadChanged();
    }
}

bool Settings::simulationMode() const {
#if defined(QT_DEBUG) && (defined(Q_OS_WIN) || defined(Q_OS_MACOS))
    return m_settings.value("developer/simulationMode", true).toBool();
#else
    return m_settings.value("developer/simulationMode", false).toBool();
#endif
}

void Settings::setSimulationMode(bool enabled) {
    if (simulationMode() != enabled) {
        m_settings.setValue("developer/simulationMode", enabled);
        emit simulationModeChanged();
    }
}

// Temperature override (persistent)
double Settings::temperatureOverride() const {
    return m_temperatureOverride;
}

void Settings::setTemperatureOverride(double temp) {
    if (!qFuzzyCompare(m_temperatureOverride, temp) || !m_hasTemperatureOverride) {
        qDebug() << "setTemperatureOverride:" << m_temperatureOverride << "→" << temp;
        m_temperatureOverride = temp;
        m_hasTemperatureOverride = true;
        m_settings.setValue("brew/temperatureOverride", temp);
        m_settings.setValue("brew/hasTemperatureOverride", true);
        emit temperatureOverrideChanged();
    }
}

bool Settings::hasTemperatureOverride() const {
    return m_hasTemperatureOverride;
}

void Settings::clearTemperatureOverride() {
    if (m_hasTemperatureOverride || !qFuzzyIsNull(m_temperatureOverride)) {
        m_hasTemperatureOverride = false;
        m_temperatureOverride = 0.0;
        m_settings.remove("brew/temperatureOverride");
        m_settings.remove("brew/hasTemperatureOverride");
        emit temperatureOverrideChanged();
    }
}

// Brew parameter overrides (persistent)
double Settings::brewYieldOverride() const {
    return m_brewYieldOverride;
}

void Settings::setBrewYieldOverride(double yield) {
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

bool Settings::hasBrewYieldOverride() const {
    return m_hasBrewYieldOverride;
}

void Settings::clearAllBrewOverrides() {
    bool changed = false;

    // Clear yield override
    if (m_hasBrewYieldOverride || !qFuzzyIsNull(m_brewYieldOverride)) {
        m_brewYieldOverride = 0.0;
        m_hasBrewYieldOverride = false;
        m_settings.remove("brew/brewYieldOverride");
        m_settings.remove("brew/hasBrewYieldOverride");
        changed = true;
    }

    // Clear temperature override
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

// Auto-wake schedule
bool Settings::autoWakeEnabled() const {
    return m_settings.value("autoWake/enabled", false).toBool();
}

void Settings::setAutoWakeEnabled(bool enabled) {
    if (autoWakeEnabled() != enabled) {
        m_settings.setValue("autoWake/enabled", enabled);
        emit autoWakeEnabledChanged();
    }
}

QVariantList Settings::autoWakeSchedule() const {
    QByteArray data = m_settings.value("autoWake/schedule").toByteArray();
    if (data.isEmpty()) {
        // Return default schedule: all days disabled, 07:00
        QVariantList defaultSchedule;
        for (int i = 0; i < 7; ++i) {
            QVariantMap day;
            day["enabled"] = false;
            day["hour"] = 7;
            day["minute"] = 0;
            defaultSchedule.append(day);
        }
        return defaultSchedule;
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

void Settings::setAutoWakeSchedule(const QVariantList& schedule) {
    QJsonArray arr;
    for (const QVariant& v : schedule) {
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    m_settings.setValue("autoWake/schedule", QJsonDocument(arr).toJson());
    emit autoWakeScheduleChanged();
}

void Settings::setAutoWakeDayEnabled(int dayIndex, bool enabled) {
    if (dayIndex < 0 || dayIndex > 6) return;

    QVariantList schedule = autoWakeSchedule();
    QVariantMap day = schedule[dayIndex].toMap();
    day["enabled"] = enabled;
    schedule[dayIndex] = day;
    setAutoWakeSchedule(schedule);
}

void Settings::setAutoWakeDayTime(int dayIndex, int hour, int minute) {
    if (dayIndex < 0 || dayIndex > 6) return;
    if (hour < 0 || hour > 23) return;
    if (minute < 0 || minute > 59) return;

    QVariantList schedule = autoWakeSchedule();
    QVariantMap day = schedule[dayIndex].toMap();
    day["hour"] = hour;
    day["minute"] = minute;
    schedule[dayIndex] = day;
    setAutoWakeSchedule(schedule);
}

bool Settings::autoWakeStayAwakeEnabled() const {
    return m_settings.value("autoWake/stayAwakeEnabled", false).toBool();
}

void Settings::setAutoWakeStayAwakeEnabled(bool enabled) {
    if (autoWakeStayAwakeEnabled() != enabled) {
        m_settings.setValue("autoWake/stayAwakeEnabled", enabled);
        emit autoWakeStayAwakeEnabledChanged();
    }
}

int Settings::autoWakeStayAwakeMinutes() const {
    return m_settings.value("autoWake/stayAwakeMinutes", 120).toInt();
}

void Settings::setAutoWakeStayAwakeMinutes(int minutes) {
    if (autoWakeStayAwakeMinutes() != minutes) {
        m_settings.setValue("autoWake/stayAwakeMinutes", minutes);
        emit autoWakeStayAwakeMinutesChanged();
    }
}

// MQTT settings (Home Automation)
bool Settings::mqttEnabled() const {
    return m_settings.value("mqtt/enabled", false).toBool();
}

void Settings::setMqttEnabled(bool enabled) {
    if (mqttEnabled() != enabled) {
        m_settings.setValue("mqtt/enabled", enabled);
        emit mqttEnabledChanged();
    }
}

QString Settings::mqttBrokerHost() const {
    return m_settings.value("mqtt/brokerHost", "").toString();
}

void Settings::setMqttBrokerHost(const QString& host) {
    if (mqttBrokerHost() != host) {
        m_settings.setValue("mqtt/brokerHost", host);
        emit mqttBrokerHostChanged();
    }
}

int Settings::mqttBrokerPort() const {
    return m_settings.value("mqtt/brokerPort", 1883).toInt();
}

void Settings::setMqttBrokerPort(int port) {
    if (mqttBrokerPort() != port) {
        m_settings.setValue("mqtt/brokerPort", port);
        emit mqttBrokerPortChanged();
    }
}

QString Settings::mqttUsername() const {
    return m_settings.value("mqtt/username", "").toString();
}

void Settings::setMqttUsername(const QString& username) {
    if (mqttUsername() != username) {
        m_settings.setValue("mqtt/username", username);
        emit mqttUsernameChanged();
    }
}

QString Settings::mqttPassword() const {
    return m_settings.value("mqtt/password", "").toString();
}

void Settings::setMqttPassword(const QString& password) {
    if (mqttPassword() != password) {
        m_settings.setValue("mqtt/password", password);
        emit mqttPasswordChanged();
    }
}

QString Settings::mqttBaseTopic() const {
    return m_settings.value("mqtt/baseTopic", "decenza").toString();
}

void Settings::setMqttBaseTopic(const QString& topic) {
    if (mqttBaseTopic() != topic) {
        m_settings.setValue("mqtt/baseTopic", topic);
        emit mqttBaseTopicChanged();
    }
}

int Settings::mqttPublishInterval() const {
    return m_settings.value("mqtt/publishInterval", 1000).toInt();
}

void Settings::setMqttPublishInterval(int interval) {
    if (mqttPublishInterval() != interval) {
        m_settings.setValue("mqtt/publishInterval", interval);
        emit mqttPublishIntervalChanged();
    }
}

bool Settings::mqttRetainMessages() const {
    return m_settings.value("mqtt/retainMessages", true).toBool();
}

void Settings::setMqttRetainMessages(bool retain) {
    if (mqttRetainMessages() != retain) {
        m_settings.setValue("mqtt/retainMessages", retain);
        emit mqttRetainMessagesChanged();
    }
}

bool Settings::mqttHomeAssistantDiscovery() const {
    return m_settings.value("mqtt/homeAssistantDiscovery", true).toBool();
}

void Settings::setMqttHomeAssistantDiscovery(bool enabled) {
    if (mqttHomeAssistantDiscovery() != enabled) {
        m_settings.setValue("mqtt/homeAssistantDiscovery", enabled);
        emit mqttHomeAssistantDiscoveryChanged();
    }
}

QString Settings::mqttClientId() const {
    return m_settings.value("mqtt/clientId", "").toString();
}

void Settings::setMqttClientId(const QString& clientId) {
    if (mqttClientId() != clientId) {
        m_settings.setValue("mqtt/clientId", clientId);
        emit mqttClientIdChanged();
    }
}

// Flow calibration

double Settings::flowCalibrationMultiplier() const {
    return m_settings.value("calibration/flowMultiplier", 1.0).toDouble();
}

void Settings::setFlowCalibrationMultiplier(double multiplier) {
    multiplier = qBound(0.35, multiplier, 2.0);
    if (qAbs(flowCalibrationMultiplier() - multiplier) > 0.001) {
        m_settings.setValue("calibration/flowMultiplier", multiplier);
        emit flowCalibrationMultiplierChanged();
    }
}

// SAW (Stop-at-Weight) learning

// Returns average lag for display in QML settings (calculated from stored drip/flow)
double Settings::sawLearnedLag() const {
    ensureSawCacheLoaded();

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        return 1.5;  // Default
    }

    QString currentScale = scaleType();
    double sumLag = 0;
    int count = 0;

    for (int i = arr.size() - 1; i >= 0 && count < 5; --i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["scale"].toString() == currentScale) {
            if (obj.contains("drip") && obj.contains("flow")) {
                double drip = obj["drip"].toDouble();
                double flow = obj["flow"].toDouble();
                if (flow > 0.5) {
                    sumLag += drip / flow;
                    count++;
                }
            } else if (obj.contains("lag")) {
                // Old format
                sumLag += obj["lag"].toDouble();
                count++;
            }
        }
    }

    return count > 0 ? sumLag / count : 1.5;
}

void Settings::ensureSawCacheLoaded() const {
    if (!m_sawHistoryCacheDirty) return;
    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    if (data.isEmpty()) {
        m_sawHistoryCache = QJsonArray();
    } else {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        m_sawHistoryCache = doc.array();
    }
    m_sawHistoryCacheDirty = false;
    m_sawConvergedCache = -1;  // Invalidate convergence cache too
}

bool Settings::isSawConverged(const QString& scaleType) const {
    ensureSawCacheLoaded();

    // Return cached result if available and for the same scale
    if (m_sawConvergedCache >= 0 && m_sawConvergedScaleType == scaleType) {
        return m_sawConvergedCache == 1;
    }

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        m_sawConvergedCache = 0;
        m_sawConvergedScaleType = scaleType;
        return false;
    }

    // Collect |overshoot| from last 5 entries for this scale that have overshoot data
    QVector<double> overshoots;
    for (int i = arr.size() - 1; i >= 0 && overshoots.size() < 5; --i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["scale"].toString() == scaleType && obj.contains("overshoot")) {
            overshoots.append(qAbs(obj["overshoot"].toDouble()));
        }
    }

    // Converged = at least 3 entries with avg |overshoot| < 1.5g
    if (overshoots.size() < 3) {
        m_sawConvergedCache = 0;
        m_sawConvergedScaleType = scaleType;
        return false;
    }

    double sum = 0;
    for (double v : overshoots) sum += v;
    bool converged = (sum / overshoots.size()) < 1.5;

    // Divergence detector: if last 3 signed overshoots are all >1g in the same
    // direction, the prediction is systematically off (bean/grind change) — force
    // adaptation mode without requiring manual reset.
    if (converged && overshoots.size() >= 3) {
        QVector<double> signedOvershoots;
        for (int i = arr.size() - 1; i >= 0 && signedOvershoots.size() < 3; --i) {
            QJsonObject obj = arr[i].toObject();
            if (obj["scale"].toString() == scaleType && obj.contains("overshoot")) {
                signedOvershoots.append(obj["overshoot"].toDouble());
            }
        }
        if (signedOvershoots.size() >= 3) {
            bool allPositive = true, allNegative = true;
            for (double v : signedOvershoots) {
                if (v <= 1.0) allPositive = false;
                if (v >= -1.0) allNegative = false;
            }
            if (allPositive || allNegative) {
                qDebug() << "[SAW] Divergence detected: last 3 overshoots all"
                         << (allPositive ? "positive" : "negative") << "- forcing adaptation mode";
                converged = false;
            }
        }
    }

    m_sawConvergedCache = converged ? 1 : 0;
    m_sawConvergedScaleType = scaleType;
    return converged;
}

double Settings::getExpectedDrip(double currentFlowRate) const {
    ensureSawCacheLoaded();

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        // Default: assume 1.5s lag worth of drip
        return currentFlowRate * 1.5;
    }

    // Check convergence state to determine adaptive parameters
    QString currentScale = scaleType();
    bool converged = isSawConverged(currentScale);
    int maxEntries = converged ? 12 : 8;
    double recencyMax = 10.0;
    double recencyMin = converged ? 3.0 : 1.0;  // Steeper recency = faster adaptation

    // Filter to current scale type and collect recent entries
    struct Entry { double drip; double flow; };
    QVector<Entry> entries;

    for (int i = arr.size() - 1; i >= 0 && entries.size() < maxEntries; --i) {
        QJsonObject obj = arr[i].toObject();
        if (obj["scale"].toString() == currentScale) {
            // Support both old format (lag) and new format (drip, flow)
            if (obj.contains("drip")) {
                entries.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
            } else if (obj.contains("lag")) {
                // Convert old lag format: drip = lag * flow (approximate)
                double lag = obj["lag"].toDouble();
                double flow = 4.0;  // Assume average flow for old entries
                entries.append({lag * flow, flow});
            }
        }
    }

    if (entries.isEmpty()) {
        return currentFlowRate * 1.5;  // Default for this scale type
    }

    // Weighted average: weight by recency AND flow similarity
    // Recency weight scales linearly from recencyMax (newest) to recencyMin (oldest)
    // Flow similarity: closer flow = higher weight (gaussian-ish)
    double weightedDripSum = 0;
    double totalWeight = 0;

    for (int i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];

        // Recency weight: linear interpolation from max to min across entry count
        double recencyWeight = recencyMax - i * (recencyMax - recencyMin) / qMax(1, entries.size() - 1);

        // Flow similarity weight: gaussian with sigma=1.5 ml/s
        double flowDiff = qAbs(e.flow - currentFlowRate);
        double flowWeight = qExp(-(flowDiff * flowDiff) / 4.5);  // sigma^2 * 2 = 4.5

        double weight = recencyWeight * flowWeight;
        weightedDripSum += e.drip * weight;
        totalWeight += weight;
    }

    if (totalWeight < 0.01) {
        // All entries have very different flow rates - fall back to default
        return currentFlowRate * 1.5;
    }

    double expectedDrip = weightedDripSum / totalWeight;

    // Clamp to reasonable range (0.5 to 20 grams)
    if (expectedDrip < 0.5) expectedDrip = 0.5;
    if (expectedDrip > 20.0) expectedDrip = 20.0;

    return expectedDrip;
}

QList<QPair<double, double>> Settings::sawLearningEntries(const QString& scaleType, int maxEntries) const {
    ensureSawCacheLoaded();
    QList<QPair<double, double>> result;
    for (int i = m_sawHistoryCache.size() - 1; i >= 0 && result.size() < maxEntries; --i) {
        QJsonObject obj = m_sawHistoryCache[i].toObject();
        if (obj["scale"].toString() == scaleType) {
            if (obj.contains("drip")) {
                result.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
            } else if (obj.contains("lag")) {
                // Convert old lag format: drip ≈ lag * typical_flow
                double lag = obj["lag"].toDouble();
                result.append({lag * 4.0, 4.0});
            }
        }
    }
    return result;
}

void Settings::addSawLearningPoint(double drip, double flowRate, const QString& scaleType, double overshoot) {
    // Validate physical constraints (scale glitches can produce negative values)
    if (drip < 0 || flowRate < 0) {
        qWarning() << "[SAW] Invalid learning point rejected: drip=" << drip << "flow=" << flowRate;
        return;
    }

    // Outlier rejection: when converged, skip learning points that deviate too far
    if (isSawConverged(scaleType)) {
        double expectedDrip = getExpectedDrip(flowRate);
        double threshold = qMax(3.0, expectedDrip);  // Reject if deviation exceeds expected drip (or 3g floor)
        if (qAbs(drip - expectedDrip) > threshold) {
            qDebug() << "[SAW] Outlier rejected: drip=" << drip
                     << "g expected=" << expectedDrip
                     << "g (converged, deviation too high)";
            return;
        }
    }

    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    // Create new entry with drip, flow, and overshoot
    QJsonObject entry;
    entry["drip"] = drip;          // grams that came after stop command
    entry["flow"] = flowRate;      // flow rate when stop was triggered
    entry["scale"] = scaleType;
    entry["overshoot"] = overshoot; // grams over/under target (for convergence detection)
    entry["ts"] = QDateTime::currentSecsSinceEpoch();
    arr.append(entry);

    // Trim to max 50 entries (converged mode uses up to 20, keep extra history)
    while (arr.size() > 50) {
        arr.removeFirst();
    }

    m_settings.setValue("saw/learningHistory", QJsonDocument(arr).toJson());
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    emit sawLearnedLagChanged();
}

void Settings::resetSawLearning() {
    m_settings.remove("saw/learningHistory");
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    emit sawLearnedLagChanged();
}

// ============================================================
// Layout configuration (dynamic IdlePage layout)
// ============================================================

QString Settings::defaultLayoutJson() const {
    QJsonObject layout;
    layout["version"] = 1;

    QJsonObject zones;

    zones["topLeft"] = QJsonArray();
    zones["topRight"] = QJsonArray();
    zones["centerStatus"] = QJsonArray({
        QJsonObject({{"type", "temperature"}, {"id", "temp1"}}),
        QJsonObject({{"type", "waterLevel"}, {"id", "water1"}}),
        QJsonObject({{"type", "connectionStatus"}, {"id", "conn1"}}),
    });
    zones["centerTop"] = QJsonArray({
        QJsonObject({{"type", "espresso"}, {"id", "espresso1"}}),
        QJsonObject({{"type", "steam"}, {"id", "steam1"}}),
        QJsonObject({{"type", "hotwater"}, {"id", "hotwater1"}}),
        QJsonObject({{"type", "flush"}, {"id", "flush1"}}),
    });
    zones["centerMiddle"] = QJsonArray({
        QJsonObject({{"type", "shotPlan"}, {"id", "plan1"}}),
    });
    zones["bottomLeft"] = QJsonArray({
        QJsonObject({{"type", "sleep"}, {"id", "sleep1"}}),
    });
    zones["bottomRight"] = QJsonArray({
        QJsonObject({{"type", "history"}, {"id", "history1"}}),
        QJsonObject({{"type", "spacer"}, {"id", "spacer2"}}),
        QJsonObject({{"type", "beans"}, {"id", "beans1"}}),
        QJsonObject({{"type", "autofavorites"}, {"id", "autofavorites1"}}),
        QJsonObject({{"type", "settings"}, {"id", "settings1"}}),
    });
    zones["statusBar"] = QJsonArray({
        QJsonObject({{"type", "pageTitle"}, {"id", "pagetitle1"}}),
        QJsonObject({{"type", "spacer"}, {"id", "spacer_sb1"}}),
        QJsonObject({{"type", "temperature"}, {"id", "temp_sb1"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb1"}}),
        QJsonObject({{"type", "waterLevel"}, {"id", "water_sb1"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb2"}}),
        QJsonObject({{"type", "scaleWeight"}, {"id", "scale_sb1"}}),
        QJsonObject({{"type", "separator"}, {"id", "sep_sb3"}}),
        QJsonObject({{"type", "connectionStatus"}, {"id", "conn_sb1"}}),
    });

    layout["zones"] = zones;
    return QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact));
}

QJsonObject Settings::getLayoutObject() const {
    if (m_layoutCacheValid)
        return m_layoutCache;

    QString stored = m_settings.value("layout/configuration").toString();
    QJsonObject layout;
    if (stored.isEmpty()) {
        layout = QJsonDocument::fromJson(defaultLayoutJson().toUtf8()).object();
    } else {
        QJsonDocument doc = QJsonDocument::fromJson(stored.toUtf8());
        if (doc.isNull() || !doc.isObject()) {
            layout = QJsonDocument::fromJson(defaultLayoutJson().toUtf8()).object();
        } else {
            layout = doc.object();
        }
    }

    // Migration: ensure statusBar zone exists for configs created before this feature
    QJsonObject zones = layout["zones"].toObject();
    if (!zones.contains("statusBar")) {
        zones["statusBar"] = QJsonArray({
            QJsonObject({{"type", "pageTitle"}, {"id", "pagetitle1"}}),
            QJsonObject({{"type", "spacer"}, {"id", "spacer_sb1"}}),
            QJsonObject({{"type", "temperature"}, {"id", "temp_sb1"}}),
            QJsonObject({{"type", "separator"}, {"id", "sep_sb1"}}),
            QJsonObject({{"type", "waterLevel"}, {"id", "water_sb1"}}),
            QJsonObject({{"type", "separator"}, {"id", "sep_sb2"}}),
            QJsonObject({{"type", "scaleWeight"}, {"id", "scale_sb1"}}),
            QJsonObject({{"type", "separator"}, {"id", "sep_sb3"}}),
            QJsonObject({{"type", "connectionStatus"}, {"id", "conn_sb1"}}),
        });
        layout["zones"] = zones;
    }

    // Migration: rename "text" type to "custom"
    bool textMigrated = false;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item["type"].toString() == "text") {
                item["type"] = "custom";
                items[i] = item;
                textMigrated = true;
            }
        }
        if (textMigrated)
            zones[zoneName] = items;
    }
    if (textMigrated) {
        layout["zones"] = zones;
        // Persist the migration so it only runs once
        const_cast<Settings*>(this)->saveLayoutObject(layout);
    }

    m_layoutCache = layout;
    m_layoutJsonCache = QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact));
    m_layoutCacheValid = true;
    return m_layoutCache;
}

void Settings::invalidateLayoutCache() {
    m_layoutCacheValid = false;
}

void Settings::saveLayoutObject(const QJsonObject& layout) {
    m_layoutCache = layout;
    m_layoutJsonCache = QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact));
    m_layoutCacheValid = true;
    m_settings.setValue("layout/configuration", m_layoutJsonCache);
    emit layoutConfigurationChanged();
}

QString Settings::generateItemId(const QString& type) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    // Find highest existing number for this type across all zones
    int maxNum = 0;
    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            if (item["type"].toString() == type) {
                QString id = item["id"].toString();
                // Extract trailing number from id
                int i = id.length() - 1;
                while (i >= 0 && id[i].isDigit()) --i;
                int num = id.mid(i + 1).toInt();
                if (num > maxNum) maxNum = num;
            }
        }
    }
    // Use type as base, append next number
    // Map type to short prefix for readable IDs
    return type + QString::number(maxNum + 1);
}

QString Settings::layoutConfiguration() const {
    // getLayoutObject() populates both caches (object + JSON string)
    getLayoutObject();
    return m_layoutJsonCache;
}

void Settings::setLayoutConfiguration(const QString& json) {
    invalidateLayoutCache();
    m_settings.setValue("layout/configuration", json);
    emit layoutConfigurationChanged();
}

QVariantList Settings::getZoneItems(const QString& zoneName) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();
    QJsonArray items = zones[zoneName].toArray();

    QVariantList result;
    for (const QJsonValue& val : items) {
        result.append(val.toObject().toVariantMap());
    }
    return result;
}

void Settings::moveItem(const QString& itemId, const QString& fromZone, const QString& toZone, int toIndex) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    // Find and remove from source zone
    QJsonArray fromItems = zones[fromZone].toArray();
    QJsonObject movedItem;
    bool found = false;
    for (int i = 0; i < fromItems.size(); ++i) {
        if (fromItems[i].toObject()["id"].toString() == itemId) {
            movedItem = fromItems[i].toObject();
            fromItems.removeAt(i);
            found = true;
            break;
        }
    }
    if (!found) return;

    zones[fromZone] = fromItems;

    // Insert into target zone
    QJsonArray toItems = zones[toZone].toArray();
    if (toIndex < 0 || toIndex >= toItems.size()) {
        toItems.append(movedItem);
    } else {
        toItems.insert(toIndex, movedItem);
    }
    zones[toZone] = toItems;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void Settings::addItem(const QString& type, const QString& zone, int index) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QString id = generateItemId(type);
    QJsonObject newItem;
    newItem["type"] = type;
    newItem["id"] = id;

    QJsonArray items = zones[zone].toArray();
    if (index < 0 || index >= items.size()) {
        items.append(newItem);
    } else {
        items.insert(index, newItem);
    }
    zones[zone] = items;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void Settings::removeItem(const QString& itemId, const QString& zone) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QJsonArray items = zones[zone].toArray();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].toObject()["id"].toString() == itemId) {
            items.removeAt(i);
            break;
        }
    }
    zones[zone] = items;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void Settings::reorderItem(const QString& zoneName, int fromIndex, int toIndex) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    QJsonArray items = zones[zoneName].toArray();
    if (fromIndex < 0 || fromIndex >= items.size() || toIndex < 0 || toIndex >= items.size() || fromIndex == toIndex) {
        return;
    }

    QJsonValue item = items[fromIndex];
    items.removeAt(fromIndex);
    items.insert(toIndex, item);
    zones[zoneName] = items;

    layout["zones"] = zones;
    saveLayoutObject(layout);
}

void Settings::resetLayoutToDefault() {
    invalidateLayoutCache();
    m_settings.remove("layout/configuration");
    emit layoutConfigurationChanged();
}

bool Settings::hasItemType(const QString& type) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& val : items) {
            if (val.toObject()["type"].toString() == type) {
                return true;
            }
        }
    }
    return false;
}

int Settings::getZoneYOffset(const QString& zoneName) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject offsets = layout["offsets"].toObject();
    int defaultOffset = (zoneName == "centerStatus") ? -65 : 0;
    return offsets[zoneName].toInt(defaultOffset);
}

void Settings::setZoneYOffset(const QString& zoneName, int offset) {
    QJsonObject layout = getLayoutObject();
    QJsonObject offsets = layout["offsets"].toObject();
    offsets[zoneName] = offset;
    layout["offsets"] = offsets;
    saveLayoutObject(layout);
}

double Settings::getZoneScale(const QString& zoneName) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject scales = layout["scales"].toObject();
    return scales[zoneName].toDouble(1.0);
}

void Settings::setZoneScale(const QString& zoneName, double scale) {
    scale = qBound(0.5, scale, 2.0);
    QJsonObject layout = getLayoutObject();
    QJsonObject scales = layout["scales"].toObject();
    scales[zoneName] = scale;
    layout["scales"] = scales;
    saveLayoutObject(layout);
}

void Settings::setItemProperty(const QString& itemId, const QString& key, const QVariant& value) {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item["id"].toString() == itemId) {
                item[key] = QJsonValue::fromVariant(value);
                items[i] = item;
                zones[zoneName] = items;
                layout["zones"] = zones;
                saveLayoutObject(layout);
                return;
            }
        }
    }
}

QVariantMap Settings::getItemProperties(const QString& itemId) const {
    QJsonObject layout = getLayoutObject();
    QJsonObject zones = layout["zones"].toObject();

    for (const QString& zoneName : zones.keys()) {
        QJsonArray items = zones[zoneName].toArray();
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            if (item["id"].toString() == itemId) {
                return item.toVariantMap();
            }
        }
    }
    return QVariantMap();
}

// Device identity
QString Settings::deviceId() const {
    QString id = m_settings.value("device/uuid").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const_cast<QSettings&>(m_settings).setValue("device/uuid", id);
    }
    return id;
}

// Generic settings access
QVariant Settings::value(const QString& key, const QVariant& defaultValue) const {
    return m_settings.value(key, defaultValue);
}

void Settings::setValue(const QString& key, const QVariant& value) {
    m_settings.setValue(key, value);
    emit valueChanged(key);
}

void Settings::factoryReset()
{
    qWarning() << "Settings::factoryReset() - WIPING ALL DATA";

    // 1. Clear primary QSettings (favorites, presets, theme, all preferences)
    m_settings.clear();
    m_settings.sync();

    // Invalidate in-memory caches so getters re-read from (now-empty) QSettings
    m_dyeCacheInitialized = false;

    // 2. Clear secondary QSettings store (used by AI, location, profilestorage)
    QSettings defaultSettings;
    defaultSettings.clear();
    defaultSettings.sync();

    // 3. Delete all data directories under AppDataLocation
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QStringList dataDirs = {
        "profiles",
        "library",
        "skins",
        "translations",
        "screensaver_videos"
    };

    for (const QString& subdir : dataDirs) {
        QDir dir(appDataDir + "/" + subdir);
        if (dir.exists()) {
            qWarning() << "  Removing:" << dir.absolutePath();
            if (!dir.removeRecursively())
                qWarning() << "  WARNING: Failed to completely remove" << dir.absolutePath();
        }
    }

    // 4. Delete shot database files
    QStringList dbFiles = {"shots.db", "shots.db-wal", "shots.db-shm"};
    for (const QString& dbFile : dbFiles) {
        QString path = appDataDir + "/" + dbFile;
        if (QFile::exists(path)) {
            qWarning() << "  Removing:" << path;
            if (!QFile::remove(path))
                qWarning() << "  WARNING: Failed to remove" << path;
        }
    }

    // 5. Delete log files in AppDataLocation
    QStringList logFiles = {"debug.log", "crash.log", "steam_debug.log"};
    for (const QString& logFile : logFiles) {
        QString path = appDataDir + "/" + logFile;
        if (QFile::exists(path)) {
            if (!QFile::remove(path))
                qWarning() << "  WARNING: Failed to remove" << path;
        }
    }

    // 6. Delete public Documents directories
    QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QStringList publicDirs = {"Decenza", "Decenza Backups", "ai_logs"};
    for (const QString& pubDir : publicDirs) {
        QDir dir(docsDir + "/" + pubDir);
        if (dir.exists()) {
            qWarning() << "  Removing:" << dir.absolutePath();
            if (!dir.removeRecursively())
                qWarning() << "  WARNING: Failed to completely remove" << dir.absolutePath();
        }
    }

    // 7. Delete visualizer debug files in Documents
    QStringList debugFiles = {
        docsDir + "/last_upload.json",
        docsDir + "/last_upload_debug.txt",
        docsDir + "/last_upload_response.txt"
    };
    for (const QString& debugFile : debugFiles) {
        if (QFile::exists(debugFile)) {
            if (!QFile::remove(debugFile))
                qWarning() << "  WARNING: Failed to remove" << debugFile;
        }
    }

    // 8. Clear cache directory
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir cache(cacheDir);
    if (cache.exists()) {
        qWarning() << "  Clearing cache:" << cache.absolutePath();
        if (!cache.removeRecursively())
            qWarning() << "  WARNING: Failed to completely clear cache";
    }

    qWarning() << "Settings::factoryReset() - COMPLETE";
}
