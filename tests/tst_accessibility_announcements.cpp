#include <QtTest>
#include <QSignalSpy>

#include "core/accessibilitymanager.h"

// Verifies the routing rule in AccessibilityManager::announce():
//   isScreenReaderActive() == true  -> only platform dispatch
//   isScreenReaderActive() == false, ttsEnabled == true  -> only TTS dispatch
//   isScreenReaderActive() == false, ttsEnabled == false -> neither
//
// Subclasses AccessibilityManager and overrides the protected virtuals so
// nothing touches real Qt accessibility / TTS state during the test.

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
    using AccessibilityManager::AccessibilityManager;

    bool fakeScreenReaderActive = false;
    QVector<PlatformCall> platformCalls;
    QVector<TtsCall> ttsCalls;

protected:
    bool isScreenReaderActive() const override { return fakeScreenReaderActive; }

    void dispatchPlatformAnnouncement(const QString& text, bool assertive) override {
        platformCalls.push_back({text, assertive});
    }

    void dispatchTtsAnnouncement(const QString& text, bool interrupt) override {
        ttsCalls.push_back({text, interrupt});
    }
};

}

class tst_AccessibilityAnnouncements : public QObject {
    Q_OBJECT

private slots:
    void platformPathTakenWhenScreenReaderActive() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.setTtsEnabled(true);
        mgr.fakeScreenReaderActive = true;

        mgr.announce("Shot complete");

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].text, QStringLiteral("Shot complete"));
        QCOMPARE(mgr.platformCalls[0].assertive, false);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void platformPathSuppressesTtsEvenIfTtsEnabled() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.setTtsEnabled(true);  // user has TTS on
        mgr.fakeScreenReaderActive = true;  // but screen reader wins

        mgr.announce("Hello");

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void interruptMapsToAssertiveOnPlatform() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.fakeScreenReaderActive = true;

        mgr.announce("Error", /*interrupt=*/true);

        QCOMPARE(mgr.platformCalls.size(), 1);
        QCOMPARE(mgr.platformCalls[0].assertive, true);
    }

    void ttsFallbackWhenNoScreenReaderAndTtsEnabled() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.setTtsEnabled(true);
        mgr.fakeScreenReaderActive = false;

        mgr.announce("Pouring");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls[0].text, QStringLiteral("Pouring"));
        QCOMPARE(mgr.ttsCalls[0].interrupt, false);
    }

    void interruptMapsToTtsInterruptArg() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.setTtsEnabled(true);
        mgr.fakeScreenReaderActive = false;

        mgr.announce("Stop", /*interrupt=*/true);

        QCOMPARE(mgr.ttsCalls.size(), 1);
        QCOMPARE(mgr.ttsCalls[0].interrupt, true);
    }

    void silentWhenNoScreenReaderAndTtsDisabled() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.setTtsEnabled(false);
        mgr.fakeScreenReaderActive = false;

        mgr.announce("Should not speak");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void disabledManagerDispatchesNothing() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(false);
        mgr.setTtsEnabled(true);
        mgr.fakeScreenReaderActive = true;

        mgr.announce("Hidden");

        QCOMPARE(mgr.platformCalls.size(), 0);
        QCOMPARE(mgr.ttsCalls.size(), 0);
    }

    void politeAndAssertiveHelpersRouteCorrectly() {
        FakeAccessibilityManager mgr;
        mgr.setEnabled(true);
        mgr.fakeScreenReaderActive = true;

        mgr.announcePolite("Polite text");
        mgr.announceAssertive("Assertive text");

        QCOMPARE(mgr.platformCalls.size(), 2);
        QCOMPARE(mgr.platformCalls[0].assertive, false);
        QCOMPARE(mgr.platformCalls[1].assertive, true);
    }
};

QTEST_GUILESS_MAIN(tst_AccessibilityAnnouncements)
#include "tst_accessibility_announcements.moc"
