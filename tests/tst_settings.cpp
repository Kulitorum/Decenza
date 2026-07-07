#include <QtTest>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>

#include "core/settings.h"
#include "history/coffeebagstorage.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/settings_network.h"
#include "core/settings_theme.h"
#include "core/settings_visualizer.h"
#include "core/settingsserializer.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

// Test Settings property round-trip and signal emission.
// Settings uses QSettings("DecentEspresso", "DE1Qt") which reads/writes to
// the system settings store. Tests save originals in init() and restore in
// cleanup() (guaranteed to run even if assertions fail mid-test).

// File-scope helper (Q_OBJECT moc rejects nested structs in test classes).
// Snapshot + clear the Known Devices store so a test owns it for the
// duration, and restore on scope exit. Settings exposes addKnownScale /
// removeKnownScale but no bulk reset; the snapshot path covers test
// isolation without leaking test entries into the user's QSettings store.
struct KnownScalesGuard {
    explicit KnownScalesGuard(Settings* s) : m_settings(s) {
        m_snapshot = s->knownScales();
        m_origPrimary = s->primaryScaleAddress();
        for (const QVariant& v : m_snapshot) {
            s->removeKnownScale(v.toMap()["address"].toString());
        }
    }
    ~KnownScalesGuard() {
        const QVariantList current = m_settings->knownScales();
        for (const QVariant& v : current) {
            m_settings->removeKnownScale(v.toMap()["address"].toString());
        }
        for (const QVariant& v : m_snapshot) {
            const QVariantMap s = v.toMap();
            m_settings->addKnownScale(s["address"].toString(),
                                      s["type"].toString(),
                                      s["name"].toString());
        }
        if (!m_origPrimary.isEmpty()) {
            m_settings->setPrimaryScale(m_origPrimary);
        }
    }
    Settings* m_settings;
    QVariantList m_snapshot;
    QString m_origPrimary;
};

class tst_Settings : public QObject {
    Q_OBJECT

private:
    Settings m_settings;

    // Saved originals — restored in cleanup() regardless of test outcome
    double m_origTargetWeight;
    double m_origDoseCupTare;
    bool m_origDoseCaptureSound;
    double m_origSteamTemp;
    QString m_origScaleAddress;
    QString m_origThemeMode;
    int m_origShotRating;
    bool m_origIgnoreVolume;
    bool m_origAutoUpdate;
    QString m_origDyeBeanBrand;
    QString m_origAutoLoadFilename;
    int m_origAutoLoadRevertMinutes;
    QString m_origDyeBeanBaseId;
    QString m_origDyeBeanBaseData;
    double m_origWaterTemperature;
    QString m_origTemperatureUnit;
    QByteArray m_origVesselPresets;
    QByteArray m_origPitcherPresets;
    bool m_origMilkAutoCapture;
    int m_origActiveRecipeId;
    int m_origActiveBagId;
    QString m_origDyeGrinderSetting;
    // applyActiveBag (triggered by the grind-pin test's bag selection) also
    // rewrites these — save/restore so the test leaves no trace.
    QString m_origDyeBeanType;
    QString m_origDyeRoastDate;
    QString m_origDyeRoastLevel;
    qint64 m_origActiveEquipmentId;
    double m_origDyeBeanWeight;
    int m_origDyeGrinderRpm;

private slots:

    void init() {
        // Save all originals before each test
        m_origTargetWeight = m_settings.brew()->targetWeight();
        m_origDoseCupTare = m_settings.brew()->doseCupTareWeight();
        m_origDoseCaptureSound = m_settings.brew()->doseCaptureSoundEnabled();
        m_origSteamTemp = m_settings.brew()->steamTemperature();
        m_origScaleAddress = m_settings.scaleAddress();
        m_origThemeMode = m_settings.theme()->themeMode();
        m_origShotRating = m_settings.visualizer()->defaultShotRating();
        m_origIgnoreVolume = m_settings.brew()->ignoreVolumeWithScale();
        m_origAutoUpdate = m_settings.visualizer()->visualizerAutoUpdate();
        m_origDyeBeanBrand = m_settings.dye()->dyeBeanBrand();
        m_origAutoLoadFilename = m_settings.app()->autoLoadProfileFilename();
        m_origAutoLoadRevertMinutes = m_settings.app()->autoLoadRevertMinutes();
        m_origDyeBeanBaseId = m_settings.dye()->dyeBeanBaseId();
        m_origDyeBeanBaseData = m_settings.dye()->dyeBeanBaseData();
        m_origWaterTemperature = m_settings.brew()->waterTemperature();
        m_origTemperatureUnit = m_settings.app()->temperatureUnit();
        // Mutated directly by the effectiveSteamDurationSec tests AND as a side effect of
        // setSteamPitcherCalibration (calibrating re-enables weight-timed steaming).
        m_origMilkAutoCapture = m_settings.brew()->milkAutoCaptureEnabled();
        { QSettings raw("DecentEspresso", "DE1Qt");
          m_origVesselPresets = raw.value("water/vesselPresets").toByteArray();
          m_origPitcherPresets = raw.value("steam/pitcherPresets").toByteArray(); }
        m_origActiveRecipeId = m_settings.dye()->activeRecipeId();
        m_origActiveBagId = m_settings.dye()->activeBagId();
        m_origDyeGrinderSetting = m_settings.dye()->dyeGrinderSetting();
        m_origDyeBeanType = m_settings.dye()->dyeBeanType();
        m_origDyeRoastDate = m_settings.dye()->dyeRoastDate();
        m_origDyeRoastLevel = m_settings.dye()->dyeRoastLevel();
        m_origActiveEquipmentId = m_settings.dye()->activeEquipmentId();
        m_origDyeBeanWeight = m_settings.dye()->dyeBeanWeight();
        m_origDyeGrinderRpm = m_settings.dye()->dyeGrinderRpm();
    }

    void cleanup() {
        // Restore all originals after each test (runs even on assertion failure)
        m_settings.brew()->setTargetWeight(m_origTargetWeight);
        m_settings.brew()->setDoseCupTareWeight(m_origDoseCupTare);
        m_settings.brew()->setDoseCaptureSoundEnabled(m_origDoseCaptureSound);
        m_settings.brew()->setSteamTemperature(m_origSteamTemp);
        m_settings.setScaleAddress(m_origScaleAddress);
        m_settings.theme()->setThemeMode(m_origThemeMode);
        m_settings.visualizer()->setDefaultShotRating(m_origShotRating);
        m_settings.brew()->setIgnoreVolumeWithScale(m_origIgnoreVolume);
        m_settings.visualizer()->setVisualizerAutoUpdate(m_origAutoUpdate);
        m_settings.dye()->setDyeBeanBrand(m_origDyeBeanBrand);
        m_settings.app()->setAutoLoadProfileFilename(m_origAutoLoadFilename);
        m_settings.app()->setAutoLoadRevertMinutes(m_origAutoLoadRevertMinutes);
        m_settings.dye()->setDyeBeanBaseId(m_origDyeBeanBaseId);
        m_settings.dye()->setDyeBeanBaseData(m_origDyeBeanBaseData);
        m_settings.brew()->setWaterTemperature(m_origWaterTemperature);
        m_settings.app()->setTemperatureUnit(m_origTemperatureUnit);
        m_settings.brew()->setMilkAutoCaptureEnabled(m_origMilkAutoCapture);
        { QSettings raw("DecentEspresso", "DE1Qt");
          raw.setValue("water/vesselPresets", m_origVesselPresets);
          raw.setValue("steam/pitcherPresets", m_origPitcherPresets);
          raw.sync(); }
        // Recipe state (add-recipes). Detach the test-local bag storage FIRST
        // (its object died with the test scope — a dangling m_bagStorage would
        // crash the next write-through, and a live one would look up the
        // restored real-world bag id in the test DB and "vanish-clear" it).
        // With no storage attached, the restores below are pure QSettings
        // writes. setBagStorage(nullptr) is a no-op when never set.
        m_settings.dye()->setBagStorage(nullptr);
        m_settings.dye()->setGrindBagWriteThroughSuspended(false);
        m_settings.dye()->setActiveRecipeId(m_origActiveRecipeId);
        m_settings.dye()->setActiveBagId(m_origActiveBagId);
        m_settings.dye()->setDyeGrinderSetting(m_origDyeGrinderSetting);
        m_settings.dye()->setDyeBeanType(m_origDyeBeanType);
        m_settings.dye()->setDyeRoastDate(m_origDyeRoastDate);
        m_settings.dye()->setDyeRoastLevel(m_origDyeRoastLevel);
        m_settings.dye()->setActiveEquipmentId(m_origActiveEquipmentId);
        m_settings.dye()->setDyeBeanWeight(m_origDyeBeanWeight);
        m_settings.dye()->setDyeGrinderRpm(m_origDyeGrinderRpm);
    }

    // ==========================================
    // Property round-trip (set -> get)
    // ==========================================

    void targetWeightRoundTrip() {
        m_settings.brew()->setTargetWeight(42.5);
        QCOMPARE(m_settings.brew()->targetWeight(), 42.5);
    }

    void doseCupTareWeightRoundTrip() {
        m_settings.brew()->setDoseCupTareWeight(12.5);
        QCOMPARE(m_settings.brew()->doseCupTareWeight(), 12.5);
    }

    void doseCupTareWeightClampsNegativeToZero() {
        // Setter clamps below 0 — a negative tare would otherwise inflate the
        // computed net dose. 0 is the "no cup / feature off" sentinel.
        m_settings.brew()->setDoseCupTareWeight(-5.0);
        QCOMPARE(m_settings.brew()->doseCupTareWeight(), 0.0);
    }

    void doseCaptureSoundEnabledRoundTrip() {
        m_settings.brew()->setDoseCaptureSoundEnabled(true);
        QCOMPARE(m_settings.brew()->doseCaptureSoundEnabled(), true);
        m_settings.brew()->setDoseCaptureSoundEnabled(false);
        QCOMPARE(m_settings.brew()->doseCaptureSoundEnabled(), false);
    }

    void steamTemperatureRoundTrip() {
        m_settings.brew()->setSteamTemperature(155.0);
        QCOMPARE(m_settings.brew()->steamTemperature(), 155.0);
    }

    void scaleAddressRoundTrip() {
        m_settings.setScaleAddress("AA:BB:CC:DD:EE:FF");
        QCOMPARE(m_settings.scaleAddress(), QString("AA:BB:CC:DD:EE:FF"));
    }

    void themeModeRoundTrip() {
        m_settings.theme()->setThemeMode("light");
        QCOMPARE(m_settings.theme()->themeMode(), QString("light"));
    }

    void defaultShotRatingRoundTrip() {
        m_settings.visualizer()->setDefaultShotRating(50);
        QCOMPARE(m_settings.visualizer()->defaultShotRating(), 50);
    }

    void visualizerAutoUpdateDefaultIsTrue() {
        // Default value is true — auto-update is opt-out, not opt-in.
        // Strategy: write the opposite (false) so any per-instance or NSUserDefaults
        // cache holds false, then remove the disk key and read through a fresh
        // Settings instance. If the result is true, the hardcoded default actually
        // ran (a stale cache would have returned false).
        m_settings.visualizer()->setVisualizerAutoUpdate(false);
        QVERIFY(!m_settings.visualizer()->visualizerAutoUpdate());
        QSettings raw("DecentEspresso", "DE1Qt");
        raw.remove("visualizer/autoUpdate");
        raw.sync();
        Settings fresh;
        QVERIFY(fresh.visualizer()->visualizerAutoUpdate());
    }

    void visualizerAutoUpdateRoundTrip() {
        m_settings.visualizer()->setVisualizerAutoUpdate(false);
        QCOMPARE(m_settings.visualizer()->visualizerAutoUpdate(), false);
        m_settings.visualizer()->setVisualizerAutoUpdate(true);
        QCOMPARE(m_settings.visualizer()->visualizerAutoUpdate(), true);
    }

    void ignoreVolumeWithScaleRoundTrip() {
        bool original = m_settings.brew()->ignoreVolumeWithScale();
        m_settings.brew()->setIgnoreVolumeWithScale(!original);
        QCOMPARE(m_settings.brew()->ignoreVolumeWithScale(), !original);
    }

    void dyeBeanBaseLinkRoundTripAndClear() {
        m_settings.dye()->setDyeBeanBaseId("5188");
        m_settings.dye()->setDyeBeanBaseData("{\"id\":\"5188\",\"origin\":\"Colombia\"}");
        QCOMPARE(m_settings.dye()->dyeBeanBaseId(), QString("5188"));
        QVERIFY(m_settings.dye()->dyeBeanBaseData().contains("Colombia"));

        m_settings.dye()->clearBeanBaseLink();
        QCOMPARE(m_settings.dye()->dyeBeanBaseId(), QString());
        QCOMPARE(m_settings.dye()->dyeBeanBaseData(), QString());
    }

    void dyeBeanIdentityExcludedFromExport() {
        // Bean identity (incl. the Bean Base link) lives on the active bag in
        // the shot history database and travels via the DB import path — the
        // settings JSON must not carry it (importing it on another device
        // would write through into whatever bag is active there).
        m_settings.dye()->setDyeBeanBaseId("abc-123");
        m_settings.dye()->setDyeBeanBaseData("{\"id\":\"abc-123\"}");
        const QJsonObject exported = SettingsSerializer::exportToJson(&m_settings, false);

        const QJsonObject dye = exported["dye"].toObject();
        QVERIFY(!dye.contains("beanBrand"));
        QVERIFY(!dye.contains("beanType"));
        QVERIFY(!dye.contains("roastDate"));
        QVERIFY(!dye.contains("roastLevel"));
        QVERIFY(!dye.contains("beanBaseId"));
        QVERIFY(!dye.contains("beanBaseData"));
        QVERIFY(!exported.contains("beans"));

        // Importing a legacy export's dye section must not touch the link.
        m_settings.dye()->setDyeBeanBaseId("keep-me");
        QJsonObject legacy = exported;
        QJsonObject legacyDye = legacy["dye"].toObject();
        legacyDye["beanBaseId"] = "stale-id";
        legacyDye["beanBrand"] = "Stale Roaster";
        legacy["dye"] = legacyDye;
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("SettingsSerializer: importFromJson replacing .* favorites")));
        SettingsSerializer::importFromJson(&m_settings, legacy);
        QCOMPARE(m_settings.dye()->dyeBeanBaseId(), QString("keep-me"));

        m_settings.dye()->clearBeanBaseLink();
    }

    // ==========================================
    // DYE fields (structured grinder data)
    // ==========================================

    void dyeFieldsRoundTrip() {
        m_settings.dye()->setDyeBeanBrand("Square Mile");
        QCOMPARE(m_settings.dye()->dyeBeanBrand(), QString("Square Mile"));
    }

    // ==========================================
    // Signal emission
    // ==========================================

    void targetWeightSignalEmitted() {
        QSignalSpy spy(m_settings.brew(), &SettingsBrew::targetWeightChanged);
        m_settings.brew()->setTargetWeight(m_origTargetWeight + 1.0);
        QVERIFY(spy.count() >= 1);
    }

    void themeModeSignalEmitted() {
        QString newMode = (m_origThemeMode == "dark") ? "light" : "dark";
        QSignalSpy spy(m_settings.theme(), &SettingsTheme::themeModeChanged);
        m_settings.theme()->setThemeMode(newMode);
        QVERIFY(spy.count() >= 1);
    }

    void visualizerAutoUpdateSignalEmitted() {
        QSignalSpy spy(m_settings.visualizer(), &SettingsVisualizer::visualizerAutoUpdateChanged);
        m_settings.visualizer()->setVisualizerAutoUpdate(!m_origAutoUpdate);
        QVERIFY(spy.count() >= 1);
    }

    // ==========================================
    // Cross-domain wiring (Visualizer -> Dye)
    // ==========================================

    void defaultShotRatingPropagatesToDyeEnjoyment() {
        // Settings::Settings() wires defaultShotRatingChanged -> setDyeEspressoEnjoyment
        // so any caller of SettingsVisualizer::setDefaultShotRating sees the new
        // value reflected in dye/espressoEnjoyment without going through Settings.
        const int origEnjoyment = m_settings.dye()->dyeEspressoEnjoyment();
        const int newRating = (m_origShotRating == 42) ? 43 : 42;
        m_settings.visualizer()->setDefaultShotRating(newRating);
        QCOMPARE(m_settings.dye()->dyeEspressoEnjoyment(), newRating);
        // Restore (cleanup() also restores defaultShotRating, but enjoyment is
        // a derived persisted value — leave it consistent for the next test).
        m_settings.dye()->setDyeEspressoEnjoyment(origEnjoyment);
    }

    // ==========================================
    // Edge cases
    // ==========================================

    void targetWeightZeroIsValid() {
        // 0 means disabled (no SAW)
        m_settings.brew()->setTargetWeight(0.0);
        QCOMPARE(m_settings.brew()->targetWeight(), 0.0);
    }

    void emptyScaleAddressIsValid() {
        m_settings.setScaleAddress("");
        QCOMPARE(m_settings.scaleAddress(), QString(""));
    }

    // ==========================================
    // Derived: effectiveHotWaterVolume
    // ==========================================

    void effectiveHotWaterVolumeRespectsMode() {
        QString origMode = m_settings.brew()->waterVolumeMode();
        int origVol = m_settings.brew()->waterVolume();

        m_settings.brew()->setWaterVolume(65);

        m_settings.brew()->setWaterVolumeMode("weight");
        QCOMPARE(m_settings.brew()->effectiveHotWaterVolume(), 0);

        m_settings.brew()->setWaterVolumeMode("volume");
        QCOMPARE(m_settings.brew()->effectiveHotWaterVolume(), 65);

        // Anything other than "volume" is treated as weight mode.
        m_settings.brew()->setWaterVolumeMode("something-else");
        QCOMPARE(m_settings.brew()->effectiveHotWaterVolume(), 0);

        // Lower bound: negative values from corrupted storage must clamp to 0,
        // not wrap to 255 after uint8 cast.
        m_settings.brew()->setWaterVolumeMode("volume");
        m_settings.brew()->setWaterVolume(-1);
        QCOMPARE(m_settings.brew()->effectiveHotWaterVolume(), 0);

        // Upper bound: values above 255 clamp to the BLE uint8 max.
        m_settings.brew()->setWaterVolume(500);
        QCOMPARE(m_settings.brew()->effectiveHotWaterVolume(), 255);

        m_settings.brew()->setWaterVolumeMode(origMode);
        m_settings.brew()->setWaterVolume(origVol);
    }

    // ==========================================
    // Auto-load profile settings
    // ==========================================

    void autoLoadFilenameRoundTrip() {
        m_settings.app()->setAutoLoadProfileFilename("");  // baseline
        QSignalSpy spy(m_settings.app(), &SettingsApp::autoLoadProfileFilenameChanged);
        m_settings.app()->setAutoLoadProfileFilename("my-profile");
        QCOMPARE(m_settings.app()->autoLoadProfileFilename(), QString("my-profile"));
        QCOMPARE(spy.count(), 1);
        // Setting the same value again is a no-op (no second signal).
        m_settings.app()->setAutoLoadProfileFilename("my-profile");
        QCOMPARE(spy.count(), 1);
    }

    void autoLoadRevertMinutesRoundTrip() {
        m_settings.app()->setAutoLoadRevertMinutes(5);
        QSignalSpy spy(m_settings.app(), &SettingsApp::autoLoadRevertMinutesChanged);
        m_settings.app()->setAutoLoadRevertMinutes(12);
        QCOMPARE(m_settings.app()->autoLoadRevertMinutes(), 12);
        QCOMPARE(spy.count(), 1);
    }

    void autoLoadRevertMinutesClamped() {
        // Range is 0..60 — 0 means "idle revert off" but startup + wake still fire.
        m_settings.app()->setAutoLoadRevertMinutes(-5);
        QCOMPARE(m_settings.app()->autoLoadRevertMinutes(), 0);
        m_settings.app()->setAutoLoadRevertMinutes(200);
        QCOMPARE(m_settings.app()->autoLoadRevertMinutes(), 60);
        m_settings.app()->setAutoLoadRevertMinutes(30);
        QCOMPARE(m_settings.app()->autoLoadRevertMinutes(), 30);
        m_settings.app()->setAutoLoadRevertMinutes(0);
        QCOMPARE(m_settings.app()->autoLoadRevertMinutes(), 0);
    }

    void autoLoadBundleRoundTrip() {
        m_settings.app()->setAutoLoadProfileFilename("preferred-profile");
        m_settings.app()->setAutoLoadRevertMinutes(17);

        QJsonObject bundle = SettingsSerializer::exportToJson(&m_settings, false);

        // Mutate to confirm import overwrites
        m_settings.app()->setAutoLoadProfileFilename("other-profile");
        m_settings.app()->setAutoLoadRevertMinutes(2);

        // importFromJson emits a qWarning when it replaces the favorites array,
        // even with 0 → 0 favorites. Suppress that one expected message so the
        // test doesn't fall foul of the "no warnings in tests" rule.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("SettingsSerializer: importFromJson replacing .* favorites")));

        QVERIFY(SettingsSerializer::importFromJson(&m_settings, bundle));
        QCOMPARE(m_settings.app()->autoLoadProfileFilename(), QString("preferred-profile"));
        QCOMPARE(m_settings.app()->autoLoadRevertMinutes(), 17);
    }

    void waterVesselPresetTemperatureRoundTrip() {
        // Per-preset hot-water temperature must survive an export -> import cycle.
        m_settings.brew()->addWaterVesselPreset("Tea", 250, "weight", 40, 92.0);
        const int idx = static_cast<int>(m_settings.brew()->waterVesselPresets().size()) - 1;

        QJsonObject bundle = SettingsSerializer::exportToJson(&m_settings, false);

        // Mutate the preset's temperature to confirm import overwrites it.
        m_settings.brew()->updateWaterVesselPreset(idx, "Tea", 250, "weight", 40, 70.0);
        QCOMPARE(m_settings.brew()->getWaterVesselPreset(idx)["temperature"].toDouble(), 70.0);

        // importFromJson emits an expected favorites-replacement warning (see
        // autoLoadBundleRoundTrip) — suppress it for the no-warnings-in-tests rule.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("SettingsSerializer: importFromJson replacing .* favorites")));
        QVERIFY(SettingsSerializer::importFromJson(&m_settings, bundle));

        QCOMPARE(m_settings.brew()->getWaterVesselPreset(idx)["temperature"].toDouble(), 92.0);
    }

    void temperatureUnitRoundTrip() {
        // The display temperature unit must survive an export -> import cycle. The
        // serializer's export/import key strings are hand-mirrored, so a typo on
        // either side would silently drop the setting during device-to-device
        // migration — this asserts both sides agree.
        m_settings.app()->setTemperatureUnit("fahrenheit");
        QCOMPARE(m_settings.app()->temperatureUnit(), QString("fahrenheit"));

        const QJsonObject bundle = SettingsSerializer::exportToJson(&m_settings, false);

        // Mutate to confirm import actually overwrites it (not a silent no-op).
        m_settings.app()->setTemperatureUnit("celsius");
        QCOMPARE(m_settings.app()->temperatureUnit(), QString("celsius"));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("SettingsSerializer: importFromJson replacing .* favorites")));
        QVERIFY(SettingsSerializer::importFromJson(&m_settings, bundle));

        QCOMPARE(m_settings.app()->temperatureUnit(), QString("fahrenheit"));
    }

    void temperatureUnitSetterRejectsGarbage() {
        // setTemperatureUnit whitelists {celsius, fahrenheit}: it normalises case and
        // whitespace to a valid value, and coerces anything else to celsius (loudly)
        // so imported garbage can't persist or re-export.
        m_settings.app()->setTemperatureUnit("celsius");
        // Case/whitespace normalise to a valid value — no warning, stored lowercased.
        m_settings.app()->setTemperatureUnit("  Fahrenheit ");
        QCOMPARE(m_settings.app()->temperatureUnit(), QString("fahrenheit"));
        // An unknown unit coerces to celsius, with a warning.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("invalid temperatureUnit")));
        m_settings.app()->setTemperatureUnit("kelvin");
        QCOMPARE(m_settings.app()->temperatureUnit(), QString("celsius"));
    }

    void temperatureUnitEmitsOnChangeOnly() {
        // The setter's `if (temperatureUnit() != normalized)` guard must emit exactly
        // once on a real change and stay silent on a no-op set.
        m_settings.app()->setTemperatureUnit("celsius");
        QSignalSpy spy(m_settings.app(), &SettingsApp::temperatureUnitChanged);
        m_settings.app()->setTemperatureUnit("fahrenheit");   // change -> 1 emit
        QCOMPARE(spy.count(), 1);
        m_settings.app()->setTemperatureUnit("fahrenheit");   // no-op -> no further emit
        QCOMPARE(spy.count(), 1);
    }

    void temperatureUnitDefaultIsCelsius() {
        // On fresh state (key absent) the getter default must be "celsius".
        { QSettings raw("DecentEspresso", "DE1Qt");
          raw.remove("display/temperatureUnit");
          raw.sync(); }
        QCOMPARE(m_settings.app()->temperatureUnit(), QString("celsius"));
        // cleanup() restores the original via m_origTemperatureUnit.
    }

    void waterVesselPresetLegacyTemperatureFallsBackToGlobal() {
        // A preset object that predates the per-preset temperature field (no
        // "temperature" key) must export with the device's current global
        // hot-water temperature, not 0 — this guards the migration fallback in
        // SettingsSerializer::exportToJson.
        QJsonObject legacy;
        legacy["name"] = "Legacy";
        legacy["volume"] = 200;
        legacy["mode"] = "weight";
        legacy["flowRate"] = 40;
        QJsonArray arr; arr.append(legacy);
        { QSettings raw("DecentEspresso", "DE1Qt");
          raw.setValue("water/vesselPresets", QJsonDocument(arr).toJson());
          raw.sync(); }

        // Read through a fresh Settings instance so it picks up the raw write
        // (avoids a stale per-instance QSettings cache).
        Settings fresh;
        fresh.brew()->setWaterTemperature(88.0);
        const QJsonObject bundle = SettingsSerializer::exportToJson(&fresh, false);

        const QJsonArray exported = bundle["water"].toObject()["vesselPresets"].toArray();
        QCOMPARE(exported.size(), 1);
        QCOMPARE(exported[0].toObject()["temperature"].toDouble(), 88.0);
    }

    void steamPitcherPresetTemperatureRoundTrip() {
        // Per-pitcher steam temperature must survive an export -> import cycle.
        m_settings.brew()->addSteamPitcherPreset("Latte", 45, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;

        QJsonObject bundle = SettingsSerializer::exportToJson(&m_settings, false);

        // Mutate the preset's temperature to confirm import overwrites it.
        m_settings.brew()->updateSteamPitcherPreset(idx, "Latte", 45, 150, 120.0);
        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(idx)["temperature"].toDouble(), 120.0);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("SettingsSerializer: importFromJson replacing .* favorites")));
        QVERIFY(SettingsSerializer::importFromJson(&m_settings, bundle));

        QCOMPARE(m_settings.brew()->getSteamPitcherPreset(idx)["temperature"].toDouble(), 135.0);
    }

    void effectiveSteamDurationSecFallsBackToBaseDuration() {
        // Weight-timed steaming OFF: scaledSteamTime() always yields 0, so the
        // effective duration must be the preset's fixed duration, not 0.
        m_settings.brew()->addSteamPitcherPreset("Latte", 45, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;
        m_settings.brew()->setSteamPitcherCalibration(idx, 300.0);
        // Disable AFTER calibrating — setSteamPitcherCalibration re-enables the toggle
        // as its explicit opt-in side effect, which would put this test back on the
        // scaled path. Milk (600) ≠ calibration (300) so scaled (90) and base (45)
        // are distinguishable: only the toggle-off gate can produce 45 here.
        m_settings.brew()->setMilkAutoCaptureEnabled(false);

        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 600.0), 45);
    }

    void effectiveSteamDurationSecClampsScaledTime() {
        // The scaled path is clamped to [5,120]s; a huge milk-to-calibration ratio
        // must cap at 120, not program a multi-minute steam.
        m_settings.brew()->addSteamPitcherPreset("Jug", 30, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;
        m_settings.brew()->setSteamPitcherCalibration(idx, 100.0);  // also enables the toggle

        // Unclamped: 30 * (600/100) = 180 → clamped to 120.
        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 600.0), 120);
        // Floor: 30 * (10/100) = 3 → clamped to 5, not a blink-and-miss 3s steam.
        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 10.0), 5);
    }

    void effectiveSteamDurationSecWarnsOnCorruptZeroDuration() {
        // An enabled preset with no positive duration is corrupt (hand-edited or a
        // failed import). The helper must warn — loud and greppable — and still
        // return 0 rather than inventing a time.
        m_settings.brew()->addSteamPitcherPreset("Corrupt", 0, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("no positive duration")));
        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 0.0), 0);
    }

    void effectiveSteamDurationSecUsesScaledTimeWhenAvailable() {
        // Weight-timed steaming ON + calibrated preset + positive milk: the scaled
        // value wins over the base duration (the PR's core new behavior).
        m_settings.brew()->setMilkAutoCaptureEnabled(true);
        m_settings.brew()->addSteamPitcherPreset("Latte", 30, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;
        m_settings.brew()->setSteamPitcherCalibration(idx, 200.0);

        // duration * (milk / calibMilk) = 30 * (400/200) = 60
        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 400.0), 60);
    }

    void effectiveSteamDurationSecZeroForDisabledPreset() {
        m_settings.brew()->setMilkAutoCaptureEnabled(true);
        m_settings.brew()->addSteamPitcherPresetDisabled("Off");
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;

        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 300.0), 0);
    }

    void effectiveSteamDurationSecZeroForMissingIndex() {
        // A stale index (e.g. every preset deleted) must return 0 AND warn — unlike a
        // disabled preset it's never deliberate, and QML guards can't detect it (an
        // empty QVariantMap is a truthy {} in JS).
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("no steam pitcher preset at index")));
        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(999, 300.0), 0);
    }

    void effectiveSteamDurationSecBaseWhenNoMilk() {
        // Calibrated preset, weight-timing ON, but no milk available (no scale, nothing
        // captured): must yield the base duration — not 0, and not the 5s clamp floor.
        // The SteamItem popup tap relies on exactly this cell when tapped scale-less.
        m_settings.brew()->addSteamPitcherPreset("Latte", 45, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;
        m_settings.brew()->setSteamPitcherCalibration(idx, 300.0);  // also enables the toggle

        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 0.0), 45);
    }

    void effectiveSteamDurationSecFallsBackWhenUncalibrated() {
        // Weight-timing on but no calibration recorded: scaledSteamTime() yields 0
        // (calibMilkG <= 0), so the base duration must be used, not 0.
        m_settings.brew()->setMilkAutoCaptureEnabled(true);
        m_settings.brew()->addSteamPitcherPreset("Cortado", 20, 150, 135.0);
        const int idx = static_cast<int>(m_settings.brew()->steamPitcherPresets().size()) - 1;

        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(idx, 300.0), 20);
    }

    void steamPitcherLegacyTemperatureFallsBackToGlobal() {
        // A pitcher preset that predates the per-pitcher temperature field (no
        // "temperature" key) must export with the device's current global steam
        // temperature, not 0 — guards the migration fallback in exportToJson.
        QJsonObject legacy;
        legacy["name"] = "Legacy";
        legacy["duration"] = 30;
        legacy["flow"] = 150;
        QJsonArray arr; arr.append(legacy);
        { QSettings raw("DecentEspresso", "DE1Qt");
          raw.setValue("steam/pitcherPresets", QJsonDocument(arr).toJson());
          raw.sync(); }

        // Read through a fresh Settings instance so it picks up the raw write.
        Settings fresh;
        fresh.brew()->setSteamTemperature(142.0);
        const QJsonObject bundle = SettingsSerializer::exportToJson(&fresh, false);

        const QJsonArray exported = bundle["steam"].toObject()["pitcherPresets"].toArray();
        QCOMPARE(exported.size(), 1);
        QCOMPARE(exported[0].toObject()["temperature"].toDouble(), 142.0);
    }

    // ==========================================
    // ==========================================
    // Layout: configurable-type allowlist + per-instance "configured" gate
    // ==========================================

    // typeHasOptions is derived from the readout capability schema plus the
    // bespoke-editor set (single source of truth for the editor's gear
    // indicator + open routing). Pin the configurable set so dropping a type
    // or breaking the screensaver prefix match is a visible, deliberate change.
    void typeHasOptionsAllowlist() {
        const QStringList configurable = {
            "custom", "scaleWeight", "shotPlan", "sleep", "machineStatus",
            "temperature", "steamTemperature", "waterLevel", "clock", "lastShot",
            "batteryLevel", "scaleBattery", "doseWeight", "milkWeight", "profileName"
        };
        for (const QString& t : configurable)
            QVERIFY2(SettingsNetwork::typeHasOptions(t), qPrintable("expected configurable: " + t));

        // "shotPlan" is the only plan widget type; "plan"/"steamPlan" were development
        // working names, appear in no saved layout, and must never enter the allowlist.
        QVERIFY(!SettingsNetwork::typeHasOptions("steamPlan"));
        QVERIFY(!SettingsNetwork::typeHasOptions("plan"));

        // Any screensaver* type is configurable (prefix match).
        QVERIFY(SettingsNetwork::typeHasOptions("screensaver"));
        QVERIFY(SettingsNetwork::typeHasOptions("screensaverFlipClock"));

        // Plain widgets and unknown/empty types are not.
        QVERIFY(!SettingsNetwork::typeHasOptions("spacer"));
        QVERIFY(!SettingsNetwork::typeHasOptions("separator"));
        QVERIFY(!SettingsNetwork::typeHasOptions("pageTitle"));
        QVERIFY(!SettingsNetwork::typeHasOptions("espresso"));
        QVERIFY(!SettingsNetwork::typeHasOptions(""));
    }

    // The capability schema drives the unified readout options editor (which
    // sections it shows) and the web editor's injected WIDGET_CAPABILITIES.
    // Pin the per-type keys and the schema↔typeHasOptions agreement.
    void optionKeysForTypeSchema() {
        QCOMPARE(SettingsNetwork::optionKeysForType("scaleWeight"),
                 (QStringList{"dataMode", "displayMode", "showRatio", "color"}));
        QCOMPARE(SettingsNetwork::optionKeysForType("temperature"),
                 (QStringList{"displayMode", "color"}));
        QCOMPARE(SettingsNetwork::optionKeysForType("batteryLevel"),
                 (QStringList{"displayMode", "color"}));
        // profileName has no meaningful icon form — color only.
        QCOMPARE(SettingsNetwork::optionKeysForType("profileName"), (QStringList{"color"}));
        // Bespoke-editor and unknown types carry no readout keys.
        QVERIFY(SettingsNetwork::optionKeysForType("custom").isEmpty());
        QVERIFY(SettingsNetwork::optionKeysForType("shotPlan").isEmpty());
        QVERIFY(SettingsNetwork::optionKeysForType("espresso").isEmpty());
        QVERIFY(SettingsNetwork::optionKeysForType("").isEmpty());

        // Every type with readout keys must be configurable.
        const QStringList readouts = {
            "machineStatus", "temperature", "steamTemperature", "waterLevel", "clock",
            "scaleWeight", "batteryLevel", "scaleBattery", "doseWeight", "milkWeight",
            "profileName"
        };
        for (const QString& t : readouts) {
            QVERIFY2(!SettingsNetwork::optionKeysForType(t).isEmpty(), qPrintable("expected keys: " + t));
            QVERIFY2(SettingsNetwork::typeHasOptions(t), qPrintable("schema/gate disagree: " + t));
        }

        // The web editor's JSON carries the same table: readouts map to their
        // keys, bespoke types to an empty array (present = has options).
        const QJsonObject caps = SettingsNetwork::readoutCapabilitiesJson();
        for (const QString& t : readouts) {
            QVERIFY2(caps.contains(t), qPrintable("missing from web JSON: " + t));
            QCOMPARE(caps.value(t).toArray(),
                     QJsonArray::fromStringList(SettingsNetwork::optionKeysForType(t)));
        }
        for (const QString& t : {QStringLiteral("custom"), QStringLiteral("sleep"),
                                 QStringLiteral("shotPlan"), QStringLiteral("lastShot")}) {
            QVERIFY2(caps.contains(t), qPrintable("bespoke missing from web JSON: " + t));
            QVERIFY(caps.value(t).toArray().isEmpty());
        }
        QVERIFY(!caps.contains("espresso"));

        // Generic invariants over EVERY entry, so future types are covered
        // without extending the hand-pinned lists above:
        const QSet<QString> knownKeys = {
            QStringLiteral("dataMode"), QStringLiteral("displayMode"),
            QStringLiteral("showRatio"), QStringLiteral("color")
        };
        for (const QString& t : caps.keys()) {
            // Web JSON and the QML-facing keys must agree for every type. A
            // type placed in both the schema and the bespoke set would break
            // this (JSON's empty array vs the schema's keys) — exactly the
            // app/web divergence this table exists to prevent.
            QCOMPARE(caps.value(t).toArray(),
                     QJsonArray::fromStringList(SettingsNetwork::optionKeysForType(t)));
            // Screensavers stay a prefix rule on both sides — a screensaver
            // schema entry would give the web editor selectors the QML
            // screensaver popup doesn't have.
            QVERIFY2(!t.startsWith("screensaver"), qPrintable("screensaver leaked into schema: " + t));
            // Editors dispatch on exactly these key strings; a typo'd key
            // ("colour", "display") would silently render nothing anywhere.
            const QJsonArray keys = caps.value(t).toArray();
            for (const auto& k : keys)
                QVERIFY2(knownKeys.contains(k.toString()), qPrintable(t + " has unknown key: " + k.toString()));
        }
        QVERIFY(SettingsNetwork::optionKeysForType("screensaverFlipClock").isEmpty());
        // Pin the entry count so adding a configurable type is as deliberate a
        // change as removing one (every entry changes web-editor behavior).
        QCOMPARE(caps.size(), 15);
    }

    // The widget catalog drives the in-app palette, chip names, the library
    // card, and the web editor's injected WIDGET_CATALOG. Pin invariants, not
    // the full list, so adding a widget stays a one-table edit.
    void widgetCatalogInvariants() {
        const QVariantList catalog = SettingsNetwork::widgetCatalog();
        const QVariantMap chips = SettingsNetwork::widgetChipNames();
        const QVariantList cats = SettingsNetwork::widgetCategoryNames();
        // The `cat` integers in the catalog are positional — pin the category
        // names in order so a reorder without renumbering is caught.
        QCOMPARE(cats.size(), 4);
        const QStringList kCatOrder = {"Actions", "Readouts", "Utility", "Screensavers"};
        for (int i = 0; i < cats.size(); ++i)
            QCOMPARE(cats[i].toMap().value("fallback").toString(), kCatOrder[i]);
        QVERIFY(catalog.size() >= 36);

        QSet<QString> seen;
        for (const QVariant& v : catalog) {
            const QVariantMap e = v.toMap();
            const QString type = e.value("type").toString();
            QVERIFY2(!seen.contains(type), qPrintable("duplicate catalog type: " + type));
            seen.insert(type);
            const int cat = e.value("cat").toInt();
            QVERIFY2(cat >= 0 && cat < cats.size(), qPrintable("bad category: " + type));
            QVERIFY2(!e.value("label").toString().isEmpty(), qPrintable("empty label: " + type));
            QVERIFY2(!e.value("labelKey").toString().isEmpty(), qPrintable("empty labelKey: " + type));
            QVERIFY2(chips.contains(type), qPrintable("missing chip name: " + type));
            // A typo'd flag would silently lose the web chip/menu coloring.
            const QString flag = e.value("flag").toString();
            QVERIFY2(flag.isEmpty() || flag == "special" || flag == "screensaver",
                     qPrintable(type + " has unknown flag: " + flag));
        }
        // Every chip entry (incl. aliases) carries a usable key + fallback —
        // an aggregate-initialized row with missing trailing fields would
        // otherwise render blank chip labels.
        for (auto it = chips.constBegin(); it != chips.constEnd(); ++it) {
            const QVariantMap c = it.value().toMap();
            QVERIFY2(!c.value("key").toString().isEmpty(), qPrintable("empty chip key: " + it.key()));
            QVERIFY2(!c.value("fallback").toString().isEmpty(), qPrintable("empty chip fallback: " + it.key()));
        }
        // Legacy alias keeps a chip name without appearing in the palette.
        QVERIFY(chips.contains("connectionStatus"));
        QVERIFY(!seen.contains("connectionStatus"));

        // Every configurable type is a real, placeable catalog type — the
        // capability schema and the catalog cannot drift apart.
        const QJsonObject caps = SettingsNetwork::readoutCapabilitiesJson();
        for (const QString& t : caps.keys())
            QVERIFY2(seen.contains(t), qPrintable("configurable type missing from catalog: " + t));

        // Web JSON parity: types/chipNames/catNames mirror the same table.
        const QJsonObject webCatalog = SettingsNetwork::widgetCatalogJson();
        QCOMPARE(webCatalog.value("types").toArray().size(), catalog.size());
        QCOMPARE(webCatalog.value("chipNames").toObject().size(), chips.size());
        QCOMPARE(webCatalog.value("catNames").toArray().size(), cats.size());
    }

    // An absent stored displayMode always means "today's rendering" — icon for
    // the battery readouts, text everywhere else. Declared once; pin it.
    void displayModeDefaultsPinned() {
        QCOMPARE(SettingsNetwork::defaultDisplayModeForType("batteryLevel"), QStringLiteral("icon"));
        QCOMPARE(SettingsNetwork::defaultDisplayModeForType("scaleBattery"), QStringLiteral("icon"));
        QCOMPARE(SettingsNetwork::defaultDisplayModeForType("temperature"), QStringLiteral("text"));
        QCOMPARE(SettingsNetwork::defaultDisplayModeForType("scaleWeight"), QStringLiteral("text"));
        QCOMPARE(SettingsNetwork::defaultDisplayModeForType(""), QStringLiteral("text"));
        // Web parity: only non-text defaults are injected.
        const QJsonObject d = SettingsNetwork::displayModeDefaultsJson();
        QCOMPARE(d.size(), 2);
        QCOMPARE(d.value("batteryLevel").toString(), QStringLiteral("icon"));
        QCOMPARE(d.value("scaleBattery").toString(), QStringLiteral("icon"));
        // Every defaulted type must support displayMode in the schema.
        for (const QString& t : d.keys())
            QVERIFY(SettingsNetwork::optionKeysForType(t).contains("displayMode"));
    }

    // itemIsConfigured gates the remove-confirmation that protects a set-up
    // widget from an accidental tap. Cover its two true-branches (configurable
    // type; or a plain type carrying an extra property) and the false cases.
    void itemIsConfiguredBranches() {
        SettingsNetwork* net = m_settings.network();
        const QString orig = net->layoutConfiguration();

        net->setLayoutConfiguration(QStringLiteral(
            "{\"version\":1,\"zones\":{\"statusBar\":["
            "{\"type\":\"temperature\",\"id\":\"t1\"},"
            "{\"type\":\"separator\",\"id\":\"sep1\"},"
            "{\"type\":\"spacer\",\"id\":\"sp1\",\"width\":20}"
            "]}}"));

        // Configurable type → configured, even with only type/id stored.
        QVERIFY(net->itemIsConfigured("t1"));
        // Plain type, only type/id → not configured.
        QVERIFY(!net->itemIsConfigured("sep1"));
        // Plain type but carries an extra (non type/id) property → configured.
        QVERIFY(net->itemIsConfigured("sp1"));
        // Unknown id → empty props → not configured.
        QVERIFY(!net->itemIsConfigured("does_not_exist"));

        net->setLayoutConfiguration(orig);
    }

    // Array-valued item properties: setItemPropertyList is the typed path QML
    // must use (a JS array through the generic QVariant setter arrives as a
    // wrapped QJSValue and would be stored as null). Regression for the Shot
    // Plan chip editor saving "shotPlanItems": null, which read back as absent
    // and silently reverted the user's edits (#1426).
    void itemPropertyListPersistsArrays() {
        SettingsNetwork* net = m_settings.network();
        const QString orig = net->layoutConfiguration();

        net->setLayoutConfiguration(QStringLiteral(
            "{\"version\":1,\"zones\":{\"centerMiddle\":["
            "{\"type\":\"shotPlan\",\"id\":\"plan1\"}"
            "]}}"));

        // Deliberately NON-canonical order (canonical is doseYield, ..., roaster):
        // a regression that sorts/normalizes the list on write or read would
        // still pass with an in-order payload, and order IS the feature.
        QVERIFY(net->setItemPropertyList("plan1", "shotPlanItems",
                QVariantList{QStringLiteral("roaster"), QStringLiteral("doseYield"), QStringLiteral("coffee")}));
        QVariantMap props = net->getItemProperties("plan1");
        QCOMPARE(props.value("shotPlanItems").toStringList(),
                 QStringList({QStringLiteral("roaster"), QStringLiteral("doseYield"), QStringLiteral("coffee")}));

        // An empty array is a valid "show nothing" config: it must survive as a
        // present, empty list — not collapse to null (which reads as absent and
        // re-triggers legacy derivation).
        net->setItemPropertyList("plan1", "shotPlanItems", QVariantList{});
        props = net->getItemProperties("plan1");
        QVERIFY(props.contains("shotPlanItems"));
        QVERIFY(!props.value("shotPlanItems").isNull());
        QVERIFY(props.value("shotPlanItems").toList().isEmpty());

        // The generic setter still takes a plain QVariantList (the web editor's
        // path — JSON arrays arrive as QVariantList, not QJSValue).
        net->setItemProperty("plan1", "shotPlanItems",
                             QVariantList{QStringLiteral("grind")});
        props = net->getItemProperties("plan1");
        QCOMPARE(props.value("shotPlanItems").toStringList(),
                 QStringList{QStringLiteral("grind")});

        // A write to a stale/unknown itemId must report failure (and warn), not
        // silently no-op with the stored state unchanged.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("no layout item with id"));
        QVERIFY(!net->setItemProperty("gone", "shotPlanSentence", true));

        // An invalid QVariant (JS undefined / missing web value) must be
        // refused, not stored as JSON null; the previous value survives.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refusing invalid value"));
        QVERIFY(!net->setItemProperty("plan1", "shotPlanItems", QVariant()));
        props = net->getItemProperties("plan1");
        QCOMPARE(props.value("shotPlanItems").toStringList(),
                 QStringList{QStringLiteral("grind")});

        net->setLayoutConfiguration(orig);
    }

    // ==========================================
    // Known scales: invariant + heal
    // ==========================================

    // Adding the first known scale into an empty primary state must auto-
    // promote that entry to primary. Without this, the Known Devices picker
    // renders with currentIndex == -1 (nothing selected) even though the
    // list has entries — the user has to tap one to make their selection
    // "stick" as primary, which is the bug the user observed in #1281.
    void addKnownScalePromotesToPrimaryWhenNoneSet() {
        KnownScalesGuard guard(&m_settings);
        // Guard cleared knownScales; also clear primary so we're in the
        // empty-primary state.
        m_settings.setPrimaryScale(QString());

        m_settings.addKnownScale("AA:BB:CC:DD:EE:FF", "decent", "Test Scale");

        QCOMPARE(m_settings.primaryScaleAddress(), QString("AA:BB:CC:DD:EE:FF"));
        QCOMPARE(m_settings.scaleAddress(), QString("AA:BB:CC:DD:EE:FF"));
        QCOMPARE(m_settings.scaleType(), QString("decent"));
        QCOMPARE(m_settings.scaleName(), QString("Test Scale"));

        // Adding a SECOND scale must NOT change primary (the first one's
        // promotion is sticky; the user explicitly switches via setPrimaryScale).
        m_settings.addKnownScale("11:22:33:44:55:66", "decent", "Second Scale");
        QCOMPARE(m_settings.primaryScaleAddress(), QString("AA:BB:CC:DD:EE:FF"));
    }

    // Re-adding an existing known scale must repair the primary invariant
    // if primary was cleared between the original add and the re-add. main.cpp
    // calls addKnownScale on every scale connect, so this is the natural
    // healing path if some other code (clearSavedScale via MCP, a future
    // migration, a test fixture) left primary empty while the entry still
    // existed. Without this, the early-return on existing entries would skip
    // the invariant check and the Known Devices picker would render with
    // nothing selected even though the list has entries.
    void addKnownScaleRepairsPrimaryOnReAddOfExistingEntry() {
        KnownScalesGuard guard(&m_settings);

        // First add — invariant auto-promotes the new entry.
        m_settings.addKnownScale("AA:BB:CC:DD:EE:01", "decent", "Test Scale");
        QCOMPARE(m_settings.primaryScaleAddress(), QString("AA:BB:CC:DD:EE:01"));

        // Force the "primary cleared, entry remains" state — unreachable in
        // normal flow but reproducible at this layer.
        m_settings.setPrimaryScale(QString());
        QVERIFY(m_settings.primaryScaleAddress().isEmpty());

        // Re-add the same address — the same call main.cpp makes on every
        // connect. Pre-fix this hit the early-return path and left primary
        // empty; post-fix it repairs the invariant.
        m_settings.addKnownScale("AA:BB:CC:DD:EE:01", "decent", "Test Scale");
        QCOMPARE(m_settings.primaryScaleAddress(), QString("AA:BB:CC:DD:EE:01"));
        // Legacy keys are also re-synced via setPrimaryScale's normal path.
        QCOMPARE(m_settings.scaleAddress(), QString("AA:BB:CC:DD:EE:01"));
    }

    // Settings' startup orphan-heal must repair the BOTH directions of the
    // scale/address ↔ knownScales/primaryAddress relationship:
    //   forward — primary empty / stale → promote first known and write legacy
    //   reverse — primary valid but legacy empty/stale → sync legacy from primary
    // The reverse case was the actual #1281 follow-up bug. QML's startup hook
    // gates `tryDirectConnectToScale` on `primaryScaleAddress`, but main.cpp's
    // BLEManager load (pre-fix) read the legacy `scale/address`. A drift
    // between the two stranded the user with no auto-connect at startup.
    void orphanHealRepairsLegacyAddressFromValidPrimary() {
        KnownScalesGuard guard(&m_settings);

        // Pre-seed: knownScales has an entry, primary points to it, but the
        // legacy keys are stale/empty (simulates the divergence). Write
        // directly to QSettings so the heal sees the pre-state on next ctor.
        QSettings raw("DecentEspresso", "DE1Qt");
        raw.remove("knownScales/scales");
        raw.beginWriteArray("knownScales/scales");
        raw.setArrayIndex(0);
        raw.setValue("address", "PRIMARY:AA:11");
        raw.setValue("type", "decent");
        raw.setValue("name", "Healed Scale");
        raw.endArray();
        raw.setValue("knownScales/primaryAddress", "PRIMARY:AA:11");
        raw.setValue("scale/address", "");
        raw.setValue("scale/type", "");
        raw.setValue("scale/name", "");
        raw.sync();

        // Construct a fresh Settings — the orphan-heal in its constructor
        // sees the divergence and syncs legacy from primary.
        Settings healed;

        QCOMPARE(healed.scaleAddress(), QString("PRIMARY:AA:11"));
        QCOMPARE(healed.scaleType(), QString("decent"));
        QCOMPARE(healed.scaleName(), QString("Healed Scale"));
        // primaryScaleAddress unchanged (it was already correct).
        QCOMPARE(healed.primaryScaleAddress(), QString("PRIMARY:AA:11"));
    }

    // ==========================================
    // Recipes (add-recipes): active id + pinned-grind write-through routing
    // ==========================================

    void activeRecipeIdRoundTrip() {
        m_settings.dye()->setActiveRecipeId(-1);
        QSignalSpy spy(m_settings.dye(), &SettingsDye::activeRecipeIdChanged);
        m_settings.dye()->setActiveRecipeId(42);
        QCOMPARE(m_settings.dye()->activeRecipeId(), 42);
        QCOMPARE(spy.count(), 1);
        m_settings.dye()->setActiveRecipeId(42);  // same value: no signal
        QCOMPARE(spy.count(), 1);
    }

    void grindPinSuspendsBagWriteThrough() {
        // A grind edit normally writes through to the active bag (bean dial
        // memory). While the active recipe PINS its grind, the bag must NOT
        // follow — the pin is the recipe's private dial and sibling recipes
        // inherit the bag's value.
        QTemporaryDir dir;
        const QString dbPath = dir.filePath("grind_pin.db");
        qint64 bagId = 0;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "grind_pin_setup");
            db.setDatabaseName(dbPath);
            QVERIFY(db.open());
            QVERIFY(CoffeeBagStorage::ensureTableStatic(db));
            CoffeeBag bag;
            bag.roasterName = "Test Roaster";
            bag.coffeeName = "Test Coffee";
            bag.grinderSetting = "1.0";
            bagId = CoffeeBagStorage::insertBagStatic(db, bag);
            QVERIFY(bagId > 0);
        }
        QSqlDatabase::removeDatabase("grind_pin_setup");

        CoffeeBagStorage storage;
        storage.initialize(dbPath);
        m_settings.dye()->setBagStorage(&storage);
        m_settings.dye()->setActiveBagId(static_cast<int>(bagId));
        // Bag apply is async: the dye cache adopts the bag's grind.
        QTRY_COMPARE(m_settings.dye()->dyeGrinderSetting(), QString("1.0"));

        // Unsuspended: the edit reaches the bag row.
        m_settings.dye()->setDyeGrinderSetting("2.0");
        {
            QSignalSpy ready(&storage, &CoffeeBagStorage::bagReady);
            // The worker is FIFO, so this read drains after the write above.
            storage.requestBag(bagId);
            QTRY_VERIFY(ready.count() >= 1);
            QCOMPARE(ready.last().at(1).toMap().value("grinderSetting").toString(),
                     QString("2.0"));
        }

        // Suspended (recipe pin active): the edit stays out of the bag.
        m_settings.dye()->setGrindBagWriteThroughSuspended(true);
        m_settings.dye()->setDyeGrinderSetting("3.0");
        {
            QSignalSpy ready(&storage, &CoffeeBagStorage::bagReady);
            storage.requestBag(bagId);
            QTRY_VERIFY(ready.count() >= 1);
            QCOMPARE(ready.last().at(1).toMap().value("grinderSetting").toString(),
                     QString("2.0"));  // unchanged — the pin never landed
        }
    }
};

QTEST_GUILESS_MAIN(tst_Settings)
#include "tst_settings.moc"
