#include <QtTest>
#include <QSignalSpy>

#include "core/settings.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/settings_theme.h"
#include "core/settings_visualizer.h"

// Test Settings property round-trip and signal emission.
// Settings uses QSettings("DecentEspresso", "DE1Qt") which reads/writes to
// the system settings store. Tests save originals in init() and restore in
// cleanup() (guaranteed to run even if assertions fail mid-test).

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
    QString m_origDyeBeanBrand;

private slots:

    void init() {
        // Save all originals before each test
        m_origTargetWeight = m_settings.brew()->targetWeight();
        m_origSteamTemp = m_settings.brew()->steamTemperature();
        m_origScaleAddress = m_settings.scaleAddress();
        m_origThemeMode = m_settings.theme()->themeMode();
        m_origShotRating = m_settings.visualizer()->defaultShotRating();
        m_origIgnoreVolume = m_settings.brew()->ignoreVolumeWithScale();
        m_origDyeBeanBrand = m_settings.dye()->dyeBeanBrand();
    }

    void cleanup() {
        // Restore all originals after each test (runs even on assertion failure)
        m_settings.brew()->setTargetWeight(m_origTargetWeight);
        m_settings.brew()->setSteamTemperature(m_origSteamTemp);
        m_settings.setScaleAddress(m_origScaleAddress);
        m_settings.theme()->setThemeMode(m_origThemeMode);
        m_settings.visualizer()->setDefaultShotRating(m_origShotRating);
        m_settings.brew()->setIgnoreVolumeWithScale(m_origIgnoreVolume);
        m_settings.dye()->setDyeBeanBrand(m_origDyeBeanBrand);
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

    void ignoreVolumeWithScaleRoundTrip() {
        bool original = m_settings.brew()->ignoreVolumeWithScale();
        m_settings.brew()->setIgnoreVolumeWithScale(!original);
        QCOMPARE(m_settings.brew()->ignoreVolumeWithScale(), !original);
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
};

QTEST_GUILESS_MAIN(tst_Settings)
#include "tst_settings.moc"
