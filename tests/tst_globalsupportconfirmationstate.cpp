#include "remote/globalsupportconfirmationstate_p.h"

#include <QtTest>

class GlobalSupportConfirmationStateTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void timeoutUsesOneHostDeadlineAndOnlyOneTerminalEventWins()
    {
        GlobalSupportConfirmationState state;
        constexpr qint64 deadline = 115000;
        QVERIFY(state.begin(QStringLiteral("confirm-1"), QStringLiteral("Start shot"),
                            deadline, 15000));
        QCOMPARE(state.id(), QStringLiteral("confirm-1"));
        QCOMPARE(state.deadlineEpochMs(), deadline);

        // A UI that connects late reads the same deadline; observing it cannot
        // restart or extend the pending confirmation's lifetime.
        QVERIFY(state.isCurrent(QStringLiteral("confirm-1")));
        QCOMPARE(state.deadlineEpochMs(), deadline);
        QVERIFY(state.takeExpired(14999).isEmpty());

        QCOMPARE(state.takeExpired(15000), QStringLiteral("confirm-1"));
        QVERIFY(state.id().isEmpty());
        QVERIFY(state.takeExpired(15001).isEmpty());
        QVERIFY(!state.clearIfCurrent(QStringLiteral("confirm-1")));
    }

    void staleCancellationAndSupersededResponsesCannotClearNewConfirmation()
    {
        GlobalSupportConfirmationState state;
        QVERIFY(state.begin(QStringLiteral("old-id"), QStringLiteral("Old"), 200, 20));
        QVERIFY(state.begin(QStringLiteral("new-id"), QStringLiteral("New"), 300, 30));

        QVERIFY(!state.clearIfCurrent(QStringLiteral("old-id")));
        QVERIFY(state.isCurrent(QStringLiteral("new-id")));
        QVERIFY(state.takeExpired(29).isEmpty());
        QCOMPARE(state.takeExpired(30), QStringLiteral("new-id"));
        QVERIFY(!state.clearIfCurrent(QStringLiteral("new-id")));
    }

    void aResponseAtDeadlineLosesToTimeoutAndCannotResolveAgain()
    {
        GlobalSupportConfirmationState state;
        constexpr qint64 deadline = 9000;
        QVERIFY(state.begin(QStringLiteral("confirm-1"), QStringLiteral("Start shot"),
                            deadline, 15));

        QCOMPARE(state.resolve(QStringLiteral("confirm-1"), 15),
                 GlobalSupportConfirmationState::Resolution::TimedOut);
        QVERIFY(state.takeExpired(15).isEmpty());
        QCOMPARE(state.resolve(QStringLiteral("confirm-1"), 14),
                 GlobalSupportConfirmationState::Resolution::Stale);

        QVERIFY(state.begin(QStringLiteral("confirm-2"), QStringLiteral("Start shot"),
                            deadline, 15));
        QCOMPARE(state.resolve(QStringLiteral("confirm-2"), 14),
                 GlobalSupportConfirmationState::Resolution::Accepted);
        QVERIFY(state.takeExpired(15).isEmpty());
    }
};

QTEST_GUILESS_MAIN(GlobalSupportConfirmationStateTest)
#include "tst_globalsupportconfirmationstate.moc"
