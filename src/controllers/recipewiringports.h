#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

// The seams RecipeWiringController talks through. MainController implements
// them over RecipeStorage / SettingsDye / SettingsBrew / ProfileManager;
// tst_recipewiringcontroller implements them with synchronous fakes.

// The live dial (SettingsDye). The ids are what the deactivate-on-swap
// decisions compare against; the setters are the edit refresh's dial push.
struct DialPort {
    virtual ~DialPort() = default;

    virtual qint64 activeRecipeId() const = 0;
    virtual qint64 activeBagId() const = 0;
    virtual qint64 activeEquipmentId() const = 0;

    virtual void setGrinderSetting(const QString& value) = 0;
    virtual void setGrinderRpm(int rpm) = 0;

    // Id + dose rung in one call (dose-source-precedence). -1 id clears.
    virtual void setActiveRecipe(int recipeId, double doseG) = 0;
};

// The recipe-store round trip. Results re-enter the controller as
// onRecipeUpdated / onRecipeReady.
struct RecipeStorePort {
    virtual ~RecipeStorePort() = default;

    virtual void requestUpdateRecipe(qint64 recipeId, const QVariantMap& fields) = 0;
    virtual void requestRecipe(qint64 recipeId) = 0;
};

// Read-only state, beyond the dial, that the decisions compare a recipe
// against. The pitcher/vessel checks resolve the live SettingsBrew preset.
struct LiveStatePort {
    virtual ~LiveStatePort() = default;

    virtual QString currentProfileTitle() const = 0;
    virtual bool steamPitcherDiverged(const QString& steamJson) const = 0;
    virtual bool waterVesselDiverged(const QString& hotWaterJson) const = 0;
    // A runtime profile load skipped its yield restore because the recipe row
    // had not arrived yet.
    virtual bool yieldRestorePending() const = 0;
};

// Actions the controller decides on but does not carry out itself.
struct SideEffectPort {
    virtual ~SideEffectPort() = default;

    virtual void emitActiveRecipeChanged() = 0;

    // Unlogged: the row vanished or was archived.
    virtual void deactivate() = 0;
    // Logged deactivations — this app is diagnosed from user-submitted logs,
    // and "my recipe deselected itself" must leave a trace.
    virtual void deactivateForProfileDivergence(qint64 recipeId,
                                                const QString& recipeProfileTitle,
                                                const QString& loadedProfileTitle) = 0;
    // pitcherGone picks the word in the log ("pitcher" vs "water vessel").
    virtual void deactivateForIngredientDivergence(qint64 recipeId, bool pitcherGone) = 0;

    virtual void applySteamSettings() = 0;
    // Re-seed the brew overrides from the recipe; upload the profile if one
    // was armed.
    virtual void applyRecipeBrewOverridesAndUpload(const QVariantMap& recipe) = 0;
    virtual void restoreYieldAnchorAfterProfileLoad() = 0;
};
