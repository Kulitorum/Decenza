#include <QtTest>
#include <QSignalSpy>

#include "core/settings.h"

// Test Settings property round-trip and signal emission.
// Settings uses QSettings("DecentEspresso", "DE1Qt") which reads/writes to
// the system settings store. Tests verify set→get round-trip and signals.
// Note: defaults are NOT tested here because the test machine may have real
// settings persisted. Default values are documented in the code comments.

class tst_Settings : public QObject {
    Q_OBJECT

private:
    Settings m_settings;

private slots:

    // ==========================================
    // Property round-trip (set → get)
    // ==========================================

    void targetWeightRoundTrip() {
        double original = m_settings.targetWeight();
        m_settings.setTargetWeight(42.5);
        QCOMPARE(m_settings.targetWeight(), 42.5);
        m_settings.setTargetWeight(original);  // Restore
    }

    void steamTemperatureRoundTrip() {
        double original = m_settings.steamTemperature();
        m_settings.setSteamTemperature(155.0);
        QCOMPARE(m_settings.steamTemperature(), 155.0);
        m_settings.setSteamTemperature(original);
    }

    void scaleAddressRoundTrip() {
        QString original = m_settings.scaleAddress();
        m_settings.setScaleAddress("AA:BB:CC:DD:EE:FF");
        QCOMPARE(m_settings.scaleAddress(), QString("AA:BB:CC:DD:EE:FF"));
        m_settings.setScaleAddress(original);
    }

    void themeModeRoundTrip() {
        QString original = m_settings.themeMode();
        m_settings.setThemeMode("light");
        QCOMPARE(m_settings.themeMode(), QString("light"));
        m_settings.setThemeMode(original);
    }

    void defaultShotRatingRoundTrip() {
        int original = m_settings.defaultShotRating();
        m_settings.setDefaultShotRating(50);
        QCOMPARE(m_settings.defaultShotRating(), 50);
        m_settings.setDefaultShotRating(original);
    }

    void ignoreVolumeWithScaleRoundTrip() {
        bool original = m_settings.ignoreVolumeWithScale();
        m_settings.setIgnoreVolumeWithScale(!original);
        QCOMPARE(m_settings.ignoreVolumeWithScale(), !original);
        m_settings.setIgnoreVolumeWithScale(original);
    }

    // ==========================================
    // DYE fields (structured grinder data)
    // ==========================================

    void dyeFieldsRoundTrip() {
        QString original = m_settings.dyeBeanBrand();
        m_settings.setDyeBeanBrand("Square Mile");
        QCOMPARE(m_settings.dyeBeanBrand(), QString("Square Mile"));
        m_settings.setDyeBeanBrand(original);
    }

    // ==========================================
    // Signal emission
    // ==========================================

    void targetWeightSignalEmitted() {
        double original = m_settings.targetWeight();
        QSignalSpy spy(&m_settings, &Settings::targetWeightChanged);
        m_settings.setTargetWeight(original + 1.0);
        QVERIFY(spy.count() >= 1);
        m_settings.setTargetWeight(original);
    }

    void themeModeSignalEmitted() {
        QString original = m_settings.themeMode();
        QString newMode = (original == "dark") ? "light" : "dark";
        QSignalSpy spy(&m_settings, &Settings::themeModeChanged);
        m_settings.setThemeMode(newMode);
        QVERIFY(spy.count() >= 1);
        m_settings.setThemeMode(original);
    }

    // ==========================================
    // Edge cases
    // ==========================================

    void targetWeightZeroIsValid() {
        // 0 means disabled (no SAW)
        double original = m_settings.targetWeight();
        m_settings.setTargetWeight(0.0);
        QCOMPARE(m_settings.targetWeight(), 0.0);
        m_settings.setTargetWeight(original);
    }

    void emptyScaleAddressIsValid() {
        QString original = m_settings.scaleAddress();
        m_settings.setScaleAddress("");
        QCOMPARE(m_settings.scaleAddress(), QString(""));
        m_settings.setScaleAddress(original);
    }
};

QTEST_GUILESS_MAIN(tst_Settings)
#include "tst_settings.moc"
