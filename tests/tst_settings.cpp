#include <QtTest>
#include <QSignalSpy>

#include "core/settings.h"

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
        m_origTargetWeight = m_settings.targetWeight();
        m_origSteamTemp = m_settings.steamTemperature();
        m_origScaleAddress = m_settings.scaleAddress();
        m_origThemeMode = m_settings.themeMode();
        m_origShotRating = m_settings.defaultShotRating();
        m_origIgnoreVolume = m_settings.ignoreVolumeWithScale();
        m_origDyeBeanBrand = m_settings.dyeBeanBrand();
    }

    void cleanup() {
        // Restore all originals after each test (runs even on assertion failure)
        m_settings.setTargetWeight(m_origTargetWeight);
        m_settings.setSteamTemperature(m_origSteamTemp);
        m_settings.setScaleAddress(m_origScaleAddress);
        m_settings.setThemeMode(m_origThemeMode);
        m_settings.setDefaultShotRating(m_origShotRating);
        m_settings.setIgnoreVolumeWithScale(m_origIgnoreVolume);
        m_settings.setDyeBeanBrand(m_origDyeBeanBrand);
    }

    // ==========================================
    // Property round-trip (set -> get)
    // ==========================================

    void targetWeightRoundTrip() {
        m_settings.setTargetWeight(42.5);
        QCOMPARE(m_settings.targetWeight(), 42.5);
    }

    void steamTemperatureRoundTrip() {
        m_settings.setSteamTemperature(155.0);
        QCOMPARE(m_settings.steamTemperature(), 155.0);
    }

    void scaleAddressRoundTrip() {
        m_settings.setScaleAddress("AA:BB:CC:DD:EE:FF");
        QCOMPARE(m_settings.scaleAddress(), QString("AA:BB:CC:DD:EE:FF"));
    }

    void themeModeRoundTrip() {
        m_settings.setThemeMode("light");
        QCOMPARE(m_settings.themeMode(), QString("light"));
    }

    void defaultShotRatingRoundTrip() {
        m_settings.setDefaultShotRating(50);
        QCOMPARE(m_settings.defaultShotRating(), 50);
    }

    void ignoreVolumeWithScaleRoundTrip() {
        bool original = m_settings.ignoreVolumeWithScale();
        m_settings.setIgnoreVolumeWithScale(!original);
        QCOMPARE(m_settings.ignoreVolumeWithScale(), !original);
    }

    // ==========================================
    // DYE fields (structured grinder data)
    // ==========================================

    void dyeFieldsRoundTrip() {
        m_settings.setDyeBeanBrand("Square Mile");
        QCOMPARE(m_settings.dyeBeanBrand(), QString("Square Mile"));
    }

    // ==========================================
    // Signal emission
    // ==========================================

    void targetWeightSignalEmitted() {
        QSignalSpy spy(&m_settings, &Settings::targetWeightChanged);
        m_settings.setTargetWeight(m_origTargetWeight + 1.0);
        QVERIFY(spy.count() >= 1);
    }

    void themeModeSignalEmitted() {
        QString newMode = (m_origThemeMode == "dark") ? "light" : "dark";
        QSignalSpy spy(&m_settings, &Settings::themeModeChanged);
        m_settings.setThemeMode(newMode);
        QVERIFY(spy.count() >= 1);
    }

    // ==========================================
    // Edge cases
    // ==========================================

    void targetWeightZeroIsValid() {
        // 0 means disabled (no SAW)
        m_settings.setTargetWeight(0.0);
        QCOMPARE(m_settings.targetWeight(), 0.0);
    }

    void emptyScaleAddressIsValid() {
        m_settings.setScaleAddress("");
        QCOMPARE(m_settings.scaleAddress(), QString(""));
    }
};

QTEST_GUILESS_MAIN(tst_Settings)
#include "tst_settings.moc"
