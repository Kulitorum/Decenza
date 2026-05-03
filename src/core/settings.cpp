#include "settings.h"
#include "settings_mqtt.h"
#include "settings_autowake.h"
#include "settings_hardware.h"
#include "settings_ai.h"
#include "settings_theme.h"
#include "settings_visualizer.h"
#include "settings_mcp.h"
#include "settings_brew.h"
#include "settings_dye.h"
#include "settings_network.h"
#include "settings_app.h"
#include "settings_calibration.h"
#include "settings_connections.h"
#include "grinderaliases.h"
#include <algorithm>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QUrl>
#include <QtMath>
#include <QColor>
#include <QUuid>
#include <QLocale>
#include <QGuiApplication>
#include <QStyleHints>
#include <QDesktopServices>

#ifdef Q_OS_IOS
#include "screensaver/iosbrightness.h"
#include "SafariViewHelper.h"
#endif

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif

Settings::Settings(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
    , m_mqtt(new SettingsMqtt(this))
    , m_autoWake(new SettingsAutoWake(this))
    , m_hardware(new SettingsHardware(this))
    , m_ai(new SettingsAI(this))
    , m_theme(new SettingsTheme(this))
    , m_visualizer(new SettingsVisualizer(this))
    , m_mcp(new SettingsMcp(this))
    , m_brew(new SettingsBrew(this))
    , m_dye(new SettingsDye(m_visualizer, this))
    , m_network(new SettingsNetwork(this))
    , m_app(new SettingsApp(this))
    , m_calibration(new SettingsCalibration(this, this))
    , m_connections(new SettingsConnections(this))
{
    qDebug() << "Settings: system time format =" << QLocale::system().timeFormat(QLocale::ShortFormat)
             << "-> use12HourTime =" << m_app->use12HourTime();

    // Force a fresh read from persistent storage before checking for missing keys.
    // On macOS, NSUserDefaults can return stale (empty) data if another instance of
    // the app just wrote to the same plist — sync() forces re-read from disk and
    // prevents the defaults-initialization code from overwriting existing settings.
    m_settings.sync();
    qDebug() << "Settings: sync() done, contains profile/favorites:" << m_settings.contains("profile/favorites");

    // Snapshot whether this looks like a fresh install before any default-init
    // blocks below write keys. Used by one-shot migrations that need to behave
    // differently for new users vs upgrades.
    const bool freshInstall = m_settings.allKeys().isEmpty();

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

        QJsonObject dflowQ;
        dflowQ["name"] = "D-Flow / Q";
        dflowQ["filename"] = "d_flow_q";
        defaultFavorites.append(dflowQ);

        m_settings.setValue("profile/favorites", QJsonDocument(defaultFavorites).toJson());
    }

    // Initialize default selected built-in profiles if none exist
    if (!m_settings.contains("profile/selectedBuiltIns")) {
        QStringList defaultSelected;
        defaultSelected << "adaptive_v2"
                        << "blooming_espresso"
                        << "best_overall_pressure_profile"
                        << "flow_profile_for_straight_espresso"
                        << "turbo_shot"
                        << "gentle_and_sweet"
                        << "extractamundo_dos"
                        << "rao_allonge"
                        << "default"
                        << "flow_profile_for_milky_drinks"
                        << "damian_s_lrv2"
                        << "d_flow_default"
                        << "d_flow_q"
                        << "80_s_espresso"
                        << "cremina_lever_machine"
                        << "e61_espresso_machine";
        m_settings.setValue("profile/selectedBuiltIns", defaultSelected);
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

    // One-time migration: auto flow calibration graduates from opt-in beta to default-on.
    // Remove old key so new default (true) applies. Users can still disable via toggle.
    if (!m_settings.contains("calibration/autoFlowCalMigrated")) {
        m_settings.remove("calibration/autoFlowCalibration");
        m_settings.setValue("calibration/autoFlowCalMigrated", true);
        qDebug() << "Settings: Migrated auto flow calibration to default-on";
    }

    // One-time migration: the headless-only "skip purge confirm" toggle was folded
    // into the unified steamTwoTapStop setting. If skipPurgeConfirm was true (user
    // explicitly opted into single-tap on a headless machine) AND the unified key
    // hasn't already been explicitly set via the old calibration popup, preserve
    // their single-tap preference. If both keys coexist with conflicting values,
    // the explicit calibration value wins (it was harder to get to and represents
    // a more deliberate choice). If skipPurgeConfirm was false (two-tap), no value
    // is written here — the seeding migration below will set steamTwoTapStop = true
    // for existing installs to preserve the pre-unification two-tap default.
    if (m_settings.contains("headless/skipPurgeConfirm")) {
        const bool wantedSingleTap = m_settings.value("headless/skipPurgeConfirm").toBool();
        if (wantedSingleTap && !m_settings.contains("calibration/steamTwoTapStop")) {
            m_settings.setValue("calibration/steamTwoTapStop", false);
            qDebug() << "Settings: Migrated headless/skipPurgeConfirm=true -> steamTwoTapStop=false (single-tap)";
        } else {
            qDebug() << "Settings: Removed legacy headless/skipPurgeConfirm key (no value migration needed)";
        }
        m_settings.remove("headless/skipPurgeConfirm");
    }

    // One-time default flip: the new default for steamTwoTapStop is false (single-tap,
    // matching de1app's firmware default of steam_two_tap_stop = 0). Decenza previously
    // defaulted to true (two-tap). For existing installs, seed steamTwoTapStop = true
    // so users who relied on the prior two-tap default by inertia don't see their stop
    // button behavior change on upgrade. Fresh installs skip this and get the new
    // single-tap default.
    if (!m_settings.contains("calibration/steamTwoTapStopDefaultMigrated")) {
        if (!freshInstall && !m_settings.contains("calibration/steamTwoTapStop")) {
            m_settings.setValue("calibration/steamTwoTapStop", true);
            qDebug() << "Settings: Seeded steamTwoTapStop=true for existing install (preserves pre-unification two-tap default)";
        }
        m_settings.setValue("calibration/steamTwoTapStopDefaultMigrated", true);
    }

    // One-time reset: clear all per-profile flow calibrations and reset global to 1.0.
    // The auto-cal algorithm prior to this version had no ratio guards, allowing shots
    // with poor scale data (machine/weight ratio > 1.4) to drag calibrations down to
    // ~0.6 when the correct value is ~0.9-1.0. This corrupted the global median, which
    // in turn poisoned new profiles via inheritance. Reset everything so the improved
    // algorithm (with per-sample and window-level ratio checks) can re-converge cleanly.
    if (!m_settings.contains("calibration/v2RatioGuardReset")) {
        m_calibration->resetAllProfileFlowCalibrations();
        m_calibration->setFlowCalibrationMultiplier(1.0);
        m_settings.setValue("calibration/v2RatioGuardReset", true);
        qDebug() << "Settings: Reset all flow calibrations to 1.0 (v2 ratio guard migration)";
    }

    // One-time reset: clear all per-profile flow calibrations and reset global to 1.0.
    // The v2 algorithm had a feedback loop for flow-controlled profiles: the DE1's PID
    // holds reported flow at the target regardless of calibration, so the formula
    // ideal = factor * weightFlow / (reportedFlow * density) made ideal proportional
    // to the current factor — it could only decrease, never converge. The v3 algorithm
    // uses the profile's target flow directly for flow profiles, breaking the loop.
    // Users who ran v2 may have factors drifted to ~0.6-0.8 instead of ~0.9-1.0.
    if (!m_settings.contains("calibration/v3FlowProfileReset")) {
        m_calibration->resetAllProfileFlowCalibrations();
        m_calibration->setFlowCalibrationMultiplier(1.0);
        m_settings.setValue("calibration/v3FlowProfileReset", true);
        qDebug() << "Settings: Reset all flow calibrations to 1.0 (v3 flow profile feedback loop fix)";
    }

    // Migrate theme/customColors → theme/customColorsDark (one-time, for light/dark mode support)
    if (m_settings.contains("theme/customColors") && !m_settings.contains("theme/customColorsDark")) {
        m_settings.setValue("theme/customColorsDark", m_settings.value("theme/customColors"));
        m_settings.remove("theme/customColors");
        if (!m_settings.contains("theme/mode"))
            m_settings.setValue("theme/mode", "dark");
        qDebug() << "Settings: Migrated theme/customColors → theme/customColorsDark";
    }

    // Migrate user themes: "colors" → "colorsDark"
    QJsonArray migrateUserThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();
    bool userThemesMigrated = false;
    for (qsizetype i = 0; i < migrateUserThemes.size(); ++i) {
        QJsonObject obj = migrateUserThemes[i].toObject();
        if (obj.contains("colors") && !obj.contains("colorsDark")) {
            obj["colorsDark"] = obj["colors"];
            obj.remove("colors");
            migrateUserThemes[i] = obj;
            userThemesMigrated = true;
        }
    }
    if (userThemesMigrated) {
        m_settings.setValue("theme/userThemes", QJsonDocument(migrateUserThemes).toJson(QJsonDocument::Compact));
        qDebug() << "Settings: Migrated user themes colors → colorsDark";
    }

    // Migrate single saved scale to known scales list (one-time)
    if (!m_settings.contains("knownScales/migrated")) {
        QString savedAddress = m_settings.value("scale/address").toString();
        QString savedType = m_settings.value("scale/type").toString();
        QString savedName = m_settings.value("scale/name").toString();
        if (!savedAddress.isEmpty()) {
            QVariantMap scale;
            scale["address"] = savedAddress;
            scale["type"] = savedType;
            scale["name"] = savedName;
            writeKnownScales({scale});
            m_settings.setValue("knownScales/primaryAddress", savedAddress);
            qDebug() << "Settings: Migrated single scale to known scales:" << savedName << savedAddress;
        }
        m_settings.setValue("knownScales/migrated", true);
    }

    // Theme initial state is now resolved inside SettingsTheme

    // Generate MCP API key on first run (avoids const_cast in the getter)
    if (m_settings.value("mcp/apiKey", "").toString().isEmpty()) {
        m_settings.setValue("mcp/apiKey", QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    // Cross-domain wiring: when the user changes the default shot rating
    // (Visualizer settings tab, MCP, settings import), also overwrite the
    // persisted dye/espressoEnjoyment so the new value is reflected in the
    // BrewDialog and on the next shot save — both in-progress and future
    // shots see the change. Pre-split this was a side effect inside
    // Settings::setDefaultShotRating(); it now lives here so any caller
    // of SettingsVisualizer::setDefaultShotRating gets the same behaviour.
    // Bean-modified tracking lives entirely inside SettingsDye now.
    connect(m_visualizer, &SettingsVisualizer::defaultShotRatingChanged, this, [this]() {
        m_dye->setDyeEspressoEnjoyment(m_visualizer->defaultShotRating());
    });

    // Cross-domain wiring: SettingsCalibration::resetSawLearning() emits
    // sawLearningResetRequested so SettingsBrew can reset the hot-water SAW
    // offset state. Sub-objects do not call into other domains directly.
    connect(m_calibration, &SettingsCalibration::sawLearningResetRequested, this, [this]() {
        m_brew->setHotWaterSawOffset(2.0);  // Back to default
        m_brew->setHotWaterSawSampleCount(0);
    });
}

// Domain sub-object QML accessors. Each sub-object IS-A QObject; the upcast
// requires the full type to be visible, hence these live in the .cpp where
// the headers are already included for construction.
QObject* Settings::mqttQObject() const { return m_mqtt; }
QObject* Settings::autoWakeQObject() const { return m_autoWake; }
QObject* Settings::hardwareQObject() const { return m_hardware; }
QObject* Settings::aiQObject() const { return m_ai; }
QObject* Settings::themeQObject() const { return m_theme; }
QObject* Settings::visualizerQObject() const { return m_visualizer; }
QObject* Settings::mcpQObject() const { return m_mcp; }
QObject* Settings::brewQObject() const { return m_brew; }
QObject* Settings::dyeQObject() const { return m_dye; }
QObject* Settings::networkQObject() const { return m_network; }
QObject* Settings::appQObject() const { return m_app; }
QObject* Settings::calibrationQObject() const { return m_calibration; }
QObject* Settings::connectionsQObject() const { return m_connections; }

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

bool Settings::keepScaleOn() const {
    // Default true preserves legacy Decenza behaviour (scale stays BLE-connected
    // when the DE1 sleeps). Set to false to mirror de1app's default of powering
    // off and disconnecting the scale — useful for battery-only scales.
    return m_settings.value("scale/keepOn", true).toBool();
}

void Settings::setKeepScaleOn(bool keep) {
    if (keepScaleOn() != keep) {
        m_settings.setValue("scale/keepOn", keep);
        emit keepScaleOnChanged();
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

// Multi-scale management
QVariantList Settings::knownScales() const {
    QVariantList result;
    QString primary = primaryScaleAddress();
    qsizetype count = m_settings.beginReadArray("knownScales/scales");
    for (qsizetype i = 0; i < count; ++i) {
        m_settings.setArrayIndex(static_cast<int>(i));
        QVariantMap scale;
        scale["address"] = m_settings.value("address").toString();
        scale["type"] = m_settings.value("type").toString();
        scale["name"] = m_settings.value("name").toString();
        scale["isPrimary"] = (scale["address"].toString().compare(primary, Qt::CaseInsensitive) == 0);
        result.append(scale);
    }
    m_settings.endArray();
    return result;
}

void Settings::addKnownScale(const QString& address, const QString& type, const QString& name) {
    if (address.isEmpty()) return;

    // Read existing scales
    QVariantList scales = knownScales();

    // Check for existing entry — update name/type if found
    for (qsizetype i = 0; i < scales.size(); ++i) {
        QVariantMap s = scales[i].toMap();
        if (s["address"].toString().compare(address, Qt::CaseInsensitive) == 0) {
            if (s["type"].toString() != type || s["name"].toString() != name) {
                s["type"] = type;
                s["name"] = name;
                scales[i] = s;
                writeKnownScales(scales);
            }
            return;
        }
    }

    // Add new scale
    QVariantMap newScale;
    newScale["address"] = address;
    newScale["type"] = type;
    newScale["name"] = name;
    newScale["isPrimary"] = false;
    scales.append(newScale);
    writeKnownScales(scales);
}

void Settings::removeKnownScale(const QString& address) {
    QVariantList scales = knownScales();
    bool wasPrimary = (primaryScaleAddress().compare(address, Qt::CaseInsensitive) == 0);

    scales.erase(std::remove_if(scales.begin(), scales.end(), [&](const QVariant& v) {
        return v.toMap()["address"].toString().compare(address, Qt::CaseInsensitive) == 0;
    }), scales.end());

    writeKnownScales(scales);

    if (wasPrimary) {
        // Clear primary — if there are remaining scales, don't auto-promote
        m_settings.setValue("knownScales/primaryAddress", QString());
        // Also clear legacy scale/address so existing code stays in sync
        setScaleAddress(QString());
        setScaleType(QString());
        setScaleName(QString());
        // Re-emit so QML sees the cleared primaryScaleAddress
        emit knownScalesChanged();
    }
}

void Settings::setPrimaryScale(const QString& address) {
    if (primaryScaleAddress().compare(address, Qt::CaseInsensitive) == 0)
        return;

    m_settings.setValue("knownScales/primaryAddress", address);

    // Sync legacy scale/* keys for backward compat
    QVariantList scales = knownScales();
    for (const QVariant& v : scales) {
        QVariantMap s = v.toMap();
        if (s["address"].toString().compare(address, Qt::CaseInsensitive) == 0) {
            setScaleAddress(address);
            setScaleType(s["type"].toString());
            setScaleName(s["name"].toString());
            break;
        }
    }

    emit knownScalesChanged();
}

QString Settings::primaryScaleAddress() const {
    return m_settings.value("knownScales/primaryAddress", "").toString();
}

bool Settings::isKnownScale(const QString& address) const {
    qsizetype count = m_settings.beginReadArray("knownScales/scales");
    for (qsizetype i = 0; i < count; ++i) {
        m_settings.setArrayIndex(static_cast<int>(i));
        if (m_settings.value("address").toString().compare(address, Qt::CaseInsensitive) == 0) {
            m_settings.endArray();
            return true;
        }
    }
    m_settings.endArray();
    return false;
}

void Settings::writeKnownScales(const QVariantList& scales) {
    m_settings.beginWriteArray("knownScales/scales", static_cast<int>(scales.size()));
    for (qsizetype i = 0; i < scales.size(); ++i) {
        m_settings.setArrayIndex(static_cast<int>(i));
        QVariantMap s = scales[i].toMap();
        m_settings.setValue("address", s["address"]);
        m_settings.setValue("type", s["type"]);
        m_settings.setValue("name", s["name"]);
    }
    m_settings.endArray();
    emit knownScalesChanged();
}

// FlowScale
bool Settings::useFlowScale() const {
    return m_settings.value("flow/useFlowScale", false).toBool();
}

void Settings::setUseFlowScale(bool enabled) {
    if (useFlowScale() != enabled) {
        m_settings.setValue("flow/useFlowScale", enabled);
        emit useFlowScaleChanged();
    }
}

// Scale connection alert dialogs
bool Settings::showScaleDialogs() const {
    return m_settings.value("scale/showDialogs", true).toBool();
}

void Settings::setShowScaleDialogs(bool enabled) {
    if (showScaleDialogs() != enabled) {
        m_settings.setValue("scale/showDialogs", enabled);
        emit showScaleDialogsChanged();
    }
}

// Refractometer
QString Settings::savedRefractometerAddress() const {
    return m_settings.value("refractometer/address", "").toString();
}

void Settings::setSavedRefractometerAddress(const QString& address) {
    if (savedRefractometerAddress() != address) {
        m_settings.setValue("refractometer/address", address);
        emit savedRefractometerChanged();
    }
}

QString Settings::savedRefractometerName() const {
    return m_settings.value("refractometer/name", "").toString();
}

void Settings::setSavedRefractometerName(const QString& name) {
    if (savedRefractometerName() != name) {
        m_settings.setValue("refractometer/name", name);
        emit savedRefractometerChanged();
    }
}

// USB serial polling
bool Settings::usbSerialEnabled() const {
    return m_settings.value("usb/serialEnabled", false).toBool();
}

void Settings::setUsbSerialEnabled(bool enabled) {
    if (usbSerialEnabled() != enabled) {
        m_settings.setValue("usb/serialEnabled", enabled);
        emit usbSerialEnabledChanged();
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

bool Settings::boolValue(const QString& key, bool defaultValue) const {
    const QVariant v = m_settings.value(key);
    if (!v.isValid()) return defaultValue;
    // QVariant::toBool() handles native bool, int, and the strings "true"/"false"/"1"/"0".
    // We bypass the plain value() path because that returns the raw QVariant (often a
    // QString on INI-backed QSettings) to QML, where JavaScript truthiness on "false"
    // yields true — causing silent persistence bugs.
    return v.toBool();
}

void Settings::factoryReset()
{
    qWarning() << "Settings::factoryReset() - WIPING ALL DATA";

    // 1. Clear primary QSettings (favorites, presets, theme, all preferences)
    m_settings.clear();
    m_settings.sync();

    // Invalidate in-memory caches so getters re-read from (now-empty) QSettings
    m_dye->invalidateCache();
    m_calibration->invalidateCache();

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
    QStringList publicDirs = {"Decenza", "ai_logs"};
    // Note: "Decenza Backups" is intentionally NOT deleted — backups are preserved
    // so the user can restore data on a fresh install if needed.
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
