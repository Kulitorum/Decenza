#include <QtTest>
#include <QSignalSpy>
#include <QSettings>

#include "ai/livesteamcoach.h"
#include "machine/machinestate.h"
#include "ble/de1device.h"
#include "core/settings.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"

// LiveSteamCoach milestone/gating tests. The coach is driven entirely through
// MachineState's phase / shotTime / steamFlowStopped signals (friend access to
// set the private state, per the DECENZA_TESTING pattern) and the two
// Settings.app steam-coach toggles. Assertions run against the public cue
// surface (cueChanged + cueText/cueSeverity/cueActive) and the speakRequested
// signal — the speak contract needs no AccessibilityManager.
//
// Expected cue texts are the English source strings (no TranslationManager is
// injected, so tr_() falls back to them).

class tst_LiveSteamCoach : public QObject {
    Q_OBJECT

private:
    // SettingsApp/SettingsBrew write to the PRIMARY store
    // QSettings("DecentEspresso", "DE1Qt") — the developer's real preferences,
    // NOT a test-scoped store. Snapshot every key the fixtures write in init()
    // and restore in cleanup() (same pattern as tst_accessibility_announcements
    // / tst_settings), otherwise a test run permanently flips the developer's
    // steam-coaching toggles (they must stay off-by-default for new users) and
    // steam duration.
    QSettings m_realSettings{"DecentEspresso", "DE1Qt"};
    QVariant m_origCoachVisual;
    QVariant m_origCoachAudio;
    QVariant m_origSteamTimeout;

    static constexpr const char* STRETCH = "Steaming — keep the tip near the surface to stretch";
    static constexpr const char* ROLL    = "Submerge the tip — roll and texture";
    static constexpr const char* ALMOST  = "Almost there — get ready";
    static constexpr const char* DONE    = "Steam done";

    struct Fixture {
        DE1Device device;
        Settings settings;
        MachineState state{&device};
        LiveSteamCoach coach{&state, &settings};
        QSignalSpy cueSpy{&coach, &LiveSteamCoach::cueChanged};
        QSignalSpy speakSpy{&coach, &LiveSteamCoach::speakRequested};

        // QSettings persists across runs — always set the toggles and timeout
        // explicitly so every test starts from a known state.
        Fixture(bool visual, bool audio, int timeoutS) {
            state.setSettings(&settings);
            settings.app()->setSteamCoachVisualEnabled(visual);
            settings.app()->setSteamCoachAudioEnabled(audio);
            settings.brew()->setSteamTimeout(timeoutS);
        }

        // Enter the Steaming phase at elapsed 0 (fires the stretch cue when the
        // coach is enabled).
        void startSteam() {
            state.m_shotTime = 0.0;
            state.m_phase = MachineState::Phase::Steaming;
            emit state.phaseChanged();
        }

        // Advance the elapsed steam time (what the 100ms shot-timer tick does).
        void tick(double elapsedSec) {
            state.m_shotTime = elapsedSec;
            emit state.shotTimeChanged();
        }

        // The machine event that ends steam flow (auto-stop or manual stop).
        // The shot timer freezes at the current elapsed value — mirror that by
        // NOT advancing m_shotTime here.
        void flowStopped() {
            emit state.steamFlowStopped();
        }

        // Leave the Steaming phase entirely (resets the coach).
        void endSteam() {
            state.m_phase = MachineState::Phase::Ready;
            emit state.phaseChanged();
        }
    };

private slots:

    void init() {
        m_origCoachVisual  = m_realSettings.value("steam/steamCoachVisualEnabled");
        m_origCoachAudio   = m_realSettings.value("steam/steamCoachAudioEnabled");
        m_origSteamTimeout = m_realSettings.value("steam/timeout");
    }

    void cleanup() {
        auto restore = [&](const char* key, const QVariant& original) {
            if (original.isValid())
                m_realSettings.setValue(key, original);
            else
                m_realSettings.remove(key);
        };
        restore("steam/steamCoachVisualEnabled", m_origCoachVisual);
        restore("steam/steamCoachAudioEnabled", m_origCoachAudio);
        restore("steam/timeout", m_origSteamTimeout);
        m_realSettings.sync();
    }

    // ==========================================
    // Gating: two independent, explicit opt-ins
    // ==========================================

    void bothOff_noCuesNoSpeech() {
        Fixture fx(false, false, 20);
        fx.startSteam();
        for (double t = 1; t <= 19; t += 1)
            fx.tick(t);
        fx.flowStopped();
        QCOMPARE(fx.cueSpy.count(), 0);
        QCOMPARE(fx.speakSpy.count(), 0);
        QVERIFY(!fx.coach.cueActive());
    }

    void visualOnly_cueSurfaceUpdatesButNothingSpoken() {
        Fixture fx(true, false, 20);
        fx.startSteam();  // stretch
        QCOMPARE(fx.cueSpy.count(), 1);
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(STRETCH));
        fx.tick(7.0);     // roll (0.35 * 20)
        fx.tick(16.0);    // almost (0.80 * 20)
        fx.tick(19.5);
        fx.flowStopped(); // done (within the completion window)
        QCOMPARE(fx.cueSpy.count(), 4);
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(DONE));
        QCOMPARE(fx.speakSpy.count(), 0);
    }

    void audioOnly_speaksAndSurfaceStillUpdates() {
        Fixture fx(false, true, 20);
        fx.startSteam();
        fx.tick(7.0);
        fx.tick(16.0);
        fx.tick(19.5);
        fx.flowStopped();
        // All four milestones spoken...
        QCOMPARE(fx.speakSpy.count(), 4);
        QCOMPARE(fx.speakSpy.at(0).at(0).toString(), QString::fromUtf8(STRETCH));
        QCOMPARE(fx.speakSpy.at(3).at(0).toString(), QString::fromUtf8(DONE));
        // ...and the cue surface still updates (the banner just isn't shown —
        // that gate lives in QML on the visual setting).
        QCOMPARE(fx.cueSpy.count(), 4);
        QVERIFY(fx.coach.cueActive());
    }

    void speakInterrupt_onlyForCompletion() {
        Fixture fx(true, true, 20);
        fx.startSteam();
        fx.tick(7.0);
        fx.tick(16.0);
        fx.tick(19.5);
        fx.flowStopped();
        QCOMPARE(fx.speakSpy.count(), 4);
        QCOMPARE(fx.speakSpy.at(0).at(1).toBool(), false);  // stretch: polite
        QCOMPARE(fx.speakSpy.at(1).at(1).toBool(), false);  // roll: polite
        QCOMPARE(fx.speakSpy.at(2).at(1).toBool(), false);  // almost: polite
        QCOMPARE(fx.speakSpy.at(3).at(1).toBool(), true);   // done: assertive
    }

    void toggleBetweenOperations_takesEffectOnNextSteam() {
        Fixture fx(false, false, 20);
        fx.startSteam();
        fx.tick(7.0);
        fx.endSteam();
        QCOMPARE(fx.cueSpy.count(), 0);

        // User enables the banner between steams (settings UI is unreachable
        // mid-steam — leaving the steam page stops steam).
        fx.settings.app()->setSteamCoachVisualEnabled(true);
        fx.startSteam();
        QCOMPARE(fx.cueSpy.count(), 1);
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(STRETCH));
    }

    // ==========================================
    // Pacing / milestone rules
    // ==========================================

    void untimedSteam_stretchOnly_noCompletion() {
        Fixture fx(true, true, 0);
        fx.startSteam();  // stretch (not gated on a target duration)
        for (double t = 1; t <= 60; t += 1)
            fx.tick(t);
        fx.flowStopped(); // untimed: end is user-decided — no done cue
        QCOMPARE(fx.cueSpy.count(), 1);
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(STRETCH));
        QCOMPARE(fx.speakSpy.count(), 1);
    }

    void shortSteam_skipsRoll_keepsAlmostAndDone() {
        Fixture fx(true, true, 6);  // < MIN_DURATION_FOR_ROLL_SEC (8)
        fx.startSteam();            // stretch
        fx.tick(2.0);
        fx.tick(3.0);               // almost via seconds-remaining (6 - 3 <= 3)
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(ALMOST));
        fx.tick(5.8);
        fx.flowStopped();           // done
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(DONE));
        // stretch + almost + done — roll never fired.
        QCOMPARE(fx.cueSpy.count(), 3);
        QCOMPARE(fx.speakSpy.count(), 3);
        for (int i = 0; i < fx.speakSpy.count(); ++i)
            QVERIFY(fx.speakSpy.at(i).at(0).toString() != QString::fromUtf8(ROLL));
    }

    void fullSweep_orderingAndOneShotLatches() {
        Fixture fx(true, true, 20);
        fx.startSteam();
        // Fine-grained sweep — each milestone must fire exactly once, in order,
        // with no re-fires on later ticks (one-shot latches are the only rate
        // control; there is no spacing governor to swallow anything).
        QStringList texts;
        texts << fx.coach.cueText();  // stretch fired at start
        int lastCount = fx.cueSpy.count();
        for (double t = 0.1; t <= 19.9; t += 0.1) {
            fx.tick(t);
            if (fx.cueSpy.count() > lastCount) {
                lastCount = fx.cueSpy.count();
                texts << fx.coach.cueText();
            }
        }
        QCOMPARE(texts, QStringList()
                     << QString::fromUtf8(STRETCH)
                     << QString::fromUtf8(ROLL)
                     << QString::fromUtf8(ALMOST));
        QCOMPARE(fx.cueSpy.count(), 3);
        QCOMPARE(fx.speakSpy.count(), 3);
    }

    // ==========================================
    // Completion semantics (event-based, exactly once)
    // ==========================================

    void completionFiresFromEvent_whenClockLagsThePredictedEnd() {
        // The regression this design exists for: the firmware auto-stops while
        // the local clock still reads short of the timeout. The old predicted
        // [timeout-1s, timeout] window would never be entered (the clock
        // freezes at flow stop); the event must deliver the cue anyway.
        Fixture fx(true, true, 20);
        fx.startSteam();
        fx.tick(7.0);
        fx.tick(17.5);    // clock frozen here — never reaches 19..20
        fx.flowStopped();
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(DONE));
        QCOMPARE(fx.coach.cueSeverity(), QStringLiteral("positive"));
        QCOMPARE(fx.speakSpy.last().at(0).toString(), QString::fromUtf8(DONE));
        QCOMPARE(fx.speakSpy.last().at(1).toBool(), true);
    }

    void completionFiresExactlyOnce() {
        Fixture fx(true, true, 20);
        fx.startSteam();
        // Jump straight to 19s: the ROLL latch is deliberately left unfired
        // (never ticked through 7s). That's what makes the frozen-clock
        // re-tick below a real probe — without the m_flowStopped guard in
        // evaluate(), it would fire roll and fail the count compare. Adding
        // an earlier tick(7.0) here would silently defuse this test's pin on
        // the never-coach-a-stopped-flow invariant.
        fx.tick(19.0);
        fx.flowStopped();
        const int cuesAfterDone = fx.cueSpy.count();
        const int speaksAfterDone = fx.speakSpy.count();
        fx.flowStopped();  // duplicate event (emitter is once-latched too — belt and suspenders)
        fx.tick(19.0);     // frozen-clock re-tick
        QCOMPARE(fx.cueSpy.count(), cuesAfterDone);
        QCOMPARE(fx.speakSpy.count(), speaksAfterDone);
    }

    void earlyManualAbort_isSilent() {
        Fixture fx(true, true, 60);
        fx.startSteam();  // stretch
        fx.tick(10.0);
        const int cuesBefore = fx.cueSpy.count();
        const int speaksBefore = fx.speakSpy.count();
        fx.flowStopped(); // 50s remaining — deliberate abort, no announcement
        QCOMPARE(fx.cueSpy.count(), cuesBefore);
        QCOMPARE(fx.speakSpy.count(), speaksBefore);
        QVERIFY(fx.coach.cueText() != QString::fromUtf8(DONE));
    }

    // ==========================================
    // Reset between operations
    // ==========================================

    void resetBetweenOperations_secondSteamCoachedFresh() {
        Fixture fx(true, false, 20);
        fx.startSteam();
        fx.tick(7.0);
        fx.tick(19.5);
        fx.flowStopped();
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(DONE));
        fx.endSteam();
        QVERIFY(!fx.coach.cueActive());  // lingering cue cleared

        fx.cueSpy.clear();
        fx.startSteam();  // latches cleared — stretch fires again
        QCOMPARE(fx.cueSpy.count(), 1);
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(STRETCH));
        fx.tick(7.0);     // and the mid-steam milestones re-arm too
        QCOMPARE(fx.coach.cueText(), QString::fromUtf8(ROLL));
    }
};

QTEST_GUILESS_MAIN(tst_LiveSteamCoach)
#include "tst_livesteamcoach.moc"
