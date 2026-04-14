#include "settings.h"
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
    , m_use12HourTime(QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive))
{
    qDebug() << "Settings: system time format =" << QLocale::system().timeFormat(QLocale::ShortFormat)
             << "-> use12HourTime =" << m_use12HourTime;

    // Force a fresh read from persistent storage before checking for missing keys.
    // On macOS, NSUserDefaults can return stale (empty) data if another instance of
    // the app just wrote to the same plist — sync() forces re-read from disk and
    // prevents the defaults-initialization code from overwriting existing settings.
    m_settings.sync();
    qDebug() << "Settings: sync() done, contains profile/favorites:" << m_settings.contains("profile/favorites");

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

    // One-time migration: auto flow calibration graduates from opt-in beta to default-on.
    // Remove old key so new default (true) applies. Users can still disable via toggle.
    if (!m_settings.contains("calibration/autoFlowCalMigrated")) {
        m_settings.remove("calibration/autoFlowCalibration");
        m_settings.setValue("calibration/autoFlowCalMigrated", true);
        qDebug() << "Settings: Migrated auto flow calibration to default-on";
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

    // Migrate legacy DYE grinder field: split combined model into brand/model/burrs
    if (!m_settings.contains("dye/grinderBrand") || m_settings.value("dye/grinderBrand").toString().isEmpty()) {
        QString oldModel = m_settings.value("dye/grinderModel").toString();
        if (!oldModel.isEmpty()) {
            auto result = GrinderAliases::lookup(oldModel);
            if (result.found) {
                m_settings.setValue("dye/grinderBrand", result.brand);
                m_settings.setValue("dye/grinderModel", result.model);
                if (m_settings.value("dye/grinderBurrs").toString().isEmpty())
                    m_settings.setValue("dye/grinderBurrs", result.stockBurrs);
                qDebug() << "Settings: Migrated DYE grinder ->"
                         << result.brand << result.model << result.stockBurrs;
            }
        }
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

    // Initialize resolved dark mode from persisted themeMode
    m_isDarkMode = (themeMode() != "light");  // "dark" or "system" default to dark until system detection runs

    // Load brew parameter overrides (persistent)
    m_hasTemperatureOverride = m_settings.value("brew/hasTemperatureOverride", false).toBool();
    if (m_hasTemperatureOverride) {
        m_temperatureOverride = m_settings.value("brew/temperatureOverride", 0.0).toDouble();
    }

    m_hasBrewYieldOverride = m_settings.value("brew/hasBrewYieldOverride", false).toBool();
    if (m_hasBrewYieldOverride) {
        m_brewYieldOverride = m_settings.value("brew/brewYieldOverride", 0.0).toDouble();
    }

    // Generate MCP API key on first run (avoids const_cast in the getter)
    if (m_settings.value("mcp/apiKey", "").toString().isEmpty()) {
        m_settings.setValue("mcp/apiKey", QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    // Beans-modified tracking: recompute whenever any DYE bean/grinder field or the
    // selected preset / preset list changes. Fields tracked here must stay in sync
    // with those compared in recomputeBeansModified() and written by applyBeanPreset().
    connect(this, &Settings::dyeBeanBrandChanged,     this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeBeanTypeChanged,      this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeRoastDateChanged,     this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeRoastLevelChanged,    this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeGrinderBrandChanged,  this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeGrinderModelChanged,  this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeGrinderBurrsChanged,  this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeGrinderSettingChanged, this, &Settings::recomputeBeansModified);
    connect(this, &Settings::dyeBaristaChanged,       this, &Settings::recomputeBeansModified);
    connect(this, &Settings::selectedBeanPresetChanged, this, &Settings::recomputeBeansModified);
    connect(this, &Settings::beanPresetsChanged,      this, &Settings::recomputeBeansModified);
    recomputeBeansModified();  // Seed initial state from persisted values
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
            setSelectedSteamCup(static_cast<int>(arr.size()) - 1);
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

void Settings::setSteamPitcherWeight(int index, double weightG) {
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
    return m_settings.value("profile/selectedFavorite", -1).toInt();
}

void Settings::setSelectedFavoriteProfile(int index) {
    if (selectedFavoriteProfile() != index) {
        qDebug() << "setSelectedFavoriteProfile:" << selectedFavoriteProfile() << "->" << index;
        m_settings.setValue("profile/selectedFavorite", index);
        emit selectedFavoriteProfileChanged();
    }
}

void Settings::addFavoriteProfile(const QString& name, const QString& filename) {
    qWarning() << "Settings: addFavoriteProfile name=" << name << "filename=" << filename;
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

void Settings::removeFavoriteProfile(int index) {
    QByteArray data = m_settings.value("profile/favorites").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr = doc.array();

    if (index >= 0 && index < arr.size()) {
        QString filename = arr[index].toObject()["filename"].toString();
        qWarning() << "Settings: removeFavoriteProfile index=" << index << "filename=" << filename;
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

    for (int i = 0; i < arr.size(); ++i) {
        QString favFilename = arr[i].toObject()["filename"].toString();
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

// Hot water SAW learning
double Settings::hotWaterSawOffset() const {
    return m_settings.value("water/sawOffset", 2.0).toDouble();
}

void Settings::setHotWaterSawOffset(double offset) {
    if (!qFuzzyCompare(hotWaterSawOffset(), offset)) {
        m_settings.setValue("water/sawOffset", offset);
        emit hotWaterSawOffsetChanged();
    }
}

int Settings::hotWaterSawSampleCount() const {
    return m_settings.value("water/sawSampleCount", 0).toInt();
}

void Settings::setHotWaterSawSampleCount(int count) {
    m_settings.setValue("water/sawSampleCount", count);
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
            setSelectedWaterCup(static_cast<int>(arr.size()) - 1);
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
        QVariantMap row = v.toObject().toVariantMap();
        // Backfill legacy presets (no showOnIdle field) — default true to preserve behavior
        if (!row.contains("showOnIdle")) {
            row["showOnIdle"] = true;
        }
        result.append(row);
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
                             const QString& grinderBrand, const QString& grinderModel,
                             const QString& grinderBurrs, const QString& grinderSetting,
                             const QString& barista) {
    QJsonArray arr = getBeanPresetsArray();

    QJsonObject preset;
    preset["name"] = name;
    preset["brand"] = brand;
    preset["type"] = type;
    preset["roastDate"] = roastDate;
    preset["roastLevel"] = roastLevel;
    preset["grinderBrand"] = grinderBrand;
    preset["grinderModel"] = grinderModel;
    preset["grinderBurrs"] = grinderBurrs;
    preset["grinderSetting"] = grinderSetting;
    preset["barista"] = barista;
    preset["showOnIdle"] = true;
    arr.append(preset);

    m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
    emit beanPresetsChanged();
}

void Settings::updateBeanPreset(int index, const QString& name, const QString& brand,
                                const QString& type, const QString& roastDate,
                                const QString& roastLevel, const QString& grinderBrand,
                                const QString& grinderModel, const QString& grinderBurrs,
                                const QString& grinderSetting, const QString& barista) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        // Preserve showOnIdle from existing entry (default true for legacy)
        QJsonObject existing = arr[index].toObject();
        bool showOnIdle = existing.contains("showOnIdle") ? existing["showOnIdle"].toBool() : true;

        QJsonObject preset;
        preset["name"] = name;
        preset["brand"] = brand;
        preset["type"] = type;
        preset["roastDate"] = roastDate;
        preset["roastLevel"] = roastLevel;
        preset["grinderBrand"] = grinderBrand;
        preset["grinderModel"] = grinderModel;
        preset["grinderBurrs"] = grinderBurrs;
        preset["grinderSetting"] = grinderSetting;
        preset["barista"] = barista;
        preset["showOnIdle"] = showOnIdle;
        arr[index] = preset;

        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
        emit beanPresetsChanged();
    }
}

void Settings::removeBeanPreset(int index) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        arr.removeAt(index);
        m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());

        // Adjust selected if needed
        int selected = selectedBeanPreset();
        if (selected >= static_cast<int>(arr.size()) && arr.size() > 0) {
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

    if (from >= 0 && from < static_cast<int>(arr.size()) && to >= 0 && to < static_cast<int>(arr.size()) && from != to) {
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

void Settings::setBeanPresetShowOnIdle(int index, bool show) {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < static_cast<int>(arr.size())) {
        QJsonObject preset = arr[index].toObject();
        if (preset["showOnIdle"].toBool(true) != show) {
            preset["showOnIdle"] = show;
            arr[index] = preset;
            m_settings.setValue("bean/presets", QJsonDocument(arr).toJson());
            emit beanPresetsChanged();
        }
    }
}

QVariantList Settings::idleBeanPresets() const {
    QJsonArray arr = getBeanPresetsArray();

    QVariantList result;
    for (qsizetype i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        bool showOnIdle = obj.contains("showOnIdle") ? obj["showOnIdle"].toBool() : true;
        if (!showOnIdle) continue;
        QVariantMap row = obj.toVariantMap();
        row["showOnIdle"] = true;
        row["originalIndex"] = static_cast<int>(i);
        result.append(row);
    }
    return result;
}

QVariantMap Settings::getBeanPreset(int index) const {
    QJsonArray arr = getBeanPresetsArray();

    if (index >= 0 && index < arr.size()) {
        QVariantMap row = arr[index].toObject().toVariantMap();
        if (!row.contains("showOnIdle")) {
            row["showOnIdle"] = true;
        }
        return row;
    }
    return QVariantMap();
}

void Settings::applyBeanPreset(int index) {
    QVariantMap preset = getBeanPreset(index);
    if (preset.isEmpty()) {
        return;
    }

    // Migrate legacy presets: split combined grinder model using alias lookup
    QString brand = preset.value("grinderBrand").toString();
    QString model = preset.value("grinderModel").toString();
    QString burrs = preset.value("grinderBurrs").toString();
    if (brand.isEmpty() && !model.isEmpty()) {
        auto result = GrinderAliases::lookup(model);
        if (result.found) {
            brand = result.brand;
            model = result.model;
            if (burrs.isEmpty()) burrs = result.stockBurrs;
            // Persist the migration back to the preset
            updateBeanPreset(index, preset.value("name").toString(),
                             preset.value("brand").toString(), preset.value("type").toString(),
                             preset.value("roastDate").toString(), preset.value("roastLevel").toString(),
                             brand, model, burrs,
                             preset.value("grinderSetting").toString(),
                             preset.value("barista").toString());
        }
    }

    // Apply all preset fields to DYE settings
    setDyeBeanBrand(preset.value("brand").toString());
    setDyeBeanType(preset.value("type").toString());
    setDyeRoastDate(preset.value("roastDate").toString());
    setDyeRoastLevel(preset.value("roastLevel").toString());
    setDyeGrinderBrand(brand);
    setDyeGrinderModel(model);
    setDyeGrinderBurrs(burrs);
    setDyeGrinderSetting(preset.value("grinderSetting").toString());
    setDyeBarista(preset.value("barista").toString());
}

void Settings::recomputeBeansModified() {
    bool modified = false;
    const int idx = selectedBeanPreset();
    if (idx >= 0) {
        const QVariantMap preset = getBeanPreset(idx);
        if (!preset.isEmpty()) {
            modified = dyeBeanBrand()      != preset.value("brand").toString()
                    || dyeBeanType()       != preset.value("type").toString()
                    || dyeRoastDate()      != preset.value("roastDate").toString()
                    || dyeRoastLevel()     != preset.value("roastLevel").toString()
                    || dyeGrinderBrand()   != preset.value("grinderBrand").toString()
                    || dyeGrinderModel()   != preset.value("grinderModel").toString()
                    || dyeGrinderBurrs()   != preset.value("grinderBurrs").toString()
                    || dyeGrinderSetting() != preset.value("grinderSetting").toString()
                    || dyeBarista()        != preset.value("barista").toString();
        }
    }
    if (modified != m_beansModified) {
        m_beansModified = modified;
        emit beansModifiedChanged();
    }
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
                        dyeGrinderBrand(),
                        dyeGrinderModel(),
                        dyeGrinderBurrs(),
                        dyeGrinderSetting(),
                        dyeBarista());
        setSelectedBeanPreset(existingIndex);
    } else {
        // Add new preset (appends to the end)
        addBeanPreset(name,
                     dyeBeanBrand(),
                     dyeBeanType(),
                     dyeRoastDate(),
                     dyeRoastLevel(),
                     dyeGrinderBrand(),
                     dyeGrinderModel(),
                     dyeGrinderBurrs(),
                     dyeGrinderSetting(),
                     dyeBarista());
        setSelectedBeanPreset(static_cast<int>(getBeanPresetsArray().size()) - 1);
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

// -- Light/Dark default palettes --

const QVariantMap& Settings::darkDefaults() {
    static const QVariantMap defaults = {
        {"backgroundColor", "#1a1a2e"},
        {"surfaceColor", "#252538"},
        {"primaryColor", "#4e85f4"},
        {"secondaryColor", "#c0c5e3"},
        {"textColor", "#ffffff"},
        {"textSecondaryColor", "#a0a8b8"},
        {"accentColor", "#e94560"},
        {"successColor", "#00cc6d"},
        {"warningColor", "#ffaa00"},
        {"highlightColor", "#ffaa00"},
        {"errorColor", "#ff4444"},
        {"borderColor", "#3a3a4e"},
        {"pressureColor", "#18c37e"},
        {"pressureGoalColor", "#69fdb3"},
        {"flowColor", "#4e85f4"},
        {"flowGoalColor", "#7aaaff"},
        {"temperatureColor", "#e73249"},
        {"temperatureGoalColor", "#ffa5a6"},
        {"weightColor", "#a2693d"},
        {"weightFlowColor", "#d4a574"},
        {"resistanceColor", "#eae83d"},
        {"waterLevelColor", "#4e85f4"},
        {"dyeDoseColor", "#6F4E37"},
        {"dyeOutputColor", "#9C27B0"},
        {"dyeTdsColor", "#FF9800"},
        {"dyeEyColor", "#a2693d"},
        {"buttonDisabled", "#555555"},
        {"stopMarkerColor", "#FF6B6B"},
        {"frameMarkerColor", "#66ffffff"},
        {"modifiedIndicatorColor", "#FFCC00"},
        {"simulationIndicatorColor", "#E65100"},
        {"warningButtonColor", "#FFA500"},
        {"successButtonColor", "#2E7D32"},
        {"rowAlternateColor", "#1a1a1a"},
        {"rowAlternateLightColor", "#222222"},
        {"sourceBadgeBlueColor", "#4a90d9"},
        {"sourceBadgeGreenColor", "#4ad94a"},
        {"sourceBadgeOrangeColor", "#d9a04a"},
        {"trackOnTargetColor", "#00cc6d"},
        {"trackDriftingColor", "#f0ad4e"},
        {"trackOffTargetColor", "#e94560"},
        {"primaryContrastColor", "#ffffff"},
        {"iconColor", "#ffffff"},
        {"bottomBarColor", "#4e85f4"},
        {"actionButtonContentColor", "#ffffff"}
    };
    return defaults;
}

const QVariantMap& Settings::lightDefaults() {
    // Light palette carries the dark theme's blue-purple DNA as subtle tints
    // rather than going neutral gray — same family, different brightness.
    static const QVariantMap defaults = {
        // Core UI — blue-gray tinted backgrounds echo the navy dark theme
        {"backgroundColor", "#eef0f6"},
        {"surfaceColor", "#ffffff"},
        {"primaryColor", "#d1daee"},
        {"secondaryColor", "#8890a8"},
        {"textColor", "#1a1a2e"},
        {"textSecondaryColor", "#5d6478"},
        {"accentColor", "#d93050"},
        {"successColor", "#00a856"},
        {"warningColor", "#e69500"},
        {"highlightColor", "#e69500"},
        {"errorColor", "#d93030"},
        {"borderColor", "#c4c9d6"},
        // Chart — slightly deeper than dark-mode goal colors for white-background contrast
        {"pressureColor", "#12a86b"},
        {"pressureGoalColor", "#40d898"},
        {"flowColor", "#3a6fd0"},
        {"flowGoalColor", "#6898e8"},
        {"temperatureColor", "#d42840"},
        {"temperatureGoalColor", "#f07080"},
        {"weightColor", "#8a5830"},
        {"weightFlowColor", "#c08858"},
        {"resistanceColor", "#a89800"},
        {"waterLevelColor", "#2c5fc0"},
        // DYE metadata — same across modes (strong hues work on both)
        {"dyeDoseColor", "#6F4E37"},
        {"dyeOutputColor", "#9C27B0"},
        {"dyeTdsColor", "#FF9800"},
        {"dyeEyColor", "#a2693d"},
        // UI indicators — blue-gray tinted where neutral before
        {"buttonDisabled", "#b0b5c2"},
        {"stopMarkerColor", "#e85050"},
        {"frameMarkerColor", "#40303048"},
        {"modifiedIndicatorColor", "#CC9900"},
        {"simulationIndicatorColor", "#E65100"},
        {"warningButtonColor", "#e69500"},
        {"successButtonColor", "#2E7D32"},
        // Lists — subtle blue-gray alternation
        {"rowAlternateColor", "#e8eaf2"},
        {"rowAlternateLightColor", "#e0e3ed"},
        {"sourceBadgeBlueColor", "#3a78c0"},
        {"sourceBadgeGreenColor", "#38b038"},
        {"sourceBadgeOrangeColor", "#c89030"},
        // Tracking indicators
        {"trackOnTargetColor", "#00a856"},
        {"trackDriftingColor", "#d99a00"},
        {"trackOffTargetColor", "#d93050"},
        {"primaryContrastColor", "#4e85f4"},
        {"iconColor", "#1a1a2e"},
        {"bottomBarColor", "#ffffff"},
        {"actionButtonContentColor", "#ffffff"}
    };
    return defaults;
}

// -- Theme mode --

QString Settings::themeMode() const {
    return m_settings.value("theme/mode", "dark").toString();
}

void Settings::setThemeMode(const QString& mode) {
    if (themeMode() != mode) {
        m_settings.setValue("theme/mode", mode);
        emit themeModeChanged();
        updateResolvedMode();
    }
}

void Settings::initSystemThemeDetection() {
    auto* hints = QGuiApplication::styleHints();
    if (hints) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
            if (themeMode() == "system") {
                updateResolvedMode();
            }
        });
    }
    // Resolve initial mode (for "system" mode)
    updateResolvedMode();
}

void Settings::updateResolvedMode() {
    bool wasDark = m_isDarkMode;
    QString mode = themeMode();

    if (mode == "light") {
        m_isDarkMode = false;
    } else if (mode == "dark") {
        m_isDarkMode = true;
    } else {
        // "system" — follow OS
        auto* hints = QGuiApplication::styleHints();
        if (hints) {
            m_isDarkMode = (hints->colorScheme() != Qt::ColorScheme::Light);
        } else {
            m_isDarkMode = true;  // fallback to dark
        }
    }

    if (wasDark != m_isDarkMode) {
        emit isDarkModeChanged();
        emit customThemeColorsChanged();  // Active palette changed
        emit activeThemeNameChanged();    // Derived from dark/lightThemeName
    }
#ifdef Q_OS_IOS
    ios_setStatusBarStyle(m_isDarkMode);
#endif
}

void Settings::setEditingPalette(const QString& palette) {
    QString p = (palette == "light") ? "light" : "dark";
    if (m_editingPalette != p) {
        m_editingPalette = p;
        emit editingPaletteChanged();
    }
}

QVariantMap Settings::editingPaletteColors() const {
    const QString key = (m_editingPalette == "light") ? "theme/customColorsLight" : "theme/customColorsDark";
    const QVariantMap& defaults = (m_editingPalette == "light") ? lightDefaults() : darkDefaults();

    QVariantMap result = defaults;  // Start with full defaults
    QByteArray data = m_settings.value(key).toByteArray();
    if (!data.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QVariantMap stored = doc.object().toVariantMap();
        // Merge stored on top of defaults
        for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

void Settings::setEditingPaletteColor(const QString& colorName, const QString& colorValue) {
    const QString key = (m_editingPalette == "light") ? "theme/customColorsLight" : "theme/customColorsDark";

    QByteArray data = m_settings.value(key).toByteArray();
    QJsonObject obj;
    if (!data.isEmpty()) {
        obj = QJsonDocument::fromJson(data).object();
    }
    obj[colorName] = colorValue;
    m_settings.setValue(key, QJsonDocument(obj).toJson());

    // If editing the active palette, notify QML
    bool editingActive = (m_editingPalette == "dark") == m_isDarkMode;
    if (editingActive) {
        emit customThemeColorsChanged();
    }

    // Mark the edited palette's mode as custom
    if (m_editingPalette == "dark") {
        if (darkThemeName() != "Custom")
            setDarkThemeName("Custom");
    } else {
        if (lightThemeName() != "Custom")
            setLightThemeName("Custom");
    }
}

// Theme settings
QVariantMap Settings::customThemeColors() const {
    // Resolve active palette based on isDarkMode
    const QString key = m_isDarkMode ? "theme/customColorsDark" : "theme/customColorsLight";
    const QVariantMap& defaults = m_isDarkMode ? darkDefaults() : lightDefaults();

    QVariantMap result = defaults;  // Start with full defaults
    QByteArray data = m_settings.value(key).toByteArray();
    if (!data.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QVariantMap stored = doc.object().toVariantMap();
        for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

void Settings::setCustomThemeColors(const QVariantMap& colors) {
    // Write to the active palette
    const QString key = m_isDarkMode ? "theme/customColorsDark" : "theme/customColorsLight";
    QJsonObject obj = QJsonObject::fromVariantMap(colors);
    m_settings.setValue(key, QJsonDocument(obj).toJson());
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
    return m_isDarkMode ? darkThemeName() : lightThemeName();
}

void Settings::setActiveThemeName(const QString& name) {
    if (m_isDarkMode)
        setDarkThemeName(name);
    else
        setLightThemeName(name);
}

QString Settings::darkThemeName() const {
    // Migrate from old single activeThemeName if per-mode names not set
    if (!m_settings.contains("theme/darkThemeName")) {
        QString old = m_settings.value("theme/activeName", "Default Dark").toString();
        return (old == "Default") ? "Default Dark" : old;
    }
    return m_settings.value("theme/darkThemeName", "Default Dark").toString();
}

void Settings::setDarkThemeName(const QString& name) {
    if (darkThemeName() != name) {
        m_settings.setValue("theme/darkThemeName", name);
        emit darkThemeNameChanged();
        if (m_isDarkMode)
            emit activeThemeNameChanged();
    }
}

QString Settings::lightThemeName() const {
    return m_settings.value("theme/lightThemeName", "Default Light").toString();
}

void Settings::setLightThemeName(const QString& name) {
    if (lightThemeName() != name) {
        m_settings.setValue("theme/lightThemeName", name);
        emit lightThemeNameChanged();
        if (!m_isDarkMode)
            emit activeThemeNameChanged();
    }
}

QStringList Settings::themeNames() const {
    QStringList names;
    names << "Default Dark" << "Default Light";

    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QString name = val.toObject()["name"].toString();
        if (!name.isEmpty())
            names << name;
    }
    return names;
}

void Settings::applyDarkTheme(const QString& name) {
    if (name == "Default Dark") {
        m_settings.remove("theme/customColorsDark");
    } else if (name == "Default Light") {
        m_settings.setValue("theme/customColorsDark",
            QJsonDocument(QJsonObject::fromVariantMap(lightDefaults())).toJson());
    } else {
        // Look for user theme
        QJsonArray userThemes = QJsonDocument::fromJson(
            m_settings.value("theme/userThemes", "[]").toByteArray()
        ).array();
        for (const QJsonValue& val : userThemes) {
            QJsonObject obj = val.toObject();
            if (obj["name"].toString() == name) {
                if (obj.contains("colorsDark"))
                    m_settings.setValue("theme/customColorsDark",
                        QJsonDocument(obj["colorsDark"].toObject()).toJson());
                else
                    m_settings.remove("theme/customColorsDark");
                break;
            }
        }
    }
    setDarkThemeName(name);
    if (m_isDarkMode)
        emit customThemeColorsChanged();
}

void Settings::applyLightTheme(const QString& name) {
    if (name == "Default Light") {
        m_settings.remove("theme/customColorsLight");
    } else if (name == "Default Dark") {
        m_settings.setValue("theme/customColorsLight",
            QJsonDocument(QJsonObject::fromVariantMap(darkDefaults())).toJson());
    } else {
        // Look for user theme
        QJsonArray userThemes = QJsonDocument::fromJson(
            m_settings.value("theme/userThemes", "[]").toByteArray()
        ).array();
        for (const QJsonValue& val : userThemes) {
            QJsonObject obj = val.toObject();
            if (obj["name"].toString() == name) {
                if (obj.contains("colorsLight"))
                    m_settings.setValue("theme/customColorsLight",
                        QJsonDocument(obj["colorsLight"].toObject()).toJson());
                else
                    m_settings.remove("theme/customColorsLight");
                break;
            }
        }
    }
    setLightThemeName(name);
    if (!m_isDarkMode)
        emit customThemeColorsChanged();
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
    setEditingPaletteColor(colorName, colorValue);
}

QString Settings::getThemeColor(const QString& colorName) const {
    QVariantMap colors = customThemeColors();
    return colors.value(colorName).toString();
}

void Settings::resetThemeToDefault() {
    m_settings.remove("theme/customColorsDark");
    m_settings.remove("theme/customColorsLight");
    m_settings.remove("theme/colorGroups");
    setDarkThemeName("Default Dark");
    setLightThemeName("Default Light");
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

    // Default Dark theme (built-in)
    QVariantMap defaultDark;
    defaultDark["name"] = "Default Dark";
    defaultDark["primaryColor"] = "#4e85f4";
    defaultDark["isBuiltIn"] = true;
    themes.append(defaultDark);

    // Default Light theme (built-in)
    QVariantMap defaultLight;
    defaultLight["name"] = "Default Light";
    defaultLight["primaryColor"] = "#d1daee";
    defaultLight["isBuiltIn"] = true;
    themes.append(defaultLight);

    // Load user-saved themes
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QJsonObject obj = val.toObject();
        QVariantMap theme;
        theme["name"] = obj["name"].toString();
        // Read from colorsDark (new format) or colors (old format)
        QJsonObject colors = obj.contains("colorsDark") ? obj["colorsDark"].toObject() : obj["colors"].toObject();
        theme["primaryColor"] = colors["primaryColor"].toString();
        theme["isBuiltIn"] = false;
        themes.append(theme);
    }

    return themes;
}

void Settings::applyPresetTheme(const QString& name) {
    if (name == "Default" || name == "Default Dark") {
        // Only reset the dark palette — preserve user's custom light palette
        m_settings.remove("theme/customColorsDark");
        setActiveShader("");
        setDarkThemeName("Default Dark");
        // Switch to dark mode (skip if already resolved to dark, e.g. "system" on a dark OS)
        if (!m_isDarkMode) {
            setThemeMode("dark");
        } else if (themeMode() != "dark" && themeMode() != "system") {
            setThemeMode("dark");
        } else {
            emit customThemeColorsChanged();  // Palette changed, mode didn't
        }
        return;
    }
    if (name == "Default Light") {
        // Only reset the light palette — preserve user's custom dark palette
        m_settings.remove("theme/customColorsLight");
        setActiveShader("");
        setLightThemeName("Default Light");
        // Switch to light mode (skip if already resolved to light, e.g. "system" on a light OS)
        if (m_isDarkMode) {
            setThemeMode("light");
        } else if (themeMode() != "light" && themeMode() != "system") {
            setThemeMode("light");
        } else {
            emit customThemeColorsChanged();  // Palette changed, mode didn't
        }
        return;
    }

    // Look for user theme
    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (const QJsonValue& val : userThemes) {
        QJsonObject obj = val.toObject();
        if (obj["name"].toString() == name) {
            // Apply dark palette
            if (obj.contains("colorsDark")) {
                m_settings.setValue("theme/customColorsDark",
                    QJsonDocument(obj["colorsDark"].toObject()).toJson());
            } else {
                m_settings.remove("theme/customColorsDark");
            }
            // Apply light palette
            if (obj.contains("colorsLight")) {
                m_settings.setValue("theme/customColorsLight",
                    QJsonDocument(obj["colorsLight"].toObject()).toJson());
            } else {
                m_settings.remove("theme/customColorsLight");
            }
            // Restore screen effect (or disable for old presets without it)
            if (obj.contains("screenEffect"))
                applyScreenEffect(obj["screenEffect"].toObject());
            else
                setActiveShader("");
            setDarkThemeName(name);
            setLightThemeName(name);
            emit customThemeColorsChanged();
            return;
        }
    }
}

void Settings::saveCurrentTheme(const QString& name) {
    if (name.isEmpty() || name == "Default" || name == "Default Dark" || name == "Default Light") {
        return; // Can't save with empty name or overwrite built-in themes
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

    // Create new theme entry with both palettes
    QJsonObject newTheme;
    newTheme["name"] = name;

    // Save dark palette
    QByteArray darkData = m_settings.value("theme/customColorsDark").toByteArray();
    if (!darkData.isEmpty())
        newTheme["colorsDark"] = QJsonDocument::fromJson(darkData).object();
    else
        newTheme["colorsDark"] = QJsonObject::fromVariantMap(darkDefaults());

    // Save light palette
    QByteArray lightData = m_settings.value("theme/customColorsLight").toByteArray();
    if (!lightData.isEmpty())
        newTheme["colorsLight"] = QJsonDocument::fromJson(lightData).object();
    else
        newTheme["colorsLight"] = QJsonObject::fromVariantMap(lightDefaults());

    newTheme["screenEffect"] = screenEffectJson();
    userThemes.append(newTheme);

    // Save to settings
    m_settings.setValue("theme/userThemes", QJsonDocument(userThemes).toJson(QJsonDocument::Compact));
    setDarkThemeName(name);
    setLightThemeName(name);
    emit themeNamesChanged();
}

void Settings::deleteUserTheme(const QString& name) {
    if (name == "Default Dark" || name == "Default Light") {
        return; // Can't delete built-in themes
    }

    QJsonArray userThemes = QJsonDocument::fromJson(
        m_settings.value("theme/userThemes", "[]").toByteArray()
    ).array();

    for (qsizetype i = userThemes.size() - 1; i >= 0; --i) {
        if (userThemes[i].toObject()["name"].toString() == name) {
            userThemes.removeAt(i);
        }
    }

    m_settings.setValue("theme/userThemes", QJsonDocument(userThemes).toJson(QJsonDocument::Compact));
    emit themeNamesChanged();

    // If we deleted a theme that was active for either mode, reset that mode
    if (darkThemeName() == name)
        applyDarkTheme("Default Dark");
    if (lightThemeName() == name)
        applyLightTheme("Default Light");
}

bool Settings::saveThemeToFile(const QString& filePath) {
    QString path = filePath;
    if (path.startsWith("file:///")) {
        path = QUrl(path).toLocalFile();
    }

    QJsonObject root;
    root["name"] = activeThemeName();

    // Save both palettes
    QByteArray darkData = m_settings.value("theme/customColorsDark").toByteArray();
    if (!darkData.isEmpty())
        root["colorsDark"] = QJsonDocument::fromJson(darkData).object();
    else
        root["colorsDark"] = QJsonObject::fromVariantMap(darkDefaults());

    QByteArray lightData = m_settings.value("theme/customColorsLight").toByteArray();
    if (!lightData.isEmpty())
        root["colorsLight"] = QJsonDocument::fromJson(lightData).object();
    else
        root["colorsLight"] = QJsonObject::fromVariantMap(lightDefaults());

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
        QString themeName = root["name"].toString();
        setDarkThemeName(themeName);
        setLightThemeName(themeName);
    }
    // New dual-palette format
    if (root.contains("colorsDark")) {
        m_settings.setValue("theme/customColorsDark",
            QJsonDocument(root["colorsDark"].toObject()).toJson());
    }
    if (root.contains("colorsLight")) {
        m_settings.setValue("theme/customColorsLight",
            QJsonDocument(root["colorsLight"].toObject()).toJson());
    }
    // Backward compat: old single-palette format
    if (root.contains("colors") && !root.contains("colorsDark")) {
        m_settings.setValue("theme/customColorsDark",
            QJsonDocument(root["colors"].toObject()).toJson());
    }
    emit customThemeColorsChanged();

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

    // Water level - analogous to flow color
    palette["waterLevelColor"] = hslColor(analogous1 + goldenAngle * 2, 70.0, 55.0);

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

    // Primary contrast - white on dark primaries, dark on light primaries
    // Use relative luminance of the primary color to decide
    QColor primary(palette["primaryColor"].toString());
    double lum = 0.2126 * primary.redF() + 0.7152 * primary.greenF() + 0.0722 * primary.blueF();
    palette["primaryContrastColor"] = (lum > 0.4) ? "#1a1a2e" : "#ffffff";

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
        m_dyeGrinderBrandCache = m_settings.value("dye/grinderBrand", "").toString();
        m_dyeGrinderModelCache = m_settings.value("dye/grinderModel", "").toString();
        m_dyeGrinderBurrsCache = m_settings.value("dye/grinderBurrs", "").toString();
        m_dyeGrinderSettingCache = m_settings.value("dye/grinderSetting", "").toString();
        m_dyeBeanWeightCache = m_settings.value("dye/beanWeight", 18.0).toDouble();
        m_dyeDrinkWeightCache = m_settings.value("dye/drinkWeight", 36.0).toDouble();
        m_dyeCacheInitialized = true;
    }
}

QString Settings::dyeGrinderBrand() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderBrandCache;
}

void Settings::setDyeGrinderBrand(const QString& value) {
    if (dyeGrinderBrand() != value) {
        m_dyeGrinderBrandCache = value;
        m_settings.setValue("dye/grinderBrand", value);
        emit dyeGrinderBrandChanged();
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

QString Settings::dyeGrinderBurrs() const {
    ensureDyeCacheLoaded();
    return m_dyeGrinderBurrsCache;
}

void Settings::setDyeGrinderBurrs(const QString& value) {
    if (dyeGrinderBurrs() != value) {
        m_dyeGrinderBurrsCache = value;
        m_settings.setValue("dye/grinderBurrs", value);
        emit dyeGrinderBurrsChanged();
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

QStringList Settings::suggestedBurrs(const QString& brand, const QString& model) const {
    return GrinderAliases::suggestedBurrs(brand, model);
}

bool Settings::isBurrSwappable(const QString& brand, const QString& model) const {
    return GrinderAliases::isBurrSwappable(brand, model);
}

QStringList Settings::knownGrinderBrands() const {
    return GrinderAliases::allBrands();
}

QStringList Settings::knownGrinderModels(const QString& brand) const {
    return GrinderAliases::modelsForBrand(brand);
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
    return qBound(5, val, 100);
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

bool Settings::screenCaptureEnabled() const {
    return m_settings.value("machine/screenCaptureEnabled", false).toBool();
}

void Settings::setScreenCaptureEnabled(bool enabled) {
    if (screenCaptureEnabled() != enabled) {
        m_settings.setValue("machine/screenCaptureEnabled", enabled);
        emit screenCaptureEnabledChanged();
    }
}

bool Settings::hideGhcSimulator() const {
    return m_settings.value("developer/hideGhcSimulator", false).toBool();
}

void Settings::setHideGhcSimulator(bool hide) {
    if (hideGhcSimulator() != hide) {
        m_settings.setValue("developer/hideGhcSimulator", hide);
        emit hideGhcSimulatorChanged();
    }
}

bool Settings::simulatedScaleEnabled() const {
    return m_settings.value("developer/simulatedScaleEnabled", true).toBool();
}

void Settings::setSimulatedScaleEnabled(bool enabled) {
    if (simulatedScaleEnabled() != enabled) {
        m_settings.setValue("developer/simulatedScaleEnabled", enabled);
        emit simulatedScaleEnabledChanged();
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

// MCP Server settings
bool Settings::mcpEnabled() const {
    return m_settings.value("mcp/enabled", false).toBool();
}

void Settings::setMcpEnabled(bool enabled) {
    if (mcpEnabled() != enabled) {
        m_settings.setValue("mcp/enabled", enabled);
        emit mcpEnabledChanged();
    }
}

int Settings::mcpAccessLevel() const {
    return m_settings.value("mcp/accessLevel", 1).toInt();
}

void Settings::setMcpAccessLevel(int level) {
    if (mcpAccessLevel() != level) {
        m_settings.setValue("mcp/accessLevel", level);
        emit mcpAccessLevelChanged();
    }
}

int Settings::mcpConfirmationLevel() const {
    return m_settings.value("mcp/confirmationLevel", 1).toInt();
}

void Settings::setMcpConfirmationLevel(int level) {
    if (mcpConfirmationLevel() != level) {
        m_settings.setValue("mcp/confirmationLevel", level);
        emit mcpConfirmationLevelChanged();
    }
}

QString Settings::mcpApiKey() const {
    return m_settings.value("mcp/apiKey", "").toString();
}

void Settings::regenerateMcpApiKey() {
    m_settings.setValue("mcp/apiKey", QUuid::createUuid().toString(QUuid::WithoutBraces));
    emit mcpApiKeyChanged();
}

// Discuss Shot settings
int Settings::discussShotApp() const {
    return m_settings.value("ai/discussShotApp", 0).toInt();
}

void Settings::setDiscussShotApp(int app) {
    if (discussShotApp() != app) {
        m_settings.setValue("ai/discussShotApp", app);
        emit discussShotAppChanged();
    }
}

QString Settings::discussShotCustomUrl() const {
    return m_settings.value("ai/discussShotCustomUrl", "").toString();
}

void Settings::setDiscussShotCustomUrl(const QString& url) {
    if (discussShotCustomUrl() != url) {
        m_settings.setValue("ai/discussShotCustomUrl", url);
        emit discussShotCustomUrlChanged();
    }
}

QString Settings::claudeRcSessionUrl() const {
    return m_settings.value("ai/claudeRcSessionUrl").toString();
}

void Settings::setClaudeRcSessionUrl(const QString& url) {
    if (claudeRcSessionUrl() != url) {
        m_settings.setValue("ai/claudeRcSessionUrl", url);
        emit claudeRcSessionUrlChanged();
    }
}

QString Settings::discussShotUrl() const {
    static const QStringList urls = {
        "claude://",
        "https://claude.ai/new",
        "https://chatgpt.com/",
        "https://gemini.google.com/app",
        "https://grok.com/"
    };
    int app = discussShotApp();
    if (app == 5) return discussShotCustomUrl();
    if (app == discussAppNone()) return QString();
    if (app == discussAppClaudeDesktop()) return claudeRcSessionUrl();
    if (app >= 0 && app < urls.size()) return urls[app];
    return urls[0];
}

void Settings::openDiscussUrl(const QString& url) {
    if (url.isEmpty()) return;

#ifdef Q_OS_IOS
    if (discussShotApp() == discussAppClaudeDesktop()) {
        if (openInSafariView(url)) return;
    }
#endif

    QDesktopServices::openUrl(QUrl(url));
}

void Settings::dismissDiscussOverlay() {
#ifdef Q_OS_IOS
    dismissSafariView();
#endif
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

bool Settings::ignoreVolumeWithScale() const {
    return m_settings.value("espresso/ignoreVolumeWithScale", false).toBool();
}

void Settings::setIgnoreVolumeWithScale(bool enabled) {
    if (ignoreVolumeWithScale() != enabled) {
        m_settings.setValue("espresso/ignoreVolumeWithScale", enabled);
        emit ignoreVolumeWithScaleChanged();
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

QVector<double> Settings::flowCalPendingIdeals(const QString& profileFilename) const {
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("calibration/flowCalBatch", "{}").toByteArray()).object();
    QVector<double> result;
    QJsonArray arr = map.value(profileFilename).toArray();
    for (const auto& v : arr)
        result.append(v.toDouble());
    return result;
}

void Settings::appendFlowCalPendingIdeal(const QString& profileFilename, double ideal) {
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("calibration/flowCalBatch", "{}").toByteArray()).object();
    QJsonArray arr = map.value(profileFilename).toArray();
    arr.append(ideal);
    map[profileFilename] = arr;
    m_settings.setValue("calibration/flowCalBatch", QJsonDocument(map).toJson(QJsonDocument::Compact));
}

void Settings::clearFlowCalPendingIdeals(const QString& profileFilename) {
    QJsonObject map = QJsonDocument::fromJson(
        m_settings.value("calibration/flowCalBatch", "{}").toByteArray()).object();
    map.remove(profileFilename);
    m_settings.setValue("calibration/flowCalBatch", QJsonDocument(map).toJson(QJsonDocument::Compact));
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

void Settings::addSawLearningPoint(double drip, double flowRate, const QString& scaleType, double overshoot) {
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
        double expectedDrip = getExpectedDrip(flowRate);
        double threshold = qMax(3.0, expectedDrip);  // Reject if deviation exceeds expected drip (or 3g floor)
        if (qAbs(drip - expectedDrip) > threshold) {
            qWarning() << "[SAW] Outlier rejected: drip=" << drip
                       << "g expected=" << expectedDrip
                       << "g threshold=" << threshold
                       << "(converged, deviation too high)";
            return;
        }
    }

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
    m_sawHistoryCacheDirty = true;
    m_sawConvergedCache = -1;
    emit sawLearnedLagChanged();

    // Also reset hot water SAW learning
    setHotWaterSawOffset(2.0);  // Back to default
    setHotWaterSawSampleCount(0);
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
                qsizetype i = id.length() - 1;
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

bool Settings::boolValue(const QString& key, bool defaultValue) const {
    const QVariant v = m_settings.value(key);
    if (!v.isValid()) return defaultValue;
    // QVariant::toBool() handles native bool, int, and the strings "true"/"false"/"1"/"0".
    // We bypass the plain value() path because that returns the raw QVariant (often a
    // QString on INI-backed QSettings) to QML, where JavaScript truthiness on "false"
    // yields true — causing silent persistence bugs.
    return v.toBool();
}

// Pocket app pairing
QString Settings::pocketPairingToken() const
{
    return m_settings.value("pocket/pairingToken").toString();
}

void Settings::setPocketPairingToken(const QString& token)
{
    m_settings.setValue("pocket/pairingToken", token);
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
