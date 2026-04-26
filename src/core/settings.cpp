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
        savePerProfileFlowCalMap(QJsonObject());
        setFlowCalibrationMultiplier(1.0);
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
        savePerProfileFlowCalMap(QJsonObject());
        setFlowCalibrationMultiplier(1.0);
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


// Flow calibration

double Settings::flowCalibrationMultiplier() const {
    return m_settings.value("calibration/flowMultiplier", 1.0).toDouble();
}

void Settings::setFlowCalibrationMultiplier(double multiplier) {
    // Upper bound bumped 2.0 → 3.0 to match DE1 firmware v1337 (de1app parity).
    multiplier = qBound(0.35, multiplier, 3.0);
    if (qAbs(flowCalibrationMultiplier() - multiplier) > 0.001) {
        m_settings.setValue("calibration/flowMultiplier", multiplier);
        emit flowCalibrationMultiplierChanged();
    }
}

// Auto flow calibration

bool Settings::autoFlowCalibration() const {
    return m_settings.value("calibration/autoFlowCalibration", true).toBool();
}

void Settings::setAutoFlowCalibration(bool enabled) {
    if (autoFlowCalibration() != enabled) {
        m_settings.setValue("calibration/autoFlowCalibration", enabled);
        emit autoFlowCalibrationChanged();
    }
}

double Settings::profileFlowCalibration(const QString& profileFilename) const {
    QJsonObject map = allProfileFlowCalibrations();
    if (map.contains(profileFilename)) {
        return map[profileFilename].toDouble();
    }
    return 0.0;
}

bool Settings::setProfileFlowCalibration(const QString& profileFilename, double multiplier) {
    if (profileFilename.isEmpty()) {
        qWarning() << "Settings: setProfileFlowCalibration called with empty profile filename";
        return false;
    }
    // Sanity bounds — persistence accepts [0.5, 2.7] to match the highest value the
    // runtime auto-cal algorithm can produce (kCalibrationMax on v1337+ firmware).
    // MainController::computeAutoFlowCalibration applies a tighter firmware-version-
    // dependent ceiling (1.8 on older firmware, 2.7 on v1337+). Persistence just
    // prevents obviously-corrupt values.
    if (multiplier < 0.5 || multiplier > 2.7) {
        qWarning() << "Settings: rejecting per-profile flow calibration"
                   << multiplier << "for" << profileFilename << "(outside [0.5, 2.7])";
        return false;
    }
    QJsonObject map = allProfileFlowCalibrations();
    map[profileFilename] = multiplier;
    savePerProfileFlowCalMap(map);
    return true;
}

void Settings::clearProfileFlowCalibration(const QString& profileFilename) {
    if (profileFilename.isEmpty()) {
        qWarning() << "Settings: clearProfileFlowCalibration called with empty profile filename";
        return;
    }
    QJsonObject map = allProfileFlowCalibrations();
    map.remove(profileFilename);
    savePerProfileFlowCalMap(map);
    // Clear any pending batch ideals — they were computed at the old C value
    clearFlowCalPendingIdeals(profileFilename);
}

double Settings::effectiveFlowCalibration(const QString& profileFilename) const {
    if (autoFlowCalibration()) {
        double perProfile = profileFlowCalibration(profileFilename);
        if (perProfile > 0.0) {
            return perProfile;
        }
    }
    return flowCalibrationMultiplier();
}

bool Settings::hasProfileFlowCalibration(const QString& profileFilename) const {
    if (!autoFlowCalibration()) return false;
    QJsonObject map = allProfileFlowCalibrations();
    return map.contains(profileFilename);
}

QJsonObject Settings::allProfileFlowCalibrations() const {
    if (m_perProfileFlowCalCacheValid)
        return m_perProfileFlowCalCache;

    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("calibration/perProfileFlow", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Settings: corrupt perProfileFlow JSON:" << parseError.errorString()
                   << "- raw data:" << m_settings.value("calibration/perProfileFlow").toByteArray().left(200)
                   << "- per-profile flow calibrations lost";
        // Clear the corrupt data so it doesn't persist and cause repeated warnings
        const_cast<QSettings&>(m_settings).setValue("calibration/perProfileFlow", "{}");
        map = QJsonObject();
    }
    // Cache the result (even if empty from corruption) to prevent repeated re-parsing.
    // INVARIANT: All modifications to "calibration/perProfileFlow" in QSettings
    // MUST go through savePerProfileFlowCalMap() to maintain cache consistency.
    m_perProfileFlowCalCache = map;
    m_perProfileFlowCalCacheValid = true;
    return m_perProfileFlowCalCache;
}

void Settings::savePerProfileFlowCalMap(const QJsonObject& map) {
    m_settings.setValue("calibration/perProfileFlow", QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileFlowCalCache = map;
    m_perProfileFlowCalCacheValid = true;
    m_perProfileFlowCalVersion++;
    emit perProfileFlowCalibrationChanged();
}

// Auto flow calibration batch accumulator

static QJsonObject parseFlowCalBatch(const QSettings& settings) {
    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        settings.value("calibration/flowCalBatch", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Settings: corrupt flowCalBatch JSON:" << parseError.errorString();
        const_cast<QSettings&>(settings).setValue("calibration/flowCalBatch", "{}");
        return QJsonObject();
    }
    return map;
}

QVector<double> Settings::flowCalPendingIdeals(const QString& profileFilename) const {
    QJsonObject map = parseFlowCalBatch(m_settings);
    QVector<double> result;
    QJsonArray arr = map.value(profileFilename).toArray();
    for (const auto& v : arr)
        result.append(v.toDouble());
    return result;
}

void Settings::appendFlowCalPendingIdeal(const QString& profileFilename, double ideal) {
    QJsonObject map = parseFlowCalBatch(m_settings);
    QJsonArray arr = map.value(profileFilename).toArray();
    arr.append(ideal);
    map[profileFilename] = arr;
    m_settings.setValue("calibration/flowCalBatch", QJsonDocument(map).toJson(QJsonDocument::Compact));
}

void Settings::clearFlowCalPendingIdeals(const QString& profileFilename) {
    QJsonObject map = parseFlowCalBatch(m_settings);
    map.remove(profileFilename);
    m_settings.setValue("calibration/flowCalBatch", QJsonDocument(map).toJson(QJsonDocument::Compact));
}

// SAW (Stop-at-Weight) learning

// Minimum committed batch-medians before a (profile, scale) pair graduates from
// the global fallbacks (globalBootstrap / globalPool / scaleDefault) to its own
// per-pair model. Each median represents 5 SAW shots that survived the IQR
// dispersion gate, so 2 medians = 10 shots — already a stronger signal than the
// legacy single-shot global pool it replaces. Trade-off: smaller = faster to
// adapt to per-profile drip dynamics; larger = more stability against early
// regime bias. See docs/CLAUDE_MD/SAW_LEARNING.md.
static constexpr int kSawMinMediansForGraduation = 2;

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

    for (qsizetype i = arr.size() - 1; i >= 0 && count < 5; --i) {
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
    for (qsizetype i = arr.size() - 1; i >= 0 && overshoots.size() < 5; --i) {
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
        for (qsizetype i = arr.size() - 1; i >= 0 && signedOvershoots.size() < 3; --i) {
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

    // Read scale type once — consistent across all fallback paths.
    const QString currentScale = scaleType();

    const QJsonArray& arr = m_sawHistoryCache;
    if (arr.isEmpty()) {
        // No history at all — use scale-specific sensor lag as first-shot default.
        // Formula: flow × (sensor_lag + 0.1s DE1 machine lag), capped at 8g.
        // Matches de1app's first-shot behaviour (lag_time_estimation=0 before learning).
        return qMin(currentFlowRate * (sensorLag(currentScale) + 0.1), 8.0);
    }

    // Check convergence state to determine adaptive parameters
    bool converged = isSawConverged(currentScale);
    int maxEntries = converged ? 12 : 8;
    double recencyMax = 10.0;
    double recencyMin = converged ? 3.0 : 1.0;  // Steeper recency = faster adaptation

    // Filter to current scale type and collect recent entries
    struct Entry { double drip; double flow; };
    QVector<Entry> entries;

    for (qsizetype i = arr.size() - 1; i >= 0 && entries.size() < maxEntries; --i) {
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
        return qMin(currentFlowRate * (sensorLag(currentScale) + 0.1), 8.0);  // No entries for this scale type
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
        // All entries have very different flow rates — fall back to sensor-lag default.
        return qMin(currentFlowRate * (sensorLag(currentScale) + 0.1), 8.0);
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
    for (qsizetype i = m_sawHistoryCache.size() - 1; i >= 0 && result.size() < maxEntries; --i) {
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

double Settings::sensorLag(const QString& scaleType)
{
    // BLE sensor lag per scale type, taken from de1app device_scale.tcl documentation.
    // Used as the first-shot SAW default before adaptive learning has any data.
    // The +0.1s added at call sites is the DE1 machine-side stop-command execution lag
    // (separate from BLE round-trip lag), keeping this value scale-specific only.
    if (scaleType == "Bookoo")           return 0.50;
    if (scaleType == "Acaia")            return 0.69;
    if (scaleType == "Acaia Pyxis")      return 0.69;  // Same Acaia BLE protocol
    if (scaleType == "Felicita")         return 0.50;
    if (scaleType == "Atomheart Eclair") return 0.50;
    if (scaleType == "Hiroia Jimmy")     return 0.25;
    if (scaleType == "Decent Scale")     return 0.38;
    if (scaleType == "Skale")            return 0.38;
    if (scaleType == "decent")           return 0.38;  // QSettings default before any scale is paired
    qWarning() << "[SAW] Unknown scale type for sensorLag:" << scaleType << "- using default 0.38s";
    return 0.38;  // de1app default for unknown/unlisted scales
}

void Settings::addSawLearningPoint(double drip, double flowRate, const QString& scaleType,
                                   double overshoot, const QString& profileFilename) {
    // Validate physical constraints (scale glitches can produce negative values)
    if (drip < 0 || flowRate < 0) {
        qWarning() << "[SAW] Invalid learning point rejected: drip=" << drip << "flow=" << flowRate;
        return;
    }

    // Reject physically implausible entries: implied lag > 4s is beyond any real BLE
    // scale (BLE round-trip + machine stop + final drip ≈ 3.5s worst case).
    // The flowRate > 0.5 guard prevents division-by-near-zero making the ratio meaningless
    // at very low flow (e.g. 0.1 g/s would flag a 0.39g drip as "too high").
    if (flowRate > 0.5 && drip / flowRate > 4.0) {
        qWarning() << "[SAW] Implied lag too high (" << drip / flowRate
                   << "s), skipping learning: drip=" << drip << "flow=" << flowRate;
        return;
    }

    // Outlier rejection: when converged, skip learning points that deviate too far.
    // Skipped when overshoot < -6g (auto-reset candidate): the model may be systematically
    // wrong and must accept the new baseline rather than defending the stale converged model.
    bool isAutoResetCandidate = (overshoot < -6.0);
    if (!isAutoResetCandidate && isSawConverged(scaleType)) {
        double expectedDrip = getExpectedDripFor(profileFilename, scaleType, flowRate);
        double threshold = qMax(3.0, expectedDrip);  // Reject if deviation exceeds expected drip (or 3g floor)
        if (qAbs(drip - expectedDrip) > threshold) {
            qWarning() << "[SAW] Outlier rejected: drip=" << drip
                       << "g expected=" << expectedDrip
                       << "g threshold=" << threshold
                       << "(converged, deviation too high)";
            return;
        }
    }

    // When called with a profile filename, route through the per-(profile, scale) batch
    // accumulator. The pending batch holds 5 shots before committing the median to the
    // per-pair history AND the global pool — this reduces churn from individual shots
    // and provides outlier rejection via the median + IQR check. See AUTO_FLOW_CALIBRATION
    // for the same pattern applied to flow cal.
    if (!profileFilename.isEmpty()) {
        addSawPerPairEntry(drip, flowRate, scaleType, overshoot, profileFilename);
        return;
    }

    // Legacy path (profile unknown): append directly to the global pool. Preserves
    // existing behaviour for callers that have not been updated to pass a profile.
    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    QJsonArray arr;
    if (!data.isEmpty()) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (doc.isArray()) {
            arr = doc.array();
        } else {
            qWarning() << "[SAW] Learning history corrupted, starting fresh:" << parseError.errorString();
        }
    }

    // Auto-reset: if this shot stopped 6g+ early (current) AND the most recent
    // prior entry for this scale type also stopped 6g+ early, the learning is
    // stuck producing too-aggressive thresholds. Clear history and start fresh
    // so the new entry becomes the sole baseline. Entries from other scale types
    // are skipped when searching backwards — "consecutive" means consecutive for
    // this scale type only.
    // NOTE: execution always falls through to the append+save below — do NOT add
    // an early return here, or the reset will wipe history without saving anything.
    if (isAutoResetCandidate) {
        bool prevAlsoEarly = false;
        for (qsizetype i = arr.size() - 1; i >= 0; --i) {
            QJsonObject obj = arr[i].toObject();
            if (obj["scale"].toString() == scaleType) {
                prevAlsoEarly = (obj["overshoot"].toDouble() < -6.0);
                break;  // Only check the most recent entry for this scale type
            }
        }
        if (prevAlsoEarly) {
            qWarning() << "[SAW] 2nd consecutive early stop for" << scaleType
                       << "- resetting learning (both shots overshoot <-6g)";
            // Remove all entries for this scale type, preserving other scales.
            // The new entry will be appended below and becomes the fresh baseline.
            QJsonArray filtered;
            for (int i = 0; i < arr.size(); ++i) {
                if (arr[i].toObject()["scale"].toString() != scaleType)
                    filtered.append(arr[i]);
            }
            arr = filtered;
        }
    }

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
    m_settings.remove("saw/perProfileHistory");
    m_settings.remove("saw/perProfileBatch");
    m_settings.remove("saw/globalBootstrapLag");
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    m_perProfileSawHistoryCacheValid = false;
    m_perProfileSawBatchCacheValid = false;
    qDebug() << "[SAW] reset all SAW learning";
    emit sawLearnedLagChanged();

    // Also reset hot water SAW learning
    m_brew->setHotWaterSawOffset(2.0);  // Back to default
    m_brew->setHotWaterSawSampleCount(0);
}

void Settings::resetSawLearningForProfile(const QString& profileFilename, const QString& scaleType) {
    if (profileFilename.isEmpty()) {
        qWarning() << "[SAW] resetSawLearningForProfile called with empty profile";
        return;
    }
    const QString key = sawPairKey(profileFilename, scaleType);
    QJsonObject historyMap = loadPerProfileSawHistoryMap();
    QJsonObject batchMap = loadPerProfileSawBatchMap();
    bool changed = false;
    if (historyMap.contains(key)) {
        historyMap.remove(key);
        savePerProfileSawHistoryMap(historyMap);
        changed = true;
    }
    if (batchMap.contains(key)) {
        batchMap.remove(key);
        savePerProfileSawBatchMap(batchMap);
        changed = true;
    }
    if (changed) {
        qDebug() << "[SAW] reset perProfileHistory for" << key;
        emit sawLearnedLagChanged();
    }
}

// ---- per-(profile, scale) helpers ----

QString Settings::sawPairKey(const QString& profileFilename, const QString& scaleType) {
    return profileFilename + QStringLiteral("::") + scaleType;
}

QJsonObject Settings::loadPerProfileSawHistoryMap() const {
    if (m_perProfileSawHistoryCacheValid) return m_perProfileSawHistoryCache;
    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("saw/perProfileHistory", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[SAW] corrupt perProfileHistory JSON:" << parseError.errorString()
                   << "- per-profile SAW history lost";
        const_cast<QSettings&>(m_settings).setValue("saw/perProfileHistory", "{}");
        map = QJsonObject();
    }
    m_perProfileSawHistoryCache = map;
    m_perProfileSawHistoryCacheValid = true;
    return m_perProfileSawHistoryCache;
}

void Settings::savePerProfileSawHistoryMap(const QJsonObject& map) {
    m_settings.setValue("saw/perProfileHistory",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileSawHistoryCache = map;
    m_perProfileSawHistoryCacheValid = true;
}

QJsonObject Settings::loadPerProfileSawBatchMap() const {
    if (m_perProfileSawBatchCacheValid) return m_perProfileSawBatchCache;
    QJsonParseError parseError;
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("saw/perProfileBatch", "{}").toByteArray(),
        &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[SAW] corrupt perProfileBatch JSON:" << parseError.errorString();
        const_cast<QSettings&>(m_settings).setValue("saw/perProfileBatch", "{}");
        map = QJsonObject();
    }
    m_perProfileSawBatchCache = map;
    m_perProfileSawBatchCacheValid = true;
    return m_perProfileSawBatchCache;
}

void Settings::savePerProfileSawBatchMap(const QJsonObject& map) {
    m_settings.setValue("saw/perProfileBatch",
                        QJsonDocument(map).toJson(QJsonDocument::Compact));
    m_perProfileSawBatchCache = map;
    m_perProfileSawBatchCacheValid = true;
}

QJsonArray Settings::perProfileSawHistory(const QString& profileFilename, const QString& scaleType) const {
    return loadPerProfileSawHistoryMap().value(sawPairKey(profileFilename, scaleType)).toArray();
}

QJsonObject Settings::allPerProfileSawHistory() const {
    return loadPerProfileSawHistoryMap();
}

QJsonArray Settings::sawPendingBatch(const QString& profileFilename, const QString& scaleType) const {
    return loadPerProfileSawBatchMap().value(sawPairKey(profileFilename, scaleType)).toArray();
}

double Settings::globalSawBootstrapLag(const QString& scaleType) const {
    const QString key = QStringLiteral("saw/globalBootstrapLag/") + scaleType;
    return m_settings.value(key, 0.0).toDouble();
}

void Settings::setGlobalSawBootstrapLag(const QString& scaleType, double lag) {
    const QString key = QStringLiteral("saw/globalBootstrapLag/") + scaleType;
    m_settings.setValue(key, lag);
}

// ---- per-(profile, scale) read path ----

QString Settings::sawModelSource(const QString& profileFilename, const QString& scaleType) const {
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) return QStringLiteral("perProfile");
    }
    if (globalSawBootstrapLag(scaleType) > 0.0) return QStringLiteral("globalBootstrap");
    ensureSawCacheLoaded();
    for (const auto& v : std::as_const(m_sawHistoryCache)) {
        if (v.toObject().value("scale").toString() == scaleType) return QStringLiteral("globalPool");
    }
    return QStringLiteral("scaleDefault");
}

QList<QPair<double, double>> Settings::sawLearningEntriesFor(const QString& profileFilename,
                                                             const QString& scaleType,
                                                             int maxEntries) const {
    QList<QPair<double, double>> result;
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) {
            for (qsizetype i = pairHistory.size() - 1; i >= 0 && result.size() < maxEntries; --i) {
                QJsonObject obj = pairHistory[i].toObject();
                if (obj.contains("drip")) {
                    result.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
                }
            }
            if (!result.isEmpty()) return result;
        }
    }
    return sawLearningEntries(scaleType, maxEntries);
}

double Settings::sawLearnedLagFor(const QString& profileFilename, const QString& scaleType) const {
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) {
            double sumLag = 0;
            qsizetype count = 0;
            for (qsizetype i = pairHistory.size() - 1; i >= 0 && count < 5; --i) {
                QJsonObject obj = pairHistory[i].toObject();
                double drip = obj.value("drip").toDouble();
                double flow = obj.value("flow").toDouble();
                if (flow > 0.5) {
                    sumLag += drip / flow;
                    ++count;
                }
            }
            if (count > 0) return sumLag / count;
        }
    }
    double bootstrap = globalSawBootstrapLag(scaleType);
    if (bootstrap > 0.0) return bootstrap;
    return sawLearnedLag();
}

double Settings::getExpectedDripFor(const QString& profileFilename,
                                    const QString& scaleType,
                                    double currentFlowRate) const {
    if (!profileFilename.isEmpty()) {
        QJsonArray pairHistory = perProfileSawHistory(profileFilename, scaleType);
        if (pairHistory.size() >= kSawMinMediansForGraduation) {
            // Weighted average using the same recency + flow-similarity scheme as the
            // global getExpectedDrip(). Uses up to 12 medians (pair history is capped at
            // 10, so this just consumes the lot in practice).
            struct Entry { double drip; double flow; };
            QVector<Entry> entries;
            for (qsizetype i = pairHistory.size() - 1; i >= 0 && entries.size() < 12; --i) {
                QJsonObject obj = pairHistory[i].toObject();
                if (obj.contains("drip")) entries.append({obj["drip"].toDouble(), obj["flow"].toDouble()});
            }
            if (!entries.isEmpty()) {
                double weightedDripSum = 0;
                double totalWeight = 0;
                const double recencyMax = 10.0;
                const double recencyMin = 3.0;
                for (qsizetype i = 0; i < entries.size(); ++i) {
                    const Entry& e = entries[i];
                    double recencyWeight = recencyMax - i * (recencyMax - recencyMin)
                                                       / qMax(qsizetype{1}, entries.size() - 1);
                    double flowDiff = qAbs(e.flow - currentFlowRate);
                    double flowWeight = qExp(-(flowDiff * flowDiff) / 4.5);
                    double w = recencyWeight * flowWeight;
                    weightedDripSum += e.drip * w;
                    totalWeight += w;
                }
                if (totalWeight >= 0.01) {
                    double expected = weightedDripSum / totalWeight;
                    return qBound(0.5, expected, 20.0);
                }
            }
        }
    }
    double bootstrap = globalSawBootstrapLag(scaleType);
    if (bootstrap > 0.0) {
        return qMin(currentFlowRate * bootstrap, 8.0);
    }
    return qMin(currentFlowRate * (sensorLag(scaleType) + 0.1), 8.0);
}

// ---- per-pair batch accumulator + commit ----

void Settings::addSawPerPairEntry(double drip, double flowRate, const QString& scaleType,
                                  double overshoot, const QString& profileFilename) {
    constexpr int kBatchSize = 5;
    constexpr int kMaxPairHistory = 10;
    constexpr double kBatchMaxIqr = 1.0;          // seconds — IQR of lags within a batch
    constexpr double kBatchMaxDeviation = 1.5;    // seconds — single lag from batch median

    const QString key = sawPairKey(profileFilename, scaleType);

    // 1. Append entry to pending batch
    QJsonObject batchMap = loadPerProfileSawBatchMap();
    QJsonArray batch = batchMap.value(key).toArray();
    QJsonObject entry;
    entry["drip"] = drip;
    entry["flow"] = flowRate;
    entry["overshoot"] = overshoot;
    entry["scale"] = scaleType;
    entry["profile"] = profileFilename;
    entry["ts"] = QDateTime::currentSecsSinceEpoch();
    batch.append(entry);

    if (batch.size() < kBatchSize) {
        batchMap[key] = batch;
        savePerProfileSawBatchMap(batchMap);
        const double lag = (flowRate > 0.5) ? drip / flowRate : 0.0;
        qDebug() << "[SAW] accumulated drip=" << drip << "flow=" << flowRate
                 << "for" << key
                 << "(" << batch.size() << "/" << kBatchSize << ") lag=" << lag;
        return;
    }

    // 2. Batch full — compute medians of drip / flow / overshoot, plus IQR of lags.
    QVector<double> drips, flows, overs, lags;
    drips.reserve(batch.size()); flows.reserve(batch.size());
    overs.reserve(batch.size()); lags.reserve(batch.size());
    for (const auto& v : std::as_const(batch)) {
        QJsonObject o = v.toObject();
        drips.append(o["drip"].toDouble());
        flows.append(o["flow"].toDouble());
        overs.append(o["overshoot"].toDouble());
        if (o["flow"].toDouble() > 0.5) lags.append(o["drip"].toDouble() / o["flow"].toDouble());
    }

    auto medianOf = [](QVector<double> v) -> double {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        const qsizetype n = v.size();
        return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];
    };
    auto iqrOf = [](QVector<double> v) -> double {
        if (v.size() < 4) return 0.0;
        std::sort(v.begin(), v.end());
        const qsizetype n = v.size();
        return v[3 * n / 4] - v[n / 4];
    };

    const double medianDrip = medianOf(drips);
    const double medianFlow = medianOf(flows);
    const double medianOver = medianOf(overs);
    const double medianLag = (medianFlow > 0.5) ? medianDrip / medianFlow : 0.0;
    const double lagIqr = iqrOf(lags);

    // 3. Outlier check on the batch as a whole.
    QString rejectReason;
    if (lagIqr > kBatchMaxIqr) {
        rejectReason = QString("iqr=%1 > %2s").arg(lagIqr).arg(kBatchMaxIqr);
    } else {
        for (double l : std::as_const(lags)) {
            double dev = qAbs(l - medianLag);
            if (dev > kBatchMaxDeviation) {
                rejectReason = QString("outlier lag=%1 deviates %2s > %3s from median")
                                   .arg(l).arg(dev).arg(kBatchMaxDeviation);
                break;
            }
        }
    }
    if (!rejectReason.isEmpty()) {
        qWarning() << "[SAW] batch rejected —" << qPrintable(rejectReason)
                   << "median_lag=" << medianLag << "for" << key << "— dropping batch";
        batchMap.remove(key);
        savePerProfileSawBatchMap(batchMap);
        return;
    }

    // 4. Auto-reset: 2nd consecutive batch with median overshoot < -6g → wipe pair history,
    //    let the new median be the sole baseline. The legacy single-shot path triggers on
    //    2 consecutive bad shots; here, since each median represents 5 shots, the threshold
    //    is effectively 10 consecutive bad shots — intentional debouncing for the batched
    //    update model.
    QJsonObject historyMap = loadPerProfileSawHistoryMap();
    QJsonArray pairHistory = historyMap.value(key).toArray();
    if (medianOver < -6.0 && !pairHistory.isEmpty()) {
        QJsonObject lastMedian = pairHistory.last().toObject();
        if (lastMedian["overshoot"].toDouble() < -6.0) {
            qWarning() << "[SAW] 2nd consecutive overshoot<-6g for" << key
                       << "— clearing committed history";
            pairHistory = QJsonArray();
        }
    }

    // 5. Commit median to per-pair history.
    QJsonObject medianEntry;
    medianEntry["drip"] = medianDrip;
    medianEntry["flow"] = medianFlow;
    medianEntry["overshoot"] = medianOver;
    medianEntry["scale"] = scaleType;
    medianEntry["profile"] = profileFilename;
    medianEntry["ts"] = QDateTime::currentSecsSinceEpoch();
    medianEntry["batchSize"] = batch.size();
    pairHistory.append(medianEntry);
    while (pairHistory.size() > kMaxPairHistory) pairHistory.removeFirst();
    historyMap[key] = pairHistory;
    savePerProfileSawHistoryMap(historyMap);

    // 6. Mirror the median into the global pool so isSawConverged + the legacy
    //    bootstrap path keep working. Trim to 50 (existing cap).
    QByteArray data = m_settings.value("saw/learningHistory").toByteArray();
    QJsonArray pool;
    if (!data.isEmpty()) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (doc.isArray()) pool = doc.array();
    }
    pool.append(medianEntry);
    while (pool.size() > 50) pool.removeFirst();
    m_settings.setValue("saw/learningHistory", QJsonDocument(pool).toJson());
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;

    // 7. Clear pending batch.
    batchMap.remove(key);
    savePerProfileSawBatchMap(batchMap);

    qDebug() << "[SAW] committed median lag=" << medianLag
             << "(drip=" << medianDrip << "flow=" << medianFlow << ")"
             << "for" << key
             << "— n_medians=" << pairHistory.size();

    // 8. Recompute global bootstrap lag for this scale type so other (profile, scale)
    //    pairs with no per-pair history can use it as their first-shot default.
    recomputeGlobalSawBootstrap(scaleType);

    emit sawLearnedLagChanged();
}

void Settings::recomputeGlobalSawBootstrap(const QString& scaleType) {
    // Bootstrap is a cold-start prior for *new* pairs: a single committed batch median
    // (5 real shots) is already more informative than the static sensorLag() constant,
    // so any pair with at least one committed median contributes. The IQR fence below
    // protects against under-trained outliers if many pairs accumulate. Pairs that have
    // crossed the per-profile graduation threshold (kSawMinMediansForGraduation
    // medians) for the read path are a stricter bar handled in sawLearnedLagFor /
    // sawModelSource.
    QJsonObject map = loadPerProfileSawHistoryMap();
    QVector<double> lags;
    for (auto it = map.begin(); it != map.end(); ++it) {
        QJsonArray pairHistory = it.value().toArray();
        if (pairHistory.isEmpty()) continue;
        // Median entries record their scale; only include this scale's pairs.
        if (pairHistory.last().toObject().value("scale").toString() != scaleType) continue;
        // Use the last committed median lag as that pair's representative.
        QJsonObject last = pairHistory.last().toObject();
        double drip = last.value("drip").toDouble();
        double flow = last.value("flow").toDouble();
        if (flow > 0.5) lags.append(drip / flow);
    }
    if (lags.size() < 2) {
        // Need at least 2 contributing pairs to compute a useful bootstrap median.
        return;
    }
    std::sort(lags.begin(), lags.end());
    // IQR fence (1.5x IQR from Q1/Q3) — same outlier-removal as flow cal's global median.
    if (lags.size() >= 4) {
        const qsizetype n = lags.size();
        const double q1 = lags[n / 4];
        const double q3 = lags[3 * n / 4];
        const double iqr = q3 - q1;
        const double lower = q1 - 1.5 * iqr;
        const double upper = q3 + 1.5 * iqr;
        QVector<double> filtered;
        for (double v : std::as_const(lags)) if (v >= lower && v <= upper) filtered.append(v);
        if (filtered.size() >= 2) lags = filtered;
    }
    const qsizetype n = lags.size();
    const double median = (n % 2 == 0) ? (lags[n / 2 - 1] + lags[n / 2]) / 2.0 : lags[n / 2];
    setGlobalSawBootstrapLag(scaleType, median);
    qDebug() << "[SAW] global bootstrap lag for" << scaleType
             << "updated to" << median << "(median of" << n << "pairs with committed history)";
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
