#include "recipewiringcontroller.h"

#include "../core/drinktypes.h"
#include "../history/recipestorage.h"  // Recipe:: divergence statics

// All logic here is a verbatim move from MainController
// (extract-recipe-wiring-controller, design D4 — behavior-preserving). The
// conditions, order, early-returns and WHY-comments are carried intact; they
// encode bug history (Flow-3, Bug A/B2, the 2026-07-28 three-shots-wrong
// incident, the echo-guard leak reasoning). Do not simplify or re-order.

void RecipeWiringController::onRecipeUpdated(qint64 recipeId, bool success) {
    if (recipeId != m_dial.activeRecipeId())
        return;
    // Skip the re-read for our own write-through stamps — the cache
    // already holds those values (mirrors SettingsDye's bag echo skip).
    // Decrement even on a FAILED stamp: the counter must track every
    // stamp we issued, or a failed self-write would leak the count and
    // silently swallow the next external edit's refresh. (A leaked
    // count across a recipe switch is separately cleared in
    // applyActivatedRecipe / deactivateRecipe.)
    if (m_pendingRecipeSelfWrites > 0) {
        m_pendingRecipeSelfWrites--;
        return;
    }
    if (success) {
        // An external edit of the active recipe (wizard/MCP/web). Flag the
        // re-read so recipeReady mirrors the new grind/rpm onto the live
        // dial — the Shot Plan binds to Settings.dye, not the recipe cache,
        // so without this the plan stays stale until re-activation.
        m_refreshDialFromRecipeEdit = true;
        m_store.requestRecipe(recipeId);
    }
}

void RecipeWiringController::onRecipeReady(qint64 recipeId, const QVariantMap& recipe) {
    if (recipeId != m_dial.activeRecipeId())
        return;
    // Consume the edit-refresh flag only for the active recipe's own
    // re-read (a concurrent non-active read returns above without touching
    // it, so it can't swallow a pending refresh).
    const bool refreshDial = m_refreshDialFromRecipeEdit;
    m_refreshDialFromRecipeEdit = false;
    if (recipe.isEmpty() || recipe.value("archived").toBool()) {
        // Row vanished or was archived out from under the selection.
        m_effects.deactivate();
        return;
    }
    // A row naming a profile that is not the loaded one belongs in the
    // branch above: the recipe no longer describes what the machine will
    // brew. This is the ONLY place the startup restore can be caught. The
    // profile is loaded inline by ProfileManager's constructor
    // (profilemanager.cpp:131-366, called at maincontroller.cpp:130) while
    // this row arrives from the async read issued at the end of
    // setupRecipeConnections(), itself called partway through THIS
    // constructor — so the mismatch watcher has already had its turn and
    // returned early on an empty m_activeRecipe, and the shot stamp reads
    // activeRecipeId straight from settings without consulting the cache at
    // all. Three shots on 2026-07-28 were recorded against a recipe whose
    // profile they never ran, exactly here.
    //
    // Must run BEFORE m_activeRecipe is assigned and before setActiveRecipe
    // below, or a recipe about to be dropped first arms the dose ladder's
    // top rung with its own dose.
    //
    // This also fires for an external edit (composer/MCP/web) that
    // re-points the recipe at a different profile — the same divergence
    // from the other direction, so the same answer. Activation itself
    // cannot reach here: it returns via recipeActivationReady.
    if (Recipe::profileDiverged(recipe.value(QStringLiteral("profileTitle")).toString(),
                                m_profile.currentProfileTitle())) {
        m_effects.deactivateForProfileDivergence(
            recipeId, recipe.value(QStringLiteral("profileTitle")).toString(),
            m_profile.currentProfileTitle());
        return;
    }
    // The pitcher and vessel are ingredients too, so the same two windows
    // this block exists for — the startup restore, and an external edit
    // that re-points the recipe — have to reconcile them. Nothing else
    // can: the live watchers only see the USER change a selection, and
    // with the write-through gone the divergence no longer self-heals.
    {
        const bool pitcherGone = m_effects.steamPitcherDiverged(
            recipe.value(QStringLiteral("steamJson")).toString());
        const bool vesselGone = m_effects.waterVesselDiverged(
            recipe.value(QStringLiteral("hotWaterJson")).toString());
        if (pitcherGone || vesselGone) {
            m_effects.deactivateForIngredientDivergence(recipeId, pitcherGone);
            return;
        }
    }
    const qint64 resolvedBagId = m_activeRecipe.value(
        QStringLiteral("resolvedBagId"), m_dial.activeBagId()).toLongLong();
    m_activeRecipe = recipe;
    m_activeRecipe.insert(QStringLiteral("resolvedBagId"), resolvedBagId);
    if (m_effects.yieldRestorePending())
        m_effects.restoreYieldAnchorAfterProfileLoad();
    // Claim the dose rung from the row we just read (dose-source-precedence).
    // This is the ONLY path that arms it on the startup restore, and the only
    // one that re-arms it after an external edit (composer / MCP / web) —
    // activation goes through recipeActivationReady, not here. Without it the
    // rung reads empty for the whole session after a launch, and a profile
    // load would both take the dose and stamp its own value over the recipe's
    // stored doseG. Profile-less (tea) recipes claim it with 0, exactly as
    // activation does: their leaf dose is not a shot dose.
    m_dial.setActiveRecipe(
        static_cast<int>(recipeId),
        m_activeRecipe.value(QStringLiteral("profileTitle")).toString().trimmed().isEmpty()
            ? 0.0
            : m_activeRecipe.value(QStringLiteral("doseG")).toDouble());
    m_effects.emitActiveRecipeChanged();
    // The active recipe is one of the policy's inputs, so any refresh of the
    // cache — startup restore, composer/MCP/web edit — can change the
    // resolved target. Re-resolve unconditionally; the BLE layer dedups an
    // unchanged payload.
    m_effects.applySteamSettings();
    // Re-seed the brew overrides from the edited recipe, exactly as
    // re-activating it would (add-yield-ratio-anchor). An edit changes the
    // recipe's DESIGN, and the live setup must follow it — the grind push
    // below has always done this; yield/temperature were left behind,
    // stranding the value activation armed. Edit the yield to 50 and the
    // plan would read an amber "50.0 -> 36.0g" while the shot still
    // targeted the old 36: the recipe reading as an override of itself.
    //
    // "Clear the overrides" in the ladder's sense = back to the store's
    // own values, which is Clear's meaning in Brew Settings — NOT a bare
    // wipe, which would drop the brew to the profile rather than to the
    // edited recipe (targetWeight() resolves the session anchor; it never
    // re-reads the recipe). Only on an actual external edit — our own
    // dose/grind stamps return above without re-reading, so a
    // write-through can't bounce back and wipe the user's dialed tweak.
    // Profile-less (hot-water) recipes own no profile to override.
    if (refreshDial
        && !m_activeRecipe.value(QStringLiteral("profileTitle")).toString().trimmed().isEmpty())
        m_effects.applyRecipeBrewOverridesAndUpload(m_activeRecipe);

    // Mirror an edited grind/rpm back onto the live dial so the Shot Plan
    // refreshes without a re-activation (Flow-3 fix). Only on an actual
    // edit re-read; same semantics as applyActivatedRecipe's grind push:
    // grind-less drink types (tea) and an empty grind leave the dial
    // untouched. The cache is already updated above, so the resulting
    // dyeGrinderSettingChanged stamp hits stampActiveRecipe's equality
    // guard and does NOT loop back into another write.
    if (refreshDial
        && DrinkTypes::hasGrind(m_activeRecipe.value(QStringLiteral("drinkType")).toString())) {
        const QString grind = m_activeRecipe.value(QStringLiteral("grindPinned")).toString();
        if (!grind.isEmpty()) {
            m_dial.setGrinderSetting(grind);
            const qint64 rpm = m_activeRecipe.value(QStringLiteral("rpmPinned")).toLongLong();
            if (rpm > 0)
                m_dial.setGrinderRpm(static_cast<int>(rpm));
        }
    }
}

bool RecipeWiringController::shouldDeactivateOnBagChange(bool applyingRecipe) const {
    if (applyingRecipe || m_activeRecipe.isEmpty())
        return false;
    const bool hasBeanLink = m_activeRecipe.value("bagId").toLongLong() > 0
        || !m_activeRecipe.value("beanBaseId").toString().isEmpty()
        || !m_activeRecipe.value("roasterName").toString().isEmpty()
        || !m_activeRecipe.value("coffeeName").toString().isEmpty();
    if (!hasBeanLink)
        return false;
    return m_dial.activeBagId()
        != m_activeRecipe.value(QStringLiteral("resolvedBagId")).toLongLong();
}

bool RecipeWiringController::shouldDeactivateOnEquipmentChange(bool applyingRecipe) const {
    if (applyingRecipe || m_activeRecipe.isEmpty())
        return false;
    const qint64 recipeEq = m_activeRecipe.value("equipmentId").toLongLong();
    return recipeEq > 0 && m_dial.activeEquipmentId() != recipeEq;
}

bool RecipeWiringController::shouldDeactivateOnProfileChange(
    bool applyingRecipe, const QString& loadedProfileTitle) const {
    if (applyingRecipe)
        return false;  // activation seeds the brew itself
    return !m_activeRecipe.isEmpty()
        && Recipe::profileDiverged(m_activeRecipe.value("profileTitle").toString(),
                                   loadedProfileTitle);
}

void RecipeWiringController::stampActiveRecipe(bool applyingRecipe, const QString& field,
                                               const QVariant& value) {
    if (applyingRecipe || m_activeRecipe.isEmpty())
        return;
    const qint64 recipeId = m_dial.activeRecipeId();
    if (recipeId <= 0)
        return;
    if (m_activeRecipe.value(field) == value)
        return;  // echo of our own apply, or no actual change
    m_activeRecipe.insert(field, value);
    m_pendingRecipeSelfWrites++;
    m_store.requestUpdateRecipe(recipeId, {{field, value}});
}
