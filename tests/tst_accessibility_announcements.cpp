#include <QtTest>
#include "core/settings.h"
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>

#include "core/accessibilitymanager.h"

// Verifies the routing rule used by AccessibilityManager:
//   isScreenReaderActive() == true  -> only platform dispatch
//   isScreenReaderActive() == false, ttsEnabled == true  -> only TTS dispatch
//   isScreenReaderActive() == false, ttsEnabled == false -> neither
//
// The fake subclass uses the TestSkipAudioInit ctor so no real QTextToSpeech
// is constructed and overrides the dispatch / isActive virtuals so nothing
// touches real Qt accessibility state. setEnabled(true) and toggleEnabled()
// route through the same helper as announce(), so each test resets the
// recorded calls AFTER setup so we only check what the test itself triggers.

namespace {

struct PlatformCall {
    QString text;
    bool assertive = false;
};

struct TtsCall {
    QString text;
    bool interrupt = false;
};

class FakeAccessibilityManager : public AccessibilityManager {
public:
    explicit FakeAccessibilityManager(QObject* parent = nullptr)
        : AccessibilityManager(TestSkipAudioInit::SkipAudio, parent) {}

    bool fakeScreenReaderActive = false;
    QVector<PlatformCall> platformCalls;
    QVector<TtsCall> ttsCalls;

    void resetCalls() {
        platformCalls.clear();
        ttsCalls.clear();
    }

protected:
    bool isScreenReaderActive() const override { return fakeScreenReaderActive; }

    void dispatchPlatformAnnouncement(const QString& text, bool assertive) override {
        platformCalls.push_back({text, assertive});
    }

    void dispatchTtsAnnouncement(const QString& text, bool interrupt) override {
        ttsCalls.push_back({text, interrupt});
    }
};

// Configures the manager and clears any calls produced by setEnabled/setTtsEnabled
// during setup, so each test asserts only against what it itself triggered.
void setup(FakeAccessibilityManager& mgr, bool enabled, bool ttsEnabled, bool screenReader) {
    mgr.fakeScreenReaderActive = screenReader;
    mgr.setEnabled(enabled);
    mgr.setTtsEnabled(ttsEnabled);
    mgr.resetCalls();
}

}

// AccessibilityManager now uses the PRIMARY store
// QSettings("DecentEspresso", "DE1Qt") like every other settings domain
// (it was an isolated QSettings("Decenza","DE1") — see
// accessibilitymanager.cpp). The two-string ctor hardcodes NativeFormat
// per Qt docs, so setDefaultFormat()/setPath() can't redirect it.
// Instead we follow the tst_settings pattern: snapshot the real store's
// accessibility keys in init() and restore them in cleanup() so the
// developer's settings round-trip even when assertions fail mid-test.

class tst_AccessibilityAnnouncements : public QObject {
    Q_OBJECT

private:
    QSettings m_realSettings{Settings::testQSettingsPath(), QSettings::IniFormat};
    QVariant m_origEnabled;
    QVariant m_origTtsEnabled;
    QVariant m_origTickEnabled;
    QVariant m_origTickSoundIndex;
    QVariant m_origTickVolume;
    QVariant m_origExtractionEnabled;
    QVariant m_origExtractionInterval;
    QVariant m_origExtractionMode;

private slots:
    void init() { QTest::failOnWarning();
        // Snapshot every key AccessibilityManager::saveSettings() touches so
        // setEnabled / setTtsEnabled writes during a test don't permanently
        // mutate the developer's real preferences.
        m_origEnabled            = m_realSettings.value("accessibility/enabled");
        m_origTtsEnabled         = m_realSettings.value("accessibility/ttsEnabled");
        m_origTickEnabled        = m_realSettings.value("accessibility/tickEnabled");
        m_origTickSoundIndex     = m_realSettings.value("accessibility/tickSoundIndex");
        m_origTickVolume         = m_realSettings.value("accessibility/tickVolume");
        m_origExtractionEnabled  = m_realSettings.value("accessibility/extractionAnnouncementsEnabled");
        m_origExtractionInterval = m_realSettings.value("accessibility/extractionAnnouncementInterval");
        m_origExtractionMode     = m_realSettings.value("accessibility/extractionAnnouncementMode");
    }

    void cleanup() {
        auto restore = [&](const char* key, const QVariant& original) {
            if (original.isValid()) {
                m_realSettings.setValue(key, original);
            } else {
                m_realSettings.remove(key);
            }
        };
        restore("accessibility/enabled",                          m_origEnabled);
        restore("accessibility/ttsEnabled",                       m_origTtsEnabled);
        restore("accessibility/tickEnabled",                      m_origTickEnabled);
        restore("accessibility/tickSoundIndex",                   m_origTickSoundIndex);
        restore("accessibility/tickVolume",                       m_origTickVolume);
        restore("accessibility/extractionAnnouncementsEnabled",   m_origExtractionEnabled);
        restore("accessibility/extractionAnnouncementInterval",   m_origExtractionInterval);
        restore("accessibility/extractionAnnouncementMode",       m_origExtractionMode);
        m_realSettings.sync();
    }

    void platformPathTakenWhenScreenReaderActive() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/true);

        mgr.announce("Shot complete");

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Shot complete"));
        QCOMPARE(mgr.platformCalls[0].assertive, false);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void platformPathSuppressesTtsEvenIfTtsEnabled() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/true);

        mgr.announce("Hello");

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void interruptMapsToAssertiveOnPlatform() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/true);

        mgr.announce("Error", /*interrupt=*/true);

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].assertive, true);
    }

    void ttsFallbackWhenNoScreenReaderAndTtsEnabled() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/false);

        mgr.announce("Pouring");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls[0].text, QStringLiteral("Pouring"));
        QCOMPARE(mgr.ttsCalls[0].interrupt, false);
    }

    void interruptMapsToTtsInterruptArg() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/false);

        mgr.announce("Stop", /*interrupt=*/true);

        QCOMPARE(mgr.ttsCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls[0].interrupt, true);
    }

    void silentWhenNoScreenReaderAndTtsDisabled() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/false, /*sr=*/false);

        mgr.announce("Should not speak");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void disabledManagerDispatchesNothing() {
        FakeAccessibilityManager mgr;
        // m_enabled defaults to false (TestSkipAudioInit skips loadSettings),
        // so we don't call setEnabled here. setTtsEnabled doesn't announce.
        // resetCalls() is called for consistency with the rest of the suite.
        mgr.fakeScreenReaderActive = true;
        mgr.setTtsEnabled(true);
        mgr.resetCalls();

        mgr.announce("Hidden");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    // Steam-coach voice must bypass BOTH accessibility voice gates — the
    // master switch (m_enabled) AND the Voice Announcements toggle
    // (m_ttsEnabled): its own opt-in (Settings.app.steamCoachAudioEnabled,
    // gated at the emitter) is the only gate. Everything that silences
    // announce() is engaged here — announceCoaching() must still speak. This
    // pins the exact regression the coaching-voice fix addressed: spoken cues
    // silently dead behind accessibility prefs the user never connected to
    // steam coaching (and can't even reach — the Voice Announcements switch
    // is greyed out while the master switch is off).
    void coachingAnnouncementBypassesAccessibilityGates() {
        FakeAccessibilityManager mgr;
        // m_enabled stays at its default false; ttsEnabled off too — the
        // deepest silent-for-announce() configuration.
        mgr.fakeScreenReaderActive = false;
        mgr.setTtsEnabled(false);
        mgr.resetCalls();

        mgr.announceCoaching("Steam done", /*interrupt=*/true);

        QCOMPARE(mgr.ttsCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls[0].text, QStringLiteral("Steam done"));
        QCOMPARE(mgr.ttsCalls[0].interrupt, true);
        QCOMPARE(mgr.platformCalls.size(), 0);  // no screen reader -> TTS route
    }

    // With a screen reader active, coaching follows the same no-TTS-overlap
    // rule as routeAnnouncement: platform dispatch only.
    void coachingAnnouncementPrefersScreenReader() {
        FakeAccessibilityManager mgr;
        mgr.fakeScreenReaderActive = true;
        mgr.setTtsEnabled(true);
        mgr.resetCalls();

        mgr.announceCoaching("Steam done", /*interrupt=*/false);

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Steam done"));
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void politeAndAssertiveHelpersRouteCorrectly() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/true);

        mgr.announcePolite("Polite text");
        mgr.announceAssertive("Assertive text");

        QCOMPARE(mgr.platformCalls.size(), 2);
        QCOMPARE(mgr.platformCalls[0].assertive, false);
        QCOMPARE(mgr.platformCalls[1].assertive, true);
    }

    // setEnabled used to call m_tts->say() directly, bypassing the routing —
    // that meant the "Accessibility enabled" confirmation double-spoke when a
    // screen reader was on. Now it routes through routeAnnouncement(), so the
    // platform dispatch fires when isScreenReaderActive() is true.
    void setEnabledRoutesThroughPlatformWhenScreenReaderActive() {
        FakeAccessibilityManager mgr;
        mgr.fakeScreenReaderActive = true;
        // Defensive: don't depend on the TestSkipAudioInit ctor leaving
        // m_enabled at false. setEnabled() short-circuits if already-set, so
        // start from a known disabled state and clear any setup-side calls.
        mgr.setEnabled(false);
        mgr.resetCalls();

        mgr.setEnabled(true);

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Accessibility enabled"));
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    // toggleEnabled() must produce exactly one announcement on the platform
    // path. Earlier the routing produced two (Polite from setEnabled() + Assertive
    // from toggleEnabled()); on the platform path there's no cancellation
    // mechanism between QAccessibleAnnouncementEvents, so TalkBack/VoiceOver
    // would speak the message twice. The fix passes announce=false to the
    // internal setEnabled call so toggleEnabled() emits a single Assertive event.
    void toggleEnabledRoutesThroughPlatformWhenScreenReaderActive() {
        FakeAccessibilityManager mgr;
        mgr.fakeScreenReaderActive = true;
        mgr.setEnabled(false);  // start from a known state
        mgr.resetCalls();

        mgr.toggleEnabled();

        // Exactly one assertive platform announcement; no TTS dispatch.
        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Accessibility enabled"));
        QCOMPARE(mgr.platformCalls[0].assertive, true);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    // announceLabel() used to call m_tts->say() directly with a pitch/rate
    // adjustment, bypassing the screen-reader gate entirely. Now it dispatches
    // a polite platform announcement when a screen reader is active and skips
    // the TTS path.
    void announceLabelRoutesThroughPlatformWhenScreenReaderActive() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/true);

        mgr.announceLabel("Temperature 93 degrees");

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Temperature 93 degrees"));
        QCOMPARE(mgr.platformCalls[0].assertive, false);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    // announceLabel still goes via TTS for the sighted-user-with-no-screen-reader
    // case so we don't regress the spoken-progress feature.
    void announceLabelUsesTtsWhenNoScreenReaderAndTtsEnabled() {
        FakeAccessibilityManager mgr;
        setup(mgr, /*enabled=*/true, /*tts=*/true, /*sr=*/false);

        mgr.announceLabel("Battery 85 percent");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls[0].text, QStringLiteral("Battery 85 percent"));
    }

    // ---- migrateAccessibilityLegacyStore (one-time legacy carry-over) ----
    // Store-injected pure static → tested with temp INI QSettings, zero
    // real-store pollution. Guards an irreversible one-time migration.

    void legacyMigration_copiesAbsentKeysAndStampsGuard() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings legacy(dir.filePath("legacy.ini"), QSettings::IniFormat);
        QSettings primary(dir.filePath("primary.ini"), QSettings::IniFormat);
        legacy.setValue("accessibility/enabled", true);
        legacy.setValue("accessibility/tickVolume", 42);
        legacy.setValue("accessibility/extractionAnnouncementMode", "milestones_only");
        legacy.sync();

        auto r = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacy);

        QVERIFY(!r.alreadyDone);
        QVERIFY(!r.deferredOnError);
        QVERIFY(r.guardStamped);
        QCOMPARE(r.copied, 3);
        QCOMPARE(r.legacyKeyCount, 3);
        QCOMPARE(primary.value("accessibility/enabled").toBool(), true);
        QCOMPARE(primary.value("accessibility/tickVolume").toInt(), 42);
        QCOMPARE(primary.value("accessibility/extractionAnnouncementMode").toString(),
                 QStringLiteral("milestones_only"));
        QVERIFY(primary.value("accessibility/_migratedFromLegacyV1").toBool());
    }

    void legacyMigration_idempotentWhenGuardSet() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings legacy(dir.filePath("legacy.ini"), QSettings::IniFormat);
        QSettings primary(dir.filePath("primary.ini"), QSettings::IniFormat);
        legacy.setValue("accessibility/enabled", true);
        legacy.sync();
        primary.setValue("accessibility/_migratedFromLegacyV1", true);
        primary.sync();

        auto r = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacy);

        QVERIFY(r.alreadyDone);
        QCOMPARE(r.copied, 0);
        // legacy value must NOT have been pulled in (guard already set)
        QVERIFY(!primary.contains("accessibility/enabled"));
    }

    void legacyMigration_copyIfAbsentNeverClobbersPrimary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings legacy(dir.filePath("legacy.ini"), QSettings::IniFormat);
        QSettings primary(dir.filePath("primary.ini"), QSettings::IniFormat);
        legacy.setValue("accessibility/enabled", false);   // stale legacy
        legacy.setValue("accessibility/tickVolume", 10);
        legacy.sync();
        primary.setValue("accessibility/enabled", true);   // newer primary
        primary.sync();

        auto r = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacy);

        QVERIFY(r.guardStamped);
        QCOMPARE(r.copied, 1);                              // only tickVolume
        QCOMPARE(primary.value("accessibility/enabled").toBool(), true);  // preserved
        QCOMPARE(primary.value("accessibility/tickVolume").toInt(), 10);
    }

    void legacyMigration_emptyLegacyStillStampsGuard() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings legacy(dir.filePath("legacy.ini"), QSettings::IniFormat);
        QSettings primary(dir.filePath("primary.ini"), QSettings::IniFormat);

        auto r = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacy);

        QVERIFY(r.guardStamped);          // one-time: don't rescan forever
        QCOMPARE(r.copied, 0);
        QCOMPARE(r.legacyKeyCount, 0);    // distinguishes "nothing" from "all present"
    }

    void legacyMigration_defersWithoutStampingGuardOnUnreadableLegacy() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacyPath = dir.filePath("legacy.ini");
        // Corrupt INI → QSettings(IniFormat).status() == FormatError.
        {
            QFile f(legacyPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[General\nthis is : not = valid ][[[ ini \x00\x01\x02");
            f.close();
        }
        QSettings legacy(legacyPath, QSettings::IniFormat);
        legacy.sync();
        // Fixture sanity: if a future Qt tolerates this input, fail
        // loudly ("fixture no longer corrupt") rather than silently
        // exercising the wrong path.
        QVERIFY2(legacy.status() != QSettings::NoError,
                 "corrupt-INI fixture must actually trip QSettings status");
        QSettings primary(dir.filePath("primary.ini"), QSettings::IniFormat);

        auto r = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacy);

        QVERIFY2(r.deferredOnError, "corrupt legacy must defer");
        QVERIFY2(!r.guardStamped, "must NOT burn the one-shot guard on a failed read");
        QVERIFY(!primary.value("accessibility/_migratedFromLegacyV1").toBool());

        // A subsequent launch with a now-readable legacy completes.
        QSettings legacyOk(dir.filePath("legacy_ok.ini"), QSettings::IniFormat);
        legacyOk.setValue("accessibility/ttsEnabled", false);
        legacyOk.sync();
        auto r2 = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacyOk);
        QVERIFY(r2.guardStamped);
        QCOMPARE(r2.copied, 1);
        QCOMPARE(primary.value("accessibility/ttsEnabled").toBool(), false);
    }

    // NOTE on the copy-before-status ordering invariant (production
    // copies keys, THEN checks status, so a partially-parseable legacy
    // hands forward whatever parsed while still deferring the guard):
    // this cannot be black-box forced here. Qt 6.11's IniFormat parser
    // is all-or-nothing for our fixtures — it either parses fully
    // (status NoError) or fails wholesale exposing zero keys (see
    // _defersWithoutStampingGuardOnUnreadableLegacy, where copied==0 on
    // FormatError). There is no reliable cross-version way to produce
    // "FormatError + some keys parsed", so a fixture-based test for the
    // ordering would be flaky. The invariant is preserved structurally
    // (copy loop precedes the status gate in migrateAccessibilityLegacyStore)
    // and its observable contract — deferred read does NOT stamp the
    // guard, and a later readable run completes — IS deterministically
    // covered by _defersWithoutStampingGuardOnUnreadableLegacy.

    // The restore handler stamps accessibility/_migratedFromLegacyV1 on
    // ANY successful restore (even a backup with no accessibility block)
    // so a restored profile is authoritative. Pin that this actually
    // suppresses a later legacy merge — the data-loss scenario the
    // unconditional stamp exists to prevent.
    void legacyMigration_restoreStampedGuardSuppressesLaterLegacyMerge() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings primary(dir.filePath("primary.ini"), QSettings::IniFormat);
        // Simulate what handleBackupRestore does on any successful
        // restore (including a no-accessibility-block backup): only the
        // guard is stamped, no accessibility values present.
        primary.setValue("accessibility/_migratedFromLegacyV1", true);
        primary.sync();

        // This device still has a populated legacy store.
        QSettings legacy(dir.filePath("legacy.ini"), QSettings::IniFormat);
        legacy.setValue("accessibility/enabled", true);
        legacy.setValue("accessibility/tickVolume", 99);
        legacy.sync();

        auto r = AccessibilityManager::migrateAccessibilityLegacyStore(primary, legacy);

        QVERIFY2(r.alreadyDone, "restored profile is authoritative — migration is a no-op");
        QCOMPARE(r.copied, 0);
        QVERIFY2(!primary.contains("accessibility/enabled"),
                 "stale legacy must NOT bleed over a restored profile");
        QVERIFY2(!primary.contains("accessibility/tickVolume"),
                 "stale legacy must NOT bleed over a restored profile");
    }
};

QTEST_GUILESS_MAIN(tst_AccessibilityAnnouncements)
#include "tst_accessibility_announcements.moc"
