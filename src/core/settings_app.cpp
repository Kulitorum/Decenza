#include "settings_app.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QUuid>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif

SettingsApp::SettingsApp(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
    , m_use12HourTime(QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive))
{
}

// Platform capabilities
bool SettingsApp::hasQuick3D() const {
#ifdef HAVE_QUICK3D
    return true;
#else
    return false;
#endif
}

bool SettingsApp::isDebugBuild() const {
#ifdef QT_DEBUG
    return true;
#else
    return false;
#endif
}

// Launcher mode (Android only)
bool SettingsApp::launcherMode() const {
    return m_settings.value("app/launcherMode", false).toBool();
}

void SettingsApp::setLauncherMode(bool enabled) {
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

// Profile favorites
QVariantList SettingsApp::favoriteProfiles() const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    QVariantList result;
    for (const QJsonValue& v : arr) {
        result.append(v.toObject().toVariantMap());
    }
    return result;
}

int SettingsApp::selectedFavoriteProfile() const {
    return m_settings.value("profile/selectedFavorite", -1).toInt();
}

void SettingsApp::setSelectedFavoriteProfile(int index) {
    if (selectedFavoriteProfile() != index) {
        qDebug() << "setSelectedFavoriteProfile:" << selectedFavoriteProfile() << "->" << index;
        m_settings.setValue("profile/selectedFavorite", index);
        emit selectedFavoriteProfileChanged();
    }
}

void SettingsApp::addFavoriteProfile(const QString& name, const QString& filename) {
    qDebug() << "SettingsApp: addFavoriteProfile name=" << name << "filename=" << filename;
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

    // Ensure consistency: un-hide and select the profile when favoriting it
    removeHiddenProfile(filename);
    addSelectedBuiltInProfile(filename);

    QJsonObject favorite;
    favorite["name"] = name;
    favorite["filename"] = filename;
    arr.append(favorite);

    m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());

    // If the newly added favorite is the currently active profile, sync the selected index
    if (currentProfile() == filename) {
        setSelectedFavoriteProfile(static_cast<int>(arr.size()) - 1);
    }

    emit favoriteProfilesChanged();
}

void SettingsApp::removeFavoriteProfile(int index) {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QString filename = arr[index].toObject()["filename"].toString();
        qDebug() << "SettingsApp: removeFavoriteProfile index=" << index << "filename=" << filename;
        arr.removeAt(index);
        m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());

        // Adjust selected if needed
        int selected = selectedFavoriteProfile();
        if (selected >= arr.size() && arr.size() > 0) {
            setSelectedFavoriteProfile(static_cast<int>(arr.size()) - 1);
        } else if (arr.size() == 0) {
            setSelectedFavoriteProfile(-1);
        }

        emit favoriteProfilesChanged();
    }
}

void SettingsApp::moveFavoriteProfile(int from, int to) {
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

QVariantMap SettingsApp::getFavoriteProfile(int index) const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        return arr[index].toObject().toVariantMap();
    }
    return QVariantMap();
}

bool SettingsApp::isFavoriteProfile(const QString& filename) const {
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

int SettingsApp::findFavoriteIndexByFilename(const QString& filename) const {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    for (int i = 0; i < arr.size(); ++i) {
        QString favFilename = arr[i].toObject()["filename"].toString();
        if (favFilename == filename) {
            return i;
        }
    }
    return -1;
}

bool SettingsApp::updateFavoriteProfile(const QString& oldFilename, const QString& newFilename, const QString& newTitle) {
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
QStringList SettingsApp::selectedBuiltInProfiles() const {
    return m_settings.value("profile/selectedBuiltIns").toStringList();
}

void SettingsApp::setSelectedBuiltInProfiles(const QStringList& profiles) {
    if (selectedBuiltInProfiles() != profiles) {
        m_settings.setValue("profile/selectedBuiltIns", profiles);
        emit selectedBuiltInProfilesChanged();
    }
}

void SettingsApp::addSelectedBuiltInProfile(const QString& filename) {
    QStringList current = selectedBuiltInProfiles();
    if (!current.contains(filename)) {
        current.append(filename);
        m_settings.setValue("profile/selectedBuiltIns", current);
        emit selectedBuiltInProfilesChanged();
    }
}

void SettingsApp::removeSelectedBuiltInProfile(const QString& filename) {
    QStringList current = selectedBuiltInProfiles();
    if (current.removeAll(filename) > 0) {
        m_settings.setValue("profile/selectedBuiltIns", current);
        emit selectedBuiltInProfilesChanged();

        // Also remove from favorites if it was a favorite
        if (isFavoriteProfile(filename)) {
            QByteArray data = m_settings.value("profile/favorites").toByteArray();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonArray arr = doc.array();

            for (qsizetype i = arr.size() - 1; i >= 0; --i) {
                if (arr[i].toObject()["filename"].toString() == filename) {
                    arr.removeAt(i);
                    break;
                }
            }

            m_settings.setValue("profile/favorites", QJsonDocument(arr).toJson());

            // Adjust selected favorite if needed
            int selected = selectedFavoriteProfile();
            if (selected >= arr.size() && arr.size() > 0) {
                setSelectedFavoriteProfile(static_cast<int>(arr.size()) - 1);
            }

            emit favoriteProfilesChanged();
        }
    }
}

bool SettingsApp::isSelectedBuiltInProfile(const QString& filename) const {
    return selectedBuiltInProfiles().contains(filename);
}

// Hidden profiles
QStringList SettingsApp::hiddenProfiles() const {
    return m_settings.value("profile/hiddenProfiles").toStringList();
}

void SettingsApp::setHiddenProfiles(const QStringList& profiles) {
    if (hiddenProfiles() != profiles) {
        m_settings.setValue("profile/hiddenProfiles", profiles);
        emit hiddenProfilesChanged();
    }
}

void SettingsApp::addHiddenProfile(const QString& filename) {
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

            for (qsizetype i = arr.size() - 1; i >= 0; --i) {
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
                setSelectedFavoriteProfile(static_cast<int>(arr.size()) - 1);
            }

            emit favoriteProfilesChanged();
        }
    }
}

void SettingsApp::removeHiddenProfile(const QString& filename) {
    QStringList current = hiddenProfiles();
    if (current.removeAll(filename) > 0) {
        m_settings.setValue("profile/hiddenProfiles", current);
        emit hiddenProfilesChanged();
    }
}

bool SettingsApp::isHiddenProfile(const QString& filename) const {
    return hiddenProfiles().contains(filename);
}

// Current profile
QString SettingsApp::currentProfile() const {
    return m_settings.value("profile/current", "Adaptive v2").toString();
}

void SettingsApp::setCurrentProfile(const QString& profile) {
    if (currentProfile() != profile) {
        m_settings.setValue("profile/current", profile);
        emit currentProfileChanged();
    }
}

// Auto-update
bool SettingsApp::autoCheckUpdates() const {
    return m_settings.value("updates/autoCheck", true).toBool();
}

void SettingsApp::setAutoCheckUpdates(bool enabled) {
    if (autoCheckUpdates() != enabled) {
        m_settings.setValue("updates/autoCheck", enabled);
        emit autoCheckUpdatesChanged();
    }
}

bool SettingsApp::betaUpdatesEnabled() const {
    return m_settings.value("updates/betaEnabled", false).toBool();
}

void SettingsApp::setBetaUpdatesEnabled(bool enabled) {
    if (betaUpdatesEnabled() != enabled) {
        m_settings.setValue("updates/betaEnabled", enabled);
        emit betaUpdatesEnabledChanged();
    }
}

bool SettingsApp::firmwareNightlyChannel() const {
    return m_settings.value("firmware/nightlyChannel", false).toBool();
}

void SettingsApp::setFirmwareNightlyChannel(bool enabled) {
    if (firmwareNightlyChannel() != enabled) {
        m_settings.setValue("firmware/nightlyChannel", enabled);
        emit firmwareNightlyChannelChanged();
    }
}

// Daily backup
int SettingsApp::dailyBackupHour() const {
    return m_settings.value("backup/dailyBackupHour", -1).toInt();  // -1 = off
}

void SettingsApp::setDailyBackupHour(int hour) {
    if (dailyBackupHour() != hour) {
        m_settings.setValue("backup/dailyBackupHour", hour);
        emit dailyBackupHourChanged();
    }
}

// Water level / refill
QString SettingsApp::waterLevelDisplayUnit() const {
    return m_settings.value("display/waterLevelUnit", "percent").toString();
}

void SettingsApp::setWaterLevelDisplayUnit(const QString& unit) {
    if (waterLevelDisplayUnit() != unit) {
        m_settings.setValue("display/waterLevelUnit", unit);
        emit waterLevelDisplayUnitChanged();
    }
}

int SettingsApp::waterRefillPoint() const {
    return m_settings.value("water/refillPoint", 5).toInt();
}

void SettingsApp::setWaterRefillPoint(int mm) {
    if (waterRefillPoint() != mm) {
        m_settings.setValue("water/refillPoint", mm);
        emit waterRefillPointChanged();
    }
}

int SettingsApp::refillKitOverride() const {
    return m_settings.value("water/refillKitOverride", 2).toInt();  // Default: auto-detect
}

void SettingsApp::setRefillKitOverride(int value) {
    if (refillKitOverride() != value) {
        m_settings.setValue("water/refillKitOverride", value);
        emit refillKitOverrideChanged();
    }
}

// Developer settings
bool SettingsApp::developerTranslationUpload() const {
    // Runtime-only flag — not persisted, resets to false on app restart
    return m_developerTranslationUpload;
}

void SettingsApp::setDeveloperTranslationUpload(bool enabled) {
    if (m_developerTranslationUpload != enabled) {
        m_developerTranslationUpload = enabled;
        emit developerTranslationUploadChanged();
    }
}

bool SettingsApp::simulationMode() const {
#if defined(QT_DEBUG) && (defined(Q_OS_WIN) || defined(Q_OS_MACOS))
    return m_settings.value("developer/simulationMode", true).toBool();
#else
    return m_settings.value("developer/simulationMode", false).toBool();
#endif
}

void SettingsApp::setSimulationMode(bool enabled) {
    if (simulationMode() != enabled) {
        m_settings.setValue("developer/simulationMode", enabled);
        emit simulationModeChanged();
    }
}

bool SettingsApp::hideGhcSimulator() const {
    return m_settings.value("developer/hideGhcSimulator", false).toBool();
}

void SettingsApp::setHideGhcSimulator(bool hide) {
    if (hideGhcSimulator() != hide) {
        m_settings.setValue("developer/hideGhcSimulator", hide);
        emit hideGhcSimulatorChanged();
    }
}

bool SettingsApp::simulatedScaleEnabled() const {
    return m_settings.value("developer/simulatedScaleEnabled", true).toBool();
}

void SettingsApp::setSimulatedScaleEnabled(bool enabled) {
    if (simulatedScaleEnabled() != enabled) {
        m_settings.setValue("developer/simulatedScaleEnabled", enabled);
        emit simulatedScaleEnabledChanged();
    }
}

bool SettingsApp::screenCaptureEnabled() const {
    return m_settings.value("machine/screenCaptureEnabled", false).toBool();
}

void SettingsApp::setScreenCaptureEnabled(bool enabled) {
    if (screenCaptureEnabled() != enabled) {
        m_settings.setValue("machine/screenCaptureEnabled", enabled);
        emit screenCaptureEnabledChanged();
    }
}

// Device identity
QString SettingsApp::deviceId() const {
    QString id = m_settings.value("device/uuid").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const_cast<QSettings&>(m_settings).setValue("device/uuid", id);
    }
    return id;
}

// Pocket app pairing
QString SettingsApp::pocketPairingToken() const {
    return m_settings.value("pocket/pairingToken").toString();
}

void SettingsApp::setPocketPairingToken(const QString& token) {
    m_settings.setValue("pocket/pairingToken", token);
}
