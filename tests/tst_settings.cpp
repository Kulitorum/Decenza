#include <QtTest>
#include <QSettings>
#include <QSignalSpy>

#include "core/settings.h"
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
        QCOMPARE(m_settings.brew()->effectiveSteamDurationSec(999, 300.0), 0);
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

    // typeHasOptions is the single source of truth for which widget types expose
    // per-instance options (drives the editor's gear indicator + open routing).
    // Pin the allowlist so dropping a type or breaking the screensaver prefix
    // match is a visible, deliberate change.
    void typeHasOptionsAllowlist() {
        const QStringList configurable = {
            "custom", "scaleWeight", "shotPlan", "sleep", "machineStatus",
            "temperature", "steamTemperature", "waterLevel", "clock", "lastShot"
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
};

QTEST_GUILESS_MAIN(tst_Settings)
#include "tst_settings.moc"
