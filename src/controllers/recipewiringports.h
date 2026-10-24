#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

// Narrow, value-typed ports the RecipeWiringController talks to
// (extract-recipe-wiring-controller, design D3). MainController implements
// these over the real RecipeStorage / SettingsDye / ProfileManager; the unit
// test implements them with synchronous fakes. Keeping the controller behind
// these pure-virtual seams is what lets it be tested without constructing the
// MainController god-object or linking the app graph.
//
// Data crosses as the shapes already flowing through recipeReady
// (QVariantMap/QString/scalars), so no new domain types are needed.

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

// The recipe-store round trip. updateRecipe is the write-through stamp;
// requestRecipe is the edit re-read. Their async results re-enter the
// controller as onRecipeUpdated / onRecipeReady (in production via
// RecipeStorage's signals; in tests the fake invokes them on demand).
struct RecipeStorePort {
    virtual ~RecipeStorePort() = default;

    virtual void requestUpdateRecipe(qint64 recipeId, const QVariantMap& fields) = 0;
    virtual void requestRecipe(qint64 recipeId) = 0;
};

// The loaded profile's title, for the restored/refreshed-recipe divergence
// check. loadProfile stays inside the heavy activation body (SideEffectPort),
// so this port exposes only the read the controller's decision needs.
struct ProfilePort {
    virtual ~ProfilePort() = default;

    virtual QString currentProfileTitle() const = 0;
};

// Effects too coupled to the live machine/DB to live in the pure core
// (extract-recipe-wiring-controller, design D3/D4). The controller owns the
// decisions (the if-conditions, the flag, the order); these carry out the
// reach into ProfileManager / SettingsBrew / RecipeStorage statics and the
// BLE/steam/upload machinery. The two divergence helpers that need live
// pitcher/vessel preset resolution, plus the logged deactivations (the app is
// diagnosed from user-submitted logs — "my recipe deselected itself" must
// leave a trace), ride here.
struct SideEffectPort {
    virtual ~SideEffectPort() = default;

    // QVariantMap activeRecipe cache changed — fire activeRecipeChanged().
    virtual void emitActiveRecipeChanged() = 0;

    // Plain deactivate (empty/archived branch — no log line in the original).
    virtual void deactivate() = 0;
    // Logged deactivate: the restored/refreshed recipe names a profile other
    // than the loaded one (the 2026-07-28 three-shots-wrong incident).
    virtual void deactivateForProfileDivergence(qint64 recipeId,
                                                const QString& recipeProfileTitle,
                                                const QString& loadedProfileTitle) = 0;
    // Logged deactivate: the recipe names a pitcher or vessel that is not the
    // live selection. pitcherGone picks the word in the log ("pitcher" vs
    // "water vessel").
    virtual void deactivateForIngredientDivergence(qint64 recipeId, bool pitcherGone) = 0;

    // Re-resolve + push the steam/heater settings (dedup'd by the BLE layer).
    virtual void applySteamSettings() = 0;

    // Re-seed the session brew overrides from the edited recipe and, if an
    // override was armed, upload the current profile. Returns true iff an
    // override was armed (so the controller can mirror the inline
    // "applyRecipeBrewOverrides(...) && uploadCurrentProfile()" order).
    virtual bool applyRecipeBrewOverridesAndUpload(const QVariantMap& recipe) = 0;

    // A runtime profile load left no yield anchor pending a recipe row; arm it.
    virtual bool yieldRestorePending() const = 0;
    virtual void restoreYieldAnchorAfterProfileLoad() = 0;

    // The steam-pitcher / water-vessel divergence checks, which need live
    // SettingsBrew preset resolution the pure core must not drag in.
    virtual bool steamPitcherDiverged(const QString& steamJson) const = 0;
    virtual bool waterVesselDiverged(const QString& hotWaterJson) const = 0;
};
