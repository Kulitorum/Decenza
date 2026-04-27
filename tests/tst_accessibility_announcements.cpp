#include <QtTest>
#include <QSignalSpy>
#include <QSettings>

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

// AccessibilityManager uses QSettings("Decenza", "DE1") — the two-string ctor
// hardcodes NativeFormat per Qt docs, so setDefaultFormat()/setPath() can't
// redirect it (NSUserDefaults / Windows registry / Linux native conf are
// unaffected). Instead we follow the tst_settings pattern: snapshot the real
// store's accessibility keys in init() and restore them in cleanup() so the
// developer's settings round-trip even when assertions fail mid-test.

class tst_AccessibilityAnnouncements : public QObject {
    Q_OBJECT

private:
    QSettings m_realSettings{QStringLiteral("Decenza"), QStringLiteral("DE1")};
    QVariant m_origEnabled;
    QVariant m_origTtsEnabled;
    QVariant m_origTickEnabled;
    QVariant m_origTickSoundIndex;
    QVariant m_origTickVolume;
    QVariant m_origExtractionEnabled;
    QVariant m_origExtractionInterval;
    QVariant m_origExtractionMode;

private slots:
    void init() {
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
};

QTEST_GUILESS_MAIN(tst_AccessibilityAnnouncements)
#include "tst_accessibility_announcements.moc"
