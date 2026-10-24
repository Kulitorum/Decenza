#pragma once

#include <QSet>
#include <QVariantMap>

#include "recipewiringports.h"

// The active-recipe write-through state and decisions. A plain C++ class — no
// Q_OBJECT, no connect, no event loop — so tst_recipewiringcontroller can drive
// it with synchronous fakes. MainController forwards the recipe/dial Qt signals
// to these methods and implements the four ports over the real objects.
//
// m_applyingRecipe stays on MainController (it guards the activation body,
// which did not move) and is passed in to the methods that read it.
class RecipeWiringController {
public:
    RecipeWiringController(DialPort& dial, RecipeStorePort& store, LiveStatePort& state,
                           SideEffectPort& effects)
        : m_dial(dial), m_store(store), m_state(state), m_effects(effects) {}

    // Const ref so hot C++ reads (steam policy, yield baseline) do not copy;
    // the QML property copies at its boundary.
    const QVariantMap& activeRecipe() const { return m_activeRecipe; }

    // --- Writers outside the write-through cluster that the cache must follow.
    // finishedBagRolled: the active recipe moved onto the successor bag.
    void updateActiveRecipeBagLink(qint64 successorBagId) {
        m_activeRecipe.insert(QStringLiteral("bagId"), successorBagId);
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), successorBagId);
    }
    // packageSuperseded: the active recipe's grinder package forked.
    void updateActiveRecipeEquipmentId(qint64 toId) {
        m_activeRecipe.insert(QStringLiteral("equipmentId"), toId);
    }

    // --- Inbound RecipeStorage signals.

    // recipeUpdated: echo-guard decrement (even on a failed stamp), else arm
    // the edit-refresh flag and re-read.
    void onRecipeUpdated(qint64 recipeId, bool success);

    // Request-scoped (web) updates emit these two back to back instead of
    // recipeUpdated: the snapshot carries the committed row and refreshes the
    // cache directly, then recipeChanged must not re-read it a second time.
    void onRecipeChangedWithSnapshot(qint64 recipeId, const QVariantMap& recipe);
    void onRecipeChanged(qint64 recipeId);

    // recipeReady: consume the edit-refresh flag, then refresh from the row.
    void onRecipeReady(qint64 recipeId, const QVariantMap& recipe);

    // Dial → recipe write-through, with the equality guard and the self-write
    // counter increment.
    void stampActiveRecipe(bool applyingRecipe, const QString& field, const QVariant& value);

    // Deactivate-on-ingredient-swap decisions. True iff the caller should
    // deactivate; the effect stays in MainController::deactivateRecipe().
    bool shouldDeactivateOnBagChange(bool applyingRecipe) const;
    bool shouldDeactivateOnEquipmentChange(bool applyingRecipe) const;
    bool shouldDeactivateOnProfileChange(const QString& loadedProfileTitle) const;
    bool shouldDeactivateOnProfileDeleted(bool applyingRecipe, const QString& deletedTitle) const;
    bool shouldDeactivateOnPitcherChange(bool applyingRecipe) const;
    bool shouldDeactivateOnVesselChange(bool applyingRecipe) const;

    // --- Activation / deactivation (the bodies stay in MainController).
    // Top of applyActivatedRecipe: drop a count or flag leaked by the previous
    // recipe.
    void onActivationStarting() {
        m_pendingRecipeSelfWrites = 0;
        m_refreshDialFromRecipeEdit = false;
    }
    // Adopt the activated row. Does not emit — applyActivatedRecipe does.
    void setActiveRecipeCache(const QVariantMap& recipe, qint64 linkedBagId) {
        m_activeRecipe = recipe;
        m_activeRecipe.insert(QStringLiteral("resolvedBagId"), linkedBagId);
    }
    // deactivateRecipe's order: this FIRST (an echo landing with no active
    // recipe would otherwise leak the count), then activeRecipeId(-1), then
    // clearActiveRecipeCache().
    void clearSelfWriteState() {
        m_pendingRecipeSelfWrites = 0;
        m_refreshDialFromRecipeEdit = false;
    }
    // True iff the cache was non-empty, so the caller emits activeRecipeChanged.
    bool clearActiveRecipeCache() {
        if (!m_activeRecipe.isEmpty()) {
            m_activeRecipe.clear();
            return true;
        }
        return false;
    }

private:
    // Deactivate if empty/archived/diverged; else update the cache, dose rung
    // and steam, and on an edit re-seed the brew overrides and grind/rpm.
    void refreshFromRow(qint64 recipeId, const QVariantMap& recipe, bool refreshDial);

    DialPort& m_dial;
    RecipeStorePort& m_store;
    LiveStatePort& m_state;
    SideEffectPort& m_effects;

    QVariantMap m_activeRecipe;
    int m_pendingRecipeSelfWrites = 0;
    bool m_refreshDialFromRecipeEdit = false;
    QSet<qint64> m_rowsRefreshedFromSnapshot;
};
