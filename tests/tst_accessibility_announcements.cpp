#include <QtTest>
#include <QSignalSpy>

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

class tst_AccessibilityAnnouncements : public QObject {
    Q_OBJECT

private slots:
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
        // setup() with enabled=false leaves m_enabled at its loaded default (false);
        // setEnabled(false) short-circuits if already-false, so no setup announcement
        // fires. Don't bother calling resetCalls() to verify.
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

        mgr.setEnabled(true);

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Accessibility enabled"));
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    // toggleEnabled() previously called m_tts->stop()+say() directly; now it
    // routes with interrupt=true, which maps to assertive on the platform path.
    void toggleEnabledRoutesThroughPlatformWhenScreenReaderActive() {
        FakeAccessibilityManager mgr;
        mgr.fakeScreenReaderActive = true;
        mgr.setEnabled(false);  // start from a known state
        mgr.resetCalls();

        mgr.toggleEnabled();

        // setEnabled(true) inside toggleEnabled fires its own polite
        // announcement, then toggleEnabled() fires an assertive one. Both go
        // through the platform path; neither hits TTS.
        QCOMPARE(mgr.ttsCalls.size(), 0);
        QVERIFY(mgr.platformCalls.size() >= 1);
        QCOMPARE(mgr.platformCalls.last().text, QStringLiteral("Accessibility enabled"));
        QCOMPARE(mgr.platformCalls.last().assertive, true);
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
