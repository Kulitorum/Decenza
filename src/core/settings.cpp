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
#include "grinderaliases.h"
#include "../ble/scales/scaletypeids.h"  // ScaleTypeIds::normalizeScaleTypeId (dependency-free)
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
#include <QSet>
#include <QMap>
#include <QPair>

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
#ifdef DECENZA_TESTING
    , m_settings(testQSettingsPath(), QSettings::IniFormat)
#else
    , m_settings("DecentEspresso", "DE1Qt")
#endif
    , m_mqtt(new SettingsMqtt(this))
    , m_autoWake(new SettingsAutoWake(this))
    , m_hardware(new SettingsHardware(this))
    , m_ai(new SettingsAI(this))
    , m_theme(new SettingsTheme(this))
    , m_visualizer(new SettingsVisualizer(this))
    , m_mcp(new SettingsMcp(this))
    , m_brew(new SettingsBrew(this))
    , m_dye(new SettingsDye(this))
    , m_network(new SettingsNetwork(this))
    , m_app(new SettingsApp(this))
    , m_calibration(new SettingsCalibration(this, this))
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

    // Evict the dead shot-rating keys. Both are orphans of the removed
    // default-shot-rating feature: shot/defaultRating was the setting itself,
    // and dye/espressoEnjoyment was the sticky field it fed, which kept
    // stamping a rating onto freshly pulled shots for one shot after the
    // feature was deleted. Nothing reads either one now — migration 16 was the
    // last reader and no longer needs it — but a stale rating sitting in the
    // store is the shape of thing that leaks back into something, so it does
    // not get to stay.
    //
    // Deliberately AFTER the freshInstall snapshot: eviction would otherwise
    // make a store holding only these two keys look like a new install and
    // skip the upgrade-only seeding below. Unreachable today (any real store
    // carries profile/* too), but it is a trap for whoever extends this list.
    //
    // contains() first because remove() does not check: on the Apple native
    // backend it issues an unconditional CFPreferencesSetValue(key, nullptr)
    // and dirties the store, so an unguarded pair would queue a settings
    // write on every launch forever. Guarded, a clean store costs two lookups.
    // A store that cannot be written stays dirty and the keys survive — which
    // is inert, since no reader for either one exists any more.
    for (const auto& deadKey : {QStringLiteral("shot/defaultRating"),
                                QStringLiteral("dye/espressoEnjoyment")}) {
        if (m_settings.contains(deadKey))
            m_settings.remove(deadKey);
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

    // Invariant: if knownScales is non-empty, primaryAddress must match one
    // of its entries, AND the legacy scale/* keys (read by older code paths
    // and by main.cpp's initial setSavedScaleAddress wiring) must match the
    // primary. An orphan (stored entry with no matching primary) is invisible
    // in the Known Devices picker AND filtered out of discovery, so the user
    // can't reach it. A divergence in the other direction (valid primary but
    // empty/stale scale/address) is what caused the user-visible bug in #1281
    // follow-up: QML's startup hook reads primaryScaleAddress and fires
    // tryDirectConnectToScale, but BLEManager's m_savedScaleAddress is loaded
    // from scale/address — if the two drifted, the direct-connect dead-ends
    // with "no saved scale address/type" and the user has to manually re-select
    // a scale even though Known Devices had multiple entries. Heal both
    // directions on launch.
    {
        const qsizetype scalesCount = m_settings.beginReadArray("knownScales/scales");
        QString promoteAddress, promoteType, promoteName;
        QSet<QString> addresses;
        // Indexed by lowercase address → (type, name) so we can look up the
        // canonical entry for whatever address ends up being primary.
        QMap<QString, QPair<QString, QString>> entriesByAddress;
        for (qsizetype i = 0; i < scalesCount; ++i) {
            m_settings.setArrayIndex(static_cast<int>(i));
            const QString a = m_settings.value("address").toString();
            if (a.isEmpty()) continue;
            const QString t = m_settings.value("type").toString();
            const QString n = m_settings.value("name").toString();
            addresses.insert(a.toLower());
            entriesByAddress.insert(a.toLower(), {t, n});
            if (promoteAddress.isEmpty()) {
                promoteAddress = a;
                promoteType = t;
                promoteName = n;
            }
        }
        m_settings.endArray();

        if (!promoteAddress.isEmpty()) {
            const QString primary = m_settings.value("knownScales/primaryAddress").toString();
            if (primary.isEmpty() || !addresses.contains(primary.toLower())) {
                // Primary missing or stale → promote the first known entry.
                m_settings.setValue("knownScales/primaryAddress", promoteAddress);
                m_settings.setValue("scale/address", promoteAddress);
                m_settings.setValue("scale/type", promoteType);
                m_settings.setValue("scale/name", promoteName);
                qDebug() << "Settings: Repaired orphaned known scale — promoted"
                         << promoteName << promoteAddress << "to primary";
            } else {
                // Primary is valid. Verify legacy scale/* keys match it; sync
                // if they don't. This is the direction the original heal
                // missed — a setPrimaryScale() call that didn't write through
                // to scale/address (because of a code-path bug or older build
                // state) would leave us in the inconsistent state that breaks
                // startup auto-connect.
                const QString legacyAddr = m_settings.value("scale/address").toString();
                if (legacyAddr.compare(primary, Qt::CaseInsensitive) != 0) {
                    const auto entry = entriesByAddress.value(primary.toLower());
                    m_settings.setValue("scale/address", primary);
                    m_settings.setValue("scale/type", entry.first);
                    m_settings.setValue("scale/name", entry.second);
                    qDebug() << "Settings: Repaired scale/address drift — synced legacy keys to primary"
                             << entry.second << primary;
                }
            }
        }
    }

    // One-time: normalize all persisted scaleType values from legacy display
    // names (e.g. "Decent Scale") to canonical type-ids (e.g. "decent"), so a
    // future display-name rename can never orphan SAW learning or miss the
    // sensorLag lookup. Covers scale/type, known-scale entries, and SAW storage.
    // Runs after the orphan-heal above so any display name it wrote is cleaned up.
    // See docs/CLAUDE_MD/SAW_LEARNING.md and the scale-type-identity capability.
    if (!m_settings.contains("scale/typeIdsMigrated")) {
        const QString rawType = m_settings.value("scale/type").toString();
        if (!rawType.isEmpty()) {
            const QString id = ScaleTypeIds::normalizeScaleTypeId(rawType);
            if (id != rawType) m_settings.setValue("scale/type", id);
        }

        QVariantList scales = knownScales();
        bool anyChanged = false;
        for (qsizetype i = 0; i < scales.size(); ++i) {
            QVariantMap s = scales[i].toMap();
            const QString t = s.value("type").toString();
            const QString id = ScaleTypeIds::normalizeScaleTypeId(t);
            if (id != t) { s["type"] = id; scales[i] = s; anyChanged = true; }
        }
        if (anyChanged) writeKnownScales(scales);

        if (m_calibration) m_calibration->migrateScaleTypeIds();

        // Mark migrated only once the writes actually landed — gate on the observable
        // effect (persisted scale/type is now canonical) rather than setting the flag
        // unconditionally. If a write failed (read-only / full store), scale/type stays
        // a display name, the flag is left unset, and the migration retries next launch.
        const QString persisted = m_settings.value("scale/type").toString();
        if (persisted.isEmpty() || persisted == ScaleTypeIds::normalizeScaleTypeId(persisted)) {
            m_settings.setValue("scale/typeIdsMigrated", true);
            qDebug() << "Settings: normalized scaleType values to canonical type-ids";
        } else {
            qWarning() << "Settings: scaleType id migration incomplete (scale/type still"
                       << persisted << ") — will retry next launch";
        }
    }

    // Theme initial state is now resolved inside SettingsTheme

    // Generate MCP API key on first run (avoids const_cast in the getter)
    if (m_settings.value("mcp/apiKey", "").toString().isEmpty()) {
        m_settings.setValue("mcp/apiKey", QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

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
    // Persist the canonical type-id, never a display name — scaleType is a
    // rename-stable key for SAW learning / sensorLag / known scales.
    const QString id = ScaleTypeIds::normalizeScaleTypeId(type);
    if (scaleType() != id) {
        m_settings.setValue("scale/type", id);
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

    // Store the canonical type-id (rename-stable key), not a display name. The
    // human label lives in `name`.
    const QString id = ScaleTypeIds::normalizeScaleTypeId(type);

    // Read existing scales
    QVariantList scales = knownScales();

    // Check for existing entry — update name/type if found. We fall through
    // to the invariant repair below in both branches (existing or newly added)
    // so a re-add of an existing scale also fixes a missing primary.
    bool alreadyExists = false;
    for (qsizetype i = 0; i < scales.size(); ++i) {
        QVariantMap s = scales[i].toMap();
        if (s["address"].toString().compare(address, Qt::CaseInsensitive) == 0) {
            if (s["type"].toString() != id || s["name"].toString() != name) {
                s["type"] = id;
                s["name"] = name;
                scales[i] = s;
                writeKnownScales(scales);
            }
            alreadyExists = true;
            break;
        }
    }

    if (!alreadyExists) {
        QVariantMap newScale;
        newScale["address"] = address;
        newScale["type"] = id;
        newScale["name"] = name;
        newScale["isPrimary"] = false;
        scales.append(newScale);
        writeKnownScales(scales);
    }

    // Invariant: a non-empty knownScales list must have exactly one primary.
    // If no primary is currently set, auto-promote this entry — whether we
    // just added it or it already existed. The original code only ran this
    // for newly-added entries; the existing-entry early-return left the
    // invariant unrepaired if primary had been cleared post-startup (rare
    // but reachable, e.g. via mcptools_devices.cpp's clearSavedScale).
    //
    // Symmetric counterpart to removeKnownScale's auto-promote-next branch:
    // removing the primary promotes another; adding into (or re-adding within)
    // an empty-primary state promotes one. Without this, the Known Devices
    // picker can render with nothing selected even though the user has known
    // scales — they then have to tap one to "select" it, which is what set
    // primary in the first place. See discussion under #1281.
    if (primaryScaleAddress().isEmpty()) {
        setPrimaryScale(address);
    }
}

void Settings::removeKnownScale(const QString& address) {
    QVariantList scales = knownScales();
    bool wasPrimary = (primaryScaleAddress().compare(address, Qt::CaseInsensitive) == 0);

    scales.erase(std::remove_if(scales.begin(), scales.end(), [&](const QVariant& v) {
        return v.toMap()["address"].toString().compare(address, Qt::CaseInsensitive) == 0;
    }), scales.end());

    writeKnownScales(scales);

    if (wasPrimary) {
        // Auto-promote the first remaining entry with a non-empty address so
        // knownScales never holds an unreachable orphan.
        QString nextAddr, nextType, nextName;
        for (const QVariant& v : scales) {
            QVariantMap s = v.toMap();
            const QString a = s["address"].toString();
            if (a.isEmpty()) continue;
            nextAddr = a;
            nextType = s["type"].toString();
            nextName = s["name"].toString();
            break;
        }
        m_settings.setValue("knownScales/primaryAddress", nextAddr);
        setScaleAddress(nextAddr);
        setScaleType(nextType);
        setScaleName(nextName);
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

    // 2b. Clear the legacy AccessibilityManager store. Accessibility now
    // lives in the primary store (cleared above), but its one-time
    // migrateLegacyStore() guard is in the primary store too — so after
    // a factory reset that guard is gone and the next launch would
    // resurrect old accessibility settings from this legacy store.
    // Wipe it so factory reset actually resets accessibility.
    QSettings legacyAccessibility(QStringLiteral("Decenza"), QStringLiteral("DE1"));
    legacyAccessibility.clear();
    legacyAccessibility.sync();

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
