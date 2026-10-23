#include <QtTest>

#include "core/steamstopstate.h"

class tst_SteamStopState : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void twoTapRequiresFreshRenderedStateAndPurgesOnce()
    {
        SteamStopState state;
        state.observeMachine(true, true);

        QCOMPARE(state.request(true, true, true, false), SteamStopState::Decision::SoftStop);
        QVERIFY(state.softStopped());
        QVERIFY(!state.purgeRequested());

        // A second client holding the pre-stop snapshot cannot turn its stale
        // first-tap request into the purge action.
        QCOMPARE(state.request(true, true, true, false), SteamStopState::Decision::Rejected);
        QVERIFY(state.softStopped());

        QCOMPARE(state.request(true, true, true, true), SteamStopState::Decision::Purge);
        QVERIFY(state.softStopped());
        QVERIFY(state.purgeRequested());

        // A queued duplicate second tap cannot dispatch another purge.
        QCOMPARE(state.request(true, true, true, true), SteamStopState::Decision::Rejected);
    }

    void rawStateExitAndReconnectResetForNextCycle()
    {
        SteamStopState state;
        state.observeMachine(true, true);
        QCOMPARE(state.request(true, true, true, false), SteamStopState::Decision::SoftStop);

        // Puffing and Ending are still raw Steam; neither should clear the
        // first-tap state while the operation remains active.
        state.observeMachine(true, true);
        QVERIFY(state.softStopped());

        state.observeMachine(true, false);
        QVERIFY(!state.softStopped());
        QVERIFY(!state.purgeRequested());

        // The next raw Steam entry starts as a fresh first tap.
        state.observeMachine(true, true);
        QCOMPARE(state.request(true, true, true, false), SteamStopState::Decision::SoftStop);
    }

    void disconnectCancelsPendingPurgeAndRejectsStalePurge()
    {
        SteamStopState state;
        state.observeMachine(true, true);
        QCOMPARE(state.request(true, true, true, false), SteamStopState::Decision::SoftStop);
        QCOMPARE(state.request(true, true, true, true), SteamStopState::Decision::Purge);
        QVERIFY(state.purgeRequested());

        state.observeMachine(false, true);
        QVERIFY(!state.softStopped());
        QVERIFY(!state.purgeRequested());
        QCOMPARE(state.request(false, true, true, true), SteamStopState::Decision::Rejected);

        // Reconnection to a fresh Steam cycle must reject an old true snapshot
        // rather than interpreting it as the second tap.
        state.observeMachine(true, true);
        QCOMPARE(state.request(true, true, true, true), SteamStopState::Decision::Rejected);
        QCOMPARE(state.request(true, true, true, false), SteamStopState::Decision::SoftStop);
    }

    void disablingTwoTapUsesSinglePurge()
    {
        SteamStopState state;
        state.observeMachine(true, true);

        QCOMPARE(state.request(true, true, false, false), SteamStopState::Decision::Purge);
        QVERIFY(!state.softStopped());
        QVERIFY(state.purgeRequested());
    }
};

QTEST_GUILESS_MAIN(tst_SteamStopState)
#include "tst_steamstopstate.moc"
