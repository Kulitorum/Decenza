#include <QtTest>
#include <QSettings>
#include <QSignalSpy>

#include "core/settings.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/settings_theme.h"
#include "core/settings_visualizer.h"
#include "core/settings_beanbase.h"
#include "core/settingsserializer.h"
#include <QJsonObject>
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
    double m_origSteamTemp;
    QString m_origScaleAddress;
    QString m_origThemeMode;
    int m_origShotRating;
    bool m_origIgnoreVolume;
    bool m_origAutoUpdate;
    QString m_origDyeBeanBrand;
    QString m_origAutoLoadFilename;
    int m_origAutoLoadRevertMinutes;
    QString m_origBeanBaseApiKey;

private slots:

    void init() {
        // Save all originals before each test
        m_origTargetWeight = m_settings.brew()->targetWeight();
        m_origSteamTemp = m_settings.brew()->steamTemperature();
        m_origScaleAddress = m_settings.scaleAddress();
        m_origThemeMode = m_settings.theme()->themeMode();
        m_origShotRating = m_settings.visualizer()->defaultShotRating();
        m_origIgnoreVolume = m_settings.brew()->ignoreVolumeWithScale();
        m_origAutoUpdate = m_settings.visualizer()->visualizerAutoUpdate();
        m_origDyeBeanBrand = m_settings.dye()->dyeBeanBrand();
        m_origAutoLoadFilename = m_settings.app()->autoLoadProfileFilename();
        m_origAutoLoadRevertMinutes = m_settings.app()->autoLoadRevertMinutes();
        m_origBeanBaseApiKey = m_settings.beanbase()->beanBaseApiKey();
    }

    void cleanup() {
        // Restore all originals after each test (runs even on assertion failure)
        m_settings.brew()->setTargetWeight(m_origTargetWeight);
        m_settings.brew()->setSteamTemperature(m_origSteamTemp);
        m_settings.setScaleAddress(m_origScaleAddress);
        m_settings.theme()->setThemeMode(m_origThemeMode);
        m_settings.visualizer()->setDefaultShotRating(m_origShotRating);
        m_settings.brew()->setIgnoreVolumeWithScale(m_origIgnoreVolume);
        m_settings.visualizer()->setVisualizerAutoUpdate(m_origAutoUpdate);
        m_settings.dye()->setDyeBeanBrand(m_origDyeBeanBrand);
        m_settings.app()->setAutoLoadProfileFilename(m_origAutoLoadFilename);
        m_settings.app()->setAutoLoadRevertMinutes(m_origAutoLoadRevertMinutes);
        m_settings.beanbase()->setBeanBaseApiKey(m_origBeanBaseApiKey);
    }

    // ==========================================
    // Property round-trip (set -> get)
    // ==========================================

    void targetWeightRoundTrip() {
        m_settings.brew()->setTargetWeight(42.5);
        QCOMPARE(m_settings.brew()->targetWeight(), 42.5);
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

    // ==========================================
    // Bean Base (Loffee Labs) API key
    // ==========================================

    void beanBaseApiKeyDefaultIsEmpty() {
        // Fresh key (never written) reads back as empty string.
        QSettings raw("DecentEspresso", "DE1Qt");
        raw.remove("beanbase/apiKey");
        raw.sync();
        Settings fresh;
        QCOMPARE(fresh.beanbase()->beanBaseApiKey(), QString());
    }

    void beanBaseApiKeyRoundTrip() {
        m_settings.beanbase()->setBeanBaseApiKey("loffee_test_key_123");
        QCOMPARE(m_settings.beanbase()->beanBaseApiKey(), QString("loffee_test_key_123"));
        m_settings.beanbase()->setBeanBaseApiKey("");
        QCOMPARE(m_settings.beanbase()->beanBaseApiKey(), QString());
    }

    void beanBaseApiKeySignalEmitted() {
        QSignalSpy spy(m_settings.beanbase(), &SettingsBeanBase::beanBaseApiKeyChanged);
        m_settings.beanbase()->setBeanBaseApiKey("abc");
        QCOMPARE(spy.count(), 1);
        // Setting the same value again does not re-emit.
        m_settings.beanbase()->setBeanBaseApiKey("abc");
        QCOMPARE(spy.count(), 1);
    }

    void beanBaseApiKeyExcludedFromNonSensitiveExport() {
        // The key is a sensitive credential — it must only appear in the
        // export when includeSensitive is true.
        m_settings.beanbase()->setBeanBaseApiKey("secret_key");

        QJsonObject withoutSensitive = SettingsSerializer::exportToJson(&m_settings, false);
        QVERIFY(!withoutSensitive.contains("beanbase"));

        QJsonObject withSensitive = SettingsSerializer::exportToJson(&m_settings, true);
        QVERIFY(withSensitive.contains("beanbase"));
        QCOMPARE(withSensitive["beanbase"].toObject()["apiKey"].toString(), QString("secret_key"));
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
    // beansModified recompute chain
    // ==========================================

    void dyeBeanWeightDoesNotAffectBeansModified() {
        // dyeBeanWeight is NOT one of the fields recomputeBeansModified compares
        // against the selected preset (only brand/type/roast/grinder*/barista).
        // This documents the contract: changing bean weight on a saved preset
        // doesn't flag it as modified.
        QSignalSpy spy(m_settings.dye(), &SettingsDye::beansModifiedChanged);
        const double orig = m_settings.dye()->dyeBeanWeight();
        m_settings.dye()->setDyeBeanWeight(orig + 0.5);
        QCOMPARE(spy.count(), 0);
        m_settings.dye()->setDyeBeanWeight(orig);
    }

    void dyeBeanBrandFiresBeansModifiedChain() {
        // Proves the dyeBeanBrandChanged -> recomputeBeansModified -> beansModifiedChanged
        // wiring is intact after the split (was previously inside Settings::Settings()).
        // Set up: select a preset whose brand differs from current dye.
        const QString origBrand = m_settings.dye()->dyeBeanBrand();
        m_settings.dye()->addBeanPreset("__test_preset__", "BrandA", "TypeA",
                                        "", "", "", "", "", "", "");
        const int idx = m_settings.dye()->findBeanPresetByName("__test_preset__");
        QVERIFY(idx >= 0);
        m_settings.dye()->setSelectedBeanPreset(idx);
        m_settings.dye()->applyBeanPreset(idx);  // dye now matches preset, beansModified=false
        QVERIFY(!m_settings.dye()->beansModified());

        QSignalSpy spy(m_settings.dye(), &SettingsDye::beansModifiedChanged);
        m_settings.dye()->setDyeBeanBrand("BrandB");
        QVERIFY(spy.count() >= 1);
        QVERIFY(m_settings.dye()->beansModified());

        // Cleanup
        m_settings.dye()->setSelectedBeanPreset(-1);
        m_settings.dye()->removeBeanPreset(idx);
        m_settings.dye()->setDyeBeanBrand(origBrand);
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
