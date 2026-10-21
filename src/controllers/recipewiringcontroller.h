#pragma once

#include <QVariantMap>

#include "recipewiringports.h"

// The active-recipe write-through STATE and DECISIONS, extracted verbatim from
// MainController (extract-recipe-wiring-controller). A plain C++ class — no
// Q_OBJECT, no connect, no event loop — so it can be unit-tested with
// synchronous fakes (tst_recipewiringcontroller). MainController is the
// adapter: it forwards the recipe/dial Qt signals to these methods and
// implements the four ports over the real objects.
//
// This owns the three state members that used to live on MainController:
//   - m_activeRecipe              the cached active-recipe row (empty = none)
//   - m_pendingRecipeSelfWrites   the write-through echo-guard counter
//   - m_refreshDialFromRecipeEdit the Flow-3 edit-refresh flag
//
// m_applyingRecipe stays on MainController (it guards the heavy activation body
// that does NOT move) and is passed in as an argument on the methods that read
// it.
class RecipeWiringController {
public:
    RecipeWiringController(DialPort& dial, RecipeStorePort& store, ProfilePort& profile,
                           SideEffectPort& effects)
        : m_dial(dial), m_store(store), m_profile(profile), m_effects(effects) {}

    // --- State (read by MainController's internal reads + the activeRecipe
    // Q_PROPERTY). The const-ref getter is for hot internal C++ reads (steam
    // policy, yield baseline) so they do not copy the map; the QML property
    // copies at its boundary.
    const QVariantMap& activeRecipe() const { return m_activeRecipe; }

    // --- External mutators (writers outside the write-through cluster that
    // must follow the cache; the decisions themselves stay in MainController).
    // finishedBagRolled: the active recipe moved onto the successor bag.
    void updateActiveRecipeBagLink(qint64 successorBagId) {
        m_activeRecipe.insert(QStringLiteral("bagId"), successorBagId);
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), successorBagId);
    }
    // packageSuperseded: the active recipe's grinder package forked.
    void updateActiveRecipeEquipmentId(qint64 toId) {
        m_activeRecipe.insert(QStringLiteral("equipmentId"), toId);
    }

    // --- Inbound events (forwarded from MainController's Qt signals).

    // recipeUpdated active-cache handler (maincontroller.cpp:1772). Echo-guard
    // decrement (even on a failed stamp) + arm the edit-refresh flag + re-read.
    void onRecipeUpdated(qint64 recipeId, bool success);

    // recipeReady active-cache handler (maincontroller.cpp:1798). Consume the
    // refresh flag; deactivate on empty/archived/diverged; update the cache;
    // dose rung; emit; steam; refreshDial brew-overrides + grind/rpm push.
    void onRecipeReady(qint64 recipeId, const QVariantMap& recipe);

    // stampActiveRecipe (maincontroller.cpp:2873): dial->recipe write-through
    // with the equality guard + self-write counter increment. applyingRecipe is
    // MainController's activation guard.
    void stampActiveRecipe(bool applyingRecipe, const QString& field, const QVariant& value);

    // The deactivate-on-ingredient-swap DECISIONS (maincontroller.cpp:1949 /
    // 1962 / 2014). Each returns true iff the caller should deactivate; the
    // effect stays in MainController (deactivateRecipe()).
    bool shouldDeactivateOnBagChange(bool applyingRecipe) const;
    bool shouldDeactivateOnEquipmentChange(bool applyingRecipe) const;
    bool shouldDeactivateOnProfileChange(bool applyingRecipe,
                                         const QString& loadedProfileTitle) const;

    // --- Activation / deactivation state coordination (the heavy bodies stay
    // in MainController; only the three state members move here).
    // applyActivatedRecipe head (maincontroller.cpp:2341/2345): clear leaked
    // counter + edit-refresh flag at the top of a switch.
    void onActivationStarting() {
        m_pendingRecipeSelfWrites = 0;
        m_refreshDialFromRecipeEdit = false;
    }
    // applyActivatedRecipe cache-set (maincontroller.cpp:2493 / 2589): adopt
    // the activated row + resolved bag. Does NOT emit — the heavy body emits
    // activeRecipeChanged itself, exactly as before.
    void setActiveRecipeCache(const QVariantMap& recipe, qint64 linkedBagId) {
        m_activeRecipe = recipe;
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), linkedBagId);
    }
    // deactivateRecipe state-clear, split to preserve MainController's exact
    // order: drop the self-write count + edit-refresh flag FIRST (their echo
    // would otherwise land with no active recipe and leak the count), then
    // MainController sets activeRecipeId(-1), THEN clears the cache.
    void clearSelfWriteState() {
        m_pendingRecipeSelfWrites = 0;
        m_refreshDialFromRecipeEdit = false;
    }
    // Clear the active-recipe cache. Returns true iff it was non-empty (so
    // MainController emits activeRecipeChanged, exactly as before).
    bool clearActiveRecipeCache() {
        if (!m_activeRecipe.isEmpty()) {
            m_activeRecipe.clear();
            return true;
        }
        return false;
    }

private:
    DialPort& m_dial;
    RecipeStorePort& m_store;
    ProfilePort& m_profile;
    SideEffectPort& m_effects;

    QVariantMap m_activeRecipe;
    int m_pendingRecipeSelfWrites = 0;
    bool m_refreshDialFromRecipeEdit = false;
};
