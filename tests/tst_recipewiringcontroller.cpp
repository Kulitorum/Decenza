#include <QtTest>

#include "controllers/recipewiringcontroller.h"

// Unit tests for the active-recipe write-through wiring extracted from
// MainController (extract-recipe-wiring-controller). The whole point of the
// extraction is that this decision-rich, bug-prone logic — Flow-3, the
// echo-guard, Bug B2 (a re-read racing a write-through), the 2026-07-28
// three-shots-wrong incident — is now testable with synchronous fakes, without
// constructing the MainController god-object or linking the app graph.
//
// The fakes drive the async round-trip deterministically: requestRecipe /
// requestUpdateRecipe record the call; the test then decides exactly when (and
// with what payload) the result re-enters the controller via onRecipeReady /
// onRecipeUpdated, so any callback ordering — including the adversarial ones —
// is reproducible.

namespace {

struct FakeDial : DialPort {
    qint64 recipeId = -1;
    qint64 bagId = -1;
    qint64 equipmentId = -1;
    QString grind;
    int rpm = 0;
    // Record every write so a test can assert "the dial was not touched".
    int grindWrites = 0;
    int rpmWrites = 0;
    int setActiveRecipeCalls = 0;
    int lastSetActiveRecipeId = 0;
    double lastSetActiveRecipeDose = -1;
    int setActiveRecipeIdCalls = 0;
    // Optional shared order log (see FakeEffects): records the grind push so an
    // ordering test can pin it relative to the brew-override upload.
    QStringList* seq = nullptr;

    qint64 activeRecipeId() const override { return recipeId; }
    qint64 activeBagId() const override { return bagId; }
    qint64 activeEquipmentId() const override { return equipmentId; }
    QString grinderSetting() const override { return grind; }
    void setGrinderSetting(const QString& value) override {
        grind = value; ++grindWrites;
        if (seq) seq->append(QStringLiteral("grind"));
    }
    int grinderRpm() const override { return rpm; }
    void setGrinderRpm(int v) override { rpm = v; ++rpmWrites; }
    void setActiveRecipe(int id, double doseG) override {
        ++setActiveRecipeCalls;
        lastSetActiveRecipeId = id;
        lastSetActiveRecipeDose = doseG;
    }
    void setActiveRecipeId(int id) override { ++setActiveRecipeIdCalls; recipeId = id; }
};

struct FakeStore : RecipeStorePort {
    struct Update { qint64 id; QVariantMap fields; };
    QVector<Update> updates;
    QVector<qint64> reads;

    void requestUpdateRecipe(qint64 id, const QVariantMap& fields) override {
        updates.push_back({id, fields});
    }
    void requestRecipe(qint64 id) override { reads.push_back(id); }
};

struct FakeProfile : ProfilePort {
    QString title;
    QString currentProfileTitle() const override { return title; }
};

struct FakeEffects : SideEffectPort {
    int activeRecipeChangedEmits = 0;
    int plainDeactivates = 0;
    int profileDivergenceDeactivates = 0;
    int ingredientDivergenceDeactivates = 0;
    bool lastIngredientPitcherGone = false;
    int steamApplies = 0;
    int brewOverrideUploads = 0;
    // Knobs the test sets to steer the decisions.
    bool overrideArmed = false;      // applyRecipeBrewOverridesAndUpload result
    bool yieldPending = false;       // yieldRestorePending()
    int yieldRestores = 0;
    bool steamDiverges = false;
    bool vesselDiverges = false;
    // Optional shared order log: records the brew-override upload so an ordering
    // test can prove it fires BEFORE the grind push (Flow-3 sequence).
    QStringList* seq = nullptr;

    void emitActiveRecipeChanged() override { ++activeRecipeChangedEmits; }
    void deactivate() override { ++plainDeactivates; }
    void deactivateForProfileDivergence(qint64, const QString&, const QString&) override {
        ++profileDivergenceDeactivates;
    }
    void deactivateForIngredientDivergence(qint64, bool pitcherGone) override {
        ++ingredientDivergenceDeactivates;
        lastIngredientPitcherGone = pitcherGone;
    }
    void applySteamSettings() override { ++steamApplies; }
    bool applyRecipeBrewOverridesAndUpload(const QVariantMap&) override {
        if (overrideArmed) {
            ++brewOverrideUploads;
            if (seq) seq->append(QStringLiteral("override"));
        }
        return overrideArmed;
    }
    bool yieldRestorePending() const override { return yieldPending; }
    void restoreYieldAnchorAfterProfileLoad() override { ++yieldRestores; }
    bool steamPitcherDiverged(const QString&) const override { return steamDiverges; }
    bool waterVesselDiverged(const QString&) const override { return vesselDiverges; }
};

// A coffee recipe row with a profile and a pinned grind, matching the loaded
// profile so no divergence fires unless a test asks for it.
QVariantMap coffeeRow(qint64 id, const QString& grind, qint64 rpm = 0) {
    QVariantMap r;
    r.insert("id", id);
    r.insert("drinkType", "espresso");
    r.insert("profileTitle", "D-Flow / Q");
    r.insert("doseG", 18.0);
    r.insert("grindPinned", grind);
    r.insert("rpmPinned", rpm);
    return r;
}

} // namespace

class tst_RecipeWiringController : public QObject {
    Q_OBJECT

    FakeDial dial;
    FakeStore store;
    FakeProfile profile;
    FakeEffects effects;

    // A fresh controller + fakes per test. The profile title matches coffeeRow
    // so profileDiverged is false by default.
    RecipeWiringController* make() {
        dial = FakeDial();
        store = FakeStore();
        profile = FakeProfile();
        effects = FakeEffects();
        profile.title = "D-Flow / Q";
        return new RecipeWiringController(dial, store, profile, effects);
    }

    // Simulate an external edit arriving for the active recipe: recipeUpdated
    // (success) → the controller re-reads → recipeReady with the edited row.
    void deliverEdit(RecipeWiringController* c, qint64 id, const QVariantMap& row) {
        c->onRecipeUpdated(id, true);
        QVERIFY(!store.reads.isEmpty());  // the edit armed a re-read
        c->onRecipeReady(id, row);
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // --- Active-edit → dial refresh (Flow-3) --------------------------------
    void activeEditRefreshesDial() {
        auto* c = make();
        dial.recipeId = 7;
        deliverEdit(c, 7, coffeeRow(7, "3.2"));
        QCOMPARE(dial.grind, QString("3.2"));
        QCOMPARE(dial.grindWrites, 1);
        delete c;
    }

    void activeEditPushesRpmWhenPositive() {
        auto* c = make();
        dial.recipeId = 7;
        deliverEdit(c, 7, coffeeRow(7, "3.2", 950));
        QCOMPARE(dial.grind, QString("3.2"));
        QCOMPARE(dial.rpm, 950);
        delete c;
    }

    // --- Inactive recipe: no dial push --------------------------------------
    void inactiveRecipeUpdateDoesNotTouchDial() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeUpdated(99, true);         // not the active recipe
        QVERIFY(store.reads.isEmpty());       // no re-read armed
        c->onRecipeReady(99, coffeeRow(99, "9.9"));  // and if a stray read lands
        QCOMPARE(dial.grindWrites, 0);
        delete c;
    }

    // --- Non-edit reads (startup/relink/prefill): no dial push --------------
    void nonEditReadDoesNotRefreshDial() {
        auto* c = make();
        dial.recipeId = 7;
        // A plain recipeReady with NO preceding recipeUpdated (startup restore,
        // relink refresh) must not arm the Flow-3 refresh.
        c->onRecipeReady(7, coffeeRow(7, "5.0"));
        QCOMPARE(dial.grindWrites, 0);
        // The cache + dose rung + emit still happen (that is the restore).
        QCOMPARE(dial.setActiveRecipeCalls, 1);
        QCOMPARE(effects.activeRecipeChangedEmits, 1);
        delete c;
    }

    // --- Grind-less drink type / empty grind: no dial push ------------------
    void teaRecipeEditLeavesDialUntouched() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap tea = coffeeRow(7, "3.2");
        tea.insert("drinkType", "tea");          // grind-less
        tea.insert("profileTitle", QString());   // tea is profile-less
        deliverEdit(c, 7, tea);
        QCOMPARE(dial.grindWrites, 0);
        delete c;
    }

    void emptyGrindEditLeavesDialUntouched() {
        auto* c = make();
        dial.recipeId = 7;
        deliverEdit(c, 7, coffeeRow(7, QString()));  // empty grind
        QCOMPARE(dial.grindWrites, 0);
        delete c;
    }

    // --- The edit-refresh flag is cleared on switch/deactivate --------------
    void refreshFlagClearedOnSwitch() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeUpdated(7, true);     // arms the refresh flag + a re-read
        c->onActivationStarting();       // a switch arrives before the re-read
        // The now-stale re-read lands: it must NOT push a grind onto the dial.
        c->onRecipeReady(7, coffeeRow(7, "8.8"));
        QCOMPARE(dial.grindWrites, 0);
        delete c;
    }

    void refreshFlagClearedOnDeactivate() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeUpdated(7, true);      // arms the flag
        c->clearSelfWriteState();         // deactivate's first step
        c->onRecipeReady(7, coffeeRow(7, "8.8"));
        QCOMPARE(dial.grindWrites, 0);
        delete c;
    }

    // --- Echo-guard: no loop, no drift (even on a failed stamp) -------------
    void stampIncrementsCounterAndEchoIsSwallowed() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));  // seed the cache
        const qsizetype readsBefore = store.reads.size();
        // A dial edit stamps the recipe (counter -> 1).
        c->stampActiveRecipe(/*applying=*/false, "grindPinned", QString("4.0"));
        QCOMPARE(store.updates.size(), 1);
        // Its recipeUpdated echo must be swallowed: no re-read, no refresh.
        c->onRecipeUpdated(7, true);
        QCOMPARE(store.reads.size(), readsBefore);   // no new read armed
        delete c;
    }

    void failedStampEchoStillDecrements() {
        // The echo-guard must decrement even on a FAILED stamp, or the leaked
        // count swallows the NEXT external edit's refresh (the comment at
        // maincontroller.cpp:1779-1782).
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        c->stampActiveRecipe(false, "grindPinned", QString("4.0"));  // counter -> 1
        c->onRecipeUpdated(7, /*success=*/false);                    // failed stamp echo
        // Now a genuine external edit must still refresh.
        const qsizetype readsBefore = store.reads.size();
        c->onRecipeUpdated(7, true);
        QCOMPARE(store.reads.size(), readsBefore + 1);  // refresh armed, not swallowed
        delete c;
    }

    void equalValueStampDoesNotWrite() {
        // The equality guard: a dial-refresh re-entering as a dial-changed event
        // (the cache already holds the value) issues no further store update.
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        c->stampActiveRecipe(false, "grindPinned", QString("3.0"));  // same as cache
        QCOMPARE(store.updates.size(), 0);  // no loop
        delete c;
    }

    void stampNoOpWhileApplying() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        c->stampActiveRecipe(/*applying=*/true, "grindPinned", QString("4.0"));
        QCOMPARE(store.updates.size(), 0);
        delete c;
    }

    // --- Deactivate-if-empty / archived / diverged on re-read ---------------
    void emptyRowDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, QVariantMap());
        QCOMPARE(effects.plainDeactivates, 1);
        QCOMPARE(dial.setActiveRecipeCalls, 0);  // never got to the cache
        delete c;
    }

    void archivedRowDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("archived", true);
        c->onRecipeReady(7, row);
        QCOMPARE(effects.plainDeactivates, 1);
        delete c;
    }

    void profileDivergenceDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        profile.title = "Some Other Profile";   // loaded profile != recipe's
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        QCOMPARE(effects.profileDivergenceDeactivates, 1);
        QCOMPARE(dial.setActiveRecipeCalls, 0);  // before the cache assignment
        delete c;
    }

    void pitcherDivergenceDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        effects.steamDiverges = true;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        QCOMPARE(effects.ingredientDivergenceDeactivates, 1);
        QVERIFY(effects.lastIngredientPitcherGone);  // pitcher, not vessel
        delete c;
    }

    void vesselDivergenceDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        effects.vesselDiverges = true;   // pitcher ok, vessel gone
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        QCOMPARE(effects.ingredientDivergenceDeactivates, 1);
        QVERIFY(!effects.lastIngredientPitcherGone);  // vessel wording
        delete c;
    }

    // --- Deactivate-on-ingredient-swap decisions ----------------------------
    void bagSwapDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("bagId", 5);
        c->onRecipeReady(7, row);         // resolvedBagId defaults from dial.bagId (-1)
        dial.bagId = 99;                  // user switched to a different bag
        QVERIFY(c->shouldDeactivateOnBagChange(/*applying=*/false));
        delete c;
    }

    void bagSwapToOwnBagDoesNotDeactivate() {
        auto* c = make();
        dial.recipeId = 7;
        dial.bagId = 5;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("bagId", 5);
        c->onRecipeReady(7, row);  // resolvedBagId = 5
        // Dial still on bag 5 (the recipe's own bag) — no deactivate.
        QVERIFY(!c->shouldDeactivateOnBagChange(false));
        delete c;
    }

    void bagSwapWhileApplyingDoesNotDeactivate() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("bagId", 5);
        c->onRecipeReady(7, row);
        dial.bagId = 99;
        QVERIFY(!c->shouldDeactivateOnBagChange(/*applying=*/true));
        delete c;
    }

    void bagSwapWithNoBeanLinkDoesNotDeactivate() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));  // no bagId / bean fields
        dial.bagId = 99;
        QVERIFY(!c->shouldDeactivateOnBagChange(false));
        delete c;
    }

    void equipmentSwapDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("equipmentId", 3);
        c->onRecipeReady(7, row);
        dial.equipmentId = 8;  // different grinder package
        QVERIFY(c->shouldDeactivateOnEquipmentChange(false));
        delete c;
    }

    void equipmentSwapToOwnDoesNotDeactivate() {
        auto* c = make();
        dial.recipeId = 7;
        dial.equipmentId = 3;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("equipmentId", 3);
        c->onRecipeReady(7, row);
        QVERIFY(!c->shouldDeactivateOnEquipmentChange(false));
        delete c;
    }

    void profileSwapDeactivates() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));  // profile "D-Flow / Q"
        QVERIFY(c->shouldDeactivateOnProfileChange(false, "Different Profile"));
        QVERIFY(!c->shouldDeactivateOnProfileChange(false, "D-Flow / Q"));
        delete c;
    }

    void profileSwapWhileApplyingDoesNotDeactivate() {
        auto* c = make();
        dial.recipeId = 7;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        QVERIFY(!c->shouldDeactivateOnProfileChange(/*applying=*/true, "Different Profile"));
        delete c;
    }

    // --- rpm-clear parity limitation (recorded, pinned) ---------------------
    void zeroingRpmDoesNotClearDialRpm() {
        // Documented parity with activation: editing rpm to 0 does NOT push 0
        // onto the dial (the `rpm > 0` guard). The test pins current behavior so
        // a future fix is deliberate (spec scenario).
        auto* c = make();
        dial.recipeId = 7;
        dial.rpm = 850;                      // a prior rpm on the dial
        deliverEdit(c, 7, coffeeRow(7, "3.2", /*rpm=*/0));
        QCOMPARE(dial.rpm, 850);             // retained, not cleared to 0
        QCOMPARE(dial.rpmWrites, 0);
        delete c;
    }

    // --- Ordering: Flow-3 brew-override upload before the grind push --------
    void editAppliesBrewOverridesThenGrind() {
        auto* c = make();
        dial.recipeId = 7;
        effects.overrideArmed = true;
        QStringList seq;
        dial.seq = &seq;
        effects.seq = &seq;
        deliverEdit(c, 7, coffeeRow(7, "3.2"));
        // Both effects fired for the same edit: the brew-override upload AND the
        // grind push. The dose rung + steam re-assert come between, unchanged.
        QCOMPARE(effects.brewOverrideUploads, 1);
        QCOMPARE(dial.grindWrites, 1);
        QCOMPARE(effects.steamApplies, 1);
        // And in THAT order — the override+upload must precede the grind push,
        // exactly as the original inline code ran them (swapping them passes the
        // three counts above but fails here).
        QCOMPARE(seq, (QStringList{QStringLiteral("override"), QStringLiteral("grind")}));
        delete c;
    }

    // --- Bug B2: the re-read's dose rung uses the freshly-read row -----------
    void reReadArmsDoseRungFromEditedRow() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.2");
        row.insert("doseG", 19.5);           // the edit changed the dose
        deliverEdit(c, 7, row);
        QCOMPARE(dial.setActiveRecipeCalls, 1);
        QCOMPARE(dial.lastSetActiveRecipeId, 7);
        QCOMPARE(dial.lastSetActiveRecipeDose, 19.5);  // from the edited row
        delete c;
    }

    // --- Yield restore fires when pending on a re-read ----------------------
    void yieldRestoreRunsWhenPending() {
        auto* c = make();
        dial.recipeId = 7;
        effects.yieldPending = true;
        c->onRecipeReady(7, coffeeRow(7, "3.0"));
        QCOMPARE(effects.yieldRestores, 1);
        delete c;
    }

    // --- External bag/equipment mutators (relink / package fork) ------------
    void bagLinkMutatorFollowsSuccessor() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("bagId", 5);
        c->onRecipeReady(7, row);
        c->updateActiveRecipeBagLink(42);
        QCOMPARE(c->activeRecipe().value("bagId").toLongLong(), qint64(42));
        QCOMPARE(c->activeRecipe().value("resolvedBagId").toLongLong(), qint64(42));
        delete c;
    }

    void equipmentMutatorFollowsFork() {
        auto* c = make();
        dial.recipeId = 7;
        QVariantMap row = coffeeRow(7, "3.0");
        row.insert("equipmentId", 3);
        c->onRecipeReady(7, row);
        c->updateActiveRecipeEquipmentId(8);
        QCOMPARE(c->activeRecipe().value("equipmentId").toLongLong(), qint64(8));
        delete c;
    }
};

QTEST_MAIN(tst_RecipeWiringController)
#include "tst_recipewiringcontroller.moc"
