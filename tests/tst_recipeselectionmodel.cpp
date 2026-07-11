#include <QtTest>

#include "controllers/recipeselectionmodel.h"

// Unit tests for the pure selection/deferred-start state machine behind
// MainController::selectedRecipeId (add-recipes, PR #1470). Extracted from
// MainController precisely so this logic — the part that can pull a shot on the
// wrong recipe if it regresses — is testable without linking the whole app.

using SD = RecipeSelectionModel::StartDecision;

class tst_RecipeSelectionModel : public QObject {
    Q_OBJECT

private slots:
    void defaultsToNone() {
        RecipeSelectionModel m;
        QCOMPARE(m.selected(), qint64(-1));
        QCOMPARE(m.pendingStart(), qint64(-1));
    }

    void resetNormalizesNonPositive() {
        RecipeSelectionModel m;
        m.reset(7);
        QCOMPARE(m.selected(), qint64(7));
        m.reset(0);   // 0 is a de-facto "none" in the codebase's > 0 guards
        QCOMPARE(m.selected(), qint64(-1));
        m.reset(-1);
        QCOMPARE(m.selected(), qint64(-1));
    }

    void activateSelectsOptimistically() {
        RecipeSelectionModel m;
        QVERIFY(m.onActivate(42));            // changed
        QCOMPARE(m.selected(), qint64(42));
        QVERIFY(!m.onActivate(42));           // no-op, no change signal
    }

    void activateNormalizesZero() {
        RecipeSelectionModel m;
        m.onActivate(5);
        QVERIFY(m.onActivate(0));             // 0 -> -1, that IS a change
        QCOMPARE(m.selected(), qint64(-1));
    }

    void successConfirmsKeepsSelection() {
        RecipeSelectionModel m;
        m.onActivate(42);
        // applyActivatedRecipe sets activeRecipeId=42 -> activeRecipeIdChanged.
        QVERIFY(!m.onActiveRecipeChanged(42)); // already 42, no thrash
        QCOMPARE(m.selected(), qint64(42));
    }

    void deactivateClearsSelection() {
        RecipeSelectionModel m;
        m.onActivate(42);
        QVERIFY(m.onActiveRecipeChanged(-1));  // deactivate -> cleared
        QCOMPARE(m.selected(), qint64(-1));
    }

    void failureRevertsToPriorActive() {
        RecipeSelectionModel m;
        m.onActivate(10);
        m.onActiveRecipeChanged(10);           // 10 is the confirmed active recipe
        m.onActivate(42);                      // optimistic switch to 42
        QCOMPARE(m.selected(), qint64(42));
        // 42's activation fails; activeRecipeId is still 10.
        const auto o = m.onActivationResult(42, /*success=*/false, /*activeId=*/10);
        QVERIFY(o.reverted);
        QVERIFY(o.selectedChanged);
        QCOMPARE(m.selected(), qint64(10));    // rolled back to the still-active recipe
        QVERIFY(!o.fireStart);
    }

    void staleFailureDoesNotClobberNewerSelection() {
        RecipeSelectionModel m;
        m.onActivate(1);
        m.onActivate(2);                       // user moved on to 2 before 1 resolved
        QCOMPARE(m.selected(), qint64(2));
        const auto o = m.onActivationResult(1, /*success=*/false, /*activeId=*/-1);
        QVERIFY(!o.reverted);                  // guard: only reverts if selected == failed id
        QCOMPARE(m.selected(), qint64(2));     // untouched
    }

    void startNowWhenAlreadyApplied() {
        RecipeSelectionModel m;
        m.onActivate(7);
        m.onActiveRecipeChanged(7);            // applied
        QCOMPARE(m.requestStart(/*activeId=*/7), SD::StartNow);
        QCOMPARE(m.pendingStart(), qint64(-1)); // no defer armed
    }

    void startDefersWhilePending() {
        RecipeSelectionModel m;
        m.onActivate(5);                       // activeId still not 5
        QCOMPARE(m.requestStart(/*activeId=*/-1), SD::Deferred);
        QCOMPARE(m.pendingStart(), qint64(5));
    }

    void deferredStartFiresOnApply() {
        RecipeSelectionModel m;
        m.onActivate(5);
        QCOMPARE(m.requestStart(-1), SD::Deferred);
        // 5 finishes applying.
        const auto o = m.onActivationResult(5, /*success=*/true, /*activeId=*/5);
        QVERIFY(o.fireStart);
        QVERIFY(!o.startDropped);
        QCOMPARE(m.pendingStart(), qint64(-1)); // consumed one-shot
    }

    void deferredStartDroppedOnFailure() {
        RecipeSelectionModel m;
        m.onActivate(5);
        m.requestStart(-1);                    // armed
        const auto o = m.onActivationResult(5, /*success=*/false, /*activeId=*/-1);
        QVERIFY(!o.fireStart);
        QVERIFY(o.startDropped);
        QCOMPARE(m.pendingStart(), qint64(-1));
    }

    void reselectCancelsArmedStart() {
        RecipeSelectionModel m;
        m.onActivate(3);
        m.requestStart(-1);                    // armed for 3
        QCOMPARE(m.pendingStart(), qint64(3));
        m.onActivate(9);                       // user picks a different recipe
        QCOMPARE(m.pendingStart(), qint64(-1)); // armed start cancelled
        // 3's late completion must NOT start a shot.
        const auto o = m.onActivationResult(3, /*success=*/true, /*activeId=*/3);
        QVERIFY(!o.fireStart);
        QVERIFY(!o.startDropped);               // nothing was armed for 3
    }

    void requestStartNoneWhenNothingSelected() {
        RecipeSelectionModel m;
        QCOMPARE(m.requestStart(-1), SD::None);
        QCOMPARE(m.pendingStart(), qint64(-1));
    }
};

QTEST_APPLESS_MAIN(tst_RecipeSelectionModel)
#include "tst_recipeselectionmodel.moc"
